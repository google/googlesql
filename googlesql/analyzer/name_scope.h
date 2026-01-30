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

#ifndef GOOGLESQL_ANALYZER_NAME_SCOPE_H_
#define GOOGLESQL_ANALYZER_NAME_SCOPE_H_

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/common/reflection.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gtest/gtest_prod.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

class ASTIdentifier;
class ASTNode;
class ASTPathExpression;
class NameList;

// Forward declaration.
struct ExprResolutionInfo;

using NameListPtr = std::shared_ptr<const NameList>;

// The set of ResolvedColumns referenced in a particular subquery that resolved
// as correlated references to values from a parent NameScope.
// The bool value is true if the reference is to a column from more than one
// enclosing NameScope away; i.e. a column that was already a correlated
// reference in the enclosing query.
using CorrelatedColumnsSet = std::map<ResolvedColumn, bool>;

// A list of the CorrelatedColumnsSets attached to all NameScopes traversed
// while looking up a name.  The sets are ordered from child scopes to
// parent scopes; i.e. the outermost query's NameScope is last.
using CorrelatedColumnsSetList = std::vector<CorrelatedColumnsSet*>;

// Identifies valid name path (i.e., 'a.b.c') that resolves to the
// specified ResolvedColumn target.
// This class by itself doesn't have much meaning, but it gets attached
// to NameTarget objects to identify that a path
// starting at a source column or target is valid to access and resolves
// to 'target_column_'.
class ValidNamePath {
 public:
  ValidNamePath(const std::vector<IdString>& name_path,
                const ResolvedColumn& target_column)
      : name_path_(name_path), target_column_(target_column) {}

  ValidNamePath() = default;
  ~ValidNamePath() = default;

  const std::vector<IdString>& name_path() const { return name_path_; }
  std::vector<IdString>* mutable_name_path() { return &name_path_; }
  void set_name_path(const std::vector<IdString>& name_path) {
    name_path_ = name_path;
  }

  const ResolvedColumn& target_column() const { return target_column_; }
  void set_target_column(const ResolvedColumn& target_column) {
    target_column_ = target_column;
  }

  // Returns a string of the form:
  //   name_path:target_column
  std::string DebugString() const;

 private:
  std::vector<IdString> name_path_;
  ResolvedColumn target_column_;

  // Copyable.
};

typedef std::vector<ValidNamePath> ValidNamePathList;

// A map of columns to namepaths and associated target columns
// that are accessible from the original column.  Used in
// the 'ValidFieldInfoMap' class, which owns the 'ValidNamePathList'
// pointers in this map.
typedef absl::flat_hash_map<ResolvedColumn, std::unique_ptr<ValidNamePathList>,
                            ResolvedColumnHasher>
    ResolvedColumnToValidNamePathsMap;

// Map from ResolvedColumn column_id to a list of name paths that are
// valid to access from that ResolvedColumn.
class ValidFieldInfoMap {
 public:
  ValidFieldInfoMap() = default;
  ValidFieldInfoMap(const ValidFieldInfoMap&) = delete;
  ValidFieldInfoMap& operator=(const ValidFieldInfoMap&) = delete;
  ~ValidFieldInfoMap();

  // Add a new 'valid_name_path' for the specified ResolvedColumn.
  // If there is already an entry in the map for the 'column' then adds
  // 'valid_name_path' to the related ValidNamePathList.
  // Otherwise inserts a new entry into the map for 'column' with a new
  // ValidNamePathList containing 'valid_name_path'.
  void InsertNamePath(const ResolvedColumn& column,
                      const ValidNamePath& valid_name_path);

  // Look up an entry in 'column_id_to_name_path_list_map_' based on the
  // input ResolvedColumn, and return true along with the related
  // 'valid_name_path_list' if it exists.  Otherwise returns false if
  // 'column' is not found.
  bool LookupNamePathList(const ResolvedColumn& column,
                          const ValidNamePathList** valid_name_path_list) const;

  // Returns true if `column_to_valid_name_paths_map_` contains `column`, and
  // some prefix of `name_path` exists in the valid name paths for `column`.
  // The target column in `name_path` is ignored.
  bool ContainsColumnAndNamePathPrefix(const ResolvedColumn& column,
                                       const ValidNamePath& name_path) const;

  void Clear();

  // Find the entry in `name_path_list` whose name path is the
  // longest prefix of `path_names`. If no entry is found returns false.
  // If a matching entry is found, returns true along with the matching
  // entry's target column in `resolved_column` and the entry's name path
  // size in `name_at`.
  static bool FindLongestMatchingPathIfAny(
      const ValidNamePathList& name_path_list,
      absl::Span<const IdString> path_names, ResolvedColumn* resolved_column,
      int* name_at);

  const ResolvedColumnToValidNamePathsMap& map() const {
    return column_to_valid_name_paths_map_;
  }
  std::string DebugString(absl::string_view indent = "") const;

 private:
  // A map from a ResolvedColumn to a name path list that is valid
  // to access from that ResolvedColumn.
  ResolvedColumnToValidNamePathsMap column_to_valid_name_paths_map_;
};

// A NamedColumn is an element of a NameList, associating a name with a
// ResolvedColumn.
class NamedColumn {
 public:
  // Constructor for non-value table columns.
  NamedColumn(IdString name, const ResolvedColumn& column, bool is_explicit)
      : name_(name),
        column_(column),
        is_explicit_(is_explicit),
        is_value_table_column_(false) {}
  // Constructor for value table columns.
  NamedColumn(IdString name, const ResolvedColumn& column, bool is_explicit,
              const IdStringSetCase& excluded_field_names)
      : name_(name),
        column_(column),
        is_explicit_(is_explicit),
        is_value_table_column_(true),
        excluded_field_names_(excluded_field_names) {}

  // Having a move constructor makes storing this in STL containers more
  // efficient.
  NamedColumn() = default;
  NamedColumn(NamedColumn&& old) = default;
  NamedColumn(const NamedColumn& other) = default;
  NamedColumn& operator=(const NamedColumn& other) = default;

  std::string DebugString() const;
  void Describe(ProductMode product_mode,
                reflection::Column* reflection_column) const;

  IdString name() const { return name_; }
  const ResolvedColumn& column() const { return column_; }
  bool is_explicit() const { return is_explicit_; }
  bool is_value_table_column() const { return is_value_table_column_; }
  const IdStringSetCase& excluded_field_names() const {
    return excluded_field_names_;
  }

 private:
  IdString name_;

  ResolvedColumn column_;

  // True if the alias for this column is an explicit name. Generally, explicit
  // names come directly from the query text, and implicit names are those that
  // are generated automatically from something outside the query text, like
  // column names that come from a table schema. Explicitness does not change
  // any scoping behavior except for the final check in strict mode that may
  // raise an error. For more information, please see the beginning of
  // (broken link).
  bool is_explicit_ = false;

  // True if this column is the value produced in a value table scan.
  // The name acts more like a range variable in this case, but is
  // stored in columns_ so it shows up in the right order in SELECT *.
  bool is_value_table_column_ = false;

  // Only relevant for value table columns.  Indicates field names that
  // should be ignored for implicit lookups from the containing NameList.
  // For example, if a NameList includes value table column 'vt' and that
  // value table contains field 'f1', then doing a NameList lookup for 'f1'
  // will fail if 'f1' is in this set for 'vt'.  This also affects whether
  // a field is returned when expanding '*' (but not rangevar.*, for
  // instance 'vt.*').
  //
  // This uses a Set rather than HashSet because it is almost always empty,
  // or has just a few elements.
  // TODO I think using a CopyOnWrite of a HashSet here may be
  // worthwhile.  We copy identical sets around a lot, and using CopyOnWrite
  // would let us use a HashSet without making copy or destruct too expensive.
  IdStringSetCase excluded_field_names_;
};

