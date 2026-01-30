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

#include "googlesql/compliance/compliance_label_extractor.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/common/function_utils.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using NameToFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>;
using PrefixSet =
    absl::btree_set<std::string, googlesql_base::CaseLess>;
using CaseInsensitiveMap =
    absl::btree_map<std::string, int, googlesql_base::CaseLess>;
using UnderscoreAndDotPrefixSets = std::pair<PrefixSet, PrefixSet>;

// Fetch all the functions from GetBuiltinFunctionsAndTypes with maximum
// language options in internal mode and populate the prefix group set.
//
// Any underscore prefix that shows up in 2 or more functions is considered a
// prefix group.
// Any underscore prefix that happens on exactly 1 function is not a prefix.
// All dot prefix group detected are considered valid for now.
//
// UnderscoreAndDotPrefixSets pair stores underscore prefix set as the first
// and dot prefix set as the element.
// TODO: There might be false positive detected. Add a skiplist
// if the outliers have side effect.
static UnderscoreAndDotPrefixSets*
GetFunctionPrefixSetsFromGoogleSQLFunctions() {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  absl::flat_hash_map<std::string, const Type*> types_ignored;
  absl::Status status = GetBuiltinFunctionsAndTypes(
      BuiltinFunctionOptions::AllReleasedFunctions(), type_factory, functions,
      types_ignored);
  GOOGLESQL_DCHECK_OK(status);

  CaseInsensitiveMap underscore_prefix_cnt;
  UnderscoreAndDotPrefixSets* prefix_sets = new UnderscoreAndDotPrefixSets;

  for (const auto& fn : functions) {
    // Ignore operators when building up valid prefix sets
    if (FunctionIsOperator(*fn.second)) {
      continue;
    }
    // Try finding dot in function name first
    std::string function_sql_name = fn.second->SQLName();
    absl::string_view dot_prefix =
        *(absl::StrSplit(function_sql_name, '.', absl::SkipEmpty()).begin());
    if (dot_prefix.length() != function_sql_name.length()) {
      prefix_sets->second.insert(std::string(dot_prefix));
    } else {
      // If function name does not contains dot, then try finding underscore.
      absl::string_view underscore_prefix =
          *(absl::StrSplit(function_sql_name, '_', absl::SkipEmpty()).begin());
      if (underscore_prefix.length() != function_sql_name.length()) {
        ++underscore_prefix_cnt[std::string(underscore_prefix)];
      }
    }
  }

  for (const auto& counter : underscore_prefix_cnt) {
    if (counter.second > 1) {
      prefix_sets->first.insert(counter.first);
    }
  }
  return prefix_sets;
}

static const PrefixSet& GetPrefixSet(PrefixGroup prefix_group) {
  static const UnderscoreAndDotPrefixSets* kPrefixSets =
      GetFunctionPrefixSetsFromGoogleSQLFunctions();
  return prefix_group == PrefixGroup::kUnderscore ? kPrefixSets->first
                                                  : kPrefixSets->second;
}

void ExtractPrefixGroupAndFunctionPrefix(absl::string_view function_sql_name,
                                         absl::string_view& function_prefix_out,
                                         PrefixGroup& prefix_group_out) {
  auto extract = [&](char separator, PrefixGroup group) {
    const PrefixSet& prefixes = GetPrefixSet(group);
    absl::string_view prefix =
        *(absl::StrSplit(function_sql_name, separator, absl::SkipEmpty())
              .begin());
    if (prefix.length() != function_sql_name.length() &&
        prefixes.contains(std::string(prefix))) {
      function_prefix_out = prefix;
      prefix_group_out = group;
      return true;
    }
    return false;
  };
  if (!extract('.', PrefixGroup::kDot)) {
    extract('_', PrefixGroup::kUnderscore);
  }
}

