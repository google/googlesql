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

#include "googlesql/reference_impl/functions/graph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/functions/json.h"
#include "googlesql/public/functions/json_format.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// TODO: Update NULL handling for SOURCE/DEST/ALL_DIFFERENT/SAME
// when NULL graph elements are supported.
absl::Status CheckNonNullArguments(absl::string_view func_or_op_name,
                                   absl::Span<const Value> args) {
  if (absl::c_any_of(args, [](const Value& x) { return x.is_null(); })) {
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "Input to the " << func_or_op_name << " must not be null";
  }
  return absl::OkStatus();
}

static absl::Status AppendComponentToPath(const Value& value,
                                          const GraphPathType* path_type,
                                          EvaluationContext* context,
                                          std::vector<Value>& components) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      Value casted,
      internal::CastValueWithoutTypeValidation(
          value, context->GetDefaultTimeZone(),
          absl::FromUnixMicros(context->GetCurrentTimestamp()),
          context->GetLanguageOptions(),
          value.IsNode() ? path_type->node_type() : path_type->edge_type(),
          /*format=*/{},
          /*time_zone=*/{}, /*extended_conversion_evaluator=*/nullptr,
          /*canonicalize_zero=*/true));
  if (components.empty() || components.back().IsEdge() || casted.IsEdge()) {
    // If there are back to back nodes we don't need the second
    // node.
    components.push_back(casted);
  }
  return absl::OkStatus();
};

static std::vector<Value> GetNodesFromPath(const Value& path) {
  std::vector<Value> nodes;
  for (const Value& component : path.graph_elements()) {
    if (component.IsNode()) {
      nodes.push_back(component);
    }
  }
  return nodes;
}

