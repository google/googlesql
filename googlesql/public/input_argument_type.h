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

#ifndef GOOGLESQL_PUBLIC_INPUT_ARGUMENT_TYPE_H_
#define GOOGLESQL_PUBLIC_INPUT_ARGUMENT_TYPE_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

class TVFConnectionArgument;
class TVFModelArgument;
class TVFRelation;

// Attributes of a function argument when a function is actually called.
// Identifies the argument's data type and its literal value if appropriate.
//
// This is mainly an internal class used while resolving function calls.
// This is exposed publicly because some of the FunctionOptions callbacks
// use it to customize resolution for particular Functions.
//
// To optimize size, some less frequently used fields are stored in an internal
// heap-allocated struct (`Data`).
//
// This class is copyable and assignable.
class InputArgumentType {
 public:
  // Same as InputArgumentType::UntypedNull(). Consider using the latter.
  InputArgumentType() : type_(types::Int64Type()), category_(kUntypedNull) {}

  // Constructor for literal arguments without an explicit type.
  // <literal_value> cannot be nullptr.
  // A Value can be either a NULL or non-NULL Value of any GoogleSQL Type.
  // The <literal_value> is not owned and must outlive all referencing
  // InputArgumentTypes. A true <is_default_argument_value> indicates that the
  // related function call argument was unspecified by the user, and
  // <literal_value> was injected as the default value for the unspecified
  // argument.
  explicit InputArgumentType(const Value& literal_value,
                             bool is_default_argument_value = false);

  // Constructor for query parameters, non-literals, and literals with
  // an explicit type.
  // WARNING: `is_literal()` will return false for any InputArgumentTypes that
  // use this constructor, including literals with an explicit type.
  // `is_literal_for_constness` should be used to provide a separate signal
  // for whether the argument is a literal for the sake of constness, and can
  // be checked with `is_literal_for_constness()`.
  explicit InputArgumentType(const Type* type, bool is_query_parameter = false,
                             bool is_literal_for_constness = false);

  // Constructor for STRUCT arguments that can have a mix of literal and
  // non-literal fields.  If a STRUCT argument is a literal or has all
  // non-literal fields, then the previous constructors can be used.
  InputArgumentType(const StructType* type,
                    const std::vector<InputArgumentType>& field_types)
      : type_(type), category_(kTypedExpression) {
    EnsureData().field_types = field_types;
  }

  // Constructor for analysis time constant expressions.
  // It takes a StatusOr<Value> because the constant evaluator that produces the
  // value of the expression may throw an error, but we want to defer the error
  // handling to when the constant value of the argument is used.
  // REQUIRES: `constant_value` does not contain fatal error (kInternal or
  // kResourceExhausted) or runtime error (kOutOfRange).
  explicit InputArgumentType(absl::StatusOr<Value> constant_value);

  InputArgumentType(const InputArgumentType& other);
  InputArgumentType& operator=(const InputArgumentType& other);

  ~InputArgumentType() = default;

  // This may return nullptr (such as for lambda).
  const Type* type() const { return type_; }

  const std::vector<InputArgumentType>& field_types() const;
  size_t field_types_size() const;
  const InputArgumentType& field_type(int i) const;

  // Returns true if the argument is an analysis time constant.
  bool is_analysis_time_constant() const {
    return ReadOnlyData().constant_value.has_value();
  }

  // Returns the analysis time constant value.
  // REQUIRES: is_analysis_time_constant() returns true.
  absl::StatusOr<Value> GetAnalysisTimeConstantValue() const;

  // Check for or get a literal value.
  // WARNING: Untyped literals ("NULL" or "[]") are not included here.
  // They must be checked for separately using is_untyped() methods.
  const Value* literal_value() const {
    return ReadOnlyData().literal_value.has_value()
               ? &ReadOnlyData().literal_value.value()
               : nullptr;
  }
  // Returns true if the argument was a ResolvedLiteral with
  // `has_explicit_type` set to false.
  // WARNING: This function may return false for explicitly typed
  // ResolvedLiterals such as those of the form DATE `2024-01-01`.
  // Other explicitly typed literals of this form could include but are not
  // limited to JSON, FLOAT, NUMERIC, TIMESTAMP, and RANGE literals.
  bool is_literal() const { return ReadOnlyData().literal_value.has_value(); }
  // Returns true if the argument was a ResolvedLiteral. This can produce
  // different results compared with `is_literal` above as it does not require
  // that a Value was supplied to InputArgumentType::literal_value_.
  bool is_literal_for_constness() const { return is_literal_for_constness_; }
  bool is_literal_null() const {
    return ReadOnlyData().literal_value.has_value() &&
           ReadOnlyData().literal_value.value().is_null();
  }
  bool is_literal_empty_array() const {
    return ReadOnlyData().literal_value.has_value() &&
           ReadOnlyData().literal_value.value().is_empty_array();
  }

