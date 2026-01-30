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

#ifndef GOOGLESQL_PUBLIC_TYPES_ARRAY_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_ARRAY_TYPE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// An array type.
// Arrays of arrays are not supported.
class LanguageOptions;
class TypeFactory;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;

class ArrayType : public ListBackedType {
 public:
#ifndef SWIG
  ArrayType(const ArrayType&) = delete;
  ArrayType& operator=(const ArrayType&) = delete;
#endif  // SWIG

  const Type* element_type() const { return element_type_; }

  const ArrayType* AsArray() const override { return this; }

  std::vector<const Type*> ComponentTypes() const override {
    return {element_type_};
  }

  // Helper function to determine deep equality or equivalence for array types.
  static bool EqualsImpl(const ArrayType* type1, const ArrayType* type2,
                         bool equivalent);

  // Arrays support ordering if FEATURE_ARRAY_ORDERING is enabled
  // and the array's element Type supports ordering.
  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;
  bool SupportsEquality() const override;

  std::string ShortTypeName(ProductMode mode,
                            bool use_external_float32) const override;
  std::string ShortTypeName(ProductMode mode) const override {
    return ShortTypeName(mode, /*use_external_float32=*/false);
  }
  std::string TypeName(ProductMode mode,
                       bool use_external_float32) const override;
  std::string TypeName(ProductMode mode) const override {
    return TypeName(mode, /*use_external_float32=*/false);
  }

  // Same as above, but the type modifier values are appended to the SQL name
  // for this ArrayType.
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32) const override;
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    return TypeNameWithModifiers(type_modifiers, mode,
                                 /*use_external_float32=*/false);
  }

  std::string CapitalizedName() const override;

  bool UsingFeatureV12CivilTimeType() const override {
    return element_type_->UsingFeatureV12CivilTimeType();
  }

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  int nesting_depth() const override {
    return element_type_->nesting_depth() + 1;
  }

  // Validate and resolve type parameters for array type, currently always
  // return error since array type itself doesn't support type parameters.
  absl::StatusOr<TypeParameters> ValidateAndResolveTypeParameters(
      const std::vector<TypeParameterValue>& type_parameter_values,
      ProductMode mode) const override;
  // Validates resolved type parameters for array element recursively.
  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

 protected:
  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  // Return estimated size of memory owned by this type. Array's owned memory
  // does not include its element type's memory (which is owned by some
  // TypeFactory).
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

 private:
  ArrayType(const TypeFactoryBase* factory, const Type* element_type);
  ~ArrayType() override;

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override;

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;
  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool EqualElementMultiSet(const ValueContent& x, const ValueContent& y,
                            const ValueEqualityCheckOptions& options) const;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;
  void FormatValueContentDebugModeImpl(
      const internal::ValueContentOrderedListRef* container_ref,
      const FormatValueContentOptions& options, std::string* result) const;
  void FormatValueContentSqlModeImpl(
      const internal::ValueContentOrderedListRef* container_ref,
      const FormatValueContentOptions& options, std::string* result) const;

  const Type* const element_type_;

  friend class TypeFactory;
  FRIEND_TEST(TypeTest, FormatValueContentArraySQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentArraySQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentArrayDebugMode);
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_ARRAY_TYPE_H_
