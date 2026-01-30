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

#ifndef GOOGLESQL_PUBLIC_TYPES_GRAPH_ELEMENT_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_GRAPH_ELEMENT_TYPE_H_

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"

namespace googlesql {

class LanguageOptions;
class TypeFactory;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;

// Property type of a graph element, representing a name and value type.
// The SWIG compiler does not understand nested classes, so this cannot be
// defined inside the scope of GraphElementType.
struct PropertyType {
  PropertyType(std::string name_in, const Type* value_type_in)
      : name(std::move(name_in)), value_type(value_type_in) {}

  std::string name;
  const Type* value_type;

  bool operator==(const PropertyType& other) const {
    return googlesql_base::CaseEqual(name, other.name) &&
           value_type->Equals(other.value_type);
  }

  template <typename H>
  friend H AbslHashValue(H h, const PropertyType& property_type) {
    for (const char c : property_type.name) {
      h = H::combine(std::move(h), absl::ascii_tolower(c));
    }
    return H::combine(std::move(h), *property_type.value_type);
  }
};

// Property type name: "<property name> <property value_type name>".
// Uses TypeNameWithModifiers for <value_type name> if <type_modifier> is not
// null, otherwise uses ShortTypeName. If <use_external_float32> is true,
// the typename for float is FLOAT32 instead of FLOAT. To be removed when all
// engines are updated (b/322105508).
absl::StatusOr<std::string> MakePropertyTypeName(
    const PropertyType& property_type, ProductMode mode,
    bool use_external_float32, const TypeModifiers* type_modifier = nullptr);

namespace internal {
struct CatalogName;
using GraphReference = CatalogName;
}  // namespace internal

// A graph element type. GraphElementType contains:
// 1. The fully qualified name (case-insensitive) of the graph this type belongs
//    to.
// 2. ElementKind (node or edge) this type represents.
// 3. A set of PropertyTypes (name is case-insensitive) that are accessible on
//    values of this type.
// 4. Whether this type is dynamic, i.e., whether dynamic properties are allowed
//    on values of this type.
// GraphElementTypes are equal if all the above are equal.
class GraphElementType : public ListBackedType {
 public:
#ifndef SWIG
  GraphElementType(const GraphElementType&) = delete;
  GraphElementType& operator=(const GraphElementType&) = delete;

  // Declared here for compatibility with existing code.
  using PropertyType = ::googlesql::PropertyType;
#endif  // SWIG

  // Graph element kind.
  enum ElementKind {
    kNode,
    kEdge,
  };

  // Path to the graph to which this GraphElementType belongs. Can be used for
  // PropertyGraph lookup inside the Catalog used for building this
  // GraphElementType.
  absl::Span<const std::string> graph_reference() const;

  // Returns a set of static property types.
  absl::Span<const PropertyType> property_types() const {
    return property_types_;
  }

  // Looks up a property type by name.
  // Returns NULL if no property type with <name> is found.
  const PropertyType* FindPropertyType(absl::string_view name) const;

  // Convenient method that returns true if this is a dynamic graph element
  // type.
  bool is_dynamic() const { return is_dynamic_; }

  // Returns the graph element kind.
  ElementKind element_kind() const { return element_kind_; }
  bool IsNode() const { return element_kind_ == kNode; }
  bool IsEdge() const { return element_kind_ == kEdge; }

  const GraphElementType* AsGraphElement() const override { return this; }

  std::vector<const Type*> ComponentTypes() const override {
    std::vector<const Type*> component_types;
    component_types.reserve(property_types_.size());
    for (const PropertyType& property_type : property_types_) {
      component_types.push_back(property_type.value_type);
    }
    return component_types;
  }

  // Check if the graph element has some fields (property types).
  bool HasAnyFields() const override;

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

  // Returns if this GraphElementType is coercible to <to>.
  bool ABSL_MUST_USE_RESULT CoercibleTo(const GraphElementType* to) const;

 protected:
  int64_t GetEstimatedOwnedMemoryBytesSize() const override
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

 private:
  GraphElementType(const internal::GraphReference* graph_reference,
                   ElementKind element_kind, const TypeFactory* factory,
                   absl::flat_hash_set<PropertyType> property_types,
                   int nesting_depth, bool is_dynamic = false);

  // Look up a property type by name.
  // Returns NULL if <name> is not found.
  // If found_idx is non-NULL, returns position of found property type in
  // <*found_idx>.
  const PropertyType* FindPropertyTypeImpl(absl::string_view name,
                                           int* found_idx) const;

  static bool EqualsImpl(const GraphElementType* type1,
                         const GraphElementType* type2, bool equivalent);

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override;

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  absl::StatusOr<std::string> TypeNameImpl(
      size_t property_type_limit,
      absl::FunctionRef<absl::StatusOr<std::string>(const PropertyType&,
                                                    size_t)>
          property_type_name_fn) const;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  HasFieldResult HasFieldImpl(const std::string& name, int* field_id,
                              bool include_pseudo_fields) const override;

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
  void FormatValueContentDebugModeImpl(const ValueContent& content,
                                       const FormatValueContentOptions& options,
                                       std::string* result) const;

  // A path that can be used for looking up the graph to which this
  // GraphElementType belongs to, inside the catalog for resolving the graph
  // query.
  const internal::GraphReference* const graph_reference_;

  // Decides whether this is a node type or edge type.
  const ElementKind element_kind_;

  // A set of static property types sorted by property type name.
  const std::vector<PropertyType> property_types_;

  // The deepest nesting depth in the type tree rooted at this GraphElementType,
  // i.e., the maximum nesting_depth of the property value types, plus 1 for the
  // GraphElementType itself. If all property values are simple types, then this
  // is 1. This field is not serialized. It is recalculated during
  // deserialization.
  const int nesting_depth_;

  // Decides whether this is a dynamic graph element type.
  bool is_dynamic_;

  // Lazily built map from name to element property index. Ambiguous lookups are
  // designated with an index of -1. This is only built if FindPropertyTypeImpl
  // is called.
  mutable absl::Mutex mutex_;
  mutable absl::flat_hash_map<absl::string_view, int,
                              googlesql_base::StringViewCaseHash,
                              googlesql_base::StringViewCaseEqual>
      property_name_to_index_map_ ABSL_GUARDED_BY(mutex_);

  friend class TypeFactory;
  friend class GraphPathType;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_GRAPH_ELEMENT_TYPE_H_
