//
// Copyright 2018 Google LLC
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

#include "googlesql/base/logging.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstddef>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"

namespace googlesql_base {

constexpr char kDefaultDirectory[] = "/tmp/";

namespace {

// The logging directory.
ABSL_CONST_INIT std::string *log_file_directory = nullptr;

// The log filename.
ABSL_CONST_INIT std::string *log_basename = nullptr;

// The ZETASQL_VLOG level, only ZETASQL_VLOG with level equal to or below this
// level is logged.
ABSL_CONST_INIT int vlog_level = 0;

const char *GetBasename(const char *file_path) {
  const char *slash = strrchr(file_path, '/');
  return slash ? slash + 1 : file_path;
}

bool set_log_basename(const std::string &filename) {
  if (log_basename || filename.empty()) {
    return false;
  }
  log_basename = new std::string(filename);
  return true;
}

std::string get_log_basename() {
  if (!log_basename || log_basename->empty()) {
    return "googlesql";
  }
  return *log_basename;
}

bool EnsureDirectoryExists(const char *path) {
  struct stat dirStat;
  if (stat(path, &dirStat)) {
    if (errno != ENOENT) {
      return false;
    }
    if (mkdir(path, 0766)) {
      return false;
    }
  } else if (!S_ISDIR(dirStat.st_mode)) {
    return false;
  }
  return true;
}

// Sets the log directory, as specified when initialized. This
// is only set once. Any request to reset it will return false.
//
// log_directory: log file directory.
//
// Returns true if and only if the log directory is set successfully.
bool set_log_directory(const std::string &log_directory) {
  std::string tmp_directory = log_directory;
  if (tmp_directory.empty()) {
    tmp_directory = kDefaultDirectory;
  }
  if (log_file_directory || !EnsureDirectoryExists(tmp_directory.c_str())) {
    return false;
  }
  if (tmp_directory.back() == '/') {
    log_file_directory = new std::string(tmp_directory);
  } else {
    log_file_directory = new std::string(tmp_directory + "/");
  }
  return true;
}

// Sets the verbosity threshold for ZETASQL_VLOG. A ZETASQL_VLOG command with a
// level greater
// than this will be ignored.
//
// level: verbosity threshold for GOOGLESQL_VLOG to be set. A GOOGLESQL_VLOG command with
//        level less than or equal to this will be logged.
void set_vlog_level(int level) { vlog_level = level; }

}  // namespace

std::string get_log_directory() {
  if (!log_file_directory) {
    return kDefaultDirectory;
  }
  return *log_file_directory;
}

int get_vlog_level() { return vlog_level; }

bool InitLogging(const char *directory, const char *file_name, int level) {
  set_vlog_level(level);
  std::string log_directory = directory ? std::string(directory) : "";
  if (!set_log_directory(log_directory)) {
    return false;
  }
  const char *binary_name = GetBasename(file_name);
  if (!set_log_basename(binary_name)) {
    return false;
  }
  std::string log_path = get_log_directory() + get_log_basename();
  if (access(log_path.c_str(), F_OK) == 0 &&
      access(log_path.c_str(), W_OK) != 0) {
    return false;
  }
  return true;
}


CheckOpMessageBuilder::CheckOpMessageBuilder(const char *exprtext)
    : stream_(new std::ostringstream) {
  *stream_ << exprtext << " (";
}

CheckOpMessageBuilder::~CheckOpMessageBuilder() { delete stream_; }

std::ostream *CheckOpMessageBuilder::ForVar2() {
  *stream_ << " vs. ";
  return stream_;
}

std::string *CheckOpMessageBuilder::NewString() {  // NOLINT
  *stream_ << ")";
  return new std::string(stream_->str());
}

template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "char value " << static_cast<int16_t>(v);
  }
}

template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const signed char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "signed char value " << static_cast<int16_t>(v);
  }
}

template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const unsigned char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "unsigned char value " << static_cast<uint16_t>(v);
  }
}

template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os,
                                     const std::nullptr_t& v) {
  (*os) << "nullptr";
}

}  // namespace googlesql_base
