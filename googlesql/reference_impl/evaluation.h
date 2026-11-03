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

#ifndef GOOGLESQL_REFERENCE_IMPL_EVALUATION_H_
#define GOOGLESQL_REFERENCE_IMPL_EVALUATION_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/civil_time.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/case.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/map_util.h"

// See description in the cc file.
ABSL_DECLARE_FLAG(int64_t, googlesql_call_verify_not_aborted_rows_period);

namespace googlesql {

// Returns OK if the first column is the primary key column for a table named
// 'table_name' represented by 'array'. Specifically, the first column must meet
// the following criteria:
//   1. SupportsGrouping() is true for the type of the column
//   2. Does not have NULL Values
//   3. Does not have duplicate Values
absl::Status ValidateFirstColumnPrimaryKey(
    absl::string_view table_name, const Value& array,
    const LanguageOptions& language_options);

struct EvaluationOptions {
  // If true, operations will act as if the first column of a non-value table is
  // a primary key that may not be NULL. EvaluationContext::AddTable() will also
  // verify tables accordingly.
  // TODO: Migrate the compliance framework off this option and
  // remove this option.
  bool emulate_primary_keys = false;

  // If true, the reference implementation will deterministically scramble the
  // output of relations whose order is not defined by GoogleSQL. This requires
  // some extra processing and so is only appropriate for tests. See
  // ReorderingTupleIterator in tuple.h for details.
  bool scramble_undefined_orderings = false;

  // If true, the reference implementation will always perform stable sorting
  // where sorting is required. This is useful for obtaining deterministic
  // results in the (text-based) reference implementation compliance tests,
  // especially when running with different implementations of the C++ standard
  // library.
  bool always_use_stable_sort = false;

  // If true, the reference implementation will store proto field values in
  // TupleSlots (to avoid extra deserialization).
  bool store_proto_field_value_maps = false;

  // If true, the reference implementation will use the TopNAccumulator instead
  // of LimitAccumulator(OrderByAccumulator) when possible in order to save
  // memory. Not safe to use in compliance or random query tests because it
  // always says its results are ordered according to GoogleSQL semantics.
  bool use_top_n_accumulator_when_possible = false;

  // Limit on the maximum number of in-memory bytes used by values. Exceeding
  // this limit results in an error. See the implementation of
  // Value::physical_byte_size for more details.
  int64_t max_value_byte_size = 1024 * 1024;

  // The limit on the maximum number of in-memory bytes that can be used for
  // storing accumulated Tuples (e.g., during an ORDER BY query). Exceeding this
  // limit results in an error.
  int64_t max_intermediate_byte_size = 128 * 1024 * 1024;

  // If true, the results of DML statements will include all rows in the
  // modified table; otherwise, only modified rows (i.e. those matching the
  // WHERE clause) are included. For DELETE, 'modified rows' means the rows to
  // be deleted.
  //
  // Note that rows are considered modified even if the new row happens to be
  // the same as the old as long as they match the WHERE clause.
  bool return_all_rows_for_dml = true;

  // If true, the reference implementation will exhaust all input rows to an
  // EXISTS subquery even after seeing the first row, to flag non-determinism
  // if some subsequent row produces an error.
  bool return_early_from_exists_subquery = false;

  // If true, the reference implementation will return all rows for INSERT
  // IGNORE DML statements, including the rows that were not inserted.
  bool return_all_insert_rows_insert_ignore_dml = false;

  // If true, the reference implementation will account for the memory used by
  // function arguments and thus limit the cumulative memory used by function
  // arguments to the value of 'max_intermediate_byte_size'.
  bool enable_function_args_memory_accounting = false;
};

class ProtoFieldReader;

// Base class for C++ values which can be associated with a variable.
class CppValueBase {
 public:
  virtual ~CppValueBase() = default;
};

// Contains state about the evaluation in progress.
class EvaluationContext {
 public:
  // Objects can register CancelCallbacks with RegisterCancelCallback() to be
  // notified when CancelStatement() is called. A CancelCallback must not block
  // for a long time.
  using CancelCallback = std::function<absl::Status()>;

  explicit EvaluationContext(const EvaluationOptions& options);
  EvaluationContext(const EvaluationContext&) = delete;
  EvaluationContext& operator=(const EvaluationContext&) = delete;

