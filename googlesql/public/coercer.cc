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

#include "googlesql/public/coercer.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/cast.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {
using SuperTypesMap = absl::flat_hash_map<TypeKind, std::vector<const Type*>>;

bool IsExtendedCoercion(const Type* from_type, const Type* to_type) {
  return from_type->IsExtendedType() || to_type->IsExtendedType();
}

bool StatusToBool(const absl::StatusOr<bool>& status) {
  // This function is used by old CoercesTo functions that return bool to
  // convert StatusOr<bool> values returned by new CoercesTo API. Old API
  // functions should never be called for expressions that may involve extended
  // types and for built-in types new API functions should never return an error
  // status unless some invariant is broken. Thus, ABSL_DCHECK status here to crash
  // in debug mode to signal that some contract is violated.
  GOOGLESQL_DCHECK_OK(status.status());
  return status.value_or(false);
}

constexpr int GetNullLiteralCoercionCost() {
  // The cost of coercing NULL to any type is 1.
  // TODO: Seems like this should be 0.  Try it and see what
  // breaks/changes.
  return 1;
}

SuperTypesMap* CreateBuiltinSuperTypesMap() {
  auto* map = new SuperTypesMap;

  for (const auto& [cast_pair, cast_function_property] :
       internal::GetGoogleSQLCasts()) {
    const auto [src_type_kind, dst_type_kind] = cast_pair;

    if (src_type_kind == dst_type_kind) {
      continue;
    }

    if (!cast_function_property.is_implicit()) {
      continue;
    }

    if (!Type::IsSimpleType(src_type_kind) ||
        !Type::IsSimpleType(dst_type_kind)) {
      continue;
    }

    std::vector<const Type*>& supertypes = (*map)[src_type_kind];
    const Type* dst_type = types::TypeFromSimpleTypeKind(dst_type_kind);
    ABSL_CHECK_NE(dst_type, nullptr);

    supertypes.push_back(dst_type);
  }

  for (auto& [type_kind, type_vector] : *map) {
    std::sort(type_vector.begin(), type_vector.end(),
              Type::TypeSpecificityLess);  // Most specific first.
  }

  return map;
}

const SuperTypesMap& GetBuiltinSuperTypesMap() {
  static SuperTypesMap* built_in_supertypes_map = CreateBuiltinSuperTypesMap();
  return *built_in_supertypes_map;
}

absl::StatusOr<TypeListView> GetSuperTypesOfBuiltinType(const Type* type) {
  GOOGLESQL_RET_CHECK_NE(type, nullptr);
  GOOGLESQL_RET_CHECK_NE(type->kind(), TypeKind::TYPE_EXTENDED);

  const SuperTypesMap& map = GetBuiltinSuperTypesMap();
  auto it = map.find(type->kind());
  if (it == map.end()) {
    return TypeListView();
  }

  return TypeListView(it->second);
}

// Helper class that stores Type and its supertypes.
class TypeSuperTypes {
 public:
  TypeSuperTypes(const Type* type, TypeListView supertypes)
      : type_(type), supertypes_(supertypes) {}

  // Checks whether <type> Equals (type->Equals(other) == true) to any type
  // stored in this class. This includes any Type from supertypes() and Type
  // returned by type().
  bool Contains(const Type* type) const {
    if (TypeEquals(type, type_)) {
      return true;
    }

    for (const Type* t : supertypes_) {
      if (TypeEquals(type, t)) {
        return true;
      }
    }

    return false;
  }

  const Type* type() const { return type_; }
  TypeListView supertypes() const { return supertypes_; }

  std::vector<const Type*> ToVector() const {
    std::vector<const Type*> result;
    result.reserve(supertypes_.size() + 1);
    AddAllTypesToVector(&result);
    return result;
  }

  void AddAllTypesToVector(std::vector<const Type*>* vector) const {
    vector->push_back(type_);
    vector->insert(vector->end(), supertypes_.begin(), supertypes_.end());
  }

 private:
  static bool TypeEquals(const Type* t1, const Type* t2) {
    return t1->Equivalent(t2);
  }

  const Type* type_;
  TypeListView supertypes_;
};

// Helper class that checks that global preference order between supertypes is
// maintained. Please see Catalog::GetExtendedTypeSuperTypes for more details on
// global preference order. To check that global order is respected for all
// types we use a topological sort:
//  1) Create a graph of supertype preference relations between all participated
//    types. E.g. if GetExtendedTypeSuperTypes(A)=[B,C] and
//    GetExtendedTypeSuperTypes(C)=[D], the graph will look like: A->B->C->D.
//  2) Run depth first search for each node of that graph and return failure if
//    cycle is found.
class TypeGlobalOrderChecker {
 public:
  static absl::Status Check(TypeListView types, Catalog* catalog = nullptr) {
    TypeGlobalOrderChecker checker;
    for (const Type* type : types) {
      GOOGLESQL_RETURN_IF_ERROR(checker.AddType(type, catalog));
    }

    return checker.Check();
  }

  static absl::Status Check(absl::Span<const TypeSuperTypes> supertypes_list,
                            Catalog* catalog = nullptr) {
    std::vector<const Type*> types;
    for (const TypeSuperTypes& supertypes : supertypes_list) {
      supertypes.AddAllTypesToVector(&types);
    }

    return Check(types, catalog);
  }

 private:
  enum class NodeState { kNone = 0, kVisited, kProcessed };

  struct Node {
    TypeFlatHashSet<> children;
    NodeState state;
  };

  Node& GetNode(const Type* type) {
    std::unique_ptr<Node>& node = graph_[type];
    ABSL_CHECK(node);

    return *node;
  }

  absl::Status AddDependency(const Type* t1, const Type* t2) {
    GOOGLESQL_RET_CHECK_NE(t1, nullptr);
    GOOGLESQL_RET_CHECK_NE(t2, nullptr);
    GOOGLESQL_RET_CHECK(!t1->Equals(t2));

    GetNode(t1).children.insert(t2);

    return absl::OkStatus();
  }

  absl::Status AddType(const Type* type, Catalog* catalog) {
    GOOGLESQL_RET_CHECK_NE(type, nullptr);

    std::unique_ptr<Node>& node = graph_[type];
    if (node != nullptr) {
      return absl::OkStatus();
    }

    node = std::make_unique<Node>();

    GOOGLESQL_ASSIGN_OR_RETURN(TypeListView supertypes,
                     GetCandidateSuperTypes(type, catalog));
    for (const Type* child : supertypes) {
      GOOGLESQL_RETURN_IF_ERROR(AddType(child, catalog));
    }

    for (int i = 0; i < supertypes.size(); i++) {
      const Type* src = i == 0 ? type : supertypes[i - 1];
      const Type* dst = supertypes[i];
      GOOGLESQL_RETURN_IF_ERROR(AddDependency(src, dst));
    }

    return absl::OkStatus();
  }

  absl::Status Check() {
    std::stack<std::pair<const Type*, bool>> stack;

    for (const auto& kvp : graph_) {
      stack.emplace(kvp.first, false);
    }

    while (!stack.empty()) {
      auto& [current_type, children_processed] = stack.top();
      auto& current_node = graph_[current_type];
      GOOGLESQL_RET_CHECK(current_node);

      if (!children_processed) {
        if (current_node->state == NodeState::kVisited) {
          // Cycle in the graph.
          return googlesql_base::FailedPreconditionErrorBuilder()
                 << "Violation of global supertype preference order for type: "
                 << current_type->DebugString();
        }

        if (current_node->state == NodeState::kProcessed) {
          stack.pop();
          continue;
        }

        GOOGLESQL_RET_CHECK(current_node->state == NodeState::kNone);
        current_node->state = NodeState::kVisited;
        children_processed = true;

        if (!current_node->children.empty()) {
          for (const Type* child : current_node->children) {
            stack.emplace(child, false);
          }

          continue;
        }
        // If there are no children - just fell through to kProcessed state.
      }

      GOOGLESQL_RET_CHECK(current_node->state == NodeState::kVisited);
      current_node->state = NodeState::kProcessed;
      stack.pop();
    }

    return absl::OkStatus();
  }

