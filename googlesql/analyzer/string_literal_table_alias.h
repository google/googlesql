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

#ifndef GOOGLESQL_ANALYZER_STRING_LITERAL_TABLE_ALIAS_H_
#define GOOGLESQL_ANALYZER_STRING_LITERAL_TABLE_ALIAS_H_

#include <string>

#include "absl/strings/string_view.h"

namespace googlesql {

// Derives the implicit range variable name for a STRING literal used as a
// table reference without an explicit alias
// (FEATURE_STRING_LITERAL_TABLE_REFERENCES), e.g.:
//
//   FROM '/cns/cell/logs/events@100.tfrecords'  -->  events
//
// `table_literal` must be the unescaped content of the literal.  Returns an
// empty string if no single unambiguous name can be derived.
//
// The rule is format-agnostic:
//  1. A comma outside {...} means a list of paths -> anonymous.  So does a
//     brace group that spans path segments (e.g. '{a/b,c}.csv' or
//     '{a,b/c}.csv'), or that is never closed and contains a comma.
//  2. The basename is the last non-empty segment after splitting on '/' and
//     '\'.  If the basename starts with a wildcard (* ? [ ] { }), the
//     immediate parent segment is used instead, if it has no wildcards.  An
//     empty parent (e.g. in 'gs://*.csv' or 'a//*.csv') gives no alias.
//  3. Everything from the first '.' is stripped, then a trailing shard
//     suffix (@[0-9*]+ or -[0-9*]+-of-[0-9*]+) is stripped.
//  4. If wildcards (* ? [ ] { }) remain, the prefix before the first wildcard
//     is used, with trailing '_', '-' and '@' trimmed.
//  5. Any non-empty result is the alias, even if it isn't a valid unquoted
//     identifier (e.g. `2026-09-24`).  This is the same for names derived from
//     file names and from parent directories.  A result starting with `$`
//     would be an internal alias (see IsInternalAlias), so it gives no alias.
//     A result ending in ':' is a URI scheme or drive letter (e.g. in
//     'gs:/*.csv'), so it also gives no alias.
//
// The literal is treated as bytes; only the ASCII characters above are
// special.  Valid UTF-8 input gives valid UTF-8 output.
//
std::string DeriveImplicitAliasForStringLiteralTable(
    absl::string_view table_literal);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_STRING_LITERAL_TABLE_ALIAS_H_