  // Creates a local evaluator that inherits statement level properties
  // from the parent evaluator. Useful for executing SQL defined
  // function bodies which require their own scope but should share some
  // global execution state.
  // The local evaluator keeps a reference to the parent, and will
  // propagate non-determinism if encountered
  // The parent context must outlive any child contexts it creates.
  std::unique_ptr<EvaluationContext> MakeChildContext() const;

  const EvaluationOptions& options() const { return options_; }

  MemoryAccountant* memory_accountant() { return memory_accountant_.get(); }

  // Returns the `value` associated with `arg_name` or an invalid Value.
  Value GetFunctionArgumentRef(std::string arg_name);
  // Makes the given `value` accessible under `arg_name`. Fails if there is
  // already an existing key `arg_name` in the underlying map. This is used
  // in the execution of user defined entities.
  absl::Status AddFunctionArgumentRef(std::string arg_name, Value value);

  // Returns the contents of table 'table_name' or Value::Invalid().
  Value GetTableAsArray(absl::string_view table_name) {
    const auto it = tables_.find(table_name);
    if (it != tables_.end()) {
      return it->second;
    }
    return Value();
  }

  // Makes the given 'array' accessible under 'table_name'.
  absl::Status AddTableAsArray(absl::string_view table_name,
                               bool is_value_table, Value array,
                               const LanguageOptions& language_options);

  // Updates the table 'table_name' with a new array.
  absl::Status UpdateTableAsArray(absl::string_view table_name, Value array);

  // Increment the number of rows modified for a table.
  void IncrementNumRowsModified(absl::string_view table_name, int64_t count);

  // Get the number of rows modified for a table.
  int64_t GetNumRowsModified(absl::string_view table_name) const;

  // Clear all row modification counts.
  void ClearNumRowsModified();

  // Indicates that the result of evaluation is non-deterministic.
  // If this context has a non-null parent context, then we also set
  // non-determinism on the parent context.
  void SetNonDeterministicOutput();
  bool IsDeterministicOutput() const { return deterministic_output_; }

  void SetLanguageOptions(LanguageOptions options) {
    language_options_ = std::move(options);
  }
  const LanguageOptions& GetLanguageOptions() const {
    return language_options_;
  }

  // Also clears the current timestamp (to force us to lazily regenerate the
  // current timestamp in the default timezone). This should not be called after
  // getting the current timestamp unless the user is using a frozen Clock.
  void SetDefaultTimeZone(absl::TimeZone timezone) {
    default_timezone_ = timezone;
    // Clear current_timestamp_ to force us to regenerate
    // *_in_default_timezone_.
    current_timestamp_.reset();
  }

  // If necessary, (lazily) initializes the default timezone. Lazy
  // initialization saves time for most evaluations, which don't require time
  // zone information.
  absl::TimeZone GetDefaultTimeZone();

  // If necessary, (lazily) initializes the random number generator. Lazy
  // initialization saves time for most evaluations, which don't require random
  // numbers.
  absl::BitGen* GetRandomNumberGenerator() {
    if (!rand_.has_value()) {
      rand_.emplace();
    }
    return &rand_.value();
  }

  // Sets the clock to use when evaluating CURRENT_TIMESTAMP(),
  // CURRENT_DATE(), CURRENT_DATETIME(), etc functions.
  // Units are microseconds since the unix epoch UTC.
  // The GoogleSQL spec requires that all timestamps are within the range of
  // years [1, 9999].
  void SetClockAndClearCurrentTimestamp(absl::Clock* clock) {
    clock_ = clock;
    current_timestamp_.reset();
  }

  // If necessary, (lazily) initializes the default timezone and the current
  // timestamp. Lazy initialization saves time for most evaluations, which don't
  // require the current timestamp.
  int64_t GetCurrentTimestamp() {
    LazilyInitializeCurrentTimestamp();
    return current_timestamp_.value();
  }

  // If necessary, (lazily) initializes the default timezone and the current
  // timestamp. Lazy initialization saves time for most evaluations, which don't
  // require the current timestamp.
  int64_t GetCurrentDateInDefaultTimezone() {
    LazilyInitializeCurrentTimestamp();
    return current_date_in_default_timezone_;
  }

