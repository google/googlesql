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

#ifndef GOOGLESQL_PARSER_MACROS_STANDALONE_MACRO_EXPANSION_IMPL_H_
#define GOOGLESQL_PARSER_MACROS_STANDALONE_MACRO_EXPANSION_IMPL_H_

#include <memory>
#include <optional>

#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/macro_expander.h"
#include "googlesql/parser/macros/token_provider.h"
#include "googlesql/public/options.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

inline absl::StatusOr<ExpansionOutput> ExpandMacros(
    absl::string_view filename, absl::string_view input,
    const MacroCatalog& catalog, MacroExpanderOptions macro_expander_options,
    int offset_in_original_input) {
  return MacroExpander::ExpandMacros(
      std::make_unique<TokenProvider>(filename, input, /*start_offset=*/0,
                                      /*end_offset=*/std::nullopt,
                                      offset_in_original_input),
      catalog, macro_expander_options);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_STANDALONE_MACRO_EXPANSION_IMPL_H_