// Indicates whether the current expression is being resolved in a pre- or
// post-grouping context (or unknown).
enum class ExprGroupingContext {
  // TODO: should we add a separate default (or just use optional) and reserve
  // this to the schrodinger column?
  //
  // The context may be unknown in the case of an expression that is a grouping
  // constant, i.e., itself a grouping column or is functionally dependent on
  // grouping columns) as it is available both before and after grouping.
  // Subsequent lateral references from other select columns may pin it to
  // either pre- or post-grouping.
  // For example:
  //     SELECT col+1 AS a
  //     FROM t
  //     GROUP BY a
  kUnknown,

  kPreGrouping,
  kPostGrouping,
};

struct ColumnReferenceContext {
  const ParseLocationPoint& location;
  ExprGroupingContext grouping_context;
};

// Forward declarations.
class NameTarget;
class NameScope;
struct SelectColumnState;

using LoggingCallback = std::function<absl::Status(
    const ASTNode* location, const NameTarget& original_target)>;

using ColumnReferenceCallback =
    std::function<absl::StatusOr<ResolvedColumn>(ColumnReferenceContext)>;

// Used to create transparent detector NameTargets which capture the name hit
// event and invoke a callback, before forwarding to the `fallback_name_scope`
// to perform the actual lookup. The `fallback_name_scope` is usually
// a similarly constructed NameScope to the one containing this target, if such
// transparent targets like this one were not added.
// This is useful when running tests to determine the impact of a new
// feature on backward compatibility) or to log warnings before deprecations,
// for example.
struct LoggingTargetInfo {
  // The NameScope which should actually be used, now that the event has been
  // captured.
  const NameScope* fallback_name_scope;
  // The callback to invoke when this name is hit in the current context, e.g.
  // to log a warning.
  LoggingCallback logging_callback;
  // Copyable.
};

// Information needed for a column that gets assigned a ResolvedColumn only on
// its first lateral reference, i.e., reference from another SELECT list expr
// or in the WHERE clause. This is *NOT* related to JOIN LATERAL.
struct DelayedColumnInfo {
  // this callback is invoked if this column is ever accessed from another
  // SELECT list column, or in the WHERE clause. This is used to isolate the
  // target ResolvedExpr to be precomputed on a ProjectScan, and replacing it
  // with a ResolvedColumnRef instead. This callback is called only once, and
  // returns the assigned ResolvedColumn for the precomputed expression.
  ColumnReferenceCallback precompute_callback;
  // The SelectColumnState represented by this delayed NameTarget.
  SelectColumnState* select_column_state = nullptr;
  // Copyable.
};

// A target that a name in a NameScope points at.
//
// A name can resolve to:
//   1. A range variable - i.e. a name that references one row of a scan as
//      we iterate through it.  e.g. A table alias introduced in a FROM clause.
//      This name points to a NameList giving the names visible in that scan.
//      A pattern variable in MATCH_RECOGNIZE is also treated as a range
//      variable.
//   2. An implicit value, for column names available on scans in the
//      from clause.
//   3. An explicit value, for column names that were given an explicit
//      alias (e.g. in the select list).
//   4. A field of a value table column.  In this case, we return the container
//      column value and the caller is expected to re-resolve the name as a
//      field of that container.
//   5. Ambiguous, meaning the name could point at multiple implicit values,
//      so a query that references it should give an error.
//   6. Access error, meaning that accessing this name is invalid and provides
//      an error.  Access error targets keep track of the Kind of the original
//      target, and may have a non-empty ValidNamePathList that
//      identifies (sub)fields that are valid to access even though this
//      target is not.
//      `is_terminal_error_` prevents accessing these valid paths from the
//      current ACCESS_ERROR target.
//
// Name collisions are resolved as follows:
//   - range variables always take precedence over column names
//   - duplicate column names become ambiguous
//   - duplicate range variable names are not possible (NameList gives an
//     error rather than allowing duplicates to be added)
//
// Note that for value tables, the alias introduced for the scan is somewhat
// of a hybrid between a range variable and an explicit column.  In NameScope,
// lookups will return the range variable, pointing at a NameList with
// is_value_table true.
//
// NameScopes cannot be built by inserting names directly.
// Callers build a NameList describing the set of columns available
// from part of a query and then build a NameScope from that NameList,
// attaching a parent NameScope if applicable (i.e. when inside a subquery).
//
// This is based on the GoogleSQL name scoping doc:
// (broken link)
class NameTarget {
 public:
  enum Kind {
    RANGE_VARIABLE,   // supports scan_columns()
    IMPLICIT_COLUMN,  // supports column()
    EXPLICIT_COLUMN,  // supports column()
    FIELD_OF,         // supports column_containing_field()
    AMBIGUOUS,
    ACCESS_ERROR,  // supports original_kind() which indicates the
                   // original Kind of this NameTarget, and a non-empty
                   // 'valid_field_info_list_'
  };

  // Default constructor makes an ambiguous NameTarget.
  NameTarget() : kind_(AMBIGUOUS) {}

  // Construct a NameTarget for a range variable pointing at a scan.
  ABSL_DEPRECATED("Inline me!")
  explicit NameTarget(const NameListPtr& scan_columns)
      : NameTarget(scan_columns, /*is_pattern_variable=*/false) {}

  // Construct a NameTarget for a range variable pointing at a scan, or a
  // MATCH_RECOGNIZE pattern variable.
  explicit NameTarget(const NameListPtr& scan_columns, bool is_pattern_variable)
      : kind_(RANGE_VARIABLE),
        scan_columns_(scan_columns),
        is_pattern_variable_(is_pattern_variable) {}

  // Construct a NameTarget pointing at a column, implicitly or explicitly.
  NameTarget(const ResolvedColumn& column, bool is_explicit)
      : kind_(is_explicit ? EXPLICIT_COLUMN : IMPLICIT_COLUMN),
        column_(column) {}

  // Construct a NameTarget for a delayed column, which gets assigned a
  // ResolvedColumn only on its first lateral reference.
  NameTarget(bool is_explicit, DelayedColumnInfo delayed_column_info)
      : kind_(is_explicit ? EXPLICIT_COLUMN : IMPLICIT_COLUMN),
        delayed_column_info_(std::move(delayed_column_info)) {}

  // Construct a NameTarget for a transparent detector column, which only logs
  // the hit event and delegates to another NameScope. This transparent target
  // does not need or know about its own ResolvedColumn.
  NameTarget(bool is_explicit, LoggingTargetInfo delayed_column_info)
      : kind_(is_explicit ? EXPLICIT_COLUMN : IMPLICIT_COLUMN),
        logging_target_info_(std::move(delayed_column_info)) {}

  // Construct a FIELD_OF NameTarget pointing at a column, with a specific
  // FIELD_OF id.  For STRUCT fields, the field_id is the field index.
  // For PROTO fields, the field_id is the field tag number.
  // For ROW fields, field_id is just filled with -1.
  // This constructor is only used for kind==FIELD_OF.
  // The stored `field_id` is not currently used anywhere.
  NameTarget(const ResolvedColumn& column, int field_id)
      : kind_(FIELD_OF), column_(column), field_id_(field_id) {}

  NameTarget(NameTarget&& old) = default;
  NameTarget(const NameTarget& other) = default;
  NameTarget& operator=(const NameTarget& other) = default;

  // Determine if two NameTargets are equal.  The current implementation
  // relies on NameList and ValidNamePathList equality using DebugString()
  // which is not ideal, but this is only used for testing at this time.
  // If we want to use these for more than just testing, then we need to
  // implement full NameList and ValidNamePathList equality.
  bool Equals_TESTING(const NameTarget& other) const;

  void SetAmbiguous() {
    scan_columns_.reset();
    column_.Clear();
    kind_ = AMBIGUOUS;
  }

