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

#include "googlesql/public/types/graph_element_type.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value_content.h"
#include "absl/algorithm/container.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

constexpr size_t kShortTypeNameNumPropertyTypeLimit = 3;

std::string ElementKindName(const GraphElementType::ElementKind element_kind) {
  switch (element_kind) {
    case GraphElementType::kNode:
      return "GRAPH_NODE";
    case GraphElementType::kEdge:
      return "GRAPH_EDGE";
  }
}

GraphElementTypeProto::ElementKind TypeProtoElementKind(
    const GraphElementType::ElementKind element_kind) {
  switch (element_kind) {
    case GraphElementType::ElementKind::kNode:
      return GraphElementTypeProto::KIND_NODE;
    case GraphElementType::ElementKind::kEdge:
      return GraphElementTypeProto::KIND_EDGE;
  }
}

// Estimates <property_type> memory usage.
int64_t GetEstimatedPropertyTypeOwnedMemoryBytesSize(
    const PropertyType& property_type) {
  static_assert(
      sizeof(property_type) ==
          sizeof(std::tuple<decltype(property_type.name),
                            decltype(property_type.value_type)>),
      "You need to update GetEstimatedPropertyTypeOwnedMemoryBytesSize "
      "when you change PropertyType");

  return sizeof(property_type) +
         internal::GetExternallyAllocatedMemoryEstimate(property_type.name);
}

// Returns the input <property_types> set as a sorted list.
std::vector<PropertyType> SortPropertyTypes(
    absl::flat_hash_set<PropertyType> property_types) {
  std::vector<PropertyType> result(
      std::make_move_iterator(property_types.begin()),
      std::make_move_iterator(property_types.end()));
  absl::c_sort(result, [](const PropertyType& pt1, const PropertyType& pt2) {
    return googlesql_base::CaseLess()(pt1.name, pt2.name);
  });
  return result;
}

// Gets GraphElementContainer from ValueContent.
const internal::GraphElementContainer* GetGraphElementContainer(
    const ValueContent& value_content) {
  return value_content.GetAs<internal::ValueContentOrderedListRef*>()
      ->value()
      ->GetAs<internal::GraphElementContainer>();
}
}  // namespace

// Property type name: "<property name> <property value_type name>".
// Uses TypeNameWithModifiers for <value_type name> if <type_modifier> is not
// null, otherwise uses ShortTypeName.
absl::StatusOr<std::string> MakePropertyTypeName(
    const PropertyType& property_type, ProductMode mode,
    bool use_external_float32, const TypeModifiers* type_modifier) {
  std::string type_name;

  if (type_modifier != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(type_name,
                     property_type.value_type->TypeNameWithModifiers(
                         *type_modifier, mode, use_external_float32));
  } else {
    type_name =
        property_type.value_type->ShortTypeName(mode, use_external_float32);
  }
  return absl::StrCat(ToIdentifierLiteral(property_type.name), " ",
                      std::move(type_name));
}

GraphElementType::GraphElementType(
    const internal::GraphReference* graph_reference, ElementKind element_kind,
    const TypeFactory* factory,
    absl::flat_hash_set<PropertyType> property_types, int nesting_depth,
    bool is_dynamic)
    : ListBackedType(factory, TYPE_GRAPH_ELEMENT),
      graph_reference_(graph_reference),
      element_kind_(element_kind),
      property_types_(SortPropertyTypes(std::move(property_types))),
      nesting_depth_(nesting_depth),
      is_dynamic_(is_dynamic) {}

absl::Span<const std::string> GraphElementType::graph_reference() const {
  return graph_reference_->path;
}

const GraphElementType::PropertyType* GraphElementType::FindPropertyType(
    absl::string_view name) const {
  return FindPropertyTypeImpl(name, /*found_idx=*/nullptr);
}

const GraphElementType::PropertyType* GraphElementType::FindPropertyTypeImpl(
    absl::string_view name, int* found_idx) const {
  if (found_idx != nullptr) {
    *found_idx = -1;
  }
  if (ABSL_PREDICT_FALSE(property_types_.empty())) {
    return nullptr;
  }
  int property_type_index;
  {
    // Lazily builds property name to index map.
    absl::MutexLock lock(&mutex_);
    if (ABSL_PREDICT_FALSE(property_name_to_index_map_.empty())) {
      for (int i = 0; i < property_types_.size(); ++i) {
        property_name_to_index_map_.emplace(property_types_.at(i).name, i);
      }
    }
    const auto iter = property_name_to_index_map_.find(name);
    if (ABSL_PREDICT_FALSE(iter == property_name_to_index_map_.end())) {
      return nullptr;
    }
    property_type_index = iter->second;
  }

  if (found_idx != nullptr) {
    *found_idx = property_type_index;
  }
  return &property_types_[property_type_index];
}