  TypeFlatHashMap<std::unique_ptr<Node>> graph_;
};

absl::Status CheckSupertypesGlobalOrderForCoercer(
    absl::Span<const TypeSuperTypes> supertypes_list, Catalog* catalog) {
  if (!std::any_of(
          supertypes_list.begin(), supertypes_list.end(),
          [](const auto& st) { return st.type()->IsExtendedType(); })) {
    // We check global order of built-in types in unit test. Thus, if we don't
    // have any extended types to check, we can safely return ok.
    return absl::OkStatus();
  }

  return TypeGlobalOrderChecker::Check(supertypes_list, catalog);
}

// Determines whether a string literal can be implicitly converted to bytes.
// Coercion is only allowed if individual characters are in the range [0, 127].
// For details, see: (broken link)
bool IsValidStringLiteralToBytesImplicitCoercion(const Value& literal_value) {
  if (!literal_value.has_content()) {
    return false;
  }

  absl::Status status;
  std::vector<int64_t> codepoints;
  if (!functions::StringToCodePoints(literal_value.string_value(), &codepoints,
                                     &status)) {
    return false;
  }
  // Only string literal containing characters [0-127] can be implicitly coerced
  // to BYTES.
  if (!std::all_of(codepoints.begin(), codepoints.end(),
                   [](int64_t c) { return c <= 127; })) {
    return false;
  }

  return true;
}

