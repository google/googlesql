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

#ifndef GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_
#define GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/json_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
namespace googlesql {

class Function;
class ResolvedExpr;
class ResolvedGraphGetElementProperty;

// Helper function that is used to determine a type is or contains in its
// nesting structure a GraphElement or GraphPath type.
bool TypeIsOrContainsGraphElement(const Type* type);

// Helper function that returns the property name from a get element property
// node.
absl::StatusOr<std::string> GetPropertyName(
    const ResolvedGraphGetElementProperty* node);

// Helper function that merges a list of properties to a JSON value.
// The JSON value is used to represent dynamic properties of a dynamic graph
// element.
absl::StatusOr<JSONValue> MakePropertiesJsonValue(
    absl::Span<Value::Property> properties,
    const LanguageOptions& language_options);

// Returns true if any dynamic property specifications is found.
// Extracts all conjuncts from `filter_expr` that are either dynamic or static
// property specifications into `property_specifications`. Place the rest of
// the conjuncts into `remaining_conjuncts`.
absl::StatusOr<bool> ContainsDynamicPropertySpecification(
    const ResolvedExpr* filter_expr,
    std::vector<const ResolvedExpr*>& property_specifications,
    std::vector<const ResolvedExpr*>& remaining_conjuncts);

// Returns a vector of property name and value pairs from `exprs`.
// REQUIRES: each expr in `exprs` can be converted to either dynamic or static
// property specifications.
absl::StatusOr<std::vector<std::pair<std::string, const ResolvedExpr*>>>
ToPropertySpecifications(std::vector<const ResolvedExpr*> exprs);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_
