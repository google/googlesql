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

#include "googlesql/analyzer/function_signature_matcher.h"

#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/lambda_util.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/case.h"
#include "absl/base/attributes.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Some methods in this class have very large huge stack frames already.
#define NOINLINE_PREVENT_HUGE_STACK_FRAMES ABSL_ATTRIBUTE_NOINLINE

// The GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK macro that uses this requires
// a string literal, so we do it as a #define rather than a constant.
#define kStackSpaceErrorMessage                               \
  "Out of stack space due to deeply nested query expression " \
  "during signature matching"

// This class performs signature matching during GoogleSQL analysis.
// The functions determine if an argument list can use a function signature.
class FunctionSignatureMatcher {
 public:
  // The class does not take ownership of any of the arguments.
  // If <allow_argument_coercion> is TRUE then function arguments can be
  // coerced to the required signature type(s), otherwise they must be an
  // exact match.
  FunctionSignatureMatcher(const LanguageOptions& language_options,
                           const Coercer& coercer, bool allow_argument_coercion,
                           TypeFactory* type_factory);
  FunctionSignatureMatcher(const FunctionSignatureMatcher&) = delete;
  FunctionSignatureMatcher& operator=(const FunctionSignatureMatcher&) = delete;

  // Determines if the function signature matches the argument list, returning
  // a non-templated signature if true.  If <allow_argument_coercion_> is TRUE
  // then function arguments can be coerced to the required signature
  // type(s), otherwise they must be an exact match. Returns a non-OK status for
  // any internal errors.
  //
  // * <arg_ast_nodes> are the list of parser ASTNodes for each input_arguments.
  //   It's used to extract additional details for named arguments and lambdas.
  // * <resolve_lambda_callback> is called to resolve lambda arguments if any.
  //   It can be set to nullptr if no lambda argument is expected.
  // * The resolved lambdas, if any, are put into <arg_overrides> if the
  //   signature matches. <arg_overrides> is undefined otherwise.
  // * <resolve_lambda_callback> is used to resolve lambda. See
  //   <CheckResolveLambdaTypeAndCollectTemplatedArguments> about how a lambda
  //   is resolved. The resolved lambdas is put in <arg_overrides> with the
  //   corresponding index, if it matches the signature specification.
  absl::StatusOr<bool> SignatureMatches(
      const std::vector<const ASTNode*>& arg_ast_nodes,
      absl::Span<const InputArgumentType> input_arguments,
      const FunctionSignature& signature,
      const ResolveLambdaCallback* resolve_lambda_callback,
      std::unique_ptr<FunctionSignature>* concrete_result_signature,
      SignatureMatchResult* signature_match_result,
      std::vector<ArgIndexEntry>* arg_index_mapping,
      std::vector<FunctionArgumentOverride>* arg_overrides) const;

 private:
  // Shorthands to make message generation statements short.
  std::string ShortTypeName(const Type* type) const {
    return type->ShortTypeName(language_.product_mode());
  }

  std::string UserFacingName(const FunctionArgumentType& input_arg_type) const {
    return input_arg_type.UserFacingName(language_.product_mode(),
                                         /*print_template_details=*/true);
  }

  std::string UserFacingName(const InputArgumentType& input_arg_type) const {
    return input_arg_type.UserFacingName(language_.product_mode());
  }

  const LanguageOptions& language_;  // Not owned.
  const Coercer& coercer_;           // Not owned.
  const bool allow_argument_coercion_;
  TypeFactory* type_factory_;  // Not owned.

  // Represents the argument types corresponding to a SignatureArgumentKind.
  // There are three possibilities:
  // 1) The object represents an untyped NULL.
  // 2) The object represents an untyped empty array.
  // 3) The object represents a list of typed arguments.
  // An object in state i and can move to state j if i < j.
  //
  // The main purpose of this class is to keep track of the types associated
  // with a templated SignatureArgumentKind in
  // CheckArgumentTypesAndCollectTemplatedArguments(). There, if we encounter a
  // typed argument for a templated SignatureArgumentKind, we add that to the
  // set. But if there are no typed arguments, then we need to know whether
  // there is an untyped NULL or empty array, because that affects the type we
  // will infer for the SignatureArgumentKind.
  class SignatureArgumentKindTypeSet {
   public:
    enum Kind { UNTYPED_NULL, UNTYPED_EMPTY_ARRAY, TYPED_ARGUMENTS };

    // Creates a set with kind UNTYPED_NULL.
    SignatureArgumentKindTypeSet() : kind_(UNTYPED_NULL) {}
    SignatureArgumentKindTypeSet(const SignatureArgumentKindTypeSet&) = delete;
    SignatureArgumentKindTypeSet& operator=(
        const SignatureArgumentKindTypeSet&) = delete;

    Kind kind() const { return kind_; }

    // Changes the set to kind UNTYPED_EMPTY_ARRAY. Cannot be called if
    // InsertTypedArgument() has already been called.
    void SetToUntypedEmptyArray() {
      ABSL_DCHECK(kind_ != TYPED_ARGUMENTS);
      kind_ = UNTYPED_EMPTY_ARRAY;
    }

    // Changes the set to kind TYPED_ARGUMENTS, and adds a typed argument to
    // the set of typed arguments. If set_dominant is true, the argument will
    // be set to the dominant type in typed_arguments.
    bool InsertTypedArgument(const InputArgumentType& input_argument,
                             bool set_dominant = false) {
      // Typed arguments have precedence over untyped arguments.
      ABSL_DCHECK(!input_argument.is_untyped());
      kind_ = TYPED_ARGUMENTS;
      return typed_arguments_.Insert(input_argument, set_dominant);
    }

    // Returns the set of typed arguments corresponding to this object. Can only
    // be called if 'kind() == TYPED_ARGUMENTS'.
    const InputArgumentTypeSet& typed_arguments() const {
      ABSL_DCHECK_EQ(kind_, TYPED_ARGUMENTS);
      return typed_arguments_;
    }

    std::string DebugString() const;

   private:
    Kind kind_;
    // Does not contain any untyped arguments. Only valid if 'kind_' is
    // TYPED_ARGUMENTS.
    InputArgumentTypeSet typed_arguments_;
  };

  // Maps templated arguments (ARG_TYPE_ANY_1, etc.) to a set of input argument
  // types. See CheckArgumentTypesAndCollectTemplatedArguments() for details.
  typedef std::map<SignatureArgumentKind, SignatureArgumentKindTypeSet>
      ArgKindToInputTypesMap;

  // Maps templated arguments (ARG_TYPE_ANY_1, etc.) to the
  // resolved (possibly coerced) Type each resolved to in a particular function
  // call.
  typedef std::map<SignatureArgumentKind, const Type*> ArgKindToResolvedTypeMap;

  static std::string ArgKindToInputTypesMapDebugString(
      const ArgKindToInputTypesMap& map);

  // Returns the concrete argument type for a given <function_argument_type>,
  // using the mapping from templated to concrete argument types in
  // <templated_argument_map>. Also used for result types.
  // Returns a non-OK status for any internal error.
  absl::StatusOr<bool> GetConcreteArgument(
      const FunctionArgumentType& argument, int num_occurrences,
      const ArgKindToResolvedTypeMap& templated_argument_map,
      std::unique_ptr<FunctionArgumentType>* output_argument) const;

  // Returns a list of concrete arguments by calling GetConcreteArgument()
  // on each entry and setting <num_occurrences_> with appropriate argument
  // counts.
  // Returns a non-OK status for any internal error.
  absl::StatusOr<FunctionArgumentTypeList> GetConcreteArguments(
      absl::Span<const InputArgumentType> input_arguments,
      const FunctionSignature& signature, int repetitions, int optionals,
      const ArgKindToResolvedTypeMap& templated_argument_map,
      std::vector<ArgIndexEntry>* arg_index_mapping) const;

  // Returns if input argument types match the signature argument types, and
  // updates related templated argument type information.
  //
  // <repetitions> identifies the number of times that repeated arguments
  // repeat.
  //
  // Also populates <templated_argument_map> with a key for every templated
  // SignatureArgumentKind that appears in the signature (including the result
  // type) and <input_arguments>.  The corresponding value is the list of typed
  // arguments that occur for that SignatureArgumentKind. (The list may be empty
  // in the case of untyped arguments.)
  //
  // There is also some special handling for ANY_K: if we see an argument (typed
  // or untyped) for ARRAY_ANY_K, we act as if we also saw the corresponding
  // array element argument for ANY_K, and add an entry to
  // <templated_argument_map> even if ANY_K is not in the signature.
  //
  // Likewise for maps, if we see the map type, we also act as if we've seen
  // the key and value types, and vice versa. Note that the key type does
  // not imply we've seen the value type, nor does the value imply the key.
  //
  // * <arg_ast_nodes> are the list of parser ASTNodes for each input_arguments.
  //   It's used to assist lambda arguments resolving.
  // * <resolve_lambda_callback> is called to resolve lambda arguments if any.
  //   It can be set to nullptr if no lambda argument is expected.
  // * The resolved lambdas, if any, are put into <arg_overrides> if the
  //   signature matches. <arg_overrides> is undefined otherwise.
  absl::StatusOr<bool> CheckArgumentTypesAndCollectTemplatedArguments(
      const std::vector<const ASTNode*>& arg_ast_nodes,
      absl::Span<const InputArgumentType> input_arguments,
      const FunctionSignature& signature, int repetitions,
      const ResolveLambdaCallback* resolve_lambda_callback,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result,
      std::vector<FunctionArgumentOverride>* arg_overrides) const;

  // Returns if a single input argument type matches the corresponding signature
  // argument type, and updates related templated argument type information.
  //
  // Updates <templated_argument_map> for templated SignatureArgumentKind. The
  // corresponding value is the list of typed arguments that occur for that
  // SignatureArgumentKind. (The list may be empty in the case of untyped
  // arguments.)
  //
  // * <arg_ast_node> is the parser ASTNode for the argument. It's used to
  //   assist lambda arguments resolving.
  // * <resolve_lambda_callback> is called to resolve lambda arguments if any.
  //   It can be set to nullptr if no lambda argument is expected.
  // * If the input argument is a lambda, the resolved lambda is put into
  //   <arg_overrides>.
  absl::StatusOr<bool> CheckSingleInputArgumentTypeAndCollectTemplatedArgument(
      int arg_idx, const ASTNode* arg_ast_node,
      const InputArgumentType& input_argument,
      const FunctionArgumentType& signature_argument,
      const ResolveLambdaCallback* resolve_lambda_callback,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result,
      std::vector<FunctionArgumentOverride>* arg_overrides) const;

  // These are split out of the method above to reduce stack size.
  absl::StatusOr<bool>
  CheckSingleInputArgumentTypeAndCollectTemplatedArgumentUntyped(
      int arg_idx, const ASTNode* arg_ast_node,
      const InputArgumentType& input_argument,
      const FunctionArgumentType& signature_argument,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result) const;
  absl::StatusOr<bool>
  CheckSingleInputArgumentTypeAndCollectTemplatedArgumentTemplated(
      int arg_idx, const ASTNode* arg_ast_node,
      const InputArgumentType& input_argument,
      const FunctionArgumentType& signature_argument,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result) const;
  absl::StatusOr<bool>
  CheckSingleInputArgumentTypeAndCollectTemplatedArgumentGraph(
      int arg_idx, const ASTNode* arg_ast_node,
      const InputArgumentType& input_argument,
      const FunctionArgumentType& signature_argument,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result) const;