struct FunctionSignatureLabel {
  FunctionSignatureLabel(FunctionSignatureId signature_id,
                         std::string& sql_name, std::string prefix = "",
                         PrefixGroup prefix_group = PrefixGroup::kNone,
                         bool is_operator = false)
      : signature_id(signature_id),
        sql_name(sql_name),
        prefix(prefix),
        prefix_group(prefix_group),
        is_operator(is_operator) {}

  FunctionSignatureId signature_id;
  std::string sql_name;
  std::string prefix = "";
  PrefixGroup prefix_group = PrefixGroup::kNone;
  bool is_operator;
  bool include_safe_error_mode = false;
  bool include_default_error_mode = false;
};

struct AggregateFunctionModifierLabel {
  explicit AggregateFunctionModifierLabel(FunctionSignatureId signature_id)
      : signature_id(signature_id) {}

  FunctionSignatureId signature_id;
  bool include_distinct = false;
  bool include_ignore_nulls = false;
  bool include_respect_nulls = false;
  bool include_having_min = false;
  bool include_having_max = false;
  bool include_order_by = false;
  bool include_limit = false;
  bool include_mla = false;
  bool include_where = false;
  bool include_having = false;
};

struct WindowFunctionModifierLabel {
  explicit WindowFunctionModifierLabel(FunctionSignatureId signature_id)
      : signature_id(signature_id) {}

  FunctionSignatureId signature_id;
  bool include_partition_by = false;
  bool include_order_by = false;
  bool include_window_frame_rows = false;
  bool include_window_frame_range = false;
};

struct TypeCastLabel {
  TypeCastLabel(TypeKind from, TypeKind to) : from_type(from), to_type(to) {}

  template <typename H>
  friend H AbslHashValue(H h, const TypeCastLabel& s) {
    return H::combine(std::move(h), s.from_type, s.to_type);
  }

  bool operator==(const TypeCastLabel& rhs) const {
    return from_type == rhs.from_type && to_type == rhs.to_type;
  }

  std::string DebugString() {
    return absl::StrCat(TypeKind_Name(from_type), ":", TypeKind_Name(to_type));
  }

  TypeKind from_type;
  TypeKind to_type;
};

class ComplianceLabelSets {
 public:
  bool AddTypeCast(TypeKind from_type, TypeKind to_type) {
    TypeCastLabel type_cast = TypeCastLabel(from_type, to_type);
    return type_casts_set_.insert(type_cast).second;
  }

  bool AddStatementNodeKind(ResolvedNodeKind node_kind) {
    return stmt_node_kinds_set_.insert(node_kind).second;
  }

  bool AddExpressionNodeKind(ResolvedNodeKind node_kind) {
    return expr_node_kinds_set_.insert(node_kind).second;
  }

  bool AddScanNodeKind(ResolvedNodeKind node_kind) {
    return scan_node_kinds_set_.insert(node_kind).second;
  }

  bool AddTypeKind(TypeKind type_kind) {
    return type_kinds_set_.insert(type_kind).second;
  }

  bool AddSetOperationModeLabel(
      ResolvedSetOperationScanEnums::SetOperationType op_type,
      ResolvedSetOperationScanEnums::SetOperationColumnMatchMode match_mode,
      ResolvedSetOperationScanEnums::SetOperationColumnPropagationMode
          propagation_mode) {
    return set_operation_modes_
        .insert(std::make_tuple(op_type, match_mode, propagation_mode))
        .second;
  }

