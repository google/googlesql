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

#include "googlesql/public/functions/format_max_output_width.h"

#include "absl/flags/flag.h"

ABSL_FLAG(int32_t, googlesql_format_max_output_width, 1024 * 1024,
          "Maximum width of a string returned from the FORMAT function. "
          "If a call would result in a wider value, it will fail at "
           "runtime.");