class SameGraphElementFunction : public SimpleBuiltinScalarFunction {
 public:
  SameGraphElementFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class AllDifferentGraphElementFunction : public SimpleBuiltinScalarFunction {
 public:
  AllDifferentGraphElementFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class SourceDestPredicateFunction : public SimpleBuiltinScalarFunction {
 public:
  SourceDestPredicateFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PropertyExistsFunction : public SimpleBuiltinScalarFunction {
 public:
  PropertyExistsFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class LabelsFunction : public SimpleBuiltinScalarFunction {
 public:
  LabelsFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PropertyNamesFunction : public SimpleBuiltinScalarFunction {
 public:
  PropertyNamesFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ElementDefinitionNameFunction : public SimpleBuiltinScalarFunction {
 public:
  ElementDefinitionNameFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ElementIdFunction : public SimpleBuiltinScalarFunction {
 public:
  ElementIdFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PathLengthFunction : public SimpleBuiltinScalarFunction {
 public:
  PathLengthFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PathElementsFunction : public SimpleBuiltinScalarFunction {
 public:
  PathElementsFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PathFirstLastFunction : public SimpleBuiltinScalarFunction {
 public:
  PathFirstLastFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PathCreateFunction : public SimpleBuiltinScalarFunction {
 public:
  PathCreateFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class PathConcatFunction : public SimpleBuiltinScalarFunction {
 public:
  PathConcatFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class IsTrailFunction : public SimpleBuiltinScalarFunction {
 public:
  IsTrailFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class IsAcyclicFunction : public SimpleBuiltinScalarFunction {
 public:
  IsAcyclicFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Defines an executable scalar function body in the reference implementation
// for the `IS_SIMPLE(path) -> bool` function.
class IsSimpleFunction : public SimpleBuiltinScalarFunction {
 public:
  IsSimpleFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  // Evaluates the function `IS_SIMPLE(path) -> bool` on the given arguments.
  // Returns a Value of type bool if successful, or a status if a runtime error
  // occurs.
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Returns true if the dynamic property value can be converted to the target
// value type and the converted value equals to the expected value.
class DynamicPropertyEqualsFunction : public SimpleBuiltinScalarFunction {
 public:
  DynamicPropertyEqualsFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> SameGraphElementFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2);
  GOOGLESQL_RETURN_IF_ERROR(CheckNonNullArguments("function SAME", args));
  GOOGLESQL_RET_CHECK(absl::c_all_of(
      args, [](const Value& x) { return x.type()->IsGraphElement(); }));
  bool output =
      !(absl::c_adjacent_find(args, [](const Value& x, const Value& y) {
          return x.GetIdentifier() != y.GetIdentifier();
        }) != args.end());
  return Value::Bool(output);
}

absl::StatusOr<Value> AllDifferentGraphElementFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2);
  GOOGLESQL_RETURN_IF_ERROR(CheckNonNullArguments("function ALL_DIFFERENT", args));
  GOOGLESQL_RET_CHECK(absl::c_all_of(
      args, [](const Value& x) { return x.type()->IsGraphElement(); }));
  absl::flat_hash_set<absl::string_view> hashed_identifier;
  hashed_identifier.reserve(args.size());
  for (const Value& arg : args) {
    hashed_identifier.insert(arg.GetIdentifier());
  }
  return Value::Bool(hashed_identifier.size() == args.size());
}

absl::StatusOr<Value> SourceDestPredicateFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  GOOGLESQL_RETURN_IF_ERROR(CheckNonNullArguments(kind() == FunctionKind::kIsSourceNode
                                            ? "operator SOURCE"
                                            : "operator DEST",
                                        args));
  const Value& node_value = args[0];
  const Value& edge_value = args[1];
  GOOGLESQL_RET_CHECK(node_value.type()->IsGraphElement() && node_value.IsNode());
  GOOGLESQL_RET_CHECK(edge_value.type()->IsGraphElement() && edge_value.IsEdge());

  bool result;
  switch (kind()) {
    case FunctionKind::kIsSourceNode:
      result =
          edge_value.GetSourceNodeIdentifier() == node_value.GetIdentifier();
      break;
    case FunctionKind::kIsDestNode:
      result = edge_value.GetDestNodeIdentifier() == node_value.GetIdentifier();
      break;
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unexpected function: " << debug_name();
  }
  return Value::Bool(result);
}

absl::StatusOr<Value> PropertyExistsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  const Value& graph_element_value = args[0];
  const Value& property_name = args[1];
  GOOGLESQL_RET_CHECK(graph_element_value.type()->IsGraphElement());
  GOOGLESQL_RET_CHECK(property_name.type()->IsString());
  GOOGLESQL_RET_CHECK(!property_name.is_null());
  if (graph_element_value.is_null()) {
    return Value::Null(output_type());
  }
  absl::Status s =
      graph_element_value
          .FindValidPropertyValueByName(property_name.string_value())
          .status();
  if (absl::IsNotFound(s)) {
    return Value::Bool(false);
  }
  GOOGLESQL_RETURN_IF_ERROR(s);
  return Value::Bool(true);
}

absl::StatusOr<Value> LabelsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  const Value& graph_element_value = args[0];
  GOOGLESQL_RET_CHECK(graph_element_value.type()->IsGraphElement());
  if (graph_element_value.is_null()) {
    return Value::Null(output_type());
  }
  auto labels = graph_element_value.GetLabels();
  std::vector<Value> label_values;
  label_values.reserve(labels.size());
  for (const auto& label : labels) {
    label_values.push_back(Value::String(label));
  }
  return Value::MakeArray(types::StringArrayType(), label_values);
}

absl::StatusOr<Value> PropertyNamesFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  const Value& graph_element_value = args[0];
  GOOGLESQL_RET_CHECK(graph_element_value.type()->IsGraphElement());
  if (graph_element_value.is_null()) {
    return Value::Null(output_type());
  }

  // Property names are sorted case-insensitively.
  std::vector<std::string> property_names =
      graph_element_value.property_names();
  std::vector<Value> property_names_values;
  property_names_values.reserve(property_names.size());
  absl::c_sort(property_names, googlesql_base::CaseLess());
  for (const auto& prop : property_names) {
    property_names_values.push_back(Value::String(prop));
  }
  return Value::MakeArray(types::StringArrayType(), property_names_values);
}

absl::StatusOr<Value> ElementDefinitionNameFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  const Value& graph_element_value = args[0];
  GOOGLESQL_RET_CHECK(graph_element_value.type()->IsGraphElement());
  if (graph_element_value.is_null()) {
    return Value::Null(output_type());
  }
  return Value::String(graph_element_value.GetDefinitionName());
}

static absl::StatusOr<Value> ConvertBytes(absl::string_view input) {
  std::string out;
  absl::Status error;
  functions::JsonFromBytes(input, &out, /*quote_output_string=*/false);
  return Value::String(out);
}

absl::StatusOr<Value> ElementIdFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  const Value& graph_element_value = args[0];
  GOOGLESQL_RET_CHECK(graph_element_value.type()->IsGraphElement());
  if (graph_element_value.is_null()) {
    return Value::Null(output_type());
  }
  switch (kind()) {
    case FunctionKind::kElementId:
      return ConvertBytes(graph_element_value.GetIdentifier());
    case FunctionKind::kSourceNodeId:
      return ConvertBytes(graph_element_value.GetSourceNodeIdentifier());
    case FunctionKind::kDestinationNodeId:
      return ConvertBytes(graph_element_value.GetDestNodeIdentifier());
    default: {
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unexpected function: " << debug_name();
    }
  }
}

absl::StatusOr<Value> PathLengthFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  if (path.is_null()) {
    return Value::Null(output_type());
  }
  return Value::Int64(path.num_graph_elements() / 2);
}

absl::StatusOr<Value> PathElementsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  if (path.is_null()) {
    return Value::Null(output_type());
  }

  bool is_nodes = kind() == FunctionKind::kPathNodes;
  std::vector<Value> elements;
  for (const Value& element : path.graph_elements()) {
    GOOGLESQL_RET_CHECK(element.type()->IsGraphElement());
    if (is_nodes == element.IsNode()) {
      elements.push_back(element);
    }
  }
  GOOGLESQL_RET_CHECK(output_type()->IsArray());
  return Value::MakeArray(output_type()->AsArray(), std::move(elements));
}

absl::StatusOr<Value> PathFirstLastFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  GOOGLESQL_RET_CHECK(output_type()->IsGraphElement());
  if (path.is_null()) {
    return Value::Null(output_type());
  }

  GOOGLESQL_RET_CHECK_GT(path.num_graph_elements(), 0);
  switch (kind()) {
    case FunctionKind::kPathFirst:
      return path.graph_element(0);
    case FunctionKind::kPathLast:
      return path.graph_element(path.num_graph_elements() - 1);
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function: " << debug_name();
  }
}

absl::StatusOr<Value> PathCreateFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 1);
  GOOGLESQL_RET_CHECK_EQ(args.size() % 2, 1);
  GOOGLESQL_RETURN_IF_ERROR(CheckNonNullArguments("function PATH", args));
  GOOGLESQL_RET_CHECK(absl::c_all_of(
      args, [](const Value& arg) { return arg.type()->IsGraphElement(); }));

  const GraphPathType* path_type = output_type()->AsGraphPath();
  GOOGLESQL_RET_CHECK(path_type != nullptr);
  std::vector<Value> path_components;
  path_components.reserve(args.size());
  for (int i = 0; i < args.size(); ++i) {
    const Value& arg = args[i];
    GOOGLESQL_RET_CHECK_EQ(arg.IsNode(), i % 2 == 0);
    switch (kind()) {
      case FunctionKind::kUncheckedPathCreate: {
        break;
      }
      case FunctionKind::kPathCreate: {
        if (i % 2 == 1) {
          GOOGLESQL_RET_CHECK(arg.IsEdge());
          bool forward =
              args[i - 1].GetIdentifier() == arg.GetSourceNodeIdentifier() &&
              args[i + 1].GetIdentifier() == arg.GetDestNodeIdentifier();
          bool backward =
              args[i - 1].GetIdentifier() == arg.GetDestNodeIdentifier() &&
              args[i + 1].GetIdentifier() == arg.GetSourceNodeIdentifier();
          if (!forward && !backward) {
            return ::googlesql_base::OutOfRangeErrorBuilder()
                   << "The arguments to PATH must contain edges that "
                      "connect the previous node to the next node. The edge at "
                      "index "
                   << i << " does not";
          }
        }
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function: " << debug_name();
    }
    GOOGLESQL_RETURN_IF_ERROR(
        AppendComponentToPath(arg, path_type, context, path_components));
  }
  return Value::MakeGraphPath(output_type()->AsGraphPath(),
                              std::move(path_components));
}

absl::StatusOr<Value> PathConcatFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2)
      << "PATH_CONCAT requires at least 2 arguments, but got " << args.size();
  GOOGLESQL_RET_CHECK(absl::c_all_of(args, [](const Value& arg) {
    return arg.type()->IsGraphPath();
  })) << "PATH_CONCAT requires all arguments to be paths";

  const GraphPathType* path_type = output_type()->AsGraphPath();
  GOOGLESQL_RET_CHECK_NE(path_type, nullptr);
  if (absl::c_any_of(args, [](const Value& arg) { return arg.is_null(); })) {
    return Value::Null(path_type);
  }

  std::vector<Value> path_components;
  std::optional<Value> tail_node;
  for (int i = 0; i < args.size(); ++i) {
    const Value& arg = args[i];
    if (tail_node.has_value()) {
      // This is not the first path, check that the head of this path matches
      // the tail of the previous path.
      const Value& head_node = arg.graph_elements().front();
      if (tail_node->GetIdentifier() != head_node.GetIdentifier()) {
        switch (kind()) {
          case FunctionKind::kUncheckedPathConcat: {
            GOOGLESQL_RET_CHECK_FAIL()
                << "Invalid path concat when calling $unchecked_path_concat";
          }
          case FunctionKind::kPathConcat: {
            return googlesql_base::OutOfRangeErrorBuilder()
                   << "Path concatenation requires that the first node of the "
                      "path to be concatenated is equal to the last node of "
                      "the previous path";
            break;
          }
          default:
            GOOGLESQL_RET_CHECK_FAIL() << "Unexpected function: " << debug_name();
        }
      }
    }
    tail_node = arg.graph_elements().back();

    for (int j = 0; j < arg.num_graph_elements(); ++j) {
      const Value& graph_element = arg.graph_element(j);
      GOOGLESQL_RETURN_IF_ERROR(AppendComponentToPath(graph_element, path_type, context,
                                            path_components));
    }
  }
  return Value::MakeGraphPath(output_type()->AsGraphPath(),
                              std::move(path_components));
}

absl::StatusOr<Value> IsTrailFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  GOOGLESQL_RET_CHECK(output_type()->IsBool());
  if (path.is_null()) {
    return Value::Null(output_type());
  }