  // If necessary, (lazily) initializes the default timezone and the current
  // timestamp. Lazy initialization saves time for most evaluations, which don't
  // require the current timestamp.
  DatetimeValue GetCurrentDatetimeInDefaultTimezone() {
    LazilyInitializeCurrentTimestamp();
    return current_datetime_in_default_timezone_;
  }

  // If necessary, (lazily) initializes the default timezone and the current
  // timestamp. Lazy initialization saves time for most evaluations, which don't
  // require the current timestamp.
  TimeValue GetCurrentTimeInDefaultTimezone() {
    LazilyInitializeCurrentTimestamp();
    return current_time_in_default_timezone_;
  }

  // Sets the statement evaluation deadline of some time duration from now.  If
  // the statement is still being evaluated after that time it will be aborted
  // and an error will be returned.
  void SetStatementEvaluationDeadlineFromNow(absl::Duration time_limit) {
    statement_eval_deadline_ =
        absl::Clock::GetRealClock().TimeNow() + time_limit;
  }

  // Sets the statement evaluation deadline.
  // If the statement is still being evaluated after that time it will be
  // aborted and an error will be returned.
  void SetStatementEvaluationDeadline(absl::Time statement_deadline) {
    statement_eval_deadline_ = statement_deadline;
  }

  void SetSessionUser(absl::string_view session_user) {
    session_user_ = session_user;
  }

  // Gets the session user string from session_user_.
  absl::string_view GetSessionUser() { return session_user_; }

  absl::Time GetStatementEvaluationDeadline() const {
    return statement_eval_deadline_;
  }

  // Register a callback to be notified when CancelStatement() is called. As
  // with all non-const methods in this thread compatible class, using this
  // method in a multithreaded setting requires external synchronization of
  // this object. In practice, this is called by iterators that need to
  // propagate the cancellation request to user code.
  void RegisterCancelCallback(const CancelCallback& cb) {
    cancel_cbs_.push_back(cb);
  }

  // Cancels the current statement and invokes all of the cancellation
  // callbacks. Cancellation support is best-effort, in that iterators should be
  // periodicially polling the cancellation state (by calling VerifyNotAborted)
  // and cancelling if they discover the statement has been cancelled. The
  // callbacks are just a way of notifying user code that the statement has been
  // cancelled if we are stuck in a user's EvaluatorTableIterator.
  absl::Status CancelStatement() {
    cancelled_ = true;
    // Call all the callbacks, returning the first non-OK error code.
    absl::Status ret = absl::OkStatus();
    for (const CancelCallback& cb : cancel_cbs_) {
      absl::Status status = cb();
      if (ret.ok() && !status.ok()) {
        ret = status;
      }
    }
    return ret;
  }

  // Reset the deadline to infinity, uncancel the statement, and clear the
  // cancellation callbacks.
  void ClearDeadlineAndCancellationState() {
    SetStatementEvaluationDeadline(absl::InfiniteFuture());
    cancelled_ = false;
    cancel_cbs_.clear();
  }

  // Returns an error if the statement has been aborted. This function is
  // expensive (it gets the current time).
  absl::Status VerifyNotAborted() const;

  int num_proto_deserializations() const { return num_proto_deserializations_; }

  void set_num_proto_deserializations(int n) {
    num_proto_deserializations_ = n;
  }

  bool used_top_n_accumulator() const { return used_top_n_accumulator_; }

  void set_used_top_n_accumulator(bool value) {
    used_top_n_accumulator_ = value;
  }

  void set_populate_last_get_field_value_call_read_fields_from_proto_map(
      bool value) {
    populate_last_get_field_value_call_read_fields_from_proto_map_ = value;
  }

  bool last_get_field_value_call_read_fields_from_proto(
      const ProtoFieldReader* reader) const {
    return googlesql_base::FindWithDefault(
        last_get_field_value_call_read_fields_from_proto_map_, reader);
  }

  void set_last_get_field_value_call_read_fields_from_proto(
      const ProtoFieldReader* reader, bool value) {
    // Only populate in unit tests for performance reasons.
    if (populate_last_get_field_value_call_read_fields_from_proto_map_) {
      last_get_field_value_call_read_fields_from_proto_map_[reader] = value;
    }
  }

