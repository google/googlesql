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

#include "googlesql/reference_impl/operator.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

constexpr char AlgebraNode::kIndentFork[];
constexpr char AlgebraNode::kIndentBar[];
constexpr char AlgebraNode::kIndentSpace[];

// -------------------------------------------------------
// AlgebraArg
// -------------------------------------------------------

AlgebraArg::AlgebraArg(const VariableId& variable,
                       std::unique_ptr<AlgebraNode> node)
    : variable_(variable), node_(std::move(node)) {}

AlgebraArg::~AlgebraArg() = default;

bool AlgebraArg::has_value_expr() const {
  return node() ? node()->IsValueExpr() : false;
}

const ValueExpr* AlgebraArg::value_expr() const {
  return node() ? node()->AsValueExpr() : nullptr;
}

ValueExpr* AlgebraArg::mutable_value_expr() {
  return node() ? mutable_node()->AsMutableValueExpr() : nullptr;
}

std::unique_ptr<ValueExpr> AlgebraArg::release_value_expr() {
  if (node() == nullptr || node()->AsValueExpr() == nullptr) {
    return nullptr;
  }
  return absl::WrapUnique(node_.release()->AsMutableValueExpr());
}

const RelationalOp* AlgebraArg::relational_op() const {
  return node() ? node()->AsRelationalOp() : nullptr;
}

RelationalOp* AlgebraArg::mutable_relational_op() {
  return node() ? mutable_node()->AsMutableRelationalOp() : nullptr;
}

bool AlgebraArg::has_inline_lambda_expr() const {
  return node() ? node()->IsInlineLambdaExpr() : false;
}

const InlineLambdaExpr* AlgebraArg::inline_lambda_expr() const {
  return node() ? node()->AsInlineLambdaExpr() : nullptr;
}

InlineLambdaExpr* AlgebraArg::mutable_inline_lambda_expr() {
  return node() ? mutable_node()->AsMutableInlineLambdaExpr() : nullptr;
}

std::string AlgebraArg::DebugString(bool verbose) const {
  return this->DebugInternal("\n", verbose);
}

std::string AlgebraArg::DebugInternal(const std::string& indent,
                                      bool verbose) const {
  std::string result;
  if (has_variable()) {
    absl::StrAppend(&result, "$", variable().ToString());
    if (has_node()) {
      if (verbose) {
        absl::StrAppend(&result, "[",
                        node()->AsValueExpr()->output_type()->DebugString(),
                        "]");
      }
      absl::StrAppend(&result, " := ");
    }
  }
  if (has_node()) {
    absl::StrAppend(&result, node()->DebugInternal(indent, verbose));
  }
  return result;
}

// -------------------------------------------------------
// CppValueArg
// -------------------------------------------------------
CppValueArg::CppValueArg(const VariableId variable,
                         absl::string_view value_debug_string)
    : AlgebraArg(variable, nullptr), value_debug_string_(value_debug_string) {}

std::string CppValueArg::DebugInternal(const std::string& indent,
                                       bool verbose) const {
  std::string result;
  if (has_variable()) {
    absl::StrAppend(&result, "$", variable().ToString());
    if (!value_debug_string_.empty()) {
      absl::StrAppend(&result, " := ");
    }
  }
  if (value_debug_string_.empty()) {
    absl::StrAppend(&result, "CppValue {", value_debug_string_, "}");
  }
  return result;
}

// -------------------------------------------------------
// ExprArg
// -------------------------------------------------------

ExprArg::ExprArg(const VariableId& variable, std::unique_ptr<ValueExpr> expr)
    : AlgebraArg(variable, std::move(expr)) {
  type_ = node()->AsValueExpr()->output_type();
}

ExprArg::ExprArg(const VariableId& variable, const Type* type)
    : AlgebraArg(variable, nullptr) {
  type_ = type;
}

ExprArg::ExprArg(std::unique_ptr<ValueExpr> expr)
    : AlgebraArg(VariableId(), std::move(expr)) {
  type_ = node()->AsValueExpr()->output_type();
}