// This method supports the logic to implicitly coerce string literals to the
// bytes type.
//
// <literal_types> is a set of InputArgumentTypes for the base types related to
// the <argument_set> entries.  This method will check to see if all of the
// entries in <argument_set> are either string or bytes type, and if so will
// replace the <literal_types> entries. It does so by repopulating
// <literal_types> from the <argument_set>, while 'promoting' the
// InputArgumentType of bytes to non-literal.  In effect, recursing on this new
// set of <literal_types> will allow string literals (or parameters being
// treated as literals) to correctly coerce/supertype to bytes.
//
// The caller will only invoke this method if each item in <argument_set> is
// either a literal or parameter that should be treated as a literal. The caller
// will recurse on the resulting <literal_types> value.
absl::Status UpdateLiteralTypesForStringAndBytesArgument(
    const InputArgumentTypeSet& argument_set, bool treat_parameters_as_literals,
    InputArgumentTypeSet* literal_types) {
  for (const auto& arg : literal_types->arguments()) {
    if (!arg.type()->IsString() && !arg.type()->IsBytes()) {
      return absl::OkStatus();
    }
  }

  literal_types->clear();
  for (const InputArgumentType& it : argument_set.arguments()) {
    if (it.is_untyped()) {
      // Ignore untyped NULL arguments since they coerce to any type.
      continue;
    }
    if (it.is_literal() ||
        (treat_parameters_as_literals && it.is_query_parameter())) {
      if (it.type()->IsString()) {
        // For STRING input argument type, preserve the existing argument type.
        literal_types->Insert(it);
      } else {
        // For BYTES input argument type, promote the existing argument type to
        // non-literal.
        GOOGLESQL_RET_CHECK(it.type()->IsBytes());
        literal_types->Insert(InputArgumentType(it.type()));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TypeListView> GetCandidateSuperTypes(const Type* type,
                                                    Catalog* catalog) {
  GOOGLESQL_RET_CHECK_NE(type, nullptr);
  GOOGLESQL_RET_CHECK(!type->IsStruct());
  GOOGLESQL_RET_CHECK(!type->IsArray());

  if (type->IsExtendedType()) {
    if (catalog == nullptr) {
      GOOGLESQL_RET_CHECK_FAIL() << "Attempt to find a conversion rule for extended type "
                       << type->DebugString() << " without providing a Catalog";
    }

    return catalog->GetExtendedTypeSuperTypes(type);
  }

  return GetSuperTypesOfBuiltinType(type);
}

// ContextBase contains a basic set of properties needed for particular coercion
// check request. The coercion check logic itself is implemented in Context
// class, while ContextBase serves just to enforce several important invariants
// by restricting direct access to context's fields.
class Coercer::ContextBase {
 public:
  ContextBase(const Coercer& coercer, bool is_explicit)
      : coercer_(coercer), is_explicit_(is_explicit) {}

  ContextBase(const ContextBase& other) = delete;
  ContextBase& operator=(const ContextBase& other) = delete;

  Catalog* catalog() const { return coercer_.catalog_; }
  TypeFactory& type_factory() { return *coercer_.type_factory_; }
  const LanguageOptions& language_options() const {
    return coercer_.language_options_;
  }

  // Returns true if explicit cast is checked. False if implicit coercion.
  bool is_explicit() const { return is_explicit_; }

  using ConversionEvaluatorOperations =
      ConversionTypePairOperations<ConversionEvaluator>;
  using ConversionEvaluatorSet =
      absl::flat_hash_set<ConversionEvaluator,
                          ConversionEvaluatorOperations::Hash,
                          ConversionEvaluatorOperations::Eq>;

  // If during a coercion process we meet some extended Type (can be source or
  // destination Type of the coercion), this property returns a conversion
  // function that Catalog provides in Conversion::function().
  const ConversionEvaluatorSet& extended_conversion_evaluators() const& {
    return extended_conversion_evaluators_;
  }

  // Saves the extended conversion function. Returns an error if function is
  // already set.
  absl::Status AddExtendedConversion(const Conversion& extended_conversion);

 private:
  const Coercer& coercer_;
  ConversionEvaluatorSet extended_conversion_evaluators_;
  const bool is_explicit_;
};

absl::Status Coercer::ContextBase::AddExtendedConversion(
    const Conversion& extended_conversion) {
  GOOGLESQL_RET_CHECK(extended_conversion.is_valid());

  extended_conversion_evaluators_.insert(extended_conversion.evaluator());
  return absl::OkStatus();
}

absl::Status CheckSuperTypePreferenceGlobalOrder(TypeListView types,
                                                 Catalog* catalog) {
  return TypeGlobalOrderChecker::Check(types, catalog);
}

// Context class implements Coercer logic for a particular coercion check
// request.
class Coercer::Context : public Coercer::ContextBase {
 public:
  using Coercer::ContextBase::ContextBase;

  absl::StatusOr<bool> CoercesTo(const InputArgumentType& from_argument,
                                 const Type* to_type,
                                 SignatureMatchResult* result);

  absl::StatusOr<bool> ExtendedTypeCoercesTo(
      const Type* from_type, const Type* to_type,
      Catalog::ConversionSourceExpressionKind source_kind,
      SignatureMatchResult* result);

  absl::StatusOr<bool> StructCoercesTo(const InputArgumentType& struct_argument,
                                       const Type* to_type,
                                       SignatureMatchResult* result);

  absl::StatusOr<bool> ArrayCoercesTo(const InputArgumentType& array_argument,
                                      const Type* to_type,
                                      SignatureMatchResult* result);

  absl::StatusOr<bool> GraphElementCoercesTo(
      const InputArgumentType& graph_element_argument, const Type* to_type,
      SignatureMatchResult* result);

  absl::StatusOr<bool> GraphPathCoercesTo(
      const InputArgumentType& graph_path_argument, const Type* to_type,
      SignatureMatchResult* result);

  absl::StatusOr<bool> MapCoercesTo(const InputArgumentType& map_argument,
                                    const Type* to_type,
                                    SignatureMatchResult* result);

  absl::StatusOr<bool> ParameterCoercesTo(const Type* from_type,
                                          const Type* to_type,
                                          SignatureMatchResult* result);

  absl::StatusOr<bool> LiteralCoercesTo(const Value& literal_value,
                                        const Type* to_type,
                                        SignatureMatchResult* result);

  absl::StatusOr<bool> TypeCoercesTo(const Type* from_type, const Type* to_type,
                                     SignatureMatchResult* result);

  // Returns whether <from_struct> can be coerced to <to_type>. <to_type>
  // must be a ProtoType whose descriptor is a map entry. <from_struct>
  // must have two fields. The struct's first and second fields must cast or
  // coerce (depending on is_explicit()) to the key and value field types of the
  // proto. The names of the struct fields are ignored.
  //
  // The result of this function is definitive for struct->proto coercion,
  // so <result> is always updated.
  absl::StatusOr<bool> StructCoercesToProtoMapEntry(
      const StructType* from_struct, const ProtoType* to_type,
      SignatureMatchResult* result);

  bool IsIntToOpaqueEnumInProductExternal(const Type* from_type,
                                          const Type* to_type) const;

  // Returns true if 'literal_value' can be coerced to 'to_type'.
  bool CoercesFromLiteralForBuiltinSimpleTypes(const Value& literal_value,
                                               const Type* to_type) const;
};

int GetLiteralCoercionCost(const Value& literal_value, const Type* to_type) {
  if (literal_value.is_null()) {
    return GetNullLiteralCoercionCost();
  }
  return Type::GetTypeCoercionCost(to_type->kind(),
                                   literal_value.type()->kind());
}

void Coercer::StripFieldAliasesFromStructType(const Type** struct_type) const {
  if (!(*struct_type)->IsStruct()) {
    // Nothing to strip here.
    return;
  }

  const StructType* in_type = (*struct_type)->AsStruct();
  std::vector<StructType::StructField> struct_field_types;
  for (int i = 0; i < in_type->num_fields(); ++i) {
    const Type* field_type = in_type->field(i).type;
    if (field_type->IsStruct()) {
      StripFieldAliasesFromStructType(&field_type);
    }
    struct_field_types.push_back({"" /* anonymous field */, field_type});
  }
  GOOGLESQL_CHECK_OK(type_factory_->MakeStructType(struct_field_types, struct_type));
}

absl::StatusOr<const StructType*> Coercer::GetCommonStructSuperType(
    const InputArgumentTypeSet& argument_set) const {
  GOOGLESQL_RET_CHECK(!argument_set.arguments().empty());
  const InputArgumentType* dominant_argument = argument_set.dominant_argument();
  GOOGLESQL_RET_CHECK_NE(dominant_argument, nullptr);

  const Type* common_type = dominant_argument->type();
  for (const InputArgumentType& argument : argument_set.arguments()) {
    if (!argument.type()->IsStruct() && !argument.is_untyped()) {
      // Structs and non-struct have no common supertype, so if one
      // of the arguments is non-struct then there is no common struct
      // supertype.
      return nullptr;
    }

    if (common_type != nullptr && !common_type->Equals(argument.type())) {
      common_type = nullptr;
    }
  }
  // If all types are equal, return that type.
  if (common_type != nullptr) {
    GOOGLESQL_RET_CHECK(common_type->IsStruct());
    return common_type->AsStruct();
  }

  GOOGLESQL_RET_CHECK(dominant_argument->type()->IsStruct());
  const StructType* dominant_struct = dominant_argument->type()->AsStruct();
  const int num_struct_fields = dominant_struct->num_fields();
  std::vector<InputArgumentTypeSet> struct_field_argument_sets(
      num_struct_fields);

  // We insert the fields arguments from dominant_argument first as we
  // need to ensure that we always use field aliases from the
  // dominant_argument only, even for nested structs.
  GOOGLESQL_RET_CHECK_EQ(dominant_argument->field_types_size(), num_struct_fields)
      << "Dominant argument: " << dominant_argument->DebugString()
      << "Argument set: " << argument_set.ToString(true /* verbose */);

  for (int i = 0; i < num_struct_fields; ++i) {
    // We have identified the dominant argument, and its fields should
    // also be dominant even if they are an untyped NULL.  This properly
    // handles struct supertype cases such as:
    //
    //   SELECT [ NULL,
    //            (NULL, NULL),
    //            struct<a int32, b int64>(1, 2) ]
    //
    // In this example, the second argument is dominant so the resulting
    // type should be struct<int32, int64> with anonymous fields since
    // this dominant argument does not have field names.  If we did not
    // allow untyped NULLs to be dominant in the field argument sets,
    // then the field names from the third argument would leak through.
    struct_field_argument_sets[i].Insert(dominant_argument->field_type(i),
                                         /*set_dominant=*/true);
  }

  for (const InputArgumentType& argument_type : argument_set.arguments()) {
    if (argument_type.is_untyped()) {
      continue;
    }

    const StructType* struct_type = argument_type.type()->AsStruct();
    if (struct_type->num_fields() != num_struct_fields) {
      return nullptr;
    }
    ABSL_DCHECK_EQ(argument_type.field_types_size(), num_struct_fields);
    for (int i = 0; i < num_struct_fields; ++i) {
      struct_field_argument_sets[i].Insert(argument_type.field_type(i));
    }
  }

  std::vector<StructType::StructField> supertyped_field_types;
  for (int i = 0; i < num_struct_fields; ++i) {
    const Type* common_field_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(
        GetCommonSuperType(struct_field_argument_sets[i], &common_field_type));
    if (common_field_type == nullptr) {
      return nullptr;
    }
    // If the dominant_argument field is an untyped NULL, we would pick the
    // field aliases for the nested struct from some other non-NULL field
    // argument which is incorrect.  To fix this, we strip off the
    // field aliases from the nested struct supertype here.
    if (common_field_type->IsStruct() &&
        dominant_argument->field_type(i).is_untyped()) {
      StripFieldAliasesFromStructType(&common_field_type);
    }

    supertyped_field_types.push_back(
        {dominant_argument->type()->AsStruct()->field(i).name,
         common_field_type});
  }

  const StructType* supertyped_struct_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructType(supertyped_field_types,
                                                &supertyped_struct_type));
  return supertyped_struct_type;
}

absl::StatusOr<const ArrayType*> Coercer::GetCommonArraySuperType(
    const InputArgumentTypeSet& argument_set,
    bool treat_query_parameters_as_literals) const {
  // First build an InputArgumentTypeSet corresponding to the element types in
  // 'argument_set'.
  InputArgumentTypeSet element_argument_set;
  for (const InputArgumentType& argument : argument_set.arguments()) {
    if (argument.is_untyped()) continue;
    const Type* argument_type = argument.type();
    if (!argument_type->IsArray()) return nullptr;
    const Type* element_type = argument_type->AsArray()->element_type();

    InputArgumentType element_argument;
    if (argument.is_literal()) {
      // Just choose an arbitrary literal value for the element.
      element_argument = InputArgumentType(Value::Null(element_type));
    } else {
      element_argument =
          InputArgumentType(element_type, argument.is_query_parameter());
    }

    const bool is_dominant_argument =
        (argument_set.dominant_argument() == &argument);
    element_argument_set.Insert(element_argument, is_dominant_argument);
  }

  // Next, compute the supertype for the elements.
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* element_supertype,
                   GetCommonSuperTypeImpl(element_argument_set,
                                          treat_query_parameters_as_literals));
  if (element_supertype == nullptr) return nullptr;

  // Finally, attempt to coerce the individual arrays to the new array type.
  const ArrayType* new_array_type;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory_->MakeArrayType(element_supertype, &new_array_type));
  for (const InputArgumentType& argument : argument_set.arguments()) {
    SignatureMatchResult result;
    auto unused_cast_evaluator = ExtendedCompositeCastEvaluator::Invalid();
    GOOGLESQL_ASSIGN_OR_RETURN(bool coerced,
                     CoercesTo(argument, new_array_type, /*is_explicit=*/false,
                               &result, &unused_cast_evaluator));
    if (!coerced) {
      return nullptr;
    }
  }

  return new_array_type;
}

absl::StatusOr<const GraphElementType*> Coercer::GetCommonGraphElementSuperType(
    const InputArgumentTypeSet& argument_set) const {
  const auto& arguments = argument_set.arguments();
  GOOGLESQL_RET_CHECK(!arguments.empty());

  const auto* dominant_arg = argument_set.dominant_argument();
  GOOGLESQL_RET_CHECK_NE(dominant_arg, nullptr);

  const Type* dominant_type = dominant_arg->type();
  GOOGLESQL_RET_CHECK_NE(dominant_type, nullptr);

  // If not GraphElementType, no supertype.
  if (!dominant_type->IsGraphElement()) {
    return nullptr;
  }

  const auto* dominant_graph_element_type = dominant_type->AsGraphElement();

  // Returns dominant type when there is only one type.
  if (argument_set.arguments().size() == 1) {
    return dominant_graph_element_type;
  }

  // No common supertype if any one argument is not the same kind of graph
  // element type.
  if (absl::c_any_of(arguments, [&](const auto& arg) {
        if (arg.is_untyped_null()) {
          // Supertype between an untyped NULL and any other type should be that
          // other type.
          return false;
        }
        if (!arg.type()->IsGraphElement()) {
          return true;
        }
        const auto* source_graph_element_type = arg.type()->AsGraphElement();
        // No coercion between element kinds or across graphs.
        return !absl::c_equal(source_graph_element_type->graph_reference(),
                              dominant_graph_element_type->graph_reference(),
                              googlesql_base::CaseEqual) ||
               source_graph_element_type->element_kind() !=
                   dominant_graph_element_type->element_kind();
      })) {
    return nullptr;
  }

  std::vector<GraphElementType::PropertyType> property_type_collection;
  bool is_dynamic = false;
  for (const auto& arg : arguments) {
    if (arg.type()->IsGraphElement()) {
      absl::c_copy(arg.type()->AsGraphElement()->property_types(),
                   std::back_inserter(property_type_collection));
      is_dynamic = is_dynamic || arg.type()->AsGraphElement()->is_dynamic();
    }
  }

  // TypeFactory will handle duplicate property types and report error for
  // inconsistent property types (property type with same name but different
  // value type).
  const GraphElementType* supertype;
  if (is_dynamic) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeDynamicGraphElementType(
        dominant_graph_element_type->graph_reference(),
        dominant_graph_element_type->element_kind(), property_type_collection,
        &supertype));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeGraphElementType(
        dominant_graph_element_type->graph_reference(),
        dominant_graph_element_type->element_kind(),
        std::move(property_type_collection), &supertype));
  }
  return supertype;
}

