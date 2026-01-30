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

#ifndef GOOGLESQL_REFERENCE_IMPL_VARIABLE_ID_H_
#define GOOGLESQL_REFERENCE_IMPL_VARIABLE_ID_H_

#include <iosfwd>
#include <ostream>
#include <string>

#include "absl/strings/string_view.h"

namespace googlesql {

// Represents a variable identifier.
class VariableId {
 public:
  // Constructs an invalid variable identifier.
  VariableId() : name_("") {}
  VariableId(const VariableId&) = default;
  VariableId& operator=(const VariableId&) = default;

  // For ease of debugging, 'name' may not contain @ or $.
  explicit VariableId(absl::string_view name);

  const std::string& name() const { return name_; }
  bool is_valid() const { return !name_.empty(); }

  bool operator==(const VariableId& that) const {
    return name_ == that.name_;
  }
  bool operator!=(const VariableId& that) const {
    return name_ != that.name_;
  }
  bool operator<(const VariableId& that) const {
    return name_ < that.name_;
  }
  std::string ToString() const {
    return is_valid() ? name() : "<invalid variable id>";
  }

  template <typename H>
  friend H AbslHashValue(H h, const VariableId& v) {
    return H::combine(std::move(h), v.name_);
  }

 private:
  std::string name_;
  // Intentionally copyable.
};

// Allow VariableId to be logged.
std::ostream& operator<<(std::ostream& out, const VariableId& id);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_VARIABLE_ID_H_