// -------------------------------------------------------
// InlineLambdaArg
// -------------------------------------------------------
InlineLambdaArg::InlineLambdaArg(std::unique_ptr<InlineLambdaExpr> lambda)
    : AlgebraArg(VariableId(), std::move(lambda)) {}

// -------------------------------------------------------
// AlgebraNode
// -------------------------------------------------------

AlgebraNode::~AlgebraNode() {
  googlesql_base::STLDeleteElements(&args_);
}

bool AlgebraNode::IsValueExpr() const { return false; }

const ValueExpr* AlgebraNode::AsValueExpr() const {
  ABSL_LOG(ERROR) << "Not a ValueExpr";
  return nullptr;
}

ValueExpr* AlgebraNode::AsMutableValueExpr() {
  ABSL_LOG(ERROR) << "Not a ValueExpr";
  return nullptr;
}

const RelationalOp* AlgebraNode::AsRelationalOp() const {
  ABSL_LOG(ERROR) << "Not a RelationalOp";
  return nullptr;
}

RelationalOp* AlgebraNode::AsMutableRelationalOp() {
  ABSL_LOG(ERROR) << "Not a RelationalOp";
  return nullptr;
}

bool AlgebraNode::IsInlineLambdaExpr() const { return false; }

const InlineLambdaExpr* AlgebraNode::AsInlineLambdaExpr() const {
  ABSL_LOG(ERROR) << "Not an InlineLambdaExpr";
  return nullptr;
}

InlineLambdaExpr* AlgebraNode::AsMutableInlineLambdaExpr() {
  ABSL_LOG(ERROR) << "Not an InlineLambdaExpr";
  return nullptr;
}

void AlgebraNode::SetArg(int kind, std::unique_ptr<AlgebraArg> argument) {
  const AlgebraArg* argument_ptr = argument.get();
  if (argument) {
    std::vector<std::unique_ptr<AlgebraArg>> args;
    args.push_back(std::move(argument));
    SetArgs<AlgebraArg>(kind, std::move(args));
  } else {
    SetArgs<AlgebraArg>(kind, {});
  }
  ABSL_DCHECK_EQ(GetArg(kind), argument_ptr);
}

const AlgebraArg* AlgebraNode::GetArg(int kind) const {
  int start = arg_slices_[kind].start;
  int size = arg_slices_[kind].size;
  if (size > 0) {
    ABSL_DCHECK_EQ(1, size);
    return args_[start];
  } else {
    return nullptr;
  }
}

AlgebraArg* AlgebraNode::GetMutableArg(int kind) {
  int start = arg_slices_[kind].start;
  int size = arg_slices_[kind].size;
  if (size > 0) {
    ABSL_DCHECK_EQ(1, size);
    return args_[start];
  } else {
    return nullptr;
  }
}

std::string AlgebraNode::DebugString(bool verbose) const {
  return this->DebugInternal("\n", verbose);
}

std::string AlgebraNode::ArgDebugString(absl::Span<const std::string> arg_names,
                                        absl::Span<const ArgPrintMode> arg_mode,
                                        const std::string& indent, bool verbose,
                                        bool more_children) const {
  ABSL_CHECK_EQ(arg_names.size(), arg_mode.size());
  std::string result;
  std::string separator;
  for (int kind = 0; kind < arg_names.size(); kind++) {
    std::string indent_child = indent;
    if (more_children || kind < arg_names.size() - 1) {
      absl::StrAppend(&indent_child, kIndentBar);
    } else {
      // No tree line is required beside the last child.
      absl::StrAppend(&indent_child, kIndentSpace);
    }
    absl::Span<const AlgebraArg* const> args = GetArgs<AlgebraArg>(kind);
    switch (arg_mode[kind]) {
      case kNOpt:
        if (args.empty()) {
          break;
        }
        ABSL_FALLTHROUGH_INTENDED;
      case kN: {
        std::vector<std::string> str;
        for (auto ch : args) {
          str.push_back(indent_child + kIndentFork +
                        ch->DebugInternal(indent_child, verbose));
        }
        absl::StrAppend(&result, separator, indent + kIndentFork,
                        arg_names[kind], ": {", absl::StrJoin(str, ","), "}");
        separator = ",";
        break;
      }
      case kOpt:
        if (args.empty()) {
          break;
        }
        ABSL_FALLTHROUGH_INTENDED;
      case k1:
        ABSL_CHECK_EQ(1, args.size());
        if (args[0] != nullptr) {
          absl::StrAppend(&result, separator, indent, kIndentFork,
                          arg_names[kind], ": ",
                          args[0]->DebugInternal(indent_child, verbose));
          separator = ",";
        }
        break;
      case k0:
        ABSL_CHECK(args.empty());
        break;
    }
  }
  return result;
}

