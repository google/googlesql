//
// Copyright 2018 Google LLC
// Copyright 2018 Asylo authors
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOGGING_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOGGING_H_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"

#ifdef NDEBUG
#define GOOGLESQL_DEBUG_MODE false
#else
#define GOOGLESQL_DEBUG_MODE true
#endif

// A ABSL_LOG command with an associated verbosity level. The verbosity threshold
// may be configured at runtime with set_vlog_level and InitLogging.
//
// GOOGLESQL_VLOG statements are logged at INFO severity if they are logged at all.
// The numeric levels are on a different scale than the severity levels.
// Example:
//
//   GOOGLESQL_VLOG(1) << "Print when GOOGLESQL_VLOG level is set to be 1 or higher";
//
// level: the numeric level that determines whether to log the message.
#define GOOGLESQL_VLOG(level) \
  ABSL_LOG_IF(INFO, (level) <= ::googlesql_base::get_vlog_level())

namespace googlesql_base {

// This formats a value for a failing CHECK_XX statement.  Ordinarily,
// it uses the definition for operator<<, with a few special cases below.
template <typename T>
inline void GoogleSqlMakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << v;
}

// Overrides for char types provide readable values for unprintable
// characters.
template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const char& v);
template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const signed char& v);
template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const unsigned char& v);

// We need an explicit specialization for std::nullptr_t.
template <>
void GoogleSqlMakeCheckOpValueString(std::ostream* os, const std::nullptr_t& v);

// A helper class for formatting "expr (V1 vs. V2)" in a CHECK_XX
// statement.  See GoogleSqlMakeCheckOpString for sample usage.
class CheckOpMessageBuilder {
 public:
  // Constructs an object to format a CheckOp message. This constructor
  // initializes the message first with exprtext followed by " (".
  //
  // exprtext A string representation of the code in file at line.
  explicit CheckOpMessageBuilder(const char* exprtext);
  // Deletes "stream_".
  ~CheckOpMessageBuilder();
  // Gets the output stream for the first argument of the message.
  std::ostream* ForVar1() { return stream_; }
  // Gets the output stream for writing the argument of the message. This
  // writes " vs. " to the stream first.
  std::ostream* ForVar2();
  // Gets the built string contents. The stream is finished with an added ")".
  std::string* NewString();

 private:
  std::ostringstream* stream_;
};

template <typename T1, typename T2>
std::string* GoogleSqlMakeCheckOpString(const T1& v1, const T2& v2,
                                        const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  GoogleSqlMakeCheckOpValueString(comb.ForVar1(), v1);
  GoogleSqlMakeCheckOpValueString(comb.ForVar2(), v2);
  return comb.NewString();
}

// Helper functions for CHECK_OP macro.
// The (int, int) specialization works around the issue that the compiler
// will not instantiate the template version of the function on values of
// unnamed enum type - see comment below.
//
// name: an identifier that is the name of the comparison, such as
//       Check_EQ or Check_NE.
// op: the comparison operator, such as == or !=.
#define DEFINE_CHECK_OP_IMPL(name, op)                                     \
  template <typename T1, typename T2>                                      \
  inline std::string* name##Impl(const T1& v1, const T2& v2,               \
                                 const char* exprtext) {                   \
    if (v1 op v2) return nullptr;                                          \
    return ::googlesql_base::GoogleSqlMakeCheckOpString(v1, v2, exprtext); \
  }                                                                        \
  inline std::string* name##Impl(int v1, int v2, const char* exprtext) {   \
    return ::googlesql_base::name##Impl<int, int>(v1, v2, exprtext);       \
  }

// We use the full name Check_EQ, Check_NE, etc.
//
// This is to prevent conflicts when the file including logging.h provides its
// own #defines for the simpler names EQ, NE, etc. This happens if, for
// example, those are used as token names in a yacc grammar.
DEFINE_CHECK_OP_IMPL(Check_EQ, ==)
DEFINE_CHECK_OP_IMPL(Check_NE, !=)
DEFINE_CHECK_OP_IMPL(Check_LE, <=)
DEFINE_CHECK_OP_IMPL(Check_LT, <)
DEFINE_CHECK_OP_IMPL(Check_GE, >=)
DEFINE_CHECK_OP_IMPL(Check_GT, >)
#undef DEFINE_CHECK_OP_IMPL

// Function is overloaded for integral types to allow static const
// integrals declared in classes and not defined to be used as arguments to
// ABSL_CHECK* macros. It's not encouraged though.
template <typename T>
inline const T& GetReferenceableValue(const T& t) {
  return t;
}

inline char GetReferenceableValue(char t) { return t; }
inline unsigned char GetReferenceableValue(unsigned char t) { return t; }
inline signed char GetReferenceableValue(signed char t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline short GetReferenceableValue(short t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline unsigned short GetReferenceableValue(unsigned short t) { return t; }
inline int GetReferenceableValue(int t) { return t; }
inline unsigned int GetReferenceableValue(unsigned int t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline long GetReferenceableValue(long t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline unsigned long GetReferenceableValue(unsigned long t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline long long GetReferenceableValue(long long t) { return t; }
// NOLINTNEXTLINE(runtime/int)
inline unsigned long long GetReferenceableValue(unsigned long long t) {
  return t;
}

#define GOOGLESQL_VLOG_IS_ON(level) \
  ::googlesql_base::get_vlog_level() <= (level)

// Gets the verbosity threshold for GOOGLESQL_VLOG. A GOOGLESQL_VLOG command with a level greater
// than this will be ignored.
int get_vlog_level();

// Gets the log directory that was specified when initialized.
std::string get_log_directory();

// Initializes minimal logging library.
//
// This should be called in main().
//
// directory: log file directory.
// file_name: name of the log file (recommend this be initialized with argv[0]).
// level: verbosity threshold for GOOGLESQL_VLOG commands. A GOOGLESQL_VLOG command with
//        a level equal to or lower than it will be logged.
// Returns true if initialized successfully. Behavior is undefined false.
bool InitLogging(const char* directory, const char* file_name, int level);

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_LOGGING_H_
