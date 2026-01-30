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

#ifndef GOOGLESQL_ANALYZER_FUNCTION_RESOLVER_H_
#define GOOGLESQL_ANALYZER_FUNCTION_RESOLVER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_signature_matcher.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

class Resolver;
class AnalyzerOptions;
class QueryResolutionInfo;

// This class performs function resolution during GoogleSQL analysis.
// The functions here generally traverse the function expressions recursively,
// constructing and returning the ResolvedFunctionCall nodes bottom-up.
class FunctionResolver {
 public:
  FunctionResolver(Catalog* catalog, TypeFactory* type_factory,
                   Resolver* resolver);
  ~FunctionResolver() = default;
  FunctionResolver(const FunctionResolver&) = delete;
  FunctionResolver& operator=(const FunctionResolver&) = delete;

  // Resolves the function call given the `function`, `arguments` expressions,
  // `expected_result_type` and creates a ResolvedFunctionCall.  No special
  // handling is done for aggregate functions - they are resolved exactly like
  // scalar functions.
  // `match_internal_signatures` indicates whether attempts to match an internal
  // signature will be made.
  // `is_analytic` indicates whether an OVER clause follows this function call.
  // Lambda arguments should have a nullptr placeholder in `arguments` and are
  // resolved during signature matching.
  // * Takes ownership of the ResolvedExprs in `arguments`.
  // * `named_arguments` is a vector of any named arguments passed into this
  //   function call along with each one's zero-based index of that argument as
  //   resolved in `arguments`. The function resolver refers to them when
  //   matching against function signatures as needed. These parse nodes
  //   should correspond to a subset of `arguments`.
  // * `expected_result_type` is optional and when specified should match the
  //   result_type of the function signature while resolving. Otherwise there is
  //   no match.
  // * `name_scope` is used to resolve lambda.
  absl::Status ResolveGeneralFunctionCall(
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures, const Function* function,
      ResolvedFunctionCallBase::ErrorMode error_mode, bool is_analytic,
      std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
      std::vector<NamedArgumentInfo> named_arguments,
      const Type* expected_result_type, const NameScope* name_scope,
      std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out);

  // These are the same as previous but they take a (possibly multipart)
  // function name and looks it up in the <resolver_> catalog.  Multipart
  // names are prefixed by their (nested) catalog names.
  absl::Status ResolveGeneralFunctionCall(
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures, absl::string_view function_name,
      bool is_analytic,
      std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
      std::vector<NamedArgumentInfo> named_arguments,
      const Type* expected_result_type,
      std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out);
  absl::Status ResolveGeneralFunctionCall(
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures,
      absl::Span<const std::string> function_name_path, bool is_analytic,
      std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
      std::vector<NamedArgumentInfo> named_arguments,
      const Type* expected_result_type,
      std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out);

  // Analyzes the body of a templated SQL function in <function> in the context
  // of the arguments provided for a specific call.
  //
  // See public/templated_sql_function.h for more background
  // templated sql functions.
  //
  // <function> may provide an alternate catalog for resolution.
  // If <function>.resolution_catalog() is set, then the function expression
  // is resolved against it, otherwise this->catalog_ is used.
  //
  // If this analysis succeeds, this method compares the result type against the
  // expected type as specified in the function signature (if present). If the
  // types are not equal, uses this->coercer_ to insert a coercion if the types
  // are compatible or returns an error otherwise.
  //
  // Finally, once this check is complete, this method returns the result type
  // of this function call in <function_call_info>.
  absl::Status ResolveTemplatedSQLFunctionCall(
      const ASTNode* ast_location, const TemplatedSQLFunction& function,
      const AnalyzerOptions& analyzer_options,
      absl::Span<const InputArgumentType> actual_arguments,
      absl::Span<const std::unique_ptr<const ResolvedExpr>> argument_list,
      const FunctionSignature& concrete_signature,
      std::shared_ptr<ResolvedFunctionCallInfo>* function_call_info_out);

