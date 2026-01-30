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

#ifndef GOOGLESQL_PUBLIC_COLLATOR_H_
#define GOOGLESQL_PUBLIC_COLLATOR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "unicode/tblcoll.h"
#include "googlesql/base/status.h"

namespace googlesql {

// How the legacy "unicode" special collation name is handled.
// See (broken link) for more background.
enum class CollatorLegacyUnicodeMode {
  // 'unicode' is considered an error. This is the desired state.
  kError,

  // 'unicode' is allowed, and is an alias for 'und' language tag in icu.
  kLegacyIcuOnly,
};

// This class is specific to the COLLATE clause in googlesql.
// For more information on collate semantics in googlesql, see
//
// https://github.com/google/googlesql/blob/master/docs/query-syntax.md#collate
//
// Usage:
// absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator =
//     googlesql::MakeSqlCollator("en_US:ci");
// GOOGLESQL_RETURN_IF_ERROR(collator);
// absl::Status error;
// int64 compare_result = (*collator)->CompareUtf8(s1, s2, &error);
class GoogleSqlCollator {
 public:
  GoogleSqlCollator(const GoogleSqlCollator&) = delete;
  GoogleSqlCollator& operator=(const GoogleSqlCollator&) = delete;
  virtual ~GoogleSqlCollator() = 0;

  // A three valued string compare method based on the collate specific rules.
  //
  // Returns -1 if s1 is less than s2.
  // Returns 1 if s1 is greater than s2.
  // Returns 0 is s1 is equal to s2.
  //
  // If an error occurs, <*error> will be updated.
  // Errors will never occur if <s1> and <s2> are valid UTF-8.
  virtual int64_t CompareUtf8(absl::string_view s1, absl::string_view s2,
                              absl::Status* error) const = 0;

  // Generates the sort key from <input> as an array of bytes, and returns it in
  // <output>. Sort keys can be compared using memcmp() honoring collation
  // settings.
  //
  // Errors will never occur if <input> is a valid UTF-8.
  virtual absl::Status GetSortKeyUtf8(absl::string_view input,
                                      absl::Cord* output) const = 0;

  // Returns true if this collator uses simple binary comparisons.
  // If true, engines can get equivalent behavior using binary comparison on
  // strings rather than using CompareUtf8, which may allow for more efficient
  // implementation.
  virtual bool IsBinaryComparison() const = 0;

  // Returns the underlying icu::RuleBasedCollator if there is one used by this
  // GoogleSqlCollator, nullptr otherwise.
  virtual const icu::RuleBasedCollator* GetIcuCollator() const = 0;

  absl::string_view GetCollationName() const { return collation_name_; }

  friend absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
  MakeSqlCollator(absl::string_view collation_name,
                  CollatorLegacyUnicodeMode mode);

  friend absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
  MakeSqlCollatorLite(absl::string_view collation_name,
                      CollatorLegacyUnicodeMode mode);

 protected:
  explicit GoogleSqlCollator(absl::string_view collation_name)
      : collation_name_(collation_name) {}
  GoogleSqlCollator() = default;
  std::string collation_name_;
};

// Returns a instance of GoogleSqlCollator corresponding to the given
// <collation_name>. Returns error if <collation_name> is not valid.
//
// Your build target must depend on :collator to use this function;
// :collator_lite is insufficient.
//
// A <collation_name> is composed as "<language_tag> ( ':' <attribute> )*".
// - <language_tag> is considered valid only if it is "unicode" (which means
//   use default unicode collation) or
//   LanguagCodeConverter::GetStatusFromOther(<language_tag>) returns status
//   i18n_identifiers::CANONICAL.
// - <attribute> is an option to customize the collation order.
//   The only supported attributes are:
//     ":ci" Use case-insensitive comparison
//     ":cs" Use case-sensitive comparison
//
absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> MakeSqlCollator(
    absl::string_view collation_name, CollatorLegacyUnicodeMode mode);

inline absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> MakeSqlCollator(
    absl::string_view collation_name) {
  return MakeSqlCollator(collation_name,
                         CollatorLegacyUnicodeMode::kLegacyIcuOnly
  );
}

// This lightweight version of MakeFromCollationName() supports only the
// "binary" collation (but see below), unless the full collator implementation
// has been linked in and statically registered, in which case it behaves the
// same way as CreateFromCollationName() above.
//
// GoogleSQL end users should not have any reason to call this function over
// CreateFromCollationName(); it is mostly an implementation detail of the
// :evaluator_lite target. It switches between the "lite" implementation and
// the full ICU-based implementation based on whether the latter has been
// registered with googlesql::internal::RegisterIcuCollatorImpl().
//
// Note on Supported Collations
// This support 'binary', 'unicode' and/or 'unicode:cs' depending on
// the value of 'mode'.
absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> MakeSqlCollatorLite(
    absl::string_view collation_name, CollatorLegacyUnicodeMode mode);

inline absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
MakeSqlCollatorLite(absl::string_view collation_name) {
  return MakeSqlCollatorLite(collation_name,
                             CollatorLegacyUnicodeMode::kLegacyIcuOnly
  );
}

namespace internal {
// Globally registers the collator implementation to be used by
// GoogleSqlCollator::CreateFromCollationNameLite().
// For internal GoogleSQL use only.
void RegisterIcuCollatorImpl(
    std::function<absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>(
        absl::string_view, CollatorLegacyUnicodeMode legacy_unicode_mode)>
        create_fn);

// Resets the globally registered collator implementation to the lightweight
// default. For testing only.
void RegisterDefaultCollatorImpl();
}  // namespace internal

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_COLLATOR_H_
