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

#ifndef GOOGLESQL_PUBLIC_SQL_FORMATTER_H_
#define GOOGLESQL_PUBLIC_SQL_FORMATTER_H_

#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Formats GoogleSQL statements.  Multiple statements separated by semi-colons
// are supported.
//
// On return, <*formatted_sql> is always populated with equivalent SQL.
// The returned error Status contains the concatenation of any errors that
// occurred while parsing the statements.
//
// Any statements that fail to parse as valid GoogleSQL are returned unchanged.
// All valid statements will be reformatted.
//
// CAVEATS:
// 1. This can only reformat SQL statements that can be parsed successfully.
// Statements that cannot be parsed are returned unchanged.
// 2. Comments are stripped in the formatted output.
// If you need formatting unparseable SQL (e.g. containing custom templates) or
// want to preserve comments, consider using googlesql::LenientFormatSql.
absl::Status FormatSql(absl::string_view sql, std::string* formatted_sql);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SQL_FORMATTER_H_