  // This is a helper method when parsing or analyzing the function's SQL
  // expression.  If 'status' is OK, also returns OK. Otherwise, returns a
  // new error forwarding any nested errors in 'status' obtained from the
  // nested parsing or analysis.
  static absl::Status ForwardNestedResolutionAnalysisError(
      const TemplatedSQLFunction& function, const absl::Status& status,
      ErrorMessageOptions options);

  // Converts <argument> to <target_type> and replaces <argument> with the new
  // expression if successful. If <argument> is a converted literal and
  // <set_has_explicit_type> is true, then the new ResolvedLiteral is marked as
  // having been explicitly cast (and it will thereafter be treated as a
  // non-literal with respect to coercion). <return_null_on_error> indicates
  // whether the cast should return a NULL value of the <target_type> in case of
  // cast failures, which should only be set to true when using SAFE_CAST.
  // <format> is the format string used for the conversion. It can be null.
  // <time_zone> is used together with the format string when casting from/to
  // timestamp type. It can be null. <type_params> holds the type parameters for
  // the cast. If no type parameters exist, <type_params> should be an empty
  // TypeParameters object.
  //
  // If <scan> is non-null, then we will peek to see if <argument> is a
  // reference to a column computed by a literal. If so, we replace the column
  // reference with a copy of the literal that is converted to <target_type>.
  // TODO: This is expression pulling. We are already questioning how
  //     much constant expression folding should happen in analysis (as opposed
  //     to rewrite), so maybe we should consider removing the pulling as well.
  //
  // Note: <ast_location> is expected to refer to the location of <argument>,
  // and would be more appropriate as type ASTExpression, though this would
  // require refactoring some callers.
  ABSL_DEPRECATED(
      "Use AddCastOrConvertLiteral function with <annotated_target_type> "
      "argument.")
  // TODO: Refactor and remove the deprecated functions in a quick
  // follow up.
  absl::Status AddCastOrConvertLiteral(
      const ASTNode* ast_location, const Type* target_type,
      std::unique_ptr<const ResolvedExpr> format,
      std::unique_ptr<const ResolvedExpr> time_zone,
      const TypeParameters& type_params,
      const ResolvedScan* scan,  // May be null
      bool set_has_explicit_type, bool return_null_on_error,
      std::unique_ptr<const ResolvedExpr>* argument) const;

  // Same as the previous method but takes <annotated_target_type> argument to
  // contain both target type and its annotation information. <type_modifers> is
  // used to hold the type modifiers for the cast.
  absl::Status AddCastOrConvertLiteral(
      const ASTNode* ast_location, AnnotatedType annotated_target_type,
      std::unique_ptr<const ResolvedExpr> format,
      std::unique_ptr<const ResolvedExpr> time_zone,
      TypeModifiers type_modifiers,
      const ResolvedScan* scan,  // May be null
      bool set_has_explicit_type, bool return_null_on_error,
      std::unique_ptr<const ResolvedExpr>* argument) const;

  // Map an operator id from the parse tree to a GoogleSQL function name.
  static absl::string_view UnaryOperatorToFunctionName(
      ASTUnaryExpression::Op op);

  // Map a binary operator id from the parse tree to a GoogleSQL function name.
  // <is_not> indicates whether IS NOT is used in the operator.
  // Sets <*not_handled> to true if <is_not> is true and the returned function
  // name already takes the "not" into account, avoiding the need to wrap the
  // function call with an additional unary NOT operator.
  //
  // <not_handled> may be nullptr in code paths where <is_not> is known to
  // be false.
  static absl::string_view BinaryOperatorToFunctionName(
      ASTBinaryExpression::Op op, bool is_not, bool* not_handled);

  // Returns the Coercer from <resolver_>.
  const Coercer& coercer() const;