absl::StatusOr<const GraphPathType*> Coercer::GetCommonGraphPathSuperType(
    const InputArgumentTypeSet& argument_set) const {
  const auto& arguments = argument_set.arguments();
  GOOGLESQL_RET_CHECK(!arguments.empty());
  InputArgumentTypeSet node_types;
  InputArgumentTypeSet edge_types;
  for (const auto& arg : arguments) {
    if (arg.is_untyped_null()) {
      continue;
    }
    if (!arg.type()->IsGraphPath()) {
      return nullptr;
    }
    node_types.Insert(
        InputArgumentType(arg.type()->AsGraphPath()->node_type()));
    edge_types.Insert(
        InputArgumentType(arg.type()->AsGraphPath()->edge_type()));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const GraphElementType* node_type,
                   GetCommonGraphElementSuperType(node_types));
  GOOGLESQL_ASSIGN_OR_RETURN(const GraphElementType* edge_type,
                   GetCommonGraphElementSuperType(edge_types));
  const GraphPathType* supertype;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory_->MakeGraphPathType(node_type, edge_type, &supertype));
  return supertype;
}

const Type* Coercer::GetCommonSuperType(
    const InputArgumentTypeSet& argument_set) const {
  const Type* common_supertype{};
  absl::Status status = GetCommonSuperType(argument_set, &common_supertype);

  GOOGLESQL_DCHECK_OK(status);
  return status.ok() ? common_supertype : nullptr;
}

