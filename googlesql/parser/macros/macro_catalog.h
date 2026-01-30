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

#ifndef GOOGLESQL_PARSER_MACROS_MACRO_CATALOG_H_
#define GOOGLESQL_PARSER_MACROS_MACRO_CATALOG_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "googlesql/public/parse_location.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

// Keep this cheap to copy.
struct MacroInfo {

  // The contents of the source where this macro was defined. This is needed
  // when printing error messages to show the definition in its context.
  absl::string_view source_text;

  // Location of the macro definition, starting from the DEFINE keyword.
  ParseLocationRange location;

  // Location of the macro name.
  ParseLocationRange name_location;

  // Location of the start of the macro body.
  ParseLocationRange body_location;

  // The offset of the macro definition in its file. This is important to
  // to decide whether tokens were originally adjacent or not. See b/389149112.
  int definition_start_offset = 0;

  // Optional line and column where the macro definition starts. Useful when
  // the full original input source is unavailable, and `source_text` contains
  // only the definition source, to report accurate locations.
  // Note that these are not themselves the offsets, but rather the actual
  // line and column in the original input source, 1-based.
  // The offsets are simply computed by subtracting 1.
  int definition_start_line = 1;
  int definition_start_column = 1;

  // Returns the name of this macro.
  absl::string_view name() const {
    return name_location.GetTextFrom(source_text);
  }

  // Returns the body of this macro.
  absl::string_view body() const {
    return body_location.GetTextFrom(source_text);
  }

  // TODO: b/310027386 - Use default == operator once ZetaSQL builds with C++20.
  friend bool operator==(const MacroInfo& lhs, const MacroInfo& rhs) {
    return lhs.source_text == rhs.source_text && lhs.location == rhs.location &&
           lhs.name_location == rhs.name_location &&
           lhs.body_location == rhs.body_location &&
           lhs.definition_start_offset == rhs.definition_start_offset &&
           lhs.definition_start_line == rhs.definition_start_line &&
           lhs.definition_start_column == rhs.definition_start_column;
  }
};

// Keep this cheap to copy.
struct MacroCatalogOptions {
  // If enabled, allows overwriting existing macro definitions in the catalog
  // when registering a macro with an existing name.
  bool allow_overwrite = false;
};

// Represents the catalog of existing macros and their definitions.
// This will likely develop into an interface for more sophisticated catalog in
// the future, like catalog.h, with multi-part paths.
class MacroCatalog {
 public:
  explicit MacroCatalog(MacroCatalogOptions options = {})
      : options_(options),
        macros_(std::make_shared<
                absl::node_hash_map<std::string, std::map<int, MacroInfo>>>()) {
  }

  // Registers the given macro. Returns an error if it fails, i.e. because a
  // macro with this name already exists and `allow_overwrite_` is not enabled.
  absl::Status RegisterMacro(MacroInfo macro_info);

  // Returns the MacroInfo for the given name, or nullopt if not found.
  std::optional<MacroInfo> Find(absl::string_view macro_name) const;

  // In case of a macro redefinition, returns a new version of the catalog
  // starting from the given version_id.
  std::unique_ptr<MacroCatalog> NewVersion();

 private:
  // Make the copy constructor private so that the class is not externally
  // copyable. This is to avoid any inconsistent shared state between the
  // different versions due to changes made to copies.
  MacroCatalog(const MacroCatalog& other) = default;

  // The version at and after which this catalog is valid.
  int version_id_ = 0;

  MacroCatalogOptions options_;

  // Uses node_hash_map<> for pointer stability. We use a shared pointer for
  // shared ownership across different versions of the catalog.
  std::shared_ptr<absl::node_hash_map<std::string, std::map<int, MacroInfo>>>
      macros_ = nullptr;
};

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_MACRO_CATALOG_H_
