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

#ifndef GOOGLESQL_PUBLIC_FUNCTION_SIGNATURE_H_
#define GOOGLESQL_PUBLIC_FUNCTION_SIGNATURE_H_

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/constness_level.pb.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/value.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status.h"

namespace googlesql {

class FunctionArgumentType;
class FunctionArgumentTypeProto;
class FunctionSignature;
class FunctionSignatureOptionsProto;
class FunctionSignatureProto;
class InputArgumentType;
class LanguageOptions;
class TVFRelation;

typedef std::vector<FunctionArgumentType> FunctionArgumentTypeList;

constexpr static auto kPositionalOnly = FunctionEnums::POSITIONAL_ONLY;
constexpr static auto kPositionalOrNamed = FunctionEnums::POSITIONAL_OR_NAMED;
constexpr static auto kNamedOnly = FunctionEnums::NAMED_ONLY;

ABSL_DEPRECATED("Inline me!")
constexpr SignatureArgumentKind ARG_RANGE_TYPE_ANY = ARG_RANGE_TYPE_ANY_1;

// This class specifies options on a function argument, including
// argument cardinality.  This includes some options that are used to specify
// argument values that are illegal and should cause an analysis error.
class FunctionArgumentTypeOptions {
 public:
  typedef FunctionEnums::ProcedureArgumentMode ProcedureArgumentMode;
  typedef FunctionEnums::ArgumentCardinality ArgumentCardinality;
  typedef FunctionEnums::ArgumentCollationMode ArgumentCollationMode;
  typedef FunctionEnums::NamedArgumentKind NamedArgumentKind;
  using ArgumentAliasKind = FunctionEnums::ArgumentAliasKind;

  FunctionArgumentTypeOptions() : data_(new Data{}) {};

  explicit FunctionArgumentTypeOptions(ArgumentCardinality cardinality)
      : data_(new Data{.cardinality = cardinality}) {}

  // This constructs a set of argument type options to specify a required schema
  // for a relation argument of a table-valued function. If this required schema
  // is present, GoogleSQL enforces that column names of the passed-in relation
  // are a superset of the column names in these options (in any order), and
  // that the type of each passed-in column is equal or coercible to the type of
  // the matching required column. If either of these conditions does not hold,
  // GoogleSQL returns a descriptive error message to the user.
  //
  // If 'extra_relation_input_columns_allowed' is true, the provided relation
  // may include column names besides those specified in
  // 'relation_input_schema'.  Otherwise, GoogleSQL rejects the query if the
  // provided relation contains such extra columns. Note that if
  // 'relation_input_schema' requires a value table, this option has no effect
  // and GoogleSQL enforces that the provided relation has exactly one column.
  //
  // For more information about table-valued functions, please see
  // public/table_valued_function.h.
  FunctionArgumentTypeOptions(const TVFRelation& relation_input_schema,
                              bool extra_relation_input_columns_allowed);

  FunctionArgumentTypeOptions(const FunctionArgumentTypeOptions& options)
      : data_(std::make_unique<Data>(*options.data_)) {}
  FunctionArgumentTypeOptions& operator=(
      const FunctionArgumentTypeOptions& options) {
    data_ = std::make_unique<Data>(*options.data_);
    return *this;
  }
  FunctionArgumentTypeOptions(FunctionArgumentTypeOptions&& options) noexcept
      : data_(std::move(options.data_)) {}
  FunctionArgumentTypeOptions& operator=(
      FunctionArgumentTypeOptions&& options) noexcept {
    data_ = std::move(options).data_;
    return *this;
  }

  ~FunctionArgumentTypeOptions() = default;

  ArgumentCardinality cardinality() const { return data_->cardinality; }
  bool must_be_constant() const {
    return data_->constness_level ==
           ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER;
  }
  bool must_be_analysis_constant() const {
    return data_->constness_level == ConstnessLevelProto::ANALYSIS_CONST;
  }
  bool must_be_immutable_constant() const {
    return data_->constness_level == ConstnessLevelProto::IMMUTABLE_CONST;
  }
  bool must_be_constant_expression() const {
    return data_->constness_level ==
           ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION;
  }
  bool must_be_non_null() const { return data_->must_be_non_null; }
  bool is_not_aggregate() const { return data_->is_not_aggregate; }
  bool must_support_equality() const { return data_->must_support_equality; }
  bool must_support_ordering() const { return data_->must_support_ordering; }
  bool must_support_grouping() const { return data_->must_support_grouping; }

  bool array_element_must_support_equality() const {
    return data_->array_element_must_support_equality;
  }
  bool array_element_must_support_ordering() const {
    return data_->array_element_must_support_ordering;
  }
  bool array_element_must_support_grouping() const {
    return data_->array_element_must_support_grouping;
  }

  bool has_min_value() const { return data_->has_min_value; }
  bool has_max_value() const { return data_->has_max_value; }
  int64_t min_value() const { return data_->min_value; }
  int64_t max_value() const { return data_->max_value; }

  bool has_relation_input_schema() const {
    return data_->relation_input_schema != nullptr;
  }

  std::optional<int> get_resolve_descriptor_names_table_offset() const {
    return data_->descriptor_resolution_table_offset;
  }

  const TVFRelation& relation_input_schema() const {
    ABSL_DCHECK(has_relation_input_schema());
    return *data_->relation_input_schema;
  }

  bool extra_relation_input_columns_allowed() const {
    return data_->extra_relation_input_columns_allowed;
  }
  bool has_argument_name() const { return !data_->argument_name.empty(); }
  const std::string& argument_name() const {
    ABSL_DCHECK(has_argument_name());
    return data_->argument_name;
  }

  // Determines whether the argument name must or must not be specified when the
  // associated function is called. When not <has_argument_name()>, this is
  // irrelevant.
  NamedArgumentKind named_argument_kind() const {
    return data_->named_argument_kind;
  }

  ArgumentAliasKind argument_alias_kind() const {
    return data_->argument_alias_kind;
  }

  ProcedureArgumentMode procedure_argument_mode() const {
    return data_->procedure_argument_mode;
  }

