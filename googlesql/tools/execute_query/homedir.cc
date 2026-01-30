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

#include "googlesql/tools/execute_query/homedir.h"

#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include <pwd.h>
#include <sys/types.h>

#include "absl/types/optional.h"

namespace googlesql {

std::optional<std::string> GetHomedir() {
  if (const char* home = ::getenv("HOME"); home != nullptr) {
    return home;
  }

  if (const struct passwd *pwd = getpwuid(getuid()); pwd != nullptr) {
    return pwd->pw_dir;
  }

  return std::nullopt;
}

}  // namespace googlesql