  // Mark this NameTarget as providing an error upon access.  This indicates
  // that the name actually exists, but the corresponding column or range
  // variable cannot validly be referenced by itself.  The original
  // 'name_target' Kind is stored for later use during error messaging.
  // If non-empty, 'access_error_message' indicates the error message
  // associated with this NameTarget.
  // 'is_terminal_error' indicates whether all lookups for this target and any
  // child/descendent fields is an error, or whether lookups for
  // child/descendent fields might be valid.  This affects how LookupNamePath()
  // behaves once it looks up an access error NameTarget.
  void SetAccessError(Kind original_kind,
                      absl::string_view access_error_message = "",
                      bool is_terminal_error = false);

  // Marks this NameTarget as a pattern variable.
  absl::Status SetIsPatternVariable(bool is_pattern_variable) {
    if (kind_ != RANGE_VARIABLE) {
      GOOGLESQL_RET_CHECK_EQ(kind_, ACCESS_ERROR);
      GOOGLESQL_RET_CHECK_EQ(original_kind_, RANGE_VARIABLE);
    }
    is_pattern_variable_ = is_pattern_variable;
    return absl::OkStatus();
  }

  Kind kind() const { return kind_; }

  Kind original_kind() const {
    ABSL_DCHECK(IsAccessError());
    return original_kind_;
  }

  const std::string& access_error_message() const {
    return access_error_message_;
  }

  bool is_terminal_error() const { return is_terminal_error_; }

  static bool IsColumnKind(Kind kind) {
    return kind == IMPLICIT_COLUMN || kind == EXPLICIT_COLUMN;
  }
  bool IsColumn() const { return IsColumnKind(kind_); }
  static bool IsRangeVariableKind(Kind kind) { return kind == RANGE_VARIABLE; }
  bool IsRangeVariable() const { return IsRangeVariableKind(kind_); }
  static bool IsFieldOfKind(Kind kind) { return kind == FIELD_OF; }
  bool IsFieldOf() const { return IsFieldOfKind(kind_); }
  static bool IsAmbiguousKind(Kind kind) { return kind == AMBIGUOUS; }
  bool IsAmbiguous() const { return IsAmbiguousKind(kind_); }
  static bool IsAccessErrorKind(Kind kind) { return kind == ACCESS_ERROR; }
  bool IsAccessError() const { return IsAccessErrorKind(kind_); }

  bool IsExplicit() const {
    return kind_ == RANGE_VARIABLE || kind_ == EXPLICIT_COLUMN;
  }
  bool IsImplicit() const {
    return kind_ == IMPLICIT_COLUMN || kind_ == FIELD_OF;
  }

  const std::shared_ptr<const NameList>& scan_columns() const {
    ABSL_DCHECK_EQ(kind_, RANGE_VARIABLE);
    return scan_columns_;
  }

  bool is_pattern_variable() const {
    if (kind_ != RANGE_VARIABLE) {
      ABSL_DCHECK_EQ(kind_, ACCESS_ERROR);
      ABSL_DCHECK_EQ(original_kind_, RANGE_VARIABLE);
    }
    return is_pattern_variable_;
  }

  const ResolvedColumn& column() const {
    ABSL_DCHECK(IsColumn()) << DebugString();
    return column_;
  }
  const ResolvedColumn& column_containing_field() const {
    ABSL_DCHECK(IsFieldOf()) << DebugString();
    return column_;
  }
  int field_id() const {
    ABSL_DCHECK(IsFieldOf()) << DebugString();
    return field_id_;
  }

  const ValidNamePathList& valid_name_path_list() const {
    return valid_name_path_list_;
  }

  ValidNamePathList* mutable_valid_name_path_list() {
    return &valid_name_path_list_;
  }

  void set_valid_name_path_list(const ValidNamePathList& valid_name_path_list) {
    valid_name_path_list_ = valid_name_path_list;
  }

  // The NameTarget must be an ACCESS_ERROR, and the 'original_kind_'
  // must not be AMBIGUOUS since if the original NameTarget was
  // AMBIGUOUS then accessing fields from it cannot be valid.
  void AppendValidNamePathList(const ValidNamePathList& info_list) {
    ABSL_DCHECK(IsAccessError()) << DebugString();
    ABSL_DCHECK(!IsAmbiguousKind(original_kind_)) << DebugString();
    valid_name_path_list_.insert(valid_name_path_list_.end(), info_list.begin(),
                                 info_list.end());
  }

  std::string DebugString() const;

  // Invoke this when the target is a column. This method is idempotent, i.e.,
  // a NOOP if called before.
  // REQUIRES: delayed_column_info_ must be set.
  absl::Status SetupPrecomputedColumnIfNeeded(ColumnReferenceContext context);

  // Should only be called after SetupPrecomputedColumnIfNeeded() has been
  // called and all initialization is done.
  const std::optional<DelayedColumnInfo>& delayed_column_info() const {
    ABSL_DCHECK(!delayed_column_info_.has_value() ||
           delayed_column_info_->precompute_callback == nullptr);
    return delayed_column_info_;
  }

  const std::optional<LoggingTargetInfo>& logging_target_info() const {
    return logging_target_info_;
  }

 private:
  Kind kind_;

  // Populated if kind_ == RANGE_VARIABLE.
  std::shared_ptr<const NameList> scan_columns_;
  // True if this is a pattern variable in a MATCH_RECOGNIZE clause, false for
  // a regular range variable.
  bool is_pattern_variable_ = false;

  // Populated if kind_ is one of:
  //   {IMPLICIT_COLUMN, EXPLICIT_COLUMN, FIELD_OF}.
  ResolvedColumn column_;

  // Populated if kind_ == FIELD_OF.
  // Represents field index for STRUCT fields.
  // Represents field tag number for PROTO fields.
  int field_id_ = -1;

  // Populated if kind_ == ACCESS_ERROR.
  // Determines what the type of the original NameTarget was.  Used
  // for error messaging and validating function calls that populate
  // 'valid_field_info_list_'.
  Kind original_kind_;

  // Can only be populated if kind_ == ACCESS_ERROR, but can be empty.
  // If non-empty, provides a custom error message that can be used
  // when the NameTarget is referenced.
  std::string access_error_message_;

  // Used with ACCESS_ERROR, to indicate whether all lookups for this target and
  // any child/descendent fields is an error, or whether lookups for
  // child/descendent fields might be valid.  This affects how LookupNamePath
  // behaves once it looks up an access error NameTarget.
  bool is_terminal_error_ = false;

  // Can only be populated if kind_ == ACCESS_ERROR.
  // Requires original_kind_ to be RANGE_VARIABLE, EXPLICIT_COLUMN,
  // IMPLICIT_COLUMN, or FIELD_OF.
  // Identifies a list of valid name paths and their associated ResolvedColumns
  // that are accessible from this target.
  ValidNamePathList valid_name_path_list_;

  // If this column is a SELECT list column, this is the info needed to enable
  // access in the WHERE clause, or other SELECT list columns.
  // REQUIRES: If set, IsColumn() must be true.
  // REQUIRES: Mutually exclusive with `logging_target_info_`.
  std::optional<DelayedColumnInfo> delayed_column_info_;

  // If set, contains the callback to invoke and the actual fallback namescope
  // for this transparent detector target.
  // REQUIRES: If set, IsColumn() must be true.
  // REQUIRES: Mutually exclusive with `delayed_column_info_`.
  std::optional<LoggingTargetInfo> logging_target_info_;
  // Copyable.
};