  bool AddFunctionSignatureLabel(
      FunctionSignatureId signature_id, const Function* function,
      ResolvedFunctionCallBaseEnums_ErrorMode error_mode) {
    bool include_safe_error_mode =
        function->SupportsSafeErrorMode() &&
        (error_mode == ResolvedFunctionCallBaseEnums::SAFE_ERROR_MODE);
    bool include_default_error_mode =
        function->SupportsSafeErrorMode() &&
        (error_mode == ResolvedFunctionCallBaseEnums::DEFAULT_ERROR_MODE);
    if (function_signatures_.contains(signature_id)) {
      if (auto found = function_signatures_.find(signature_id);
          found != function_signatures_.end()) {
        if (include_safe_error_mode) {
          found->second.include_safe_error_mode = true;
        }
        if (include_default_error_mode) {
          found->second.include_default_error_mode = true;
        }
      }
    }
    std::string sql_name = function->SQLName();
    PrefixGroup prefix_group = PrefixGroup::kNone;
    absl::string_view prefix = "";
    bool is_operator = FunctionIsOperator(*function);
    if (!is_operator) {
      ExtractPrefixGroupAndFunctionPrefix(sql_name, prefix, prefix_group);
    }
    FunctionSignatureLabel signature_label = FunctionSignatureLabel(
        signature_id, sql_name, std::string(prefix), prefix_group, is_operator);
    signature_label.include_safe_error_mode = include_safe_error_mode;
    signature_label.include_default_error_mode = include_default_error_mode;
    return function_signatures_.try_emplace(signature_id, signature_label)
        .second;
  }

  bool AddAggregateFunctionModifierLabel(
      FunctionSignatureId signature_id,
      const ResolvedAggregateFunctionCall* node, const Function* function) {
    if (!aggregate_function_modifiers_.contains(signature_id)) {
      aggregate_function_modifiers_.try_emplace(
          signature_id, AggregateFunctionModifierLabel(signature_id));
    }

    auto found = aggregate_function_modifiers_.find(signature_id);
    ABSL_DCHECK(found != aggregate_function_modifiers_.end());
    if (node->distinct() == true) {
      found->second.include_distinct = true;
    }
    if (function->SupportsNullHandlingModifier() &&
        node->null_handling_modifier() ==
            ResolvedNonScalarFunctionCallBaseEnums::IGNORE_NULLS) {
      found->second.include_ignore_nulls = true;
    }
    if (function->SupportsNullHandlingModifier() &&
        node->null_handling_modifier() ==
            ResolvedNonScalarFunctionCallBaseEnums::RESPECT_NULLS) {
      found->second.include_respect_nulls = true;
    }
    if (function->SupportsHavingModifier() &&
        node->having_modifier() != nullptr) {
      if (node->having_modifier()->kind() ==
          ResolvedAggregateHavingModifierEnums::MIN) {
        found->second.include_having_min = true;
      }
      if (node->having_modifier()->kind() ==
          ResolvedAggregateHavingModifierEnums::MAX) {
        found->second.include_having_max = true;
      }
    }
    if (node->order_by_item_list_size() > 0) {
      found->second.include_order_by = true;
    }
    if (node->limit() != nullptr) {
      found->second.include_limit = true;
    }
    if (!node->group_by_list().empty()) {
      found->second.include_mla = true;
    }
    if (node->where_expr() != nullptr) {
      found->second.include_where = true;
    }
    if (node->having_expr() != nullptr) {
      found->second.include_having = true;
    }

    return true;
  }

  bool AddWindowFunctionModifierLabel(FunctionSignatureId signature_id,
                                      const ResolvedAnalyticFunctionCall* node,
                                      const Function* function,
                                      bool include_partition_by,
                                      bool include_order_by) {
    if (!window_function_modifiers_.contains(signature_id)) {
      window_function_modifiers_.try_emplace(
          signature_id, WindowFunctionModifierLabel(signature_id));
    }
    auto found = window_function_modifiers_.find(signature_id);
    ABSL_DCHECK(found != window_function_modifiers_.end());
    if (include_partition_by) {
      found->second.include_partition_by = true;
    }
    if (include_order_by) {
      found->second.include_order_by = true;
    }
    if (node->window_frame() != nullptr) {
      if (node->window_frame()->frame_unit() ==
          ResolvedWindowFrameEnums::ROWS) {
        found->second.include_window_frame_rows = true;
      }
      if (node->window_frame()->frame_unit() ==
          ResolvedWindowFrameEnums::RANGE) {
        found->second.include_window_frame_range = true;
      }
    }

    return true;
  }