  absl::flat_hash_set<Value> seen_edges;
  for (int i = 0; i < path.num_graph_elements(); ++i) {
    if (i % 2 == 1) {
      if (!seen_edges.insert(path.graph_element(i)).second) {
        return Value::Bool(false);
      }
    }
  }
  return Value::Bool(true);
}

absl::StatusOr<Value> IsAcyclicFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  GOOGLESQL_RET_CHECK(output_type()->IsBool());
  if (path.is_null()) {
    return Value::Null(output_type());
  }
  absl::flat_hash_set<Value> seen_nodes;
  for (const Value& node : GetNodesFromPath(path)) {
    if (!seen_nodes.insert(node).second) {
      return Value::Bool(false);
    }
  }
  return Value::Bool(true);
}

absl::StatusOr<Value> IsSimpleFunction::Eval(
    absl::Span<const TupleData* const> /*params*/, absl::Span<const Value> args,
    EvaluationContext* /*context*/) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphPath()) << args[0].type()->DebugString();
  const Value& path = args[0];
  GOOGLESQL_RET_CHECK(output_type()->IsBool());
  if (path.is_null()) {
    return Value::Null(output_type());
  }
  if (path.num_graph_elements() <= 3) {
    return Value::Bool(true);
  }

  const Value& first_node = path.graph_elements().front();
  GOOGLESQL_RET_CHECK(first_node.IsNode());
  const Value& last_node = path.graph_elements().back();
  GOOGLESQL_RET_CHECK(last_node.IsNode());
  absl::flat_hash_set<Value> seen_nodes;
  seen_nodes.insert(first_node);
  seen_nodes.insert(last_node);
  for (int i = 2; i < path.num_graph_elements() - 1; i += 2) {
    const Value& node = path.graph_element(i);
    GOOGLESQL_RET_CHECK(node.IsNode());
    if (!seen_nodes.insert(node).second) {
      return Value::Bool(false);
    }
  }
  return Value::Bool(true);
}

