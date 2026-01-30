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

// Utility classes for working with query parameters.
#ifndef GOOGLESQL_REFERENCE_IMPL_PARAMETERS_H_
#define GOOGLESQL_REFERENCE_IMPL_PARAMETERS_H_

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/reference_impl/variable_id.h"

namespace googlesql {

// Maps lower-case query parameter or column names to variable names.
// Using std::map ensures a stable iteration order.
using ParameterMap = std::map<std::string, VariableId>;
// List of positional query parameters.
using ParameterList = std::vector<VariableId>;

// Represents either a map of named parameters associated with their
// VariableIds or a list of positional parameter VariableIds.
class Parameters {
 public:
  Parameters() : parameters_(ParameterMap()) {}

  explicit Parameters(const ParameterMap& parameter_map)
      : parameters_(parameter_map) {}

  explicit Parameters(const ParameterList& parameter_list)
      : parameters_(parameter_list) {}

  Parameters(const Parameters&) = delete;
  Parameters& operator=(const Parameters&) = delete;

  // Returns whether this represents a collection of named parameters.
  bool is_named() const {
    return std::holds_alternative<ParameterMap>(parameters_);
  }

  // Sets the parameter mode. Clears any existing parameters.
  void set_named(bool named) {
    if (named != is_named()) {
      if (named) {
        parameters_ = std::variant<ParameterMap, ParameterList>(ParameterMap());
      } else {
        parameters_ =
            std::variant<ParameterMap, ParameterList>(ParameterList());
      }
    }
  }

  // Precondition: is_named().
  const ParameterMap& named_parameters() const {
    return *std::get_if<ParameterMap>(&parameters_);
  }
  ParameterMap& named_parameters() {
    return *std::get_if<ParameterMap>(&parameters_);
  }

  // Precondition: !is_named().
  const ParameterList& positional_parameters() const {
    return *std::get_if<ParameterList>(&parameters_);
  }
  ParameterList& positional_parameters() {
    return *std::get_if<ParameterList>(&parameters_);
  }

 private:
  std::variant<ParameterMap, ParameterList> parameters_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_PARAMETERS_H_
