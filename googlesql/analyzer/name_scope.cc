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

#include "googlesql/analyzer/name_scope.h"

#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/common/reflection.pb.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/catalog_helper.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

STATIC_IDSTRING(kValueTableName, "$value");

// Returns "" or " (excluded_field_names F1, F2, ...)".
static std::string ExclusionsDebugString(
    const IdStringSetCase& excluded_field_names) {
  if (excluded_field_names.empty()) return "";
  return absl::StrCat(
      " (excluded_field_names ",
      absl::StrJoin(excluded_field_names, ", ", IdStringFormatter), ")");
}

std::string ValidNamePath::DebugString() const {
  return absl::StrCat(absl::StrJoin(name_path_, ".", IdStringFormatter), ":",
                      target_column_.DebugString());
}

// Includes a leading space if non-empty.
static std::string ValidNamePathListDebugString(
    const ValidNamePathList& valid_name_path_list) {
  std::string debug_string;
  if (!valid_name_path_list.empty()) {
    absl::StrAppend(&debug_string, " (");
    bool first = true;
    for (const ValidNamePath& valid_name_path : valid_name_path_list) {
      if (first) {
        first = false;
      } else {
        absl::StrAppend(&debug_string, ", ");
      }
      absl::StrAppend(&debug_string, "+", valid_name_path.DebugString());
    }
    absl::StrAppend(&debug_string, ")");
  }
  return debug_string;
}

ValidFieldInfoMap::~ValidFieldInfoMap() = default;

void ValidFieldInfoMap::InsertNamePath(const ResolvedColumn& column,
                                       const ValidNamePath& valid_name_path) {
  std::unique_ptr<ValidNamePathList>& valid_name_path_list =
      column_to_valid_name_paths_map_[column];
  if (valid_name_path_list == nullptr) {
    valid_name_path_list = std::make_unique<ValidNamePathList>();
  }
  valid_name_path_list->push_back(valid_name_path);
}

bool ValidFieldInfoMap::LookupNamePathList(
    const ResolvedColumn& column,
    const ValidNamePathList** valid_name_path_list) const {
  auto it = column_to_valid_name_paths_map_.find(column);
  if (it == column_to_valid_name_paths_map_.end()) {
    *valid_name_path_list = nullptr;
  } else {
    *valid_name_path_list = it->second.get();
  }
  return (*valid_name_path_list != nullptr);
}

bool ValidFieldInfoMap::ContainsColumnAndNamePathPrefix(
    const ResolvedColumn& column, const ValidNamePath& name_path) const {
  const ValidNamePathList* valid_name_path_list;
  if (!LookupNamePathList(column, &valid_name_path_list)) {
    return false;
  }

  ResolvedColumn unused_target_column;
  int unused_prefix_length;
  return FindLongestMatchingPathIfAny(
      *valid_name_path_list, name_path.name_path(), &unused_target_column,
      &unused_prefix_length);
}

void ValidFieldInfoMap::Clear() { column_to_valid_name_paths_map_.clear(); }

std::string ValidFieldInfoMap::DebugString(absl::string_view indent) const {
  std::string debug_string;
  for (const auto& entry : column_to_valid_name_paths_map_) {
    absl::StrAppend(&debug_string, indent, entry.first.DebugString(), ":",
                    ValidNamePathListDebugString(*entry.second), "\n");
  }
  return debug_string;
}