  // Returns if a lambda input argument matches the signature argument type and
  // updates <templated_argument_map> for templated lambda body type.
  //
  // This method infers the types of the arguments of the lambda based on
  // <templated_argument_map> and signature then resolves the lambda body.
  //
  // * <arg_idx> is the index of the argument.
  // * <arg_ast_node> is the parser ASTNode of the argument which is used to
  //   assist lambda resolving.
  // * the resolved lambda is put into <arg_overrides>.
  //
  // The lambda input argument matches the signature if:
  //   * the number of argument match the signature.
  //   * types of arguments of the lambda can be inferred.
  //   * the lambda body can be resolved.
  //   * the lambda body result type matches that of the signature.
  absl::StatusOr<bool> CheckResolveLambdaTypeAndCollectTemplatedArguments(
      int arg_idx, const ASTNode* arg_ast_node,
      const FunctionArgumentType& signature_argument,
      const InputArgumentType& input_argument,
      const ResolveLambdaCallback* resolve_lambda_callback,
      ArgKindToInputTypesMap* templated_argument_map,
      SignatureMatchResult* signature_match_result,
      std::vector<FunctionArgumentOverride>* arg_overrides) const;

  // This method is only relevant for table-valued functions. It returns true in
  // 'signature_matches' if a relation input argument type matches a signature
  // argument type, and sets information in 'signature_match_result' either way.
  absl::Status CheckRelationArgumentTypes(
      int arg_idx, const InputArgumentType& input_argument,
      const FunctionArgumentType& signature_argument,
      SignatureMatchResult* signature_match_result,
      bool* signature_matches) const;

  // Returns ok if all types in the `type_set` are implicitly coercible to
  // `inferred_type`. If not, an InvalidArgumentError is returned.
  //
  // * `type_set`: the set of types that are being considered for coercion. This
  //   should be the type set for `kind`.
  // * `inferred_type`: the type to verify implicit coercion to.
  // * `kind`: the SignatureArgumentKind for of the `type_set`.
  // * `error_message_type_name`: stringified type name to use in the error
  //    message (ex: "ARRAY" or "MAP").
  absl::Status TypeSetImplicitlyCoercesTo(
      const SignatureArgumentKindTypeSet& type_set, const Type* inferred_type,
      const SignatureArgumentKind& kind,
      absl::string_view error_message_type_name) const;

  // Determines the resolved Type related to all of the templated types present
  // in a function signature. <templated_argument_map> must have been populated
  // by CheckArgumentTypesAndCollectTemplatedArguments().
  absl::Status DetermineResolvedTypesForTemplatedArguments(
      const ArgKindToInputTypesMap& templated_argument_map,
      ArgKindToResolvedTypeMap* resolved_templated_arguments) const;

  // Unifies `signature_type_kind` (function parameter type) and `type_set`
  // (passed argument type) to infer the concrete type of the function argument.
  absl::StatusOr<const Type*> InferTemplatedFunctionArgument(
      SignatureArgumentKind signature_type_kind,
      const SignatureArgumentKindTypeSet& type_set) const;
};

FunctionSignatureMatcher::FunctionSignatureMatcher(
    const LanguageOptions& language_options, const Coercer& coercer,
    bool allow_argument_coercion, TypeFactory* type_factory)
    : language_(language_options),
      coercer_(coercer),
      allow_argument_coercion_(allow_argument_coercion),
      type_factory_(type_factory) {}

std::string
FunctionSignatureMatcher::SignatureArgumentKindTypeSet::DebugString() const {
  switch (kind_) {
    case UNTYPED_NULL:
      return "UNTYPED NULL";
    case UNTYPED_EMPTY_ARRAY:
      return "UNTYPED []";
    case TYPED_ARGUMENTS: {
      std::string ret;
      bool first = true;
      for (const InputArgumentType& argument : typed_arguments_.arguments()) {
        absl::StrAppend(&ret, (first ? "" : ", "), argument.DebugString());
        first = false;
      }
      return ret;
    }
  }
}

std::string FunctionSignatureMatcher::ArgKindToInputTypesMapDebugString(
    const ArgKindToInputTypesMap& map) {
  std::string debug_string;
  for (const auto& map_entry : map) {
    absl::StrAppend(
        &debug_string,
        FunctionArgumentType::SignatureArgumentKindToString(map_entry.first),
        ":\n    ", map_entry.second.DebugString(), "\n");
  }
  return debug_string;
}

// Return the related template kind for `kind`, where `kind` is an ANY_N,
// or ARRAY_ANY_N. Other templated types are not supported by the function at
// this time.
SignatureArgumentKind ArrayRelatedTemplatedKind(SignatureArgumentKind kind) {
  switch (kind) {
    case ARG_TYPE_ANY_1:
      return ARG_ARRAY_TYPE_ANY_1;
    case ARG_TYPE_ANY_2:
      return ARG_ARRAY_TYPE_ANY_2;
    case ARG_ARRAY_TYPE_ANY_1:
      return ARG_TYPE_ANY_1;
    case ARG_ARRAY_TYPE_ANY_2:
      return ARG_TYPE_ANY_2;
    case ARG_TYPE_ANY_3:
      return ARG_ARRAY_TYPE_ANY_3;
    case ARG_ARRAY_TYPE_ANY_3:
      return ARG_TYPE_ANY_3;
    case ARG_TYPE_ANY_4:
      return ARG_ARRAY_TYPE_ANY_4;
    case ARG_ARRAY_TYPE_ANY_4:
      return ARG_TYPE_ANY_4;
    case ARG_TYPE_ANY_5:
      return ARG_ARRAY_TYPE_ANY_5;
    case ARG_ARRAY_TYPE_ANY_5:
      return ARG_TYPE_ANY_5;
    case ARG_RANGE_TYPE_ANY_1:
      // TODO: Remove range handling from this function in a
      // follow-up.
      return ARG_TYPE_ANY_1;
    default:
      break;
  }
  ABSL_LOG(ERROR) << "Unexpected ArrayRelatedTemplateKind: "
              << FunctionArgumentType::SignatureArgumentKindToString(kind);
  return kind;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool> FunctionSignatureMatcher::GetConcreteArgument(
    const FunctionArgumentType& argument, int num_occurrences,
    const ArgKindToResolvedTypeMap& templated_argument_map,
    std::unique_ptr<FunctionArgumentType>* output_argument) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);
  GOOGLESQL_RET_CHECK_NE(argument.kind(), ARG_TYPE_ARBITRARY);
  output_argument->reset();

  // Make a copy of the arg type options, so that we can clear the default
  // argument value. This is necessary because we will later construct a
  // FunctionSignature using this argument, and FunctionSignature construction
  // will fail its validity check if the default argument value is set for this
  // concrete argument.
  // It is assumed that in GetConcreteArguments the element in <input_arguments>
  // corresponding to <argument> here either has an explicitly provided value,
  // or already carries the default value as a literal which can be referenced
  // later. So in both cases it is safe to remove the default from the argument
  // options.
  FunctionArgumentTypeOptions options(argument.options());
  if (options.has_default()) {
    options.clear_default();
  }
  if (argument.IsTemplated() && !argument.IsRelation() && !argument.IsModel() &&
      !argument.IsConnection() && !argument.IsLambda() &&
      !argument.IsSequence() && !argument.IsGraph()) {
    const Type* const* found_type =
        googlesql_base::FindOrNull(templated_argument_map, argument.kind());
    if (found_type == nullptr) {
      return false;
    }
    GOOGLESQL_RET_CHECK_NE(*found_type, nullptr);

    *output_argument = std::make_unique<FunctionArgumentType>(
        *found_type, std::move(options), num_occurrences);
  } else if (argument.IsRelation()) {
    // Table-valued functions should return ARG_TYPE_RELATION. There is no Type
    // object in this case, so return a new FunctionArgumentType with
    // ARG_TYPE_RELATION and the specified number of occurrences.
    *output_argument = std::make_unique<FunctionArgumentType>(
        ARG_TYPE_RELATION, argument.options(), num_occurrences);
  } else if (argument.IsModel()) {
    *output_argument = std::make_unique<FunctionArgumentType>(
        ARG_TYPE_MODEL, argument.options(), num_occurrences);
  } else if (argument.IsConnection()) {
    *output_argument = std::make_unique<FunctionArgumentType>(
        ARG_TYPE_CONNECTION, argument.options(), num_occurrences);
  } else if (argument.IsSequence()) {
    *output_argument = std::make_unique<FunctionArgumentType>(
        ARG_TYPE_SEQUENCE, argument.options(), num_occurrences);
  } else if (argument.IsGraph()) {
    *output_argument = std::make_unique<FunctionArgumentType>(
        ARG_TYPE_GRAPH, argument.options(), num_occurrences);
  } else if (argument.IsLambda()) {
    std::vector<FunctionArgumentType> concrete_arg_types;
    for (const FunctionArgumentType& arg_type :
         argument.lambda().argument_types()) {
      std::unique_ptr<FunctionArgumentType> concrete_arg_type;
      GOOGLESQL_ASSIGN_OR_RETURN(
          const bool matches,
          GetConcreteArgument(arg_type, /*num_occurrences=*/1,
                              templated_argument_map, &concrete_arg_type));
      if (!matches) {
        return false;
      }
      GOOGLESQL_RET_CHECK_NE(concrete_arg_type->type(), nullptr);
      concrete_arg_types.push_back(*concrete_arg_type);
    }

    std::unique_ptr<FunctionArgumentType> concrete_expr_arg;
    GOOGLESQL_ASSIGN_OR_RETURN(
        const bool matches,
        GetConcreteArgument(argument.lambda().body_type(),
                            /*num_occurrences=*/1, templated_argument_map,
                            &concrete_expr_arg));
    if (!matches) {
      return false;
    }
    GOOGLESQL_RET_CHECK_NE(concrete_expr_arg->type(), nullptr);

    *output_argument =
        std::make_unique<FunctionArgumentType>(FunctionArgumentType::Lambda(
            concrete_arg_types, *concrete_expr_arg, argument.options()));
  } else if (argument.IsVoid()) {
    // This function is used to process both arguments and return types.
    // Procedures can have a VOID return type, which does not have an associated
    // googlesql::Type*, so we handle it explicitly here to avoid falling
    // into the default ARG_TYPE_FIXED case.
    *output_argument =
        std::make_unique<FunctionArgumentType>(ARG_TYPE_VOID, num_occurrences);
  } else {
    *output_argument = std::make_unique<FunctionArgumentType>(
        argument.type(), std::move(options), num_occurrences);
  }

  // Set the original templated kind for the concrete argument.
  output_argument->get()->set_original_kind(argument.kind());
  return true;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<FunctionArgumentTypeList>