bool GraphElementType::IsSupportedType(
    const LanguageOptions& language_options) const {
  if (!language_options.LanguageFeatureEnabled(FEATURE_SQL_GRAPH)) {
    return false;
  }
  if (is_dynamic() &&
      (!language_options.LanguageFeatureEnabled(FEATURE_JSON_TYPE) ||
       !language_options.LanguageFeatureEnabled(
           FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE))) {
    return false;
  }

  // A graph element is only supported when all of its static property value
  // types are supported.
  return absl::c_all_of(
      property_types_, [&](const PropertyType& property_type) {
        return property_type.value_type->IsSupportedType(language_options);
      });
}

bool GraphElementType::CoercibleTo(const GraphElementType* to) const {
  // No coercion between element kinds or across graphs.
  if (Equals(to)) {
    return true;
  }
  bool contains_all_static_properties_in_from_type =
      absl::c_all_of(property_types(), [&](const auto& from_pt) {
        const auto* to_pt = to->FindPropertyType(from_pt.name);
        return to_pt != nullptr &&
               from_pt.value_type->Equals(to_pt->value_type);
      });
  bool is_dynamic_to_static = is_dynamic() && !to->is_dynamic();
  return absl::c_equal(graph_reference(), to->graph_reference(),
                       googlesql_base::CaseEqual) &&
         element_kind() == to->element_kind() &&
         contains_all_static_properties_in_from_type && !is_dynamic_to_static;
}

bool GraphElementType::EqualsForSameKind(const Type* that,
                                         bool equivalent) const {
  const GraphElementType* other = that->AsGraphElement();
  ABSL_DCHECK(other);
  return GraphElementType::EqualsImpl(this, other, equivalent);
}

void GraphElementType::DebugStringImpl(bool /* details */,
                                       TypeOrStringVector* stack,
                                       std::string* debug_string) const {
  absl::StrAppend(debug_string, ElementKindName(element_kind()), "(",
                  *graph_reference_->path_string, ")<");
  stack->push_back(">");
  // Loops property types backwards.
  if (is_dynamic_) {
    stack->push_back("DYNAMIC");
    if (!property_types_.empty()) {
      stack->push_back(", ");
    }
  }
  for (auto itr = property_types_.rbegin(); itr != property_types_.rend();
       ++itr) {
    // No trailing coma.
    if (itr != property_types_.rbegin()) {
      stack->push_back(", ");
    }
    stack->push_back(itr->value_type);
    stack->push_back(absl::StrCat(ToIdentifierLiteral(itr->name), " "));
  }
}

bool GraphElementType::SupportsGroupingImpl(
    const LanguageOptions& language_options,
    const Type** no_grouping_type) const {
  return true;
}

bool GraphElementType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  return true;
}

bool GraphElementType::SupportsOrdering(const LanguageOptions& language_options,
                                        std::string* type_description) const {
  if (type_description != nullptr) {
    *type_description =
        TypeKindToString(kind(), language_options.product_mode());
  }
  return false;
}

bool GraphElementType::SupportsEquality() const { return true; }

bool GraphElementType::UsingFeatureV12CivilTimeType() const {
  return absl::c_any_of(property_types_, [](const PropertyType& property_type) {
    return property_type.value_type->UsingFeatureV12CivilTimeType();
  });
}

absl::Status GraphElementType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  GraphElementTypeProto* graph_element_type_proto =
      type_proto->mutable_graph_element_type();
  graph_element_type_proto->mutable_graph_reference()->Add(
      graph_reference_->path.begin(), graph_reference_->path.end());
  graph_element_type_proto->set_kind(TypeProtoElementKind(element_kind_));
  for (const PropertyType& property_type : property_types_) {
    auto* property_type_proto = graph_element_type_proto->add_property_type();
    property_type_proto->set_name(property_type.name);
    GOOGLESQL_RETURN_IF_ERROR(property_type.value_type
                        ->SerializeToProtoAndDistinctFileDescriptorsImpl(
                            options, property_type_proto->mutable_value_type(),
                            file_descriptor_set_map));
  }
  graph_element_type_proto->set_is_dynamic(is_dynamic_);
  return absl::OkStatus();
}

