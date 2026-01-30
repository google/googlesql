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

#ifndef GOOGLESQL_PUBLIC_TABLE_VALUED_FUNCTION_H_
#define GOOGLESQL_PUBLIC_TABLE_VALUED_FUNCTION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

class AnalyzerOptions;
class ResolvedExpr;
class SignatureMatchResult;
class TVFInputArgumentType;
class TVFRelationColumnProto;
class TVFRelationProto;
class TVFSignature;
class TableValuedFunctionProto;
class TableValuedFunctionOptionsProto;

struct TVFSchemaColumn;

// This callback signature takes matched signature, an argument list as input.
// Callback implementations are intended to validate the argument list with
// matched signature and any additional constraints on argument value which TVF
// wants to enforce.
using TVFPostResolutionArgumentConstraintsCallback = std::function<absl::Status(
    const FunctionSignature&, const std::vector<TVFInputArgumentType>&,
    const LanguageOptions&)>;

// This callback signature takes matched signature, an argument list as input
// and returns the computed output type for TVF call. New types should be
// allocated in the provided TypeFactory.
using TVFComputeResultTypeCallback =
    std::function<absl::StatusOr<std::shared_ptr<TVFSignature>>(
        Catalog*, TypeFactory*, const FunctionSignature&,
        const std::vector<TVFInputArgumentType>&, const AnalyzerOptions&)>;

// Options that apply to a table-valued function.
// The setter methods here return a reference to *self so options can be
// constructed inline, and chained if desired.
struct TableValuedFunctionOptions {
  TableValuedFunctionOptions() = default;

  static absl::Status Deserialize(
      const TableValuedFunctionOptionsProto& proto,
      std::unique_ptr<TableValuedFunctionOptions>* result);

  void Serialize(TableValuedFunctionOptionsProto* proto) const;

  TableValuedFunctionOptions& set_uses_upper_case_sql_name(bool value) {
    uses_upper_case_sql_name = value;
    return *this;
  }

  TableValuedFunctionOptions& set_post_resolution_argument_constraint(
      TVFPostResolutionArgumentConstraintsCallback constraint) {
    post_resolution_constraint_callback = std::move(constraint);
    return *this;
  }

  TableValuedFunctionOptions& set_compute_result_type_callback(
      TVFComputeResultTypeCallback callback) {
    compute_result_type_callback = std::move(callback);
    return *this;
  }

  // Add a LanguageFeature that must be enabled for this function to be enabled.
  // This is used only on built-in functions, and determines whether they will
  // be loaded in GetBuiltinFunctionsAndTypes.
  TableValuedFunctionOptions& AddRequiredLanguageFeature(
      LanguageFeature feature) {
    googlesql_base::InsertIfNotPresent(&required_language_features, feature);
    return *this;
  }

  // Returns whether or not all language features required by the function are
  // enabled.
  ABSL_MUST_USE_RESULT bool CheckAllRequiredFeaturesAreEnabled(
      const LanguageOptions::LanguageFeatureSet& enabled_features) const;

  // Returns whether the given feature is required by the function.
  ABSL_MUST_USE_RESULT bool RequiresFeature(LanguageFeature feature) const;

  // Indicates whether to use upper case name in GetSignatureUserFacingText(),
  // which is used for error messages such as
  // "No matching signature for function ...".
  bool uses_upper_case_sql_name = true;

  // If not nullptr, this callback is called after resolution to check any
  // additional constraints on the TVF's arguments. Post-resolution checks are
  // performed against the coerced arguments after a specific TVF signature has
  // been matched. If the callback returns non-OK status, then analysis
  // immediately fails. Please consult `googlesql/common/errors.h` for
  // which status code to return. Resolver code will attach error at TVF call
  // parse location.
  TVFPostResolutionArgumentConstraintsCallback
      post_resolution_constraint_callback = nullptr;

  // If not nullptr, this callback is called to calculate tvf output signature.
  // It can be used when the output type is dependent on argument types and/or
  // argument literal values. Callers must set this callback to avoid analysis
  // error, unless they are using TableValuedFunction subclass which overrides
  // Resolve. Internal error will be returned when using TableValuedFunction
  // without setting the callback and analysis will stop. If the callback
  // returns non-OK status, then analysis immediately fails. Please consult
  // `googlesql/common/errors.h` for which status code to return. Resolver code
  // will attach error at TVF call parse location.
  TVFComputeResultTypeCallback compute_result_type_callback = nullptr;

  // A set of LanguageFeatures that need to be enabled for the function to be
  // loaded in GetBuiltinFunctionsAndTypes.
  std::set<LanguageFeature> required_language_features;

  friend bool operator==(const TableValuedFunctionOptions& a,
                         const TableValuedFunctionOptions& b) {
    return a.uses_upper_case_sql_name == b.uses_upper_case_sql_name &&
           a.required_language_features == b.required_language_features;
  }
};

// This interface describes a table-valued function (TVF) available in a query
// engine.
//
// For reference, each call to the regular Function class:
//
// * accepts value (not relation) arguments only,
// * has a fixed list of (maybe templated) signatures and the function resolver
//   selects one concrete signature,
// * returns a single value.
//
// In contrast, each TVF call:
//
// * accepts scalar or TVF specific input arguments (relation, model,
//   connection, descriptor, graph),
// * returns a stream of rows,
// * has an output table schema (column names and types, or a value table)
//   computed by a method in this class, and not described in the signature.
//
// Similar to Functions, TVFs also have a list of signatures which specify the
// number and types of input arguments that each signature accepts.
//
// To resolve a TVF call, the resolver:
//
// (1) finds the matching signature. Currently only a single matching signature
//     is supported. If multiple signatures match the provided input arguments,
//     an internal error is returned.
// (2) resolves all input arguments based on the signature
// (3) prepares a TableValuedFunction::InputArgumentList from the resolved input
//     arguments
// (4) calls TableValuedFunction::Resolve, passing the input arguments, to get
//     a TableValuedFunctionCall object with the output schema for the TVF call
// (5) fills the output name list from the column names in the output schema
// (6) returns a new ResolvedTVFScan with the resolved arguments as children
// TODO: b/447515225 - Update this comment once finding the closest matching
// TVF signature is supported.
class TableValuedFunction {
 public:
  // The SQL SECURITY specified when the function was created.
  ResolvedCreateStatementEnums::SqlSecurity sql_security() const {
    return sql_security_;
  }

