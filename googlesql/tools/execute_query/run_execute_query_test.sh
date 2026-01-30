#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#!/bin/bash
# This script is used under the execute_query_test build rule as part of a
# sh_test to invoke execute_query on a file.

if [[ $# -lt 2 ]]; then
  echo "Usage: run_execute_query_test.sh <tool_binary> <sql_file> [<args> ...]"
  exit 1
fi

TOOL="$1"
shift

FILE="$1"
shift

echo "Tool binary is $TOOL"
echo "Input file is $FILE"
echo "pwd is $(pwd)"
echo

if [[ ! -f "$FILE" ]]; then
  echo "Failed: Can't read file $FILE"
  exit 1
fi

echo "Running..."
"$TOOL" " $(cat "$FILE")" "$@"
exit $?
