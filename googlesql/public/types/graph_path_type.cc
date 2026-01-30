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

#include "googlesql/public/types/graph_path_type.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value_content.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {
constexpr size_t kShortTypeNameNumPropertyTypeLimit = 3;

// Gets ValueContentOrderedList from ValueContent.
const internal::ValueContentOrderedList* GetContainer(
    const ValueContent& value_content) {
  return value_content.GetAs<internal::ValueContentOrderedListRef*>()
      ->value()
      ->GetAs<internal::ValueContentOrderedList>();
}
}  // namespace

GraphPathType::GraphPathType(const TypeFactory* factory,
                             const GraphElementType* node_type,
                             const GraphElementType* edge_type,
                             int nesting_depth)
    : ListBackedType(factory, TYPE_GRAPH_PATH),
      node_type_(node_type),
      edge_type_(edge_type),
      nesting_depth_(nesting_depth) {}

bool GraphPathType::IsSupportedType(
    const LanguageOptions& language_options) const {
  if (!language_options.LanguageFeatureEnabled(FEATURE_SQL_GRAPH) ||
      !language_options.LanguageFeatureEnabled(FEATURE_SQL_GRAPH_PATH_TYPE)) {
    return false;
  }

  return node_type_->IsSupportedType(language_options) &&
         edge_type_->IsSupportedType(language_options);
}

bool GraphPathType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const GraphPathType* other = that->AsGraphPath();
  ABSL_DCHECK(other);
  return GraphPathType::EqualsImpl(this, other, equivalent);
}

void GraphPathType::DebugStringImpl(bool /* details */,
                                    TypeOrStringVector* stack,
                                    std::string* debug_string) const {
  absl::StrAppend(debug_string, "PATH<");
  stack->push_back(">");
  stack->push_back(edge_type_);
  stack->push_back("edge: ");
  stack->push_back(", ");
  stack->push_back(node_type_);
  stack->push_back("node: ");
}

bool GraphPathType::SupportsGroupingImpl(
    const LanguageOptions& language_options,
    const Type** no_grouping_type) const {
  if (language_options.LanguageFeatureEnabled(FEATURE_GROUP_BY_GRAPH_PATH)) {
    return true;
  }
  if (no_grouping_type != nullptr) {
    *no_grouping_type = this;
  }
  return false;
}

bool GraphPathType::SupportsPartitioningImpl(
    const LanguageOptions& language_options,
    const Type** no_partitioning_type) const {
  if (language_options.LanguageFeatureEnabled(FEATURE_GROUP_BY_GRAPH_PATH)) {
    return true;
  }
  if (no_partitioning_type != nullptr) {
    *no_partitioning_type = this;
  }
  return false;
}

bool GraphPathType::SupportsOrdering(const LanguageOptions& language_options,
                                     std::string* type_description) const {
  if (type_description != nullptr) {
    *type_description =
        TypeKindToString(kind(), language_options.product_mode());
  }
  return false;
}

bool GraphPathType::SupportsEquality() const { return true; }

bool GraphPathType::UsingFeatureV12CivilTimeType() const {
  return node_type_->UsingFeatureV12CivilTimeType() ||
         edge_type_->UsingFeatureV12CivilTimeType();
}

absl::Status GraphPathType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  GraphPathTypeProto& graph_path_type_proto =
      *type_proto->mutable_graph_path_type();
  TypeProto node_type_proto;
  GOOGLESQL_RETURN_IF_ERROR(node_type_->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, &node_type_proto, file_descriptor_set_map));
  *graph_path_type_proto.mutable_node_type() =
      node_type_proto.graph_element_type();
  TypeProto edge_type_proto;
  GOOGLESQL_RETURN_IF_ERROR(edge_type_->SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, &edge_type_proto, file_descriptor_set_map));
  *graph_path_type_proto.mutable_edge_type() =
      edge_type_proto.graph_element_type();
  return absl::OkStatus();
}

absl::StatusOr<std::string> GraphPathType::TypeNameImpl(
    size_t property_type_limit, ProductMode mode,
    bool use_external_float32) const {
  auto property_type_name_fn = [&](const PropertyType& property_type,
                                   size_t /*index*/) {
    return MakePropertyTypeName(property_type, mode, use_external_float32);
  };
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string node_name,
      node_type_->TypeNameImpl(property_type_limit, property_type_name_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string edge_name,
      edge_type_->TypeNameImpl(property_type_limit, property_type_name_fn));
  return absl::StrCat("PATH<", node_name, ", ", edge_name, ">");
}

std::string GraphPathType::ShortTypeName(ProductMode mode,
                                         bool use_external_float32) const {
  return TypeNameImpl(kShortTypeNameNumPropertyTypeLimit, mode,
                      use_external_float32)
      .value_or("");
}

std::string GraphPathType::TypeName(ProductMode mode,
                                    bool use_external_float32) const {
  return TypeNameImpl(std::numeric_limits<int>::max(), mode,
                      use_external_float32)
      .value_or("");
}

absl::StatusOr<std::string> GraphPathType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::string node_name,
                   node_type_->TypeNameWithModifiers(type_modifiers, mode,
                                                     use_external_float32));
  GOOGLESQL_ASSIGN_OR_RETURN(std::string edge_name,
                   edge_type_->TypeNameWithModifiers(type_modifiers, mode,
                                                     use_external_float32));
  return absl::StrCat("PATH<", node_name, ", ", edge_name, ">");
}

std::string GraphPathType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_GRAPH_PATH);  // Crash OK
  return "GraphPath";
}