// A name scope for aliases visible in a query, not including the global names
// visible in a Catalog.
//
// NameScopes can be chained together, and child scopes can hide names from
// parent scopes.
//
// NameScopes are normally stored in shared_ptrs to make lifetime management
// easier when multiple child scopes share a parent scope.
//
// All name lookups are case insensitive.
//
// How correlated lookups are tracked:
//   When resolving a correlated subquery, we'll build a local NameScope that
//   inherits the outer query's NameScope as its previous_scope.
//
//   In the child NameScope (the subquery's NameScope), we'll store a
//   CorrelatedColumnsSet, and that set will store all ResolvedColumns looked
//   up via this NameScope (i.e. inside the subquery) that resolve to a
//   column from a parent NameScope.  This set will provide the list of
//   parameters that must be passed to the ResolvedSubqueryExpr.
//
//   When LookupName finds a name from a parent scope, it returns the
//   CorrelatedColumnsSet pointer for the child NameScope, and the
//   caller must insert into it any columns resolved from a parent scope.
//   This happens in the caller (rather than here) because the NameScope lookup
//   may return a range variable, and won't know the specific columns
//   referenced using it (e.g. rangevar.col or rangevar.*).
//
//   With multiply nested NameScopes, a ResolvedColumn resolved from the
//   outermost ancestor scope must be added to CorrelatedColumnsSets for each
//   child scope that that lookup passes through, so the columns can be added
//   to the ResolvedSubqueryExpr parameters list at each subquery nesting
//   level.  To allow this, LookupName returns a list of CorrelatedColumnsSets.
//
// For full specification, see (broken link).
class NameScope {
 public:
  // Make a scope with no underlying fallback scope.
  NameScope() : previous_scope_(nullptr) {}

  // Make a scope with a fallback to look for names in <previous_scope>, if
  // non-NULL.
  // If <correlated_columns_set> is non-NULL, store that pointer with this scope
  // so it can be updated to compute the set of correlated column references.
  // The <correlated_columns_set> and <previous_scope> pointers must outlive
  // this NameScope.
  explicit NameScope(const NameScope* previous_scope,
                     CorrelatedColumnsSet* correlated_columns_set = nullptr)
      : previous_scope_(previous_scope),
        correlated_columns_set_(correlated_columns_set) {}

  // Make a NameScope that inherits names from <previous_scope>, if non-NULL,
  // and making names in <name_list> visible over top of it.
  // If <correlated_columns_set> is non-NULL, store that pointer with this scope
  // so it can be updated to compute the set of correlated column references.
  // The <correlated_columns_set> and <previous_scope> pointers must outlive
  // this NameScope.
  NameScope(const NameScope* previous_scope, const NameListPtr& name_list,
            CorrelatedColumnsSet* correlated_columns_set = nullptr);

  // Make a NameScope with names from <name_list>.
  explicit NameScope(const NameList& name_list);

  ~NameScope() {
    ABSL_DCHECK(lateral_column_reference_observers_.empty())
        << "Lateral column reference observers must be cleared before "
           "destructing the NameScope.";
  }

  // Creates a new NameScope copied from the current NameScope, where
  // locally defined names are updated with new NameTargets.  The entries
  // in 'valid_field_info_list_in' determine which local names (including
  // fields and subfields) remain valid to access, while other local names
  // become error targets.  The new NameScope has the same 'previous_scope_'
  // as this NameScope, so all previous scope names remain available for
  // lookup (if not overridden by local names).
  //
  // WARNING - The caller must ensure that the previous_scope_'s names
  // remain valid for access in the new NameScope.
  absl::Status CreateNameScopeGivenValidNamePaths(
      const ValidFieldInfoMap& valid_field_info_map_in,
      std::unique_ptr<NameScope>* new_name_scope) const;

  // Creates a new NameScope copied from this NameScope, with names in
  // 'name_list_with_overriding_names' overriding local names in this
  // NameScope.  The new NameScope has the same 'previous_scope_'
  // as this NameScope, so all previous scope names remain available for
  // lookup (if not overridden by local names).
  // A current limitation is that 'namelist_with_overriding_names'
  // cannot contain pseudocolumns, range variables, or value table
  // columns - it can only contain normal columns.  So this function
  // returns an error status if any non-normal columns are present.
  absl::Status CopyNameScopeWithOverridingNames(
      const std::shared_ptr<NameList>& namelist_with_overriding_names,
      std::unique_ptr<NameScope>* scope_with_new_names) const;

  // Creates a new NameScope copied from this NameScope, with NameTargets
  // in 'overriding_name_targets' overriding local names in this
  // NameScope.  The new NameScope has the same 'previous_scope_'
  // as this NameScope, so all previous scope names remain available for
  // lookup (if not overridden by local names).
  absl::Status CopyNameScopeWithOverridingNameTargets(
      const IdStringHashMapCase<NameTarget>& overriding_name_targets,
      std::unique_ptr<NameScope>* scope_with_new_names) const;

  // Clones range variables from `other_scope` into this name scope, replacing
  // old columns in `other_scope` with new columns.
  // `column_map` maps old columns (by int column_id) to new ResolvedColumns.
  //
  // Old columns that are not in `column_map` will be dropped from the
  // NameLists under copied range variables.
  //
  // For example, suppose other_scope contains the following range variable:
  //   t -> RANGE_VARIABLE<a#1, b#2, c#3>
  // and the input column_map is:
  //   #1 -> new_a#4
  //   #2 -> new_b#5
  // Then this function will add the following range variables to the this name
  // scope:
  //   t -> RANGE_VARIABLE<new_a#4, new_b#5>
  // Note that column c#3 in the old range variable is skipped since it is not
  // in the column_map.
  //
  // Range variables whose names appear in `excluded_range_variable_set` will
  // be excluded from cloning.
  absl::Status CloneRangeVariablesMapped(
      const NameScope& other_scope,
      const absl::flat_hash_map<int, ResolvedColumn>& column_map,
      const ASTNode& ast_location,
      const IdStringHashSetCase& excluded_range_variable_set);

  // Look up a name in this scope, and underlying scopes if necessary.
  // Return true and copy result into <*found> if found.
  //
  // If <correlated_columns_sets> is non-NULL, on return, it will contain
  // any non-NULL correlated_columns_set_ from any NameScopes traversed before
  // reaching the NameScope containing <name>.  Any column used via this
  // resolved name should be added to all of these CorrelatedColumnsSets,
  // and then passed as a parameter to the corresponding ResolvedSubqueryExpr.
  //
  // The returned sets are ordered so the ones attached to child scopes come
  // before the ones attached to their parent scopes.
  bool LookupName(IdString name, NameTarget* found,
                  CorrelatedColumnsSetList* correlated_columns_sets = nullptr,
                  const std::vector<ExprResolutionInfo*>**
                      out_lateral_column_reference_observers = nullptr) const;