  // DEPRECATED - Prefer the delegated constructor which accepts
  // `std::vector<FunctionSignature>` and a `group` argument.
  // Constructs a new TVF object with the given name and argument signature with
  // an empty string for group name.
  ABSL_DEPRECATED("Inline me!")
  TableValuedFunction(const std::vector<std::string>& function_name_path,
                      const FunctionSignature& signature,
                      TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"",
                            std::vector<FunctionSignature>{signature},
                            std::move(tvf_options)) {}

  // Constructs a new TVF object with the given name, group and argument
  // signatures.
  //
  // Each TVF may accept value or relation arguments. The signature specifies
  // whether each argument should be a value or a relation. For a value
  // argument, the signature may specify a concrete Type or a (possibly
  // templated) SignatureArgumentKind. For relation arguments, the signature
  // should use ARG_TYPE_RELATION, and any relation will be accepted as an
  // argument.
  TableValuedFunction(const std::vector<std::string>& function_name_path,
                      std::string group,
                      std::vector<FunctionSignature> signatures,
                      TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(
            function_name_path, std::move(group), std::move(signatures),
            /*anonymization_info=*/nullptr, std::move(tvf_options)) {}

  // Table functions constructed this way should use AddSignature() to
  // add a related signature.
  TableValuedFunction(const std::vector<std::string>& function_name_path,
                      std::string group,
                      TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(function_name_path, std::move(group),
                            /*signatures=*/{},
                            /*anonymization_info=*/nullptr,
                            std::move(tvf_options)) {}

  TableValuedFunction(const std::vector<std::string>& function_name_path,
                      std::string group,
                      std::vector<FunctionSignature> signatures,
                      std::unique_ptr<AnonymizationInfo> anonymization_info,
                      TableValuedFunctionOptions tvf_options = {})
      : function_name_path_(function_name_path),
        group_(std::move(group)),
        signatures_(std::move(signatures)),
        anonymization_info_(std::move(anonymization_info)),
        tvf_options_(std::move(tvf_options)) {
    for (const FunctionSignature& signature : signatures_) {
      GOOGLESQL_CHECK_OK(signature.IsValidForTableValuedFunction());  // Crash ok.
    }
    GOOGLESQL_CHECK_OK(CheckNoGraphAndJustScalarsOverload());  // Crash ok.
  }

  TableValuedFunction(const TableValuedFunction&) = delete;
  TableValuedFunction& operator=(const TableValuedFunction&) = delete;
  virtual ~TableValuedFunction() = default;

  // Returns the name of this TVF.
  const std::string& Name() const { return function_name_path_.back(); }

  std::string FullName(bool include_group = true) const {
    return absl::StrCat(
        ((include_group && !group_.empty()) ? absl::StrCat(group_, ":") : ""),
        absl::StrJoin(function_name_path_, "."));
  }

  // Returns an external 'SQL' name for the table valued function, for use in
  // error messages and anywhere else appropriate.
  std::string SQLName() const {
    std::string name;
    if (IsGoogleSQLBuiltin()) {
      name = FullName(/*include_group=*/false);
    } else {
      name = FullName();
    }
    return this->tvf_options_.uses_upper_case_sql_name
               ? absl::AsciiStrToUpper(name)
               : name;
  }

  const std::vector<std::string>& function_name_path() const {
    return function_name_path_;
  }

  // Returns the 'group' the TVF belongs to.
  const std::string& GetGroup() const { return group_; }

  bool IsGoogleSQLBuiltin() const {
    return group_ == Function::kGoogleSQLFunctionGroupName;
  }
  // Returns the number of function signatures.
  int64_t NumSignatures() const;

  // Returns all of the function signatures.
  // Note: For templated TVFs, the return type may or may not be concrete, and
  // if the return type is determined by the function call arguments then by
  // convention AnyRelation is returned.
  const std::vector<FunctionSignature>& signatures() const;

  // Adds a function signature to an existing table function.  TVFs currently
  // only support one signature, so an error is returned if a signature
  // already exists.
  // TODO: Support more than one signature.
  absl::Status AddSignature(const FunctionSignature& function_signature);

  // Returns the requested FunctionSignature.  The caller does not take
  // ownership of the returned FunctionSignature.  Returns NULL if the
  // specified idx does not exist.
  // TODO: Consider making this return a const reference instead.
  // This signature should be consistent with Function::GetSignature(), so
  // if we change it here we should also change it there.  Should it return
  // Status, in case <idx> is out of range (rather than ABSL_CHECK failing)?
  const FunctionSignature* GetSignature(int64_t idx) const;

  // Returns user facing text (to be used in error messages) listing function
  // signatures. Signatures are hidden from this list if they are internal,
  // deprecated, explicitly hidden, or unsupported according to LanguageOptions.
  virtual std::string GetSupportedSignaturesUserFacingText(
      const LanguageOptions& language_options,
      bool print_template_and_name_details) const;

  virtual std::string DebugString() const;

  // Returns an error message for a table-valued function call named
  // 'tvf_name_string' with 'tvf_catalog_entry' that did not match the
  // function signature (identified by 'signature_idx') with 'input_arg_types'.
  // 'signature_match_result' should contain the result of a previous call to
  // FunctionResolver::SignatureMatches and 'language_options' should contain
  // the language options for the query.
  std::string GetTVFSignatureErrorMessage(
      absl::string_view tvf_name_string,
      absl::Span<const InputArgumentType> input_arg_types, int signature_idx,
      const SignatureMatchResult& signature_match_result,
      const LanguageOptions& language_options) const;

  // Serializes this table-valued function to a protocol buffer. Subclasses may
  // override this to add more information as needed.
  virtual absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                                 TableValuedFunctionProto* proto) const;

  // Deserializes a table-valued function from a protocol buffer.
  // The specific steps taken to perform the deserialization depend on the
  // 'type' field of 'proto'. An associated deserializer for this 'type' must
  // already exist by this time from a previous call to RegisterDeserializer.
  static absl::Status Deserialize(const TableValuedFunctionProto& proto,
                                  const TypeDeserializer& type_deserializer,
                                  std::unique_ptr<TableValuedFunction>* result);

  // Registers 'deserializer' as a function to deserialize a specific
  // TableValuedFunction subclass of 'type'. The returned TVF is owned by the
  // caller. This must be called at module initialization time. Dies if more
  // than one deserializer for the same 'type' is registered, or if any other
  // error occurs. For an example, please see the REGISTER_MODULE_INITIALIZER in
  // table_valued_function.cc.
  using TVFDeserializer =
      std::function<absl::Status(const TableValuedFunctionProto& proto,
                                 const TypeDeserializer& type_deserializer,
                                 std::unique_ptr<TableValuedFunction>* result)>;
  static void RegisterDeserializer(FunctionEnums::TableValuedFunctionType type,
                                   TVFDeserializer deserializer);
  // Backward-compatible version of above that does not support extended types
  // in the TVF signature.
  using TVFDeserializerWithoutTypeDeserializer = std::function<absl::Status(
      const TableValuedFunctionProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory, std::unique_ptr<TableValuedFunction>* result)>;
  static void RegisterDeserializer(
      FunctionEnums::TableValuedFunctionType type,
      TVFDeserializerWithoutTypeDeserializer deserializer);

  // Determines if this object is of the <TableFunctionSubclass>.
  template <class TableValuedFunctionSubclass>
  bool Is() const {
    return dynamic_cast<const TableValuedFunctionSubclass*>(this) != nullptr;
  }

  // Returns this TableValuedFunction as TableValuedFunctionSubclass*.  Must
  // only be used when it is known that the object *is* this subclass, which can
  // be checked using Is() before calling GetAs().
  template <class TableValuedFunctionSubclass>
  TableValuedFunctionSubclass* GetAs() {
    return static_cast<TableValuedFunctionSubclass*>(this);
  }
  template <class TableValuedFunctionSubclass>
  const TableValuedFunctionSubclass* GetAs() const {
    return static_cast<const TableValuedFunctionSubclass*>(this);
  }

  // Sets the <anonymization_info_> with the specified <userid_column_name_path>
  // (overwriting any previous anonymization info).  An error is returned if
  // the named column is ambiguous or does not exist in this table valued
  // function.
  //
  // Setting the AnonymizationInfo defines this table valued function as
  // supporting anonymization semantics and returning sensitive private data.
  absl::Status SetUserIdColumnNamePath(
      absl::Span<const std::string> userid_column_name_path);

  // Returns anonymization info for a table valued function, including a column
  // reference that indicates the userid column for anonymization purposes.
  std::optional<const AnonymizationInfo> anonymization_info() const {
    if (anonymization_info_ != nullptr) {
      return *anonymization_info_;
    }
    return std::nullopt;
  }

  // The Resolve method determines the output schema of a particular call to
  // this TVF based on the input arguments provided in the query. The default
  // implementation uses `compute_result_type_callback` from `tvf_options_` to
  // compute the output schema. It is possible for subclasses to provide their
  // own implementation which may or may not use `compute_result_type_callback`.
  //
  // GoogleSQL provides information about the number and types of these
  // arguments in 'actual_arguments'.  GoogleSQL also includes the concrete
  // version of signature_ in 'concrete_signature' providing the resolved type
  // of templated arguments and occurrence counts of optional or repeated
  // arguments (with concrete relation schemas identified in the
  // FunctionArgumentTypeOptions).
  //
  // Validating that the 'actual_arguments' match the 'concrete_signature'
  // generally happens in two phases.  First, the Resolver performs initial
  // matching which primarily covers non-templated arguments (for both
  // table and scalar arguments).  The initial matching also covers templated
  // arguments, verifying that they are the proper general type (i.e., if the
  // function takes ANY TABLE then the related argument must be a table
  // argument and not a scalar argument).  Second, this Resolve() method is
  // invoked which may do additional validation (primarily for templated
  // arguments).  If the arguments are incompatible then this method returns
  // a descriptive error message indicating the nature of the failure.
  //
  // Otherwise, this method fills 'output_tvf_signature' to indicate the result
  // schema of the table returned by this TVF call.
  //
  // This method accepts a Catalog and TypeFactory for possible use when
  // computing the output schema. It is important to note that GoogleSQL
  // provides these items when each function is resolved, not when it is
  // declared. Therefore the engine may add new types, tables, or functions
  // in-between these two times and they will be available for lookup here.
  virtual absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* output_tvf_signature) const;

