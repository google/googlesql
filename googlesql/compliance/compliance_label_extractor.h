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

#ifndef GOOGLESQL_COMPLIANCE_COMPLIANCE_LABEL_EXTRACTOR_H_
#define GOOGLESQL_COMPLIANCE_COMPLIANCE_LABEL_EXTRACTOR_H_

#include <set>
#include <string>

#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/btree_set.h"

namespace googlesql {

// Public API for extracting all computed compliance test labels from a resolved
// AST tree. Label strings are represented based on pre-defined categories:
//   1) Used TypeKind;
//   2) ResolvedNodeKind for ResolvedScan, ResolvedStatement and ResolvedExpr
//      node type;
//   3) FunctionSignature with 4 types of granularities: operator or function,
//      function name prefix group, SQLName and FunctionSignatureId;
//   4) Type cast function call;
//
// See `ComplianceLabelSets::GenerateLabelStrings` for detailed label string
// format.
// See (broken link):engine_compliance_reportcard for design doc.
absl::Status ExtractComplianceLabels(
    const ResolvedNode* node,
    absl::btree_set<std::string>& labels_out);

// A function's prefix group represents the type of prefix being extracted.
// There are three types of prefix groups and a function can only belong to one
// group at a time:
//   1) No prefix group;
//   2) Underscore prefix group;
//   3) Dot prefix group.
enum class PrefixGroup { kNone = 0, kDot = 1, kUnderscore = 2 };

// Identifies a function's prefix group and returns extracted prefix based on
// its sql name if the prefix exists in valid set. Valid prefix sets are
// established from GetBuiltinFunctionsAndTypes. 'function_prefix_out' will be
// set to a substring of 'function_sql_name'.
void ExtractPrefixGroupAndFunctionPrefix(absl::string_view function_sql_name,
                                         absl::string_view& function_prefix_out,
                                         PrefixGroup& prefix_group_out);

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_COMPLIANCE_LABEL_EXTRACTOR_H_