  bool AddOther(absl::string_view label) {
    other_features_.emplace(label);
    return true;
  }

  void GenerateLabelStrings(absl::btree_set<std::string>& output) {
    // Generate node kind label in a format of
    // "ResolvedNodeKind:<nonleaf_node_group>:<leaf_node_kind_name>"
    for (ResolvedNodeKind node_kind : stmt_node_kinds_set_) {
      output.insert(absl::StrCat("ResolvedNodeKind:Statement:",
                                 ResolvedNodeKind_Name(node_kind)));
    }
    for (ResolvedNodeKind node_kind : expr_node_kinds_set_) {
      output.insert(absl::StrCat("ResolvedNodeKind:Expression:",
                                 ResolvedNodeKind_Name(node_kind)));
    }
    for (ResolvedNodeKind node_kind : scan_node_kinds_set_) {
      output.insert(absl::StrCat("ResolvedNodeKind:Scan:",
                                 ResolvedNodeKind_Name(node_kind)));
    }

    // Generate type kind label in a format of "TypeKind:<type_kind_name>"
    for (TypeKind type_kind : type_kinds_set_) {
      output.insert(absl::StrCat("TypeKind:", TypeKind_Name(type_kind)));
    }

    // Generate type cast label for CAST function
    for (TypeCastLabel type_cast : type_casts_set_) {
      output.insert(absl::StrCat("TypeCast:", type_cast.DebugString()));
    }

    // Generate function signature label for each signature.
    // Function label is in the following formats:
    //  "FunctionErrorMode:<prefix>:<sql_name>:<signature_id_name>:<error_mode>"
    //  "FunctionSignature:<prefix>:<sql_name>:<signature_id_name>"
    //  "FunctionName:<prefix>:<sql_name>"
    //  "FunctionFamily:<prefix>"
    // Operator label is in the following formats:
    //  "OperatorSignature:<sql_name>:<signature_id_name>"
    for (const auto& fn_signature : function_signatures_) {
      const FunctionSignatureLabel& signature_label = fn_signature.second;
      if (signature_label.is_operator) {
        output.insert(absl::StrCat("OperatorName:", signature_label.sql_name));
        output.insert(absl::StrCat(
            "OperatorSignature:", signature_label.sql_name, ":",
            FunctionSignatureId_Name(signature_label.signature_id)));
      } else {
        output.insert(absl::StrCat(
            "FunctionSignature:", signature_label.prefix, ":",
            signature_label.sql_name, ":",
            FunctionSignatureId_Name(signature_label.signature_id)));
        if (signature_label.include_default_error_mode) {
          output.insert(absl::StrCat(
              "FunctionErrorMode:", signature_label.prefix, ":",
              signature_label.sql_name, ":",
              FunctionSignatureId_Name(signature_label.signature_id), ":",
              "DEFAULT_ERROR_MODE"));
        }
        if (signature_label.include_safe_error_mode) {
          output.insert(absl::StrCat(
              "FunctionErrorMode:", signature_label.prefix, ":",
              signature_label.sql_name, ":",
              FunctionSignatureId_Name(signature_label.signature_id), ":",
              "SAFE_ERROR_MODE"));
        }
        output.insert(absl::StrCat("FunctionName:", signature_label.prefix, ":",
                                   signature_label.sql_name));
        // Exclude prefix label for functions with empty prefix group.
        if (signature_label.prefix_group != PrefixGroup::kNone) {
          output.insert(
              absl::StrCat("FunctionFamily:", signature_label.prefix));
        }
      }
      if (auto found =
              aggregate_function_modifiers_.find(signature_label.signature_id);
          found != aggregate_function_modifiers_.end()) {
        output.insert(absl::StrCat(
            "AggregateFunctionSignature:", signature_label.prefix, ":",
            signature_label.sql_name, ":",
            FunctionSignatureId_Name(signature_label.signature_id)));
        absl::flat_hash_set<absl::string_view> supported_modifiers = {};
        if (found->second.include_distinct) {
          supported_modifiers.insert("DISTINCT");
        }
        if (found->second.include_ignore_nulls) {
          supported_modifiers.insert("IGNORE_NULLS");
        }
        if (found->second.include_respect_nulls) {
          supported_modifiers.insert("RESPECT_NULLS");
        }
        if (found->second.include_having_min) {
          supported_modifiers.insert("HAVING_MIN");
        }
        if (found->second.include_having_max) {
          supported_modifiers.insert("HAVING_MAX");
        }
        if (found->second.include_order_by) {
          supported_modifiers.insert("ORDER_BY");
        }
        if (found->second.include_limit) {
          supported_modifiers.insert("LIMIT");
        }
        if (found->second.include_mla) {
          supported_modifiers.insert("GROUP_BY");
        }
        if (found->second.include_where) {
          supported_modifiers.insert("WHERE");
        }
        if (found->second.include_having) {
          supported_modifiers.insert("HAVING");
        }
        for (const absl::string_view& modifier : supported_modifiers) {
          output.insert(absl::StrCat(
              "AggregateFunctionModifier:", signature_label.prefix, ":",
              signature_label.sql_name, ":",
              FunctionSignatureId_Name(signature_label.signature_id), ":",
              modifier));
        }
      }
      if (auto found =
              window_function_modifiers_.find(signature_label.signature_id);
          found != window_function_modifiers_.end()) {
        output.insert(absl::StrCat(
            "WindowFunctionSignature:", signature_label.prefix, ":",
            signature_label.sql_name, ":",
            FunctionSignatureId_Name(signature_label.signature_id)));
        absl::flat_hash_set<absl::string_view> supported_modifiers = {};
        if (found->second.include_partition_by) {
          supported_modifiers.insert("PARTITION_BY");
        }
        if (found->second.include_order_by) {
          supported_modifiers.insert("ORDER_BY");
        }
        if (found->second.include_window_frame_rows) {
          supported_modifiers.insert("WINDOW_FRAME_UNIT_ROWS");
        }
        if (found->second.include_window_frame_range) {
          supported_modifiers.insert("WINDOW_FRAME_UNIT_RANGE");
        }
        for (const absl::string_view& modifier : supported_modifiers) {
          output.insert(absl::StrCat(
              "WindowFunctionModifier:", signature_label.prefix, ":",
              signature_label.sql_name, ":",
              FunctionSignatureId_Name(signature_label.signature_id), ":",
              modifier));
        }
      }
    }

    // Generate labels for set operations in the following format:
    // "SetOperation:<OperationType>:<MatchMode>:<PropagationMode>"
    for (const auto [op_type, match_mode, propagation_mode] :
         set_operation_modes_) {
      output.insert(absl::StrCat(
          "SetOperation:",
          ResolvedSetOperationScanEnums::SetOperationType_Name(op_type), ":",
          ResolvedSetOperationScanEnums::SetOperationColumnMatchMode_Name(
              match_mode),
          ":",
          ResolvedSetOperationScanEnums::SetOperationColumnPropagationMode_Name(
              propagation_mode)));
    }

    for (const std::string& other_feature : other_features_) {
      output.insert(absl::StrCat("OtherSqlFeature:", other_feature));
    }
  }

