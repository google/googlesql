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

// Utility classes for generating variable names and associating them with
// columns and query parameters.
#ifndef GOOGLESQL_REFERENCE_IMPL_VARIABLE_GENERATOR_H_
#define GOOGLESQL_REFERENCE_IMPL_VARIABLE_GENERATOR_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace googlesql {

using SystemVariablesAlgebrizerMap =
    std::map<std::vector<std::string>, VariableId, StringVectorCaseLess>;

// Generates variable names corresponding to parameters or columns.
class VariableGenerator {
 public:
  VariableGenerator() = default;
  VariableGenerator(const VariableGenerator&) = delete;
  VariableGenerator& operator=(const VariableGenerator&) = delete;
  ~VariableGenerator() = default;

  // Returns a fresh variable name.
  VariableId GetNewVariableName(std::string suggested_name);

  // Returns a unique variable ID for a given query or column parameter.
  // If a VariableId has already been created for this parameter, returns it.
  VariableId GetVariableNameFromParameter(const std::string& parameter_name,
                                          ParameterMap* map);

  // Returns a unique variable ID for a given system variable.
  // If a VariableId has already been created for this parameter, returns it.
  VariableId GetVariableNameFromSystemVariable(
      const std::vector<std::string>& name_path,
      SystemVariablesAlgebrizerMap* system_variables_map);

  // Returns a unique variable ID for a given positional query parameter.
  // Positional parameters have an ID of the form $positional_param_<position>.
  // If a VariableId has already been created for this parameter, returns it.
  VariableId GetVariableNameFromParameter(int parameter_position,
                                          ParameterList* parameter_list);

 private:
  // Maps name to usage counter. The variable names in this map do not contain $
  // or dot characters.
  absl::flat_hash_map<VariableId, int> used_variable_names_;
};

// Maintains the mapping between ResolvedColumns and variables.
class ColumnToVariableMapping {
 public:
  using Map = absl::flat_hash_map<ResolvedColumn, VariableId>;

  explicit ColumnToVariableMapping(
      std::unique_ptr<VariableGenerator> variable_gen)
      : variable_gen_(std::move(variable_gen)) {}

  ColumnToVariableMapping(const ColumnToVariableMapping&) = delete;
  ColumnToVariableMapping& operator=(const ColumnToVariableMapping&) = delete;
  ~ColumnToVariableMapping() = default;

  VariableGenerator* variable_generator() { return variable_gen_.get(); }

  // Assigns a new variable name and returns it.
  VariableId AssignNewVariableToColumn(const ResolvedColumn& column);

  // Returns a unique variable name for a given column, inserting one if it is
  // not already present.
  VariableId GetVariableNameFromColumn(const ResolvedColumn& column);

  // Same as above, but returns NOT_FOUND if the column is missing.
  absl::StatusOr<VariableId> LookupVariableNameForColumn(
      const ResolvedColumn& column) const;

  const Map& map() const { return column_to_variable_; }
  void set_map(const Map& column_to_variable) {
    column_to_variable_ = column_to_variable;
  }

  std::string DebugString() const;

 private:
  std::unique_ptr<VariableGenerator> variable_gen_;
  Map column_to_variable_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_VARIABLE_GENERATOR_H_
