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

#include "googlesql/public/input_argument_type.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

// Determine equivalence class of the input argument type, for ordering
// purposes via InputArgumentTypeLess.  Typed and untyped nulls are
// considered in the same class, non-null literals are in their own class,
// and non-literals and parameters are in the same class.
static int InputArgumentTypeEquivalenceClass(const InputArgumentType& type) {
  if (type.is_untyped() || type.is_literal_null()) {
    return 2;
  }
  if (type.is_literal()) {
    return 1;
  }
  // Non-literals (including parameters).
  return 0;
}

bool InputArgumentTypeLess::operator()(const InputArgumentType& type1,
                                       const InputArgumentType& type2) const {
  // If arguments have different type kinds, then order by kind.
  if (type1.type() != nullptr && type2.type() != nullptr &&
      type1.type()->kind() != type2.type()->kind()) {
    return type1.type()->kind() < type2.type()->kind();
  }

  // For same type kinds, generally order non-literals (including parameters)
  // before literals, and literals before nulls (with untyped nulls last).
  // We determine the equivalence classes of the InputArgumentTypes and
  // order between them.
  int type1_class = InputArgumentTypeEquivalenceClass(type1);
  int type2_class = InputArgumentTypeEquivalenceClass(type2);
  if (type1_class == type2_class) {
    // They are in the same equivalence class.  For complex types we still
    // need to handle the case of different distinct types for the same type
    // kind (for example, two different struct literals, etc.).  We order
    // between them via DebugString();
    if (type1.type() != nullptr && !type1.type()->IsSimpleType()) {
      return type1.DebugString() < type2.DebugString();
    }
  }
  return type1_class < type2_class;
}

bool InputArgumentType::operator==(const InputArgumentType& rhs) const {
  const bool types_equal =
      (type_ == nullptr && rhs.type_ == nullptr) ||
      (type_ != nullptr && rhs.type_ != nullptr && type_->Equals(rhs.type_));
  return category_ == rhs.category_ && types_equal &&
         is_literal_null() == rhs.is_literal_null() &&
         is_literal_empty_array() == rhs.is_literal_empty_array();
}

InputArgumentType::InputArgumentType(const Value& literal_value,
                                     bool is_default_argument_value)
    : type_(literal_value.type()),
      category_(kTypedLiteral),
      is_default_argument_value_(is_default_argument_value) {
  EnsureData().literal_value = literal_value;
  is_literal_for_constness_ = true;
  if (literal_value.type()->IsStruct()) {
    if (literal_value.is_null()) {
      // This is a NULL struct, so its field InputArgumentTypes are the
      // struct's field types.
      for (const StructType::StructField& struct_field :
           literal_value.type()->AsStruct()->fields()) {
        EnsureData().field_types.push_back(
            InputArgumentType(struct_field.type));
      }
    } else {
      for (const Value& field_value : literal_value.fields()) {
        EnsureData().field_types.push_back(InputArgumentType(field_value));
      }
    }
  }
}

InputArgumentType::InputArgumentType(const Type* type, bool is_query_parameter,
                                     bool is_literal_for_constness)
    : type_(type),
      category_(is_query_parameter ? kTypedParameter : kTypedExpression),
      is_literal_for_constness_(is_literal_for_constness) {
  ABSL_DCHECK(type != nullptr);
  if (type->IsStruct()) {
    for (const StructType::StructField& struct_field :
         type->AsStruct()->fields()) {
      // The struct itself might be a parameter, but its fields should not
      // coerce like parameters.
      EnsureData().field_types.push_back(
          InputArgumentType(struct_field.type, false /* is_query_parameter */));
    }
  }
}

InputArgumentType::InputArgumentType(absl::StatusOr<Value> constant_value)
    : category_(kTypedExpression) {
  ABSL_DCHECK(!absl::IsInternal(constant_value.status()));
  ABSL_DCHECK(!absl::IsResourceExhausted(constant_value.status()));
  ABSL_DCHECK(!absl::IsOutOfRange(constant_value.status()));
  if (!constant_value.ok()) {
    type_ = types::Int64Type();
  } else {
    type_ = constant_value->type();
    EnsureData().constant_value = std::move(constant_value);
  }
}

InputArgumentType::InputArgumentType(const InputArgumentType& other)
    : type_(other.type_),
      category_(other.category_),
      is_default_argument_value_(other.is_default_argument_value_),
      is_pipe_input_table_(other.is_pipe_input_table_),
      is_chained_function_call_input_(other.is_chained_function_call_input_),
      is_literal_for_constness_(other.is_literal_for_constness_) {
  if (other.data_) {
    data_ = std::make_unique<Data>(*other.data_);
  }
}