  FunctionArgumentTypeOptions& set_cardinality(ArgumentCardinality c) {
    data_->cardinality = c;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_be_constant(bool v = true) {
    if (v) {
      ABSL_DCHECK(data_->constness_level ==
                 ConstnessLevelProto::CONSTNESS_UNSPECIFIED ||
             data_->constness_level ==
                 ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER)
          << "Cannot set must_be_constant when another constness "
             "level is already set.";
      ;
      data_->constness_level = ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER;
      return *this;
    }
    data_->constness_level = ConstnessLevelProto::CONSTNESS_UNSPECIFIED;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_be_analysis_constant(bool v = true) {
    if (v) {
      ABSL_DCHECK(data_->constness_level ==
                 ConstnessLevelProto::CONSTNESS_UNSPECIFIED ||
             data_->constness_level == ConstnessLevelProto::ANALYSIS_CONST)
          << "Cannot set must_be_analysis_constant when another constness "
             "level is already set.";
      data_->constness_level = ConstnessLevelProto::ANALYSIS_CONST;
      return *this;
    }
    data_->constness_level = ConstnessLevelProto::CONSTNESS_UNSPECIFIED;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_be_immutable_constant(bool v = true) {
    if (v) {
      ABSL_DCHECK(data_->constness_level ==
                 ConstnessLevelProto::CONSTNESS_UNSPECIFIED ||
             data_->constness_level == ConstnessLevelProto::IMMUTABLE_CONST)
          << "Cannot set must_be_immutable_constant when another constness "
             "level is already set.";
      data_->constness_level = ConstnessLevelProto::IMMUTABLE_CONST;
      return *this;
    }
    data_->constness_level = ConstnessLevelProto::CONSTNESS_UNSPECIFIED;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_be_constant_expression(bool v = true) {
    if (v) {
      ABSL_DCHECK(data_->constness_level ==
                 ConstnessLevelProto::CONSTNESS_UNSPECIFIED ||
             data_->constness_level ==
                 ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION)
          << "Cannot set must_be_constant_expression when another constness "
             "level is already set.";
      ;
      data_->constness_level = ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION;
      return *this;
    }
    data_->constness_level = ConstnessLevelProto::CONSTNESS_UNSPECIFIED;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_be_non_null(bool v = true) {
    data_->must_be_non_null = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_is_not_aggregate(bool v = true) {
    data_->is_not_aggregate = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_support_equality(bool v = true) {
    data_->must_support_equality = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_support_ordering(bool v = true) {
    data_->must_support_ordering = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_must_support_grouping(bool v = true) {
    data_->must_support_grouping = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_array_element_must_support_equality(
      bool v = true) {
    data_->array_element_must_support_equality = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_array_element_must_support_ordering(
      bool v = true) {
    data_->array_element_must_support_ordering = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_array_element_must_support_grouping(
      bool v = true) {
    data_->array_element_must_support_grouping = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_min_value(int64_t value) {
    data_->has_min_value = true;
    data_->min_value = value;
    return *this;
  }
  FunctionArgumentTypeOptions& set_max_value(int64_t value) {
    data_->has_max_value = true;
    data_->max_value = value;
    return *this;
  }
  FunctionArgumentTypeOptions& set_relation_input_schema(
      std::shared_ptr<TVFRelation> relation_input_schema) {
    data_->relation_input_schema = std::move(relation_input_schema);
    return *this;
  }
  FunctionArgumentTypeOptions& set_extra_relation_input_columns_allowed(
      bool v = true) {
    data_->extra_relation_input_columns_allowed = v;
    return *this;
  }
  FunctionArgumentTypeOptions& set_argument_name(absl::string_view name,
                                                 NamedArgumentKind kind) {
    data_->argument_name = name;
    data_->named_argument_kind = kind;
    return *this;
  }

  FunctionArgumentTypeOptions& set_argument_alias_kind(
      ArgumentAliasKind argument_alias_kind) {
    data_->argument_alias_kind = argument_alias_kind;
    return *this;
  }

  FunctionArgumentTypeOptions& set_procedure_argument_mode(
      ProcedureArgumentMode mode) {
    data_->procedure_argument_mode = mode;
    return *this;
  }
  FunctionArgumentTypeOptions& set_allow_coercion_from(
      std::function<bool(const googlesql::Type*)> allow_coercion_from) {
    data_->allow_coercion_from = allow_coercion_from;
    return *this;
  }
  FunctionArgumentTypeOptions& set_resolve_descriptor_names_table_offset(
      int table_offset) {
    data_->descriptor_resolution_table_offset = table_offset;
    return *this;
  }

  std::function<bool(const googlesql::Type*)> allow_coercion_from() const {
    return data_->allow_coercion_from;
  }

  static absl::Status Deserialize(
      const FunctionArgumentTypeOptionsProto& options_proto,
      const TypeDeserializer& type_deserializer, SignatureArgumentKind arg_kind,
      const Type* arg_type, FunctionArgumentTypeOptions* options);

  absl::Status Serialize(const Type* arg_type,
                         FunctionArgumentTypeOptionsProto* options_proto,
                         FileDescriptorSetMap* file_descriptor_set_map) const;

  // Return a string describing the options (not including cardinality).
  // If no options are set, this returns an empty string.
  // Otherwise, includes a leading space.
  std::string OptionsDebugString() const;

  // Get the SQL declaration for these options.
  // The result is formatted as SQL that can be included inside a function
  // signature in CREATE FUNCTION, DROP FUNCTION, etc, if possible.
  std::string GetSQLDeclaration(ProductMode product_mode) const;

  // Same as thee above `GetSQLDeclaration`, but setting `use_external_float32`
  // to true will return FLOAT32 as the type name for TYPE_FLOAT in
  // PRODUCT_EXTERNAL mode.
  // TODO: Remove this signature once FLOAT32 is used by default
  // for PRODUCT_EXTERNAL.
  std::string GetSQLDeclaration(ProductMode product_mode,
                                bool use_external_float32) const;

  // Sets the ParseLocationRange of the argument name. Returns the modified
  // class object.
  FunctionArgumentTypeOptions& set_argument_name_parse_location(
      const ParseLocationRange& argument_name_parse_location) {
    data_->argument_name_parse_location = argument_name_parse_location;
    return *this;
  }

  // Gets the ParseLocationRange of the argument name.
  std::optional<ParseLocationRange> argument_name_parse_location() const {
    return data_->argument_name_parse_location;
  }

  // Sets the ParseLocationRange of the argument type. Returns the modified
  // class object.
  FunctionArgumentTypeOptions& set_argument_type_parse_location(
      const ParseLocationRange& argument_type_parse_location) {
    data_->argument_type_parse_location = argument_type_parse_location;
    return *this;
  }

  // Gets the ParseLocationRange of the argument type.
  std::optional<ParseLocationRange> argument_type_parse_location() const {
    return data_->argument_type_parse_location;
  }

  // Sets the default value of this argument. Only optional arguments can
  // (optionally) have default values.
  // Restrictions on the default values:
  // - For fixed-typed arguments, the type of <default_value> must be Equals to
  //   the type of the argument.
  // - Non-expression-typed templated arguments (e.g., tables, connections,
  //   models, etc.) cannot have default values.
  //
  // Also note that the type of <default_value> must outlive this object as well
  // as all the FunctionSignature instances created using this object.
  FunctionArgumentTypeOptions& set_default(Value default_value) {
    ABSL_DCHECK(default_value.is_valid()) << "Default value must be valid";
    data_->default_value = std::move(default_value);
    return *this;
  }

  // Returns true if a default value has been defined for this argument.
  bool has_default() const { return data_->default_value.has_value(); }

  // Gets the default value of this argument. Cannot use <default> here because
  // it is a C++ reserved word.
  const std::optional<Value>& get_default() const {
    return data_->default_value;
  }

  // Clears the default argument value set to this object.
  FunctionArgumentTypeOptions& clear_default() {
    data_->default_value = std::nullopt;
    return *this;
  }

  // See comments on `argument_collation_mode` field.
  ArgumentCollationMode argument_collation_mode() const {
    return data_->argument_collation_mode;
  }

  // See comments on `uses_array_element_for_collation` field.
  bool uses_array_element_for_collation() const {
    return data_->uses_array_element_for_collation;
  }

  // See comments on `uses_array_element_for_collation` field.
  FunctionArgumentTypeOptions& set_uses_array_element_for_collation(
      bool uses_array_element_for_collation = true) {
    data_->uses_array_element_for_collation = uses_array_element_for_collation;
    return *this;
  }

  // See comments on `argument_collation_mode` field.
  FunctionArgumentTypeOptions& set_argument_collation_mode(
      ArgumentCollationMode argument_collation_mode) {
    data_->argument_collation_mode = argument_collation_mode;
    return *this;
  }

 private:
  friend class FunctionSerializationTests;
  // The size of FunctionArgumentTypeOptions has grown over time contributing to
  // large stack frames. Large stack frames are problematic given the recursive
  // nature of SQL compilers, therefore, the data members of this class are
  // always allocated on the heap to increase the complexity of SQL that may be
  // compiled on small stacks.
  struct Data {
    ArgumentCardinality cardinality = FunctionEnums::REQUIRED;

    // Function argument always has value NOT_SET.
    // Procedure argument is in one of the 3 modes:
    // IN: argument is used only for input to the procedure. It is also the
    //     default mode for procedure argument if no mode is specified.
    // OUT: argument is used as output of the procedure.
    // INOUT: argument is used both for input to and output from the procedure.
    ProcedureArgumentMode procedure_argument_mode = FunctionEnums::NOT_SET;

    // These are min or max values (inclusive) for this argument.
    // If the argument has a literal value that is outside this range, the
    // analyzer will give an error.
    int64_t min_value = std::numeric_limits<int64_t>::lowest();
    int64_t max_value = std::numeric_limits<int64_t>::max();

    // This is a list of required column names and types for a relation argument
    // to a table-valued function. This is NULL if this is a non-relation
    // argument. For more information, please refer to the comment for the
    // constructor that fills this field.
    // TODO: Rename this to 'relation_schema' since this can apply for
    // the output table of table-valued functions too.
    std::shared_ptr<const TVFRelation> relation_input_schema;

    // Callback to support custom argument coercion in addition to standard
    // coercion rules.
    std::function<bool(const googlesql::Type*)> allow_coercion_from;

    // Optional user visible name for referring to the function argument by name
    // using explicit syntax: name => value. For CREATE [AGGREGATE/TABLE]
    // FUNCTION statements, this comes from the name specified for each argument
    // in the statement's function signature. In other cases, engines may assign
    // this in custom ways as needed.
    std::string argument_name;

    // Optional parse location range for argument name. It is populated by
    // resolver only when analyzing UDFs and TVFs. <record_parse_locations>
    // must also be set to true in the GoogleSQL analyzer options.
    std::optional<ParseLocationRange> argument_name_parse_location;

    // Optional parse location range for argument type. It is populated by
    // resolver only when analyzing UDFs and TVFs. <record_parse_locations>
    // must also be set to true in the GoogleSQL analyzer options.
    std::optional<ParseLocationRange> argument_type_parse_location;

    // Optional argument offset for descriptor argument types, which is only
    // populated for descriptor arguments whose columns should be resolved
    // from the table argument in the same tvf call at the specified argument
    // offset. The value must be the offset of an argument with table type.
    std::optional<int> descriptor_resolution_table_offset;

    // Optional value that holds the default value of the argument, if
    // applicable.
    std::optional<Value> default_value;

    // Defines how a function argument's collation should affect the function.
    // Can be used as a bit mask to check whether AFFECTS_OPERATION or
    // AFFECTS_PROPAGATION bit is set.
    // See FunctionEnums::ArgumentCollationMode for mode definitions.
    // See also FunctionSignatureOptions::propagates_collation_ and
    // uses_operation_collation_.
    ArgumentCollationMode argument_collation_mode =
        FunctionEnums::AFFECTS_OPERATION_AND_PROPAGATION;

    // Determines whether the argument name must or must not be specified when
    // the associated function is called.
    NamedArgumentKind named_argument_kind = FunctionEnums::POSITIONAL_ONLY;

    // Determines whether aliases are supported for a function argument.
    // An argument alias is an identifier associated with a function argument in
    // the form of F(<arg> AS <alias>).
    ArgumentAliasKind argument_alias_kind = FunctionEnums::ARGUMENT_NON_ALIASED;

    // Constness level required by the argument, stored internally, and set by
    // must be constant options.
    ConstnessLevelProto::Level constness_level =
        ConstnessLevelProto::CONSTNESS_UNSPECIFIED;

    // If true on function input argument, uses the array element's collation
    // when calculating the function's propagation or operation collation. If
    // true on function's result_type, the propagation collation should be set
    // on the array element.
    // The option should only be turned on when the type of the
    // FunctionArgumentType is array.
    bool uses_array_element_for_collation = false;

    // If true, this argument cannot be NULL.
    // An error will be returned if this overload is chosen and the argument
    // is a literal NULL.
    bool must_be_non_null = false;

    // If true, this argument is a NOT AGGREGATE argument to an aggregate
    // function.  This means that the argument must have a constant value over
    // all rows passed to the same aggregate function call.
    // Currently, this is enforced the same as `must_be_constant`.
    // This is ignored for non-aggregate functions.
    bool is_not_aggregate = false;

    // If true, this argument must have a type with SupportsEquality().
    // This is checked after choosing a concrete signature.
    bool must_support_equality = false;

    // If true, this argument must have a type with SupportsOrdering().
    // This is checked after choosing a concrete signature.
    bool must_support_ordering = false;

    // If true, this argument must have a type with SupportsGrouping().
    bool must_support_grouping = false;

    // If true, this argument must be an array type and have an element type
    // with SupportsEquality(). This is checked after choosing a concrete
    // signature.
    bool array_element_must_support_equality = false;

    // If true, this argument must be an array type and have an element type
    // with SupportsOrdering(). This is checked after choosing a concrete
    // signature.
    bool array_element_must_support_ordering = false;

    // If true, this argument must be an array type and have an element type
    // with SupportsGrouping().
    bool array_element_must_support_grouping = false;

    bool has_min_value = false;
    bool has_max_value = false;

    // If true, the provided input relation may contain extra column names
    // besides those required in `relation_input_schema`. Otherwise, GoogleSQL
    // rejects the query if the provided relation contains such extra columns.
    bool extra_relation_input_columns_allowed = true;
  };
  std::unique_ptr<Data> data_;
};

// A type for an argument or result value in a function signature.  Types
// can be fixed or templated.  Arguments can be marked as repeated (denoting
// it can occur zero or more times in a function invocation) or optional.
// Result types cannot be marked as repeated or optional.
// Type VOID is valid for the return type in Procedures and in
// ResolvedDropFunctionStmt only; VOID is not allowed as the return type for
// Functions, and is never allowed as a argument type.
// A FunctionArgumentType is concrete if it is not templated and
// num_occurrences_ indicates how many times the argument appears in a
// concrete FunctionSignature.  FunctionArgumentTypeOptions can be used to
// apply additional constraints on legal values for the argument.
class FunctionArgumentType {
 public:
  class ArgumentTypeLambda;
  typedef FunctionEnums::ArgumentCardinality ArgumentCardinality;
  static constexpr ArgumentCardinality REQUIRED = FunctionEnums::REQUIRED;
  static constexpr ArgumentCardinality REPEATED = FunctionEnums::REPEATED;
  static constexpr ArgumentCardinality OPTIONAL = FunctionEnums::OPTIONAL;

  // Construct a templated argument of <kind>, which must not be ARG_TYPE_FIXED.
  // The num_occurrences default value (-1) indicates a non-concrete argument.
  // Concrete arguments must have this set to a non-negative number.
  FunctionArgumentType(SignatureArgumentKind kind,
                       ArgumentCardinality cardinality,
                       int num_occurrences = -1);
  FunctionArgumentType(SignatureArgumentKind kind,
                       FunctionArgumentTypeOptions options,
                       int num_occurrences = -1);
  FunctionArgumentType(SignatureArgumentKind kind,  // implicit; NOLINT
                       int num_occurrences = -1);
  // Construct a non-templated argument kind fixed type <type>.
  // Does not take ownership of <type>.
  FunctionArgumentType(const Type* type, ArgumentCardinality cardinality,
                       int num_occurrences = -1);
  FunctionArgumentType(const Type* type, FunctionArgumentTypeOptions options,
                       int num_occurrences = -1);
  FunctionArgumentType(const Type* type,  // implicit; NOLINT
                       int num_occurrences = -1);
  // For convenience when manually crafting concrete signatures.
  // The original kind is needed for annotations propagation.
  // Manual crafting of signatures is discouraged, but to make things safer,
  // all other constructors should delegate to this one, and if num_occurrences
  // is not concrete (i.e., > -1), we should ensure that a valid original kind
  // is passed, e.g. ABSL_DCHECK(original_kind != __switch_must_have_a_default__).
  FunctionArgumentType(const Type* type, int num_occurrences,
                       SignatureArgumentKind original_kind);

  // Construct a relation argument type for a table-valued function. This
  // argument will accept any input relation of any schema.
  static FunctionArgumentType AnyRelation() {
    return FunctionArgumentType(ARG_TYPE_RELATION);
  }

  // Construct a model argument type for a table-valued function. This argument
  // will accept any model.
  static FunctionArgumentType AnyModel() {
    return FunctionArgumentType(ARG_TYPE_MODEL);
  }

  // Construct a graph argument type for a table-valued function. This argument
  // will accept any graph.
  static FunctionArgumentType AnyGraph() {
    return FunctionArgumentType(ARG_TYPE_GRAPH);
  }

  // Constructs a connection argument type for a table-valued function. This
  // argument will accept any connection.
  static FunctionArgumentType AnyConnection() {
    return FunctionArgumentType(ARG_TYPE_CONNECTION);
  }

  // Constructs a descriptor argument type for a table-valued function. This
  // argument accepts a <table_offset> parameter to indicate if resolving column
  // names from a table parameter. <table_offset> < 0 means does not resolve
  // column names. <table_offset> >= 0 means an argument offset.
  static FunctionArgumentType AnyDescriptor(int table_offset = -1) {
    FunctionArgumentTypeOptions option =
        FunctionArgumentTypeOptions(FunctionEnums::REQUIRED);
    if (table_offset >= 0) {
      option.set_resolve_descriptor_names_table_offset(table_offset);
    }
    return FunctionArgumentType(ARG_TYPE_DESCRIPTOR, option);
  }

  // Construct a lambda argument type with lambda_argument_types and
  // lambda_body_type. This argument will accept lambdas with the specified
  // argument types and body type.
  static FunctionArgumentType Lambda(
      FunctionArgumentTypeList lambda_argument_types,
      FunctionArgumentType lambda_body_type);

  // Construct a lambda argument type with lambda_argument_types,
  // lambda_body_type and options. This argument will accept
  // lambdas with the specified argument types and body type.
  static FunctionArgumentType Lambda(
      FunctionArgumentTypeList lambda_argument_types,
      FunctionArgumentType lambda_body_type,
      FunctionArgumentTypeOptions options);

  // Constructs a sequence argument type for a function. This argument will
  // accept any sequence.
  static FunctionArgumentType AnySequence() {
    return FunctionArgumentType(ARG_TYPE_SEQUENCE);
  }

  // Construct a relation argument type for a table-valued function.
  //
  // This argument accepts an input relation with the names of columns in
  // 'relation_input_schema', and the type of each column of the input relation
  // must be equal or coercible to the matching column in
  // 'relation_input_schema'.
  //
  // The provided input relation may contain extra column names besides those
  // listed here if and only if 'extra_relation_input_columns_allowed' is set to
  // true. Extra columns are allowed by default here, but can be disabled if
  // needed.
  // TODO: Move the relation schema out of
  // FunctionArgumentTypeOptions and into this class.  It is analogous to
  // <type> for scalar arguments and result types.  The options should only
  // be relevant to argument types (not result types), but currently a result
  // type puts this schema into options.
  static FunctionArgumentType RelationWithSchema(
      const TVFRelation& relation_input_schema,
      bool extra_relation_input_columns_allowed) {
    return FunctionArgumentType(
        ARG_TYPE_RELATION,
        FunctionArgumentTypeOptions(relation_input_schema,
                                    extra_relation_input_columns_allowed));
  }

  FunctionArgumentType(const FunctionArgumentType& other) = default;
  FunctionArgumentType& operator=(const FunctionArgumentType& other) = default;

  // Deserialization of ParseLocationRange would refer to the filename in
  // ParseLocationRangeProto as string_view.
  // TODO Add support for storing filename as string in
  // ParseLocationPoint.
  static absl::StatusOr<std::unique_ptr<FunctionArgumentType>> Deserialize(
      const FunctionArgumentTypeProto& proto,
      const TypeDeserializer& type_deserializer);

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         FunctionArgumentTypeProto* proto) const;

  const FunctionArgumentTypeOptions& options() const { return *options_; }

  bool required() const { return cardinality() == REQUIRED; }
  bool repeated() const { return cardinality() == REPEATED; }
  bool optional() const { return cardinality() == OPTIONAL; }
  ArgumentCardinality cardinality() const { return options_->cardinality(); }

  bool must_be_constant() const { return options_->must_be_constant(); }
  bool must_be_analysis_constant() const {
    return options_->must_be_analysis_constant();
  }
  bool must_be_immutable_constant() const {
    return options_->must_be_immutable_constant();
  }
  bool must_be_constant_expression() const {
    return options_->must_be_constant_expression();
  }
  bool has_argument_name() const { return options_->has_argument_name(); }
  const std::string& argument_name() const { return options_->argument_name(); }

  int num_occurrences() const { return num_occurrences_; }
  void IncrementNumOccurrences() { ++num_occurrences_; }
  void set_num_occurrences(int num) { num_occurrences_ = num; }

  // Returns NULL if kind_ is not ARG_TYPE_FIXED or ARG_TYPE_LAMBDA. If kind_ is
  // ARG_TYPE_LAMBDA, returns the type of lambda body type, which could be NULL
  // if the body type is templated.
  const Type* type() const { return type_; }

  SignatureArgumentKind kind() const { return kind_; }

  SignatureArgumentKind original_kind() const {
    ABSL_DCHECK_NE(original_kind_,
              SignatureArgumentKind::
                  __SignatureArgumentKind__switch_must_have_a_default__)
        << DebugString(/*verbose=*/true);
    return original_kind_;
  }
  void set_original_kind(SignatureArgumentKind kind) {
    ABSL_DCHECK_NE(kind, SignatureArgumentKind::
                        __SignatureArgumentKind__switch_must_have_a_default__)
        << DebugString(/*verbose=*/true);
    original_kind_ = kind;
  }

  // Returns information about a lambda typed function argument.
  const ArgumentTypeLambda& lambda() const {
    ABSL_DCHECK(IsLambda());
    ABSL_DCHECK(lambda_ != nullptr)
        << "FunctionArgumentType with ARG_TYPE_LAMBDA constructed directly is "
           "not allowed. Use FunctionArgumentType::Lambda instead.";
    return *lambda_;
  }

  // Returns TRUE if kind_ is ARG_TYPE_FIXED or ARG_TYPE_RELATION and the number
  // of occurrences is greater than -1.
  bool IsConcrete() const;

  bool IsTemplated() const;

  bool IsScalar() const;
  bool IsRelation() const { return kind_ == ARG_TYPE_RELATION; }
  bool IsModel() const { return kind_ == ARG_TYPE_MODEL; }
  bool IsConnection() const { return kind_ == ARG_TYPE_CONNECTION; }
  bool IsLambda() const { return kind_ == ARG_TYPE_LAMBDA; }
  bool IsSequence() const { return kind_ == ARG_TYPE_SEQUENCE; }
  bool IsGraph() const { return kind_ == ARG_TYPE_GRAPH; }
  bool IsFixedRelation() const {
    return kind_ == ARG_TYPE_RELATION && options_->has_relation_input_schema();
  }
  bool IsVoid() const { return kind_ == ARG_TYPE_VOID; }

  bool IsDescriptor() const { return kind_ == ARG_TYPE_DESCRIPTOR; }
  std::optional<int> GetDescriptorResolutionTableOffset() const {
    return options_->get_resolve_descriptor_names_table_offset();
  }

  // Returns TRUE if kind() can be used to derive something about kind.
  // For example, if kind() is ARG_ARRAY_TYPE_ANY_1, it can be used to derive
  // information about ARG_TYPE_ANY_1, but not ARG_TYPE_ANY_2. Likewise, a
  // proto map key can be used to derive information about the map itself, but
  // not about the map value.
  bool TemplatedKindIsRelated(SignatureArgumentKind kind) const;

  bool AllowCoercionFrom(const googlesql::Type* actual_arg_type) const {
    if (options_->allow_coercion_from() == nullptr) {
      return false;
    }
    return options_->allow_coercion_from()(actual_arg_type);
  }

  // Returns TRUE if the argument has a default value provided in the argument
  // option.
  bool HasDefault() const { return options_->get_default().has_value(); }
  // Returns default value provided in the argument option, or std::nullopt if
  // the argument does not have a default value.
  const std::optional<Value>& GetDefault() const {
    return options_->get_default();
  }

  // Returns argument type name to be used in error messages.
  // This either would be a scalar short type name - DATE, INT64, BYTES etc. or
  // STRUCT, PROTO, ENUM for complex type names, or ANY when any data type is
  // allowed.
  // If `print_template_details` is true, template arguments ANY_1/2 are printed
  // as T1/T2 rather than just as ANY.
  std::string UserFacingName(ProductMode product_mode,
                             bool print_template_details = false) const;

  // When printing the argument with cardinality, this enum controls whether
  // argument names are printed.
  enum class NamePrintingStyle {
    // Prints argument names with their type when `named_argument_kind()` is
    // not `POSITIONAL_ONLY`.
    kIfNotPositionalOnly,

    // Prints argument names with their type when `named_argument_kind()` is
    // `NAMED_ONLY`.
    kIfNamedOnly,
  };

  // Returns user facing text for the argument including argument cardinality
  // (to be used in error message):
  //   - required, just argument type, e.g. INT64
  //   - optional, argument type enclosed in [], e.g. [INT64]
  //   - repeated, argument type enclosed in [] with ..., e.g. [INT64, ...]
  // If `print_template_details` is true, template arguments ANY_1/2 are printed
  // as T1/T2 rather than just as ANY.
  std::string UserFacingNameWithCardinality(
      ProductMode product_mode, NamePrintingStyle print_style,
      bool print_template_details = false) const;

  // Checks concrete arguments to validate the number of occurrences.
  absl::Status IsValid(ProductMode product_mode) const;

  // If verbose is true, include FunctionOptions modifiers.
  std::string DebugString(bool verbose = false) const;

  // Get the SQL declaration for this argument, including all options.
  // The result is formatted as SQL that can be included inside a function
  // signature in CREATE FUNCTION, DROP FUNCTION, etc, if possible.
  std::string GetSQLDeclaration(ProductMode product_mode) const;

  // Same as thee above `GetSQLDeclaration`, but setting `use_external_float32`
  // to true will return FLOAT32 as the type name for TYPE_FLOAT in
  // PRODUCT_EXTERNAL mode.
  // TODO: Remove this signature once FLOAT32 is used by default
  // for PRODUCT_EXTERNAL.
  std::string GetSQLDeclaration(ProductMode product_mode,
                                bool use_external_float32) const;

  static std::string SignatureArgumentKindToString(SignatureArgumentKind kind);

 private:
  FunctionArgumentType(
      SignatureArgumentKind kind, const Type* type,
      std::shared_ptr<const FunctionArgumentTypeOptions> options,
      int num_occurrences);

  // Checks that 'arg_type' could be used as lambda argument type and body type.
  static absl::Status CheckLambdaArgType(const FunctionArgumentType& arg_type);

  // Returns shared options objects used in most common cases.
  static std::shared_ptr<const FunctionArgumentTypeOptions> SimpleOptions(
      ArgumentCardinality cardinality = FunctionEnums::REQUIRED);

  SignatureArgumentKind kind_;

  // Used during resolution for annotations propagation to find correlated
  // templated arguments. This must be set for concrete signatures, and never
  // appears in the final tree.
  // This is also why it's never serialized in the proto.
  SignatureArgumentKind original_kind_ = SignatureArgumentKind::
      __SignatureArgumentKind__switch_must_have_a_default__;

  // Indicates how many times a concrete argument occurred in a concrete
  // function signature.  REQUIRED concrete arguments must occur exactly 1
  // time, OPTIONALs can occur 0 or 1 times, and REPEATEDs can occur 0 or
  // more times.  For non-concrete arguments it is -1.
  int num_occurrences_;

  const Type* type_;

  // This holds the argument type options. It is a shared pointer to reduce
  // stack frame sizes when the function signatures are kept on the stack.
  std::shared_ptr<const FunctionArgumentTypeOptions> options_;

  // This holds lambda type specifications.
  // It is a shared pointer to
  //   * reduce stack frame sizes when the function signatures are on the stack
  //   * avoid having a manual copy constructor required by unique_ptr.
  //   * avoid compiler error of ArgumentTypeLambda recursively uses
  //   FunctionArgumentType.
  std::shared_ptr<ArgumentTypeLambda> lambda_;

  friend class FunctionSerializationTests;
  // Copyable.
};

// Contains type information for ARG_TYPE_LAMBDA, which represents the lambda
// type of a function argument. A lambda has a list of arguments and a body.
// Both the lambda arguments and body could be templated or nontemplated.
// Putting them together to minimize stack usage of FunctionArgumentType.
class FunctionArgumentType::ArgumentTypeLambda {
 public:
  ArgumentTypeLambda(FunctionArgumentTypeList lambda_argument_types,
                     FunctionArgumentType lambda_body_type)
      : argument_types_(std::move(lambda_argument_types)),
        body_type_(lambda_body_type) {}

  const FunctionArgumentTypeList& argument_types() const {
    return argument_types_;
  }
  const FunctionArgumentType& body_type() const { return body_type_; }

 private:
  // A list of types for lambda arguments.
  FunctionArgumentTypeList argument_types_;
  // Type of the lambda body.
  FunctionArgumentType body_type_;
};

// Configures a built-in ResolvedAST rewriter to replace calls to a function
// signature with a `ResolvedExpr` that expresses the function logic. Rewriting
// typically happens as the last step in statement analysis. Some clients
// disable rewriting during analysis and separately run `RewriteResolvedAST`.
// See ./analyzer.h for more details.
//
// Glossary:
// 'direct implementation': An implementation of a SQL function's logic in C++
//     or whatever language the query engine is implemented in.
// 'rewrite implementation': An implementation of a SQL function's logic by
//     replacing the `ResolvedFunctionCall` with a `ResolvedExpr` that
//     implements the appropriate logic.
// 'ResolvedAST rewriter': A component of the analyzer that transforms one
//     ResolvedAST shape into another semantically equivalent ResolvedAST shape.
//
// Configuring rewrite implementations is appropriate for built-in functions
// that are part of GoogleSQL's core function library.
//
// For now (Q1'23) these options should only be used for GoogleSQL core library
// functions because the APIs are still in development and are not yet stable.
// TODO: Replace the above paragraph with a link to the user
//     documentation for engines once it is published.
//
// The `rewrite` field identifies which ResolvedAST rewriter will be used to
// implement this function. See `ResolvedASTRewite` and files in
// '../analyzer/rewriters/*' for more.
//
// The `enabled` field controls whether the analyzer performs the rewriting for
// this `FunctionSignature`. The `false` state is set for built-in functions to
// provide off-by-default rewrite implementations. Engines can also set
// `enabled` to `false` to disable the rewrite implementation for this
// `FunctionSignature` when they provide an optimized direct implementation.
// TODO: Document enabling and disabling rewrites once mutable
//     accessors are checked in.
//
// ## Inspecting rewrite configuration of built-in functions for an engine.
//
// Engine code that sets up a GoogleSQL `Catalog` typically calls
// `GetBuiltinFunctionsAndTypes` (found in ./builtin_function.h) to get the
// function signatures for GoogleSQL core library functions. Some engines add
// all the returned `FunctionSignature`s to the catalog, while other engines
// filter the set to only implemented functions. Engines that filter might want
// to consider the rewrite configuration in the filter. For example, an engine
// that wants to allow functions it has direct evaluators for and functions with
// rewrite implementations, might write code like this:
//
// ```c++
// googlesql::LanguageOptions language_opts = GetLanguageOptions();
// googlesql::TypeFactory* type_factory = GetTypeFactory();
// googlesql::NameToFunctionMap function_map;
// googlesql::NameToTypeMap types_map;
// GOOGLESQL_CHECK_OK(googlesql::GetBuiltinFunctionsAndTypes(
//     googlesql::BuiltinFunctionOptions(language_opts), type_factory,
//     function_map, types_map));
//
// for (const auto& [name, function] : function_map) {
//   std::vector<googlesql::FunctionSignature> allowed_signatures;
//   for (int i = 0; i < function->NumSignatures(); ++i) {
//     const googlesql::FunctionSignature* signature =
//         function->GetSignature(i);
//     bool has_rewrite_impl = signature->HasEnabledRewriteImplementation();
//     bool has_direct_impl = my_engine::HasDirectEvaluator(signature);
//     if (has_rewrite_impl || has_direct_impl) {
//       allowed_signatures.push_back(*signature);
//     }
//   }
//   if (!allowed_signatures.empty()) {
//     AddToCatalog(function, allowed_signatures);
//   }
// }
// ```
//
// ## Adjusting rewrite configuration of built-in functions for an engine.
//
// Engine code that sets up a GoogleSQL `Catalog` typically calls
// `GetBuiltinFunctionsAndTypes` (found in ./builtin_function.h) to get all
// the function signatures for GoogleSQL core library functions. Some engines
// might want to change some of the fields in `FunctionSignatureRewriteOptions`
// to enable/disable the rewrite implementation for the function signature or to
// change other fields in this configuration. The `FunctionSignature` API is not
// conducive to adjusting rewrite configuration right now because it doesn't
// have getters for mutable access.
class FunctionSignatureRewriteOptions {
 public:
  FunctionSignatureRewriteOptions() = default;

  // When true, and the AnalyzerOptions enable `rewrite()`, the rewriting phase
  // of the analyzer will attempt to replace calls to this function signature
  // with a ResolvedAST fragment that implements it. If this is false, or if
  // `rewrite()` is not enabled, this function signature is not subject to
  // rewrite implementation.
  bool enabled() const { return enabled_; }
  FunctionSignatureRewriteOptions& set_enabled(bool value) {
    enabled_ = value;
    return *this;
  }

  // Identify the ResolvedAST rewriter used to implement this function
  // signature.
  ResolvedASTRewrite rewriter() const { return rewriter_; }
  FunctionSignatureRewriteOptions& set_rewriter(ResolvedASTRewrite rewriter) {
    rewriter_ = rewriter;
    return *this;
  }

  // SQL expression implementing the logic of this function. Depending on which
  // ResolvedAST rewriter is used, it may or may not expect this field to be
  // filled. See the ResolvedAST rewriter specified in `rewrite()` for
  // specific documentation regarding the requirements of this SQL.
  //
  // For example, the `REWRITE_BUILTIN_FUNCTION_INLINER` ResolvedAST rewriter
  // requires a SQL definition of the function to be specified here. The SQL
  // definition does not need to handle as-if-once semantics of arguments or
  // differentiation of SAFE mode calls. It does need to do any appropriate
  // handling of `NULL` argument values and should produce runtime errors using
  // the `ERROR(...)` function. Arguments are referenced by the name supplied in
  // the `FunctionArgumentTypeOptions`.
  absl::string_view sql() const { return sql_; }
  FunctionSignatureRewriteOptions& set_sql(absl::string_view sql) {
    sql_ = std::string(sql);
    return *this;
  }

  bool allow_table_references() const { return allow_table_references_; }
  FunctionSignatureRewriteOptions& set_allow_table_references(
      const bool allow_table_references) {
    allow_table_references_ = allow_table_references;
    return *this;
  }

  std::vector<std::string> allowed_function_groups() const {
    return allowed_function_groups_;
  }
  FunctionSignatureRewriteOptions& set_allowed_function_groups(
      std::vector<std::string> allowed_function_groups) {
    allowed_function_groups_ = std::move(allowed_function_groups);
    return *this;
  }

  static absl::Status Deserialize(
      const FunctionSignatureRewriteOptionsProto& proto,
      FunctionSignatureRewriteOptions& result);

  void Serialize(FunctionSignatureRewriteOptionsProto* proto) const;

 private:
  bool enabled_ = true;
  ResolvedASTRewrite rewriter_ = ResolvedASTRewrite::REWRITE_INVALID_DO_NOT_USE;
  std::string sql_;
  // Whether or not the rewrite SQL is allowed to reference tables. This
  // restriction applies to `REWRITE_BUILTIN_FUNCTION_INLINER`, but might not be
  // enforced by other rewriters.
  bool allow_table_references_ = false;
  // A (case-sensitive) list of function groups that are allowed to be
  // referenced in the rewrite SQL. By default, only GoogleSQL-builtin functions
  // are allowed, but engines can set this to allow additional function groups.
  // This restriction applies to `REWRITE_BUILTIN_FUNCTION_INLINER`, but might
  // not be enforced by other rewriters.
  std::vector<std::string> allowed_function_groups_ = {};
};

// Returns the reason why the concrete argument list is NOT valid for a matched
// concrete FunctionSignature. Returns empty string if no error is found. The
// returned reason is used to compose detailed signature mismatching error
// message for the signature.
// It is guaranteed that the passed in signature is concrete and the input
// argument types match 1:1 with the concrete arguments in the signature.
using FunctionSignatureArgumentConstraintsCallback = std::function<std::string(
    const FunctionSignature&, const std::vector<InputArgumentType>&)>;

// This callback calculates the annotations for the function signature's result
// type. It is used when a SQL function signature needs non-default annotation
// propagation logic.
//
// New annotations should be allocated in `type_factory`.
//
// The callback will be invoked after `ComputeResultTypeCallback`, if the SQL
// function has it. When it is invoked, the function signature is already
// selected. Errors returned by this function will become analysis errors
// for the query.
using ComputeResultAnnotationsCallback =
    std::function<absl::StatusOr<const AnnotationMap*>(
        const ResolvedFunctionCallBase& function_call,
        TypeFactory& type_factory)>;

class FunctionSignatureOptions {
 public:
  FunctionSignatureOptions() = default;

  // Setter/getter for a callback to check if a concrete argument list is valid
  // for a matched concrete signature.
  FunctionSignatureOptions& set_constraints(
      FunctionSignatureArgumentConstraintsCallback argument_constraints) {
    constraints_ = argument_constraints;
    return *this;
  }

  // Setter/getter for whether this is a deprecated function signature. If so,
  // the analyzer will generate a deprecation warning if this signature is used
  // when the related function is called.
  FunctionSignatureOptions& set_is_deprecated(bool value) {
    is_deprecated_ = value;
    return *this;
  }
  bool is_deprecated() const { return is_deprecated_; }

  // Setter/getter for whether this is an internal function signature. If so,
  // the analyzer won't match this signature when the function name is called
  // by a user.
  FunctionSignatureOptions& set_is_internal(bool value) {
    is_internal_ = value;
    return *this;
  }
  bool is_internal() const { return is_internal_; }

  // Setter/getter for whether or not this signature is hidden from the user.
  FunctionSignatureOptions& set_is_hidden(bool value) {
    is_hidden_ = value;
    return *this;
  }
  bool is_hidden() const { return is_hidden_; }

  // Setters/getters for additional deprecation warnings associated with
  // this function signature. These have DeprecationWarning protos attached. The
  // analyzer will propagate these warnings to any statement that invokes this
  // function signature. (They will appear in
  // AnalyzerOutput::deprecation_warnings().)
  //
  // These warnings are typically populated for SQL UDFs, whose SQL
  // expressions use deprecated functionality. A specific example is a
  // non-templated SQL UDF whose body invokes a deprecated function signature.
  // (Templated functions are handled differently because their bodies are not
  // resolved until they are called.) We propagate the deprecation warning to
  // any statement that invokes the UDF to make it more visible, particularly
  // for cases where we are trying to assess the number of queries that use a
  // deprecated feature that we are considering removing.
  const std::vector<FreestandingDeprecationWarning>&
  additional_deprecation_warnings() const {
    return additional_deprecation_warnings_;
  }
  // Uses a template to allow both vectors and RepeatedPtrFields.
  template <class ContainerType>
  FunctionSignatureOptions& set_additional_deprecation_warnings(
      const ContainerType& warnings) {
    additional_deprecation_warnings_.clear();
    additional_deprecation_warnings_.reserve(warnings.size());
    for (const FreestandingDeprecationWarning& warning : warnings) {
      additional_deprecation_warnings_.push_back(warning);
    }
    return *this;
  }

  // Add a LanguageFeature that must be enabled for this function to be enabled.
  // This is used only on built-in functions, and determines whether they will
  // be loaded in GetBuiltinFunctionsAndTypes.
  FunctionSignatureOptions& AddRequiredLanguageFeature(
      LanguageFeature feature) {
    googlesql_base::InsertIfNotPresent(&required_language_features_, feature);
    return *this;
  }

  // Returns whether or not all language features required by a function are
  // enabled.
  ABSL_MUST_USE_RESULT bool CheckAllRequiredFeaturesAreEnabled(
      const LanguageOptions::LanguageFeatureSet& enabled_features) const {
    for (const LanguageFeature& feature : required_language_features_) {
      if (enabled_features.find(feature) == enabled_features.end()) {
        return false;
      }
    }
    return true;
  }

  // Returns whether the given feature is required.
  ABSL_MUST_USE_RESULT bool RequiresFeature(LanguageFeature feature) const {
    return required_language_features_.find(feature) !=
           required_language_features_.end();
  }

  // Setter/getter for whether function name for this signature is aliased
  // (non primary).
  FunctionSignatureOptions& set_is_aliased_signature(bool value) {
    is_aliased_signature_ = value;
    return *this;
  }
  bool is_aliased_signature() const { return is_aliased_signature_; }

  // Checks constraint satisfaction on the <concrete_signature> (that must be
  // concrete).
  // If constraints are not met then the signature is ignored during analysis.
  // Evaluates the constraint callback if populated, and if the signature is
  // sensitive to the TimestampMode then checks that as well.
  absl::StatusOr<std::string> CheckFunctionSignatureConstraints(
      const FunctionSignature& concrete_signature,
      const std::vector<InputArgumentType>& arguments) const;

  // See comments on propagates_collation_ field.
  bool propagates_collation() const { return propagates_collation_; }

  // See comments on propagates_collation_ field.
  FunctionSignatureOptions& set_propagates_collation(
      bool propagates_collation) {
    propagates_collation_ = propagates_collation;
    return *this;
  }

  // See comments on uses_operation_collation_ field.
  bool uses_operation_collation() const { return uses_operation_collation_; }

  // See comments on uses_operation_collation_ field.
  FunctionSignatureOptions& set_uses_operation_collation(
      bool uses_operation_collation = true) {
    uses_operation_collation_ = uses_operation_collation;
    return *this;
  }

  // See comments on rejects_collation_ field.
  bool rejects_collation() const { return rejects_collation_; }

  // See comments on rejects_collation_ field.
  FunctionSignatureOptions& set_rejects_collation(
      bool rejects_collation = true) {
    rejects_collation_ = rejects_collation;
    return *this;
  }

  // Sets options related to a rewrite implementation of this function
  // signature.
  FunctionSignatureOptions& set_rewrite_options(
      FunctionSignatureRewriteOptions options) {
    rewrite_options_ = options;
    return *this;
  }

  // Adds a custom callback for the SQL function signature to compute the result
  // annotations.
  // Note the callback is invoked after `compute_restul_type_callback`, if it is
  // not nullptr, because this callback needs the correct concrete result type.
  FunctionSignatureOptions& set_compute_result_annotations_callback(
      ComputeResultAnnotationsCallback callback) {
    compute_result_annotations_callback_ = std::move(callback);
    return *this;
  }

  const ComputeResultAnnotationsCallback& compute_result_annotations_callback()
      const {
    return compute_result_annotations_callback_;
  }

  // Returns options related to a rewrite implementation of this function
  // signature.
  const std::optional<FunctionSignatureRewriteOptions>& rewrite_options()
      const {
    return rewrite_options_;
  }

  // Returns a non-const reference to the function inlining configuration.
  std::optional<FunctionSignatureRewriteOptions>& mutable_rewrite_options() {
    return rewrite_options_;
  }

  static absl::Status Deserialize(
      const FunctionSignatureOptionsProto& proto,
      std::unique_ptr<FunctionSignatureOptions>* result);

  void Serialize(FunctionSignatureOptionsProto* proto) const;

 private:
  friend class FunctionSerializationTests;

  // Validates constraints against a concrete argument list after matching
  // a FunctionSignature.  For example, this could verify that at least one
  // argument is floating point.
  FunctionSignatureArgumentConstraintsCallback constraints_;

  // Stores any deprecation warnings associated with the body of a SQL function.
  std::vector<FreestandingDeprecationWarning> additional_deprecation_warnings_;

  // A set of LanguageFeatures that need to be enabled for the signature to be
  // loaded in GetBuiltinFunctionsAndTypes.
  std::set<LanguageFeature> required_language_features_;

  bool is_deprecated_ = false;

  bool is_internal_ = false;

  bool is_hidden_ = false;

  // When true, the signature uses the same signature (context) id as another
  // signature with different function name, and this signature's function name
  // is an alias. This flag is useful when trying to resolve signature id to
  // function name - there could be multiple function names for same id, but
  // the one with is_aliased_signature = false should be picked as primary.
  bool is_aliased_signature_ = false;

  // When true, collation will be propagated from the function arguments to the
  // function's return type. Collation propagates only when the output type
  // supports collation and at least one input argument type supports collation.
  // Only Arguments where FunctionArgumentTypeOptions::argument_collation_mode()
  // has AFFECTS_PROPAGATION bit set are considered in calculating the
  // propagation collation.
  //
  // This option is only effective when FEATURE_COLLATION_SUPPORT is
  // enabled.
  bool propagates_collation_ = true;

  // When true, this function's behavior is affected by collation (e.g. for
  // string comparisons).  The resolver will select the collation to use and
  // record it in the resolved AST.
  //
  // The operation collation will be derived based on collation of input
  // arguments.  Only Arguments where
  // FunctionArgumentTypeOptions::argument_collation_mode() has
  // AFFECTS_OPERATION bit set are considered in calculating the operation
  // collation.
  //
  // This option is only effective when FEATURE_COLLATION_SUPPORT is
  // enabled.
  bool uses_operation_collation_ = false;

  // When true, an error will be returned during resolution if any argument
  // of the function has collation.
  bool rejects_collation_ = false;

  // Configures a rewrite implementation of this function signature.
  std::optional<FunctionSignatureRewriteOptions> rewrite_options_;

  // If not nullptr, this is used to compute the result annotations after the
  // function call return type is determined.
  ComputeResultAnnotationsCallback compute_result_annotations_callback_ =
      nullptr;

  // Copyable.
};

// FunctionSignature identifies the argument Types and other properties
// per overload of a Function (or a similar object, like a Procedure or
// TableValuedFunction).  A FunctionSignature is concrete if it
// identifies the exact number and fixed Types of its arguments and results.
// A FunctionSignature can be non-concrete, but have concrete arguments.
// A FunctionSignature can be abstract, specifying templated types and
// identifying arguments as repeated or optional.  Optional arguments must
// appear at the end of the argument list.
//
// If multiple arguments are repeated, they must be consecutive and are
// treated as if they repeat together.  To illustrate, consider the expression:
// 'CASE WHEN <bool_expr_1> THEN <expr_1>
//       WHEN <bool_expr_2> THEN <expr_2>
//       ...
//       ELSE <expr_n> END'.
//
// This expression has the following signature <arguments>:
//   arg1: <bool> repeated - WHEN
//   arg2: <any_type_1> repeated - THEN
//   arg3: <any_type_1> optional - ELSE
//   result: <any_type_1>
//
// The WHEN and THEN arguments (arg1 and arg2) repeat together and must
// occur at least once, and the ELSE is optional.  The THEN, ELSE, and
// RESULT types can be any type, but must be the same type.
//
// In order to avoid potential ambiguity, the number of optional arguments
// must be less than the number of repeated arguments.
//
// The FunctionSignature also includes <options> for specifying
// additional signature matching requirements, if any.
class FunctionSignature {
 public:
  // Does not take ownership of <context_ptr>.
  FunctionSignature(FunctionArgumentType result_type,
                    FunctionArgumentTypeList arguments, void* context_ptr);

  FunctionSignature(FunctionArgumentType result_type,
                    FunctionArgumentTypeList arguments, int64_t context_id);

  FunctionSignature(const FunctionArgumentType& result_type,
                    FunctionArgumentTypeList arguments, int64_t context_id,
                    FunctionSignatureOptions options);

  // Copy a FunctionSignature, assigning a new context_ptr or context_id.
  FunctionSignature(const FunctionSignature& old, void* context_ptr)
      : FunctionSignature(old) {
    context_ptr_ = context_ptr;
  }
  FunctionSignature(const FunctionSignature& old, int64_t context_id)
      : FunctionSignature(old) {
    context_id_ = context_id;
  }

  // Returns the initialization status of this function signature.
  // A Create method that returns the signature with a status would be ideal,
  // but there are too many calls to the constructor to migrate without
  // a large effort.
  // This can be called after construction to check the status early. Otherwise,
  // this will be checked when resolving function calls with this signature.
  absl::Status init_status() const { return init_status_; }

  FunctionSignature(const FunctionSignature& other) = default;
  FunctionSignature& operator=(const FunctionSignature& other) = default;

  static absl::StatusOr<std::unique_ptr<FunctionSignature>> Deserialize(
      const FunctionSignatureProto& proto,
      const TypeDeserializer& type_deserializer);

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         FunctionSignatureProto* proto) const;

  const FunctionArgumentTypeList& arguments() const { return arguments_; }

  const FunctionArgumentType& argument(int idx) const {
    return arguments_[idx];
  }

  // Returns the number of concrete arguments, with repeated and optional
  // arguments expanded.
  // Requires: HasConcreteArguments()
  int NumConcreteArguments() const {
    ABSL_DCHECK(HasConcreteArguments());
    return concrete_arguments_.size();
  }

  // Returns concrete argument number <concrete_idx>.
  // Differs from argment(idx) above in that repeated and optional arguments
  // are fully expanded in a concrete signature.
  // Requires that the signature has concrete arguments.
  const FunctionArgumentType& ConcreteArgument(int concrete_idx) const {
    ABSL_DCHECK(HasConcreteArguments());
    return concrete_arguments_[concrete_idx];
  }

  // Returns the Type associated with the concrete argument number
  // <concrete_idx>.
  // Requires that the signature has concrete arguments.
  const Type* ConcreteArgumentType(int concrete_idx) const {
    return ConcreteArgument(concrete_idx).type();
  }

  const FunctionArgumentType& result_type() const { return result_type_; }

  // These methods use state populated during construction, if the signature is
  // valid. With invalid signatures (init_status() is not OK), these may not be
  // populated.
  bool HasNamedArgument(absl::string_view name) const {
    return argument_name_to_options_.contains(name);
  }
  // Check init_status before using.
  const FunctionArgumentTypeOptions* FindNamedArgumentOptions(
      absl::string_view name) const {
    return googlesql_base::FindPtrOrNull(argument_name_to_options_, name);
  }
  // Check init_status before using.
  // -1 if no argument with default exists.
  int last_arg_index_with_default() const {
    return last_arg_index_with_default_;
  }
  // Check init_status before using.
  // -1 if no named argument exists.
  int last_named_arg_index() const { return last_named_arg_index_; }

  // Checks the signature result type and argument types for unsupported
  // Types given 'language_options', and returns true if found.  This
  // function considers ProductMode, TimestampMode, and enabled features
  // that affect supported types.
  bool HasUnsupportedType(const LanguageOptions& language_options) const;

  // The context is a union where only one of context_ptr_ and context_id_
  // is valid.  The caller distinguishes which is valid based on the
  // Function group name.
  const void* context_ptr() const { return context_ptr_; }
  int64_t context_id() const { return context_id_; }

  bool IsConcrete() const { return is_concrete_; }

  // Returns TRUE if all arguments are concrete.
  bool HasConcreteArguments() const;

  // Determines whether the argument and result types are valid.  Additionally,
  // it requires that all repeated arguments are consecutive, and all optional
  // arguments appear at the end.  There may be required arguments before
  // the repeated arguments, and there may be required arguments between the
  // repeated and optional arguments.
  absl::Status IsValid(ProductMode product_mode) const;

  // Checks specific invariants for the argument and return types for regular
  // function calls or table-valued function calls. The latter may use relation
  // types (returning true for FunctionArgumentType::IsRelation()) but the
  // former may not.
  absl::Status IsValidForFunction() const;
  absl::Status IsValidForTableValuedFunction() const;

  // Checks if this signature is valid for Procedure.
  // Procedure may only have fixed required arguments.
  absl::Status IsValidForProcedure() const;

  // Gets the first or last repeated argument index.  If there are no repeated
  // arguments then returns -1.
  int FirstRepeatedArgumentIndex() const;
  int LastRepeatedArgumentIndex() const;

  // Gets the number of required, repeated or optional arguments.
  int NumRequiredArguments() const;
  int NumRepeatedArguments() const { return num_repeated_arguments_; }
  int NumOptionalArguments() const { return num_optional_arguments_; }

  // Returns whether or not the constraints are satisfied.
  // If <options_.constraints_> is NULL, returns true.
  absl::StatusOr<std::string> CheckArgumentConstraints(
      const std::vector<InputArgumentType>& arguments) const;

  // If verbose is true, include FunctionOptions modifiers.
  std::string DebugString(absl::string_view function_name = "",
                          bool verbose = false) const;

  // Returns a string containing the DebugString()s of all its
  // signatures.  Each signature string is prefixed with <prefix>, and
  // <separator> appears between each signature string.
  static std::string SignaturesToString(
      absl::Span<const FunctionSignature> signatures, bool verbose = false,
      absl::string_view prefix = "  ", absl::string_view separator = "\n");

  // Get the SQL declaration for this signature, including all options.
  // For each argument in the signature, the name will be taken from the
  // corresponding entry of <argument_names> if present.  An empty
  // <argument_names> will result in a signature with just type names.
  // The result is formatted as "(arg_name type, ...) RETURNS type", which
  // is valid to use in CREATE FUNCTION, DROP FUNCTION, etc, if possible.
  std::string GetSQLDeclaration(absl::Span<const std::string> argument_names,
                                ProductMode product_mode) const;

  // Same as thee above `GetSQLDeclaration`, but setting `use_external_float32`
  // to true will return FLOAT32 as the type name for TYPE_FLOAT in
  // PRODUCT_EXTERNAL mode.
  // TODO: Remove this signature once FLOAT32 is used by default
  // for PRODUCT_EXTERNAL.
  std::string GetSQLDeclaration(absl::Span<const std::string> argument_names,
                                ProductMode product_mode,
                                bool use_external_float32) const;

  bool IsDeprecated() const { return options_.is_deprecated(); }

  bool IsInternal() const { return options_.is_internal(); }

  bool IsHidden() const { return options_.is_hidden(); }

  void SetIsDeprecated(bool value) { options_.set_is_deprecated(value); }

  void SetArgumentConstraintsCallback(
      FunctionSignatureArgumentConstraintsCallback callback) {
    options_.set_constraints(std::move(callback));
  }

  const FunctionSignatureOptions& options() const { return options_; }

  const std::vector<FreestandingDeprecationWarning>&
  AdditionalDeprecationWarnings() const {
    return options_.additional_deprecation_warnings();
  }

  // Returns a non-const reference to the function signature options.
  FunctionSignatureOptions& mutable_options() { return options_; }

  void SetAdditionalDeprecationWarnings(
      const std::vector<FreestandingDeprecationWarning>& warnings) {
    options_.set_additional_deprecation_warnings(warnings);
  }

  void SetConcreteResultType(const Type* type);

  // Please use the overloaded function without original_kind. The original kind
  // should be automatically inferred from the signature itself.
  void SetConcreteResultType(const Type* type,
                             SignatureArgumentKind original_kind);

  // Returns true if this function signature contains any templated arguments.
  bool IsTemplated() const {
    for (const FunctionArgumentType& arg : arguments_) {
      if (arg.IsTemplated()) {
        return true;
      }
    }
    return false;
  }

  // Returns true if all the arguments in the signature have default values.
  // Note this function returns true when the signature has no arguments.
  bool AllArgumentsHaveDefaults() const {
    return absl::c_all_of(arguments(),
                          [](const FunctionArgumentType& arg_type) {
                            return arg_type.HasDefault();
                          });
  }

  // Returns true if a rewrite implementatation is configured for this
  // function signature (when `rewrite_options` has a value) and it is enabled.
  // See `FunctionSignatureRewriteOptions` for details.
  bool HasEnabledRewriteImplementation() const;

  // Returns true if this signature should be hidden in the supported signature
  // list in signature mismatch error message.
  // Signatures are hidden if they are internal, deprecated, explicitly hidden
  // or unsupported according to LanguageOptions.
  bool HideInSupportedSignatureList(
      const LanguageOptions& language_options) const;

  // Returns the list of arguments to be used in error messages by calling
  // FunctionArgumentType::UserFacingNameWithCardinality on each individual
  // argument of the signature.
  // If `print_template_details` is true, template arguments ARG_ANY_1/2 are
  // printed as T1/T2 rather than just as ANY.
  std::vector<std::string> GetArgumentsUserFacingTextWithCardinality(
      const LanguageOptions& language_options,
      FunctionArgumentType::NamePrintingStyle print_style,
      bool print_template_details = false) const;

  // Returns the custom callback to compute the result annotations. Returns
  // nullptr if the SQL function signature does not have a custom annotation
  // callback.
  const ComputeResultAnnotationsCallback& GetComputeResultAnnotationsCallback()
      const;

 private:
  void Init();

  absl::Status InitInternal();

  absl::Status CreateNamedArgumentToOptionsMap();

  bool ComputeIsConcrete() const;
  void ComputeConcreteArgumentTypes();

  int ComputeNumRepeatedArguments() const;
  int ComputeNumOptionalArguments() const;

  FunctionArgumentTypeList arguments_;
  FunctionArgumentType result_type_;
  int num_repeated_arguments_ = -1;
  int num_optional_arguments_ = -1;

  // Deferred errors that happened during the constructor.
  // See comments on init_status().
  absl::Status init_status_;

  // Map from argument name to its options.
  absl::flat_hash_map<std::string, const FunctionArgumentTypeOptions*,
                      googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      argument_name_to_options_;
  int last_arg_index_with_default_ = -1;
  int last_named_arg_index_ = -1;

  // This union should hold enough context for the implementation
  // to map a specific function signature back to an evaluator for the
  // function.  For GoogleSQL functions, the <context_id_> is relevant.
  // For non-GoogleSQL functions, the relevant field is
  // implementation-defined.
  union {
    void* context_ptr_;
    int64_t context_id_;
  };

  // Additional constraints on the signature for it to be valid for a
  // list of concrete input arguments.
  FunctionSignatureOptions options_;

  // We precompute and materialize the list of concrete arguments because
  // we end up asking for these repeatedly.  This vector could be large if
  // functions have huge numbers of arguments, but then we probably have other
  // data structures that are proportionally large too.
  bool is_concrete_ = false;
  FunctionArgumentTypeList concrete_arguments_;

  friend class FunctionSerializationTests;
  // Copyable.
};

// Returns true if the signature has an argument that supports argument aliases.
bool SignatureSupportsArgumentAliases(const FunctionSignature& signature);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTION_SIGNATURE_H_
