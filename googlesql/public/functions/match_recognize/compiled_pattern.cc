//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/public/functions/match_recognize/compiled_pattern.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/common/match_recognize/nfa_builder.h"
#include "googlesql/common/match_recognize/nfa_match_partition.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

static bool PatternReferencesQueryParameter(
    const ResolvedMatchRecognizeScan& scan) {
  std::vector<const ResolvedNode*> resolved_params;
  scan.pattern()->GetDescendantsWithKinds({RESOLVED_PARAMETER},
                                          &resolved_params);
  return !resolved_params.empty();
}

// Returns true if we are in longest match mode, false if not, or std::nullopt
// if we can't determine this at compile time, due to the option value being
// specified by a late-bound query parameter.
static absl::StatusOr<std::optional<bool>> IsLongestMatchMode(
    const ResolvedMatchRecognizeScan& scan,
    std::function<absl::StatusOr<Value>(std::variant<int, absl::string_view>)>
        parameter_evaluator) {
  for (const auto& option : scan.option_list()) {
    if (absl::AsciiStrToLower(option->name()) != "use_longest_match") {
      continue;
    }
    GOOGLESQL_RET_CHECK(option->value()->type()->IsBool());
    if (option->value()->Is<ResolvedLiteral>()) {
      const Value& option_value =
          option->value()->GetAs<ResolvedLiteral>()->value();
      // The resolver should not have allowed a null value to make it here,
      // but check anyway to be safe.
      GOOGLESQL_RET_CHECK(!option_value.is_null());
      return option_value.bool_value();
    } else if (option->value()->Is<ResolvedParameter>()) {
      if (parameter_evaluator == nullptr) {
        // Parameter values are not available yet; need to defer compilation.
        return std::nullopt;
      } else {
        std::variant<int, absl::string_view> param;
        const ResolvedParameter& resolved_param =
            *option->value()->GetAs<ResolvedParameter>();
        if (resolved_param.name().empty()) {
          param = resolved_param.position();
        } else {
          param = resolved_param.name();
        }
        GOOGLESQL_ASSIGN_OR_RETURN(const Value& option_value, parameter_evaluator(param));
        if (option_value.is_null()) {
          return absl::OutOfRangeError(
              "NULL value for use_longest_match option not allowed");
        }
        return option_value.bool_value();
      }
    }
  }
  return false;
}

// CompiledPattern implementation used to represent the case where pattern
// compilation is deferred because the pattern requires query parameter values,
// but the engine does not support providing query parameter values at
// compile-time. When CreateMatchPartition() is called, we must see query
// parameter values then.
class StateMachineDeferredCompile : public CompiledPattern {
 public:
  explicit StateMachineDeferredCompile(
      std::unique_ptr<const ResolvedMatchRecognizeScan> scan,
      const NFAMatchPartitionOptions match_options)
      : scan_(std::move(scan)), match_options_(match_options) {}

  absl::StatusOr<std::unique_ptr<MatchPartition>> CreateMatchPartition(
      const MatchOptions& options) const override {
    if (options.parameter_evaluator == nullptr) {
      return absl::InvalidArgumentError(
          "Pattern requires query parameters, but no parameter evaluator "
          "supplied");
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const NFA> nfa,
        NFABuilder::BuildNFAForPattern(*scan_, options.parameter_evaluator));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<CompiledNFA> compiled_nfa,
                     CompiledNFA::Create(*nfa));
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<bool> longest_match_mode,
                     IsLongestMatchMode(*scan_, options.parameter_evaluator));
    GOOGLESQL_RET_CHECK(longest_match_mode != std::nullopt);
    NFAMatchPartitionOptions match_options(match_options_);
    match_options.longest_match_mode = *longest_match_mode;
    return std::make_unique<NFAMatchPartition>(std::move(compiled_nfa),
                                               match_options);
  }

  CompiledPatternProto Serialize() const override {
    // TODO: Implement serialization.
    return CompiledPatternProto();
  }

  std::string DebugString() const override {
    return absl::StrCat("StateMachineDeferredCompile:\n",
                        match_options_.DebugString());
  }

 private:
  std::unique_ptr<const ResolvedMatchRecognizeScan> scan_;
  const NFAMatchPartitionOptions match_options_;
};

static absl::StatusOr<bool> IsOverlappingMode(
    const ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode& mode) {
  switch (mode) {
    case ResolvedMatchRecognizeScanEnums::NEXT_ROW:
      return true;
    case ResolvedMatchRecognizeScanEnums::END_OF_MATCH:
      return false;
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Unknown AfterMatchSkipMode: ",
          ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode_Name(mode)));
  }
}