  // Returns Status indicating whether or not any specified constraints were
  // violated. If `post_resolution_constraint_callback` is nullptr returns OK.
  absl::Status CheckPostResolutionArgumentConstraints(
      const FunctionSignature& signature,
      const std::vector<TVFInputArgumentType>& arguments,
      const LanguageOptions& language_options) const;

  const TableValuedFunctionOptions& tvf_options() const { return tvf_options_; }

  // Returns non-const reference to the tvf options.
  TableValuedFunctionOptions& mutable_tvf_options() { return tvf_options_; }

  void set_sql_security(ResolvedCreateStatementEnums::SqlSecurity security) {
    sql_security_ = security;
  }

  // Argument for TVF evaluator table iterator. Only one field will be set.
  struct TvfEvaluatorArg {
    // If set, this argument is a Value.
    std::optional<Value> value;
    // If set, this argument is a relation.
    std::unique_ptr<EvaluatorTableIterator> relation;
    // If set, this argument is a model.
    const Model* model;
    // If set, this argument is a graph.
    const PropertyGraph* graph;
  };

  // The CreateEvaluatorTableIterator method allows a subclass to provide TVF
  // evaluation logic for reference implementation. Implementation should return
  // an iterator representing a table containing the results of the TVF call.
  //
  // 'input_arguments' is a list of input arguments passed to the TVF.
  // Relational inputs are represented as table iterators. The implementation
  // takes ownership of these iterators and can iterate in any order necessary.
  //
  // 'output_columns' is a list of output columns selected by the resolved
  // TVF scan, which is subset of output columns returned by Resolve method.
  // This parameter allows implementation to prune unused columns and provides
  // column names and types for TVFs with dynamic schemas.
  //
  // 'function_call_signature' contains signature that matched the invocation,
  // including concrete arguments. Set only if the invocation is ambiguous and
  // contains omitted arguments.
  //
  // The output iterator must provide column names, types and values for all
  // output_columns. If the TVF schema is fixed and pruning is not necessary,
  // the iterator can ignore output_columns parameter and provide columns for
  // all output columns of the result schema. The order of columns is not
  // relevant.
  //
  // Not used for googlesql analysis.
  // Used only for evaluating queries on this table with the reference
  // implementation, using the interfaces in evaluator.h.
  virtual absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluator(std::vector<TvfEvaluatorArg> input_arguments,
                  const std::vector<TVFSchemaColumn>& output_columns,
                  const FunctionSignature* function_call_signature) const {
    return googlesql_base::UnimplementedErrorBuilder()
           << "TVF " << FullName()
           << " does not support the API in evaluator.h";
  }

