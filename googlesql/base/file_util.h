//
// Copyright 2020 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_H_

// This file abstract file system interactions so they can be more easily
// implemented in the open source version. The API is documented here for
// convenience.
//
#include "googlesql/base/file_util_oss.h"

namespace googlesql::internal {

//
// Match()
//
// Return a list of files that match the given filespec. Supports simple
// wild card matching only (i.e. "*.test").
//
// `filespec` must not contain '\0'.
//
// absl::Status Match(absl::string_view filespec,
//                    std::vector<std::string>* file_names);

//
// GetContents()
//
// Read the entire contents of `filename` into `file_contents`.
//
// `filename` must not contain '\0'.
//
// absl::Status GetContents(absl::string_view filename,
//                          std::string* file_contents);

//
// SetContents()
//
// Overwrite the contents of `filename` with `file_contents`.
//
// `filename` must not contain '\0'.
//
// absl::Status GetContents(absl::string_view filename,
//                          std::string* file_contents);
//

// TestSrcRootDir()
//
// The path in blaze/bazel where we expect to find inputs like `.test` files.
//
// std::string TestSrcRootDir();
//

// TestTmpDir()
//
// An absolute path to a directory where we can write temporary files.
//
// std::string TestTmpDir();
//

// RecursivelyCreateDir()
//
// Recursively creates the given directory path. If the directory already
// exists, do nothing and return OK.
//
// absl::Status RecursivelyCreateDir(absl::string_view directory);

// Copy()
//
// Copies the given file to the target location. The target directory should
// already exist.
//
// absl::Status Copy(absl::string_view from, absl::string_view to);

}  // namespace googlesql::internal
#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_H_
