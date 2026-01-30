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

#ifndef GOOGLESQL_TESTING_TYPE_UTIL_H_
#define GOOGLESQL_TESTING_TYPE_UTIL_H_

#include <string>
#include <vector>

#include "google/protobuf/descriptor.h"

namespace googlesql {

class Type;
class TypeFactory;

namespace testing {

// Returns true if "type" is or contains fields in double or float type.
bool HasFloatingPointNumber(const googlesql::Type* type);

// Returns the list of complex Types used during compliance testing.
std::vector<const Type*> GoogleSqlComplexTestTypes(
    googlesql::TypeFactory* type_factory,
    const ::google::protobuf::DescriptorPool* descriptor_pool);

// Returns the list of proto files used during compliance testing to create
// complex types.
std::vector<std::string> GoogleSqlTestProtoFilepaths();

// Returns the names of proto messages used during compliance testing.
std::vector<std::string> GoogleSqlTestProtoNames();

// Returns the names of enums used during compliance testing.
std::vector<std::string> GoogleSqlTestEnumNames();

// Returns the names of proto messages used during random query generation.
std::vector<std::string> GoogleSqlRandomTestProtoNames();

}  // namespace testing
}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_TYPE_UTIL_H_