FunctionSignatureMatcher::GetConcreteArguments(
    absl::Span<const InputArgumentType> input_arguments,
    const FunctionSignature& signature, int repetitions, int optionals,
    const ArgKindToResolvedTypeMap& templated_argument_map,
    std::vector<ArgIndexEntry>* arg_index_mapping) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);
  if (signature.NumOptionalArguments() == 0 &&
      signature.NumRepeatedArguments() == 0) {
    GOOGLESQL_RET_CHECK(arg_index_mapping == nullptr ||
              input_arguments.size() == arg_index_mapping->size());

    // Fast path for functions without optional or repeated arguments
    // to resolve.
    FunctionArgumentTypeList resolved_argument_list;
    resolved_argument_list.reserve(signature.arguments().size());
    for (int i = 0; i < signature.arguments().size(); ++i) {
      const FunctionArgumentType& signature_argument = signature.argument(i);
      if (signature_argument.kind() == ARG_TYPE_ARBITRARY) {
        // For arbitrary type arguments the type is derived from the input.
        resolved_argument_list.emplace_back(input_arguments[i].type(),
                                            signature_argument.options(), 1);
        resolved_argument_list.back().set_original_kind(
            signature_argument.kind());
      } else {
        std::unique_ptr<FunctionArgumentType> argument_type;
        // GetConcreteArgument may fail if templated argument's type is not
        // in the map. This can only happen if num_occurrences=0, so it is
        // not expected here.
        GOOGLESQL_ASSIGN_OR_RETURN(
            const bool matches,
            GetConcreteArgument(signature_argument, /*num_occurrences=*/1,
                                templated_argument_map, &argument_type));
        GOOGLESQL_RET_CHECK(matches);
        resolved_argument_list.push_back(std::move(*argument_type));
      }
      if (arg_index_mapping != nullptr) {
        (*arg_index_mapping)[i].concrete_signature_arg_index = i;
      }
    }
    return resolved_argument_list;
  }

  bool has_repeated_arbitrary = false;
  for (const FunctionArgumentType& signature_argument : signature.arguments()) {
    if (signature_argument.repeated() &&
        signature_argument.kind() == ARG_TYPE_ARBITRARY) {
      has_repeated_arbitrary = true;
    }
  }

  FunctionArgumentTypeList resolved_argument_list;
  resolved_argument_list.reserve(signature.arguments().size());
  int first_repeated_index = signature.FirstRepeatedArgumentIndex();
  int last_repeated_index = signature.LastRepeatedArgumentIndex();
  int input_position = 0;
  // Note: this isn't a simple loop. In the case of repeated arguments
  // we may repeat values of 'sig_arg_index' multiple times.
  for (int sig_arg_index = 0; sig_arg_index < signature.arguments().size();
       ++sig_arg_index) {
    const FunctionArgumentType& signature_argument =
        signature.argument(sig_arg_index);
    int num_occurrences = 1;
    if (signature_argument.repeated()) {
      num_occurrences =
          (has_repeated_arbitrary && repetitions > 0) ? 1 : repetitions;
    } else if (signature_argument.optional()) {
      if (optionals == 0) {
        num_occurrences = 0;
      }
      optionals -= num_occurrences;
    }

    if (num_occurrences > 0) {
      // Sanity check about the default signature_argument value.
      GOOGLESQL_RET_CHECK_LT(input_position, input_arguments.size());
      const InputArgumentType& input_arg = input_arguments[input_position];
      if (input_arg.is_default_argument_value()) {
        GOOGLESQL_RET_CHECK(input_arg.is_literal()) << input_arg.DebugString();
        GOOGLESQL_RET_CHECK(signature_argument.HasDefault())
            << signature_argument.DebugString() << "; "
            << input_arg.DebugString();
      }

      // Note, num_occurrences for repeated arbitrary is always 1 here because
      // each argument can have a different type.

      if (arg_index_mapping != nullptr) {
        GOOGLESQL_RET_CHECK_LT(input_position, arg_index_mapping->size());
        // In most cases, the concrete_signature_arg_index will match the
        // sig_arg_index. But with ARBITRARY REPEATED we 'expand' the
        // concrete signature to be 1:1 with input arguments.
        int concrete_signature_arg_index =
            has_repeated_arbitrary
                ? static_cast<int>(resolved_argument_list.size())
                : sig_arg_index;
        (*arg_index_mapping)[input_position].concrete_signature_arg_index =
            concrete_signature_arg_index;
      }
    }
    if (signature_argument.kind() == ARG_TYPE_ARBITRARY) {
      // Make a copy of the arg type options, so that we can clear the default
      // signature_argument value to avoid conflicting with the concrete type
      // which is a fatal error FunctionSignature::IsValid(). It is assumed that
      // the <signature_argument> already carries the default value as a literal
      // which can be referenced later. So it is safe to remove the default from
      // the signature_argument options.
      FunctionArgumentTypeOptions options(signature_argument.options());
      options.clear_default();
      if (num_occurrences > 0) {
        resolved_argument_list.emplace_back(
            input_arguments[input_position].type(), options, 1);
      } else {
        resolved_argument_list.emplace_back(signature_argument.kind(), options,
                                            num_occurrences);
      }
    } else {
      std::unique_ptr<FunctionArgumentType> argument_type;
      // GetConcreteArgument may fail if templated signature_argument's type is
      // not in the map. This can only happen if num_occurrences=0.
      GOOGLESQL_ASSIGN_OR_RETURN(
          const bool matches,
          GetConcreteArgument(signature_argument, num_occurrences,
                              templated_argument_map, &argument_type));
      if (!matches) {
        GOOGLESQL_RET_CHECK_EQ(0, num_occurrences);
        argument_type = std::make_unique<FunctionArgumentType>(
            signature_argument.kind(), signature_argument.cardinality(), 0);
      }
      resolved_argument_list.push_back(std::move(*argument_type));
    }
    resolved_argument_list.back().set_original_kind(signature_argument.kind());

    if (sig_arg_index == last_repeated_index) {
      // This is the end of the block of repeated arguments.
      // Decrease "repetitions" by the num_occurrences we've just output,
      // And, if necessary, go back to the first repeated signature_argument
      // again.
      repetitions -= num_occurrences;
      if (repetitions > 0) {
        sig_arg_index = first_repeated_index - 1;
      }
    }
    input_position += num_occurrences;
  }
  GOOGLESQL_RET_CHECK_EQ(0, optionals);
  return resolved_argument_list;
}
}  // namespace

// Assumes availability of local variable `signature_match_result`,
// `arg_idx` and `signature_argument`.
#define SET_MISMATCH_ERROR_WITH_INDEX(msg)                                     \
  if (signature_match_result->allow_mismatch_message()) {                      \
    if (signature_argument.has_argument_name() && arg_ast_node != nullptr &&   \
        arg_ast_node->Is<ASTNamedArgument>()) {                                \
      signature_match_result->set_mismatch_message(absl::StrCat(               \
          "Named argument ",                                                   \
          ToAlwaysQuotedIdentifierLiteral(signature_argument.argument_name()), \
          ": ", msg));                                                         \
    } else {                                                                   \
      signature_match_result->set_mismatch_message(                            \
          absl::StrCat("Argument ", arg_idx + 1, ": ", msg));                  \
    }                                                                          \
  }

// Assumes availability of local variable `signature_match_result`.
#define SET_MISMATCH_ERROR(msg)                           \
  if (signature_match_result->allow_mismatch_message()) { \
    signature_match_result->set_mismatch_message(msg);    \
  }

NOINLINE_PREVENT_HUGE_STACK_FRAMES
bool SignatureArgumentCountMatches(
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> input_arguments, int* repetitions,
    int* optionals, SignatureMatchResult* signature_match_result) {
  const int signature_num_required = signature.NumRequiredArguments();

  *repetitions = 0;
  *optionals = 0;

  const int input_arguments_size = input_arguments.size();
  if (signature_num_required == input_arguments_size) {
    // Fast path: exactly the required arguments passed, return early.
    return true;
  }

  absl::string_view argument_size_suffix;
  if (!input_arguments.empty() &&
      input_arguments.front().is_chained_function_call_input()) {
    argument_size_suffix = " (including chained function call input)";
  }
  if (!input_arguments.empty() &&
      input_arguments.front().is_pipe_input_table()) {
    argument_size_suffix = " (including pipe input table)";
  }

  auto optional_s = [](int num) { return num == 1 ? "" : "s"; };
  if (signature_num_required > input_arguments_size) {
    // Fast path: fewer required arguments provided, return early.
    SET_MISMATCH_ERROR(absl::StrFormat(
        "Signature requires at least %d argument%s, found %d argument%s%s",
        signature_num_required, optional_s(signature_num_required),
        input_arguments_size, optional_s(input_arguments_size),
        argument_size_suffix));
    return false;
  }

  const int signature_num_repeated = signature.NumRepeatedArguments();
  const int signature_num_optional = signature.NumOptionalArguments();

  // Initially qualify the signature based on the number of arguments, taking
  // into account optional and repeated arguments.  Find x and y such that:
  //   input_arguments_size = signature_num_required +
  //       x*signature_num_repeated + y
  // where 0 < y <= signature_num_optional.
  if (signature_num_repeated > 0) {
    while (input_arguments_size > signature_num_required +
                                      *repetitions * signature_num_repeated +
                                      signature_num_optional) {
      ++(*repetitions);
    }
  }

  if (signature_num_repeated == 0 &&
      input_arguments_size > signature.arguments().size()) {
    const int sig_arguments_size =
        static_cast<int>(signature.arguments().size());
    SET_MISMATCH_ERROR(absl::StrCat(
        "Signature accepts at most ", sig_arguments_size, " argument",
        optional_s(sig_arguments_size), ", found ", input_arguments_size,
        " argument", optional_s(input_arguments_size), argument_size_suffix));
    return false;
  }

  const int input_remainder_size = input_arguments_size -
                                   signature_num_required -
                                   *repetitions * signature_num_repeated;
  if (input_remainder_size < 0) {
    // We have calculated too many repetitions. Since the repetitions takes
    // optionals into account, this means the number of repeated arguments
    // provided isn't a multiple of the number of repeated arguments in the
    // signature.
    const int num_repeated_actual =
        *repetitions * signature_num_repeated + input_remainder_size;
    SET_MISMATCH_ERROR(absl::StrCat(
        "Wrong number of repeated arguments provided. Expected a multiple of ",
        signature_num_repeated, " but got ", num_repeated_actual,
        " repeated argument", optional_s(num_repeated_actual)));
    return false;
  }

  if (input_remainder_size > signature_num_optional) {
    // We do not have enough optionals to match the arguments size, and
    // repeating the repeated block again would require too many arguments.
    SET_MISMATCH_ERROR(absl::StrCat(
        input_remainder_size, " optional argument",
        optional_s(input_remainder_size),
        " provided while signature has at most ", signature_num_optional,
        " optional argument", optional_s(signature_num_optional)));
    return false;
  }

  *optionals = input_remainder_size;
  return true;
}