  void set_statement_context(StatementContext context) {
    statement_context_ = context;
  }
  StatementContext statement_context() const { return statement_context_; }

 protected:
  // Returns user facing text (to be used in error messages) for the
  // specified table function <signature>. For example:
  //   tvf_name(DOUBLE, TABLE<col_name1 BOOL, col_name2 STRING>)
  //
  // The <language_options> identifies the product mode (INTERNAL vs. EXTERNAL),
  // affecting the resulting Type names (i.e., DOUBLE vs. FLOAT8).
  std::string GetSignatureUserFacingText(
      const FunctionSignature& signature,
      const LanguageOptions& language_options,
      bool print_template_and_name_details) const;

  // If this TVF has a signature that takes a graph argument we don't allow any
  // signatures that just take scalar arguments because it doesn't let us
  // diagnose common mistakes. See shortn/_0Uc6eBN0Lu for details.
  absl::Status CheckNoGraphAndJustScalarsOverload() const;

  // This is the name of this TVF.
  const std::vector<std::string> function_name_path_;

  // Group in which this TVF belongs. If it is kGoogleSQLFunctionGroupName
  // it is GoogleSQL builtin.
  const std::string group_;

  // The signatures describe the input arguments that this TVF accepts.
  std::vector<FunctionSignature> signatures_;

  // The AnonymizationInfo related to a TVF. See
  // (broken link) for further details.
  std::unique_ptr<AnonymizationInfo> anonymization_info_;

  TableValuedFunctionOptions tvf_options_ = {};

 private:
  ResolvedCreateStatementEnums::SqlSecurity sql_security_ =
      ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED;

  // The GoogleSQL statement context in which this object was defined,
  // indicating the context in which it was analyzed or needs to be analyzed.
  // Specifically, this indicates if the object came from a module, which may
  // have some different resolution behavior.
  StatementContext statement_context_ = CONTEXT_DEFAULT;
};

// Represents a column for some TVF input argument types (e.g. TVFRelation and
// TVFModelArgument).
struct TVFSchemaColumn {
  TVFSchemaColumn(absl::string_view name_in, const Type* type_in,
                  bool is_pseudo_column_in = false)
      : name(name_in), type(type_in), is_pseudo_column(is_pseudo_column_in) {}

  TVFSchemaColumn(absl::string_view name_in, AnnotatedType annotated_type_in,
                  const bool is_pseudo_column_in = false)
      : name(name_in),
        type(annotated_type_in.type),
        annotation_map(annotated_type_in.annotation_map),
        is_pseudo_column(is_pseudo_column_in) {}

  // Serializes this TVFRelation column to a protocol buffer.
  absl::StatusOr<TVFRelationColumnProto> ToProto(
      FileDescriptorSetMap* file_descriptor_set_map) const;

  // Deserializes a TVFSchema column from a protocol buffer.
  // ParseLocationRangeProto stores the filename that will become string_view
  // when deserialized. Returned TVFSchemaColumn references a string owned by
  // 'proto', and therefore 'proto' must outlive the returned value.
  // TODO Add support for storing filename as string in
  // ParseLocationPoint.
  static absl::StatusOr<TVFSchemaColumn> FromProto(
      const TVFRelationColumnProto& proto,
      const TypeDeserializer& type_deserializer);
  static absl::StatusOr<TVFSchemaColumn> FromProto(
      const TVFRelationColumnProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory);

  std::string DebugString(bool is_for_value_table) const {
    std::string type_name;
    if (annotation_map == nullptr) {
      type_name = type->DebugString();
    } else {
      type_name =
          absl::StrCat(type->DebugString(), annotation_map->DebugString());
    }
    // Prevent concatenating value column name.
    if (!is_for_value_table || is_pseudo_column) {
      return absl::StrCat(name, " ", type_name);
    }
    return type_name;
  }

  AnnotatedType annotated_type() const { return {type, annotation_map}; }

  std::string name;
  // The type and annotations. Doesn't own either pointer.
  const Type* type = nullptr;
  const AnnotationMap* annotation_map = nullptr;
  bool is_pseudo_column;
  // Parse location ranges for the TVFSchema column name and type. As of now
  // they are populated if and only if this TVF definition comes from a
  // CREATE TABLE FUNCTION statement and record_parse_locations is true in
  // AnalyzerOptions.
  std::optional<ParseLocationRange> name_parse_location_range;
  std::optional<ParseLocationRange> type_parse_location_range;
};

// To support GOOGLESQL_RET_CHECK_EQ.
bool operator==(const TVFSchemaColumn& a, const TVFSchemaColumn& b);

// This represents a relation passed as an input argument to a TVF, or returned
// from a TVF. It either contains a list of columns, where each column contains
// a name and a type, or the relation may be a value table. For the value table
// case, there should be exactly one column, with an empty name. Note that each
// column name is a GoogleSQL IdString and so it must outlive the TVFRelation
// that contains it.
// TODO: Give this class a better name that suggests it is the schema
// of a table-valued argument or return value. The word 'relation' implies that
// it might contain an entire table, which is untrue.
class TVFRelation {
 public:
  using Column = TVFSchemaColumn;
  using ColumnList = std::vector<Column>;