  // Retrieves the C++ value associated with the given VariableId, or nullptr
  // if not value with the given VariableId exists.
  //
  // The returned pointer is owned by the EvaluationContext and destroyed when
  // the same variable is passed to ClearCppValue() or
  // SetCppValueIfNotPresent().
  //
  // Ideally, these values would be passed to ValueExpr::Eval() and
  // RelationalOp::CreateIterator(), alongside the TupleData's. However, the
  // refactoring required to alter the signatures of these two methods was too
  // much, so we store VariableId's C++ values instead, using the TupleData's
  // only for Tuple values.
  CppValueBase* GetCppValue(VariableId variable) const {
    auto it = cpp_values_.find(variable);
    return it == cpp_values_.end() ? nullptr : it->second.get();
  }

  // Sets the C++ value associated with the given variable ID, taking ownership
  // of the provided object. Returns true if successful, false if the given
  // VariableId already has a value.
  ABSL_MUST_USE_RESULT bool SetCppValueIfNotPresent(
      VariableId variable, std::unique_ptr<CppValueBase> value) {
    return cpp_values_.insert(std::make_pair(variable, std::move(value)))
        .second;
  }

  // Deletes the C++ value associated with the given variable Id.
  void ClearCppValue(VariableId variable) { cpp_values_.erase(variable); }

  // Stores the accumulated input rows for the UDA currently being evaluated
  // in this context. Set by UserDefinedAggregateFunctionEvaluator and consumed
  // by RowsForUdaOp.
  const TupleDataDeque* uda_input_rows() const { return uda_input_rows_; }
  void set_uda_input_rows(const TupleDataDeque* uda_input_rows) {
    uda_input_rows_ = uda_input_rows;
  }

  // Registers accumulated group rows to context.
  absl::Status RegisterGroupRows(const VariableId& id,
                                 const TupleDataDeque* rows) {
    bool inserted = accumulated_group_rows_map_.insert({id, rows}).second;
    GOOGLESQL_RET_CHECK(inserted) << "Variable id " << id
                        << " already in accumulated_group_rows_map_";
    return absl::OkStatus();
  }

  // Unregisters accumulated group rows to context.
  absl::Status UnregisterGroupRows(const VariableId& id) {
    GOOGLESQL_RET_CHECK(accumulated_group_rows_map_.erase(id))
        << "Variable " << id << " not found in accumulated_group_rows_map_";
    return absl::OkStatus();
  }

  // Lookup accumulated group rows from context.
  const TupleDataDeque* GetGroupRows(const VariableId& id) const {
    auto it = accumulated_group_rows_map_.find(id);
    if (it != accumulated_group_rows_map_.end()) {
      return it->second;
    }
    return nullptr;
  }

 private:
  EvaluationContext(const EvaluationOptions& options,
                    std::shared_ptr<MemoryAccountant> memory_accountant,
                    EvaluationContext* parent_context);

  void LazilyInitializeDefaultTimeZone() {
    if (!default_timezone_.has_value()) {
      InitializeDefaultTimeZone();
    }
  }

  void InitializeDefaultTimeZone();

  void LazilyInitializeCurrentTimestamp() {
    if (!current_timestamp_.has_value()) {
      InitializeCurrentTimestamp();
    }
  }

  void InitializeCurrentTimestamp();

  const EvaluationOptions options_;
  std::shared_ptr<MemoryAccountant> memory_accountant_;
  // Tables added by AddTableAsArray().
  std::map<std::string, Value, std::less<>> tables_;
  absl::flat_hash_map<std::string, int64_t> num_rows_modified_;

  const TupleDataDeque* uda_input_rows_ = nullptr;
  // Indicates that the result of evaluation is non-deterministic.
  bool deterministic_output_;
  LanguageOptions language_options_;
  // Default is no deadline.
  absl::Time statement_eval_deadline_ = absl::InfiniteFuture();
  bool cancelled_ = false;
  std::vector<CancelCallback> cancel_cbs_;

  // Used to obtain the current timestamp.
  absl::Clock* clock_ = &absl::Clock::GetRealClock();

  // Lazily initialized because not all queries need them, and initializing them
  // is expensive.
  std::optional<absl::TimeZone> default_timezone_;
  std::optional<int64_t> current_timestamp_;
  std::optional<absl::BitGen> rand_;
  // Only valid if 'current_timestamp_' has a value.
  int32_t current_date_in_default_timezone_;
  DatetimeValue current_datetime_in_default_timezone_;
  TimeValue current_time_in_default_timezone_;