  // Check for an untyped literal value ("NULL" or "[]").
  // WARNING: These are distinct argument types not included in is_literal().
  bool is_untyped() const {
    return category_ == kUntypedNull || category_ == kUntypedParameter ||
           category_ == kUntypedEmptyArray;
  }
  bool is_untyped_null() const { return category_ == kUntypedNull; }
  bool is_untyped_empty_array() const {
    return category_ == kUntypedEmptyArray;
  }

  // Check for either literal (typed) or untyped NULL or [].
  bool is_null() const { return is_literal_null() || is_untyped_null(); }
  bool is_empty_array() const {
    return is_literal_empty_array() || is_untyped_empty_array();
  }

  // Indicates whether or not the argument is a SQL query parameter, whose
  // value is unknown at resolution time but which is constant for the
  // query execution.  Some functions require one or more arguments to
  // be either a literal or parameter (for example, the separator argument
  // to STRING_AGG).
  bool is_query_parameter() const {
    return category_ == kTypedParameter || category_ == kUntypedParameter;
  }

  bool is_untyped_query_parameter() const {
    return category_ == kUntypedParameter;
  }
  bool is_relation() const { return category_ == kRelation; }
  bool is_model() const { return category_ == kModel; }
  bool is_connection() const { return category_ == kConnection; }
  bool is_lambda() const { return category_ == kLambda; }
  bool is_sequence() const { return category_ == kSequence; }
  bool is_descriptor() const { return category_ == kDescriptor; }
  bool is_graph() const { return category_ == kGraph; }

  bool is_default_argument_value() const { return is_default_argument_value_; }

  std::optional<IdString> argument_alias() const {
    return ReadOnlyData().argument_alias;
  }

  void set_argument_alias(IdString argument_alias) {
    EnsureData().argument_alias = argument_alias;
  }

  // Argument type name to be used in user facing text (i.e. error messages).
  std::string UserFacingName(ProductMode product_mode) const;

  // If <verbose>, then NULLs are identified as untyped if appropriate,
  // and parameters are identified.
  // External error messaging should not be verbose.
  std::string DebugString(bool verbose = false) const;

  // Returns a comma-separated string in vector order.  If <verbose>, then
  // the generated string for each argument is also verbose. If `argument_names`
  // is provided, any arguments with non-empty names will be printed as in the
  // function signature, like NAME => TYPE. If `argument_names` is shorter
  // than `arguments`, any arguments past the end of `argument_names` are
  // printed with type only.
  //
  // TODO: Separate ArgumentsToString into a debug string function,
  // which is what many callers want, and a function for generating diagnostics.
  static std::string ArgumentsToString(
      absl::Span<const InputArgumentType> arguments,
      ProductMode product_mode = PRODUCT_INTERNAL,
      absl::Span<const absl::string_view> argument_names = {});

  // Represents an argument type for untyped NULL that is coercible to any other
  // type. By convention, <type_> defaults to INT64.
  static InputArgumentType UntypedNull() {
    return InputArgumentType(kUntypedNull, types::Int64Type());
  }

  // Represents an argument type for untyped empty array that is coercible to
  // any other array type. By convention, <type_> defaults to ARRAY<INT64>.
  static InputArgumentType UntypedEmptyArray() {
    return InputArgumentType(kUntypedEmptyArray, types::Int64ArrayType());
  }

  // Represents an argument type for untyped query parameters that is coercible
  // to any other type. <type_> defaults to INT64.
  static InputArgumentType UntypedQueryParameter() {
    return InputArgumentType(kUntypedParameter, types::Int64Type());
  }

  // Constructor for relation arguments. Only for use when analyzing
  // table-valued functions. For more information, see table_valued_function.h.
  // 'relation_input_schema' specifies the schema for the provided input
  // relation when function is called.
  // 'is_pipe_input_table' indicates a TVF argument that came from the
  // pipe input in pipe CALL. This is currently used only for error messages.
  static InputArgumentType RelationInputArgumentType(
      const TVFRelation& relation_input_schema,
      bool is_pipe_input_table = false);

  // Constructor for model arguments. Only for use when analyzing
  // table-valued functions. 'model_arg' specifies the model object for the
  // provided input. For more information about model argument,
  // see table_valued_function.h.
  static InputArgumentType ModelInputArgumentType(
      const TVFModelArgument& model_arg);