namespace {

bool IsArgKind_ARRAY_ANY_K(SignatureArgumentKind kind) {
  return kind == ARG_ARRAY_TYPE_ANY_1 || kind == ARG_ARRAY_TYPE_ANY_2 ||
         kind == ARG_ARRAY_TYPE_ANY_3 || kind == ARG_ARRAY_TYPE_ANY_4 ||
         kind == ARG_ARRAY_TYPE_ANY_5;
}

// Shorthand for making resolved function argument with lambda.
std::unique_ptr<const ResolvedFunctionArgument> MakeResolvedFunctionArgument(
    std::unique_ptr<const ResolvedInlineLambda> resolved_inline_lambda) {
  std::unique_ptr<ResolvedFunctionArgument> arg =
      googlesql::MakeResolvedFunctionArgument();
  arg->set_inline_lambda(std::move(resolved_inline_lambda));
  return arg;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool>
FunctionSignatureMatcher::CheckResolveLambdaTypeAndCollectTemplatedArguments(
    const int arg_idx, const ASTNode* arg_ast_node,
    const FunctionArgumentType& signature_argument,
    const InputArgumentType& input_argument,
    const ResolveLambdaCallback* resolve_lambda_callback,
    ArgKindToInputTypesMap* templated_argument_map,
    SignatureMatchResult* signature_match_result,
    std::vector<FunctionArgumentOverride>* arg_overrides) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);
  GOOGLESQL_RET_CHECK(arg_overrides);
  GOOGLESQL_RET_CHECK(arg_ast_node->Is<ASTLambda>());

  // Get lambda argument names from AST
  const ASTLambda* ast_lambda = arg_ast_node->GetAs<ASTLambda>();
  absl::StatusOr<std::vector<IdString>> arg_names_or =
      ExtractLambdaArgumentNames(ast_lambda);
  // Lambda argument names are already validated by
  // ValidateLambdaArgumentListIsIdentifierList before signature matching so it
  // shouldn't fail here.
  GOOGLESQL_RET_CHECK_OK(arg_names_or.status())
      << "Failed to extract lambda argument names";
  const std::vector<IdString>& arg_names = arg_names_or.value();

  // Check that number of lambda arguments match signature.
  const FunctionArgumentType::ArgumentTypeLambda& arg_type_lambda =
      signature_argument.lambda();
  const FunctionArgumentTypeList& sig_arg_types =
      arg_type_lambda.argument_types();
  if (arg_names.size() != sig_arg_types.size()) {
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("lambda requires %d arguments but %d is provided",
                        sig_arg_types.size(), arg_names.size()));
    return false;
  }

  // Get types of lambda argument list.
  std::vector<const Type*> concrete_arg_types;
  concrete_arg_types.reserve(sig_arg_types.size());
  for (int sig_arg_idx = 0; sig_arg_idx < sig_arg_types.size(); sig_arg_idx++) {
    const FunctionArgumentType& sig_arg_type = sig_arg_types[sig_arg_idx];
    // Use the type if it is explicitly specified by signature.
    if (sig_arg_type.type() != nullptr) {
      concrete_arg_types.push_back(sig_arg_type.type());
      continue;
    }

    // This argument is a templated, find it from <templated_argument_map>.
    SignatureArgumentKindTypeSet* param_typeset =
        googlesql_base::FindOrNull(*templated_argument_map, sig_arg_type.kind());
    // FunctionSignature::IsValid() guarantees that a templated argument of
    // lambda must have been seen before the lambda argument.
    if (param_typeset == nullptr) {
      SET_MISMATCH_ERROR(absl::StrFormat("Failed to infer type %s",
                                         sig_arg_type.DebugString()));
      return false;
    }
    // Untyped arguments get their types after all templated args are
    // registered. But lambdas are resolved before the registration completes.
    // We are not able to infer types of lambda arguments.
    if (param_typeset->kind() !=
        SignatureArgumentKindTypeSet::TYPED_ARGUMENTS) {
      SET_MISMATCH_ERROR_WITH_INDEX(
          absl::StrFormat("failed to infer type for %d-th argument (%s) of "
                          "lambda from other untyped arguments",
                          sig_arg_idx, arg_names[sig_arg_idx].ToString()));
      return false;
    }
    // Get the type from typeset.
    const Type* common_supertype = nullptr;
    const absl::Status s = coercer_.GetCommonSuperType(
        param_typeset->typed_arguments(), &common_supertype);
    // Failed to deduce type
    if (!s.ok() || common_supertype == nullptr) {
      if (ShouldPropagateError(s)) {
        return s;
      }
      SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
          "failed to infer type for %d-th argument (%s) for lambda because "
          "common super type of templated arguments doesn't exist",
          sig_arg_idx, arg_names[sig_arg_idx].ToString()));
      return false;
    }
    concrete_arg_types.push_back(common_supertype);
  }

  const Type* body_result_type = arg_type_lambda.body_type().type();
  bool body_result_type_is_from_template_map = false;
  // If lambda body is templated and we already have a registered concrete type
  // for it, use that type as body result type.
  if (arg_type_lambda.body_type().IsTemplated()) {
    SignatureArgumentKindTypeSet* param_typeset = googlesql_base::FindOrNull(
        *templated_argument_map, arg_type_lambda.body_type().kind());
    if (param_typeset != nullptr) {
      // Get the type from typeset.
      const Type* common_supertype = nullptr;
      const absl::Status s = coercer_.GetCommonSuperType(
          param_typeset->typed_arguments(), &common_supertype);
      if (ShouldPropagateError(s)) {
        return s;
      }
      if (s.ok() && common_supertype != nullptr) {
        body_result_type = common_supertype;
        body_result_type_is_from_template_map = true;
      }
    }
  }

  // Resolve the lambda.
  std::unique_ptr<const ResolvedInlineLambda> resolved_lambda;
  GOOGLESQL_RET_CHECK(resolve_lambda_callback != nullptr)
      << "Cannot resolve lambda argument with a nullptr callback";
  const absl::Status s = (*resolve_lambda_callback)(
      ast_lambda, arg_names, concrete_arg_types, body_result_type,
      allow_argument_coercion_, &resolved_lambda);
  if (!s.ok()) {
    if (ShouldPropagateError(s)) {
      return s;
    }
    SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
        "failed to resolve lambda body, error: %s", s.message()));
    return false;
  }
  GOOGLESQL_RET_CHECK(resolved_lambda != nullptr);

  const Type* resolved_body_type = resolved_lambda->body()->type();
  // Body result type doesn't match signature specification.
  if (body_result_type != nullptr &&
      !resolved_body_type->Equals(body_result_type)) {
    SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
        "expected the lambda body of type %s, found %s",
        ShortTypeName(body_result_type), ShortTypeName(resolved_body_type)));
    return false;
  }

  if (arg_type_lambda.body_type().IsTemplated() &&
      !body_result_type_is_from_template_map) {
    // Update <templated_argument_map> with lambda expr.
    // This is useful when this lambda's body result type determines:
    //   * the return type of the function is decided by the.
    //   * the argument type of another lambda.
    InputArgumentType body_arg_type(resolved_lambda->body()->type());
    GOOGLESQL_ASSIGN_OR_RETURN(bool result,
                     CheckSingleInputArgumentTypeAndCollectTemplatedArgument(
                         /*arg_idx=*/-1, ast_lambda->body(), body_arg_type,
                         arg_type_lambda.body_type(), resolve_lambda_callback,
                         templated_argument_map, signature_match_result,
                         /*arg_overrides=*/nullptr));
    if (!result) {
      if (signature_match_result->mismatch_message().empty()) {
        SET_MISMATCH_ERROR_WITH_INDEX(
            absl::StrFormat("lambda body type %s is not compatible with other "
                            "arguments for the templated type %s",
                            ShortTypeName(resolved_body_type),
                            UserFacingName(arg_type_lambda.body_type())));
      }
      return false;
    }
  }

  // Add resolved lambda to arg override list.
  arg_overrides->push_back(FunctionArgumentOverride{
      arg_idx, MakeResolvedFunctionArgument(std::move(resolved_lambda))});
  return true;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool>
FunctionSignatureMatcher::CheckArgumentTypesAndCollectTemplatedArguments(
    const std::vector<const ASTNode*>& arg_ast_nodes,
    absl::Span<const InputArgumentType> input_arguments,
    const FunctionSignature& signature, int repetitions,
    const ResolveLambdaCallback* resolve_lambda_callback,
    ArgKindToInputTypesMap* templated_argument_map,
    SignatureMatchResult* signature_match_result,
    std::vector<FunctionArgumentOverride>* arg_overrides) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  const int repeated_idx_start = signature.FirstRepeatedArgumentIndex();
  const int repeated_idx_end = signature.LastRepeatedArgumentIndex();
  int signature_arg_idx = 0;
  int repetition_idx = 0;
  for (int arg_idx = 0; arg_idx < input_arguments.size(); ++arg_idx) {
    if (repetitions == 0 && signature_arg_idx == repeated_idx_start) {
      // There are zero repetitions, skip past all repeated args in the
      // signature.
      signature_arg_idx = repeated_idx_end + 1;
      GOOGLESQL_RET_CHECK_LT(signature_arg_idx, signature.arguments().size());
    }
    const FunctionArgumentType& signature_argument =
        signature.argument(signature_arg_idx);

    const InputArgumentType& input_argument = input_arguments[arg_idx];
    const ASTNode* arg_ast = nullptr;
    if (arg_idx < arg_ast_nodes.size()) {
      arg_ast = arg_ast_nodes[arg_idx];
    }
    GOOGLESQL_ASSIGN_OR_RETURN(bool result,
                     CheckSingleInputArgumentTypeAndCollectTemplatedArgument(
                         arg_idx, arg_ast, input_argument, signature_argument,
                         resolve_lambda_callback, templated_argument_map,
                         signature_match_result, arg_overrides));
    if (!result) {
      GOOGLESQL_RET_CHECK(!signature_match_result->allow_mismatch_message() ||
                !signature_match_result->mismatch_message().empty())
          << "Mismatch error message should have been set.";
      return false;
    }

    // Update signature_arg_idx, rewinding for repeated blocks the appropriate
    // number of times.
    if (signature_argument.repeated() &&
        signature_arg_idx == repeated_idx_end &&
        repetition_idx < repetitions - 1) {
      // Need to iterate through the repeated block again.
      ++repetition_idx;
      signature_arg_idx = repeated_idx_start;
    } else {
      ++signature_arg_idx;
    }
  }

  // Ensure that templated result types that have their containing type
  // referenced in the argument map also have an entry in the argument map,
  // creating an untyped NULL if necessary.
  const SignatureArgumentKind result_kind = signature.result_type().kind();
  if (IsArgKind_ARRAY_ANY_K(result_kind) &&
      googlesql_base::ContainsKey(*templated_argument_map,
                       ArrayRelatedTemplatedKind(result_kind))) {
    // Creates an UNTYPED_NULL if no entry exists.
    (*templated_argument_map)[result_kind];
  }
  if (result_kind == ARG_PROTO_MAP_ANY &&
      (googlesql_base::ContainsKey(*templated_argument_map, ARG_PROTO_MAP_KEY_ANY) ||
       googlesql_base::ContainsKey(*templated_argument_map, ARG_PROTO_MAP_VALUE_ANY))) {
    (*templated_argument_map)[result_kind];
  }
  if (result_kind == ARG_RANGE_TYPE_ANY_1 &&
      googlesql_base::ContainsKey(*templated_argument_map, ARG_TYPE_ANY_1)) {
    (*templated_argument_map)[result_kind];
  }
  if ((result_kind == ARG_MAP_TYPE_ANY_1_2 &&
       (googlesql_base::ContainsKey(*templated_argument_map, ARG_TYPE_ANY_1) ||
        googlesql_base::ContainsKey(*templated_argument_map, ARG_TYPE_ANY_2)))) {
    (*templated_argument_map)[result_kind];
  }

  return true;
}

