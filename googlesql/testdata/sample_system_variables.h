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

#ifndef GOOGLESQL_TESTDATA_SAMPLE_SYSTEM_VARIABLES_H_
#define GOOGLESQL_TESTDATA_SAMPLE_SYSTEM_VARIABLES_H_

#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/type.h"

namespace googlesql {
// Adds a set of sample system variables to the analyzer options.
// See sample_system_variables.cc for the names and types of variables added.
void SetupSampleSystemVariables(TypeFactory* type_factory,
                                AnalyzerOptions* options);
}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_SAMPLE_SYSTEM_VARIABLES_H_