  // Constructor for connection arguments. Only for use when analyzing
  // table-valued functions. 'connection_arg' specifies the connection object
  // for the provided input. For more information about connection argument, see
  // table_valued_function.h.
  static InputArgumentType ConnectionInputArgumentType(
      const TVFConnectionArgument& connection_arg);

  // Constructor for descriptor argument. Only for use when analyzing
  // table-valued functions. For more information about descriptor
  // argument, see table_valued_function.h.
  static InputArgumentType DescriptorInputArgumentType();

  // Constructor for graph argument. Only for use when analyzing
  // table-valued functions. For more information about graph
  // argument, see table_valued_function.h.
  static InputArgumentType GraphInputArgumentType();

  // Constructor for lambda arguments. Only for use when analyzing lambda
  // arguments.
  static InputArgumentType LambdaInputArgumentType();

  // Constructor for Sequence arguments.
  static InputArgumentType SequenceInputArgumentType();

  bool has_relation_input_schema() const {
    return ReadOnlyData().relation_input_schema != nullptr;
  }
  const TVFRelation& relation_input_schema() const {
    ABSL_DCHECK(has_relation_input_schema());
    return *ReadOnlyData().relation_input_schema;
  }
  bool is_pipe_input_table() const { return is_pipe_input_table_; }

  bool is_chained_function_call_input() const {
    return is_chained_function_call_input_;
  }
  void set_is_chained_function_call_input() {
    is_chained_function_call_input_ = true;
  }

  // Determines equality/inequality of two InputArgumentTypes, considering Type
  // equality via Type::Equals() and whether they are literal or NULL.
  // The comparison between non-null literal InputArgumentTypes does not
  // consider their Values so they are considered equal here.
  bool operator==(const InputArgumentType& type) const;

 private:
  enum Category : uint8_t {
    kTypedExpression,  // non-literal, non-parameter
    kTypedLiteral,
    kTypedParameter,
    kUntypedParameter,
    kUntypedNull,
    kUntypedEmptyArray,
    kRelation,
    kModel,
    kConnection,
    kDescriptor,
    kLambda,
    kSequence,
    kGraph,
  };

  explicit InputArgumentType(Category category, const Type* type)
      : type_(type), category_(category) {}

  struct Data {
    // TODO: b/277365877 - Deprecate this in favor of `constant_value`.
    std::optional<Value> literal_value;  // only set for kTypedLiteral.

    // Analysis time constant value.
    // When this is set for kTypedExpression, it means that this is a named
    // constant.
    std::optional<absl::StatusOr<Value>> constant_value;

    // Populated only for STRUCT type arguments. Stores the InputArgumentType
    // of the struct fields (in the same order). We need this for STRUCT
    // coercion where we need to check field-by-field whether 'from_struct'
    // field is castable/coercible to the corresponding 'to_struct' field
    // type.
    std::vector<InputArgumentType> field_types;

    // This is only non-NULL for table-valued functions. It holds a list of
    // provided column names and types for a relation argument. This is a
    // shared pointer only because the InputArgumentType is copyable and there
    // is only need for one TVFRelation instance to exist.
    std::shared_ptr<const TVFRelation> relation_input_schema;

    // This is only non-NULL for table-valued functions. It holds the model
    // argument. This is a shared pointer only because the InputArgumentType
    // is copyable and there is only need for one TVFModelArgument instance to
    // exist.
    std::shared_ptr<const TVFModelArgument> model_arg;

    // This is only non-NULL for table-valued functions. It holds the
    // connection argument. This is a shared pointer only because the
    // InputArgumentType is copyable and there is only need for one
    // TVFConnectionArgument instance to exist.
    std::shared_ptr<const TVFConnectionArgument> connection_arg;

    // The alias of the argument this InputArgumentType corresponds to. If the
    // argument does not support aliases, `argument_alias` = std::nullopt.
    std::optional<IdString> argument_alias;
  };

  // Mutating functions must use this function to ensure that `data_` is
  // initialized.
  Data& EnsureData() {
    if (!data_) {
      data_ = std::make_unique<Data>();
    }
    return *data_;
  }

  // Returns a reference to the Data struct if it has been initialized,
  // otherwise returns a reference to a default Data struct.
  const Data& ReadOnlyData() const;

  // Note: type_ is filled in by use of the default constructor under
  // factory methods for categories that shouldn't have types.
  // This would ideally be fixed but some function resolving code
  // currently looks at types and fails if they aren't present.
  const Type* type_ = nullptr;