namespace {
// Computes the key and value types for the entries in a proto map (i.e. an
// array of protos with the map_entry option set to true).
struct MapEntryTypes {
  const Type* key_type;
  const Type* value_type;
};
absl::StatusOr<MapEntryTypes> GetMapEntryTypes(const Type* map_type,
                                               TypeFactory& factory) {
  GOOGLESQL_RET_CHECK(IsProtoMap(map_type)) << map_type->DebugString();

  const ProtoType* map_entry_type =
      map_type->AsArray()->element_type()->AsProto();
  const Type* key_type;
  GOOGLESQL_RETURN_IF_ERROR(factory.GetProtoFieldType(
      map_entry_type->map_key(), map_entry_type->CatalogNamePath(), &key_type));
  const Type* value_type;
  GOOGLESQL_RETURN_IF_ERROR(factory.GetProtoFieldType(map_entry_type->map_value(),
                                            map_entry_type->CatalogNamePath(),
                                            &value_type));
  return {{key_type, value_type}};
}

#define SET_ARG_KIND_MISMATCH_ERROR()                                     \
  SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(                          \
      "expected %s, found %s",                                            \
      signature_argument.UserFacingName(language_.product_mode(),         \
                                        /*print_template_details=*/true), \
      input_argument.UserFacingName(language_.product_mode())));

}  // namespace