absl::Status NameTarget::SetupPrecomputedColumnIfNeeded(
    ColumnReferenceContext context) {
  GOOGLESQL_RET_CHECK(IsColumn()) << DebugString();
  if (!delayed_column_info_.has_value() ||
      delayed_column_info_->precompute_callback == nullptr) {
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(delayed_column_info_->precompute_callback != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      ResolvedColumn column,
      delayed_column_info_->precompute_callback(std::move(context)));
  column_ = column;

  // Make sure we do not call the callback again.
  // The NameTarget should just look like any other column now, except for the
  // extra info about its SelectColumnState.
  delayed_column_info_->precompute_callback = nullptr;

  return absl::OkStatus();
}

std::string NamedColumn::DebugString() const {
  return absl::StrCat(
      IsInternalAlias(name_) ? "<unnamed>" : ToIdentifierLiteral(name_), " ",
      column_.type()->ShortTypeName(ProductMode::PRODUCT_INTERNAL), " ",
      column_.DebugString(),
      (is_value_table_column_
           ? absl::StrCat(" (value table)",
                          ExclusionsDebugString(excluded_field_names_))
           : ""));
}

void NamedColumn::Describe(ProductMode product_mode,
                           reflection::Column* reflection_column) const {
  // Note: Pseudo-columns need to be populated with the same fields, but are
  // handled separately in NameScope::DescribeInto.
  if (is_value_table_column_) {
    // For value tables, set the column name to <value>.
    // Its name will be added later as the table_alias from the corresponding
    // range variable.
    reflection_column->set_column_name("<value>");
    // `excluded_field_names_` is not included here yet.
    reflection_column->set_is_value_table_column(true);
  } else if (!IsInternalAlias(name_)) {
    reflection_column->set_column_name(name_.ToString());
  }
  reflection_column->set_type(column_.type()->ShortTypeName(product_mode));
}

NameScope::NameScope(const NameScope* previous_scope,
                     const NameListPtr& name_list,
                     CorrelatedColumnsSet* correlated_columns_set)
    : previous_scope_(previous_scope),
      correlated_columns_set_(correlated_columns_set),
      // Copy state_ from name_list_.
      state_(name_list->name_scope_.state_) {}

NameScope::NameScope(const NameScope* previous_scope,
                     const IdStringHashMapCase<NameTarget>& name_targets,
                     const std::vector<ValueTableColumn>& value_table_columns,
                     CorrelatedColumnsSet* correlated_columns_set)
    : previous_scope_(previous_scope),
      correlated_columns_set_(correlated_columns_set) {
  // Copy state_ from the new name targets and value table columns.
  *mutable_names() = name_targets;
  *mutable_value_table_columns() = value_table_columns;
}

NameScope::NameScope(const NameList& name_list)
    // Copy state_ from name_list_.
    : state_(name_list.name_scope_.state_) {}

bool NameScope::IsEmpty() const {
  return names().empty() && value_table_columns().empty();
}

void NameScope::CopyStateFrom(const NameScope& other) {
  // This does CopyOnWrite.
  state_ = other.state_;
}

void NameScope::AddNameTarget(IdString name, const NameTarget& target) {
  ABSL_DCHECK(!name.empty()) << "Empty name not expected in NameScope";
  ABSL_DCHECK(!IsInternalAlias(name)) << "Internal names not expected in NameScope";

  // This is InsertOrReturnExisting, but we're not using the helper because
  // this code path is hot, and we we want to get out both the existence bit
  // and the iterator, which we can use in the erase() call below.
  auto /* pair<iter, is_found> */ insert_result =
      mutable_names()->emplace(name, target);  // emplace behaves like insert

  if (!insert_result.second) {  // Found existing value.
    NameTarget* existing = &insert_result.first->second;
    const bool existing_is_range_variable = existing->IsRangeVariable();
    const bool new_is_range_variable = target.IsRangeVariable();
    if (existing_is_range_variable == new_is_range_variable) {
      if (existing_is_range_variable) {
        // Duplicate range variables cannot happen through public interfaces
        // because NameLists will not allow adding duplicates.
        ABSL_LOG(ERROR) << "Cannot add duplicate table alias: " << name;
      } else {
        // Duplicate column names become ambiguous.
        existing->SetAmbiguous();
      }
      return;
    } else if (existing_is_range_variable) {
      // No-op because columns don't override range variables.
      return;
    } else {
      ABSL_DCHECK(new_is_range_variable);
      // Replace the old NameTarget with this one.
      // NOTE: We remove the old entry first because we don't want to inherit
      // its case, and if we just do update, the map key doesn't change.
      // This causes some weird output, like
      //   select value from KeyValue value, KeyValue value
      // will give the error
      //   Duplicate range variable Value in the same FROM clause
      // even though "Value" does not show up in the query.
      mutable_names()->erase(insert_result.first);  // Erase using iterator ptr.
      mutable_names()->emplace(name, target);
    }
  }
}

void NameScope::AddColumn(IdString name, const ResolvedColumn& column,
                          bool is_explicit) {
  AddNameTarget(name, NameTarget(column, is_explicit));
}

void NameScope::AddRangeVariable(IdString name, const NameListPtr& scan_columns,
                                 bool is_pattern_variable) {
  AddNameTarget(name, NameTarget(scan_columns, is_pattern_variable));
}

bool NameScope::LookupName(IdString name, NameTarget* found,
                           CorrelatedColumnsSetList* correlated_columns_sets,
                           const std::vector<ExprResolutionInfo*>**
                               out_lateral_column_reference_observers) const {
  if (correlated_columns_sets != nullptr) {
    correlated_columns_sets->clear();
  }
  if (out_lateral_column_reference_observers != nullptr) {
    *out_lateral_column_reference_observers = nullptr;
  }

  const NameScope* current = this;
  while (current != nullptr) {
    // Look for any matching names stored directly in this NameScope.
    const NameTarget* tmp = googlesql_base::FindOrNull(current->names(), name);

    // Look for matching field names on value_table_columns_ in this NameScope.
    // We can skip this if we already have a range variable or ambiguous name
    // because fields names will always be implicit and cannot override that.
    NameTarget field_target;
    if (tmp == nullptr || tmp->IsColumn()) {
      switch (current->LookupFieldTargetLocalOnly(name, &field_target)) {
        case Type::HAS_NO_FIELD:
          break;
        case Type::HAS_FIELD:
        case Type::HAS_PSEUDO_FIELD:
          if (tmp != nullptr) {
            found->SetAmbiguous();
            return true;
          } else {
            tmp = &field_target;
          }
          break;
        case Type::HAS_AMBIGUOUS_FIELD:
          found->SetAmbiguous();
          return true;
      }
    }

    if (tmp != nullptr) {
      *found = *tmp;

      if (!tmp->IsAmbiguous() && current != this) {
        // This is a correlated lookup.
        if (!current->allows_correlated_access_) {
          found->SetAccessError(tmp->kind(),
                                current->reason_to_disallow_correlated_access_,
                                /*is_terminal_error=*/true);
          return true;
        }

        // If we are collecting correlated_columns_sets, traverse from
        // <this> to <current> again, collecting all non-NULL
        // correlated_columns_set_ pointers we pass.
        if (correlated_columns_sets != nullptr) {
          const NameScope* scope = this;
          while (scope != current) {
            if (scope->correlated_columns_set_ != nullptr) {
              correlated_columns_sets->push_back(
                  scope->correlated_columns_set_);
            }
            scope = scope->previous_scope_;
          }
        }
      }
      // Collect the lateral reference observers for the target.
      // Those are the ExprResolutionInfo objects at the same level of `current`
      // which need to know that the current expression references this
      // NameTarget, even from within a subquery, in order to track properties
      // such as `has_aggregation`, etc.
      if (out_lateral_column_reference_observers != nullptr) {
        *out_lateral_column_reference_observers =
            &current->lateral_column_reference_observers_;
      }

      return true;
    }
    current = current->previous_scope_;
  }

  // We didn't find anything.
  found->SetAmbiguous();
  return false;
}

bool ValidFieldInfoMap::FindLongestMatchingPathIfAny(
    const ValidNamePathList& name_path_list,
    absl::Span<const IdString> path_names, ResolvedColumn* resolved_column,
    int* name_at) {
  bool found = false;
  *name_at = 0;
  for (const ValidNamePath& valid_name_path : name_path_list) {
    if (valid_name_path.name_path().size() > path_names.size() ||
        valid_name_path.name_path().size() < *name_at) {
      // The 'valid_name_path' is longer than 'path_names', or it
      // is shorter than the longest prefix so far, so it cannot be
      // the longest prefix.
      continue;
    }
    if (IdStringVectorHasPrefix<IdStringCaseEqualFunc>(
            path_names, valid_name_path.name_path())) {
      // The valid name path was a prefix of the requested 'path_names'.
      // We resolve the prefix name path to the target column and then
      // can access the remainder of the path names as field references
      // from the target column.
      *resolved_column = valid_name_path.target_column();
      *name_at = valid_name_path.name_path().size();
      found = true;
    }
  }
  return found;
}

static absl::Status LogNameHitAndFallbackToActualNameScope(
    const PathExpressionSpan& path_expr, const char* clause_name,
    const char* problem_string, bool in_strict_mode,
    CorrelatedColumnsSetList& correlated_columns_sets, int* num_names_consumed,
    const NameTarget& target,
    std::optional<IdString>& out_referenced_pattern_variable,
    const std::vector<ExprResolutionInfo*>*&
        out_lateral_column_reference_observers,
    NameTarget* target_out) {
  // We added this target only to trigger logging for this name hit.
  // We do not actually invoke the compute callback.
  GOOGLESQL_RET_CHECK(target.logging_target_info()->fallback_name_scope != nullptr);

  // This namescope intercepting the reference for logging might itself be in
  // the middle of a chain of parent namescopes, when looking up a correlated
  // name. We need to add any previous CorrelatedColumnsSets from the actual
  // calling namescope that were stored so far.
  CorrelatedColumnsSetList tmp_correlated_columns_sets;
  GOOGLESQL_RETURN_IF_ERROR(
      target.logging_target_info()->fallback_name_scope->LookupNamePath(
          path_expr, clause_name, problem_string, in_strict_mode,
          tmp_correlated_columns_sets, num_names_consumed,
          out_referenced_pattern_variable,
          out_lateral_column_reference_observers, target_out));
  GOOGLESQL_RET_CHECK(!target_out->delayed_column_info().has_value());
  GOOGLESQL_RET_CHECK(!target_out->logging_target_info().has_value());

  // `tmp_correlated_columns_sets` has the sets from the fallback namescope
  // to the top, but is missing those from the original child namescope, which
  // were recorded in `correlated_columns_sets` and stopped with the
  // intercepting namescope as the (intercepting) name appears non-correlated
  // in it.
  // See b/477296061 for more details.
  for (CorrelatedColumnsSet* correlated_columns_set :
       tmp_correlated_columns_sets) {
    correlated_columns_sets.push_back(correlated_columns_set);
  }

  return target.logging_target_info()->logging_callback(
      path_expr.first_name(), /*original_target=*/*target_out);
}

const char* const NameScope::kDefaultProblemString =
    "neither grouped nor aggregated";

absl::Status NameScope::LookupNamePath(
    const PathExpressionSpan& path_expr, const char* clause_name,
    const char* problem_string, bool in_strict_mode,
    CorrelatedColumnsSetList& correlated_columns_sets, int* num_names_consumed,
    std::optional<IdString>& out_referenced_pattern_variable,
    const std::vector<ExprResolutionInfo*>*&
        out_lateral_column_reference_observers,
    NameTarget* target_out) const {
  const IdString first_name = path_expr.GetFirstIdString();

  *num_names_consumed = 0;
  // First we try resolving path_expr using name_scope.
  NameTarget target;
  // This call to LookupName() populates correlated_columns_sets if
  // the lookup resolves to a correlated column.
  if (LookupName(first_name, &target, &correlated_columns_sets,
                 &out_lateral_column_reference_observers)) {
    if (in_strict_mode && target.IsImplicit()) {
      return MakeSqlErrorAt(path_expr.first_name())
             << "Alias " << ToIdentifierLiteral(first_name)
             << " cannot be used without a qualifier in strict name "
                "resolution mode";
    }

    auto make_correlated_pattern_variable_ref_error = [&path_expr]() {
      return MakeSqlErrorAt(path_expr.first_name())
             << "Pattern variables can only be referenced in the same "
             << "MATCH_RECOGNIZE clause defining them directly without "
             << "subqueries";
    };

    switch (target.kind()) {
      case NameTarget::RANGE_VARIABLE: {
        if (target.is_pattern_variable()) {
          if (!correlated_columns_sets.empty()) {
            return make_correlated_pattern_variable_ref_error();
          }
          out_referenced_pattern_variable = first_name;
        }
        const NameList& scan_columns = *target.scan_columns();
        if (path_expr.num_names() >= 2) {
          // We have at least two identifiers and the first is a scan alias.
          // The second identifier must be a column in that scan to be valid.
          const IdString dot_name = path_expr.name(1)->GetAsIdString();
          NameTarget found_column;
          if (!scan_columns.LookupName(dot_name, &found_column)) {
            if (scan_columns.is_value_table()) {
              // We'll get a better error message by returning the current
              // target and letting the caller try to resolve the next
              // field name from this target.
              *target_out = target;
              *num_names_consumed = 1;
              break;
            }
            return MakeSqlErrorAt(path_expr.name(1))
                   << "Name " << dot_name << " not found inside " << first_name;
          }
          switch (found_column.kind()) {
            case NameTarget::ACCESS_ERROR: {
              // This should never happen.  If we correctly resolve the
              // first name in the path to a range variable, then accessing
              // its fields is always valid.  Field access can only be an
              // error if accessing the range variable is an error.
              GOOGLESQL_RET_CHECK_FAIL() << "Unexpected NameTarget kind ACCESS_ERROR "
                               << " for field";
            }
            case NameTarget::AMBIGUOUS:
              return MakeSqlErrorAt(path_expr.name(1))
                     << "Name " << dot_name << " is ambiguous inside "
                     << first_name;
            case NameTarget::RANGE_VARIABLE:
              GOOGLESQL_RET_CHECK(!target.is_pattern_variable())
                  << "Pattern variable " << dot_name
                  << " cannot be referenced from alias " << first_name;
              return MakeSqlErrorAt(path_expr.first_name())
                     << "Name " << first_name << "." << dot_name
                     << " is a table alias, but a column was expected";
            case NameTarget::FIELD_OF:
              if (scan_columns.is_value_table()) {
                // We found a field of a value table.  Return the range
                // variable NameTarget and let the caller re-resolve the
                // field reference.
                // TODO: I'm not sure why we return the range
                // variable NameTarget and require the caller to re-resolve
                // the second name, but it is what the original code did.
                // Returning the 'found_column' FIELD_OF NameTarget and
                // setting num_names_consumed = 2 does not work as I
                // would normally expect.  Figure out why not.
                *target_out = target;
                *num_names_consumed = 1;
                break;
              } else {
                // This should be impossible.  The only case where we'll have
                // a path A.B where B can resolve as a FIELD_OF is if A is
                // a range variable for a value table scan.  We don't have
                // any cases where a range variable introduced in a FROM
                // clause contains value table columns but is not a value
                // table range variable itself.
                return MakeSqlErrorAt(path_expr.first_name())
                       << "Name " << first_name << "." << dot_name
                       << " is a value table field, but a column was expected";
              }
            case NameTarget::EXPLICIT_COLUMN:
            case NameTarget::IMPLICIT_COLUMN: {
              // This is the only allowed case, where we have 'rangevar.col'.
              // Return the target, and set 'num_names_consumed' to indicate
              // two names of the path have been consumed.
              *target_out = found_column;
              *num_names_consumed = 2;
              break;
            }
          }
        } else {
          // We have only one identifier and it is a range variable,
          // so return the target and set 'num_names_consumed'.
          *target_out = target;
          *num_names_consumed = 1;
        }
        break;
      }

      case NameTarget::ACCESS_ERROR: {
        if (target.is_terminal_error()) {
          // Currently we only hit this path when doing a correlated access on
          // a parent namescope that disallows correlated lookups, the message
          // is set in all such situations so far.
          GOOGLESQL_RET_CHECK(!target.access_error_message().empty());

          return MakeSqlErrorAt(path_expr.first_name())
                 << target.access_error_message();
        }

        // The name exists but accessing it is an error.  However,
        // if we are accessing fields from this name then check to
        // see if the field access is valid.
        // But first, make a note of any pattern variable access along the path
        if (NameTarget::IsRangeVariableKind(target.original_kind()) &&
            target.is_pattern_variable()) {
          if (!correlated_columns_sets.empty()) {
            return make_correlated_pattern_variable_ref_error();
          }
          out_referenced_pattern_variable = first_name;
        }

        if (path_expr.num_names() >= 2) {
          std::vector<IdString> path_names;

          for (int idx = 1; idx < path_expr.num_names(); ++idx) {
            const ASTIdentifier* identifier = path_expr.name(idx);
            path_names.push_back(identifier->GetAsIdString());
          }

          ResolvedColumn resolved_column;
          if (!ValidFieldInfoMap::FindLongestMatchingPathIfAny(
                  target.valid_name_path_list(), path_names, &resolved_column,
                  num_names_consumed)) {
            if (!target.access_error_message().empty()) {
              return MakeSqlErrorAt(path_expr.first_name())
                     << target.access_error_message();
            }
            return MakeSqlErrorAt(path_expr.first_name())
                   << (strlen(clause_name) == 0 ? "An" : clause_name)
                   << " expression references "
                   << path_expr.ToIdentifierPathString() << " which is "
                   << problem_string;
          }
          // Build a new name target for the ResolvedColumn that matched
          // the longest path, and update 'num_names_consumed' to identify
          // the part of the name that resolved. 'is_explicit' is set to true
          // here because the name for 'resolved_column' comes directly from the
          // query text.
          *target_out = NameTarget(resolved_column, true /* is_explicit */);
          (*num_names_consumed)++;
        } else {
          // Accessing the column or range variable name is invalid.
          if (!target.access_error_message().empty()) {
            return MakeSqlErrorAt(path_expr.first_name())
                   << target.access_error_message();
          }
          if (target.IsRangeVariableKind(target.original_kind())) {
            // However, for range variables the caveat is that if all of its
            // fields are valid to access then we could reconstruct the
            // range variable struct from its fields so this could be ok.
            // TODO: Add support for this if/when needed.  This
            // used to work originally, but was disabled during refactoring
            // and apparently no one used it because nothing broke.  If
            // there is no interest over time, we can delete this case and
            // remove this TODO.
          }
          return MakeSqlErrorAt(path_expr.first_name())
                 << (strlen(clause_name) == 0 ? "An" : clause_name)
                 << " expression references "
                 // TODO: We currently model array offset references
                 // as value tables, so we incorrectly label those as
                 // 'range variable' in error messages.  Fix this.
                 << (target.IsRangeVariableKind(target.original_kind())
                         ? "table alias "
                         : "column ")
                 << path_expr.ToIdentifierPathString() << " which is "
                 << problem_string;
        }
        break;
      }

      case NameTarget::AMBIGUOUS: {
        const auto& logging_target_info = target.logging_target_info();
        if (logging_target_info.has_value()) {
          return LogNameHitAndFallbackToActualNameScope(
              path_expr, clause_name, problem_string, in_strict_mode,
              correlated_columns_sets, num_names_consumed, target,
              out_referenced_pattern_variable,
              out_lateral_column_reference_observers, target_out);
        }
        return MakeSqlErrorAt(path_expr.first_name())
               << "Column name " << ToIdentifierLiteral(first_name)
               << " is ambiguous";
      }
      case NameTarget::EXPLICIT_COLUMN:
      case NameTarget::IMPLICIT_COLUMN: {
        // The first name is a column, so return the target.
        const auto& logging_target_info = target.logging_target_info();
        if (logging_target_info.has_value()) {
          return LogNameHitAndFallbackToActualNameScope(
              path_expr, clause_name, problem_string, in_strict_mode,
              correlated_columns_sets, num_names_consumed, target,
              out_referenced_pattern_variable,
              out_lateral_column_reference_observers, target_out);
        }
        *target_out = target;
        *num_names_consumed = 1;
        break;
      }

      case NameTarget::FIELD_OF:
        // The first name is a field of the target column, just
        // return the target.
        //
        // Note: this code executes when referencing a column of
        // a value table.  For example, if ValueTable has field 'a':
        //
        // SELECT a.b.c
        // FROM ValueTable;
        *target_out = target;
        *num_names_consumed = 1;
        break;
    }

    // This branch is for when the first name was found in scope, so we
    // should have resolved at least one name.
    GOOGLESQL_RET_CHECK_GE(*num_names_consumed, 1);
  }

  // We resolved part of the path without error, as indicated by
  // 'num_names_consumed' (which could be 0).  Return successfully to
  // the caller, who must resolve the remaining names of the path
  // itself, as appropriate.  We don't do any further (nested) field
  // analysis here.

  // We only return OK if 1) we didn't resolve any part of the name, or
  // 2) we did resolve part of the name and the returned NameTarget is
  // valid to reference and is not an error target.  If we resolved part
  // of the name to an error target, then we already returned an error
  // status above.
  GOOGLESQL_RET_CHECK(*num_names_consumed == 0 || target_out->IsRangeVariable() ||
            target_out->IsColumn() || target_out->IsFieldOf());

  return absl::OkStatus();
}

// static
absl::Status NameScope::CreateGetFieldTargetFromInvalidValueTableColumn(
    const ValueTableColumn& value_table_column, IdString field_name,
    NameTarget* field_target) {
  GOOGLESQL_RET_CHECK(!value_table_column.is_valid_to_access())
      << value_table_column.DebugString();
  field_target->SetAccessError(NameTarget::FIELD_OF);
  for (const ValidNamePath& valid_name_path :
       value_table_column.valid_name_path_list()) {
    if (valid_name_path.name_path().empty()) {
      continue;
    }
    if (IdStringCaseEqual(valid_name_path.name_path()[0], field_name)) {
      if (valid_name_path.name_path().size() == 1) {
        // Accessing this field is valid, and the name resolves to
        // the specified column.  The returned NameTarget is marked as
        // 'is_explicit' because this column access is the result of
        // having a ValidNamePath entry, and such entries are only
        // constructed for fields actually present in the query.
        *field_target =
            NameTarget(valid_name_path.target_column(), true /* is_explicit */);
        break;
      } else {
        // There is a field path starting at this field that
        // is valid to access.  Collect that sub-path information
        // to attach it to the returned error target.  The caller
        // can then resolve the rest of its name path (if any)
        // against the returned NameTarget.
        // TODO: Maybe use an absl::Span here.
        std::vector<IdString> sub_name_path(
            ++valid_name_path.name_path().begin(),
            valid_name_path.name_path().end());
        ValidNamePath new_name_path(
            {sub_name_path, valid_name_path.target_column()});
        field_target->mutable_valid_name_path_list()->emplace_back(
            new_name_path);
      }
    }
  }
  return absl::OkStatus();
}

Type::HasFieldResult NameScope::LookupFieldTargetLocalOnly(
    IdString name, NameTarget* field_target) const {
  int found_count = 0;
  Type::HasFieldResult result = Type::HAS_NO_FIELD;
  for (const ValueTableColumn& value_table_column : value_table_columns()) {
    int field_id = -1;
    if (googlesql_base::ContainsKey(value_table_column.excluded_field_names(), name)) {
      // If this name is excluded from lookups for this value table then ignore
      // it.
      continue;
    }
    Type::HasFieldResult has_field =
        value_table_column.column().type()->HasField(name.ToString(),
                                                     &field_id);
    switch (has_field) {
      case Type::HAS_NO_FIELD:
        break;
      case Type::HAS_FIELD:
      case Type::HAS_PSEUDO_FIELD:
        result = has_field;
        ++found_count;
        if (value_table_column.is_valid_to_access()) {
          // Accessing the value table range variable is valid, so
          // accessing its fields is valid.
          *field_target = NameTarget(value_table_column.column(), field_id);
        } else {
          // Accessing the value table range variable directly is not valid,
          // but accessing some of its fields might be as indicated
          // by the ValueTableColumn's ValidFieldInfo.
          //
          // We must return a NameTarget that represents this field,
          // and valid field paths from it are derived from the value
          // table column's field paths but with the first name
          // removed (since that first name is the name of this field).
          //
          // For example, if the input argument 'name' is "a", and
          // the value table column has name path "a.b.c", then
          // we return an error NameTarget for "a" with valid name
          // path of "b.c" that can be accessed from "a".
          //
          // ABSL_CHECK validated: !value_table_column.is_valid_to_access.
          GOOGLESQL_CHECK_OK(CreateGetFieldTargetFromInvalidValueTableColumn(
              value_table_column, name, field_target));
        }
        break;
      case Type::HAS_AMBIGUOUS_FIELD:
        found_count += 2;  // Enough to be ambiguous.
        break;
    }
  }
  if (found_count > 1) {
    field_target->SetAmbiguous();
    result = Type::HAS_AMBIGUOUS_FIELD;
  } else if (found_count == 1) {
    ABSL_DCHECK(result == Type::HAS_FIELD || result == Type::HAS_PSEUDO_FIELD);
  } else {
    ABSL_DCHECK_EQ(found_count, 0);
    ABSL_DCHECK_EQ(result, Type::HAS_NO_FIELD);
  }
  return result;
}

absl::Status NameScope::CloneRangeVariablesMapped(
    const NameScope& other_scope,
    const absl::flat_hash_map<int, ResolvedColumn>& column_map,
    const ASTNode& ast_location,
    const IdStringHashSetCase& excluded_range_variable_set) {
  for (const auto& [name, target] : other_scope.names()) {
    if (!target.IsRangeVariable() ||
        excluded_range_variable_set.contains(name)) {
      continue;
    }

    auto range_var = std::make_shared<NameList>();
    for (const NamedColumn& column : target.scan_columns()->columns()) {
      const auto& iter = column_map.find(column.column().column_id());
      if (iter == column_map.end()) continue;
      const ResolvedColumn& new_column = iter->second;
      GOOGLESQL_RETURN_IF_ERROR(
          range_var->AddColumn(column.name(), new_column, target.IsExplicit()));
    }

    // Note that this can add a range variable containing an empty column list.
    this->AddRangeVariable(name, range_var, /*is_pattern_variable=*/false);
  }

  return absl::OkStatus();
}

bool NameScope::HasName(IdString name) const {
  NameTarget found;
  return LookupName(name, &found);
}

std::string NameScope::SuggestName(IdString mistyped_name) const {
  std::vector<std::string> possible_names;
  for (const auto& [name, target] : names()) {
    if (target.logging_target_info().has_value()) {
      // This target is here only for logging purposes. It should not be seen as
      // actually available.
      ABSL_DCHECK(target.logging_target_info()->fallback_name_scope != nullptr);
      ABSL_DCHECK(!target.delayed_column_info().has_value());
      continue;
    }
    possible_names.push_back(name.ToString());
  }
  return ClosestName(mistyped_name.ToString(), possible_names);
}

bool NameScope::HasLocalRangeVariables() const {
  for (const auto& name_and_target : names()) {
    if (name_and_target.second.IsRangeVariable()) {
      return true;
    }
  }
  return false;
}

std::string NameScope::ValueTableColumn::DebugString() const {
  std::string out;
  absl::StrAppend(&out, column_.DebugString(),
                  ExclusionsDebugString(excluded_field_names_),
                  (is_valid_to_access_ ? "" : " ACCESS_INVALID"),
                  ValidNamePathListDebugString(valid_name_path_list_));
  return out;
}

// `name_list_columns` is the set of columns present in the NameList.
// These are assumed to not be pseudo-columns.
std::string NameScope::DebugString(
    absl::string_view indent, const IdStringSetCase* name_list_columns) const {
  // Sort the output lines for the hash_set to make the output deterministic,
  // and split a separate list for range variables.
  std::vector<std::string> name_lines;
  std::vector<std::string> rv_name_lines;
  for (const auto& name : names()) {
    std::string line = absl::StrCat(name.first.ToStringView(), " -> ",
                                    name.second.DebugString());
    if (name.second.IsRangeVariable()) {
      rv_name_lines.push_back(line);
    } else {
      if (name_list_columns != nullptr && name.second.IsColumn() &&
          !googlesql_base::ContainsKey(*name_list_columns, name.first)) {
        absl::StrAppend(&line, " (pseudo-column)");
      }
      name_lines.push_back(line);
    }
  }
  std::sort(name_lines.begin(), name_lines.end());
  std::sort(rv_name_lines.begin(), rv_name_lines.end());

  std::string out;
  if (!name_lines.empty()) {
    absl::StrAppend(&out, indent, "Names:");
    for (const auto& name_line : name_lines) {
      absl::StrAppend(&out, "\n", indent, "  ", name_line);
    }
  }
  if (!rv_name_lines.empty()) {
    if (!out.empty()) out += "\n";
    absl::StrAppend(&out, indent, "Range variables:");
    for (const auto& name_line : rv_name_lines) {
      absl::StrAppend(&out, "\n", indent, "  ", name_line);
    }
  }
  if (!value_table_columns().empty()) {
    if (!out.empty()) out += "\n";
    absl::StrAppend(&out, indent, "Value table columns:");
    for (const ValueTableColumn& value_table_column : value_table_columns()) {
      absl::StrAppend(&out, "\n", indent, "  ",
                      value_table_column.DebugString());
    }
  }
  if (previous_scope_ != nullptr) {
    if (!out.empty()) out += "\n";
    absl::StrAppend(&out, indent, "Parent scope:\n",
                    previous_scope_->DebugString(absl::StrCat(indent, "  ")));
  }

  if (!allows_correlated_access_) {
    absl::StrAppend(&out, "\n", indent, "NOTE: Disallows correlated access!\n");
  }
  return out;
}

void NameScope::DescribeInto(
    reflection::ResultTable* result_table,
    const IdStringHashMapCase<int>& name_list_column_names_count,
    const absl::flat_hash_map<ResolvedColumn, std::vector<int>>&
        name_list_columns_map) const {
  // Track the pseudo-columns added so far.
  absl::flat_hash_set<ResolvedColumn> pseudo_columns_added;
  // Track all column names added, adding in pseudo-column names.
  IdStringHashMapCase<int> all_column_names_count =
      name_list_column_names_count;

  // The first pass over NameTargets handles range variables.
  for (const auto& name_pair : names()) {
    const IdString& name = name_pair.first;
    const NameTarget& name_target = name_pair.second;

    if (name_target.IsRangeVariable()) {
      // Handle range variables ("table aliases" in the proto).
      reflection::TableAlias* table_alias = result_table->add_table_alias();
      table_alias->set_name(name.ToString());
      name_target.scan_columns()->DescribeInto(table_alias);

      // Handle columns under the range variable.
      for (const NamedColumn& named_column :
           name_target.scan_columns()->columns()) {
        const IdString& column_name = named_column.name();

        // Try attaching this table alias to a NameList column.
        // If any ResolvedColumn is uniquely pointed to by one range variable,
        // that range variable will be its reported table alias.
        const ResolvedColumn& column = named_column.column();
        if (name_list_columns_map.contains(column)) {
          for (int index : name_list_columns_map.at(column)) {
            ABSL_DCHECK_LT(index, result_table->column_size());
            reflection::Column* reflection_column =
                result_table->mutable_column(index);
            if (!googlesql_base::StringViewCaseEqual()(
                    reflection_column->column_name(), column_name.ToString()) &&
                !IsInternalAlias(column_name)) {
              // The NameList column name doesn't match the name under the
              // range variable, so don't show the alias on that column.
              // We still show table aliases for anonymous columns, which will
              // have column name "<unnamed>".
              // We compare names case insensitively mainly because of the
              // JOIN USING case, where the output column name uses the case
              // written in USING, which might not match.  But it probably
              // still makes sense to show the table alias attached.
              continue;
            }

            if (!reflection_column->has_table_alias()) {
              reflection_column->set_table_alias(name.ToString());
            } else {
              // We found a second table alias pointing at the same column.
              // We'll just leave the first one.
              // There's no test for this branch because I'm not aware of any
              // case that makes a NameScope like this.
            }
          }
        }
      }

      // Handle pseudo-columns under the range variable.
      for (const NamedColumn& named_column :
           name_target.scan_columns()->GetNamedPseudoColumns()) {
        const IdString& column_name = named_column.name();
        const ResolvedColumn& column = named_column.column();

        // This mirrors NamedColumn::Describe that handles non-pseudo-columns.
        reflection::Column* reflection_column =
            result_table->add_pseudo_column();
        reflection_column->set_column_name(column_name.ToString());
        reflection_column->set_table_alias(name.ToString());
        reflection_column->set_type(
            column.type()->ShortTypeName(ProductMode::PRODUCT_INTERNAL));

        pseudo_columns_added.insert(column);
        ++all_column_names_count[column_name];
      }
    }
  }

  // Do a second pass over NameTargets to find any pseudo-columns in the
  // top-level NameScope that weren't found under a range variable above.
  for (const auto& name_pair : names()) {
    const IdString& name = name_pair.first;
    const NameTarget& name_target = name_pair.second;

    if (name_target.IsColumn()) {
      const ResolvedColumn column = name_target.column();
      // If a NameScope column name didn't occur in the NameList, it must be
      // a pseudo-column.
      if (!IsInternalAlias(name) && !pseudo_columns_added.contains(column) &&
          !name_list_column_names_count.contains(name)) {
        // This mirrors NamedColumn::Describe that handles non-pseudo-columns.
        reflection::Column* reflection_column =
            result_table->add_pseudo_column();
        reflection_column->set_column_name(name.ToString());
        reflection_column->set_type(
            column.type()->ShortTypeName(ProductMode::PRODUCT_INTERNAL));
      }
    } else if (name_target.IsAmbiguous()) {
      // If we have an ambiguous name marker, include that under the
      // pseudo-columns list with <ambiguous> in the type.
      // This seems like a reasonable reflection of why this name isn't
      // queryable, without making a separate list of ambiguous column names.

      // If the name was in the column list twice already, we don't need to
      // show the <ambiguous> entry in the pseudo-columns.
      if (googlesql_base::FindWithDefault(all_column_names_count, name, 0) <= 1) {
        reflection::Column* reflection_column =
            result_table->add_pseudo_column();
        reflection_column->set_column_name(name.ToString());
        reflection_column->set_type("<ambiguous>");
      }
    }
  }

  // Sort the lists to make the output deterministic.
  // Constraints on NameScope mean these lists cannot have duplicate names.
  std::sort(
      result_table->mutable_table_alias()->pointer_begin(),
      result_table->mutable_table_alias()->pointer_end(),
      [](const reflection::TableAlias* a, const reflection::TableAlias* b) {
        return googlesql_base::CaseLess()(a->name(), b->name());
      });
  std::sort(result_table->mutable_pseudo_column()->pointer_begin(),
            result_table->mutable_pseudo_column()->pointer_end(),
            [](const reflection::Column* a, const reflection::Column* b) {
              int comp = googlesql_base::CaseCompare(a->table_alias(),
                                                              b->table_alias());
              if (comp != 0) return comp < 0;
              return googlesql_base::CaseLess()(a->column_name(),
                                                         b->column_name());
            });
}

void NameScope::InsertNameTargetsIfNotPresent(
    const IdStringHashMapCase<NameTarget>& names) {
  for (auto name_and_target = names.begin(); name_and_target != names.end();
       ++name_and_target) {
    googlesql_base::InsertIfNotPresent(mutable_names(), name_and_target->first,
                            name_and_target->second);
  }
}

void NameScope::ExcludeNameFromValueTableIfPresent(
    IdString name, ValueTableColumn* value_table_column) {
  if (googlesql_base::ContainsKey(value_table_column->excluded_field_names(), name)) {
    // This name is already excluded from lookups for this value table.
    return;
  }
  int field_id = -1;
  switch (value_table_column->column().type()->HasField(name.ToString(),
                                                        &field_id)) {
    case Type::HAS_NO_FIELD:
      // The field does not exist, so we do not need to exclude it.
      break;
    case Type::HAS_FIELD:
    case Type::HAS_PSEUDO_FIELD:
    case Type::HAS_AMBIGUOUS_FIELD:
      // The value table column has a field with this name so exclude it.
      value_table_column->excluded_field_names().insert(name);
      break;
  }
}

absl::Status NameScope::CopyNameScopeWithOverridingNames(
    const std::shared_ptr<NameList>& namelist_with_overriding_names,
    std::unique_ptr<NameScope>* scope_with_new_names) const {
  // The namelist_with_overriding_names cannot currently include
  // value table columns, range variables, or pseudocolumns.
  GOOGLESQL_RET_CHECK(!namelist_with_overriding_names->HasValueTableColumns());
  GOOGLESQL_RET_CHECK(!namelist_with_overriding_names->HasRangeVariables());

  // We will merge the existing scope's local names with the new
  // NameList, and have new names override the old ones (rather
  // than making them ambiguous).
  //
  // Note that some of the names in 'namelist_with_overriding_names' can
  // themselves be ambiguous.
  *scope_with_new_names = std::make_unique<NameScope>(
      previous_scope_, namelist_with_overriding_names, correlated_columns_set_);
  (*scope_with_new_names)->InsertNameTargetsIfNotPresent(names());

  // Add the existing value table entries into the new NameScope.  Note that
  // we have to exclude any names from value table range variables if they
  // appear in the new NameList, to allow the new NameList names to override
  // the value table names without ambiguity.
  for (const ValueTableColumn& value_table : value_table_columns()) {
    ValueTableColumn new_value_table = value_table;
    for (const IdString name :
         namelist_with_overriding_names->GetColumnNames()) {
      // Since we want the new NameList name to override other names from
      // within the NameScope, if 'name' exists as a 'new_value_table'
      // field name then we must exclude this name from the value table
      // column.  Otherwise a name lookup would result as ambiguous.
      ExcludeNameFromValueTableIfPresent(name, &new_value_table);
    }
    (*scope_with_new_names)
        ->mutable_value_table_columns()
        ->push_back(new_value_table);
  }
  return absl::OkStatus();
}

absl::Status NameScope::CopyNameScopeWithOverridingNameTargets(
    const IdStringHashMapCase<NameTarget>& overriding_name_targets,
    std::unique_ptr<NameScope>* scope_with_new_names) const {
  // We will merge this NameScope's local names with the new NameTargets,
  // where the new NameTargets override the current names (rather
  // than making conflicting names ambiguous).
  //
  // Note that some of the NameTargets in 'overriding_name_targets' can
  // themselves be error targets or ambiguous.
  scope_with_new_names->reset(
      new NameScope(previous_scope_, overriding_name_targets,
                    /*value_table_columns=*/{}, correlated_columns_set_));
  (*scope_with_new_names)->InsertNameTargetsIfNotPresent(names());

  // Add the existing value table entries into the new NameScope.
  // Note that we have to exclude any names from value table range
  // variables if they appear in the 'overriding_name_targets', to
  // allow the 'overriding_name_targets' names to override the value
  // table names without ambiguity.
  for (const ValueTableColumn& value_table : value_table_columns()) {
    ValueTableColumn new_value_table = value_table;
    for (const auto& entry : overriding_name_targets) {
      const IdString name = entry.first;
      ExcludeNameFromValueTableIfPresent(name, &new_value_table);
    }
    (*scope_with_new_names)
        ->mutable_value_table_columns()
        ->push_back(new_value_table);
  }
  return absl::OkStatus();
}

// Tries to find an entry in 'valid_field_info_map' whose 'source_column'
// matches 'original_column' and whose 'name_path' is empty.  If so, then
// sets 'new_target_column' to the 'valid_field_info_map' entry's
// 'target_column' and returns true.
//
// Otherwise populates 'related_valid_name_path_list' with entries
// from 'valid_field_info_map' that are related to 'original_column'
// (whose 'source_column' == 'original_column') and returns false.
static bool GetNewResolvedColumnOrRelatedValidNamePathList(
    const ResolvedColumn& original_column,
    const ValidFieldInfoMap& valid_field_info_map,
    ResolvedColumn* new_target_column,
    ValidNamePathList* related_valid_name_path_list) {
  *new_target_column = ResolvedColumn();
  related_valid_name_path_list->clear();
  const ValidNamePathList* original_column_name_path_list;
  if (valid_field_info_map.LookupNamePathList(
          original_column, &original_column_name_path_list)) {
    for (const ValidNamePath& valid_name_path :
         *original_column_name_path_list) {
      if (valid_name_path.name_path().empty()) {
        // We map this column to a new ResolvedColumn as
        // indicated by 'valid_name_path.target_column'.
        //
        // For example, this occurs when using this function to
        // build a post-GROUP BY NameScope and we unnest an array
        // and group by its range variable.
        // For example:
        //
        //   SELECT m
        //   FROM UNNEST([1, 2, 3]) m
        //   GROUP BY m;
        *new_target_column = valid_name_path.target_column();
        related_valid_name_path_list->clear();
        return true;
      }
      related_valid_name_path_list->push_back(valid_name_path);
    }
  }
  return false;
}

void NameScope::CreateNewValueTableColumnsGivenValidNamePaths(
    const ValidFieldInfoMap& valid_field_info_map_in,
    std::vector<ValueTableColumn>* new_value_table_columns) const {
  // Iterate through the value table columns, and produce updated value
  // table columns based on the 'valid_field_info_map_in'.
  for (const ValueTableColumn& value_table_column : value_table_columns()) {
    const ResolvedColumn& original_value_table_column =
        value_table_column.column();

    ResolvedColumn new_range_variable_column;
    ValidNamePathList new_name_path_list;
    ValueTableColumn new_value_table_column;
    if (GetNewResolvedColumnOrRelatedValidNamePathList(
            original_value_table_column, valid_field_info_map_in,
            &new_range_variable_column, &new_name_path_list)) {
      // The value table column name is valid to access directly.  It
      // resolves to an updated version of that column.  All
      // fields are therefore accessible from this range variable
      // and we do not need to populate the 'valid_field_info_map'.
      new_value_table_column = ValueTableColumn(
          new_range_variable_column, value_table_column.excluded_field_names(),
          true /* is_valid_to_access */, {} /* valid_field_info_map */);
    } else {
      // The value table column name is not valid to access directly,
      // but its fields might be.
      new_value_table_column =
          ValueTableColumn(original_value_table_column,
                           value_table_column.excluded_field_names(),
                           false /* is_valid_to_access */, new_name_path_list);
    }

    // Add this value table column to the value table column list.  We
    // must do this so that column names from this value table can
    // be looked up directly (note that they do not always appear
    // independently in the names() of this NameScope).
    new_value_table_columns->emplace_back(new_value_table_column);
  }
}

// static
absl::Status NameScope::CreateNewRangeVariableTargetGivenValidNamePaths(
    const NameTarget& original_name_target,
    const ValidFieldInfoMap& valid_field_info_map_in,
    NameTarget* new_name_target) {
  if (original_name_target.scan_columns()->is_value_table()) {
    GOOGLESQL_RET_CHECK(!original_name_target.is_pattern_variable());
    ValidNamePathList new_name_path_list;
    const ResolvedColumn range_variable_column =
        original_name_target.scan_columns()->column(0).column();
    // Check to see if any of the valid_field_info_map_in columns
    // derive from this value table range variable.  If so add
    // it to the valid access list.
    ResolvedColumn new_range_variable_column;
    if (GetNewResolvedColumnOrRelatedValidNamePathList(
            range_variable_column, valid_field_info_map_in,
            &new_range_variable_column, &new_name_path_list)) {
      // The value table target column is valid to access directly.  When
      // invoked for GROUP BY processing, this means that we return a
      // target for the post-GROUP BY version of the original target.
      // All fields are therefore referenceable from it, and we do not
      // need any additional GetField path lists.
      *new_name_target = NameTarget(new_range_variable_column,
                                    original_name_target.IsExplicit());
    } else {
      // The value table target column is not valid to access directly,
      // but its fields might be.
      new_name_target->SetAccessError(NameTarget::RANGE_VARIABLE);
      if (!new_name_path_list.empty()) {
        new_name_target->AppendValidNamePathList(new_name_path_list);
      }
    }
  } else {
    // Create a new RANGE_VARIABLE error target with a
    // ValidNamePathList that reflects the valid
    // name paths and columns from 'valid_field_info_map_in'.
    ValidNamePathList new_name_path_list;
    for (const NamedColumn& named_column :
         original_name_target.scan_columns()->columns()) {
      const ValidNamePathList* named_column_name_path_list;
      if (valid_field_info_map_in.LookupNamePathList(
              named_column.column(), &named_column_name_path_list)) {
        // There are field paths accessible from this column, so
        // remember them.
        for (const ValidNamePath& valid_name_path :
             *named_column_name_path_list) {
          std::vector<IdString> names;
          names.push_back(named_column.name());
          names.insert(names.end(), valid_name_path.name_path().begin(),
                       valid_name_path.name_path().end());
          new_name_path_list.push_back(
              {names, valid_name_path.target_column()});
        }
      }
    }
    // Accessing this range variable directly is an error,
    // but accessing its fields are possibly ok.
    new_name_target->SetAccessError(NameTarget::RANGE_VARIABLE);
    GOOGLESQL_RETURN_IF_ERROR(new_name_target->SetIsPatternVariable(
        original_name_target.is_pattern_variable()));
    if (!new_name_path_list.empty()) {
      new_name_target->AppendValidNamePathList(new_name_path_list);
    }
  }
  return absl::OkStatus();
}

absl::Status NameScope::CreateNewLocalNameTargetsGivenValidNamePaths(
    const ValidFieldInfoMap& valid_field_info_map_in,
    IdStringHashMapCase<NameTarget>* new_name_targets) const {
  // For each (name,target) in names(), create a new NameTarget for the
  // name based on the original target and 'valid_field_info_map_in'.
  // The new NameTarget either identifies a new ResolvedColumn that the
  // name maps to, or that the name is invalid to access.  If invalid
  // to access, there may be a ValidFieldInfoMap added to the target if
  // any of its (sub)fields are themselves valid to access.
  //
  // For example, this function is used to create a post-GROUP BY version
  // of a NameScope from a pre-GROUP BY version of a NameScope, where
  // the names that are grouped by are updated to resolve to the
  // post-GROUP BY version of the column, and names that are not grouped
  // by are given error NameTargets.  When grouping by a field path like
  // 'a.b.c', then the NameTarget for 'a' will be an error NameTarget but
  // it will include a ValidFieldInfo entry that allows 'b.c' to resolve
  // from 'a' to the post-GROUP BY version of 'a.b.c'.
  for (auto iter = names().begin(); iter != names().end(); ++iter) {
    NameTarget new_name_target;
    const IdString name = iter->first;
    const NameTarget& target = iter->second;
    switch (target.kind()) {
      case NameTarget::RANGE_VARIABLE: {
        GOOGLESQL_RETURN_IF_ERROR(CreateNewRangeVariableTargetGivenValidNamePaths(
            target, valid_field_info_map_in, &new_name_target));
        break;
      }
      case NameTarget::IMPLICIT_COLUMN:
      case NameTarget::EXPLICIT_COLUMN: {
        // This name maps to a column.  If this column maps directly to
        // a new column then use it, otherwise make this an error NameTarget.
        new_name_target.SetAccessError(target.kind());
        ValidNamePathList new_name_path_list;
        const ResolvedColumn& original_target_column = target.column();
        ResolvedColumn new_target_column;
        if (GetNewResolvedColumnOrRelatedValidNamePathList(
                original_target_column, valid_field_info_map_in,
                &new_target_column, &new_name_path_list)) {
          new_name_target = NameTarget(new_target_column, target.IsExplicit());
        }
        if (new_name_target.IsAccessError() && !new_name_path_list.empty()) {
          new_name_target.AppendValidNamePathList(new_name_path_list);
        }
        break;
      }
      case NameTarget::AMBIGUOUS:
        // If the name was already ambiguous, then leave it as ambiguous
        // in the new NameScope.
        new_name_target = target;
        break;
      case NameTarget::ACCESS_ERROR: {
        // If this NameTarget is already an access error, then leave it
        // as an access error in the new NameScope.  However, if this
        // NameTarget has ValidNamePaths then we must update those as
        // necessary. The 'valid_field_info_map_in' identifies all fields
        // that are valid to access, so all ValidNamePaths associated with
        // this NameTarget must be updated to reflect the
        // 'valid_field_info_map_in' entries.
        //
        // If a NameTarget ValidNamePath has a target_column that matches
        // a source_column of a 'valid_field_info_map_in' entry (with
        // no name path), then the ValidNamePath's target_column is updated
        // to match the 'valid_field_info_map_in' entry's target_column.
        //
        // If a NameTarget ValidNamePath has a target column that does *not*
        // match a source_column of a 'valid_field_info_map_in' entry, then
        // that field is not valid to access and that ValidNamePath
        // is removed from the NameTarget.
        //
        // Note:  For context, this code gets exercised for queries with
        // GROUP BY and DISTINCT, such as the following:
        //
        //   SELECT DISTINCT a.b.c
        //   FROM foo
        //   GROUP BY a.b.c
        //   ORDER BY a.b.c
        //
        ValidNamePathList new_valid_name_path_list;
        for (const ValidNamePath& target_valid_name_path :
             target.valid_name_path_list()) {
          const ValidNamePathList* target_column_name_path_list_in;
          if (valid_field_info_map_in.LookupNamePathList(
                  target_valid_name_path.target_column(),
                  &target_column_name_path_list_in)) {
            // Look for a mapping from this ResolvedColumn to another
            // ResolvedColumn without any name path.  If we find it then
            // update the target ResolvedColumn for this
            // 'target_valid_name_path'.
            for (const ValidNamePath& valid_name_path_in :
                 *target_column_name_path_list_in) {
              if (valid_name_path_in.name_path().empty()) {
                new_valid_name_path_list.push_back(
                    {target_valid_name_path.name_path(),
                     valid_name_path_in.target_column()});
                // There should only be one ValidNamePath with an
                // empty name path to each column, so we break this loop
                // here.  It would be nice to assert that condition, but
                // detection of duplicates is generally expensive so we
                // avoid that here.
                break;
              } else {
                // A valid_name_path_in with a non-empty
                // name_path() is unexpected, and currently unhandled.
                // I think it is not possible to hit this until we allow
                // GROUP BY <struct>, which would enable the following
                // type of query:
                //
                //   SELECT DISTINCT a.b.c.d
                //   FROM table
                //   GROUP BY a.b
                //
                // If a non-empty name_path() needs to be handled then
                // ignoring it potentially produces a wrong result NameScope
                // (and therefore an invalid AST when used for resolution),
                // so we ABSL_CHECK on this condition instead.
                GOOGLESQL_RET_CHECK_FAIL() << "Unexpected ValidNamePath for "
                                 << "an ACCESS_ERROR target";
              }
            }
          }
        }
        new_name_target = target;
        new_name_target.set_valid_name_path_list(new_valid_name_path_list);
        break;
      }
      case NameTarget::FIELD_OF:
        // This is not expected to appear in the local set of NameTargets
        // for any NameScope - this target only appears during lookups.
        GOOGLESQL_RET_CHECK_FAIL() << "NameTarget has unexpected kind: "
                         << target.DebugString();
        break;
    }
    if (!googlesql_base::InsertIfNotPresent(new_name_targets, name, new_name_target)) {
      // Every name in names() should be unique so this should never fail.
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected duplicate NameTarget for name: " << name;
    }
  }
  return absl::OkStatus();
}

absl::Status NameScope::CreateNameScopeGivenValidNamePaths(
    const ValidFieldInfoMap& valid_field_info_map_in,
    std::unique_ptr<NameScope>* new_name_scope) const {
  IdStringHashMapCase<NameTarget> new_name_targets;
  GOOGLESQL_RETURN_IF_ERROR(CreateNewLocalNameTargetsGivenValidNamePaths(
      valid_field_info_map_in, &new_name_targets));

  std::vector<ValueTableColumn> new_value_table_columns;
  CreateNewValueTableColumnsGivenValidNamePaths(valid_field_info_map_in,
                                                &new_value_table_columns);
  new_name_scope->reset(new NameScope(previous_scope_, new_name_targets,
                                      new_value_table_columns,
                                      correlated_columns_set_));

  // If this NameScope does not allow correlated access, grouping paths are not
  // allowed to be accessed from a child scope, whether before or after
  // grouping.
  if (!allows_correlated_access_) {
    new_name_scope->get()->DisallowCorrelatedAccess(
        reason_to_disallow_correlated_access_);
  }

  return absl::OkStatus();
}

void NameTarget::SetAccessError(const Kind original_kind,
                                absl::string_view access_error_message,
                                bool is_terminal_error) {
  // Initialize fields.
  kind_ = ACCESS_ERROR;
  access_error_message_ = access_error_message;
  is_terminal_error_ = is_terminal_error;
  original_kind_ = original_kind;
  // Clear irrelevant fields.
  scan_columns_.reset();
  column_ = ResolvedColumn();
  field_id_ = -1;
}

bool NameTarget::Equals_TESTING(const NameTarget& other) const {
  if (kind_ != other.kind()) {
    return false;
  }
  switch (kind_) {
    case RANGE_VARIABLE:
      // TODO: We should not rely on NameList.DebugString() for
      // equality.  Implement NameList equality and use that here.
      return is_pattern_variable_ == other.is_pattern_variable() &&
             scan_columns()->DebugString() ==
                 other.scan_columns()->DebugString();
    case IMPLICIT_COLUMN:
    case EXPLICIT_COLUMN:
      return column_ == other.column();
    case FIELD_OF:
      return column_ == other.column_containing_field() &&
             field_id_ == other.field_id();
    case AMBIGUOUS:
      return true;
    case ACCESS_ERROR:
      // TODO: We should not rely on ValidNamePathListDebugString
      // for equality.  Implement real equality ValidNamePathList.
      return original_kind_ != other.original_kind() &&
             ValidNamePathListDebugString(valid_name_path_list_) ==
                 ValidNamePathListDebugString(other.valid_name_path_list());
  }
}

inline void RangeVarNamedColumnFormatter(std::string* out, NamedColumn nc,
                                         bool is_value_table) {
  absl::StrAppend(out, nc.column().name(), "#", nc.column().column_id());
  // If the NameList name is different from the ResolvedColumn name, print the
  // NameList name as well, since that is what a query would reference. Don't
  // print different names for value tables, since the name is not meaningful.
  if (!is_value_table && !nc.name().Equals(nc.column().name_id())) {
    absl::StrAppend(out, " AS ", nc.name().ToString());
  }
}

std::string NameTarget::DebugString() const {
  std::string debug_string;
  if (IsAccessError()) {
    absl::StrAppend(&debug_string, "access error(");
  }
  switch (kind_) {
    case RANGE_VARIABLE: {
      std::string column_string =
          absl::StrJoin(scan_columns()->columns(), ",",
                        [this](std::string* out, NamedColumn nc) {
                          RangeVarNamedColumnFormatter(
                              out, nc, scan_columns()->is_value_table());
                        });
      std::string pseudo_column_string = absl::StrJoin(
          scan_columns()->GetNamedPseudoColumns(), ",",
          [](std::string* out, NamedColumn nc) {
            RangeVarNamedColumnFormatter(out, nc, /*is_value_table=*/false);
          });
      return absl::StrCat(
          is_pattern_variable_ ? "PATTERN_VARIABLE<" : "RANGE_VARIABLE<",
          column_string,
          column_string.empty() || pseudo_column_string.empty() ? "" : ", ",
          pseudo_column_string.empty() ? "" : "pseudo_columns: ",
          pseudo_column_string, ">",
          scan_columns()->is_value_table() ? " (value table)" : "");
    }
    case IMPLICIT_COLUMN:
    case EXPLICIT_COLUMN:
      return absl::StrCat(
          (column_.type() != nullptr
               ? column_.type()->ShortTypeName(ProductMode::PRODUCT_INTERNAL)
               : "nullptr"),
          " (", column_.DebugString(), ")",
          (kind_ == IMPLICIT_COLUMN ? " (implicit)" : ""));
    case FIELD_OF:
      return absl::StrCat("FIELD_OF<", column_.DebugString(),
                          "> (id: ", field_id_, ")");
    case AMBIGUOUS:
      return "ambiguous";
      break;
    case ACCESS_ERROR:
      switch (original_kind_) {
        case RANGE_VARIABLE:
          if (is_pattern_variable_) {
            absl::StrAppend(&debug_string, "PATTERN_VARIABLE");
          } else {
            absl::StrAppend(&debug_string, "RANGE_VARIABLE");
          }
          break;
        case IMPLICIT_COLUMN:
          absl::StrAppend(&debug_string, "IMPLICIT_COLUMN");
          break;
        case EXPLICIT_COLUMN:
          absl::StrAppend(&debug_string, "EXPLICIT_COLUMN");
          break;
        case FIELD_OF:
          absl::StrAppend(&debug_string, "FIELD_OF");
          break;
        case AMBIGUOUS:
          absl::StrAppend(&debug_string, "AMBIGUOUS");
          break;
        case ACCESS_ERROR:
          absl::StrAppend(&debug_string, "ACCESS_ERROR");
          break;
      }
      absl::StrAppend(&debug_string, ", name_path_list<");
      bool first = true;
      for (const ValidNamePath& valid_name_path : valid_name_path_list_) {
        absl::StrAppend(&debug_string, (first ? "" : ","),
                        valid_name_path.DebugString());
        first = false;
      }
      absl::StrAppend(&debug_string, ">");
      break;
  }
  if (IsAccessError()) {
    absl::StrAppend(&debug_string, ")");
  }
  return debug_string;
}

absl::Status NameList::AddColumn(IdString name, const ResolvedColumn& column,
                                 bool is_explicit) {
  GOOGLESQL_RET_CHECK(!is_value_table()) << "Cannot add more columns to a value table";
  columns_.emplace_back(name, column, is_explicit);
  if (!IsInternalAlias(name)) {
    name_scope_.AddColumn(name, column, is_explicit);
  }
  return absl::OkStatus();
}

absl::Status NameList::AddDelayedColumn(IdString name, bool is_explicit,
                                        DelayedColumnInfo delayed_column_info) {
  GOOGLESQL_RET_CHECK(!IsInternalAlias(name));
  GOOGLESQL_RET_CHECK(!is_value_table()) << "Cannot add more columns to a value table";
  // Assign an invalid ID while this column is uninitialized.
  size_t index = columns_.size();
  columns_.emplace_back(name, ResolvedColumn(), is_explicit);

  GOOGLESQL_RET_CHECK(!columns_[index].column().IsInitialized())
      << "Column " << name.ToStringView()
      << " already initialized: " << columns_[index].column().DebugString();

  // Augment the callback to update this name list accordingly.
  auto initial_callback = std::move(delayed_column_info.precompute_callback);
  delayed_column_info.precompute_callback =
      [initial_callback = std::move(initial_callback), index,
       this](ColumnReferenceContext context) -> absl::StatusOr<ResolvedColumn> {
    const NamedColumn& named_column = columns_[index];

    // The NameTarget may have been copied. If the callback had been called
    // already, do not perform it again.
    if (named_column.column().IsInitialized()) {
      return named_column.column();
    }

    // This is the first time, invoke `initial_callback`.
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedColumn column, initial_callback(context));

    // Update the column with the correct ResolvedColumn, in its place.
    columns_[index] =
        NamedColumn(named_column.name(), column, named_column.is_explicit(),
                    named_column.excluded_field_names());
    return column;
  };
  name_scope_.AddNameTarget(
      name, NameTarget(is_explicit, std::move(delayed_column_info)));
  return absl::OkStatus();
}