 private:
  absl::flat_hash_set<ResolvedNodeKind> stmt_node_kinds_set_;
  absl::flat_hash_set<ResolvedNodeKind> expr_node_kinds_set_;
  absl::flat_hash_set<ResolvedNodeKind> scan_node_kinds_set_;
  absl::flat_hash_set<TypeCastLabel> type_casts_set_;
  absl::flat_hash_set<TypeKind> type_kinds_set_;
  absl::flat_hash_map<FunctionSignatureId, FunctionSignatureLabel>
      function_signatures_;
  absl::flat_hash_map<FunctionSignatureId, AggregateFunctionModifierLabel>
      aggregate_function_modifiers_;
  absl::flat_hash_map<FunctionSignatureId, WindowFunctionModifierLabel>
      window_function_modifiers_;
  absl::flat_hash_set<std::tuple<
      ResolvedSetOperationScanEnums::SetOperationType,
      ResolvedSetOperationScanEnums::SetOperationColumnMatchMode,
      ResolvedSetOperationScanEnums::SetOperationColumnPropagationMode>>
      set_operation_modes_;
  absl::flat_hash_set<std::string> other_features_;
};

class ComplianceLabelExtractor : public ResolvedASTVisitor {
 public:
  explicit ComplianceLabelExtractor(ComplianceLabelSets* compliance_labels)
      : compliance_labels_(*compliance_labels) {}

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    if (node->IsScan()) {
      compliance_labels_.AddScanNodeKind(node->node_kind());
      GOOGLESQL_VLOG(5) << "Inserted scan node kind: "
              << ResolvedNodeKind_Name(node->node_kind()) << ".\n";
    } else if (node->IsExpression()) {
      compliance_labels_.AddExpressionNodeKind(node->node_kind());
      GOOGLESQL_VLOG(5) << "Inserted expression node kind: "
              << ResolvedNodeKind_Name(node->node_kind()) << ".\n";

      auto* expr_node = node->GetAs<ResolvedExpr>();
      GOOGLESQL_RET_CHECK_NE(expr_node, nullptr);
      compliance_labels_.AddTypeKind(expr_node->type()->kind());
      GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedExpr): "
              << TypeKind_Name(expr_node->type()->kind()) << ".\n";
    } else if (node->IsStatement()) {
      compliance_labels_.AddStatementNodeKind(node->node_kind());
      GOOGLESQL_VLOG(5) << "Inserted statement node kind: "
              << ResolvedNodeKind_Name(node->node_kind()) << ".\n";
    }
    return ResolvedASTVisitor::DefaultVisit(node);
  }

  template <typename T>
  absl::Status ExtractSignatureIdAndDefaultVisit(const T* node) {
    auto* function = node->function();
    GOOGLESQL_RET_CHECK_NE(function, nullptr);
    if (function->IsGoogleSQLBuiltin()) {
      compliance_labels_.AddFunctionSignatureLabel(
          static_cast<FunctionSignatureId>(node->signature().context_id()),
          function, node->error_mode());
      GOOGLESQL_VLOG(5) << "Inserted function id: "
              << FunctionSignatureId_Name(static_cast<FunctionSignatureId>(
                     node->signature().context_id()))
              << ".\n";
    }
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override {
    return ExtractSignatureIdAndDefaultVisit<ResolvedFunctionCall>(node);
  }

  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    auto* function = node->function();
    GOOGLESQL_RET_CHECK_NE(function, nullptr);
    if (function->IsGoogleSQLBuiltin()) {
      compliance_labels_.AddAggregateFunctionModifierLabel(
          static_cast<FunctionSignatureId>(node->signature().context_id()),
          node, function);
    }
    return ExtractSignatureIdAndDefaultVisit<ResolvedAggregateFunctionCall>(
        node);
  }

  absl::Status VisitResolvedAnalyticFunctionCall(
      const ResolvedAnalyticFunctionCall* node) override {
    return ExtractSignatureIdAndDefaultVisit<ResolvedAnalyticFunctionCall>(
        node);
  }

  absl::Status VisitResolvedAnalyticFunctionGroup(
      const ResolvedAnalyticFunctionGroup* node) override {
    for (const std::unique_ptr<const ResolvedComputedColumnBase>& computed_col :
         node->analytic_function_list()) {
      if (computed_col == nullptr) {
        return DefaultVisit(node);
      }
      if (computed_col->expr()->Is<ResolvedAnalyticFunctionCall>()) {
        const ResolvedAnalyticFunctionCall* analytic_function_node =
            computed_col->expr()->GetAs<ResolvedAnalyticFunctionCall>();
        auto* function = analytic_function_node->function();
        GOOGLESQL_RET_CHECK_NE(function, nullptr);
        if (function->IsGoogleSQLBuiltin()) {
          bool include_partition_by = node->partition_by() != nullptr;
          bool include_order_by = node->order_by() != nullptr;
          compliance_labels_.AddWindowFunctionModifierLabel(
              static_cast<FunctionSignatureId>(
                  analytic_function_node->signature().context_id()),
              analytic_function_node, function, include_partition_by,
              include_order_by);
        }
      }
    }
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedCast(const ResolvedCast* node) override {
    auto* expr = node->expr();
    GOOGLESQL_RET_CHECK_NE(expr, nullptr);
    compliance_labels_.AddTypeCast(expr->type()->kind(), node->type()->kind());
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedExtendedCastElement(
      const ResolvedExtendedCastElement* node) override {
    compliance_labels_.AddTypeKind(node->from_type()->kind());
    compliance_labels_.AddTypeKind(node->to_type()->kind());
    GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedExtendedCastElement): "
            << TypeKind_Name(node->from_type()->kind()) << ", "
            << TypeKind_Name(node->to_type()->kind()) << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedColumnDefinition(
      const ResolvedColumnDefinition* node) override {
    compliance_labels_.AddTypeKind(node->type()->kind());
    GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedColumnDefinition): "
            << TypeKind_Name(node->type()->kind()) << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedArgumentDef(
      const ResolvedArgumentDef* node) override {
    compliance_labels_.AddTypeKind(node->type()->kind());
    GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedArgumentDef): "
            << TypeKind_Name(node->type()->kind()) << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedAlterColumnSetDataTypeAction(
      const ResolvedAlterColumnSetDataTypeAction* node) override {
    compliance_labels_.AddTypeKind(node->updated_type()->kind());
    GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedAlterColumnSetDataTypeAction): "
            << TypeKind_Name(node->updated_type()->kind()) << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedCreateFunctionStmt(
      const ResolvedCreateFunctionStmt* node) override {
    compliance_labels_.AddTypeKind(node->return_type()->kind());
    GOOGLESQL_VLOG(5) << "Inserted type kind (ResolvedCreateFunctionStmt): "
            << TypeKind_Name(node->return_type()->kind()) << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedSetOperationScan(
      const ResolvedSetOperationScan* node) override {
    compliance_labels_.AddSetOperationModeLabel(
        node->op_type(), node->column_match_mode(),
        node->column_propagation_mode());
    GOOGLESQL_VLOG(5) << "Inserted set operation label: "
            << ResolvedSetOperationScanEnums::SetOperationType_Name(
                   node->op_type())
            << ":"
            << ResolvedSetOperationScanEnums::SetOperationColumnMatchMode_Name(
                   node->column_match_mode())
            << ":"
            << ResolvedSetOperationScanEnums::
                   SetOperationColumnPropagationMode_Name(
                       node->column_propagation_mode())
            << ".\n";
    return DefaultVisit(node);
  }

  absl::Status VisitResolvedJoinScan(const ResolvedJoinScan* node) override {
    if (node->is_lateral()) {
      compliance_labels_.AddOther("LATERAL_JOIN");
    }
    return DefaultVisit(node);
  }

 private:
  ComplianceLabelSets& compliance_labels_;
};

absl::Status ExtractComplianceLabels(const ResolvedNode* node,
                                     absl::btree_set<std::string>& labels_out) {
  // Create resolved AST visitor and traverse the tree to collect labels against
  // each node.
  ComplianceLabelSets compliance_labels = ComplianceLabelSets();
  ComplianceLabelExtractor visitor =
      ComplianceLabelExtractor(&compliance_labels);
  GOOGLESQL_RETURN_IF_ERROR(node->Accept(&visitor));
  compliance_labels.GenerateLabelStrings(labels_out);
  return absl::OkStatus();
}

}  // namespace googlesql