class StateMachineCompiledPattern : public CompiledPattern {
 public:
  static absl::StatusOr<std::unique_ptr<const CompiledPattern>> Create(
      const ResolvedMatchRecognizeScan& scan, const PatternOptions& options) {
    NFAMatchPartitionOptions match_options;
    GOOGLESQL_ASSIGN_OR_RETURN(match_options.overlapping_mode,
                     IsOverlappingMode(scan.after_match_skip_mode()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<bool> longest_match_mode,
                     IsLongestMatchMode(scan, options.parameter_evaluator));
    if ((longest_match_mode == std::nullopt) ||
        (options.parameter_evaluator == nullptr &&
         PatternReferencesQueryParameter(scan))) {
      // Query parameter values are required to compile the pattern, but we
      // don't have them at compilation time. Defer real compilation
      // until CreateMatchPartition() is called.
      //
      // TODO: Consider only cloning the pattern portion of the
      // scan, rather than the entire scan and/or compiling as much of the
      // pattern up front as possible.
      ResolvedASTDeepCopyVisitor visitor;
      GOOGLESQL_RETURN_IF_ERROR(scan.Accept(&visitor));
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedMatchRecognizeScan> scan_copy,
          visitor.ConsumeRootNode<ResolvedMatchRecognizeScan>());

      return std::make_unique<const StateMachineDeferredCompile>(
          std::move(scan_copy), match_options);
    }
    match_options.longest_match_mode = *longest_match_mode;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const NFA> nfa,
        NFABuilder::BuildNFAForPattern(scan, options.parameter_evaluator));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<CompiledNFA> compiled_nfa,
                     CompiledNFA::Create(*nfa));
    return std::make_unique<StateMachineCompiledPattern>(
        std::move(compiled_nfa), match_options);
  }

  explicit StateMachineCompiledPattern(
      std::unique_ptr<const CompiledNFA> nfa,
      const NFAMatchPartitionOptions& match_options)
      : nfa_(std::move(nfa)), match_options_(match_options) {}

  static absl::StatusOr<std::unique_ptr<const StateMachineCompiledPattern>>
  Deserialize(const StateMachineProto& proto) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const CompiledNFA> nfa,
                     CompiledNFA::Deserialize(proto.nfa()));
    NFAMatchPartitionOptions options;
    GOOGLESQL_ASSIGN_OR_RETURN(options.overlapping_mode,
                     IsOverlappingMode(proto.after_match_skip_mode()));
    options.longest_match_mode = proto.longest_match_mode();

    return std::make_unique<StateMachineCompiledPattern>(std::move(nfa),
                                                         options);
  }

  absl::StatusOr<std::unique_ptr<MatchPartition>> CreateMatchPartition(
      const MatchOptions& options) const override {
    return std::make_unique<NFAMatchPartition>(nfa_, match_options_);
  }

  CompiledPatternProto Serialize() const override {
    CompiledPatternProto proto;
    proto.mutable_state_machine()->set_after_match_skip_mode(
        match_options_.overlapping_mode
            ? ResolvedMatchRecognizeScanEnums::NEXT_ROW
            : ResolvedMatchRecognizeScanEnums::END_OF_MATCH);
    proto.mutable_state_machine()->set_longest_match_mode(
        match_options_.longest_match_mode);
    *proto.mutable_state_machine()->mutable_nfa() = nfa_->Serialize();
    return proto;
  }

  std::string DebugString() const override {
    return absl::StrCat("StateMachineCompiledPattern:\n", nfa_->DebugString(),
                        match_options_.DebugString());
  }

 private:
  std::shared_ptr<const CompiledNFA> nfa_;
  const NFAMatchPartitionOptions match_options_;
};

absl::StatusOr<std::unique_ptr<const CompiledPattern>> CompiledPattern::Create(
    const ResolvedMatchRecognizeScan& scan, const PatternOptions& options) {
  switch (options.algorithm) {
    case kStateMachineAlgorithm:
      return StateMachineCompiledPattern::Create(scan, options);
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Unsupported pattern matching algorithm: ", options.algorithm));
  }
}

absl::StatusOr<std::unique_ptr<const CompiledPattern>>
CompiledPattern::Deserialize(const CompiledPatternProto& serialized) {
  if (serialized.has_state_machine()) {
    return StateMachineCompiledPattern::Deserialize(serialized.state_machine());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid serialized pattern:\n", serialized));
}

}  // namespace googlesql::functions::match_recognize