InputArgumentType& InputArgumentType::operator=(
    const InputArgumentType& other) {
  if (this != &other) {
    type_ = other.type_;
    category_ = other.category_;
    is_default_argument_value_ = other.is_default_argument_value_;
    is_pipe_input_table_ = other.is_pipe_input_table_;
    is_chained_function_call_input_ = other.is_chained_function_call_input_;
    is_literal_for_constness_ = other.is_literal_for_constness_;
    if (other.data_) {
      data_ = std::make_unique<Data>(*other.data_);
    } else {
      data_.reset();
    }
  }
  return *this;
}

const std::vector<InputArgumentType>& InputArgumentType::field_types() const {
  return ReadOnlyData().field_types;
}

size_t InputArgumentType::field_types_size() const {
  return ReadOnlyData().field_types.size();
}

const InputArgumentType& InputArgumentType::field_type(int i) const {
  ABSL_DCHECK(data_ != nullptr && data_->field_types.size() > i);
  return ReadOnlyData().field_types.at(i);
}

absl::StatusOr<Value> InputArgumentType::GetAnalysisTimeConstantValue() const {
  GOOGLESQL_RET_CHECK(ReadOnlyData().constant_value.has_value())
      << "constant value is not set";
  return *ReadOnlyData().constant_value;
}

std::string InputArgumentType::UserFacingName(ProductMode product_mode) const {
  if (is_untyped_null()) {
    return "NULL";
  } else if (is_untyped_empty_array()) {
    return "[]";
  } else if (is_relation()) {
    std::vector<std::string> type_strings;
    type_strings.reserve(relation_input_schema().num_columns());
    for (const TVFRelation::Column& column :
         relation_input_schema().columns()) {
      if (!relation_input_schema().is_value_table() &&
          !IsInternalAlias(column.name)) {
        type_strings.push_back(absl::StrCat(
            column.name, " ", column.type->ShortTypeName(product_mode)));
      } else {
        type_strings.push_back(column.type->ShortTypeName(product_mode));
      }
    }
    return absl::StrCat("TABLE<", absl::StrJoin(type_strings, ", "), ">");
  } else if (is_model()) {
    return "MODEL";
  } else if (is_connection()) {
    return "CONNECTION";
  } else if (is_lambda()) {
    return "LAMBDA";
  }
  if (type() == nullptr) {
    return DebugString(false);
  }
  return type()->ShortTypeName(product_mode);
}

std::string InputArgumentType::DebugString(bool verbose) const {
  switch (category_) {
    case kRelation:
      return absl::StrCat(
          "RELATION", (is_pipe_input_table_ ? "(is_pipe_input_table)" : ""));
    case kModel:
      return "MODEL";
    case kConnection:
      return "CONNECTION";
    case kDescriptor:
      return "DESCRIPTOR";
    case kLambda:
      return "LAMBDA";
    case kSequence:
      return "SEQUENCE";
    case kGraph:
      return "GRAPH";
    case kUntypedNull:
      return absl::StrCat(verbose ? "untyped" : "", "NULL");
    case kUntypedEmptyArray:
      return absl::StrCat(verbose ? "untyped" : "", "empty array");

    case kTypedExpression:
    case kTypedLiteral:
    case kTypedParameter:
    case kUntypedParameter:
      // These expression types are handled below.
      break;
  }

  std::string prefix;
  if (ReadOnlyData().literal_value.has_value()) {
    if (ReadOnlyData().literal_value.value().is_null()) {
      absl::StrAppend(&prefix, "null ");
    } else if (type()->IsSimpleType()) {
      absl::StrAppend(&prefix, "literal ");
    }
  } else if (verbose && is_query_parameter()) {
    if (is_untyped()) {
      absl::StrAppend(&prefix, "untyped ");
    }
    absl::StrAppend(&prefix, "parameter ");
  }
  if (is_chained_function_call_input_) {
    absl::StrAppend(&prefix, "chained_function_call_input ");
  }
  if (type_ != nullptr) {
    absl::StrAppend(&prefix, type_->DebugString());
  }
  return prefix;
}

// static
std::string InputArgumentType::ArgumentsToString(
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode,
    absl::Span<const absl::string_view> argument_names) {
  constexpr int kMaxArgumentsStringLength = 1024;
  std::string arguments_string;
  bool first = true;
  for (const InputArgumentType& argument : arguments) {
    // Shift the argument name at the front of the list.
    absl::string_view argument_name;
    if (!argument_names.empty()) {
      argument_name = argument_names.front();
      argument_names.remove_prefix(1);
    }

    // Mark pipe input table arguments to try to make error messages clearer
    // when a pipe input table is inserted somewhere in the argument list.
    absl::StrAppend(&arguments_string, (first ? "" : ", "),
                    !argument_name.empty() ? argument_name : "",
                    !argument_name.empty() ? " => " : "",
                    argument.UserFacingName(product_mode),
                    argument.is_pipe_input_table() ? " (from pipe input)" : "",
                    argument.is_chained_function_call_input()
                        ? " (from chained function call input)"
                        : "");
    if (arguments_string.size() > kMaxArgumentsStringLength) {
      constexpr absl::string_view kEllipses = "...";
      arguments_string.resize(kMaxArgumentsStringLength - kEllipses.size());
      arguments_string.append(std::string(kEllipses));
      break;
    }
    first = false;
  }
  return arguments_string;
}

