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

#ifndef GOOGLESQL_PUBLIC_TYPES_RANGE_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_RANGE_TYPE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

class LanguageOptions;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;
class TypeModifiers;

// A range type. A range represents a contiguous range of values of type
// element_type(). A range is represented by its start and end element, which
// are inclusive/exclusive bounds respectively. A range is groupable,
// partitionable, and orderable. Range element types must be orderable,
// groupable, and partitionable. Only ranges of DATE, DATETIME, and TIMESTAMP
// are supported.
class RangeType : public ListBackedType {
 public:
#ifndef SWIG
  RangeType(const RangeType&) = delete;
  RangeType& operator=(const RangeType&) = delete;
#endif  // SWIG

  // The element type of the range.
  const Type* element_type() const { return element_type_; }

  std::vector<const Type*> ComponentTypes() const override {
    return {element_type_};
  }

  bool UsingFeatureV12CivilTimeType() const override {
    return element_type_->UsingFeatureV12CivilTimeType();
  }

  const RangeType* AsRange() const override { return this; }

  std::string ShortTypeName(ProductMode mode) const override;
  std::string TypeName(ProductMode mode) const override;

  // Same as above, but the type modifier values are appended to the SQL name
  // for this RangeType.
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override;

  std::string CapitalizedName() const override;

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  int nesting_depth() const override {
    return element_type_->nesting_depth() + 1;
  }

  // Helper function for determining if a type is a valid range element type.
  static bool IsValidElementType(const Type* element_type);

  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

 protected:
  // Return estimated size of memory owned by this type. Range's owned memory
  // does not include its element type's memory (which is owned by some
  // TypeFactory).
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

 private:
  RangeType(const TypeFactoryBase* factory, const Type* element_type);
  ~RangeType() override;

  // Helper function for determining if a type kind is a supported range element
  // type kind.
  static bool IsSupportedElementTypeKind(TypeKind element_type_kind);

  // Helper function for determining equality or equivalence for range types.
  // Equals means that the range element type is the same.
  static bool EqualsImpl(const RangeType* type1, const RangeType* type2,
                         bool equivalent);

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override {
    if (no_grouping_type != nullptr) {
      *no_grouping_type = nullptr;
    }
    return true;
  }

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;

  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  std::string FormatNullableValueContent(
      const internal::NullableValueContent& element,
      const Type::FormatValueContentOptions& options) const;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  const Type* const element_type_;

  friend class TypeFactory;
  friend class RangeTypeTestPeer;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_RANGE_TYPE_H_