  // Determines if the function signature matches the argument list, returning
  // a non-templated signature if true. If <allow_argument_coercion> is TRUE
  // then function arguments can be coerced to the required signature
  // type(s), otherwise they must be an exact match.
  // <name_scope> is used to resolve lambda. Resolved lambdas are put in
  // <arg_overrides> and their parse ASTs are put in <lambda_ast_nodes>. See
  // <CheckResolveLambdaTypeAndCollectTemplatedArguments> about how lambda is
  // resolved.
  // <arg_index_mapping> if non-null will be updated with the
  // the concrete signatures.
  // Returns non-OK status only for internal errors, like GOOGLESQL_RET_CHECK failures.
  // E.g., the ones in FunctionSignatureMatcher::GetConcreteArguments.
  absl::StatusOr<bool> SignatureMatches(
      const std::vector<const ASTNode*>& arg_ast_nodes,
      absl::Span<const InputArgumentType> input_arguments,
      const FunctionSignature& signature, bool allow_argument_coercion,
      const NameScope* name_scope,
      std::unique_ptr<FunctionSignature>* result_signature,
      SignatureMatchResult* signature_match_result,
      std::vector<ArgIndexEntry>* arg_index_mapping,
      std::vector<FunctionArgumentOverride>* arg_overrides,
      absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>*
          lambda_ast_nodes) const;

  // Perform post-processing checks on CREATE AGGREGATE FUNCTION statements at
  // initial declaration time or when the functions are called later.
  // <expr_info> and <query_info> should be valid at the time that
  // <resolved_expr> is generated. Note that <sql_function_body_location> is
  // optional, and will be used to help construct the error message if present.
  static absl::Status CheckCreateAggregateFunctionProperties(
      const ResolvedExpr& resolved_expr,
      const ASTNode* sql_function_body_location,
      const ExprResolutionInfo* expr_info, QueryResolutionInfo* query_info,
      const LanguageOptions& language_options);

  // Iterates through <arg_locations> and <named_arguments> and compares them
  // against <signature> to match the order of the arguments in the signature,
  // or returns an error if the signature does not match.
  //
  // Returns mismatch message for the provided signature and returns error if
  // the call cannot possibly match any signature. If nothing wrong happens,
  // empty mismatch message is returned.
  //
  // <num_repeated_args_repetitions> should be the number of repetitions of
  // the repeated arguments. It is determined by the
  // SignatureArgumentCountMatches function.
  //
  // Returns the mapped function argument indexes in a list of <ArgIndexPair>
  // structs. The list size will be the number of function call arguments plus
  // the number of omitted optional arguments (i.e., #arguments in <signature>
  // + (<num_repeated_args_repetitions> - 1) * #repeated arguments). It is
  // guaranteed that the <signature_arg_index> is always increasing, except for
  // the repeated arguments part. Indexes to repeated arguments appear
  // <num_repeated_args_repetitions> times, while indexes to required and
  // optional arguments appear exactly once in the list, even for the omitted
  // optional arguments (whose index values depending on the
  // <always_include_omitted_named_arguments_in_index_mapping> parameter below).
  //
  // <always_include_omitted_named_arguments_in_index_mapping> controls how to
  // deal with trailing omitted arguments. When false and <named_arguments> is
  // empty, trailing arguments in the signature that are omitted in the call and
  // have no default values will also be omitted in the output <index_mapping>.
  // Otherwise, their corresponding entries are included in <index_mapping>
  // with <call_arg_index> as -1.
  absl::StatusOr<std::string> GetFunctionArgumentIndexMappingPerSignature(
      absl::string_view function_name, const FunctionSignature& signature,
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      absl::Span<const NamedArgumentInfo> named_arguments,
      int num_repeated_args_repetitions,
      bool always_include_omitted_named_arguments_in_index_mapping,
      std::vector<ArgIndexEntry>* index_mapping) const;