absl::Status NameList::AddLoggingTarget(IdString name, bool is_explicit,
                                        LoggingTargetInfo logging_target_info) {
  GOOGLESQL_RET_CHECK(!IsInternalAlias(name));
  GOOGLESQL_RET_CHECK(!is_value_table()) << "Cannot add more columns to a value table";
  // Assign an invalid ID while this column is uninitialized.
  size_t index = columns_.size();
  columns_.emplace_back(name, ResolvedColumn(), is_explicit);

  GOOGLESQL_RET_CHECK(!columns_[index].column().IsInitialized())
      << "Column " << name.ToStringView()
      << " already initialized: " << columns_[index].column().DebugString();

  name_scope_.AddNameTarget(
      name, NameTarget(is_explicit, std::move(logging_target_info)));
  return absl::OkStatus();
}

absl::Status NameList::AddValueTableColumn(
    IdString range_variable_name, const ResolvedColumn& column,
    const ASTNode* ast_location, const IdStringSetCase& excluded_field_names,
    const NameListPtr& pseudo_columns_name_list) {
  GOOGLESQL_RET_CHECK(!is_value_table()) << "Cannot add more columns to a value table";
  if (pseudo_columns_name_list != nullptr) {
    // The only names present in pseudo_columns_name_list should be the
    // pseudo-columns.  No range variables or regular columns.

    // We should have no visible columns.
    GOOGLESQL_RET_CHECK_EQ(pseudo_columns_name_list->num_columns(), 0);
    // And we should have no range variables.
    if (GOOGLESQL_DEBUG_MODE) {
      for (const auto& name_and_target :
           pseudo_columns_name_list->name_scope_.names()) {
        GOOGLESQL_RET_CHECK(name_and_target.second.IsColumn());
      }
    }
  }

  // We will add a range variable for this value table pointing at a NameList
  // including the pseudo-columns attached, if any.
  // The value table column in this inner NameList is anonymous to prevent
  //   SELECT alias.alias FROM ValueTable AS alias;
  std::shared_ptr<NameList> value_table_name_list(new NameList);
  // This NameList is used only for the contents of a range variable and not
  // as a column.  It will never be expanded by SELECT * (without rangevar.*),
  // so excluded_field_names is not actually used, but we fill it in for
  // clarity.
  value_table_name_list->columns_.emplace_back(
      kValueTableName, column, false /* is_explicit */, excluded_field_names);
  value_table_name_list->name_scope_.mutable_value_table_columns()->push_back(
      {column,
       excluded_field_names,
       true /* is_valid_to_access */,
       {} /* valid_field_info_map */});

  if (pseudo_columns_name_list != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(value_table_name_list->MergeFrom(*pseudo_columns_name_list,
                                                     ast_location));
  }
  GOOGLESQL_RETURN_IF_ERROR(value_table_name_list->SetIsValueTable());

  if (HasRangeVariable(range_variable_name)) {
    return MakeSqlErrorAt(ast_location)
           << "Duplicate alias " << range_variable_name << " found";
  }

  // We put in an implicit column that will expand to the value table column
  // in select star.
  columns_.emplace_back(range_variable_name, column, false /* is_explicit */,
                        excluded_field_names);

  if (!IsInternalAlias(range_variable_name)) {
    // Add the value table column as a range variable in the NameScope.
    // We don't need to add a column because the column would always be
    // hidden by the range variable.
    name_scope_.AddRangeVariable(range_variable_name, value_table_name_list,
                                 /*is_pattern_variable=*/false);
  }
  // We need to also add it as a value table in the NameScope so we can find
  // implicit fields underneath it.
  name_scope_.mutable_value_table_columns()->push_back(
      {column,
       excluded_field_names,
       true /* is_valid_to_access */,
       {} /* valid_field_info_map */});

  return absl::OkStatus();
}