absl::StatusOr<std::string> GraphElementType::TypeNameImpl(
    size_t property_type_limit,
    absl::FunctionRef<absl::StatusOr<std::string>(const PropertyType&, size_t)>
        property_type_name_fn) const {
  std::string ret;
  absl::StrAppend(&ret, ElementKindName(element_kind()), "(",
                  *graph_reference_->path_string, ")<");

  const size_t num_property_types_to_show =
      std::min(property_type_limit, property_types_.size());

  // Appends up to <num_property_types_to_show> property type names.
  for (size_t i = 0; i < num_property_types_to_show; ++i) {
    const PropertyType& property_type = property_types_.at(i);
    GOOGLESQL_ASSIGN_OR_RETURN(std::string property_type_name,
                     property_type_name_fn(property_type, i));
    absl::StrAppend(&ret, i > 0 ? ", " : "", std::move(property_type_name));
  }

  if (num_property_types_to_show < property_types_.size()) {
    absl::StrAppend(&ret, ", ...");
  }
  if (is_dynamic_) {
    absl::StrAppend(&ret, ", DYNAMIC");
  }
  absl::StrAppend(&ret, ">");
  return ret;
}

std::string GraphElementType::ShortTypeName(ProductMode mode,
                                            bool use_external_float32) const {
  return TypeNameImpl(kShortTypeNameNumPropertyTypeLimit,
                      [&](const PropertyType& property_type, size_t /*index*/) {
                        return MakePropertyTypeName(property_type, mode,
                                                    use_external_float32);
                      })
      .value_or("");
}

std::string GraphElementType::TypeName(ProductMode mode,
                                       bool use_external_float32) const {
  return TypeNameImpl(std::numeric_limits<int>::max(),
                      [&](const PropertyType& property_type, size_t /*index*/) {
                        return MakePropertyTypeName(property_type, mode,
                                                    use_external_float32);
                      })
      .value_or("");
}

absl::StatusOr<std::string> GraphElementType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  const TypeParameters& type_params = type_modifiers.type_parameters();
  if (!type_params.IsEmpty() &&
      (type_params.num_children() != property_types_.size())) {
    return MakeSqlError() << "Input type parameter does not correspond to this "
                             "GraphElementType: "
                          << DebugString();
  }
  const Collation& collation = type_modifiers.collation();
  if (!collation.HasCompatibleStructure(this)) {
    return MakeSqlError() << "Input collation " << collation.DebugString()
                          << " is not compatible with type " << DebugString();
  }
  GOOGLESQL_RET_CHECK(collation.Empty() ||
            collation.num_children() == property_types_.size());
  const auto property_type_name_fn = [&](const PropertyType& property_type,
                                         int field_index) {
    const auto modifiers = TypeModifiers::MakeTypeModifiers(
        type_params.IsEmpty() ? TypeParameters()
                              : type_params.child(field_index),
        collation.Empty() ? Collation() : collation.child(field_index));
    return MakePropertyTypeName(property_type, mode, use_external_float32,
                                &modifiers);
  };
  return TypeNameImpl(std::numeric_limits<int>::max(), property_type_name_fn);
}

std::string GraphElementType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_GRAPH_ELEMENT);  // Crash OK
  return AsGraphElement()->IsNode() ? "GraphNode" : "GraphEdge";
}

Type::HasFieldResult GraphElementType::HasFieldImpl(
    const std::string& name, int* field_id, bool include_pseudo_fields) const {
  return FindPropertyTypeImpl(name, field_id) == nullptr ? HAS_NO_FIELD
                                                         : HAS_FIELD;
}

bool GraphElementType::HasAnyFields() const { return !property_types_.empty(); }

int64_t GraphElementType::GetEstimatedOwnedMemoryBytesSize() const {
  int64_t result = sizeof(*this);

  for (const PropertyType& property_type : property_types_) {
    result += GetEstimatedPropertyTypeOwnedMemoryBytesSize(property_type);
  }

  // Map property_name_to_index_map_ is built lazily, we account its memory
  // in advance, which potentially can lead to overestimation.
  int64_t properties_to_load =
      property_types_.size() - property_name_to_index_map_.size();
  if (properties_to_load < 0) {
    properties_to_load = 0;
  }
  result += internal::GetExternallyAllocatedMemoryEstimate(
      property_name_to_index_map_, properties_to_load);
  return result;
}

bool GraphElementType::EqualsImpl(const GraphElementType* type1,
                                  const GraphElementType* type2,
                                  bool equivalent) {
  bool has_equal_static_properties = absl::c_equal(
      type1->property_types(), type2->property_types(),
      [&](const auto& lhs, const auto& rhs) {
        // Property type name is case insensitive.
        return googlesql_base::CaseEqual(lhs.name, rhs.name) &&
               lhs.value_type->EqualsImpl(rhs.value_type, equivalent);
      });
  return absl::c_equal(type1->graph_reference(), type2->graph_reference(),
                       googlesql_base::CaseEqual) &&
         type1->is_dynamic() == type2->is_dynamic() &&
         type1->element_kind() == type2->element_kind() &&
         has_equal_static_properties;
}