absl::StatusOr<Value> ConvertJsonValueToValue(
    functions::WideNumberMode wide_number_mode, ProductMode product_mode,
    JSONValueConstRef json_value, const Type* type, bool is_nested = false) {
  switch (type->kind()) {
    case TYPE_BOOL: {
      GOOGLESQL_ASSIGN_OR_RETURN(bool v, functions::ConvertJsonToBool(json_value));
      return Value::Bool(v);
    }
    case TYPE_INT32: {
      GOOGLESQL_ASSIGN_OR_RETURN(int32_t v, functions::ConvertJsonToInt32(json_value));
      return Value::Int32(v);
    }
    case TYPE_UINT32: {
      GOOGLESQL_ASSIGN_OR_RETURN(uint32_t v, functions::ConvertJsonToUint32(json_value));
      return Value::Uint32(v);
    }
    case TYPE_INT64: {
      GOOGLESQL_ASSIGN_OR_RETURN(int64_t v, functions::ConvertJsonToInt64(json_value));
      return Value::Int64(v);
    }
    case TYPE_UINT64: {
      GOOGLESQL_ASSIGN_OR_RETURN(uint64_t v, functions::ConvertJsonToUint64(json_value));
      return Value::Uint64(v);
    }
    case TYPE_STRING: {
      GOOGLESQL_ASSIGN_OR_RETURN(std::string v,
                       functions::ConvertJsonToString(json_value));
      return Value::String(v);
    }
    case TYPE_FLOAT: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          float v, functions::ConvertJsonToFloat(json_value, wide_number_mode,
                                                 product_mode));
      return Value::Float(v);
    }
    case TYPE_DOUBLE: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          double v, functions::ConvertJsonToDouble(json_value, wide_number_mode,
                                                   product_mode));
      return Value::Double(v);
    }
    case TYPE_ARRAY: {
      if (is_nested) {
        return MakeEvalError() << "Nested arrays are not supported";
      }
      if (json_value.IsArray()) {
        std::vector<Value> gsql_values;
        gsql_values.reserve(json_value.GetArrayElements().size());
        for (const JSONValueConstRef& element : json_value.GetArrayElements()) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              Value gsql_value,
              ConvertJsonValueToValue(wide_number_mode, product_mode, element,
                                      type->AsArray()->element_type(),
                                      /*is_nested=*/true));
          gsql_values.push_back(std::move(gsql_value));
        }
        return Value::MakeArray(type->AsArray(), std::move(gsql_values));
      }
      return MakeEvalError() << "JSON value is not an array";
    }
    default:
      return MakeEvalError() << "Unsupported type: " << type->DebugString();
  }
}