absl::Status NameList::AddColumnMaybeValueTable(IdString name,
                                                const ResolvedColumn& column,
                                                bool is_explicit,
                                                const ASTNode* ast_location,
                                                bool is_value_table_column) {
  if (is_value_table_column) {
    return AddValueTableColumn(name, column, ast_location);
  } else {
    return AddColumn(name, column, is_explicit);
  }
}

absl::Status NameList::AddPseudoColumn(IdString name,
                                       const ResolvedColumn& column,
                                       const ASTNode* ast_location) {
  ABSL_DCHECK(ast_location != nullptr);
  // Pseudo-columns go in the NameScope as implicit columns, but don't show
  // up in the columns_ list because they don't show up in SELECT *.
  if (!IsInternalAlias(name)) {
    name_scope_.AddColumn(name, column, false /* is_explicit */);
  }
  return absl::OkStatus();
}

absl::Status NameList::SetIsValueTable() {
  GOOGLESQL_RET_CHECK(HasValueTableColumns());
  GOOGLESQL_RET_CHECK_EQ(num_columns(), 1);
  is_value_table_ = true;
  return absl::OkStatus();
}

bool NameList::HasRangeVariable(IdString name) const {
  NameTarget found;
  return name_scope_.LookupName(name, &found) && found.IsRangeVariable();
}

