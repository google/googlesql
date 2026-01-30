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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_OSS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_OSS_H_

#include <dirent.h>
#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>  // NOLINT
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>  // NOLINT
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/path.h"
#include "re2/re2.h"

namespace googlesql::internal {

inline absl::Status NullFreeString(absl::string_view str,
                                   std::string* out_str) {
  if (str.find('\0') != absl::string_view::npos) {
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("filename contains null characters: ", str));
  }
  *out_str = std::string(str);
  return absl::OkStatus();
}

inline absl::Status Match(absl::string_view filespec,
                          std::vector<std::string>* file_names) {
  std::string filename_str;
  if (absl::Status status = NullFreeString(filespec, &filename_str);
      !status.ok()) {
    return status;
  }

  // We pass this c-libraries, so must store as string to get null terminator.
  std::string dirname_str = std::string(googlesql_base::Dirname(filespec));
  absl::string_view filename = googlesql_base::Basename(filespec);

  if (absl::StrContains(dirname_str, "*")) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("googlesql_base::Match does not support "
                                     "wild cards (*) in directory names: ",
                                     filespec));
  }
  if (filename.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("googlesql_base::Match requires a "
                                     "filespec; found only a directory: ",
                                     filespec));
  }

  std::vector<std::string> tokens = absl::StrSplit(filename, "*");
  auto re2_escape = [](std::string* out, std::string in) {
    if (!in.empty()) {
      out->append(RE2::QuoteMeta(in));
    }
  };

  std::string filespec_re2 = absl::StrJoin(tokens, ".*", re2_escape);
  DIR* dir = opendir(dirname_str.data());
  if (dir == nullptr) {
    return absl::Status(
        absl::StatusCode::kNotFound,
        absl::StrCat("Could not find directory: ", dirname_str));
  }

  struct dirent* entry;

  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_type != DT_REG && entry->d_type != DT_LNK) {
      continue;
    }
    std::string file = entry->d_name;
    if (RE2::FullMatch(file, filespec_re2)) {
      file_names->push_back(googlesql_base::JoinPath(dirname_str, file));
    }
  }

  return absl::OkStatus();
}

inline absl::Status GetContents(absl::string_view filename,
                                std::string* file_contents) {
  // Because we are using a c api, check for in-string nulls.
  std::string filename_str;
  if (absl::Status status = NullFreeString(filename, &filename_str);
      !status.ok()) {
    return status;
  }

  struct stat status;
  if (stat(filename.data(), &status) != 0) {
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Could not find", filename));
  } else if (S_ISREG(status.st_mode) == 0) {
    return absl::Status(absl::StatusCode::kFailedPrecondition,
                        absl::StrCat("File is not regular", filename));
  }

  std::ifstream stream(std::string(filename), std::ifstream::in);
  if (!stream) {
    // Could be a wider range of reasons.
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Unable to open: ", filename));
  }
  stream.seekg(0, stream.end);
  int length = stream.tellg();
  stream.seekg(0, stream.beg);
  file_contents->clear();
  file_contents->resize(length);
  stream.read(&(*file_contents)[0], length);
  stream.close();
  return absl::OkStatus();
}

inline absl::Status SetContents(absl::string_view filename,
                                absl::string_view file_contents) {
  // Because we are using a c api, check for in-string nulls.
  std::string filename_str;
  if (absl::Status status = NullFreeString(filename, &filename_str);
      !status.ok()) {
    return status;
  }
  std::ofstream stream(filename_str, std::ofstream::out);

  if (!stream) {
    // Could be a wider range of reasons.
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Unable to open: ", filename));
  }
  stream.write(file_contents.data(), file_contents.size());
  stream.close();
  return absl::OkStatus();
}

// The path in blaze where we expect to find inputs like `.test` files.
inline std::string TestSrcRootDir() {
  return googlesql_base::JoinPath(getenv("TEST_SRCDIR"), "_main");
}

// The path in bazel where we expect to find inputs like `.test` files.
inline std::string TestTmpDir() { return getenv("TEST_TMPDIR"); }

inline absl::Status RecursivelyCreateDir(absl::string_view directory) {
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    // Always return kUnknown for now because there is no need to distinguish
    // the underlying error. May be updated to check `ec` and produce different
    // codes, if required in the future.
    return absl::Status(
        absl::StatusCode::kUnknown,
        absl::StrCat("Error creating directory: ", ec.message()));
  }
  return absl::OkStatus();
};

inline absl::Status Copy(absl::string_view from, absl::string_view to) {
  std::error_code ec;
  std::filesystem::copy(from, to, ec);
  if (ec) {
    // Always return kUnknown for now because there is no need to distinguish
    // the underlying error. May be updated to check `ec` and produce different
    // codes, if required in the future.
    return absl::Status(
        absl::StatusCode::kUnknown,
        absl::StrCat("Error copying file: ", ec.message()));
  }
  return absl::OkStatus();
};

}  // namespace googlesql::internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FILE_UTIL_OSS_H_