  // Reorders the given <input_argument_types> with respect to the given
  // <index_mapping> which is the output of
  // GetFunctionArgumentIndexMappingPerSignature, and also fills in default
  // values for omitted arguments.
  //
  // As an input, <input_argument_types> represents the arguments provided to
  // the original function call (i.e., matches the order of the input to
  // GetFunctionArgumentIndexMappingPerSignature).
  //
  // `arg_locations` is the input AST locations for the input arguments. If it
  // is not nullptr, it must match positionally with `input_argument_types`.
  //
  // Upon success, this function reorders the provided <input_argument_types>
  // list, so that the elements match the argument order in the matching
  // signature. For any optional arguments whose values are not provided
  // (either positionally or named) the function may inject suitable default
  // values into the list:
  // - If a default value is present in the FunctionArgumentTypeOptions
  //   corresponding to the omitted argument, this default value is injected.
  // - Otherwise, a NULL value for this omitted argument might be injected.
  //
  // `arg_locations`, if provided, will also be reordered to match the
  // signature. For the optional arguments without a provided values, nullptr
  // will be added to indicate no input location.
  //
  // Note that a NULL value is only injected for omitted optional arguments that
  // appear before a named argument or an argument with a default, and are not
  // injected otherwise. The NULL injection is a temporary workaround to
  // represent omitted arguments that do not have default values. This behavior
  // is subject to change in the near future.
  //
  static absl::Status
  ReorderInputArgumentTypesPerIndexMappingAndInjectDefaultValues(
      const FunctionSignature& signature,
      absl::Span<const ArgIndexEntry> index_mapping,
      std::vector<InputArgumentType>* input_argument_types,
      std::vector<const ASTNode*>* arg_locations = nullptr);

  // Reorders the given input argument representations <arg_locations>,
  // <resolved_args> and <resolved_tvf_args> with the given <index_mapping>
  // which is the output of
  // GetFunctionArgumentIndexMappingPerSignature and the <input_argument_types>
  // which is the output of
  // ReorderInputArgumentTypesPerIndexMappingAndInjectDefaultValues.
  //
  // All of the <arg_locations>, <resolved_args> and <resolved_tvf_args> are
  // optional. If provided, they represent the arguments provided to the
  // original function call (i.e., matches the order of the input to
  // GetFunctionArgumentIndexMappingPerSignature).
  //
  // Upon success, this function reorders the provided lists, so that they match
  // the argument order in the matching signature. For any optional arguments
  // whose values are not provided (either positionally or named) the function
  // may inject suitable default values in the lists, which are already
  // available in <input_argument_types>.
  static absl::Status ReorderArgumentExpressionsPerIndexMapping(
      absl::string_view function_name, const FunctionSignature& signature,
      absl::Span<const ArgIndexEntry> index_mapping,
      const ASTNode* ast_location,
      absl::Span<const InputArgumentType> input_argument_types,
      std::vector<const ASTNode*>* arg_locations,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_args,
      std::vector<ResolvedTVFArg>* resolved_tvf_args);

  // Check an argument against constraints for a given argument, and return an
  // error if any are violated.
  absl::Status CheckArgumentConstraints(
      const ASTNode* function_location, absl::string_view function_name,
      bool is_tvf, const ASTNode* arg_location, int idx,
      const FunctionArgumentType& concrete_argument,
      const ResolvedExpr* arg_expr,
      const std::function<std::string(int)>& BadArgErrorPrefix) const;

  // Check a literal argument value against value constraints for a given
  // argument, and return an error if any are violated.
  absl::Status CheckArgumentValueConstraints(
      const ASTNode* arg_location, int idx, const Value& value,
      const FunctionArgumentType& concrete_argument,
      const std::function<std::string(int)>& BadArgErrorPrefix) const;

  // Computes the result annotations of the `function_call` using the provided
  // `callback` and stores them in `function_call`. This method is called when
  // a non-SQL function has a custom callback to compute the result annotations.
  absl::Status CustomPropagateAnnotations(
      const ComputeResultAnnotationsCallback& callback,
      const ASTNode* ast_location, ResolvedFunctionCallBase& function_call);

 private:
  Catalog* catalog_;           // Not owned.
  TypeFactory* type_factory_;  // Not owned.
  Resolver* resolver_;         // Not owned.