Value SafeConvertJsonValueToValue(functions::WideNumberMode wide_number_mode,
                                  ProductMode product_mode,
                                  JSONValueConstRef json_value,
                                  const Type* type) {
  absl::StatusOr<Value> value =
      ConvertJsonValueToValue(wide_number_mode, product_mode, json_value, type);
  if (!value.ok()) {
    return Value::Null(type);
  }
  return *value;
}

absl::StatusOr<Value> DynamicPropertyEqualsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 3);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphElement());
  GOOGLESQL_RET_CHECK(args[1].type()->IsString());
  Value graph_element = args[0];
  GOOGLESQL_RET_CHECK(graph_element.type()->AsGraphElement()->is_dynamic())
      << "$dynamic_property_equals requires a dynamic graph element";
  if (graph_element.is_null() || args[2].is_null()) {
    return Value::Null(output_type());
  }
  absl::string_view property_name = args[1].string_value();
  GOOGLESQL_RET_CHECK_EQ(
      graph_element.type()->AsGraphElement()->FindPropertyType(property_name),
      nullptr)
      << "property " << property_name << " is static";
  absl::StatusOr<Value> prop_value =
      graph_element.FindValidPropertyValueByName(std::string(property_name));
  if (absl::IsNotFound(prop_value.status())) {
    return Value::Null(output_type());
  }
  GOOGLESQL_RETURN_IF_ERROR(prop_value.status());
  GOOGLESQL_RET_CHECK(prop_value->type()->IsJson());

  const Value& target_value = args[2];
  const LanguageOptions& language_options = context->GetLanguageOptions();
  functions::WideNumberMode wide_number_mode =
      language_options.LanguageFeatureEnabled(
          FEATURE_JSON_STRICT_NUMBER_PARSING)
          ? functions::WideNumberMode::kExact
          : functions::WideNumberMode::kRound;

  Value prop_gsql_value = SafeConvertJsonValueToValue(
      wide_number_mode, language_options.product_mode(),
      prop_value->json_value(), target_value.type());
  return prop_gsql_value.SqlEquals(target_value);
}

}  // namespace

void RegisterBuiltinGraphFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kSameGraphElement},
      [](FunctionKind kind, const Type* output_type) {
        return new SameGraphElementFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kAllDifferentGraphElement},
      [](FunctionKind kind, const Type* output_type) {
        return new AllDifferentGraphElementFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsSourceNode, FunctionKind::kIsDestNode},
      [](FunctionKind kind, const Type* output_type) {
        return new SourceDestPredicateFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPropertyExists},
      [](FunctionKind kind, const Type* output_type) {
        return new PropertyExistsFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kLabels}, [](FunctionKind kind, const Type* output_type) {
        return new LabelsFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPropertyNames},
      [](FunctionKind kind, const Type* output_type) {
        return new PropertyNamesFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kElementDefinitionName},
      [](FunctionKind kind, const Type* output_type) {
        return new ElementDefinitionNameFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kElementId, FunctionKind::kSourceNodeId,
       FunctionKind::kDestinationNodeId},
      [](FunctionKind kind, const Type* output_type) {
        return new ElementIdFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPathLength},
      [](FunctionKind kind, const Type* output_type) {
        return new PathLengthFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPathNodes, FunctionKind::kPathEdges},
      [](FunctionKind kind, const Type* output_type) {
        return new PathElementsFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPathFirst, FunctionKind::kPathLast},
      [](FunctionKind kind, const Type* output_type) {
        return new PathFirstLastFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPathCreate, FunctionKind::kUncheckedPathCreate},
      [](FunctionKind kind, const Type* output_type) {
        return new PathCreateFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kPathConcat, FunctionKind::kUncheckedPathConcat},
      [](FunctionKind kind, const Type* output_type) {
        return new PathConcatFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsTrail}, [](FunctionKind kind, const Type* output_type) {
        return new IsTrailFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsAcyclic},
      [](FunctionKind kind, const Type* output_type) {
        return new IsAcyclicFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsSimple},
      [](FunctionKind kind, const Type* output_type) {
        return new IsSimpleFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kDynamicPropertyEquals},
      [](FunctionKind kind, const Type* output_type) {
        return new DynamicPropertyEqualsFunction(kind, output_type);
      });
}

}  // namespace googlesql