  // Creates a new TVFRelation with a fixed list of columns.
  explicit TVFRelation(ColumnList columns)
      : columns_(std::move(columns)), is_value_table_(false) {}

  // Creates a new value-table TVFRelation with a single column of 'type' with
  // no name.
  //
  // A value table will act like each row is a single unnamed value with some
  // type rather than acting like each row is a vector of named columns.
  static TVFRelation ValueTable(const Type* type) {
    TVFRelation result = TVFRelation({Column("", type)});
    result.is_value_table_ = true;
    return result;
  }

  // Similar to the function above but accepts an <annotated_type> argument to
  // indicate both type and its annotation_map of the column in the value table.
  static TVFRelation ValueTable(AnnotatedType annotated_type) {
    TVFRelation result = TVFRelation({Column("", annotated_type)});
    result.is_value_table_ = true;
    return result;
  }

  // Creates a new value-table TVFRelation with at least one column, and the
  // first column (column 0) is treated as the value of the row. Additional
  // columns may be present and must be pseudo-columns.
  static absl::StatusOr<TVFRelation> ValueTable(
      const Type* type, const ColumnList& pseudo_columns) {
    ColumnList columns;
    columns.reserve(pseudo_columns.size() + 1);
    columns.emplace_back("", type);
    for (const Column& column : pseudo_columns) {
      GOOGLESQL_RET_CHECK(column.is_pseudo_column);
      columns.push_back(column);
    }
    TVFRelation result = TVFRelation(std::move(columns));
    result.is_value_table_ = true;
    return result;
  }

  // Similar to the above function but accepts an <annotated_type> argument to
  // indicate both type and its annotation_map of the value-table column.
  static absl::StatusOr<TVFRelation> ValueTable(
      AnnotatedType annotated_type, const ColumnList& pseudo_columns) {
    ColumnList columns;
    columns.reserve(pseudo_columns.size() + 1);
    columns.emplace_back("", annotated_type);
    for (const Column& column : pseudo_columns) {
      GOOGLESQL_RET_CHECK(column.is_pseudo_column);
      columns.push_back(column);
    }
    TVFRelation result = TVFRelation(std::move(columns));
    result.is_value_table_ = true;
    return result;
  }

  // Creates a new value-table TVFRelation with the provided column.
  static TVFRelation ValueTable(const Column& column) {
    TVFRelation result = TVFRelation({column});
    result.is_value_table_ = true;
    return result;
  }

  const ColumnList& columns() const { return columns_; }
  const Column& column(int i) const { return columns_[i]; }
  int num_columns() const { return columns_.size(); }
  bool is_value_table() const { return is_value_table_; }

  std::string GetSQLDeclaration(ProductMode product_mode) const;
  std::string DebugString() const;

  // Serializes this relation to a proto and deserializes it back again. This is
  // useful when serializing the GoogleSQL catalog.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         TVFRelationProto* proto) const;
  static absl::StatusOr<TVFRelation> Deserialize(
      const TVFRelationProto& proto, const TypeDeserializer& type_deserializer);

 private:
  ColumnList columns_;
  bool is_value_table_;
};

bool operator==(const TVFRelation& a, const TVFRelation& b);

inline std::ostream& operator<<(std::ostream& out,
                                const TVFRelation& relation) {
  out << relation.DebugString();
  return out;
}

// This represents a model passed as an input argument to a TVF. It contains a
// pointer to the model object in the catalog.
class TVFModelArgument {
 public:
  // Creates a new TVFModelArgument using a model catalog object.
  // Does not take ownership of <model>, which must outlive this class.
  explicit TVFModelArgument(const Model* model) : model_(model) {}

  const Model* model() const { return model_; }
  std::string DebugString() const;

  // TODO: Implement serialize and deserialize.

 private:
  const Model* model_;  // Not owned.
};

// This represents a connection passed as an input argument to a TVF. It
// contains a pointer to the connection object in the catalog.
class TVFConnectionArgument {
 public:
  // Creates a new TVFConnectionArgument using a connection catalog object.
  // Does not take ownership of <connection>, which must outlive this class.
  explicit TVFConnectionArgument(const Connection* connection)
      : connection_(connection) {}

  const Connection* connection() const { return connection_; }
  std::string DebugString() const;

 private:
  const Connection* connection_;  // Not owned.
};

// This represents a descriptor passed as an input argument to a TVF. It
// contains a list of column names.
class TVFDescriptorArgument {
 public:
  explicit TVFDescriptorArgument(std::vector<std::string> column_names)
      : column_names_(std::move(column_names)) {}

  std::vector<std::string> column_names() const { return column_names_; }
  std::string DebugString() const;

 private:
  std::vector<std::string> column_names_;
};

class TVFGraphArgument {
 public:
  explicit TVFGraphArgument(const PropertyGraph* graph) : graph_(graph) {}

  const PropertyGraph* graph() const { return graph_; }
  std::string DebugString() const;

 private:
  const PropertyGraph* graph_;  // Not owned.
};

// This represents one input argument to a call to a TVF.
// Each such call includes zero or more input arguments.
// Each input argument may be either scalar or relation.
class TVFInputArgumentType {
 public:
  // Creates a scalar input argument with a single type.
  explicit TVFInputArgumentType(const InputArgumentType& input_arg_type)
      : kind_(TVFInputArgumentTypeKind::SCALAR),
        scalar_arg_type_(input_arg_type.type()) {
    if (input_arg_type.literal_value() != nullptr) {
      scalar_arg_value_.reset(new Value(*input_arg_type.literal_value()));
    }
  }

  // Creates a relation input argument of several columns, where each column
  // contains a name and a type. If the input argument is a value table, the
  // vector should have one element with an empty name.
  explicit TVFInputArgumentType(const TVFRelation& relation)
      : kind_(TVFInputArgumentTypeKind::RELATION), relation_(relation) {}

  // Creates a model argument.
  explicit TVFInputArgumentType(const TVFModelArgument& model)
      : kind_(TVFInputArgumentTypeKind::MODEL), model_(model) {}