// We aren't defending against cycles in AddRangeVariable.  If a cycle occurs,
// a memory leak will happen.  Cycles should not happen, by construction, since
// NameLists are usually built hierarchically as we go up the tree, and only
// reference older NameLists.  If a problem shows up, we can update this.
absl::Status NameList::AddRangeVariable(IdString name,
                                        const NameListPtr& scan_columns,
                                        const ASTNode* ast_location) {
  GOOGLESQL_RET_CHECK_NE(scan_columns.get(), this)
      << "AddRangeVariable cannot add a NameList to itself";
  GOOGLESQL_RET_CHECK(!scan_columns->is_value_table())
      << "AddRangeVariable cannot add a value table NameList";

  if (HasRangeVariable(name)) {
    return MakeSqlErrorAt(ast_location)
           << "Duplicate table alias " << name << " in the same FROM clause";
  }

  // Range variables are stored inside the NameScope only.
  name_scope_.AddRangeVariable(name, scan_columns,
                               /*is_pattern_variable=*/false);

  return absl::OkStatus();
}

absl::Status NameList::AddAmbiguousColumn_Test(IdString name) {
  GOOGLESQL_RETURN_IF_ERROR(AddColumn(name, {}, true /* is_explicit */));
  return AddColumn(name, {}, true /* is_explicit */);
}

