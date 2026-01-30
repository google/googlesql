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

#ifndef GOOGLESQL_SCRIPTING_SCRIPT_SEGMENT_H_
#define GOOGLESQL_SCRIPTING_SCRIPT_SEGMENT_H_

#include <ostream>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Represents a segment of a script, containing both the entire text of the
// script in addition to the begin and end location of the segment within it,
// along with an ASTNode representing the text comprising the segment.
class ScriptSegment {
 public:
  // Creates a script segment backed by the specified range within the specified
  // script.  <script> and <node> are owned externally, and must remain alive
  // while the returned ScriptSegment is being used.
  static ScriptSegment FromASTNode(absl::string_view script,
                                   const ASTNode* node);

  ScriptSegment(const ScriptSegment&) = default;
  ScriptSegment& operator=(const ScriptSegment&) = delete;

  // Returns a string_view representing the entire script.
  absl::string_view script() const { return script_; }

  // Returns the start and end locations of the segment.
  // This is NOT the same as node()->GetParseLocationRange(), which can
  // sometimes return only a portion of this range.  See b/113538338 for
  // details.
  const ParseLocationRange& range() const { return range_; }

  // Returns the ASTNode that comprises this segment.
  const ASTNode* node() const { return node_; }

  // Returns a substring of the script containing only the current segment.
  absl::string_view GetSegmentText() const;

 private:
  ScriptSegment(absl::string_view script, const ASTNode* node);

  const absl::string_view script_;
  const ASTNode* node_;
  const ParseLocationRange range_;
};

// Provide better error messages in gmock-based tests when a ScriptSegment
// does not match its expected value.
std::ostream& operator<<(std::ostream& os, const ScriptSegment& segment);
}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_SCRIPT_SEGMENT_H_