absl::Status Coercer::GetCommonSuperType(
    const InputArgumentTypeSet& argument_set,
    const Type** common_supertype) const {
  GOOGLESQL_RET_CHECK_NE(common_supertype, nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(*common_supertype,
                   GetCommonSuperTypeImpl(
                       argument_set, /*treat_parameters_as_literals=*/false));

  if (*common_supertype == nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(*common_supertype,
                     GetCommonSuperTypeImpl(
                         argument_set, /*treat_parameters_as_literals=*/true));
  }

  return absl::OkStatus();
}

// TODO: Add some unit tests for untyped empty array to coercer_test
// and coercer_supertypes_test.
absl::StatusOr<const Type*> Coercer::GetCommonSuperTypeImpl(
    const InputArgumentTypeSet& argument_set,
    bool treat_parameters_as_literals) const {
  if (argument_set.empty()) {
    return nullptr;
  }
  const InputArgumentType* dominant_argument = argument_set.dominant_argument();
  if (dominant_argument != nullptr) {
    GOOGLESQL_RET_CHECK(
        absl::c_linear_search(argument_set.arguments(), *dominant_argument));
  }

  for (const InputArgumentType& argument : argument_set.arguments()) {
    // This method gets called somehow for non-expression arguments and
    // relies on argument.type() being non-NULL, even though non-expression
    // arguments shouldn't have types, and only do by accident because
    // of code in the InputArgumentType factory methods.
    GOOGLESQL_RET_CHECK(argument.type() != nullptr) << argument.DebugString();

    // There are dedicated methods for special cases.
    if (argument.type()->IsStruct()) {
      return GetCommonStructSuperType(argument_set);
    }
    if (argument.type()->IsArray() && !argument.is_untyped()) {
      return GetCommonArraySuperType(argument_set,
                                     treat_parameters_as_literals);
    }
    if (argument.type()->IsGraphElement()) {
      return GetCommonGraphElementSuperType(argument_set);
    }
    if (argument.type()->IsGraphPath()) {
      return GetCommonGraphPathSuperType(argument_set);
    }
  }

  std::vector<const InputArgumentType*> typed_arguments;
  bool has_untyped_empty_array = false;
  bool has_floating_point_input = false;
  bool has_numeric_input = false;
  bool has_bignumeric_input = false;
  for (const InputArgumentType& argument : argument_set.arguments()) {
    // Ignore untyped arguments since they do not contribute towards
    // supertyping if any other argument type is present. Also keeps
    // track if there is untyped empty array in the argument.
    if (argument.is_untyped()) {
      if (argument.is_untyped_empty_array()) {
        has_untyped_empty_array = true;
      }
      continue;
    }
    if (argument.type()->IsFloatingPoint()) {
      has_floating_point_input = true;
    }
    if (argument.type()->IsNumericType()) {
      has_numeric_input = true;
    }
    if (argument.type()->IsBigNumericType()) {
      has_bignumeric_input = true;
    }
    typed_arguments.push_back(&argument);
  }

  if (typed_arguments.empty()) {
    if (!has_untyped_empty_array) {
      // All arguments were untyped NULL, so return the default NULL
      // type (INT64).
      return type_factory_->get_int64();
    } else {
      // There are untyped empty arrays and possibly untyped NULLs.
      // In this case, return the default ARRAY<INT64> type.
      const ArrayType* array_type = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(type_factory_->get_int64(),
                                                   &array_type));
      return array_type;
    }
  }

  GOOGLESQL_RET_CHECK_NE(dominant_argument, nullptr);
  const Type* common_type = dominant_argument->type();
  for (int i = 0; i < typed_arguments.size(); ++i) {
    const Type* type = typed_arguments[i]->type();
    if (common_type != nullptr && !common_type->Equals(type)) {
      common_type = nullptr;
    }
  }

  // If all types are equal, then return the dominant type.
  if (common_type != nullptr) {
    return common_type;
  }

  // For each non-literal argument, get the set of its supertypes.  Also,
  // get the set of literal argument types.
  std::vector<TypeSuperTypes> non_literal_candidate_supertypes;
  InputArgumentTypeSet literal_types;
  // Always add the dominant type (as a non-literal) to literal_types
  // so its type will be treated as the first type.
  literal_types.Insert(InputArgumentType(dominant_argument->type()));

  for (const InputArgumentType& it : argument_set.arguments()) {
    if (it.is_untyped()) {
      // Ignore untyped NULL arguments since they coerce to any type.
      continue;
    }
    if (it.is_literal() ||
        (treat_parameters_as_literals && it.is_query_parameter())) {
      // This argument is a literal or parameter to be treated like a literal.
      // We add a new InputArgumentType with just a Type (and no Value or
      // parameter) to <literals_types> for later handling in case
      // there are no non-literals present.
      literal_types.Insert(InputArgumentType(it.type()));
      continue;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(TypeListView supertypes,
                     GetCandidateSuperTypes(it.type(), catalog_));
    non_literal_candidate_supertypes.emplace_back(it.type(), supertypes);
  }

  GOOGLESQL_VLOG(6) << "non_literal_candidate_supertypes.size(): "
          << non_literal_candidate_supertypes.size();

  if (non_literal_candidate_supertypes.empty()) {
    GOOGLESQL_RET_CHECK(!literal_types.empty());

    if (language_options_.LanguageFeatureEnabled(
            FEATURE_IMPLICIT_COERCION_STRING_LITERAL_TO_BYTES)) {
      GOOGLESQL_RETURN_IF_ERROR(UpdateLiteralTypesForStringAndBytesArgument(
          argument_set, treat_parameters_as_literals, &literal_types));
    }

    // We did not have any non-literal arguments but we did have literal
    // arguments.  The <literal_types> list contains related
    // arguments that are neither literals nor parameters and we perform
    // supertype analysis over them instead.  Since these arguments are
    // not literals or parameters, recursion will not happen more than
    // once and <treat_parameters_as_literals> is irrelevant.
    return GetCommonSuperTypeImpl(literal_types, treat_parameters_as_literals);
  }

  // Check that global order of supertypes is maintained. This is a DCHECK
  // rather than a GOOGLESQL_RET_CHECK because the checking can be expensive for many
  // types/supertypes, and it should be sufficient to catch Type declaration
  // problems in non-production.
  GOOGLESQL_DCHECK_OK(CheckSupertypesGlobalOrderForCoercer(
      non_literal_candidate_supertypes, catalog_));

  // Find the most specific Type (i.e., lowest Type::TypeSpecificity())
  // that appears in all sets (note: sets are already sorted by specificity). We
  // do this by iterating through the sorted Types and returning the first one
  // that appears in all the sets.
  std::vector<const Type*>
      common_supertypes;  // Most specific to least specific.
  const TypeSuperTypes& initial_candidates =
      non_literal_candidate_supertypes[0];
  if (non_literal_candidate_supertypes.size() == 1) {
    common_supertypes = initial_candidates.ToVector();
  } else {
    for (int i = 0; i <= initial_candidates.supertypes().size(); i++) {
      // We should firstly check the type itself and only after that all its
      // supertypes in the specificity order.
      const Type* type = (i == 0) ? initial_candidates.type()
                                  : initial_candidates.supertypes()[i - 1];
      bool found_in_all = true;
      for (int j = 1; j < non_literal_candidate_supertypes.size(); j++) {
        const TypeSuperTypes& supertypes = non_literal_candidate_supertypes[j];
        if (!supertypes.Contains(type)) {
          found_in_all = false;
          break;
        }
      }
      if (found_in_all) {
        common_supertypes.push_back(type);
      }
    }
  }
  if (common_supertypes.empty()) {
    GOOGLESQL_VLOG(6) << "No common supertype found, return NULL";
    return nullptr;
  }

  GOOGLESQL_VLOG(6) << "common_supertype_kinds: "
          << Type::TypeListToString(common_supertypes,
                                    language_options_.product_mode());

  // Iterate through the common kinds, from most specific to least specific.
  // For each candidate type, see if all the literals can implicitly coerce.
  // If so, this is the common supertype.  If not, check the next candidate
  // type.
  for (const Type* most_specific_type : common_supertypes) {
    // Even though signed and unsigned integers have double as a common
    // supertype, double is not an allowed supertype of combinations of
    // signed/unsigned integers unless a floating point numeric (non-literal
    // or literal) is also present in the arguments.
    // Similarly, NUMERIC and BIGNUMERIC are not allowed as a supertype unless
    // an argument of the type is present.
    if ((most_specific_type->kind() == TYPE_DOUBLE &&
         !has_floating_point_input) ||
        (most_specific_type->kind() == TYPE_NUMERIC && !has_numeric_input) ||
        (most_specific_type->kind() == TYPE_BIGNUMERIC &&
         !has_bignumeric_input)) {
      continue;
    }

    const Type* candidate_type = nullptr;
    if (most_specific_type->IsSimpleType()) {
      candidate_type = most_specific_type;
    } else {
      // For non-simple types to have a supertype, all non-literals must have
      // the same kind, which should match the kind of the dominant argument.
      // All complex types should be equivalent (implying they are coercible),
      // and we want to pick the dominant argument type as the supertype.
      candidate_type = dominant_argument->type();
      if (candidate_type == nullptr ||
          candidate_type->kind() != most_specific_type->kind()) {
        continue;
      }
      // Check that all other non-literal args are equivalent to the dominant
      // argument.  Otherwise, coercion would not be allowed.
      bool all_equivalent = true;
      for (const auto& arg : typed_arguments) {
        if (!(arg->is_literal() ||
              (treat_parameters_as_literals && arg->is_query_parameter())) &&
            !arg->type()->Equivalent(candidate_type)) {
          GOOGLESQL_VLOG(6) << "Argument " << arg->DebugString()
                  << " does not have equivalent type to "
                  << candidate_type->DebugString();

          all_equivalent = false;
          break;
        }
      }
      if (!all_equivalent) {
        continue;
      }
    }
    GOOGLESQL_RET_CHECK_NE(candidate_type, nullptr);
    GOOGLESQL_VLOG(6) << "candidate type: " << candidate_type->DebugString();

    // We have a candidate.  Check parameters and literals to see if they
    // can all be implicitly coerced.
    bool parameters_and_literals_can_coerce = true;
    for (const InputArgumentType& argument : argument_set.arguments()) {
      SignatureMatchResult result;
      if (argument.is_untyped()) {
        // Untyped nulls coerce to any type.
        continue;
      }

      if (argument.is_literal()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool coerced,
                         Context(*this, /*is_explicit=*/false)
                             .LiteralCoercesTo(*argument.literal_value(),
                                               candidate_type, &result));
        if (!coerced) {
          GOOGLESQL_VLOG(6) << "Literal argument "
                  << argument.literal_value()->FullDebugString()
                  << " does not coerce to candidate type "
                  << candidate_type->DebugString();
          parameters_and_literals_can_coerce = false;
          break;
        }
      }

      if (treat_parameters_as_literals && argument.is_query_parameter()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            bool coerced,
            Context(*this, /*is_explicit=*/false)
                .ParameterCoercesTo(argument.type(), candidate_type, &result));
        if (!coerced) {
          GOOGLESQL_VLOG(6) << "Parameter argument " << argument.DebugString()
                  << " does not coerce to candidate type "
                  << candidate_type->DebugString();
          parameters_and_literals_can_coerce = false;
          break;
        }
      }
    }
    if (parameters_and_literals_can_coerce) {
      return candidate_type;
    }
  }
  return nullptr;  // No common supertype.
}