  // Creates a connection argument.
  explicit TVFInputArgumentType(const TVFConnectionArgument& connection)
      : kind_(TVFInputArgumentTypeKind::CONNECTION), connection_(connection) {}

  // Creates a graph argument.
  explicit TVFInputArgumentType(const TVFGraphArgument& graph)
      : kind_(TVFInputArgumentTypeKind::GRAPH), graph_(graph) {}

  explicit TVFInputArgumentType(
      const TVFDescriptorArgument& descriptor_argument)
      : kind_(TVFInputArgumentTypeKind::DESCRIPTOR),
        descriptor_argument_(descriptor_argument) {}

  bool is_scalar() const { return kind_ == TVFInputArgumentTypeKind::SCALAR; }
  bool is_relation() const {
    return kind_ == TVFInputArgumentTypeKind::RELATION;
  }
  bool is_model() const { return kind_ == TVFInputArgumentTypeKind::MODEL; }
  bool is_connection() const {
    return kind_ == TVFInputArgumentTypeKind::CONNECTION;
  }
  bool is_descriptor() const {
    return kind_ == TVFInputArgumentTypeKind::DESCRIPTOR;
  }
  bool is_graph() const { return kind_ == TVFInputArgumentTypeKind::GRAPH; }
  absl::StatusOr<InputArgumentType> GetScalarArgType() const {
    GOOGLESQL_RET_CHECK(kind_ == TVFInputArgumentTypeKind::SCALAR);
    if (scalar_arg_value_ != nullptr) {
      return InputArgumentType(*scalar_arg_value_);
    } else {
      return InputArgumentType(scalar_arg_type_);
    }
  }

  // Returns the resolved expression for scalar arguments, if present. This is
  // optional, and is not owned or serialized. If present, this class assumes
  // that it is consistent with scalar_arg_type_ and scalar_arg_value_.
  const ResolvedExpr* scalar_expr() const {
    ABSL_DCHECK(is_scalar());
    return scalar_expr_;
  }
  void set_scalar_expr(const ResolvedExpr* expr) {
    ABSL_DCHECK(is_scalar());
    scalar_expr_ = expr;
  }

  // TODO: Rename to GetRelation and return StatusOr to keep
  // consistent with GetScalarArgType above.
  const TVFRelation& relation() const {
    ABSL_DCHECK(is_relation());
    return relation_;
  }
  const TVFModelArgument& model() const {
    ABSL_DCHECK(is_model());
    return model_;
  }
  const TVFConnectionArgument& connection() const {
    ABSL_DCHECK(is_connection());
    return connection_;
  }

  const TVFDescriptorArgument& descriptor_argument() const {
    ABSL_DCHECK(is_descriptor());
    return descriptor_argument_;
  }

  const TVFGraphArgument& graph() const {
    ABSL_DCHECK(is_graph());
    return graph_;
  }

  std::string DebugString() const {
    if (kind_ == TVFInputArgumentTypeKind::RELATION) {
      return relation_.DebugString();
    } else if (kind_ == TVFInputArgumentTypeKind::MODEL) {
      return model_.DebugString();
    } else if (kind_ == TVFInputArgumentTypeKind::CONNECTION) {
      return connection_.DebugString();
    } else if (kind_ == TVFInputArgumentTypeKind::DESCRIPTOR) {
      return descriptor_argument_.DebugString();
    } else if (kind_ == TVFInputArgumentTypeKind::GRAPH) {
      return graph_.DebugString();
    } else if (scalar_arg_value_ != nullptr) {
      return InputArgumentType(*scalar_arg_value_).DebugString();
    } else {
      return InputArgumentType(scalar_arg_type_).DebugString();
    }
  }

 private:
  enum class TVFInputArgumentTypeKind {
    UNKNOWN,
    CONNECTION,
    MODEL,
    RELATION,
    SCALAR,
    DESCRIPTOR,
    GRAPH,
  };
  // Defines whether this is a relation, scalar argument or a model.
  TVFInputArgumentTypeKind kind_;

  // TODO: Refactor and use std::optional instead of having multiple
  // member variables.
  // Only one of the following is defined, based on kind_.
  TVFRelation relation_ = TVFRelation({});
  TVFModelArgument model_ = TVFModelArgument(nullptr);
  TVFConnectionArgument connection_ = TVFConnectionArgument(nullptr);
  TVFDescriptorArgument descriptor_argument_ =
      TVFDescriptorArgument(std::vector<std::string>());
  TVFGraphArgument graph_ = TVFGraphArgument(nullptr);
  const Type* scalar_arg_type_ = nullptr;

  // This is the literal value for 'scalar_arg_type_', if applicable. We store
  // it here separately because the InputArgumentType class does not own its
  // Value, but instead holds a pointer to an external Value. The Value provided
  // in the class constructor does not live as long as this class instance does,
  // thus the need to keep a separate owned copy here as a field.
  std::shared_ptr<Value> scalar_arg_value_;

  // This is the resolved expression for scalar arguments. This is optional,
  // and is not owned or serialized.
  const ResolvedExpr* scalar_expr_ = nullptr;

  // Copyable.
};

struct TVFSignatureOptions {
  // Deprecation warnings associated with the body of a SQL TVF.
  std::vector<FreestandingDeprecationWarning> additional_deprecation_warnings;
};

// Converts a `const Table*` to a `TVFRelation` which describes the same
// schema.
//
// Returns an error if the table schema is not a valid Table schema, e.g., if it
// is a value table with more than one non-pseudo columns.
absl::StatusOr<TVFRelation> CreateTVFRelationFromTable(const Table* table);

// This class contains information about a specific resolved TVF call. It
// includes the input arguments passed into the TVF call and also its output
// schema (including whether it is a value table). Engines may also subclass
// this to include more information if needed.
class TVFSignature {
 public:
  ABSL_DEPRECATED("Use the Create() factory function instead")
  // Represents a TVF call that returns 'output_schema'.
  TVFSignature(const std::vector<TVFInputArgumentType>& input_arguments,
               const TVFRelation& result_schema,
               const TVFSignatureOptions& options = {})
      : input_arguments_(input_arguments),
        result_schema_(result_schema),
        options_(options) {}

