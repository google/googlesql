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

#include <cstdint>
#include <functional>
#include <memory>

#include "googlesql/public/collator.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/synchronization/mutex.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace {

class CaseSensitiveUnicodeCollator : public GoogleSqlCollator {
 public:
  explicit CaseSensitiveUnicodeCollator(absl::string_view collation_name)
      : GoogleSqlCollator(collation_name) {}

  int64_t CompareUtf8(absl::string_view s1, absl::string_view s2,
                      absl::Status* error) const override {
    const int result = s1.compare(s2);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
  }

  absl::Status GetSortKeyUtf8(absl::string_view input,
                              absl::Cord* output) const override {
    *output = input;
    return absl::OkStatus();
  }

  bool IsBinaryComparison() const override { return true; }

  const icu::RuleBasedCollator* GetIcuCollator() const override {
    return nullptr;
  }
};

class CollatorRegistration {
 public:
  using CreateFromCollationNameFn =
      std::function<absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>(
          absl::string_view collation_name, CollatorLegacyUnicodeMode mode)>;

  CollatorRegistration() {
    registered_fn_ = &CollatorRegistration::DefaultCreateFromCollationNameFn;
  }
  CollatorRegistration(const CollatorRegistration&) = delete;
  CollatorRegistration& operator=(const CollatorRegistration&) = delete;

  static CollatorRegistration* GetInstance() {
    static auto* instance = new CollatorRegistration();
    return instance;
  }

  void SetCreateFromCollationNameFn(const CreateFromCollationNameFn& fn) {
    absl::MutexLock lock(&mu_);
    registered_fn_ = fn;
  }

  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
  CreateFromCollationName(absl::string_view collation_name,
                          CollatorLegacyUnicodeMode mode) {
    absl::MutexLock lock(&mu_);
    return registered_fn_(collation_name, mode);
  }

  // This default function returns a basic case-sensitive Unicode collator
  // if that's what is requested, and fails otherwise. The ICU
  // implementation is needed for any more complex collations.
  static absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
  DefaultCreateFromCollationNameFn(absl::string_view collation_name,
                                   CollatorLegacyUnicodeMode mode) {
    const bool is_unicode =
        collation_name == "unicode" || collation_name == "unicode:cs";

    if (collation_name == "binary" ||
        (mode != CollatorLegacyUnicodeMode::kError && is_unicode)) {
      return std::make_unique<CaseSensitiveUnicodeCollator>(collation_name);
    }
    // Should match googlesql::MakeEvalError(), but we want to avoid pulling
    // in those dependencies.
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "Invalid collation_string '" << collation_name << "': "
           << " collator is not registered in this binary";
  }

 private:
  absl::Mutex mu_;
  CreateFromCollationNameFn registered_fn_ ABSL_GUARDED_BY(mu_);
};

}  // namespace

// Destructor for the pure virtual class is defined here, not in collator.cc, so
// that :collator_lite can be used without depending on :collator (which itself
// has a dependency on :collator_lite).
GoogleSqlCollator::~GoogleSqlCollator() {}

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> MakeSqlCollatorLite(
    absl::string_view collation_name, CollatorLegacyUnicodeMode mode) {
  return CollatorRegistration::GetInstance()->CreateFromCollationName(
      collation_name, mode);
}

namespace internal {
void RegisterDefaultCollatorImpl() {
  CollatorRegistration::GetInstance()->SetCreateFromCollationNameFn(
      &CollatorRegistration::DefaultCreateFromCollationNameFn);
}

void RegisterIcuCollatorImpl(
    std::function<absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>(
        absl::string_view, CollatorLegacyUnicodeMode)>
        create_fn) {
  CollatorRegistration::GetInstance()->SetCreateFromCollationNameFn(create_fn);
}
}  // namespace internal

}  // namespace googlesql
