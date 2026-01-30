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

#ifndef GOOGLESQL_PUBLIC_TYPES_GRAPH_PATH_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_GRAPH_PATH_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// A graph path type. GraphPathType contains:
// 1. A node element type that can represent any node in this path.
// 2. A edge element type that can represent any edge in this path.
// GraphPathTypes are equal if all the above are equal.
class GraphPathType : public ListBackedType {
 public:
#ifndef SWIG
  GraphPathType(const GraphPathType&) = delete;
  GraphPathType& operator=(const GraphPathType&) = delete;
#endif  // SWIG

  const GraphElementType* node_type() const { return node_type_; }
  const GraphElementType* edge_type() const { return edge_type_; }

  const GraphPathType* AsGraphPath() const override { return this; }

  std::vector<const Type*> ComponentTypes() const override {
    return {node_type_, edge_type_};
  }

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;

  bool SupportsEquality() const override;

  bool UsingFeatureV12CivilTimeType() const override;

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

  int nesting_depth() const override { return nesting_depth_; }

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  // Returns if this GraphPathType is coercible to <to>.
  bool ABSL_MUST_USE_RESULT CoercibleTo(const GraphPathType* to) const;

 protected:
  int64_t GetEstimatedOwnedMemoryBytesSize() const override
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

 private:
  GraphPathType(const TypeFactory* factory, const GraphElementType* node_type,
                const GraphElementType* edge_type, int nesting_depth);

  static bool EqualsImpl(const GraphPathType* type1, const GraphPathType* type2,
                         bool equivalent);

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override;

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  absl::StatusOr<std::string> TypeNameImpl(size_t property_type_limit,
                                           ProductMode mode,
                                           bool use_external_float32) const;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;
  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;
  const Type* GetElementType(int index) const;
  void FormatValueContentDebugModeImpl(
      const internal::ValueContentOrderedList* container,
      const FormatValueContentOptions& options, std::string* result) const;

  // The super type of all nodes in this path.
  const GraphElementType* const node_type_;

  // The super type of all edges in this path.
  const GraphElementType* const edge_type_;

  // The deepest nesting depth in the type tree rooted at this GraphPathType.
  // This field is not serialized. It is recalculated during deserialization.
  const int nesting_depth_;

  friend class TypeFactory;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_GRAPH_PATH_TYPE_H_
