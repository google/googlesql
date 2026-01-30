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

#include "googlesql/parser/macros/macro_catalog.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

absl::Status MacroCatalog::RegisterMacro(MacroInfo macro_info) {
  std::string macro_name(macro_info.name());
  auto it = macros_->find(macro_name);
  if (it != macros_->end()) {
    if (!options_.allow_overwrite) {
      return absl::AlreadyExistsError(
          absl::StrCat("Macro ", macro_name, " already exists"));
    }
  } else {
    auto [jt, success] = macros_->insert_or_assign(macro_name, {});
    if (!success) {
      return absl::InternalError("Error adding macro definition");
    }
    it = jt;
  }
  it->second[version_id_] = macro_info;
  return absl::OkStatus();
}

std::optional<MacroInfo> MacroCatalog::Find(
    absl::string_view macro_name) const {
  auto it = macros_->find(macro_name);
  if (it == macros_->end()) {
    return std::nullopt;
  }
  auto jt = it->second.upper_bound(version_id_);
  // If the upper bound iterator lands at begin, then the macro was not defined
  // before the current version.
  if (jt == it->second.begin()) {
    return std::nullopt;
  }
  --jt;
  return jt->second;
}

std::unique_ptr<MacroCatalog> MacroCatalog::NewVersion() {
  auto new_catalog = std::make_unique<MacroCatalog>();
  *new_catalog = *this;
  new_catalog->version_id_ = version_id_ + 1;
  return new_catalog;
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