  // Similar to the previous <LookupName> function, but allows multi-part
  // names to be looked up, and takes a PathExpressionSpan as input instead.
  // Looks into underlying scopes if necessary.
  //
  // Returns error status if the name path is ambiguous, is an access
  // error, or includes an invalid field reference.
  //
  // Returns OK (with 'num_names_consumed' = 0) if the first name is not
  // found in the scope.
  //
  // Also returns OK if a prefix of the name path resolves to a valid target
  // (range variable, column, or field).  In this case, 'target_out' is returned
  // as the target of the lookup, 'num_names_consumed' indicates how many names
  // were consumed (matching 'target_out'), and 'correlated_columns_sets'
  // identifies correlation information (if any).
  //
  // For example, if we look up 'a.b.c.d' and find that 'a' looks up
  // as a range variable and 'b' is a field of 'a', then a FIELD_OF
  // NameTarget is returned and 'num_names_consumed' is set to 2.  On
  // the other hand if we had found that 'a.b.c.d' as a whole looked
  // up as a column, then a column NameTarget would be returned and
  // 'num_names_consumed' would be set to 4.
  //
  // If the lookup results in an ACCESS_ERROR target:
  //  1. If `is_terminal_error` is true, LookupNamePath terminates and returns
  //     the ACCESS_ERROR target.
  //  2. Otherwise, LookupNamePath continues to look up valid field paths to see
  //     if accessing the full path is valid, even if the current target is not
  //     itself valid to access.
  //  For example, when grouping by `a.b`, and looking up `a.b.c`:
  //     Accessing `a` returns an ACCESS_ERROR:
  //     - If `is_terminal_error` is true, LookupNamePath terminates and returns
  //       the ACCESS_ERROR target.
  //     - Otherwise, LookupNamePath will find `a.b.c` and return that target.
  //
  // `out_referenced_pattern_variable` is set to the pattern variable from which
  // the column was resolved, if the column was resolved from a pattern
  // variable, and std::nullopt otherwise.
  //
  // 'in_strict_mode' identifies whether unqualified names are valid
  // to access.  'clause_name' and 'problem_string' are only used for
  // error messaging.
  static const char* const kDefaultProblemString;
  absl::Status LookupNamePath(
      const PathExpressionSpan& path_expr, const char* clause_name,
      const char* problem_string, bool in_strict_mode,
      CorrelatedColumnsSetList& correlated_columns_sets,
      int* num_names_consumed,
      std::optional<IdString>& out_referenced_pattern_variable,
      const std::vector<ExprResolutionInfo*>*&
          out_lateral_column_reference_observers,
      NameTarget* target_out) const;

  // Look up a name in this scope, and underlying scopes if necessary.
  // Return true if the name exists, including ambiguous names and field
  // names of value tables.
  bool HasName(IdString name) const;

  // Returns the closest suggestion on a <mistyped_name> from the names present
  // in this scope, if one exists. Otherwise returns an empty string.
  std::string SuggestName(IdString mistyped_name) const;

  // Returns whether or not any of the local names (not in previous scopes)
  // are ValueTableColumns.
  bool HasLocalValueTableColumns() const {
    return !value_table_columns().empty();
  }

  // Returns whether or not any of the local names (not in previous scopes)
  // are range variables.
  bool HasLocalRangeVariables() const;

  std::string DebugString(
      absl::string_view indent = "",
      const IdStringSetCase* name_list_columns = nullptr) const;

  // Add pseudo-columns and table aliases into `result_table`.
  void DescribeInto(
      reflection::ResultTable* result_table,
      const IdStringHashMapCase<int>& name_list_column_names_count,
      const absl::flat_hash_map<ResolvedColumn, std::vector<int>>&
          name_list_columns_map) const;

  const NameScope* previous_scope() const { return previous_scope_; }

  // Disallows correlated access from this scope.
  // This is used to prevent correlated access from a child scope, like in
  // MATCH_RECOGNIZE where we do not want subqueries to access columns from
  // the main table, as column access should define row ranges but subqueries
  // do not have that info.
  // See b/394128431 for more details.
  //
  // Here is an example to illustrate why MATCH_RECOGNIZE needs to disallow
  // correlated access in some situations:
  //      MEASURES SUM(b.key - (SELECT key))
  //
  // The access to `b.key` pins the row range to those assigned to `b`.
  // However, when resolving (SELECT key), because it's a subquery, the resolver
  // creates a completely new QueryResolutionInfo and resolves against the inner
  // row range of the subquery. The access to `key` here is correlated, and
  // the subquery doesn't have the means to pin the row range at the appropriate
  // QueryResolutionInfo, or to detect the conflict with the earlier `b.key`
  // access.
  void DisallowCorrelatedAccess(absl::string_view reason) {
    allows_correlated_access_ = false;
    reason_to_disallow_correlated_access_ = reason;
  }

  void EnableMatchNumberFunction() { allows_match_number_function_ = true; }
  void EnableAllMatchFunctions() {
    allows_match_number_function_ = true;
    allows_match_row_functions_ = true;
  }

  bool allows_match_number_function() const {
    return allows_match_number_function_;
  }
  bool allows_match_row_functions() const {
    return allows_match_row_functions_;
  }

  void PushLateralColumnReferenceObserver(
      ExprResolutionInfo* expr_resolution_info) {
    lateral_column_reference_observers_.push_back(expr_resolution_info);
  }
  void PopLateralColumnReferenceObserver(
      ExprResolutionInfo* expr_resolution_info) {
    ABSL_DCHECK(!lateral_column_reference_observers_.empty());
    ABSL_DCHECK(expr_resolution_info == lateral_column_reference_observers_.back());
    lateral_column_reference_observers_.pop_back();
  }

 private:
  const NameScope* const previous_scope_ = nullptr;  // may be NULL

  // Set used to collect correlated columns referenced from previous scopes.
  // This is not used or updated by the NameScope class itself.
  // If this is non-NULL, this set pointer is returned from LookupName, and
  // any referenced columns should be added to it by the caller.
  CorrelatedColumnsSet* correlated_columns_set_ = nullptr;  // Not owned.

  // Indicates whether or this NameScope allows correlated access. When false,
  // LookupNamePath() will return an error if a column from *this scope* is
  // accessed via some child scope.
  bool allows_correlated_access_ = true;
  // Explanation to provide as the access error message when correlated access
  // is disallowed and a lookup results in finding a correlated name target.
  absl::string_view reason_to_disallow_correlated_access_;

  // Value table column information, including a ResolvedColumn and a list of
  // field names that are excluded/ignored from a lookup in the containing
  // NameScope.  For instance, if the NameScope contains value table column vt1
  // with fields f1 and f2, and f2 is in <excluded_field_names>, then
  // a LookupName() for 'f2' into the NameScope returns not found.
  //
  // Additionally, a ValueTableColumn includes a flag indicating whether
  // or not it is valid to access, and a list of its fields that
  // are valid to access.  For example, in the example above if vt1 is
  // !is_valid_to_access then LookupName() for 'vt1' will return an error
  // NameTarget (which is different than returning not found).  This
  // returned error NameTarget identifies any field paths that are valid
  // to access, allowing the caller to access them if appropriate even
  // if 'vt1' itself is not valid to access.
  class ValueTableColumn {
   public:
    ValueTableColumn(const ResolvedColumn& column,
                     const IdStringSetCase& excluded_field_names,
                     bool is_valid_to_access,
                     const ValidNamePathList& valid_name_path_list)
        : column_(column),
          excluded_field_names_(excluded_field_names),
          is_valid_to_access_(is_valid_to_access),
          valid_name_path_list_(valid_name_path_list) {}

    ValueTableColumn() = default;
    ValueTableColumn(ValueTableColumn&& old) = default;
    ValueTableColumn(const ValueTableColumn& other) = default;
    ValueTableColumn& operator=(const ValueTableColumn& other) = default;

    // Copyable.
    std::string DebugString() const;

    ResolvedColumn column() const { return column_; }
    const IdStringSetCase& excluded_field_names() const {
      return excluded_field_names_;
    }
    IdStringSetCase& excluded_field_names() { return excluded_field_names_; }

    bool is_valid_to_access() const { return is_valid_to_access_; }
    const ValidNamePathList& valid_name_path_list() const {
      return valid_name_path_list_;
    }

   private:
    ResolvedColumn column_;
    IdStringSetCase excluded_field_names_;
    bool is_valid_to_access_ = false;
    ValidNamePathList valid_name_path_list_;
  };

  // A private constructor for internal use only, taking already-constructed
  // NameTargets and ValueTableColumns as arguments.
  NameScope(const NameScope* previous_scope,
            const IdStringHashMapCase<NameTarget>& name_targets,
            const std::vector<ValueTableColumn>& value_table_columns,
            CorrelatedColumnsSet* correlated_columns_set);

  NameScope(const NameScope&) = delete;
  NameScope& operator=(const NameScope&) = delete;

  // Adds 'name' to the excluded columns list of the 'value_table_column', if
  // and only if 'value_table_column' contains a field of that 'name' and that
  // 'name' is not already excluded.
  static void ExcludeNameFromValueTableIfPresent(
      IdString name, ValueTableColumn* value_table_column);