  std::unique_ptr<Data> data_;

  Category category_;

  // True if this InputArgumentType was constructed from a default function
  // argument value.
  bool is_default_argument_value_ : 1 = false;

  // Indicates a TVF argument that came from the pipe input in pipe CALL.
  // This is currently used only for error messages.
  bool is_pipe_input_table_ : 1 = false;

  // Indicates this argument is the base expression for a chained function call.
  // This is currently used only for error messages.
  bool is_chained_function_call_input_ : 1 = false;

  // True if the InputArgumentType was constructed from a ResolvedLiteral.
  // This assessment is independent of whether or not
  // `literal_value_` has a value.
  bool is_literal_for_constness_ : 1 = false;
};

// Only hashes the type kind, not the type itself (so two different enums will
// hash the same).  WARNING - this could perform poorly if there are many
// different arguments of the same non-simple type kind (ENUM, PROTO, ARRAY,
// or STRUCT).  This is potentially possible for instance for a large IN-list
// of one of these types (though ENUM would be currently ok).  TODO:
// Address this if/when we support IN-list of non-simple types, which may
// require updating the InputArgumentType::operator==() function.
struct InputArgumentTypeLossyHasher {
  size_t operator()(const InputArgumentType& type) const {
    return absl::HashOf(type.type() != nullptr ? type.type()->kind() : -2,
                        type.is_literal(), type.is_untyped(),
                        type.is_query_parameter(), type.is_literal_null());
  }
};

// Less function for InputArgumentType.  It is currently used to provide
// deterministic ordering for a set of InputArgumentTypes.  Arguments of the
// same TypeKind sort together, and within a TypeKind the non-literals sort
// before literals, which sort before nulls.
struct InputArgumentTypeLess {
  bool operator()(const InputArgumentType& type1,
                  const InputArgumentType& type2) const;
};

// Set container for InputArgumentType which has a special property where we
// can fetch the dominant argument.
//
// The dominant argument is the single argument that most determines the
// supertype.
// 1. If an argument is Inserted with <set_dominant> then that argument
//    is set as dominant.
// 2. Otherwise, if any non-simple typed arguments (other than an untyped
//    empty array) exist then the first non-simple-typed argument is dominant.
// 3. Otherwise, the first argument that isn't an untyped NULL or empty
//    array is dominant.
// 4. Otherwise, there is no dominant argument.
// The dominant argument will always be the first argument of its class so that
// supertyping prefers the type and field names from the leftmost argument.
class InputArgumentTypeSet {
 public:
  InputArgumentTypeSet() = default;
  InputArgumentTypeSet(const InputArgumentTypeSet&) = delete;
  InputArgumentTypeSet& operator=(const InputArgumentTypeSet&) = delete;
  ~InputArgumentTypeSet() = default;

  const InputArgumentType* dominant_argument() const {
    return dominant_argument_.get();
  }

  // Return the set of unique InputArgumentTypes.
  // Order is arbitrary.
  const std::vector<InputArgumentType>& arguments() const {
    return arguments_vector_;
  }

  bool empty() const { return arguments_vector_.empty(); }

  // Insert a new InputArgumentType, updating <dominant_argument_> if
  // appropriate.  <set_dominant> will force the argument to be dominant.
  // Returns true if argument was inserted; false means it was already present.
  bool Insert(const InputArgumentType& argument, bool set_dominant = false);

  void clear();

  // Returns a comma-separated string surrounded by {}, where the arguments
  // are ordered by InputArgumentTypeLess.  If <verbose>, then untyped NULL
  // and parameter arguments are identified as such.
  std::string ToString(bool verbose = false) const;

 private:
  // We store the set of unique arguments in a vector rather than a hash_set
  // so we can iterate over it quickly, and clear it quickly.
  // Many callers call arguments() just so they can iterate through them,
  // and this is very slow in a hash_set.
  // Using a vector also ensures order is preserved when these types are
  // reported in error messages.
  std::vector<InputArgumentType> arguments_vector_;

  // If the set of arguments gets large, we'll also start storing a hash_set of
  // arguments so we can do membership check in Insert in sub-linear time.
  const int kMaxSizeBeforeMakingHashSet = 4;
  using ArgumentsHashSet =
      absl::flat_hash_set<InputArgumentType, InputArgumentTypeLossyHasher>;
  std::unique_ptr<ArgumentsHashSet> arguments_set_;

  // The dominant argument, according to the definition above.
  std::unique_ptr<const InputArgumentType> dominant_argument_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_INPUT_ARGUMENT_TYPE_H_
