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

#ifndef GOOGLESQL_SCRIPTING_STACK_FRAME_H_
#define GOOGLESQL_SCRIPTING_STACK_FRAME_H_

#include "googlesql/parser/parse_tree.h"
#include "googlesql/scripting/parsed_script.h"

namespace googlesql {

// Represents a stack frame in script execution.
class StackFrame {
 public:
  virtual ~StackFrame() = default;

  // ParsedScript associated with the current script/procedure.
  virtual const ParsedScript* parsed_script() const = 0;

  // Whether or not the stack frame is dynamic SQL.
  virtual bool is_dynamic_sql() const = 0;

  // AST node that is currently executing.  For frames other than the leaf
  // frame, this is always a CALL statement.
  //
  // If the entire script is complete, there is one stack frame, whose
  // current_node() is nullptr.
  virtual const ControlFlowNode* current_node() const = 0;
};

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_STACK_FRAME_H_