// This is an optimized version of inserting all elements of <from> into <to>.
// If we can just do assignment rather than an insert loop, that will be faster.
template <class SET_TYPE>
static void InsertFrom(const SET_TYPE& from, SET_TYPE* to) {
  if (from.empty()) return;
  if (to->empty()) {
    *to = from;
  } else {
    for (const IdString name : from) {
      to->insert(name);
    }
  }
}

absl::Status NameList::MergeFrom(const NameList& other,
                                 const ASTNode* ast_location) {
  ABSL_DCHECK_NE(&other, this) << "Merging NameList with itself";
  ABSL_DCHECK(ast_location != nullptr);

  if (columns_.empty() && name_scope_.IsEmpty()) {
    // Optimization: When merging into an empty NameList with no options,
    // we can just copy the full state.
    columns_ = other.columns_;
    name_scope_.CopyStateFrom(other.name_scope_);
    return absl::OkStatus();
  }

  return MergeFrom(other, ast_location, MergeOptions());
}

std::shared_ptr<NameList> NameList::Copy() const {
  // This is not implemented by calling the Copy that takes `options` because
  // we want to use the optimized construction path like MergeFrom above uses.
  // This is not implemented via MergeFrom to avoid needing to handle errors.
  auto new_name_list = std::make_shared<NameList>();
  new_name_list->columns_ = columns_;
  new_name_list->name_scope_.CopyStateFrom(name_scope_);
  return new_name_list;
}

