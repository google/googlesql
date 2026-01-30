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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_SOURCE_LOCATION_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_SOURCE_LOCATION_H_

// API for capturing source-code location information.
// Based on http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4519.pdf.
//
// To define a function that has access to the source location of the
// callsite, define it with a parameter of type
// `googlesql_base::SourceLocation`. The caller can then invoke the function,
// passing `GOOGLESQL_LOC` as the argument.
//

#include <cstdint>

#include "absl/base/config.h"

#if defined(__is_identifier)
#define GOOGLESQL_INTERNAL_HAS_KEYWORD(x) !(__is_identifier(x))
#else
#define GOOGLESQL_INTERNAL_HAS_KEYWORD(x) 0
#endif

#if !defined(GOOGLESQL_INTERNAL_HAVE_SOURCE_LOCATION_CURRENT)
#if GOOGLESQL_INTERNAL_HAS_KEYWORD(__builtin_LINE) && \
    GOOGLESQL_INTERNAL_HAS_KEYWORD(__builtin_FILE)
#define GOOGLESQL_INTERNAL_HAVE_SOURCE_LOCATION_CURRENT 1
#else
#define GOOGLESQL_INTERNAL_HAVE_SOURCE_LOCATION_CURRENT 0
#endif
#endif

#undef GOOGLESQL_INTERNAL_HAS_KEYWORD

namespace googlesql_base {

// Class representing a specific location in the source code of a program.
// `googlesql_base::SourceLocation` is copyable.
class SourceLocation {
  struct PrivateTag {
   private:
    explicit PrivateTag() = default;
    friend class SourceLocation;
  };

 public:
  // Avoid this constructor; it populates the object with dummy values.
  constexpr SourceLocation()
      : line_(0),
        file_name_(nullptr) {}

  // Wrapper to invoke the private constructor below. This should only be used
  // by the `GOOGLESQL_LOC` macro, hence the name.
  static constexpr SourceLocation DoNotInvokeDirectly(std::uint_least32_t line,
                                                      const char* file_name) {
    return SourceLocation(line, file_name);
  }


#if GOOGLESQL_INTERNAL_HAVE_SOURCE_LOCATION_CURRENT
  // SourceLocation::current
  //
  // Creates a `SourceLocation` based on the current line and file.  APIs that
  // accept a `SourceLocation` as a default parameter can use this to capture
  // their caller's locations.
  //
  // Example:
  //
  //   void TracedAdd(int i, SourceLocation loc = SourceLocation::current()) {
  //     std::cout << loc.file_name() << ":" << loc.line() << " added " << i;
  //     ...
  //   }
  //
  //   void UserCode() {
  //     TracedAdd(1);
  //     TracedAdd(2);
  //   }
  static constexpr SourceLocation current(
      PrivateTag = PrivateTag{}, std::uint_least32_t line = __builtin_LINE(),
      const char* file_name = __builtin_FILE()) {
    return SourceLocation(line, file_name);
  }
#else
  // Creates a dummy `SourceLocation` of "<source_location>" at line number 1,
  // if no `SourceLocation::current()` implementation is available.
  static constexpr SourceLocation current() {
    return SourceLocation(1, "<source_location>");
  }
#endif
  // The line number of the captured source location.
  constexpr std::uint_least32_t line() const { return line_; }

  // The file name of the captured source location.
  constexpr const char* file_name() const { return file_name_; }

  // `column()` and `function_name()` are omitted because we don't have a way to
  // support them.

 private:
  // Do not invoke this constructor directly. Instead, use the `GOOGLESQL_LOC`
  // macro below.
  //
  // `file_name` must outlive all copies of the `googlesql_base::SourceLocation`
  // object, so in practice it should be a string literal.
  constexpr SourceLocation(std::uint_least32_t line, const char* file_name)
      : line_(line),
        file_name_(file_name) {}

  std::uint_least32_t line_;
  const char* file_name_;
};

}  // namespace googlesql_base

// If a function takes an `googlesql_base::SourceLocation` parameter, pass this
// as the argument.
#define GOOGLESQL_LOC \
  ::googlesql_base::SourceLocation::DoNotInvokeDirectly(__LINE__, __FILE__)

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_SOURCE_LOCATION_H_