  // Iterates over 'names', and for each entry inserts the name and
  // corresponding NameTarget into the current NameScope if
  // the name is not already present in the NameScope.
  void InsertNameTargetsIfNotPresent(
      const IdStringHashMapCase<NameTarget>& names);

  // Creates a new RangeVariable NameTarget derived from the
  // 'original_name_target', where the new NameTarget reflects whether
  // or not the name (and its fields) are valid to access based on
  // entries in 'valid_field_info_list_in'.
  static absl::Status CreateNewRangeVariableTargetGivenValidNamePaths(
      const NameTarget& original_name_target,
      const ValidFieldInfoMap& valid_field_info_map_in,
      NameTarget* new_name_target);

  // Creates a new map of (name, NameTarget) pairs derived from the local
  // names(), where the new NameTargets reflect whether or not the
  // name (and its fields) are valid to access based on entries in
  // 'valid_field_info_list_in'.
  //
  // Note that if an existing local name is invalid to access, it remains
  // invalid to access, but it may have valid name paths that are accessible
  // and need to be updated.  In that case, the resulting NameTarget valid
  // field path is generated by concatenating the existing valid name paths
  // with matching entries in 'valid_field_info_map_in'.  Currently, such
  // concatenation is only supported for 'valid_field_info_map_in' entries
  // that have empty name paths (that in effect is used to update the
  // target ResolvedColumn associated with the existing name path).
  absl::Status CreateNewLocalNameTargetsGivenValidNamePaths(
      const ValidFieldInfoMap& valid_field_info_map_in,
      IdStringHashMapCase<NameTarget>* new_name_targets) const;

  // Creates a new vector of ValueTableColumns from the local
  // ValueTableColumns, where the new ValueTableColumns and their fields
  // identify which are valid to access and which are invalid based
  // on the entries in 'valid_field_info_list_in'.
  void CreateNewValueTableColumnsGivenValidNamePaths(
      const ValidFieldInfoMap& valid_field_info_map_in,
      std::vector<ValueTableColumn>* new_value_table_columns) const;

  // Returns a new 'field_target' given a 'value_table_column' with
  // field 'field_name'.  Returns a valid target if 'value_table_column'
  // has a valid path name list that exactly matches this field name.
  // Otherwise returns an invalid target.  If returning an invalid
  // target, valid path names from the invalid
  // target are populated if corresponding valid path names exist in
  // the 'value_table_column'.  For instance, if the 'value_table_column'
  // has valid path 'a.b.c', and this is invoked for field 'a', then
  // an invalid target is returned with valid path 'b.c'.
  static absl::Status CreateGetFieldTargetFromInvalidValueTableColumn(
      const ValueTableColumn& value_table_column, IdString field_name,
      NameTarget* field_target);

  // The local state for this NameScope is stored in this struct which is
  // stored in a CopyOnWrite.  This allows cheap copies when constructing
  // NameScopes from NameLists and in NameList::MergeFrom.
  struct State {
    // This is the main map storing the names visible in this local scope
    // (not including names from parent scopes).
    // Using map rather than hash_map because the set is often small,
    // and these may be constructed and destructed frequently.
    IdStringHashMapCase<NameTarget> names;

    // Vector of ValueTableColumns for all value tables in this local scope.
    // When looking up a name, we also look for fields of any of these columns
    // (except for fields marked as excluded for each value table column).
    std::vector<ValueTableColumn> value_table_columns;

    void CopyFrom(const State& other) {
      names = other.names;
      value_table_columns = other.value_table_columns;
    }
  };

  // Indicates whether or not this NameScope allows the MATCH_NUMBER() function,
  // Which is available anywhere in the MEASURES clause, even outside
  // aggregates,since it's the same for all rows of the match.
  bool allows_match_number_function_ = false;
  // Indicates whether or not this NameScope allows the MATCH_ROW_NUMBER() &
  // CLASSIFIER() functions, which are only available inside aggregate args of
  // measures in the MEASURES clause, as they vary per row.
  // If this is true, `allows_match_number_function_` must also be true.
  bool allows_match_row_functions_ = false;

  // Acts as a registry so that ExprResolutionInfo objects can subscribe to be
  // notified when a lateral column from their namescopes is referenced from a
  // subquery. This is needed to propagate properties such as
  // `has_aggregation`, `has_analytic`, etc. To the subquery, the outer
  // reference is just a constant, but the subquery expression itself needs to
  // retain these properties. For example:
  //   SELECT
  //      MAX(x) AS agg,
  //      (SELECT
  //          (SELECT MIN(agg) AS inner2 FROM t3) AS inner1
  //        FROM t2
  //      ) AS a,                     # `a` is considered as `has_aggregation`
  //      SUM(a) OVER() AS analytic   # No problem: agg in analytic
  //      MIN(a)                      # Error: agg inside agg.
  //   FROM t1;
  // Note that we need to track the correct level, since `has_agg` propagates
  // only to the expression on its same level.
  std::vector<ExprResolutionInfo*> lateral_column_reference_observers_;

  State state_;

  // Accessors for fields inside the CopyOnWrite state_.
  const IdStringHashMapCase<NameTarget>& names() const {
    return state_.names;
  }
  IdStringHashMapCase<NameTarget>* mutable_names() {
    return &state_.names;
  }
  const std::vector<ValueTableColumn>& value_table_columns() const {
    return state_.value_table_columns;
  }
  std::vector<ValueTableColumn>* mutable_value_table_columns() {
    return &state_.value_table_columns;
  }

  // These are used internally to optimize copying.
  bool IsEmpty() const;
  void CopyStateFrom(const NameScope& other);

  // Add a name to this scope.  A NameTarget is constructed for this object.
  //
  // Collisions are resolved as described above, with range variables overriding
  // columns. Overriding names in the underlying scope is always allowed.
  void AddRangeVariable(IdString name, const NameListPtr& scan_columns,
                        bool is_pattern_variable);

  ABSL_DEPRECATED("Inline me!")
  void AddRangeVariable(IdString name, const NameListPtr& scan_columns) {
    AddRangeVariable(name, scan_columns, /*is_pattern_variable=*/false);
  }

  void AddColumn(IdString name, const ResolvedColumn& column, bool is_explicit);

  void AddNameTarget(IdString name, const NameTarget& target);

  // Search for a field called <name> on any column in <value_table_columns_>.
  // Returns HAS_AMBIGUOUS_FIELD if <name> exists on multiple columns.
  Type::HasFieldResult LookupFieldTargetLocalOnly(
      IdString name, NameTarget* field_target) const;

  friend class NameList;
  FRIEND_TEST(NameScope, Test);
  FRIEND_TEST(NameScope, CorrelatedColumnsSet);
  FRIEND_TEST(NameScope, TestCreateNewRangeVariableTargetGivenValidNamePaths);
  FRIEND_TEST(NameScope, TestCreateNewLocalNameTargetsGivenValidNamePaths);
  FRIEND_TEST(NameScope, TestCreateNewValueTableColumnsGivenValidNamePaths);
  FRIEND_TEST(NameScope, TestCreateGetFieldTargetFromInvalidValueTableColumn);
  FRIEND_TEST(NameList, TestRangeVarCaseOverridesColumn);
};