std::shared_ptr<NameList> NameList::CopyWithIsValueTable() const {
  std::shared_ptr<NameList> copy = Copy();
  if (is_value_table_) {
    // SetIsValueTable() can return errors, but only on assertion failures
    // that can't occur here, so just set the field directly.
    copy->is_value_table_ = true;
  }
  return copy;
}

absl::Status NameList::MergeFrom(const NameList& other,
                                 const ASTNode* ast_location,
                                 const MergeOptions& options) {
  ABSL_DCHECK_NE(&other, this) << "Merging NameList with itself";
  ABSL_DCHECK(ast_location != nullptr);

  if (options.rename_value_table_to_name != nullptr) {
    GOOGLESQL_RET_CHECK(!options.flatten_to_table);
    GOOGLESQL_RET_CHECK(other.is_value_table());
  }
  GOOGLESQL_RET_CHECK_LE((options.excluded_field_names ? 1 : 0) +
                   (options.columns_to_replace ? 1 : 0) +
                   (options.columns_to_rename ? 1 : 0),
               1);
  if (options.columns_to_replace != nullptr) {
    GOOGLESQL_RET_CHECK(!options.flatten_to_table);
    GOOGLESQL_RET_CHECK(!options.rename_value_table_to_name);
  }

  const IdStringSetCase* excluded_field_names = options.excluded_field_names;
  const MergeOptions::ColumnsToReplaceMap* columns_to_replace =
      options.columns_to_replace;
  const MergeOptions::ColumnsToRenameMap* columns_to_rename =
      options.columns_to_rename;

  // Copy the columns vector, with exclusions.
  // We're not using AddColumn because we're going to copy the NameScope
  // maps directly below.
  for (const NamedColumn& named_column : other.columns()) {
    IdString new_name = named_column.name();
    bool renamed_column = false;
    if (columns_to_rename != nullptr) {
      const IdString* found_name =
          googlesql_base::FindOrNull(*columns_to_rename, named_column.name());
      if (found_name != nullptr) {
        new_name = *found_name;
        renamed_column = true;
      }
    }

    const ResolvedColumn* replacement_column = nullptr;
    if (columns_to_replace != nullptr) {
      replacement_column =
          googlesql_base::FindOrNull(*columns_to_replace, named_column.name());
    }

    if (replacement_column != nullptr) {
      // This always makes a NamedColumn without is_value_table.
      // TODO Is that right?  Pipe SET columns become
      // non-value-tables.
      columns_.push_back(NamedColumn(new_name, *replacement_column,
                                     /*is_explicit=*/true));
    } else if (excluded_field_names == nullptr ||
               !googlesql_base::ContainsKey(*excluded_field_names, named_column.name())) {
      // For value table columns, we add new excluded_field_names so fields
      // with those names won't show up in SELECT *.
      // With `flatten_to_table`, we skip handling the column as
      // a value table and will add it as a regular column (in the else).
      if (named_column.is_value_table_column() && !options.flatten_to_table) {
        // Compute the union of the existing excluded_field_names and the
        // newly added excluded_field_names.
        // Columns from `columns_to_replace` are also excluded.
        IdStringSetCase new_excluded_field_names;
        if (excluded_field_names != nullptr) {
          InsertFrom(*excluded_field_names, &new_excluded_field_names);
        }
        if (columns_to_replace != nullptr) {
          googlesql_base::InsertKeysFromMap(*columns_to_replace,
                                 &new_excluded_field_names);
        }
        InsertFrom(named_column.excluded_field_names(),
                   &new_excluded_field_names);

        // A NameList has NamedColumns, and NamedColumns don't have any
        // way to express paths that are accessible off of them.  So we
        // leave 'valid_field_info_map' empty here.
        name_scope_.mutable_value_table_columns()->push_back(
            {named_column.column(),
             new_excluded_field_names,
             true /* is_valid_to_access */,
             {} /* valid_field_info_map */});

        if (options.rename_value_table_to_name != nullptr) {
          new_name = *options.rename_value_table_to_name;
        }

        // Copy the column, but update excluded_field_names with the
        // new list.
        columns_.emplace_back(new_name, named_column.column(),
                              named_column.is_explicit(),
                              new_excluded_field_names);
      } else if (renamed_column) {
        if (named_column.is_value_table_column()) {
          // Using this constructor makes a NamedColumn with is_value_table.
          columns_.push_back(NamedColumn(new_name, named_column.column(),
                                         /*is_explicit=*/true,
                                         named_column.excluded_field_names()));
        } else {
          columns_.push_back(NamedColumn(new_name, named_column.column(),
                                         /*is_explicit=*/true));
        }
      } else {
        // TODO There's a bug here where with flatten_to_table
        // (e.g. pipe AS), we get a column marked is_value_table but not
        // listed in value_table_columns, so it doesn't work consistently.
        // I think value_table_columns needs to be updated here too, like it
        // is in the !flatten_to_table branch above.
        columns_.push_back(named_column);
      }
    }
  }

  // Copy columns (including pseudo-columns, and including ambiguous column
  // markers) and range variables from inside the NameScope.
  for (const auto& item : other.name_scope_.names()) {
    IdString name = item.first;
    const NameTarget& target = item.second;

    // Exclude both columns and range variables in the exclude set.
    if (excluded_field_names != nullptr &&
        googlesql_base::ContainsKey(*excluded_field_names, name)) {
      continue;
    }

    // If we have a replacement column, replace whatever we have in the scope
    // with that column.  Even range variables, error targets, etc.
    if (columns_to_replace != nullptr) {
      const ResolvedColumn* replacement_column =
          googlesql_base::FindOrNull(*columns_to_replace, name);
      if (replacement_column != nullptr) {
        NameTarget new_target(*replacement_column, /*is_explicit=*/true);
        name_scope_.AddNameTarget(name, new_target);
        continue;
      }
    }

    if (columns_to_rename != nullptr) {
      const IdString* found_name = googlesql_base::FindOrNull(*columns_to_rename, name);
      if (found_name != nullptr) {
        name = *found_name;
      }
    }

    if (target.IsRangeVariable()) {
      // With `flatten_to_table`, range variables are dropped, except for
      // value table range variables, which will be converted to columns.
      if (options.flatten_to_table) {
        if (target.scan_columns()->is_value_table()) {
          // Convert value table column to be a regular column.
          NameTarget new_target(target.scan_columns()->column(0).column(),
                                /*is_explicit=*/true);
          name_scope_.AddNameTarget(name, new_target);
        }
        continue;
      }

      if (target.scan_columns()->is_value_table() &&
          options.rename_value_table_to_name != nullptr) {
        name = *options.rename_value_table_to_name;
      }

      // For range variables, we need to check for duplicates while merging.
      // Normally this would happen in AddRangeVariable, but we are bypassing
      // that call and copying the existing NameTarget directly.
      if (HasRangeVariable(name)) {
        return MakeSqlErrorAt(ast_location) << "Duplicate table alias " << name
                                            << " in the same FROM clause";
      }
    } else {
      ABSL_DCHECK(!target.IsFieldOf());
    }

    name_scope_.AddNameTarget(name, target);
  }

  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<NameList>> NameList::Copy(
    const ASTNode* ast_location, const MergeOptions& options) const {
  auto new_name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(new_name_list->MergeFrom(*this, ast_location, options));
  return new_name_list;
}

// static
absl::StatusOr<std::shared_ptr<NameList>>
NameList::AddRangeVariableInWrappingNameList(
    IdString alias, const ASTNode* ast_location,
    std::shared_ptr<const NameList> original_name_list) {
  std::shared_ptr<NameList> range_variable_name_list =
      std::make_shared<NameList>();

  // When making the range variable, we don't want to include nested range
  // variables, including for value tables, so we use `flatten_to_table`
  // which drops range variables.
  GOOGLESQL_RETURN_IF_ERROR(range_variable_name_list->MergeFrom(
      *original_name_list, ast_location, {.flatten_to_table = true}));

  auto wrapper_name_list = std::make_shared<NameList>();
  GOOGLESQL_RETURN_IF_ERROR(
      wrapper_name_list->MergeFrom(*original_name_list, ast_location));
  GOOGLESQL_RETURN_IF_ERROR(wrapper_name_list->AddRangeVariable(
      alias, range_variable_name_list, ast_location));
  return wrapper_name_list;
}

absl::StatusOr<std::shared_ptr<NameList>> NameList::CloneWithNewColumns(
    const ASTNode* ast_location, absl::string_view value_table_error,
    const ASTIdentifier* alias,
    std::function<ResolvedColumn(const ResolvedColumn&)> clone_column,
    IdStringPool* id_string_pool) const {
  if (is_value_table()) {
    return MakeSqlErrorAt(ast_location) << value_table_error;
  }

  // A new NameList pointing at the new ResolvedColumns.
  auto cloned_name_list = std::make_shared<NameList>();

  // Make a new ResolvedColumn for each column from the current list.
  ResolvedColumnList column_list;
  for (const NamedColumn& column : columns()) {
    const ResolvedColumn resolved_col = clone_column(column.column());
    column_list.emplace_back(resolved_col);
    if (column.is_value_table_column()) {
      return MakeSqlErrorAt(ast_location) << value_table_error;
    } else {
      GOOGLESQL_RETURN_IF_ERROR(cloned_name_list->AddColumn(column.name(), resolved_col,
                                                  column.is_explicit()));
    }
  }

  if (alias != nullptr) {
    // If alias is provided, add a range variable to the name list so that this
    // alias can be referred to.
    GOOGLESQL_ASSIGN_OR_RETURN(
        cloned_name_list,
        AddRangeVariableInWrappingNameList(
            alias->GetAsIdString(), alias,
            std::const_pointer_cast<const NameList>(cloned_name_list)));
  }

  return cloned_name_list;
}

std::vector<ResolvedColumn> NameList::GetResolvedColumns() const {
  std::vector<ResolvedColumn> ret;
  ret.reserve(columns_.size());
  for (const NamedColumn& named_column : columns_) {
    ret.push_back(named_column.column());
  }
  return ret;
}

static IdStringHashSetCase GetColumnNameSet(
    absl::Span<const NamedColumn> columns) {
  IdStringHashSetCase name_list_columns;
  for (const NamedColumn& named_column : columns) {
    name_list_columns.insert(named_column.name());
  }
  return name_list_columns;
}

// Pseudo-columns only exist in name_scope_, but not within columns_. So we
// need to find all columns in name_scope_ that are not in columns_.
std::vector<ResolvedColumn> NameList::GetResolvedPseudoColumns() const {
  IdStringHashSetCase name_list_columns = GetColumnNameSet(columns_);

  // Find pseudo-columns by finding all columns in name scope that are not in
  // the name list.
  std::vector<ResolvedColumn> pseudo_columns;
  for (const auto& item : name_scope_.names()) {
    IdString name = item.first;
    if (name_list_columns.find(name) == name_list_columns.end() &&
        item.second.IsColumn()) {
      pseudo_columns.push_back(item.second.column());
    }
  }

  // Sort pseudo-columns by name for consistent Resolved AST output in tests.
  std::stable_sort(pseudo_columns.begin(), pseudo_columns.end(),
                   [](const ResolvedColumn& a, const ResolvedColumn& b) {
                     return a.name() < b.name();  // Sort by name
                   });

  return pseudo_columns;
}

std::vector<NamedColumn> NameList::GetNamedPseudoColumns() const {
  IdStringHashSetCase name_list_columns = GetColumnNameSet(columns_);

  // Find pseudo-columns by finding all columns in name scope that are not in
  // the name list.
  std::vector<NamedColumn> pseudo_columns;
  for (const auto& item : name_scope_.names()) {
    IdString name = item.first;
    if (name_list_columns.find(name) == name_list_columns.end() &&
        item.second.IsColumn()) {
      pseudo_columns.push_back(NamedColumn(name, item.second.column(),
                                           /*is_explicit=*/false));
    }
  }

  // Sort pseudo-columns by name for consistent Resolved AST output in tests.
  std::stable_sort(pseudo_columns.begin(), pseudo_columns.end(),
                   [](const NamedColumn& a, const NamedColumn& b) {
                     return a.name() < b.name();  // Sort by name
                   });

  return pseudo_columns;
}

std::vector<IdString> NameList::GetColumnNames() const {
  std::vector<IdString> ret;
  ret.reserve(columns_.size());
  for (const NamedColumn& named_column : columns_) {
    ret.push_back(named_column.name());
  }
  return ret;
}

bool NameList::LookupName(IdString name, NameTarget* found) const {
  // We only need to look in name_scope_ because all named columns in columns_
  // will also be in name_scope_.
  return name_scope_.LookupName(name, found);
}

Type::HasFieldResult NameList::SelectStarHasColumn(IdString name) const {
  if (name.empty()) return Type::HAS_NO_FIELD;

  int fields_found = 0;
  for (const NamedColumn& column : columns_) {
    // Value table columns *with fields* will be expanded to the list of
    // fields rather than the column itself in SELECT *.
    if (!column.is_value_table_column() ||
        !column.column().type()->HasAnyFields()) {
      if (IdStringCaseEqual(column.name(), name)) {
        ++fields_found;
      }
    } else {
      if (googlesql_base::ContainsKey(column.excluded_field_names(), name)) {
        continue;
      }
      switch (
          column.column().type()->HasField(name.ToString(),
                                           /*field_id=*/nullptr,
                                           /*include_pseudo_fields=*/false)) {
        case Type::HAS_NO_FIELD:
          break;
        case Type::HAS_FIELD:
          ++fields_found;
          break;
        case Type::HAS_AMBIGUOUS_FIELD:
          fields_found += 2;
          break;
        case Type::HAS_PSEUDO_FIELD:
          ABSL_DLOG(FATAL) << "Type::HasField returned unexpected HAS_PSEUDO_FIELD "
                         "value when "
                         "called with include_pseudo_fields=false argument";
          break;
      }
    }

    if (fields_found > 1) break;
  }

  switch (fields_found) {
    case 0:
      return Type::HAS_NO_FIELD;
    case 1:
      return Type::HAS_FIELD;
    default:
      return Type::HAS_AMBIGUOUS_FIELD;
  }
}

std::string NameList::DebugString(absl::string_view indent) const {
  std::string out;
  if (!is_value_table()) {
    absl::StrAppend(&out, indent, "NameList:");
  } else {
    absl::StrAppend(&out, indent, "NameList (is_value_table = true):");
  }

  // We don't track whether names in the NameScope are pseudo-columns, so we
  // reverse engineer this by checking if the names match any name from the
  // NameList.  If not, they must have been pseudo-columns.
  IdStringSetCase name_list_columns;

  for (const NamedColumn& named_column : columns_) {
    if (!out.empty()) out += "\n";
    absl::StrAppend(&out, indent, "  ", named_column.DebugString());

    if (!IsInternalAlias(named_column.name())) {
      name_list_columns.insert(named_column.name());
    }
  }

  const std::string name_scope_contents =
      name_scope_.DebugString(absl::StrCat(indent, "  "), &name_list_columns);
  if (!out.empty()) out += "\n";
  absl::StrAppend(&out, indent, "NameScope:");
  if (!name_scope_contents.empty()) {
    absl::StrAppend(&out, "\n", name_scope_contents);
  }
  return out;
}

reflection::ResultTable NameList::Describe(ProductMode product_mode) const {
  reflection::ResultTable result_table;

  if (is_value_table()) {
    result_table.set_is_value_table(true);
  }

  // Build the set of names in the NameList, so pseudo-columns can be detected.
  // The value is the number of times the column name occurs.
  IdStringHashMapCase<int> name_list_column_names_count;

  // Build multi-map of ResolvedColumn to column offsets in the `column` array.
  // There's no multimap class so we use a vector value.
  absl::flat_hash_map<ResolvedColumn, std::vector<int>> name_list_columns_map;

  for (const NamedColumn& named_column : columns_) {
    if (!IsInternalAlias(named_column.name())) {
      ++name_list_column_names_count[named_column.name()];
    }

    name_list_columns_map[named_column.column()].push_back(
        result_table.column_size());

    named_column.Describe(product_mode, result_table.add_column());
  }

  name_scope_.DescribeInto(&result_table, name_list_column_names_count,
                           name_list_columns_map);

  return result_table;
}

void NameList::DescribeInto(reflection::TableAlias* table_alias,
                            bool include_pseudo_columns) const {
  for (const NamedColumn& named_column : columns()) {
    const IdString& column_name = named_column.name();

    if (is_value_table()) {
      table_alias->add_column_name("<value>");
    } else if (IsInternalAlias(column_name)) {
      table_alias->add_column_name("<unnamed>");
    } else {
      table_alias->add_column_name(column_name.ToString());
    }
  }

  if (include_pseudo_columns) {
    for (const NamedColumn& named_column : GetNamedPseudoColumns()) {
      const IdString& column_name = named_column.name();

      if (IsInternalAlias(column_name)) {
        table_alias->add_pseudo_column_name("<unnamed>");
      } else {
        table_alias->add_pseudo_column_name(column_name.ToString());
      }
    }
  }
}

absl::StatusOr<std::shared_ptr<NameList>> NameListBuilder::Build() {
  auto name_list = std::make_shared<NameList>();

  // Add pseudo-columns to name list.
  for (auto column : pseudo_columns_) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddPseudoColumn(
        column.name, column.resolved_column, ast_location_));
  }

  // Create name list for value table.
  if (is_value_table_) {
    GOOGLESQL_RET_CHECK_EQ(columns_.size(), 1)
        << "Value table cannot have more than one column.";
    GOOGLESQL_RET_CHECK(alias_.has_value()) << "Value table must specify an alias.";

    // Add value table column, passing it list of pseudo-columns so they can
    // be attached to the range variable for the value table.
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddValueTableColumn(
        alias_.value(), columns_[0].resolved_column, ast_location_,
        /*excluded_field_names =*/{}, name_list));
    GOOGLESQL_RETURN_IF_ERROR(name_list->SetIsValueTable());

    return name_list;
  }

  // Create name list for regular table.
  for (auto& column : columns_) {
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(column.name, column.resolved_column,
                                         /*is_explicit=*/true));
  }

  // If the scan has an alias, add it as a range variable to the name list.
  if (alias_.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(name_list, NameList::AddRangeVariableInWrappingNameList(
                                    alias_.value(), ast_location_, name_list));
  }

  return name_list;
}

}  // namespace googlesql