 public:
  // Creates a `TVFSignature` with the given input arguments and output schema.
  static absl::StatusOr<std::shared_ptr<TVFSignature>> Create(
      const std::vector<TVFInputArgumentType>& input_arguments,
      const TVFRelation& result_schema,
      const TVFSignatureOptions& options = {}) {
    return std::make_shared<TVFSignature>(input_arguments, result_schema,
                                          options);
  }

  // Similar to the above function, but takes a `const Table*` instead of a
  // `TVFRelation` as the output schema. The caller manages the lifetime of the
  // provided `Table*`.
  //
  // Returns an error if the table schema is not a valid TVF output schema,
  // e.g., if it is a value table with more than one non-pseudo columns.
  static absl::StatusOr<std::shared_ptr<TVFSignature>> Create(
      const std::vector<TVFInputArgumentType>& input_arguments,
      const Table* result_table_schema,
      const TVFSignatureOptions& options = {});

  TVFSignature(const TVFSignature&) = delete;
  TVFSignature& operator=(const TVFSignature&) = delete;
  virtual ~TVFSignature() = default;

  const std::vector<TVFInputArgumentType>& input_arguments() const {
    return input_arguments_;
  }
  const TVFInputArgumentType& argument(int idx) const {
    return input_arguments_[idx];
  }

  // Returns the output schema of the TVFSignature. If the TVFSignature was
  // constructed with a `const Table*` as the output schema, this will be a
  // TVFRelation constructed from the output Table in the signature.
  const TVFRelation& result_schema() const { return result_schema_; }

  // Returns the output schema as a `const Table*`. Returns nullptr if the
  // TVFSignature was not constructed with a `const Table*` as the output
  // schema.
  //
  // Must be non-null if the TVFSignature outputs measure columns.
  const Table* result_table_schema() const { return result_table_schema_; }

  const TVFSignatureOptions& options() const { return options_; }

  virtual std::string DebugString(bool verbose) const {
    std::vector<std::string> arg_debug_strings;
    arg_debug_strings.reserve(input_arguments_.size());
    for (const TVFInputArgumentType& input_argument : input_arguments_) {
      arg_debug_strings.push_back(input_argument.DebugString());
    }
    std::string ret = absl::StrCat("(", absl::StrJoin(arg_debug_strings, ", "),
                                   ") -> ", result_schema_.DebugString());
    if (result_table_schema_ != nullptr) {
      absl::StrAppend(&ret, " (table: ", result_table_schema_->FullName(), ")");
    }
    if (verbose) {
      const std::string deprecation_warnings_debug_string =
          DeprecationWarningsToDebugString(
              options_.additional_deprecation_warnings);
      if (!deprecation_warnings_debug_string.empty()) {
        absl::StrAppend(&ret, " ", deprecation_warnings_debug_string);
      }
    }
    return ret;
  }

  // Returns AnonymizationInfo related to a resolved call of this TVF.
  // For further details, see:
  //
  // (broken link).
  //
  // This method only returns AnonymizationInfo for TVFs that produce private
  // user data and that support anonymization queries.
  std::optional<const AnonymizationInfo> GetAnonymizationInfo() const {
    return anonymization_info_ == nullptr
               ? std::nullopt
               : std::optional<const googlesql::AnonymizationInfo>(
                     *anonymization_info_);
  }
  void SetAnonymizationInfo(
      std::unique_ptr<AnonymizationInfo> anonymization_info) {
    anonymization_info_ = std::move(anonymization_info);
  }
  bool SupportsAnonymization() const {
    return GetAnonymizationInfo().has_value();
  }

  std::string DebugString() const { return DebugString(/*verbose=*/false); }

  // Returns whether or not this TVFCall is a specific table-valued function
  // call interface or implementation.
  template <class TVFCallSubclass>
  bool Is() const {
    return dynamic_cast<const TVFCallSubclass*>(this) != nullptr;
  }

  // Returns this TVFCall as TVFCall*.  Must only be used when it is known that
  // the object *is* this subclass, which can be checked using Is() before
  // calling GetAs().
  template <class TVFCallSubclass>
  TVFCallSubclass* GetAs() {
    return static_cast<TVFCallSubclass*>(this);
  }
  template <class TVFCallSubclass>
  const TVFCallSubclass* GetAs() const {
    return static_cast<const TVFCallSubclass*>(this);
  }

 protected:
  // Precondition: If `result_table_schema` is set, `result_schema` must match
  // the schema produced by `CreateTVFRelationFromTable(result_table_schema)`.
  TVFSignature(const std::vector<TVFInputArgumentType>& input_arguments,
               const TVFRelation& result_schema,
               const Table* result_table_schema,
               const TVFSignatureOptions& options = {})
      : input_arguments_(input_arguments),
        result_schema_(result_schema),
        result_table_schema_(result_table_schema),
        options_(options) {}

 private:
  // Returns the input arguments passed into this TVF call.
  const std::vector<TVFInputArgumentType> input_arguments_;

  // Returns the output schema returned by this TVF call.
  //
  // This field is always populated. If the TVFSignature was constructed with a
  // `const Table*` as the output schema, this will be a TVFRelation constructed
  // from the output Table in the signature.
  const TVFRelation result_schema_;

  // The output schema of the TVFSignature represented as a `Table*`. This field
  // is only populated if the TVFSignature was constructed with the Create()
  // factory function that accepts a `const Table*` as the output schema.
  const Table* result_table_schema_ = nullptr;

  const TVFSignatureOptions options_;

  // The AnonymizationInfo related to a resolved call of this TVF. See
  // (broken link) for further details.
  std::unique_ptr<AnonymizationInfo> anonymization_info_;
};

// This represents a TVF that always returns a relation with the same fixed
// output schema.
class FixedOutputSchemaTVF : public TableValuedFunction {
 public:
  // DEPRECATED - Prefer the delegated constructor which accepts
  // `std::vector<FunctionSignature>`.
  // Constructs a new TVF object with the given name and fixed output schema.
  ABSL_DEPRECATED("Inline me!")
  FixedOutputSchemaTVF(const std::vector<std::string>& function_name_path,
                       const FunctionSignature& signature,
                       const TVFRelation& result_schema,
                       TableValuedFunctionOptions tvf_options = {})
      : FixedOutputSchemaTVF(function_name_path,
                             std::vector<FunctionSignature>{signature},
                             result_schema, std::move(tvf_options)) {}

