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

#ifndef GOOGLESQL_ANALYZER_PATH_EXPRESSION_SPAN_H_
#define GOOGLESQL_ANALYZER_PATH_EXPRESSION_SPAN_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_generated.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/base/check.h"

namespace googlesql {

// Point to a contiguous span of identifiers within an ASTPathExpression. This
// is useful as an input to resolution steps that can operate on a complete
// path, prefixes, suffixes, or simply sub-spans of an identifier path.
// This class provides a copy-cheap view of the given parsed AST node without
// modifying it. It attaches analyzer metadata to parsed AST node, including a
// starting (inclusive) and an ending index (exclusive) of identifier path array
// to indicate a slicing range against ASTPathExpression::names().
// The metadata remedies the fact that parsed AST node does not expose setter or
// standalone constructor for usage outside the parser arena.
// Note that, the caller will need to verify that the input node is not nullptr.
class PathExpressionSpan {
 public:
  explicit PathExpressionSpan(const ASTPathExpression& node)
      : node_(node),
        start_index_(0),
        end_index_(static_cast<int32_t>(node.num_names())) {
    ABSL_DCHECK_GE(node.num_names(), 0);
    ABSL_DCHECK_LE(node.num_names(), std::numeric_limits<int32_t>::max());
  }

  // Validate the input close-open range [start, end) for the current path
  // expression node and create a sub-span as a copy of the current span.
  // An error will be returned on invalid input.
  absl::StatusOr<PathExpressionSpan> subspan(int32_t start, int32_t end);

  // The default constructor is disabled
  PathExpressionSpan() = delete;
  PathExpressionSpan(const PathExpressionSpan&) = default;
  PathExpressionSpan& operator=(const PathExpressionSpan&) = default;
  PathExpressionSpan(PathExpressionSpan&& other) = default;
  PathExpressionSpan& operator=(PathExpressionSpan&& other) = default;

  // The number of names within the range of span.
  int32_t num_names() const;

  const ASTIdentifier* first_name() const;
  IdString GetFirstIdString() const;

  // The name at a position relative to the starting index.
  // The caller needs to verify <relative_index> is in the range of
  // [0, num_names()).
  const ASTIdentifier* name(int32_t relative_index) const;

  // Return a vector of IdString's within the range of span.
  std::vector<IdString> ToIdStringVector() const;

  // Return a vector of identifier strings within the range of span (without
  // quoting).
  std::vector<std::string> ToIdentifierVector() const;

  // Return a dotted SQL identifier string representation of the path expression
  // span, with quoting if necessary. If <max_prefix_size> is non-zero, include
  // at most that many identifiers starting from <start_index_>.
  std::string ToIdentifierPathString(size_t max_prefix_size = 0) const;

  // The parsed location range for the defined span.
  ParseLocationRange GetParseLocationRange();

 private:
  explicit PathExpressionSpan(const ASTPathExpression& node, int start, int end)
      : node_(node), start_index_(start), end_index_(end) {}

  const ASTPathExpression& node_;
  int32_t start_index_;
  int32_t end_index_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_PATH_EXPRESSION_SPAN_H_