// A NameList is an ordered list of visible column names produced by a scan.
// It corresponds to what will be visible in SELECT * or JOIN USING.
//
// The NameList also contains:
//   - The range variables visible for scans, which can be used in
//     SELECT alias.* or SELECT alias.column.
//   - The pseudo-columns visible coming out of this scan.
//     Order is not relevant for pseudo-columns since they never show up
//     in SELECT * or other ordered lists of outputs.
//
// Differences from a NameScope:
//   - NameLists are the ordered list of columns produced locally as part
//     of a FROM clause or SELECT list.
//   - NameScopes are unordered and just act like a map from names to
//     objects.
//   - NameScopes have hierarchy and can find names in parent scopes.
//     NameLists have no such hierarchy.
//
// As an implementation detail, for efficiency, the NameList is implemented
// using a NameScope inside it to store all names that can be resolved locally.
//
// Duplicate column names are not an error.  Lookups by name for duplicate
// columns will return ambiguous.  Duplicate range variables are not allowed
// and will give an error at insertion time.  (A NameList corresponds to one
// FROM clause and duplicate range variables are not allowed in the same FROM.)
//
// Value tables are added to the NameList as columns, but also act like range
// variables for the purpose of uniqueness checking.  For the purpose of
// resolution, and for loading into a NameScope, the value table alias is
// treated as a range variable (pointing at a value table NameList).
//
// Value tables may have a set of field names that are excluded from NameList
// lookups.  For instance, if a NameList includes value table column 't'
// with field 'f', and 'f' is in the field exclusion list of 't', then
// looking up 'f' in the NameList does not find it.  Additionally, if column
// 'f' also exists in the NameList as regular column then looking up 'f' in
// the NameList returns regular column 'f' - the look up is not ambiguous
// even though 'f' exists in 't' and would be ambiguous if 'f' was not in
// the field exclusion list.
//
// NameList also has an is_value_table() marker bit that is used on NameLists
// representing a query result to indicate that the query result is a
// value table.
//
// Several methods here take an <ast_location> argument and use that location
// for returned error messages (usually, name collisions).
class NameList {
 public:
  NameList() = default;
  NameList(const NameList&) = delete;
  NameList& operator=(const NameList&) = delete;
  ~NameList() = default;

  // Prepare this NameList for 'size' new columns. This is for efficiency
  // purposes only.
  void ReserveColumns(size_t size) { columns_.reserve(size); }

  // Add a named column.
  // <is_explicit> should be true if the alias for this column is an explicit
  // name. Generally, explicit names come directly from the query text, and
  // implicit names are those that are generated automatically from something
  // outside the query text, like column names that come from a table schema.
  // Explicitness does not change any scoping behavior except for the final
  // check in strict mode that may raise an error. For more information, please
  // see the beginning of (broken link).
  absl::Status AddColumn(IdString name, const ResolvedColumn& column,
                         bool is_explicit);

  // Adds a column for which an ID has not yet been allocated.
  // Such a column gets allocated upon the first reference.
  absl::Status AddDelayedColumn(IdString name, bool is_explicit,
                                DelayedColumnInfo delayed_column_info);
  absl::Status AddLoggingTarget(IdString name, bool is_explicit,
                                LoggingTargetInfo logging_target_info);

  // Add a column that stores the value produced by a value table scan,
  // and also a range variable that can be used to reference rows from the scan.
  //
  // If <pseudo_columns_name_list> is non-NULL, it provides a list of
  // pseudo-columns that should be usable via the new range variable.
  // <pseudo_columns_name_list> should have no other columns or range variables.
  // <pseudo_columns_name_list> can be the same NameList as <this>.
  //
  // <excluded_field_names> identifies a list of field names from the
  // value table that should be excluded from NameList lookups.  For example,
  // if 'f1' is on the excluded list for value table column 'vt', then a
  // lookup for name 'f1' will not find it.  But the excluded field can still
  // be found explicitly by resolving the path 'vt.f1'.  Also, star expansion
  // on the NameList does not include excluded fields, but 'rangevar.*'
  // expansion does include them.
  //
  // Lookups for <range_variable_name> will return the range variable, not the
  // column. The returned NameList will have is_value_table()=true.
  absl::Status AddValueTableColumn(
      IdString range_variable_name, const ResolvedColumn& column,
      const ASTNode* ast_location,
      const IdStringSetCase& excluded_field_names = {},
      const NameListPtr& pseudo_columns_name_list = nullptr);

  // This does either AddColumn or AddValueTableColumn (without the
  // optional args), depending on `is_value_table_column`.
  // `is_explicit` is unused if `is_value_table_column` is true.
  absl::Status AddColumnMaybeValueTable(IdString name,
                                        const ResolvedColumn& column,
                                        bool is_explicit,
                                        const ASTNode* ast_location,
                                        bool is_value_table_column);

  // Add a pseudo-column.  Pseudo-columns are always implicit.
  // They can be looked up by name but don't show up in columns().
  absl::Status AddPseudoColumn(IdString name, const ResolvedColumn& column,
                               const ASTNode* ast_location);

  // Add a range variable.  Returns an error if the name conflicts with another
  // range variable.
  // Adding a NameList to itself is not allowed and will fail.
  // Adding a value table NameList is not allowed and will fail.
  // Adding cycles of NameLists is also not allowed but will not be caught.
  absl::Status AddRangeVariable(
      IdString name, const std::shared_ptr<const NameList>& scan_columns,
      const ASTNode* ast_location);

  // Adds a Column with <name> twice to the NameList, ensuring its ambiguity.
  // Note that a range variable with the same name still takes precedence
  // over the ambiguous column.
  // This is intended for testing only.
  absl::Status AddAmbiguousColumn_Test(IdString name);

  // Options to customize behavior of NameList::MergeFrom.
  //
  // `excluded_field_names`, `columns_to_rename` and `columns_to_rename` are
  // mutually exclusive.
  struct MergeOptions {
    // If non-NULL, names in this list will be excluded.
    // Range variables with matching names are also excluded.
    // For value tables, this name gets added to their excluded_field_names,
    // so that field cannot be read implicitly.
    const IdStringSetCase* excluded_field_names = nullptr;

    // If non-NULL, names in this map will be replaced with the new
    // ResolvedColumn.  All matching names in the NameList will be replaced
    // with the new column.  All existing names in the scope (including
    // columns, pseudo-columns, range variables, ambiguous names, etc)
    // will be removed, and replaced by one new entry pointing at the column.
    // This also acts like `excluded_field_names` for other occurrences of
    // replaced name.
    typedef absl::flat_hash_map<IdString, ResolvedColumn, IdStringCaseHash,
                                IdStringCaseEqualFunc>
        ColumnsToReplaceMap;
    ColumnsToReplaceMap* columns_to_replace = nullptr;

    // If non-NULL, names in this map will be renamed to the map entry's value.
    // All matching names in the NameList will be renamed.
    // A matching name in the scope will be renamed (possibly resulting
    // in an ambiguous name if it collides with another name).
    // All renames are applied simultaneously, so swaps will work.
    // If this is present, `flatten_to_table` and `rename_value_table_to_name`
    // are not allowed.
    using ColumnsToRenameMap =
        absl::flat_hash_map<IdString, IdString, IdStringCaseHash,
                            IdStringCaseEqualFunc>;
    ColumnsToRenameMap* columns_to_rename = nullptr;

    // If true, the copied names are converted to be just a flat table.
    // Range variables are dropped, and value tables are converted to
    // regular columns.
    // Pseudo-columns are still preserved.
    // This is used when assigning a new range variable to a partial query
    // result, making the result act as if that result was a single table.
    // This is used when assigning an alias for a table subquery, and
    // would be used for parenthesized joins (if supported).  It is similar to
    // how a WITH subquery becomes a flat table when referenced.
    bool flatten_to_table = false;

    // If present, the NameList must be a value table.  This copies the
    // NameList, preserving the value table, but changing its alias.
    // Cannot be used with `flatten_to_table`.
    const IdString* rename_value_table_to_name = nullptr;
  };

  // Add all names from <other>, with modifications per <options>.
  // Returns an error if there are collisions in range variable names.
  absl::Status MergeFrom(const NameList& other, const ASTNode* ast_location);
  absl::Status MergeFrom(const NameList& other, const ASTNode* ast_location,
                         const MergeOptions& options);