// static
InputArgumentType InputArgumentType::RelationInputArgumentType(
    const TVFRelation& relation_input_schema, bool is_pipe_input_table) {
  InputArgumentType type;
  type.category_ = kRelation;
  type.EnsureData().relation_input_schema.reset(
      new TVFRelation(relation_input_schema));
  type.is_pipe_input_table_ = is_pipe_input_table;
  return type;
}

InputArgumentType InputArgumentType::ModelInputArgumentType(
    const TVFModelArgument& model_arg) {
  InputArgumentType type;
  type.category_ = kModel;
  type.EnsureData().model_arg.reset(new TVFModelArgument(model_arg));
  return type;
}

InputArgumentType InputArgumentType::ConnectionInputArgumentType(
    const TVFConnectionArgument& connection_arg) {
  InputArgumentType type;
  type.category_ = kConnection;
  type.EnsureData().connection_arg.reset(
      new TVFConnectionArgument(connection_arg));
  return type;
}

InputArgumentType InputArgumentType::DescriptorInputArgumentType() {
  InputArgumentType type;
  type.category_ = kDescriptor;
  return type;
}

InputArgumentType InputArgumentType::GraphInputArgumentType() {
  InputArgumentType type;
  type.category_ = kGraph;
  return type;
}

InputArgumentType InputArgumentType::LambdaInputArgumentType() {
  InputArgumentType type;
  type.category_ = kLambda;
  type.type_ = nullptr;
  return type;
}

InputArgumentType InputArgumentType::SequenceInputArgumentType() {
  InputArgumentType type;
  type.category_ = kSequence;
  type.type_ = nullptr;
  return type;
}

const InputArgumentType::Data& InputArgumentType::ReadOnlyData() const {
  static const absl::NoDestructor<Data> kEmptyData;
  return data_ ? *data_ : *kEmptyData;
}

bool InputArgumentTypeSet::Insert(const InputArgumentType& argument,
                                  bool set_dominant) {
  if (set_dominant) {
    dominant_argument_ = std::make_unique<InputArgumentType>(argument);
  } else if (dominant_argument_ != nullptr &&
             dominant_argument_->type() != nullptr &&
             dominant_argument_->type()->IsSimpleType() &&
             argument.type() != nullptr && !argument.type()->IsSimpleType() &&
             !argument.is_untyped_empty_array()) {
    dominant_argument_ = std::make_unique<InputArgumentType>(argument);
  } else if (dominant_argument_ == nullptr && !argument.is_untyped()) {
    dominant_argument_ = std::make_unique<InputArgumentType>(argument);
  }

  if (arguments_set_ != nullptr) {
    const bool inserted =
        googlesql_base::InsertIfNotPresent(arguments_set_.get(), argument);
    if (inserted) {
      arguments_vector_.push_back(argument);
    }
    ABSL_DCHECK_EQ(arguments_set_->size(), arguments_vector_.size());
    return inserted;
  } else {
    for (const InputArgumentType& arg : arguments_vector_) {
      if (arg == argument) return false;
    }
    // It didn't exist already, so we'll insert it.
    arguments_vector_.push_back(argument);

    // If the vector is big enough that we don't want to do linear scans,
    // start storing the hash_set too.  We still keep the bucket_count low
    // because we still don't expect these hash_sets to usually get large.
    if (arguments_vector_.size() > kMaxSizeBeforeMakingHashSet) {
      arguments_set_ = std::make_unique<ArgumentsHashSet>(
          arguments_vector_.begin(), arguments_vector_.end(),
          10 /* bucket_count */);
    }
    return true;
  }
}

void InputArgumentTypeSet::clear() {
  arguments_set_.reset();
  arguments_vector_.clear();
  dominant_argument_.reset();
}

std::string InputArgumentTypeSet::ToString(bool verbose) const {
  std::vector<InputArgumentType> argument_list = arguments_vector_;
  std::sort(argument_list.begin(), argument_list.end(),
            InputArgumentTypeLess());
  std::string dominant_argument;
  if (verbose) {
    absl::StrAppend(&dominant_argument, ": dominant_argument(",
                    (dominant_argument_ != nullptr
                         ? dominant_argument_->DebugString(verbose)
                         : "null"),
                    ")");
  }
  return absl::StrCat("{", InputArgumentType::ArgumentsToString(argument_list),
                      "}", dominant_argument);
}

}  // namespace googlesql
