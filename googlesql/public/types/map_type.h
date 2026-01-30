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

#ifndef GOOGLESQL_PUBLIC_TYPES_MAP_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_MAP_TYPE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/container_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

class LanguageOptions;
class TypeFactory;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;

class MapType : public ContainerType {
 public:
#ifndef SWIG
  MapType(const MapType&) = delete;
  MapType& operator=(const MapType&) = delete;
#endif  // SWIG

  const Type* key_type() const { return key_type_; }
  const Type* value_type() const { return value_type_; }

  const MapType* AsMap() const override { return this; }

  std::vector<const Type*> ComponentTypes() const override {
    return {key_type_, value_type_};
  }

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

  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32) const override;
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    return TypeNameWithModifiers(type_modifiers, mode,
                                 /*use_external_float32=*/false);
  }

  std::string CapitalizedName() const override;

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;
  bool SupportsEquality() const override;

  bool UsingFeatureV12CivilTimeType() const override {
    return key_type_->UsingFeatureV12CivilTimeType() ||
           value_type_->UsingFeatureV12CivilTimeType();
  }

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  int nesting_depth() const override;

  // Validates resolved type parameters for map key and value.
  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

 protected:
  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  // Return estimated size of memory owned by this type. Map's owned memory
  // does not include its key or value type's memory (which is owned by some
  // TypeFactory).
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

 private:
  MapType(const TypeFactoryBase* factory, const Type* key_type,
          const Type* value_type);
  ~MapType() override;

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
  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  // Helper method to compare the value of `lookup_key` in the given
  // `lookup_map` with some `expected_value`. Key and value comparisons consider
  // the provided equality options. If the equality check fails and
  // `options.reason` is present, it will be populated with a message
  // including the reason for mismatch and a stringified version of each map,
  // given by `formattable_lookup_content` and `formattable_expected_content`.
  bool LookupMapEntryEqualsExpected(
      const internal::ValueContentMap* lookup_map,
      const internal::NullableValueContent& lookup_key,
      const internal::NullableValueContent& expected_value,
      const NullableValueContentEq& value_eq,
      const ValueContent& formattable_lookup_content,
      const ValueContent& formattable_expected_content,
      const ValueEqualityCheckOptions& options,
      const ValueEqualityCheckOptions& key_equality_options) const;

  void FormatValueContentDebugModeImpl(
      const internal::ValueContentMap* value_content_map,
      const FormatValueContentOptions& options, std::string* result) const;
  void FormatValueContentSqlModeImpl(
      const internal::ValueContentMap* value_content_map,
      const FormatValueContentOptions& options, std::string* result) const;

  struct ValueContentMapElementHasher;
  const Type* const key_type_;
  const Type* const value_type_;

  friend class TypeFactory;
};

// Get the Type of the map key. map_type *must be* a MapType.
const Type* GetMapKeyType(const Type* map_type);
// Get the Type of the map value. map_type *must be* a MapType.
const Type* GetMapValueType(const Type* map_type);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_MAP_TYPE_H_