absl::StatusOr<bool> Coercer::Context::CoercesTo(
    const InputArgumentType& from_argument, const Type* to_type,
    SignatureMatchResult* result) {
  if (from_argument.is_untyped()) {
    // This is an untyped NULL, so the cost of coercion is considered as 0
    // and <result> is left unchanged.
    // TODO: Should this be consistent with untyped NULL coercion
    // cost defined in SignatureMatches?  We have to force it to be non-zero
    // there to avoid changing signature matching (and therefore the
    // result type for 'ROUND(NULL)', etc.
    return true;
  }
  if (from_argument.type()->IsStruct()) {
    return StructCoercesTo(from_argument, to_type, result);
  }
  if (from_argument.type()->IsArray()) {
    return ArrayCoercesTo(from_argument, to_type, result);
  }
  if (from_argument.type()->IsGraphElement()) {
    return GraphElementCoercesTo(from_argument, to_type, result);
  }
  if (from_argument.type()->IsGraphPath()) {
    return GraphPathCoercesTo(from_argument, to_type, result);
  }
  if (from_argument.type()->IsMap()) {
    return MapCoercesTo(from_argument, to_type, result);
  }
  if (from_argument.literal_value() != nullptr) {
    return LiteralCoercesTo(*(from_argument.literal_value()), to_type, result);
  }
  if (from_argument.is_query_parameter()) {
    return ParameterCoercesTo(from_argument.type(), to_type, result);
  }
  return TypeCoercesTo(from_argument.type(), to_type, result);
}

bool Coercer::AssignableTo(const InputArgumentType& from_argument,
                           const Type* to_type, bool is_explicit,
                           SignatureMatchResult* result) const {
  if (CoercesTo(from_argument, to_type, is_explicit, result)) {
    return true;
  }
  if ((from_argument.type()->IsInt64() && to_type->IsInt32()) ||
      (from_argument.type()->IsUint64() && to_type->IsUint32())) {
    return true;
  }
  return false;
}

absl::StatusOr<bool> Coercer::Context::ExtendedTypeCoercesTo(
    const Type* from_type, const Type* to_type,
    Catalog::ConversionSourceExpressionKind source_kind,
    SignatureMatchResult* result) {
  if (catalog() == nullptr) {
    return googlesql_base::FailedPreconditionErrorBuilder()
           << "Attempt to find a conversion rule for extended type without "
              "providing a Catalog to Coercer";
  }

  Catalog::FindConversionOptions options(is_explicit(), source_kind,
                                         language_options().product_mode());
  Conversion conversion = Conversion::Invalid();
  absl::Status find_status =
      catalog()->FindConversion(from_type, to_type, options, &conversion);
  if (!find_status.ok()) {
    if (absl::IsNotFound(find_status)) {
      // Benign error - conversion not found.
      result->incr_non_matched_arguments();
      return false;
    }
    return find_status;
  }

  GOOGLESQL_RETURN_IF_ERROR(AddExtendedConversion(conversion));

  switch (source_kind) {
    case Catalog::ConversionSourceExpressionKind::kLiteral:
    case Catalog::ConversionSourceExpressionKind::kParameter:
      result->incr_literals_coerced();
      result->incr_literals_distance(conversion.property().cost);
      break;
    case Catalog::ConversionSourceExpressionKind::kOther:
      result->incr_non_literals_coerced();
      result->incr_non_literals_distance(conversion.property().cost);
      break;
  }

  return true;
}

absl::StatusOr<bool> Coercer::Context::ParameterCoercesTo(
    const Type* from_type, const Type* to_type, SignatureMatchResult* result) {
  if (IsExtendedCoercion(from_type, to_type)) {
    return ExtendedTypeCoercesTo(
        from_type, to_type, Catalog::ConversionSourceExpressionKind::kParameter,
        result);
  }

  if (from_type->IsStruct()) {
    return StructCoercesTo(
        InputArgumentType(from_type, true /* is_query_parameter */), to_type,
        result);
  }
  if (from_type->IsArray()) {
    return ArrayCoercesTo(
        InputArgumentType(from_type, /*is_query_parameter=*/true), to_type,
        result);
  }
  if (from_type->IsMap()) {
    return MapCoercesTo(
        InputArgumentType(from_type, /*is_query_parameter=*/true), to_type,
        result);
  }

  const CastFunctionProperty* property =
      googlesql_base::FindOrNull(internal::GetGoogleSQLCasts(),
                      TypeKindPair(from_type->kind(), to_type->kind()));
  if (property != nullptr &&
      (SupportsParameterCoercion(property->type) ||
       (is_explicit() && SupportsExplicitCast(property->type))) &&
      (from_type->IsSimpleType() || to_type->IsSimpleType() ||
       from_type->Equivalent(to_type)) &&
      !IsIntToOpaqueEnumInProductExternal(from_type, to_type)) {
    // Count these the same as literal coercion.  This is because
    // it is a useful property to have the same coercion cost and stability
    // between two queries where one uses literals and the other uses
    // same-type parameters interchanged with the literals.
    result->incr_literals_coerced();
    result->incr_literals_distance(property->cost);
    return true;
  }
  result->incr_non_matched_arguments();
  return false;
}

bool Coercer::Context::IsIntToOpaqueEnumInProductExternal(
    const Type* from_type, const Type* to_type) const {
  // Note, this logic should probably be based on IsSupportedType, but
  // there are weird exceptions related to 'legacy' enums, like NormalizeMode.
  return from_type->IsInteger() && to_type->IsEnum() &&
         to_type->AsEnum()->IsOpaque() &&
         language_options().product_mode() == PRODUCT_EXTERNAL;
}

absl::StatusOr<bool> Coercer::Context::TypeCoercesTo(
    const Type* from_type, const Type* to_type, SignatureMatchResult* result) {
  if (IsExtendedCoercion(from_type, to_type)) {
    return ExtendedTypeCoercesTo(
        from_type, to_type, Catalog::ConversionSourceExpressionKind::kOther,
        result);
  }

  // Note: We have to check struct coercion before CastFunctionProperty, because
  // STRUCT->PROTO is not a generically supported cast, but there is a special
  // case for map entries.
  if (from_type->IsStruct()) {
    return StructCoercesTo(InputArgumentType(from_type), to_type, result);
  }

  const CastFunctionProperty* property =
      googlesql_base::FindOrNull(internal::GetGoogleSQLCasts(),
                      TypeKindPair(from_type->kind(), to_type->kind()));
  if (property == nullptr) {
    result->incr_non_matched_arguments();
    return false;
  }

  if (from_type->IsArray()) {
    return ArrayCoercesTo(InputArgumentType(from_type), to_type, result);
  }

  if (from_type->IsMap()) {
    return MapCoercesTo(InputArgumentType(from_type), to_type, result);
  }

  if (!is_explicit() && !SupportsImplicitCoercion(property->type)) {
    result->incr_non_matched_arguments();
    return false;
  }

  // Enum and proto types can coerce to a (same or different) type if
  // the from/to types are equivalent.
  if (!from_type->IsSimpleType() && !to_type->IsSimpleType()) {
    if (!from_type->Equivalent(to_type)) {
      result->incr_non_matched_arguments();
      return false;
    }
  }

  result->incr_non_literals_coerced();
  result->incr_non_literals_distance(property->cost);
  return true;
}

