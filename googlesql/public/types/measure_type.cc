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

#include "googlesql/public/types/measure_type.h"

#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string MeasureType::ShortTypeName(ProductMode mode,
                                       bool use_external_float32) const {
  return absl::StrCat(
      "MEASURE<", result_type_->ShortTypeName(mode, use_external_float32), ">");
}

std::string MeasureType::TypeName(ProductMode mode,
                                  bool use_external_float32) const {
  return absl::StrCat("MEASURE<",
                      result_type_->TypeName(mode, use_external_float32), ">");
}

absl::StatusOr<std::string> MeasureType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  if (!type_params.IsEmpty() && type_params.num_children() != 1) {
    return MakeSqlError()
           << "Input type parameter does not correspond to MeasureType";
  }

  // TODO: b/350555383 - Support collation if it's needed in the future. Adding
  // collation support to this function is trivial, but general collation code
  // needs to support MeasureType.
  const Collation& collation = type_modifiers.collation();
  if (!collation.Empty()) {
    return MakeSqlError() << "MeasureType does not support collation";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string result_type_name,
      result_type_->TypeNameWithModifiers(
          TypeModifiers::MakeTypeModifiers(
              type_params.IsEmpty() ? TypeParameters() : type_params.child(0),
              collation),
          mode, use_external_float32));

  return absl::StrCat("MEASURE<", result_type_name, ">");
}

std::string MeasureType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_MEASURE);  // Crash OK
  return absl::StrCat("Measure<", AsMeasure()->result_type()->CapitalizedName(),
                      ">");
}

bool MeasureType::SupportsOrdering(const LanguageOptions& language_options,
                                   std::string* type_description) const {
  if (type_description != nullptr) {
    *type_description = "MEASURE";
  }
  return false;
}

bool MeasureType::SupportsEquality() const { return false; }

bool MeasureType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  *no_partitioning_type = this;
  return false;
}

bool MeasureType::IsSupportedType(
    const LanguageOptions& language_options) const {
  return language_options.LanguageFeatureEnabled(FEATURE_ENABLE_MEASURES) &&
         result_type_->IsSupportedType(language_options);
}

void MeasureType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                  std::string* debug_string) const {
  absl::StrAppend(debug_string, "MEASURE<");
  stack->push_back(">");
  stack->push_back(result_type());
}

absl::HashState MeasureType::HashTypeParameter(absl::HashState state) const {
  return result_type()->Hash(std::move(state));
}

absl::HashState MeasureType::HashValueContent(const ValueContent& value,
                                              absl::HashState state) const {
  absl::HashState result = absl::HashState::Create(&state);
  const internal::ValueContentMeasure* value_content_measure =
      value.GetAs<internal::ValueContentMeasureRef*>()->value();
  const internal::ValueContentOrderedList* captured_values =
      value_content_measure->GetCapturedValues();
  const StructType* captured_values_struct_type =
      value_content_measure->GetCapturedValuesStructType();
  ABSL_DCHECK(captured_values_struct_type->num_fields() ==
         captured_values->num_elements());
  for (int i = 0; i < captured_values_struct_type->num_fields(); i++) {
    const StructField& field = captured_values_struct_type->field(i);
    NullableValueContentHasher hasher(field.type);
    result = absl::HashState::combine(std::move(result),
                                      hasher(captured_values->element(i)));
  }
  for (int key_index : value_content_measure->KeyIndices()) {
    result = absl::HashState::combine(std::move(result), key_index);
  }
  return result;
}

absl::Status MeasureType::SerializeValueContent(const ValueContent& value,
                                                ValueProto* value_proto) const {
  // A measure value is currently modeled as a thin wrapper around a STRUCT.
  // However, we choose not to support serialization because it detracts from
  // the conceptual meaning of a measure (which is a specialized aggregate
  // lambda).
  return absl::UnimplementedError(
      "SerializeValueContent is unsupported for MeasureType.");
}

absl::Status MeasureType::DeserializeValueContent(const ValueProto& value_proto,
                                                  ValueContent* value) const {
  // A measure value is currently modeled as a thin wrapper around a STRUCT.
  // However, we choose not to support serialization because it detracts from
  // the conceptual meaning of a measure (which is a specialized aggregate
  // lambda).
  return absl::UnimplementedError(
      "DeserializeValueContent is unsupported for MeasureType.");
}

absl::Status MeasureType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind_);
  return result_type()->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto->mutable_measure_type()->mutable_result_type(),
      file_descriptor_set_map);
}

// Measure values are currently just a thin wrapper around a STRUCT.
// ValueContent comparisons are technically possible, but we choose not to
// support them because it detracts from the conceptual meaning of a measure
// (which is a specialized aggregate lambda).
bool MeasureType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  return false;
}
bool MeasureType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                   const Type* other_type) const {
  return false;
}

std::string MeasureType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  switch (options.mode) {
    case Type::FormatValueContentOptions::Mode::kDebug:
      break;
    case Type::FormatValueContentOptions::Mode::kSQLLiteral:
    case Type::FormatValueContentOptions::Mode::kSQLExpression:
      ABSL_LOG(ERROR) << "No SQL expression or literal for measures";
      return "()";
  }

  const internal::ValueContentMeasure* value_content_measure =
      value.GetAs<internal::ValueContentMeasureRef*>()->value();
  std::string result;
  // Always print the type name for measures. This prevents measure values from
  // being printed (and possibly interpreted) as just a STRUCT.
  absl::StrAppend(&result, CapitalizedName());
  absl::StrAppend(&result, "{");
  value_content_measure->GetCapturedValuesStructType()
      ->FormatValueContentDebugModeImpl(
          value_content_measure->GetCapturedValues(), options, &result);
  absl::StrAppend(&result, "}");
  return result;
}

void MeasureType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentMeasureRef*>()->Unref();
}

void MeasureType::CopyValueContent(const ValueContent& from,
                                   ValueContent* to) const {
  from.GetAs<internal::ValueContentMeasureRef*>()->Ref();
  *to = from;
}

}  // namespace googlesql