// -------------------------------------------------------
// KeyArg
// -------------------------------------------------------

std::string KeyArg::DebugInternal(const std::string& indent,
                                  bool verbose) const {
  std::string sort;
  switch (order()) {
    case kAscending:
      sort = " ASC";
      break;
    case kDescending:
      sort = " DESC";
      break;
    case kNotApplicable:
      break;
  }
  switch (null_order()) {
    case kNullsFirst:
      absl::StrAppend(&sort, " NULLS FIRST");
      break;
    case kNullsLast:
      absl::StrAppend(&sort, " NULLS LAST");
      break;
    default:
      break;
  }

  if (collation() != nullptr) {
    absl::StrAppend(&sort,
                    " collation=", collation()->DebugInternal(indent, verbose));
  }

  return absl::StrCat(ExprArg::DebugInternal(indent, verbose), sort);
}

// -------------------------------------------------------
// Misc helpers
// -------------------------------------------------------
absl::Status GetSlotsForKeysAndValues(const TupleSchema& schema,
                                      absl::Span<const KeyArg* const> keys,
                                      std::vector<int>* slots_for_keys,
                                      std::vector<int>* slots_for_values) {
  slots_for_keys->reserve(keys.size());
  for (const KeyArg* key : keys) {
    std::optional<int> slot = schema.FindIndexForVariable(key->variable());
    GOOGLESQL_RET_CHECK(slot.has_value()) << "Cannot find variable " << key->variable()
                                << " in TupleSchema " << schema.DebugString();
    slots_for_keys->push_back(slot.value());
  }

  if (slots_for_values != nullptr) {
    const absl::flat_hash_set<int> slots_for_keys_set(slots_for_keys->begin(),
                                                      slots_for_keys->end());
    slots_for_values->reserve(schema.num_variables() -
                              slots_for_keys_set.size());
    for (int i = 0; i < schema.num_variables(); ++i) {
      if (!slots_for_keys_set.contains(i)) {
        slots_for_values->push_back(i);
      }
    }
  }

  return absl::OkStatus();
}

bool IsPosInf(const Value& value) {
  if (value.is_null()) {
    // A value of NULL is not positive infinity.
    return false;
  }
  switch (value.type_kind()) {
    case TYPE_FLOAT: {
      float v = value.float_value();
      return std::isinf(v) && v > 0;
    }
    case TYPE_DOUBLE: {
      double v = value.double_value();
      return std::isinf(v) && v > 0;
    }
    default:
      return false;
  }
}

bool IsNegInf(const Value& value) {
  if (value.is_null()) {
    // A value of NULL is not negative infinity.
    return false;
  }
  switch (value.type_kind()) {
    case TYPE_FLOAT: {
      float v = value.float_value();
      return std::isinf(v) && v < 0;
    }
    case TYPE_DOUBLE: {
      double v = value.double_value();
      return std::isinf(v) && v < 0;
    }
    default:
      return false;
  }
}

absl::StatusOr<Value> CreateTypedZeroForCost(const Type* type) {
  switch (type->kind()) {
    case TYPE_INT64:
      return Value::Int64(0);
    case TYPE_UINT64:
      return Value::Uint64(0);
    case TYPE_DOUBLE:
      return Value::Double(0);
    case TYPE_NUMERIC:
      return Value::Numeric(NumericValue(0));
    case TYPE_BIGNUMERIC:
      return Value::BigNumeric(BigNumericValue(0));
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected argument type in CreateTypedZeroForCost: "
                       << type->DebugString();
    }
  }
}

}  // namespace googlesql
