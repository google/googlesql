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

#ifndef GOOGLESQL_COMMON_INITIALIZE_REQUIRED_FIELDS_H_
#define GOOGLESQL_COMMON_INITIALIZE_REQUIRED_FIELDS_H_

#include "google/protobuf/message.h"

namespace googlesql {

// For all required fields of message, default-initializes them if they're not
// yet set.  If default values aren't specified in the .proto file, numbers are
// initialized to 0 or 0.0, bools to false, strings to the empty string, and
// enums to the appropriate default enum value.
void InitializeRequiredFields(google::protobuf::Message* message);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_INITIALIZE_REQUIRED_FIELDS_H_