  // Returns a signature that matches the argument type list, returning
  // a concrete FunctionSignature if found.  If not found, returns NULL.
  // The caller takes ownership of the returned FunctionSignature.
  //
  // The `ast_location` and `arg_locations` identify the function call and
  // argument locations for use in error messages.
  // If `match_internal_signatures` is false and the function signature is
  // internal, do not attempt signature matching and do not generate a
  // user-facing error message.
  // The `named_arguments` optionally identify a list of named arguments
  // provided as part of the function call which may be used to help identify
  // the signature. The pair in each of `named_arguments` comprises a pointer to
  // the ASTNamedArgument object that the parser produced for this named
  // argument reference and also an integer identifying the corresponding
  // argument type by indexing into the passed-in list in `input_arguments`.
  // `name_scope` is used to resolve lambda. Resolved lambdas are put in
  // `arg_overrides`. See `CheckResolveLambdaTypeAndCollectTemplatedArguments`
  // for how lambda is resolved.
  // `input_arguments` is an in-out parameter. As an input, it represents the
  // arguments provided to the function call to be resolved. As an output, it
  // contains the argument list reordered to match the returned signature. For
  // any optional arguments whose values are not provided (either positionally
  // or named) elements are also injected into this list.
  // `arg_index_mapping` is the output of
  // `GetFunctionArgumentIndexMappingPerSignature` against the matching
  // signature, so that the caller can reorder the input argument list
  // representations accordingly.
  // If `mismatch_errors` is non-null, it triggers generating detailed signature
  // mismatch message.
  absl::StatusOr<const FunctionSignature*> FindMatchingSignature(
      const Function* function, const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures,
      absl::Span<const NamedArgumentInfo> named_arguments,
      const NameScope* name_scope,
      std::vector<InputArgumentType>* input_arguments,
      std::vector<FunctionArgumentOverride>* arg_overrides,
      std::vector<ArgIndexEntry>* arg_index_mapping,
      absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>*
          lambda_ast_nodes,
      std::vector<std::string>* mismatch_errors) const;

  // Returns user-facing text with a list of signatures along with the reason
  // why they didn't match the call (from `mismatch_error`), in the following
  // format:
  // Signature: <signature_1>
  //   <single_line_reason>.
  // Signature: <signature_2>
  //   <indented_line_1_of_multiple_line_reason>
  //   <indented_line_2_of_multiple_line_reason>.
  absl::StatusOr<std::string> GetSupportedSignaturesWithMessage(
      const Function* function, absl::Span<const std::string> mismatch_errors,
      FunctionArgumentType::NamePrintingStyle print_style,
      int* num_signatures) const;

  // Generates an error message for function call mismatching with the existing
  // signatures, with <prefix_message> followed by a list of supported
  // signatures of <function>.
  // If <function> has no valid signatures, the returned message would be like
  // "Function not found: <function name>".
  // Returns error status if an internal error happens.
  absl::StatusOr<std::string> GenerateErrorMessageWithSupportedSignatures(
      const Function* function, absl::string_view prefix_message,
      FunctionArgumentType::NamePrintingStyle argument_print_style,
      const std::vector<std::string>* mismatch_errors = nullptr) const;

  // Converts <argument_literal> to <target_type> and replaces
  // <converted_literal> with the new expression if successful. If
  // <set_has_explicit_type> is true, then the new ResolvedLiteral is marked as
  // having been explicitly cast (and it will thereafter be treated as a
  // non-literal with respect to coercion). <return_null_on_error> indicates
  // whether the cast should return a NULL value of the <target_type> in case of
  // cast failures, which should only be set to true when using SAFE_CAST.
  absl::Status ConvertLiteralToType(
      const ASTNode* ast_location, const ResolvedLiteral* argument_literal,
      const Type* target_type, const TypeParameters& target_type_params,
      const ResolvedScan* scan, bool set_has_explicit_type,
      bool return_null_on_error,
      std::unique_ptr<const ResolvedLiteral>* converted_literal) const;

  // For COLLATE(<string_expr>, <collation_spec>) function, resolves collation
  // and set the annotation on the <function_call>.type_annotation_map field.
  absl::Status ResolveCollationForCollateFunction(
      const ASTNode* error_location, ResolvedFunctionCall* function_call);

  // Re-resolves any lambdas related to arguments that have annotations.
  absl::Status ReResolveLambdasIfNecessary(
      const FunctionSignature* result_signature, const NameScope* name_scope,
      const absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>&
          lambda_ast_nodes,
      ResolvedFunctionCall* resolved_expr_out) const;

  friend class FunctionResolverTest;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_FUNCTION_RESOLVER_H_