  // Create a new NameList as a copy of this one, optionally applying
  // modifications described in `options`.
  // Copy without `options` cannot fail, but with `options` it can.
  // This is equivalent to allocating a new NameList and calling MergeFrom.
  // Note that these do not propagate `is_value_table`, since that may not
  // make sense if the new NameList gets mutated after copying.
  std::shared_ptr<NameList> Copy() const;
  absl::StatusOr<std::shared_ptr<NameList>> Copy(
      const ASTNode* ast_location, const MergeOptions& options) const;

  // Copy this NameList, like Copy() above, but also propagate `is_value_table`.
  std::shared_ptr<NameList> CopyWithIsValueTable() const;

  // Clone current NameList, invoking clone_column for each column to create new
  // columns.
  //
  // The <value_table_error> is used to produce a caller-context-specific error
  // message if the name list contains currently unsupported value table
  // columns.
  //
  // The <clone_column> function is invoked once for each column to be
  // cloned. Range variables and pseudo columns are not cloned.
  //
  // In practice, note that some <clone_column> function
  // implementations remember the mapping from the original
  // column to the cloned column.
  absl::StatusOr<std::shared_ptr<NameList>> CloneWithNewColumns(
      const ASTNode* ast_location, absl::string_view value_table_error,
      const ASTIdentifier* alias,
      std::function<ResolvedColumn(const ResolvedColumn&)> clone_column,
      IdStringPool* id_string_pool) const;

  // Clones range variables from `other_scope` to this name list, replacing
  // old columns in `other_scope` with new columns.
  // `column_map` maps old columns (by int column_id) to new ResolvedColumns.
  //
  // Old columns that are not in `column_map` will be dropped from the
  // NameLists under copied range variables.
  //
  // See NameScope::CloneRangeVariablesMapped() for more details.
  absl::Status CloneRangeVariablesFromNameScopeMapped(
      const NameScope& other_scope,
      const absl::flat_hash_map<int, ResolvedColumn>& column_map,
      const ASTNode& ast_location,
      const IdStringHashSetCase& excluded_range_variable_set) {
    return name_scope_.CloneRangeVariablesMapped(
        other_scope, column_map, ast_location, excluded_range_variable_set);
  }

  // Get the regular columns in this NameList.  Does not include pseudo-columns.
  int num_columns() const { return columns_.size(); }
  const std::vector<NamedColumn>& columns() const { return columns_; }
  const NamedColumn& column(int i) const { return columns_[i]; }

  // Return vector of ResolvedColumns contained in columns().
  std::vector<ResolvedColumn> GetResolvedColumns() const;

  // Return vector of Resolved pseudo-columns contained in columns().
  std::vector<ResolvedColumn> GetResolvedPseudoColumns() const;

  // Return vector of NamedColumns corresponding to the pseudo-columns in
  // local name scope.
  std::vector<NamedColumn> GetNamedPseudoColumns() const;

  // Return vector of column names.  Excludes pseudo-columns.
  std::vector<IdString> GetColumnNames() const;

  // Look up a name in this NameList.
  bool LookupName(IdString name, NameTarget* found) const;

  // Check whether a column name will show up in SELECT * expansion for this
  // NameList.  The return value indicates if this column name was not present,
  // was present exactly once, or was present multiple times.
  //
  // This looks for the same set of columns that will show up in SELECT *,
  // so it excludes range variables, pseudo-columns, and for value table
  // columns with fields, looks for field names rather than the column name.
  //
  // Please note that since SELECT * queries will never return special
  // pseudo-columns like has_ fields or named extensions, this function will
  // never return Type::HAS_PSEUDO_FIELD.
  Type::HasFieldResult SelectStarHasColumn(IdString name) const;

  // is_value_table indicates that this NameList represents a FROM clause that
  // produced a value table.  It has no effect on any lookup or mutation
  // behavior on either NameList or NameScope.  It is used by the Resolver
  // as a marker.  The NameList gives the set of columns produced by a query,
  // and this marker bit indicates that the query produces a value table.
  //
  // When this is true, the NameList must have exactly one column, and
  // it should be a value table column.  The column must be added with
  // AddValueTableColumn before calling SetIsValueTable.
  // Adding more columns after this will fail.
  absl::Status SetIsValueTable();
  bool is_value_table() const { return is_value_table_; }

  std::string DebugString(absl::string_view indent = absl::string_view()) const;

  // Describe the content of this NameList into a `reflection::ResultTable`.
  // This makes the proto-structured form of pipe DESCRIBE output.
  reflection::ResultTable Describe(ProductMode product_mode) const;

  // Describe the content of this NameList into a `reflection::TableAlias`.
  // This is used for the inner NameLists for range variables and CTEs.
  // This does not set the `name` field.
  void DescribeInto(reflection::TableAlias* table_alias,
                    bool include_pseudo_columns = true) const;

  bool HasRangeVariable(IdString name) const;

  bool HasValueTableColumns() const {
    return name_scope_.HasLocalValueTableColumns();
  }
  bool HasRangeVariables() const {
    return name_scope_.HasLocalRangeVariables();
  }

  // Add a range variable, using a wrapper NameList that gets the range
  // variable. Example:
  //   select ... from (select a,b,c) AS S
  // The subquery produces a NameList with [a,b,c].
  // To add the range variable S, we construct a NameList [a,b,c,S->[a,b,c]].
  // Adding S to the initial NameList would allow cyclic lookups like S.S.S.a.
  static absl::StatusOr<std::shared_ptr<NameList>>
  AddRangeVariableInWrappingNameList(
      IdString alias, const ASTNode* ast_location,
      std::shared_ptr<const NameList> original_name_list);

 private:
  bool is_value_table_ = false;

  // This is the vector of columns that will show up in SELECT *.
  // Some will be marked as value tables; those may be expanded further
  // during SELECT * to show their fields instead of the value itself.
  std::vector<NamedColumn> columns_;

  // This stores all resolvable names in the NameList, including range
  // variables and pseudo-columns, but excluding anonymous columns.
  // Duplicate names will be collapsed to one NameTarget, possibly indicating
  // the name is ambiguous.
  //
  // Range variables inside the NameScope always point at a NameList giving the
  // columns available inside the scan under that range variable.
  NameScope name_scope_;

  friend class NameScope;
  FRIEND_TEST(NameList, TestRangeVarCaseOverridesColumn);
};

// Simplifies creating a namelist for a scan. Supports the following:
// - Regular table
// - Value table (must have only one regular column)
// - Pseudo-columns
// - Table aliases
class NameListBuilder {
 public:
  explicit NameListBuilder(const ASTNode* ast_location)
      : ast_location_(ast_location) {}

  NameListBuilder& AddColumn(IdString name, ResolvedColumn column,
                             bool is_pseudo_column) {
    if (is_pseudo_column) {
      pseudo_columns_.push_back(
          {.name = name, .resolved_column = std::move(column)});
    } else {
      columns_.push_back({.name = name, .resolved_column = std::move(column)});
    }
    return *this;
  };

  // Sets this name list as a value table.
  NameListBuilder& SetValueTable() {
    is_value_table_ = true;
    return *this;
  }

  // Sets the user provided alias for this namelist.
  NameListBuilder& SetAlias(IdString alias) {
    alias_ = alias;
    return *this;
  }

  absl::StatusOr<std::shared_ptr<NameList>> Build();

 private:
  const ASTNode* ast_location_ = nullptr;

  struct Column {
    IdString name;
    ResolvedColumn resolved_column;
  };
  std::vector<Column> columns_;
  std::vector<Column> pseudo_columns_;

  bool is_value_table_ = false;

  // User specified alias for the scan.
  std::optional<IdString> alias_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_NAME_SCOPE_H_