// Utility used by above function
NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool> FunctionSignatureMatcher::
    CheckSingleInputArgumentTypeAndCollectTemplatedArgument(
        const int arg_idx, const ASTNode* arg_ast_node,
        const InputArgumentType& input_argument,
        const FunctionArgumentType& signature_argument,
        const ResolveLambdaCallback* resolve_lambda_callback,
        ArgKindToInputTypesMap* templated_argument_map,
        SignatureMatchResult* signature_match_result,
        std::vector<FunctionArgumentOverride>* arg_overrides) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  // Compare the input argument to the signature argument. Note that
  // the array types are handled differently because they have the same
  // kind even if the element types are different.
  if (signature_argument.IsRelation()) {
    if (signature_argument.optional() && input_argument.is_untyped_null()) {
      // Allow optional relations to be skipped in the function call.
      return true;
    }
  }
  if (signature_argument.IsRelation() != input_argument.is_relation()) {
    // Relation signature argument types match only relation input arguments.
    // No other signature argument types match relation input arguments.
    SET_ARG_KIND_MISMATCH_ERROR();
    return false;
  }
  if (signature_argument.IsModel() != input_argument.is_model()) {
    // Model signature argument types match only model input arguments.
    // No other signature argument types match model input arguments.
    SET_ARG_KIND_MISMATCH_ERROR();
    return false;
  }
  if (signature_argument.IsConnection() != input_argument.is_connection()) {
    // Connection signature argument types match only connection input
    // arguments.
    // No other signature argument types match connection input arguments.
    SET_ARG_KIND_MISMATCH_ERROR();
    return false;
  }
  if (signature_argument.IsSequence() != input_argument.is_sequence()) {
    // Sequence signature argument types match only sequence input
    // arguments.
    SET_ARG_KIND_MISMATCH_ERROR();
    return false;
  }
  if (signature_argument.IsLambda() != input_argument.is_lambda()) {
    // Lambda signature argument types match only lambda input arguments.
    // No other signature argument types match lambda input arguments.
    SET_ARG_KIND_MISMATCH_ERROR();
    return false;
  }
  if (signature_argument.IsRelation()) {
    bool signature_matches = false;
    const absl::Status status =
        CheckRelationArgumentTypes(arg_idx, input_argument, signature_argument,
                                   signature_match_result, &signature_matches);
    GOOGLESQL_RET_CHECK_OK(status);
    if (!signature_matches) return false;
  } else if (signature_argument.IsModel()) {
    GOOGLESQL_RET_CHECK(input_argument.is_model());
    // We currently only support ANY MODEL signatures and there is no need to
    // to check for coercion given that the models are templated.
  } else if (signature_argument.IsConnection()) {
    GOOGLESQL_RET_CHECK(input_argument.is_connection());
    // We currently only support ANY CONNECTION signatures and there is no
    // need to to check for coercion given that the connections are templated.
  } else if (signature_argument.IsSequence()) {
    GOOGLESQL_RET_CHECK(input_argument.is_sequence());
  } else if (signature_argument.kind() == ARG_TYPE_ARBITRARY) {
    // Arbitrary kind arguments match any input argument type.
  } else if (signature_argument.IsLambda()) {
    GOOGLESQL_RET_CHECK(arg_overrides)
        << "Resolved lambdas need to be put into arg_overrides";
    return CheckResolveLambdaTypeAndCollectTemplatedArguments(
        arg_idx, arg_ast_node, signature_argument, input_argument,
        resolve_lambda_callback, templated_argument_map, signature_match_result,
        arg_overrides);
  } else if (!signature_argument.IsTemplated()) {
    // Input argument type must either be equivalent or (if coercion is
    // allowed) coercible to signature argument type.
    if (!input_argument.type()->Equivalent(signature_argument.type()) &&
        (!allow_argument_coercion_ ||
         (!coercer_.CoercesTo(input_argument, signature_argument.type(),
                              /*is_explicit=*/false, signature_match_result) &&
          !signature_argument.AllowCoercionFrom(input_argument.type())))) {
      SET_MISMATCH_ERROR_WITH_INDEX(
          absl::StrFormat("Unable to coerce type %s to expected type %s",
                          ShortTypeName(input_argument.type()),
                          ShortTypeName(signature_argument.type())));
      return false;
    }
  } else if (input_argument.is_untyped()) {
    // Templated argument, input is an untyped NULL, empty array or empty map.
    // We create an empty entry for them if one does not already exist.
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool result,
        CheckSingleInputArgumentTypeAndCollectTemplatedArgumentUntyped(
            arg_idx, arg_ast_node, input_argument, signature_argument,
            templated_argument_map, signature_match_result));
    if (!result) return result;
  } else {
    // Templated argument, input is not null.
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool result,
        CheckSingleInputArgumentTypeAndCollectTemplatedArgumentTemplated(
            arg_idx, arg_ast_node, input_argument, signature_argument,
            templated_argument_map, signature_match_result));
    if (!result) return result;
  }

  // Handle graph node types.
  return CheckSingleInputArgumentTypeAndCollectTemplatedArgumentGraph(
      arg_idx, arg_ast_node, input_argument, signature_argument,
      templated_argument_map, signature_match_result);
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool> FunctionSignatureMatcher::
    CheckSingleInputArgumentTypeAndCollectTemplatedArgumentUntyped(
        int arg_idx, const ASTNode* arg_ast_node,
        const InputArgumentType& input_argument,
        const FunctionArgumentType& signature_argument,
        ArgKindToInputTypesMap* templated_argument_map,
        SignatureMatchResult* signature_match_result) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  const SignatureArgumentKind kind = signature_argument.kind();
  SignatureArgumentKindTypeSet& type_set = (*templated_argument_map)[kind];
  if (type_set.kind() != SignatureArgumentKindTypeSet::TYPED_ARGUMENTS &&
      input_argument.is_untyped_empty_array()) {
    type_set.SetToUntypedEmptyArray();
  }
  // When adding an entry for ARRAY_ANY_K, we must also have one for ANY_K.
  if (IsArgKind_ARRAY_ANY_K(kind)) {
    // Initializes to UNTYPED_NULL if not already set.
    (*templated_argument_map)[ArrayRelatedTemplatedKind(kind)];
  }
  if (kind == ARG_PROTO_MAP_ANY) {
    // It is not possible to infer the type of a map entry proto, because
    // they are not actually generic maps. The proto descriptor for a map
    // entry is specific to the field it is sourced from. So untyped null
    // is not resolvable for ARG_PROTO_MAP_ANY-taking function arguments.
    signature_match_result->incr_non_matched_arguments();
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("expected proto map type but found: %s",
                        UserFacingName(input_argument)));
    return false;
  }
  if (kind == ARG_PROTO_MAP_KEY_ANY || kind == ARG_PROTO_MAP_VALUE_ANY) {
    // We should always see the map type if we see the key or the value.
    // But they don't imply that we should see each other. For example,
    // DELETE_KEY(map, key) would not include the value type in its
    // signature's template types.
    (*templated_argument_map)[ARG_PROTO_MAP_ANY];
  }
  if (kind == ARG_RANGE_TYPE_ANY_1) {
    // Initializes to UNTYPED_NULL if not already set.
    // TODO: Investigate if this is necessary/correct.
    (*templated_argument_map)[ARG_RANGE_TYPE_ANY_1];
  }
  if (kind == ARG_MEASURE_TYPE_ANY_1) {
    // Measure type is based on a specific definition and expression, it
    // cannot coerce, so it doesn't make sense to infer the type of a measure
    // from an untyped argument.
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("Measure cannot be inferred from untyped "
                        "argument: %s",
                        UserFacingName(input_argument)));
    return false;
  }
  // ARG_MAP_TYPE_ANY_1_2 is intentionally not handled here, because we return
  // an error when the key or value type can't be inferred.
  return true;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool> FunctionSignatureMatcher::
    CheckSingleInputArgumentTypeAndCollectTemplatedArgumentTemplated(
        int arg_idx, const ASTNode* arg_ast_node,
        const InputArgumentType& input_argument,
        const FunctionArgumentType& signature_argument,
        ArgKindToInputTypesMap* templated_argument_map,
        SignatureMatchResult* signature_match_result) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  // Templated argument, input is not null.
  SignatureArgumentKind signature_argument_kind = signature_argument.kind();

  // If it is a templated array type, but the argument type is not
  // an array, then they do not match. Undeclared query parameters are
  // coercible to arrays.
  if (IsArgKind_ARRAY_ANY_K(signature_argument_kind) &&
      !input_argument.type()->IsArray()) {
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("expected array type but found %s",
                        ShortTypeName(input_argument.type())));
    return false;
  }

  if (signature_argument_kind == ARG_PROTO_MAP_ANY &&
      !IsProtoMap(input_argument.type())) {
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("expected proto map type but found %s",
                        ShortTypeName(input_argument.type())));
    return false;
  }

  // If it is templated RANGE type, but the input argument type is not
  // RANGE, then they do not match.
  if (signature_argument_kind == ARG_RANGE_TYPE_ANY_1 &&
      !input_argument.type()->IsRangeType()) {
    SET_MISMATCH_ERROR_WITH_INDEX(
        absl::StrFormat("expected range type but found %s",
                        ShortTypeName(input_argument.type())));
    return false;
  }

  // If it is a templated enum/proto type, but the argument type is not
  // an enum/proto/struct, then they do not match.
  if ((signature_argument_kind == ARG_ENUM_ANY &&
       !input_argument.type()->IsEnum()) ||
      (signature_argument_kind == ARG_PROTO_ANY &&
       !input_argument.type()->IsProto()) ||
      (signature_argument_kind == ARG_STRUCT_ANY &&
       !input_argument.type()->IsStruct())) {
    SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
        "expected %s but found %s",
        signature_argument.UserFacingName(language_.product_mode(),
                                          /*print_template_details=*/true),
        ShortTypeName(input_argument.type())));
    return false;
  }

  // Collect input arguments related to a signature's templated argument
  // for subsequent type coercion and lambda argument type inference.
  (*templated_argument_map)[signature_argument_kind].InsertTypedArgument(
      input_argument);

  auto MakeConcreteArgument = [&](const Type* type) {
    if (input_argument.is_literal()) {
      // Any value will do.
      return InputArgumentType(Value::Null(type));
    } else {
      // Handles the non-literal and query parameter cases.
      return InputArgumentType(type, input_argument.is_query_parameter());
    }
  };

  // If ARRAY_ANY_K is associated with type ARRAY<T> in
  // 'templated_argument_map', then we always bind ANY_K to T.
  if (IsArgKind_ARRAY_ANY_K(signature_argument_kind)) {
    InputArgumentType new_argument =
        MakeConcreteArgument(input_argument.type()->AsArray()->element_type());
    const SignatureArgumentKind related_kind =
        ArrayRelatedTemplatedKind(signature_argument_kind);
    (*templated_argument_map)[related_kind].InsertTypedArgument(new_argument);
  }

  if (signature_argument_kind == ARG_RANGE_TYPE_ANY_1) {
    // Get T from RANGE<T> and put it as template_arg_map[ARG_ANY_1] = T
    // This is used to resolve function signature with RANGE<T> -> T
    InputArgumentType arg_type =
        MakeConcreteArgument(input_argument.type()->AsRange()->element_type());
    (*templated_argument_map)[ARG_TYPE_ANY_1].InsertTypedArgument(arg_type);
  }

  if (signature_argument_kind == ARG_PROTO_MAP_ANY) {
    // If this is a proto map argument, we can infer the templated types
    // for the key and value.
    absl::StatusOr<MapEntryTypes> entry_types =
        GetMapEntryTypes(input_argument.type(), *type_factory_);
    if (!entry_types.ok()) {
      if (ShouldPropagateError(entry_types.status())) {
        return entry_types.status();
      }
      SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
          "failed to infer type for proto map key or value with error: %s",
          entry_types.status().message()));
      return false;
    }
    // For map entry functions, the template type is dominant. Other arguments
    // must be coercible to this type.
    constexpr bool set_dominant = true;
    (*templated_argument_map)[ARG_PROTO_MAP_KEY_ANY].InsertTypedArgument(
        MakeConcreteArgument(entry_types->key_type), set_dominant);
    (*templated_argument_map)[ARG_PROTO_MAP_VALUE_ANY].InsertTypedArgument(
        MakeConcreteArgument(entry_types->value_type), set_dominant);
  }

  if (signature_argument_kind == ARG_MAP_TYPE_ANY_1_2) {
    if (!input_argument.type()->IsMap()) {
      SET_ARG_KIND_MISMATCH_ERROR();
      return false;
    }
    // The map template type is dominant. Other arguments must be coercible to
    // this type.
    const MapType* map_type = input_argument.type()->AsMap();
    constexpr bool set_dominant = true;
    (*templated_argument_map)[ARG_TYPE_ANY_1].InsertTypedArgument(
        MakeConcreteArgument(map_type->key_type()), set_dominant);
    (*templated_argument_map)[ARG_TYPE_ANY_2].InsertTypedArgument(
        MakeConcreteArgument(map_type->value_type()), set_dominant);
  }

  if (signature_argument_kind == ARG_MEASURE_TYPE_ANY_1) {
    if (!input_argument.type()->IsMeasureType()) {
      SET_ARG_KIND_MISMATCH_ERROR();
      return false;
    }
    // For MEASURE<T1>, store T in argument map for ARG_TYPE_ANY_1. This is
    // used to resolve function signatures like MEASURE<T1> -> T1.
    (*templated_argument_map)[ARG_TYPE_ANY_1].InsertTypedArgument(
        MakeConcreteArgument(input_argument.type()->AsMeasure()->result_type()),
        /*set_dominant=*/true);
  }

  return true;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::StatusOr<bool> FunctionSignatureMatcher::
    CheckSingleInputArgumentTypeAndCollectTemplatedArgumentGraph(
        int arg_idx, const ASTNode* arg_ast_node,
        const InputArgumentType& input_argument,
        const FunctionArgumentType& signature_argument,
        ArgKindToInputTypesMap* templated_argument_map,
        SignatureMatchResult* signature_match_result) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  const Type* input_type = input_argument.type();
  switch (signature_argument.kind()) {
    case ARG_TYPE_GRAPH_NODE:
    case ARG_TYPE_GRAPH_EDGE:
    case ARG_TYPE_GRAPH_ELEMENT:
      if (input_type == nullptr) {
        SET_ARG_KIND_MISMATCH_ERROR();
        return false;
      }
      if (!input_type->IsGraphElement()) {
        SET_ARG_KIND_MISMATCH_ERROR();
        return false;
      }
      if ((signature_argument.kind() == ARG_TYPE_GRAPH_EDGE) &&
          input_type->AsGraphElement()->IsNode()) {
        SET_ARG_KIND_MISMATCH_ERROR();
        return false;
      }
      if ((signature_argument.kind() == ARG_TYPE_GRAPH_NODE) &&
          input_type->AsGraphElement()->IsEdge()) {
        SET_ARG_KIND_MISMATCH_ERROR();
        return false;
      }
      break;
    case ARG_TYPE_GRAPH_PATH:
      if (input_type == nullptr || !input_type->IsGraphPath()) {
        SET_ARG_KIND_MISMATCH_ERROR();
        return false;
      }
      break;
    default:
      if (
          // Disallow graph types when the signature does not *explicitly* take
          // graph element types: even if the signature type is ANY.
          (!language_.LanguageFeatureEnabled(
               FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT) &&
           input_type != nullptr && input_type->IsGraphElement()) ||
          // Disallow measure types for templated arguments except
          // ARG_MEASURE_TYPE_ANY_1.
          (signature_argument.kind() != ARG_MEASURE_TYPE_ANY_1 &&
           input_type != nullptr && input_type->IsMeasureType()) ||
          // Disallow ROW types as any kind of function argument.
          (input_type != nullptr && input_type->IsRow())) {
        SET_MISMATCH_ERROR_WITH_INDEX(absl::StrFormat(
            "expected %s, found %s: which is not allowed for %s arguments",
            signature_argument.UserFacingName(language_.product_mode(),
                                              /*print_template_details=*/true),
            input_argument.UserFacingName(language_.product_mode()),
            signature_argument.UserFacingName(language_.product_mode())));
        return false;
      }
      break;
  }

  return true;
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status FunctionSignatureMatcher::CheckRelationArgumentTypes(
    int arg_idx, const InputArgumentType& input_argument,
    const FunctionArgumentType& signature_argument,
    SignatureMatchResult* signature_match_result,
    bool* signature_matches) const {
  if (!signature_argument.options().has_relation_input_schema()) {
    // Do nothing. As long as the input argument is a relation, the
    // signature matches.
    *signature_matches = true;
    return absl::OkStatus();
  }
  const TVFRelation& provided_schema = input_argument.relation_input_schema();
  const TVFRelation& required_schema =
      signature_argument.options().relation_input_schema();

  // Store the set of required column names for later reference.
  std::set<std::string, googlesql_base::CaseLess> required_col_names;
  for (const TVFRelation::Column& column : required_schema.columns()) {
    required_col_names.emplace(column.name);
  }

  // The input relation argument specifies a required schema. Start by building
  // a map from each provided column's name to its index in the ordered list of
  // columns in the relation.
  std::map<std::string, int, googlesql_base::CaseLess>
      provided_col_name_to_required_col_idx;
  for (int provided_col_idx = 0;
       provided_col_idx < provided_schema.num_columns(); ++provided_col_idx) {
    const std::string& provided_col_name =
        provided_schema.column(provided_col_idx).name;
    if (googlesql_base::ContainsKey(required_col_names, provided_col_name)) {
      if (!provided_col_name_to_required_col_idx
               .insert_or_assign(provided_col_name, provided_col_idx)
               .second) {
        // There was a duplicate column name in the input relation. This is
        // invalid.
        signature_match_result->set_mismatch_message(absl::StrCat(
            "Table-valued function does not allow duplicate input ",
            "columns named \"", provided_col_name, "\" for argument ",
            arg_idx + 1));
        signature_match_result->set_bad_argument_index(arg_idx);
        *signature_matches = false;
        return absl::OkStatus();
      }
    } else if (!signature_argument.options()
                    .extra_relation_input_columns_allowed() &&
               !required_schema.is_value_table() &&
               !provided_schema.is_value_table()) {
      // There was a column name in the input relation not specified in the
      // required output schema, and the signature does not allow this.
      signature_match_result->set_mismatch_message(
          absl::StrCat("Function does not allow extra input column named \"",
                       provided_col_name, "\" for argument ", arg_idx + 1));
      signature_match_result->set_bad_argument_index(arg_idx);
      *signature_matches = false;
      return absl::OkStatus();
    }
  }

  // Check that each provided column is either equivalent or coercible to
  // the corresponding required column.
  const int num_required_cols = required_schema.num_columns();
  for (int required_col_idx = 0; required_col_idx < num_required_cols;
       ++required_col_idx) {
    const std::string& required_col_name =
        required_schema.column(required_col_idx).name;
    const Type* required_col_type =
        required_schema.column(required_col_idx).type;

    // Find the index of the matching column in the input relation.
    int provided_col_idx = -1;
    if (required_schema.is_value_table()) {
      provided_col_idx = 0;
      if (provided_schema.num_columns() != 1) {
        // The required value table was not found in the provided input
        // relation. Generate a descriptive error message.
        GOOGLESQL_RET_CHECK_EQ(1, required_schema.num_columns());
        signature_match_result->set_mismatch_message(absl::StrCat(
            "Expected value table of type ",
            required_schema.column(0).type->ShortTypeName(
                language_.product_mode()),
            " for argument ", arg_idx + 1, "; got ",
            provided_schema.num_columns() == 0
                ? "no columns"
                : provided_schema.GetSQLDeclaration(language_.product_mode())));
        signature_match_result->set_bad_argument_index(arg_idx);
        *signature_matches = false;
        return absl::OkStatus();
      }
    } else {
      const int* lookup = googlesql_base::FindOrNull(provided_col_name_to_required_col_idx,
                                          required_col_name);
      if (lookup == nullptr) {
        // The required column name was not found in the provided input
        // relation. Generate a descriptive error message.
        signature_match_result->set_mismatch_message(absl::StrCat(
            "Required column \"", required_col_name,
            "\" not found in table passed as argument ", arg_idx + 1));
        signature_match_result->set_bad_argument_index(arg_idx);
        *signature_matches = false;
        return absl::OkStatus();
      }
      provided_col_idx = *lookup;
    }

    // Compare the required column type with the provided column type.
    const Type* provided_col_type =
        provided_schema.column(provided_col_idx).type;
    if (provided_col_type->Equals(required_col_type)) {
      // The provided column type is acceptable. Continue.
    } else if (SignatureMatchResult unused_result;
               allow_argument_coercion_ &&
               coercer_.CoercesTo(InputArgumentType(provided_col_type),
                                  required_col_type, /*is_explicit=*/false,
                                  &unused_result)) {
      // Make a note to coerce the relation argument later and continue.
      signature_match_result->AddTVFRelationCoercionEntry(
          arg_idx, provided_col_idx, required_col_type);
    } else {
      // The provided column type is invalid. Mark the argument index and
      // column name to return a descriptive error later.
      signature_match_result->set_mismatch_message(absl::StrCat(
          "Invalid type ",
          provided_col_type->ShortTypeName(language_.product_mode()),
          (required_schema.is_value_table()
               ? " for value table column with expected type \""
               : absl::StrCat(" for column \"", required_col_name, " ")),
          required_col_type->ShortTypeName(language_.product_mode()),
          "\" of argument ", arg_idx + 1));
      signature_match_result->set_bad_argument_index(arg_idx);
      *signature_matches = false;
      return absl::OkStatus();
    }
  }
  *signature_matches = true;
  return absl::OkStatus();
}