  // Records the number of times a proto was deserialized. Only for unit tests.
  int num_proto_deserializations_ = 0;

  // Whether to populate
  // 'last_get_field_value_call_read_fields_from_proto_map_'. For performance
  // reasons, this is only set to true in unit tests.
  bool populate_last_get_field_value_call_read_fields_from_proto_map_ = false;

  // Maps a ProtoFieldReader to whether the last call to its GetFieldValue()
  // method read fields from the proto. Only for unit tests.
  absl::flat_hash_map<const ProtoFieldReader*, bool>
      last_get_field_value_call_read_fields_from_proto_map_;

  // Records whether a TopNAccumulator was used. Only for unit tests.
  bool used_top_n_accumulator_ = false;

  // Current C++ values associated with variables.
  absl::flat_hash_map<VariableId, std::unique_ptr<CppValueBase>> cpp_values_;

  // Maps variable IDs to their corresponding accumulated group rows. This
  // registry ensures that nested GROUP ROWS subqueries can independently locate
  // their own active grouping context.
  absl::flat_hash_map<VariableId, const TupleDataDeque*>
      accumulated_group_rows_map_;

  // The current user, specified by the engine. Used to evaluate the
  // SESSION_USER function. Defaults to an empty string if not set.
  std::string session_user_ = "";

  // A reference to the EvaluationContext that created this context by
  // calling `MakeChildContext`. Always nullptr if not created this way.
  EvaluationContext* parent_context_ = nullptr;

  // UDF argument references
  std::map<std::string, Value, googlesql_base::CaseLess>
      udf_argument_references_;
};

// Returns true if we should suppress 'error' (which must not be OK) in
// 'error_mode'.
bool ShouldSuppressError(const absl::Status& error,
                         ResolvedFunctionCallBase::ErrorMode error_mode);

// Returns ResourceExhausted error when the statement should be aborted.
absl::Status PeriodicallyVerifyNotAborted(EvaluationContext* context,
                                          uint64_t num_steps);

// Recursively replaces collated strings in the value with their collation keys.
// The output type is the same as the input type, except that **collated**
// STRINGs are replaced with BYTESs. Not all strings, e.g.
//   $CollateKey(STRUCT<a STRING, b STRING COLLATE 'und:ci'>) will return a
//   key whose type is STRUCT<a STRING, b BYTES>>.
absl::StatusOr<Value> ReplaceStringsWithCollationKeys(
    const Value& value, const Type* output_type,
    const GoogleSqlCollator* collator);

// Bag-equality (multiset equality) compares two collections such that element
// multiplicity matters, but element order does not. For example, [1, 2] and
// [2, 1] are bag-equal, but [1, 2] and [1, 2, 2] are not. In GoogleSQL,
// arrays with `kIgnoresOrder` (e.g. produced by an ARRAY subquery without an
// explicit ORDER BY) are compared using bag equality under
// `InternalValue::Equals`.

// Returns true if `v1` and `v2` are equal using bag equality for any nested
// unordered arrays (`kIgnoresOrder`), and at least one value contains an array
// with uncertain order (an unordered array with >= 2 distinct elements).
//
// In GoogleSQL MAP operations, keys are compared using strict ordered array
// equality. When keys contain unordered arrays (e.g., from an ARRAY subquery
// without ORDER BY), their physical element order is arbitrary. If two keys
// match under bag equality but contain uncertainly-ordered arrays (for example,
// v1 = [1, 2] (kIgnoresOrder) and v2 = [2, 1]), their strict equality is
// non-deterministic because they can match under some element orderings but
// not others.
bool AreValuesBagEquatable(const Value& v1, const Value& v2);

// Returns true if `map_entries` contains any pair of keys that are
// bag-equatable with uncertain array order, which would make duplicate-key
// collision non-deterministic.
//
// Takes `map_entries` (pairs of key and value) directly to inspect map key
// collisions prior to calling `Value::MakeMap` without allocating an
// intermediate vector to extract keys.
bool HasBagEquatableKeyCollision(
    absl::Span<const std::pair<Value, Value>> map_entries);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_EVALUATION_H_
