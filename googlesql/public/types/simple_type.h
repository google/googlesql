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

#ifndef GOOGLESQL_PUBLIC_TYPES_SIMPLE_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_SIMPLE_TYPE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/civil_time.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/type.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace googlesql {

class Value;
class TypeModifiers;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;

// SimpleType includes all the non-parameterized builtin types (all scalar types
// except enum).
class SimpleType : public Type {
 public:
  SimpleType(const TypeFactoryBase* factory, TypeKind kind);
#ifndef SWIG
  SimpleType(const SimpleType&) = delete;
  SimpleType& operator=(const SimpleType&) = delete;
#endif  // SWIG

  std::string TypeName(ProductMode mode,
                       bool use_external_float32) const override;
  std::string TypeName(ProductMode mode) const override {
    return TypeName(mode, /*use_external_float32=*/false);
  }

  // Same as above, but the type modifier values are appended to the SQL name
  // for this SimpleType.
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32) const override;
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    return TypeNameWithModifiers(type_modifiers, mode,
                                 /*use_external_float32=*/false);
  }

  std::string CapitalizedName() const override;

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  // Check whether type with a given name exists and is simple. If yes, returns
  // the type kind of the found simple type. Returns TYPE_UNKNOWN otherwise.
  // If 'language_features' is null, then assume all language features are
  // enabled.
  static TypeKind GetTypeKindIfSimple(
      absl::string_view type_name, ProductMode mode,
      const LanguageOptions::LanguageFeatureSet* language_features = nullptr);
  // Validate and resolve type parameters for SimpleTypes.
  // Resolvable type parameters:
  //   - STRING(L) / BYTES(L)
  //   - STRING(MAX) / BYTES(MAX)
  //   - NUMERIC(P) / BIGNUMERIC(P)
  //   - NUMERIC(P, S) / BIGNUMERIC(P, S)
  //   - BIGNUMERIC(MAX) / BIGNUMERIC(MAX, S)
  absl::StatusOr<TypeParameters> ValidateAndResolveTypeParameters(
      const std::vector<TypeParameterValue>& type_parameter_values,
      ProductMode mode) const override;
  // Validates resolved type parameters, used in validator.cc.
  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

 protected:
  ~SimpleType() override;

  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

 private:
  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override {
    return true;
  }

  friend class Value;
  static void ClearValueContent(TypeKind kind, const ValueContent& value);
  static void CopyValueContent(TypeKind kind, const ValueContent& from,
                               ValueContent* to);

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;
  uint64_t GetValueContentExternallyAllocatedByteSize(
      const ValueContent& value) const override;
  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  // Resolves type parameters for STRING(L), BYTES(L).
  absl::StatusOr<TypeParameters> ResolveStringBytesTypeParameters(
      absl::Span<const TypeParameterValue> type_parameter_values,
      ProductMode mode) const;
  // Resolves type parameters for NUMERIC(P), BIGNUMERIC(P), NUMERIC(P, S),
  // BIGNUMERIC(P, S) and create respective TypeParameters class.
  absl::StatusOr<TypeParameters> ResolveNumericBignumericTypeParameters(
      absl::Span<const TypeParameterValue> type_parameter_values,
      ProductMode mode) const;
  // Resolves type parameters for TIMESTAMP(<precision>). `precision` must be
  // one of 0, 3, 6, 9 or 12, and must be less than or equal to the maximum
  // precision supported by the system (e.g. Micros, Nanos).
  absl::StatusOr<TypeParameters> ResolveTimestampTypeParameters(
      absl::Span<const TypeParameterValue> type_parameter_values,
      ProductMode mode) const;

  // Validates the resolved numeric type parameters.
  // We put ValidateNumericTypeParameters() in Type class instead of
  // TypeParameters class because TypeParameters class doesn't know whether
  // type is Numeric or BigNumeric.
  absl::Status ValidateNumericTypeParameters(
      const NumericTypeParametersProto& numeric_param, ProductMode mode) const;

  // Returns the a TokenList formatted as a string.
  std::string FormatTokenList(const ValueContent& value,
                              const FormatValueContentOptions& options) const;
  // Returns the debug content of the TokenList as a sequence of lines, each
  // representing a single token.
  static std::vector<std::string> FormatTokenLines(
      const ValueContent& value, const FormatValueContentOptions& options);

  // Used for TYPE_TIMESTAMP.
  static absl::Time GetTimestampValue(const ValueContent& value);
  static absl::Status SetTimestampValue(absl::Time time, ValueContent* value);

  // Used for TYPE_TIME.
  static TimeValue GetTimeValue(const ValueContent& value);
  static absl::Status SetTimeValue(TimeValue time, ValueContent* value);

  // Used for TYPE_DATETIME.
  static DatetimeValue GetDateTimeValue(const ValueContent& value);
  static absl::Status SetDateTimeValue(DatetimeValue datetime,
                                       ValueContent* value);

  friend class TypeFactory;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_SIMPLE_TYPE_H_