absl::Status FunctionSignatureMatcher::TypeSetImplicitlyCoercesTo(
    const SignatureArgumentKindTypeSet& type_set, const Type* inferred_type,
    const SignatureArgumentKind& kind,
    absl::string_view error_message_type_name) const {
  GOOGLESQL_RET_CHECK_EQ(type_set.kind(), SignatureArgumentKindTypeSet::TYPED_ARGUMENTS);

  for (const InputArgumentType& argument :
       type_set.typed_arguments().arguments()) {
    SignatureMatchResult unused_result;
    if (!coercer_.CoercesTo(argument, inferred_type,
                            /*is_explicit=*/false, &unused_result)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Unable to coerce type %s to inferred %s type %s for "
          "argument %s",
          UserFacingName(argument), error_message_type_name,
          ShortTypeName(inferred_type),
          FunctionArgumentType::SignatureArgumentKindToString(kind)));
    }
  }
  return absl::OkStatus();
}

NOINLINE_PREVENT_HUGE_STACK_FRAMES
absl::Status
FunctionSignatureMatcher::DetermineResolvedTypesForTemplatedArguments(
    const ArgKindToInputTypesMap& templated_argument_map,
    ArgKindToResolvedTypeMap* resolved_templated_arguments) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);
  for (const auto& templated_argument_entry : templated_argument_map) {
    const SignatureArgumentKind& kind = templated_argument_entry.first;

    const SignatureArgumentKindTypeSet& type_set =
        templated_argument_entry.second;

    if (kind == ARG_PROTO_MAP_VALUE_ANY || kind == ARG_PROTO_MAP_KEY_ANY) {
      if (type_set.kind() != SignatureArgumentKindTypeSet::TYPED_ARGUMENTS) {
        continue;
      }

      // For the key and the value kinds of a proto map, ensure any arguments
      // can be coerced to the dominant type. The dominant type will be the one
      // indicated by the proto map template argument.
      const InputArgumentType* dominant_type =
          type_set.typed_arguments().dominant_argument();
      if (dominant_type == nullptr) {
        ABSL_DLOG(FATAL) << "Dominant type should be set for map key and value in "
                    << "all cases";
        return absl::InvalidArgumentError(absl::StrCat(
            "Unable to determine type for ",
            FunctionArgumentType::SignatureArgumentKindToString(kind)));
      }

      for (const InputArgumentType& other_type :
           type_set.typed_arguments().arguments()) {
        SignatureMatchResult unused_result;
        if (!coercer_.CoercesTo(other_type, dominant_type->type(),
                                /*is_explicit=*/false, &unused_result)) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Unable to coerce type %s to resolved type %s for %s",
              UserFacingName(other_type), ShortTypeName(dominant_type->type()),
              FunctionArgumentType::SignatureArgumentKindToString(kind)));
        }
      }
      (*resolved_templated_arguments)[kind] = dominant_type->type();
    } else if (kind == ARG_RANGE_TYPE_ANY_1) {
      const Type** element_type =
          googlesql_base::FindOrNull(*resolved_templated_arguments, ARG_TYPE_ANY_1);

      if (element_type != nullptr) {
        // element_type is not null, meaning ARG_TYPE_ANY_1 was already
        // seen and resolved, which is used as a subtype for RANGE, such as DATE
        // This is used for the RANGE constructor function
        const RangeType* range_type;
        GOOGLESQL_RETURN_IF_ERROR(
            type_factory_->MakeRangeType(*element_type, &range_type));
        // Use 'new_range_type'. InsertOrDie() is safe because 'kind' only
        // occurs once in 'templated_argument_map'.
        googlesql_base::InsertOrDie(resolved_templated_arguments, kind, range_type);
      } else {
        GOOGLESQL_RETURN_IF_ERROR(
            InferTemplatedFunctionArgument(kind, type_set).status());
        // Unless it's possible to create an unresolved templated RANGE<T> type,
        // this path should never be reached.
        GOOGLESQL_RET_CHECK_FAIL() << "Found unresolved templated RANGE<T> type";
      }
    } else if (kind == ARG_MAP_TYPE_ANY_1_2) {
      const Type* key_type =
          googlesql_base::FindPtrOrNull(*resolved_templated_arguments, ARG_TYPE_ANY_1);
      const Type* value_type =
          googlesql_base::FindPtrOrNull(*resolved_templated_arguments, ARG_TYPE_ANY_2);

      if (key_type == nullptr || value_type == nullptr) {
        std::string kv_error_msg_part;
        if (key_type == nullptr && value_type == nullptr) {
          kv_error_msg_part = "key and value types were";
        } else if (key_type == nullptr) {
          kv_error_msg_part = "key type was";
        } else {
          kv_error_msg_part = "value type was";
        }
        return absl::InvalidArgumentError(
            absl::StrCat("The map type could not be constructed because the ",
                         kv_error_msg_part, " not determinable"));
      }

      std::string no_grouping_type;
      if (!key_type->SupportsGrouping(language_, &no_grouping_type)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Invalid map: ", no_grouping_type, " key type is not groupable"));
      }

      GOOGLESQL_ASSIGN_OR_RETURN(const Type* inferred_map_type,
                       type_factory_->MakeMapType(key_type, value_type));

      // Check that typed input arguments can implicitly coerce to
      // 'inferred_map_type'
      if (type_set.kind() == SignatureArgumentKindTypeSet::TYPED_ARGUMENTS) {
        GOOGLESQL_RETURN_IF_ERROR(TypeSetImplicitlyCoercesTo(type_set, inferred_map_type,
                                                   kind, "map"));
      }
      googlesql_base::InsertOrDie(resolved_templated_arguments, kind, inferred_map_type);

    } else if (kind == ARG_MEASURE_TYPE_ANY_1) {
      if (type_set.typed_arguments().arguments().size() != 1) {
        // As measure types are not coercible, there should be exactly one
        // argument which determines the type of ARG_MEASURE_TYPE_ANY_1.
        ABSL_DLOG(FATAL) << "Expected function to have exactly one argument "
                       "determining the type of ARG_MEASURE_TYPE_ANY_1";
        return absl::InvalidArgumentError(absl::StrCat(
            "Unable to determine type for ",
            FunctionArgumentType::SignatureArgumentKindToString(kind)));
      }

      const InputArgumentType* measure_argument_type =
          &type_set.typed_arguments().arguments().front();
      (*resolved_templated_arguments)[kind] = measure_argument_type->type();

    } else if (!IsArgKind_ARRAY_ANY_K(kind)) {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* inferred_type,
                       InferTemplatedFunctionArgument(kind, type_set));
      // InsertOrDie() is safe because 'kind' only occurs once in
      // 'templated_argument_map'.
      googlesql_base::InsertOrDie(resolved_templated_arguments, kind, inferred_type);
    } else {
      // For ARRAY_ANY_K, also consider the ArrayType whose element type is
      // bound to ANY_K.
      const SignatureArgumentKind related_kind =
          ArrayRelatedTemplatedKind(kind);
      const Type** element_type =
          googlesql_base::FindOrNull(*resolved_templated_arguments, related_kind);
      // ANY_K is handled before ARRAY_ANY_K.
      GOOGLESQL_RET_CHECK_NE(element_type, nullptr);

      if ((*element_type)->IsArray()) {
        // Arrays of arrays are not supported.
        return absl::InvalidArgumentError(absl::StrFormat(
            "%s is inferred to be array of array, which is not supported",
            FunctionArgumentType::SignatureArgumentKindToString(kind)));
      }

      const Type* new_array_type;
      GOOGLESQL_RETURN_IF_ERROR(
          type_factory_->MakeArrayType(*element_type, &new_array_type));

      // Check that typed input arguments can implicitly coerce to
      // 'new_array_type'
      if (type_set.kind() == SignatureArgumentKindTypeSet::TYPED_ARGUMENTS) {
        GOOGLESQL_RETURN_IF_ERROR(TypeSetImplicitlyCoercesTo(type_set, new_array_type,
                                                   kind, "array"));
      }
      // Use 'new_array_type'. InsertOrDie() is safe because 'kind' only occurs
      // once in 'templated_argument_map'.
      googlesql_base::InsertOrDie(resolved_templated_arguments, kind, new_array_type);
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<const Type*>
FunctionSignatureMatcher::InferTemplatedFunctionArgument(
    SignatureArgumentKind signature_type_kind,
    const SignatureArgumentKindTypeSet& type_set) const {
  bool is_templated_type = signature_type_kind == ARG_PROTO_ANY ||
                           signature_type_kind == ARG_STRUCT_ANY ||
                           signature_type_kind == ARG_ENUM_ANY ||
                           signature_type_kind == ARG_RANGE_TYPE_ANY_1;
  switch (type_set.kind()) {
    case SignatureArgumentKindTypeSet::UNTYPED_NULL:
      if (is_templated_type) {
        // For a templated type we do not know what the actual type is given
        // just a NULL argument, so we cannot match the signature with all
        // untyped null arguments.
        return absl::InvalidArgumentError(absl::StrFormat(
            "Unable to determine type for untyped null for argument kind "
            "%s",
            FunctionArgumentType::SignatureArgumentKindToString(
                signature_type_kind)));
      }
      // Untyped non-array arguments have type INT64.
      return types::Int64Type();
    case SignatureArgumentKindTypeSet::UNTYPED_EMPTY_ARRAY:
      if (is_templated_type) {
        // An untyped empty array cannot be matched to a templated type.
        return absl::InvalidArgumentError(absl::StrFormat(
            "Unexpected untyped empty array for argument type %s",
            FunctionArgumentType::SignatureArgumentKindToString(
                signature_type_kind)));
      }
      // Untyped array arguments have type ARRAY<INT64>.
      return types::Int64ArrayType();
    case SignatureArgumentKindTypeSet::TYPED_ARGUMENTS: {
      const Type* common_supertype = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(coercer_.GetCommonSuperType(type_set.typed_arguments(),
                                                  &common_supertype));
      if (common_supertype == nullptr) {
        return absl::InvalidArgumentError(absl::Substitute(
            "Unable to find common supertype for templated argument $0\n"
            "  Input types for $0: $1",
            FunctionArgumentType::SignatureArgumentKindToString(
                signature_type_kind),
            type_set.typed_arguments().ToString()));
      }
      return common_supertype;
    }
  }
}

absl::StatusOr<bool> FunctionSignatureMatcher::SignatureMatches(
    const std::vector<const ASTNode*>& arg_ast_nodes,
    absl::Span<const InputArgumentType> input_arguments,
    const FunctionSignature& signature,
    const ResolveLambdaCallback* resolve_lambda_callback,
    std::unique_ptr<FunctionSignature>* concrete_result_signature,
    SignatureMatchResult* signature_match_result,
    std::vector<ArgIndexEntry>* arg_index_mapping,
    std::vector<FunctionArgumentOverride>* arg_overrides) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);
  if (!signature.options().CheckAllRequiredFeaturesAreEnabled(
          language_.GetEnabledLanguageFeatures())) {
    // Signature will be hidden in error message, so no need to set mismatch
    // message.
    return false;
  }
  GOOGLESQL_RET_CHECK_GE(input_arguments.size(), arg_ast_nodes.size());

  concrete_result_signature->reset();

  int repetitions = 0;
  int optionals = 0;

  // Sanity check.
  GOOGLESQL_RET_CHECK_LE(input_arguments.size(), std::numeric_limits<int>::max());

  // Initially qualify the signature based on the number of arguments, taking
  // into account optional and repeated arguments.  Find x and y such that:
  //   input_arguments.size() = sig.num_required + x*sig.num_repeated + y
  // where 0 < y <= sig.num_optional.
  if (!SignatureArgumentCountMatches(signature, input_arguments, &repetitions,
                                     &optionals, signature_match_result)) {
    GOOGLESQL_RET_CHECK(!signature_match_result->allow_mismatch_message() ||
              !signature_match_result->mismatch_message().empty());
    return false;
  }

  // The signature matches based on just the number of arguments.  Now
  // check for type compatibility and collect all the templated types so
  // we can make sure they match.  <signature_match_result> is updated for
  // non-matched arguments and non-templated arguments.
  ArgKindToInputTypesMap templated_argument_map;
  SignatureMatchResult local_signature_match_result;
  local_signature_match_result.set_allow_mismatch_message(
      signature_match_result->allow_mismatch_message());
  GOOGLESQL_ASSIGN_OR_RETURN(bool match,
                   CheckArgumentTypesAndCollectTemplatedArguments(
                       arg_ast_nodes, input_arguments, signature, repetitions,
                       resolve_lambda_callback, &templated_argument_map,
                       &local_signature_match_result, arg_overrides));

  if (!match) {
    signature_match_result->UpdateFromResult(local_signature_match_result);
    GOOGLESQL_RET_CHECK(!signature_match_result->allow_mismatch_message() ||
              !signature_match_result->mismatch_message().empty());
    return false;
  }

  // Determine the resolved type associated with the templated types in
  // the signature.
  // TODO: Need to consider 'allow_argument_coercion_' here.  We do
  // not currently have a function definition that needs this.
  ArgKindToResolvedTypeMap resolved_templated_arguments;
  absl::Status resolved_templated_arguments_match_status =
      DetermineResolvedTypesForTemplatedArguments(
          templated_argument_map, &resolved_templated_arguments);
  if (!resolved_templated_arguments_match_status.ok()) {
    // If the error is InvalidArgumentError, we can assume that it's user-facing
    // and can use it to set the mismatch message.
    if (resolved_templated_arguments_match_status.code() ==
        absl::StatusCode::kInvalidArgument) {
      SET_MISMATCH_ERROR(resolved_templated_arguments_match_status.message());
      return false;
    }
    return resolved_templated_arguments_match_status;
  }

  // Consistency check to verify that templated array element types and their
  // corresponding templated types match.
  std::vector<std::pair<SignatureArgumentKind, SignatureArgumentKind>> kinds(
      {{ARG_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_1},
       {ARG_TYPE_ANY_2, ARG_ARRAY_TYPE_ANY_2},
       {ARG_TYPE_ANY_3, ARG_ARRAY_TYPE_ANY_3},
       {ARG_TYPE_ANY_4, ARG_ARRAY_TYPE_ANY_4},
       {ARG_TYPE_ANY_5, ARG_ARRAY_TYPE_ANY_5}});
  for (const auto& kind : kinds) {
    const Type** arg_type =
        googlesql_base::FindOrNull(resolved_templated_arguments, kind.first);
    if (arg_type != nullptr) {
      const Type** arg_related_type =
          googlesql_base::FindOrNull(resolved_templated_arguments, kind.second);
      if (arg_related_type != nullptr) {
        if ((*arg_type)->IsArray()) {
          GOOGLESQL_RET_CHECK(
              (*arg_type)->AsArray()->element_type()->Equals(*arg_related_type))
              << "arg_type: " << (*arg_type)->DebugString()
              << "\nelement_type: "
              << (*arg_type)->AsArray()->element_type()->DebugString()
              << "\narg_related_type: " << (*arg_related_type)->DebugString();
        } else {
          GOOGLESQL_RET_CHECK((*arg_related_type)->IsArray());
          GOOGLESQL_RET_CHECK((*arg_related_type)
                        ->AsArray()
                        ->element_type()
                        ->Equals(*arg_type));
        }
      }
    }
  }

  // Construct a concrete return signature (result type) if possible.  For
  // templated functions, determining a concrete result type is not always
  // possible here (computing the return type will be done later in
  // ResolveGeneralFunctionCall for TemplatedSQLFunctions).  In this
  // templated function case, the return type is arbitrary.
  std::unique_ptr<FunctionArgumentType> result_type;
  if (signature.result_type().kind() == ARG_TYPE_ARBITRARY) {
    result_type =
        std::make_unique<FunctionArgumentType>(signature.result_type());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        const bool matches,
        GetConcreteArgument(signature.result_type(), /*num_occurrences=*/1,
                            resolved_templated_arguments, &result_type));
    if (!matches) {
      signature_match_result->UpdateFromResult(local_signature_match_result);
      SET_MISMATCH_ERROR(absl::StrFormat(
          "Unable to determine type for function return type of kind %s",
          UserFacingName(signature.result_type())));
      return false;
    }
  }
  GOOGLESQL_RET_CHECK_NE(signature.result_type().kind(),
               __SignatureArgumentKind__switch_must_have_a_default__);
  result_type->set_original_kind(signature.result_type().kind());

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionArgumentTypeList arg_list,
      GetConcreteArguments(input_arguments, signature, repetitions, optionals,
                           resolved_templated_arguments, arg_index_mapping));
  *concrete_result_signature = std::make_unique<FunctionSignature>(
      std::move(*result_type), std::move(arg_list), signature.context_id(),
      signature.options());
  // We have a matching concrete signature, so update <signature_match_result>
  // for all arguments as compared to this signature.
  for (int idx = 0; idx < input_arguments.size(); ++idx) {
    if (input_arguments[idx].is_relation() || input_arguments[idx].is_model() ||
        input_arguments[idx].is_connection() ||
        input_arguments[idx].is_lambda() ||
        input_arguments[idx].is_sequence() || input_arguments[idx].is_graph()) {
      // The cost of matching a relation/model/connection-type argument is not
      // currently considered in the SignatureMatchResult.
      continue;
    }
    const Type* input_argument_type = input_arguments[idx].type();
    const FunctionArgumentType& signature_argument =
        (*concrete_result_signature)->ConcreteArgument(idx);
    if (signature_argument.IsRelation() && signature_argument.optional() &&
        input_arguments[idx].is_untyped_null()) {
      continue;
    }
    const Type* signature_argument_type =
        (*concrete_result_signature)->ConcreteArgumentType(idx);
    if (!input_argument_type->Equals(signature_argument_type)) {
      if (input_arguments[idx].is_untyped()) {
        // Ideally, the coercion cost should be 0 if the argument is any
        // type so <signature_match_result> should be left unchanged.  However,
        // in order to preserve existing behavior we have to treat it like
        // it has literal coercion cost (i.e., the cost of coercing an
        // INT64 NULL literal to the target type).  We cannot modify this
        // without changing the result type of some function calls, like
        // 'ROUND(NULL)' which currently returns FLOAT but would return
        // DOUBLE if coercion cost was 0.  TODO: Figure out if
        // we can change this, without completely breaking existing
        // implementations and queries.
        signature_match_result->incr_literals_coerced();
        signature_match_result->incr_literals_distance(GetLiteralCoercionCost(
            Value::NullInt64(), signature_argument_type));
      } else if (input_arguments[idx].is_literal()) {
        signature_match_result->incr_literals_coerced();
        signature_match_result->incr_literals_distance(GetLiteralCoercionCost(
            *input_arguments[idx].literal_value(), signature_argument_type));
      } else {
        signature_match_result->incr_non_literals_coerced();
        signature_match_result->incr_non_literals_distance(
            Type::GetTypeCoercionCost(signature_argument_type->kind(),
                                      input_argument_type->kind()));
      }
    }
  }

  // Propagate information about coercing TVF relation arguments to the final
  // signature match statistics, if applicable.
  for (const auto& [key, value] :
       local_signature_match_result.tvf_relation_coercion_map()) {
    signature_match_result->AddTVFRelationCoercionEntry(
        key.argument_index, key.column_index, value);
  }

  return true;
}

}  // namespace

absl::StatusOr<bool> FunctionSignatureMatchesWithStatus(
    const LanguageOptions& language_options, const Coercer& coercer,
    const std::vector<const ASTNode*>& arg_ast_nodes,
    absl::Span<const InputArgumentType> input_arguments,
    const FunctionSignature& signature, bool allow_argument_coercion,
    TypeFactory* type_factory,
    const ResolveLambdaCallback* resolve_lambda_callback,
    std::unique_ptr<FunctionSignature>* concrete_result_signature,
    SignatureMatchResult* signature_match_result,
    std::vector<ArgIndexEntry>* arg_index_mapping,
    std::vector<FunctionArgumentOverride>* arg_overrides) {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(kStackSpaceErrorMessage);

  FunctionSignatureMatcher signature_matcher(
      language_options, coercer, allow_argument_coercion, type_factory);
  return signature_matcher.SignatureMatches(
      arg_ast_nodes, input_arguments, signature, resolve_lambda_callback,
      concrete_result_signature, signature_match_result, arg_index_mapping,
      arg_overrides);
}

}  // namespace googlesql