int64_t GraphPathType::GetEstimatedOwnedMemoryBytesSize() const {
  int64_t result = sizeof(*this);
  result += node_type_->GetEstimatedOwnedMemoryBytesSize();
  result += edge_type_->GetEstimatedOwnedMemoryBytesSize();
  return result;
}

bool GraphPathType::EqualsImpl(const GraphPathType* type1,
                               const GraphPathType* type2, bool equivalent) {
  return GraphElementType::EqualsImpl(type1->node_type_, type2->node_type_,
                                      equivalent) &&
         GraphElementType::EqualsImpl(type1->edge_type_, type2->edge_type_,
                                      equivalent);
}

absl::HashState GraphPathType::HashTypeParameter(absl::HashState state) const {
  return absl::HashState::combine(std::move(state), *node_type_, *edge_type_);
}

void GraphPathType::CopyValueContent(const ValueContent& from,
                                     ValueContent* to) const {
  from.GetAs<internal::ValueContentOrderedListRef*>()->Ref();
  *to = from;
}

void GraphPathType::ClearValueContent(const ValueContent& value) const {
  value.GetAs<internal::ValueContentOrderedListRef*>()->Unref();
}

absl::HashState GraphPathType::HashValueContent(const ValueContent& value,
                                                absl::HashState state) const {
  const internal::ValueContentOrderedList* path = GetContainer(value);
  for (int i = 0; i < path->num_elements(); ++i) {
    NullableValueContentHasher hasher(GetElementType(i));
    state =
        absl::HashState::combine(std::move(state), hasher(path->element(i)));
  }
  return state;
}

bool GraphPathType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const internal::ValueContentOrderedList* x_path = GetContainer(x);
  const internal::ValueContentOrderedList* y_path = GetContainer(y);
  if (x_path->num_elements() != y_path->num_elements()) {
    return false;
  }
  for (int i = 0; i < x_path->num_elements(); ++i) {
    ABSL_DCHECK(!x_path->element(i).is_null());
    ABSL_DCHECK(!y_path->element(i).is_null());
    if (!GetElementType(i)->ValueContentEquals(
            x_path->element(i).value_content(),
            y_path->element(i).value_content(), options)) {
      return false;
    }
  }
  return true;
}

bool GraphPathType::ValueContentLess(const ValueContent& x,
                                     const ValueContent& y,
                                     const Type* other_type) const {
  const GraphPathType* other_path_type = other_type->AsGraphPath();
  ABSL_DCHECK(other_path_type != nullptr);
  const internal::ValueContentOrderedList* x_path = GetContainer(x);
  const internal::ValueContentOrderedList* y_path = GetContainer(y);
  if (x_path->num_elements() != y_path->num_elements()) {
    return x_path->num_elements() < y_path->num_elements();
  }
  for (int i = 0; i < x_path->num_elements(); ++i) {
    ABSL_DCHECK(!x_path->element(i).is_null());
    ABSL_DCHECK(!y_path->element(i).is_null());
    if (GetElementType(i)->ValueContentLess(
            x_path->element(i).value_content(),
            y_path->element(i).value_content(),
            other_path_type->GetElementType(i))) {
      return true;
    } else if (other_path_type->GetElementType(i)->ValueContentLess(
                   y_path->element(i).value_content(),
                   x_path->element(i).value_content(), GetElementType(i))) {
      return false;
    }
  }
  return false;
}

void GraphPathType::FormatValueContentDebugModeImpl(
    const internal::ValueContentOrderedList* container,
    const FormatValueContentOptions& options, std::string* result) const {
  if (options.verbose) {
    absl::StrAppend(result, CapitalizedName());
  }
  absl::StrAppend(result, "{");

  for (int i = 0; i < container->num_elements(); ++i) {
    const internal::NullableValueContent& element_value_content =
        container->element(i);
    if (i > 0) {
      absl::StrAppend(result, ", ");
    }

    absl::StrAppend(result, i % 2 == 0 ? "node" : "edge", ":");
    absl::StrAppend(result,
                    DebugFormatNullableValueContentForContainer(
                        element_value_content, GetElementType(i), options));
  }

  absl::StrAppend(result, "}");
}

std::string GraphPathType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!ThreadHasEnoughStack()) {
    return std::string(kFormatValueContentOutOfStackError);
  }

  const internal::ValueContentOrderedList* container =
      value.GetAs<internal::ValueContentOrderedListRef*>()->value();
  std::string result;

  switch (options.mode) {
    case Type::FormatValueContentOptions::Mode::kDebug:
      FormatValueContentDebugModeImpl(container, options, &result);
      return result;
    case Type::FormatValueContentOptions::Mode::kSQLLiteral:
    case Type::FormatValueContentOptions::Mode::kSQLExpression:
      ABSL_LOG(ERROR) << "No SQL expression or literal for graph path";
      return "()";
  }
}

const Type* GraphPathType::GetElementType(int index) const {
  if (index % 2 == 0) {
    return node_type_;
  } else {
    return edge_type_;
  }
}

absl::Status GraphPathType::SerializeValueContent(
    const ValueContent& value, ValueProto* value_proto) const {
  return absl::UnimplementedError(
      "SerializeValueContent not yet implemented for GraphPathType");
}

absl::Status GraphPathType::DeserializeValueContent(
    const ValueProto& value_proto, ValueContent* value) const {
  return absl::UnimplementedError(
      "DeserializeValueContent not yet implemented for GraphPathType");
}

bool GraphPathType::CoercibleTo(const GraphPathType* to) const {
  return node_type_->CoercibleTo(to->node_type_) &&
         edge_type_->CoercibleTo(to->edge_type_);
}

}  // namespace googlesql