absl::StatusOr<bool> Coercer::Context::StructCoercesTo(
    const InputArgumentType& struct_argument, const Type* to_type,
    SignatureMatchResult* result) {
  // Expected invariant.
  GOOGLESQL_RET_CHECK(struct_argument.type()->IsStruct());

  const StructType* from_struct = struct_argument.type()->AsStruct();

  if (to_type->IsProto()) {
    return StructCoercesToProtoMapEntry(from_struct, to_type->AsProto(),
                                        result);
  }

  if (!to_type->IsStruct() ||
      from_struct->num_fields() != to_type->AsStruct()->num_fields()) {
    result->incr_non_matched_arguments();
    return false;
  }

  const StructType* to_struct = to_type->AsStruct();
  if (struct_argument.is_literal_null()) {
    // We still must check the field types for coercibility.
    for (int idx = 0; idx < to_struct->num_fields(); ++idx) {
      SignatureMatchResult local_result;
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool coerced,
          CoercesTo(InputArgumentType(from_struct->field(idx).type),
                    to_struct->field(idx).type, &local_result));
      if (!coerced) {
        result->incr_non_matched_arguments();
        return false;
      }
    }
    // Coercion distance of NULL to any type is 1.
    result->incr_literals_coerced();
    result->incr_literals_distance(GetNullLiteralCoercionCost());
    return true;
  }

  // Note that this can be empty if the input argument has no fields (for
  // example, 'STRUCT<>()')
  const std::vector<InputArgumentType>& struct_field_types =
      struct_argument.field_types();
  // We expect a non-NULL struct with matching number of fields.
  GOOGLESQL_RET_CHECK(!struct_argument.is_literal_null());

  std::vector<Value> coerced_field_values;
  for (int idx = 0; idx < to_struct->num_fields(); ++idx) {
    // Test expected invariant that the field types in the struct and the
    // InputArgumentType field types are the same.
    GOOGLESQL_RET_CHECK(
        struct_field_types[idx].type()->Equals(from_struct->field(idx).type))
        << "struct_field_types[" << idx
        << "]: " << struct_field_types[idx].type()->DebugString()
        << "; from_struct->field(" << idx
        << ").type: " << from_struct->field(idx).type->DebugString();
    if (!from_struct->field(idx).type->Equals(to_struct->field(idx).type)) {
      SignatureMatchResult local_result;
      if (struct_field_types[idx].is_literal()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            bool coerced,
            LiteralCoercesTo(*struct_field_types[idx].literal_value(),
                             to_struct->field(idx).type, &local_result));
        if (!coerced) {
          result->incr_non_matched_arguments();
          return false;
        }
      } else {
        // The struct field is not a literal.
        GOOGLESQL_ASSIGN_OR_RETURN(bool coerced,
                         CoercesTo(struct_field_types[idx],
                                   to_struct->field(idx).type, &local_result));
        if (!coerced) {
          result->incr_non_matched_arguments();
          return false;
        }
      }
      result->UpdateFromResult(local_result);
    }
  }
  return true;
}