  // Constructs a FixedOutputSchemaTVF with the given name, list of signatures,
  // anonymization info and fixed output schema.
  FixedOutputSchemaTVF(const std::vector<std::string>& function_name_path,
                       std::vector<FunctionSignature> signatures,
                       std::unique_ptr<AnonymizationInfo> anonymization_info,
                       const TVFRelation& result_schema,
                       TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(
            function_name_path, /*group=*/"", std::move(signatures),
            std::move(anonymization_info), std::move(tvf_options)),
        result_schema_(std::move(result_schema)) {}

  // Constructs a FixedOutputSchemaTVF with the given name, list of signatures,
  // anonymization info and fixed output schema.
  FixedOutputSchemaTVF(const std::vector<std::string>& function_name_path,
                       std::vector<FunctionSignature> signatures,
                       const TVFRelation& result_schema,
                       TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"",
                            std::move(signatures), std::move(tvf_options)),
        result_schema_(std::move(result_schema)) {}

  FixedOutputSchemaTVF(const FixedOutputSchemaTVF&) = delete;
  FixedOutputSchemaTVF& operator=(const FixedOutputSchemaTVF&) = delete;
  ~FixedOutputSchemaTVF() override = default;

  const TVFRelation& result_schema() const { return result_schema_; }

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         TableValuedFunctionProto* proto) const override;

  static absl::Status Deserialize(const TableValuedFunctionProto& proto,
                                  const TypeDeserializer& type_deserializer,
                                  std::unique_ptr<TableValuedFunction>* result);

  // Returns the fixed output schema set in the constructor.
  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* tvf_signature) const override;

 private:
  // This is the fixed output schema of the TVF.
  TVFRelation result_schema_;
};

// This represents a TVF that accepts a relation for its first argument. The TVF
// returns a relation with the same output schema as this input relation. The
// TVF may also accept additional arguments as defined by the signature passed
// to the constructor.
class ForwardInputSchemaToOutputSchemaTVF : public TableValuedFunction {
 public:
  // DEPRECATED - Prefer the delegated constructor which accepts
  // `std::vector<FunctionSignature>`.
  // Constructs a new instance of this TVF with name 'function_name_path'.
  // 'signature' specifies the number and types of arguments that the TVF
  // accepts. This signature must have at least one argument and the first
  // argument must be a relation, or otherwise the Resolve method returns an
  // error.
  ABSL_DEPRECATED("Inline me!")
  ForwardInputSchemaToOutputSchemaTVF(
      const std::vector<std::string>& function_name_path,
      const FunctionSignature& signature,
      TableValuedFunctionOptions tvf_options = {})
      : ForwardInputSchemaToOutputSchemaTVF(
            function_name_path, std::vector<FunctionSignature>{signature},
            std::move(tvf_options)) {}

  // Constructs a new instance of this TVF with name 'function_name_path' and a
  // list of signatures. Each sigantures specifies the number and types of
  // arguments that the TVF accepts. This signature must have at least one
  // argument and the first argument must be a relation, or otherwise the
  // Resolve method returns an error.
  ForwardInputSchemaToOutputSchemaTVF(
      const std::vector<std::string>& function_name_path,
      std::vector<FunctionSignature> signatures,
      TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"",
                            std::move(signatures), std::move(tvf_options)) {
    GOOGLESQL_CHECK_OK(CheckIsValid());  // Crash OK
  }

  ForwardInputSchemaToOutputSchemaTVF(
      const ForwardInputSchemaToOutputSchemaTVF&) = delete;
  ForwardInputSchemaToOutputSchemaTVF& operator=(
      const ForwardInputSchemaToOutputSchemaTVF&) = delete;
  ~ForwardInputSchemaToOutputSchemaTVF() override = default;

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         TableValuedFunctionProto* proto) const override;

  static absl::Status Deserialize(
      const TableValuedFunctionProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory, std::unique_ptr<TableValuedFunction>* result);

  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* output_tvf_signature) const override;

 private:
  // Performs some quick sanity checks on the function signature.
  absl::Status CheckIsValid() const;
};

// This represents a TVF that accepts a relation for its first (templated)
// argument. The TVF returns a relation with a schema that is constructed by
// copying the schema of input relation and appending <extra_columns_> to the
// schema. The TVF may also accept additional arguments as defined by the
// signature passed to the constructor.
class ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF
    : public TableValuedFunction {
 public:
  // Constructs an instance of this TVF with the given name, a list of
  // signatures and a list of schema columns. The first argument of each
  // signature must be a templated relation.
  ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
      const std::vector<std::string>& function_name_path,
      std::vector<FunctionSignature> signatures,
      const std::vector<TVFSchemaColumn>& extra_columns,
      TableValuedFunctionOptions tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"",
                            std::move(signatures), tvf_options),
        extra_columns_(extra_columns) {
    GOOGLESQL_CHECK_OK(  // Crash OK
        IsValidForwardInputSchemaToOutputSchemaWithAppendedColumnTVF());
  }

  ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
      const ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF&) = delete;
  ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF& operator=(
      const ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF&) = delete;
  ~ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF() override = default;

  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* output_tvf_signature) const override;

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         TableValuedFunctionProto* proto) const override;

  static absl::Status Deserialize(
      const TableValuedFunctionProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory, std::unique_ptr<TableValuedFunction>* result);

  // This method checks if the `signatures_` and `extra_columns_` are valid.
  // Specifically, it returns a non-OK status if:
  //  a. extra column name is empty.
  //  b. extra column name is duplicated.
  //  c. extra column is a pseudo column, which is invalid usage for this tvf.
  //  d. first argument in the signature(s) is not a templated relation.
  absl::Status IsValidForwardInputSchemaToOutputSchemaWithAppendedColumnTVF()
      const;

 private:
  const std::vector<TVFSchemaColumn> extra_columns_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TABLE_VALUED_FUNCTION_H_