absl::HashState GraphElementType::HashTypeParameter(
    absl::HashState state) const {
  return absl::HashState::combine(
      std::move(state),
      is_dynamic_,
      element_kind_, property_types_);
}

void GraphElementType::CopyValueContent(const ValueContent& from,
                                        ValueContent* to) const {
  from.GetAs<internal::ValueContentOrderedListRef*>()->Ref();
  *to = from;
}

void GraphElementType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentOrderedListRef*>()->Unref();
}

absl::HashState GraphElementType::HashValueContent(
    const ValueContent& value, absl::HashState state) const {
  const internal::GraphElementContainer* container =
      GetGraphElementContainer(value);
  state = absl::HashState::combine(std::move(state),
                                   *this->graph_reference_->path_string,
                                   container->GetIdentifier());
  return state;
}

bool GraphElementType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::GraphElementContainer* x_container =
      GetGraphElementContainer(x);
  const internal::GraphElementContainer* y_container =
      GetGraphElementContainer(y);
  if (x_container->GetIdentifier() != y_container->GetIdentifier()) {
    return false;
  }

  return true;
}

bool GraphElementType::ValueContentLess(const ValueContent& x,
                                        const ValueContent& y,
                                        const Type* other_type) const {
  return GetGraphElementContainer(x)->GetIdentifier() <
         GetGraphElementContainer(y)->GetIdentifier();
}

void GraphElementType::FormatValueContentDebugModeImpl(
    const ValueContent& content, const FormatValueContentOptions& options,
    std::string* result) const {
  const internal::GraphElementContainer* container =
      GetGraphElementContainer(content);

  if (options.verbose) {
    absl::StrAppend(result, CapitalizedName(), "{");

    // Adds name, identifier, labels, source/dest node identifier (for edge),
    // is_dynamic, in verbose mode.
    absl::StrAppend(result,
                    "$name:", ToStringLiteral(container->GetDefinitionName()),
                    ", ");

    absl::StrAppend(result, "$id:", ToBytesLiteral(container->GetIdentifier()),
                    ", ");
    absl::StrAppend(
        result, "$labels:[",
        absl::StrJoin(container->GetLabels(), ", ",
                      [](std::string* out, absl::string_view label) {
                        absl::StrAppend(out, ToStringLiteral(label));
                      }),
        "], ");
    if (IsEdge()) {
      absl::StrAppend(result, "$source_node_id:",
                      ToBytesLiteral(container->GetSourceNodeIdentifier()),
                      ", $dest_node_id:",
                      ToBytesLiteral(container->GetDestNodeIdentifier()), ", ");
    }
    absl::StrAppend(result, "$is_dynamic:", is_dynamic_, ", ");
  } else {
    absl::StrAppend(result, "{");
  }

  for (int i = 0; i < property_types_.size(); ++i) {
    const internal::NullableValueContent& element_value_content =
        container->element(i);
    if (i > 0) {
      absl::StrAppend(result, ", ");
    }

    const PropertyType& current_property = property_types_[i];

    absl::StrAppend(result, current_property.name, ":");
    absl::StrAppend(result, DebugFormatNullableValueContentForContainer(
                                element_value_content,
                                current_property.value_type, options));
  }
  absl::StrAppend(result, "}");

  if (options.verbose) {
    absl::StrAppend(result, "\n property_name_to_index: {\n");
    for (const auto& [property_name, element_index] :
         container->GetValidPropertyNameToIndexMap()) {
      absl::StrAppend(result, "  ", property_name, ": ", element_index, "\n");
    }
    absl::StrAppend(result, " }");
  }
}

std::string GraphElementType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  std::string result;

  switch (options.mode) {
    case Type::FormatValueContentOptions::Mode::kDebug:
      FormatValueContentDebugModeImpl(value, options, &result);
      return result;
    case Type::FormatValueContentOptions::Mode::kSQLLiteral:
    case Type::FormatValueContentOptions::Mode::kSQLExpression:
      ABSL_LOG(ERROR) << "No SQL expression or literal for graph element";
      return "()";
  }
}

absl::Status GraphElementType::SerializeValueContent(
    const ValueContent& value, ValueProto* value_proto) const {
  return absl::UnimplementedError(
      "SerializeValueContent not yet implemented for GraphElementType");
}

absl::Status GraphElementType::DeserializeValueContent(
    const ValueProto& value_proto, ValueContent* value) const {
  return absl::UnimplementedError(
      "DeserializeValueContent not yet implemented for GraphElementType");
}

}  // namespace googlesql