absl::StatusOr<bool> Coercer::Context::StructCoercesToProtoMapEntry(
    const StructType* from_struct, const ProtoType* to_type,
    SignatureMatchResult* result) {
  const ProtoType* to_type_proto = to_type->AsProto();
  if (from_struct->fields().size() != 2 || to_type_proto == nullptr ||
      !to_type_proto->descriptor()->options().map_entry() ||
      !language_options().LanguageFeatureEnabled(
          LanguageFeature::FEATURE_PROTO_MAPS)) {
    result->incr_non_matched_arguments();
    return false;
  }

  const Type* key_type;
  const Type* value_type;
  bool ignore_annotations = false;
  if (!type_factory()
           .GetProtoFieldType(ignore_annotations, to_type_proto->map_key(),
                              to_type_proto->CatalogNamePath(), &key_type)
           .ok() ||
      !type_factory()
           .GetProtoFieldType(ignore_annotations, to_type_proto->map_value(),
                              to_type_proto->CatalogNamePath(), &value_type)
           .ok()) {
    result->incr_non_matched_arguments();
    return false;
  }

  SignatureMatchResult local_result;
  InputArgumentType arg_type;
  GOOGLESQL_ASSIGN_OR_RETURN(bool coerced,
                   CoercesTo(InputArgumentType(from_struct->field(0).type),
                             key_type, &local_result));
  if (!coerced) {
    result->incr_non_matched_arguments();
    return false;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(coerced,
                   CoercesTo(InputArgumentType(from_struct->field(1).type),
                             value_type, &local_result));
  if (!coerced) {
    result->incr_non_matched_arguments();
    return false;
  }
  result->UpdateFromResult(local_result);
  return true;
}

bool Coercer::Context::CoercesFromLiteralForBuiltinSimpleTypes(
    const Value& literal_value, const Type* to_type) const {
  // Note - ideally IsValidStringLiteralToBytesImplicitCoercion should
  // be checked alongside the other literal coercion checks in
  // FunctionResolver::ConvertLiteralToType, but this isn't currently feasible.
  // In that method we don't have the context to distinguish between explicit
  // and implicit cast calls, and results in unwanted AST changes for valid
  // statements like `CAST("\x80" as BYTES)`.
  if (literal_value.type()->IsString() && to_type->IsBytes() &&
      language_options().LanguageFeatureEnabled(
          FEATURE_IMPLICIT_COERCION_STRING_LITERAL_TO_BYTES) &&
      (literal_value.is_null() ||
       IsValidStringLiteralToBytesImplicitCoercion(literal_value))) {
    return true;
  }

  const CastFunctionProperty* cast_function_property =
      googlesql_base::FindOrNull(internal::GetGoogleSQLCasts(),
                      TypeKindPair(literal_value.type_kind(), to_type->kind()));

  if (cast_function_property == nullptr) {
    return false;
  }

  if (is_explicit()) {
    return SupportsExplicitCast(cast_function_property->type);
  }

  if (!SupportsLiteralCoercion(cast_function_property->type)) {
    return false;
  }

  return true;
}

absl::StatusOr<bool> Coercer::Context::ArrayCoercesTo(
    const InputArgumentType& array_argument, const Type* to_type,
    SignatureMatchResult* result) {
  GOOGLESQL_RET_CHECK(array_argument.type()->IsArray());
  const ArrayType* from_array = array_argument.type()->AsArray();
  if (from_array->Equivalent(to_type)) return true;

  if (language_options().LanguageFeatureEnabled(
          FEATURE_CAST_DIFFERENT_ARRAY_TYPES) &&
      to_type->IsArray()) {
    const Type* from_element_type = from_array->element_type();

    const ArrayType* to_array = to_type->AsArray();
    const Type* to_element_type = to_array->element_type();

    if (array_argument.is_literal() || array_argument.is_query_parameter()) {
      InputArgumentType element_arg;
      if (array_argument.is_literal()) {
        // Just check whether a NULL element coerces; any value will do.
        element_arg = InputArgumentType(Value::Null(from_element_type));
      } else {
        element_arg = InputArgumentType(from_element_type,
                                        /*is_query_parameter=*/true);
      }
      return CoercesTo(element_arg, to_element_type, result);
    }

    if (from_element_type->IsGraphElement() &&
        to_element_type->IsGraphElement()) {
      InputArgumentType element_arg(from_element_type,
                                    /*is_query_parameter=*/false,
                                    /*is_literal_for_constness=*/false);
      return GraphElementCoercesTo(element_arg, to_element_type, result);
    }
    if (from_element_type->IsGraphPath() && to_element_type->IsGraphPath()) {
      InputArgumentType element_arg(from_element_type,
                                    /*is_query_parameter=*/false,
                                    /*is_literal_for_constness=*/false);
      return GraphPathCoercesTo(element_arg, to_element_type, result);
    }

    if (is_explicit() ||
        // Per (broken link), ARRAY<STRUCT<K,V>> coerces implicitly
        // to ARRAY<MapEntryProto> if STRUCT<K,V> coerces to MapEntryProto.
        (to_element_type->IsProto() &&
         to_element_type->AsProto()->descriptor()->options().map_entry() &&
         language_options().LanguageFeatureEnabled(FEATURE_PROTO_MAPS))) {
      return TypeCoercesTo(from_element_type, to_element_type, result);
    }
  }

  result->incr_non_matched_arguments();
  return false;
}

absl::StatusOr<bool> Coercer::Context::GraphElementCoercesTo(
    const InputArgumentType& graph_element_argument, const Type* to_type,
    SignatureMatchResult* result) {
  GOOGLESQL_RET_CHECK(graph_element_argument.type()->IsGraphElement());

  // The language syntax does not support graph element literal definition.
  GOOGLESQL_RET_CHECK(!graph_element_argument.is_literal())
      << "Graph query should not produce GraphElementType literals";

  if (!to_type->IsGraphElement()) {
    result->incr_non_matched_arguments();
    return false;
  }

  const GraphElementType* to_graph_element = to_type->AsGraphElement();
  if (!graph_element_argument.type()->AsGraphElement()->CoercibleTo(
          to_graph_element)) {
    result->incr_non_matched_arguments();
    return false;
  }
  return true;
}

absl::StatusOr<bool> Coercer::Context::GraphPathCoercesTo(
    const InputArgumentType& graph_path_argument, const Type* to_type,
    SignatureMatchResult* result) {
  GOOGLESQL_RET_CHECK(graph_path_argument.type()->IsGraphPath());

  // Path literals not supported.
  GOOGLESQL_RET_CHECK(!graph_path_argument.is_literal())
      << "Graph query should not produce GraphPathType literals";

  if (!to_type->IsGraphPath()) {
    result->incr_non_matched_arguments();
    return false;
  }
  const GraphPathType* from = graph_path_argument.type()->AsGraphPath();
  const GraphPathType* to = to_type->AsGraphPath();
  if (from->node_type()->CoercibleTo(to->node_type()) &&
      from->edge_type()->CoercibleTo(to->edge_type())) {
    return true;
  }

  result->incr_non_matched_arguments();
  return false;
}

absl::StatusOr<bool> Coercer::Context::MapCoercesTo(
    const InputArgumentType& map_argument, const Type* to_type,
    SignatureMatchResult* result) {
  GOOGLESQL_RET_CHECK(map_argument.type()->IsMap());
  const MapType* from_map = map_argument.type()->AsMap();
  if (from_map->Equivalent(to_type)) return true;
  if (!to_type->IsMap()) {
    result->incr_non_matched_arguments();
    return false;
  }

  const MapType* to_map = to_type->AsMap();

  if (is_explicit() || map_argument.is_literal() ||
      map_argument.is_query_parameter()) {
    InputArgumentType key_arg;
    InputArgumentType value_arg;
    if (map_argument.is_literal()) {
      // Just check whether a NULL element coerces; any value will do.
      key_arg = InputArgumentType(Value::Null(from_map->key_type()));
      value_arg = InputArgumentType(Value::Null(from_map->value_type()));
    } else if (map_argument.is_query_parameter()) {
      key_arg = InputArgumentType(from_map->key_type(),
                                  /*is_query_parameter=*/true);
      value_arg = InputArgumentType(from_map->value_type(),
                                    /*is_query_parameter=*/true);
    } else {
      key_arg = InputArgumentType(from_map->key_type());
      value_arg = InputArgumentType(from_map->value_type());
    }
    SignatureMatchResult local_result;
    GOOGLESQL_ASSIGN_OR_RETURN(bool key_coerces,
                     CoercesTo(key_arg, to_map->key_type(), &local_result));
    GOOGLESQL_ASSIGN_OR_RETURN(bool value_coerces,
                     CoercesTo(value_arg, to_map->value_type(), &local_result));

    if (!key_coerces || !value_coerces) {
      result->incr_non_matched_arguments();
      return false;
    }

    return true;
  }

  result->incr_non_matched_arguments();
  return false;
}

absl::StatusOr<bool> Coercer::Context::LiteralCoercesTo(
    const Value& literal_value, const Type* to_type,
    SignatureMatchResult* result) {
  if (IsExtendedCoercion(literal_value.type(), to_type)) {
    return ExtendedTypeCoercesTo(
        literal_value.type(), to_type,
        Catalog::ConversionSourceExpressionKind::kLiteral, result);
  }

  SignatureMatchResult local_result;
  GOOGLESQL_ASSIGN_OR_RETURN(bool general_coercion_result,
                   TypeCoercesTo(literal_value.type(), to_type, &local_result));
  if (general_coercion_result) {
    // General Type coercion is allowed independent of literalness.
    result->incr_literals_coerced();
    result->incr_literals_distance(local_result.non_literals_distance());
    return true;
  }
  if (IsIntToOpaqueEnumInProductExternal(literal_value.type(), to_type)) {
    return false;
  }

  if (literal_value.type()->IsStruct()) {
    // Structs are coerced on a field-by-field basis.
    return StructCoercesTo(InputArgumentType(literal_value), to_type, result);
  }
  if (!literal_value.type()->IsSimpleType()) {
    // Non-struct complex-typed literals (enum, proto, array) coerce exactly
    // like non-literals, so if the general Type coercion check failed
    // above, then the complex literal does not coerce.
    result->incr_non_matched_arguments();
    return false;
  }
  // There are a few additional special cases for literal coercion in addition
  // to general implicit coercion.  For example, narrowing coercions are
  // required because the parser produces INT64, DOUBLE, and STRING literals but
  // not other INT, FLOAT or DATE/TIMESTAMP literals, and we need to be able
  // to swizzle them to these other literal types in order to provide desired
  // behavior.
  if (CoercesFromLiteralForBuiltinSimpleTypes(literal_value, to_type)) {
    // TODO: We may want to consider implicitly coercing a STRING
    // literal to DATE/TIMESTAMP to be an exact match (or distance 0) since we
    // don't produce DATE/TIMESTAMP literals in the parser.  The parsed
    // representation is STRING and matching this to a function signature
    // seems like it should logically be considered as an exact match.  Same
    // for narrowing a DOUBLE to FLOAT, and INT64 to other INT types.
    result->incr_literals_coerced();
    result->incr_literals_distance(
        GetLiteralCoercionCost(literal_value, to_type));
    return true;
  }
  result->incr_non_matched_arguments();
  return false;
}

bool Coercer::CoercesTo(const InputArgumentType& from_argument,
                        const Type* to_type, bool is_explicit,
                        SignatureMatchResult* result) const {
  ExtendedCompositeCastEvaluator extended_conversion_evaluator =
      ExtendedCompositeCastEvaluator::Invalid();
  return StatusToBool(CoercesTo(from_argument, to_type, is_explicit, result,
                                &extended_conversion_evaluator));
}

absl::StatusOr<bool> Coercer::CoercesTo(
    const InputArgumentType& from_argument, const Type* to_type,
    bool is_explicit, SignatureMatchResult* result,
    ExtendedCompositeCastEvaluator* extended_conversion_evaluator) const {
  Context context(*this, is_explicit);
  auto status = context.CoercesTo(from_argument, to_type, result);
  if (status.value_or(false)) {
    if (!context.extended_conversion_evaluators().empty()) {
      std::vector<ConversionEvaluator> evaluators;
      evaluators.reserve(context.extended_conversion_evaluators().size());
      evaluators.insert(evaluators.begin(),
                        context.extended_conversion_evaluators().begin(),
                        context.extended_conversion_evaluators().end());
      *extended_conversion_evaluator =
          ExtendedCompositeCastEvaluator(std::move(evaluators));
    }
  }
  return status;
}

bool Coercer::StructCoercesTo(const InputArgumentType& struct_argument,
                              const Type* to_type, bool is_explicit,
                              SignatureMatchResult* result) const {
  return StatusToBool(Context(*this, is_explicit)
                          .StructCoercesTo(struct_argument, to_type, result));
}

bool Coercer::ParameterCoercesTo(const Type* from_type, const Type* to_type,
                                 bool is_explicit,
                                 SignatureMatchResult* result) const {
  return StatusToBool(Context(*this, is_explicit)
                          .ParameterCoercesTo(from_type, to_type, result));
}

bool Coercer::LiteralCoercesTo(const Value& literal_value, const Type* to_type,
                               bool is_explicit,
                               SignatureMatchResult* result) const {
  return StatusToBool(Context(*this, is_explicit)
                          .LiteralCoercesTo(literal_value, to_type, result));
}

}  // namespace googlesql
