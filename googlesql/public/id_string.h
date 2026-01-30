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

#ifndef GOOGLESQL_PUBLIC_ID_STRING_H_
#define GOOGLESQL_PUBLIC_ID_STRING_H_

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <new>
#include <ostream>
#include <set>
#include <string>

#include "googlesql/base/arena.h"
#include "googlesql/common/unicode_utils.h"
#include "gtest/gtest_prod.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "googlesql/base/endian.h"

namespace googlesql {

class IdStringPool;

// An IdString is an immutable string that supports cheap copying and
// assignment.  It is intended primarily to store identifiers. Like
// absl::string_view, it should preferably be passed by value.
//
// This class is meant to replace the standard string class, so its interface
// is a subset of the basic_string interface.
//
// IdStrings are allocated in an IdStringPool, using
//   IdStringPool id_string_pool;
//   IdString str = id_string_pool.Make("value");
//
// IMPORTANT: The IdString contents (**and any copies of it**) are valid for
// the lifetime of the IdStringPool.  Think of copying an IdString like copying
// a absl::string_view - the IdString itself is a reference to a string stored
// inside an IdStringPool.
//
// There is also a global pool IdStringPool::Global that can be used to
// allocate static IdStrings (which will never be cleaned up).
// Use IdString::MakeGlobal as a shorthand.
class IdString {
 public:
  // Create an empty string.
  IdString() : IdString(*GlobalEmptyString()) {}

  // Create an IdString in the global IdStringPool.
  //
  // WARNING: Memory allocated this way will never be freed.
  // Do NOT use for any allocations that are done on a per-query basis.
  static IdString MakeGlobal(absl::string_view str);

  void clear() { *this = *GlobalEmptyString(); }

  char operator[](size_t pos) const { CheckAlive(); return value_->str[pos]; }

  bool empty() const { CheckAlive(); return value_->str.empty(); }
  size_t size() const {
    CheckAlive();
    return value_->str.size();
  }
  size_t size_lower() const {
    CheckAlive();
    return value_->str_casefolded.size();
  }
  size_t length() const {
    CheckAlive();
    return value_->str.length();
  }

  std::string substr(size_t start, size_t count) const {
    CheckAlive();
    return std::string(value_->str).substr(start, count);
  }

  size_t copy(char* s, size_t len, size_t pos = 0) const {
    CheckAlive();
    return std::string(value_->str).copy(s, len, pos);
  }

  // Convert to a string.  This requires copying the value.
  std::string ToString() const {
    CheckAlive();
    return std::string(value_->str);
  }

  // Convert to a string_view.  This is a cheap operation.
  absl::string_view ToStringView() const {
    CheckAlive();
    return value_->str;
  }

  bool Equals(IdString other) const {
    CheckAlive();
    other.CheckAlive();
    if (value_ == other.value_) return true;
    if (size() != other.size()) return false;
    const int64_t* str_words =
        reinterpret_cast<const int64_t*>(value_->str.data());
    const int64_t* other_str_words =
        reinterpret_cast<const int64_t*>(other.value_->str.data());
    return WordsEqual(str_words, other_str_words, value_->size_words);
  }

  bool LessThan(IdString other) const {
    CheckAlive();
    other.CheckAlive();
    if (value_ == other.value_) return false;
    const int64_t* str_words =
        reinterpret_cast<const int64_t*>(value_->str.data());
    const int64_t* other_str_words =
        reinterpret_cast<const int64_t*>(other.value_->str.data());
    int64_t min_size_words =
        std::min(value_->size_words, other.value_->size_words);
    for (int i = 0; i < min_size_words; ++i) {
      if (str_words[i] != other_str_words[i]) {
        // Compare partial word using big-endian integer comparison. This is
        // faster than a byte-by-byte comparison.
        return googlesql_base::ghtonll(str_words[i]) < googlesql_base::ghtonll(other_str_words[i]);
      }
    }
    return value_->str.size() < other.value_->str.size();
  }

  // Case-insensitive string equality.
  bool CaseEquals(IdString other) const {
    CheckAlive();
    other.CheckAlive();
    if (value_ == other.value_) return true;
    if (size_lower() != other.size_lower()) return false;
    const int64_t* str_words =
        reinterpret_cast<const int64_t*>(value_->str_casefolded.data());
    const int64_t* other_str_words =
        reinterpret_cast<const int64_t*>(other.value_->str_casefolded.data());
    return WordsEqual(str_words, other_str_words,
                      value_->size_words_casefolded);
    return true;
  }

  // Case-insensitive version of LessThan.
  bool CaseLessThan(IdString other) const {
    CheckAlive();
    other.CheckAlive();
    if (value_ == other.value_) return false;
    const int64_t* str_words =
        reinterpret_cast<const int64_t*>(value_->str_casefolded.data());
    const int64_t* other_str_words =
        reinterpret_cast<const int64_t*>(other.value_->str_casefolded.data());
    int64_t min_size_words = std::min(value_->size_words_casefolded,
                                      other.value_->size_words_casefolded);
    for (int i = 0; i < min_size_words; ++i) {
      if (str_words[i] != other_str_words[i]) {
        // Compare partial word using big-endian integer comparison. This is
        // faster than a byte-by-byte comparison.
        return googlesql_base::ghtonll(str_words[i]) < googlesql_base::ghtonll(other_str_words[i]);
      }
    }
    return value_->str_casefolded.size() < other.value_->str_casefolded.size();
  }

  // Make a new IdString with the lower-cased value of this.
  IdString ToLower(IdStringPool* pool) const;

  // Return a hash value for this string.
  // The hash value will be computed once and memoized.
  // Use like: hash_set<IdString, IdStringHash>.
  size_t Hash() const { CheckAlive(); return value_->Hash(); }

  // Return a hash value for this string suitable for a case-insensitive hash
  // table.  The hash value will be computed once and memoized.
  // Use like: hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>.
  size_t HashCase() const { CheckAlive(); return value_->HashCase(); }

  // Check if this IdString is still alive, i.e. its IdStringPool has not
  // been destructed. Crash if not.
  // This is a no-op in non-debug mode.
  void CheckAlive() const;

 private:
  struct Shared {
    // <sp_lower> must be the lowercased version of <sp>. <size_words> must be
    // the size of 'sp' and 'sp_lower', rounded up to a multiple of 8 bytes.
    Shared(const absl::string_view sp, const absl::string_view sp_lower,
           int64_t size_words, int64_t size_words_lower)
        : str(sp),
          str_casefolded(sp_lower),
          size_words(size_words),
          size_words_casefolded(size_words_lower) {}
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;

    // Case sensitive hash.
    size_t Hash() const {
      // This double-checked locking is threadsafe because hash_ is an
      // atomic value that can only be overwritten by an identical value.
      size_t h = hash_;
      if (h == 0) {
        h = absl::Hash<absl::string_view>()(str);
        hash_ = h;
      }
      return h;
    }

    // Case insensitive hash.
    size_t HashCase() const {
      // This double-checked locking is threadsafe because hash_ is an
      // atomic value that can only be overwritten by an identical value.
      size_t h = hash_case_;
      if (h == 0) {
        h = absl::Hash<absl::string_view>()(str_casefolded);
        hash_case_ = h;
      }
      return h;
    }

    // The contents of this IdString::Shared. Guaranteed to be 8-byte aligned.
    // The last 8-byte word is padded with zeros (not included in the
    // string_view).
    const absl::string_view str;

    // Normalized and case folded version of <str>. Guaranteed to be 8-byte
    // aligned. The last 8-byte word is padded with zeros (not included in the
    // string_view).
    const absl::string_view str_casefolded;

    // Size of 'str' in 64-bit words, rounded up.
    const int64_t size_words;

    // Size of 'str_casefolded' in 64-bit words, rounded up.
    const int64_t size_words_casefolded;

   private:
    // Hash of <str>.
    mutable std::atomic<size_t> hash_ = 0;

    // Hash of <str_casefolded>.
    mutable std::atomic<size_t> hash_case_ = 0;
  };

  FRIEND_TEST(IdString, SharedSizeAlignment);

  // Returns true if the first num_words values pointed to by 'lhs' and 'rhs'
  // are equal.
  static bool WordsEqual(const int64_t* lhs, const int64_t* rhs,
                         int64_t num_words) {
    switch (num_words) {
      case 1:
        return lhs[0] == rhs[0];
      case 2:
        return lhs[0] == rhs[0] && lhs[1] == rhs[1];
      case 3:
        return lhs[0] == rhs[0] && lhs[1] == rhs[1] && lhs[2] == rhs[2];
      default:
        for (int i = 0; i < num_words; ++i) {
          if (lhs[i] != rhs[i]) {
            return false;
          }
        }
        return true;
    }
  }

  const Shared* value_;
#ifndef NDEBUG
  // In debug mode only, we track which IdStringPool this string is allocated
  // in so we check that IdStrings are not used after the pool is destroyed.
  int64_t pool_id_;
#endif

  static const IdString* GlobalEmptyString();

#ifndef NDEBUG
  explicit IdString(const Shared* shared, int64_t pool_id)
      : value_(shared), pool_id_(pool_id) {}
#else
  explicit IdString(const Shared* shared)
      : value_(shared) {}
#endif

  friend class IdStringPool;
  // Copyable.
};

// Use this to make static global (or local) constant IdStrings.
// The strings will be allocated in the static global IdStringPool.
// Example:
//   STATIC_IDSTRING(kSomeString, "SOME_STRING");
//   IdString s = kSomeString;
#define STATIC_IDSTRING(name, value) \
  static const ::googlesql::IdString& name = \
      *new ::googlesql::IdString(::googlesql::IdString::MakeGlobal((value)));

inline std::ostream& operator<<(std::ostream& os, IdString id) {
  return os << id.ToStringView();
}

inline bool operator==(IdString a, IdString b) {
  return a.Equals(b);
}

inline bool operator!=(IdString a, IdString b) { return !(a == b); }

inline bool operator<(IdString a, IdString b) {
  return a.LessThan(b);
}

// Comparator for map or set.
struct IdStringCaseLess {
  bool operator()(IdString a, IdString b) const {
    return a.CaseLessThan(b);
  }
};

// Compare two IdStrings case insensitively.
inline bool IdStringCaseEqual(IdString a, IdString b) {
  return a.CaseEquals(b);
}

// Helper for use in absl::StrJoin to format a vector of IdStrings.
inline void IdStringFormatter(std::string* out, IdString str) {
  absl::StrAppend(out, str.ToStringView());
}

struct IdStringHash {
  size_t operator()(IdString str) const {
    return str.Hash();
  }
};
struct IdStringCaseHash {
  size_t operator()(IdString str) const {
    return str.HashCase();
  }
};
struct IdStringCaseEqualFunc {
  bool operator()(IdString a, IdString b) const {
    return IdStringCaseEqual(a, b);
  }
};

// Case-sensitive equality.
struct IdStringEqualFunc {
  bool operator()(IdString a, IdString b) const {
    return a.Equals(b);
  }
};

template <class KeyEqual>
bool IdStringVectorHasPrefix(absl::Span<const IdString> original_idstrings,
                             absl::Span<const IdString> prefix_idstrings) {
  if (prefix_idstrings.size() > original_idstrings.size()) {
    return false;
  }
  for (int idx = 0; idx < prefix_idstrings.size(); ++idx) {
    if (!KeyEqual()(original_idstrings[idx], prefix_idstrings[idx])) {
      return false;
    }
  }
  return true;
}

#ifndef SWIG

// Typedef for IdStringSetCase, a case insensitive ordered set of IdString.
using IdStringSetCase = std::set<IdString, IdStringCaseLess>;

// Typedef for IdStringHashSetCase, a case insensitive hash set of IdString.
using IdStringHashSetCase =
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>;

// Typedef for IdStringHashMapCase<VALUE>, a case insensitive hash map of
// IdString -> VALUE.
template <class VALUE>
using IdStringHashMapCase =
    absl::flat_hash_map<IdString, VALUE, IdStringCaseHash,
                        IdStringCaseEqualFunc>;

#endif  // SWIG

// An IdStringPool is used to allocate IdStrings and manage their lifetimes.
// All IdString values (and any copies made of them) are valid for the
// lifetime of the IdStringPool.
//
// In debug mode, accessing an IdString after its IdStringPool has been
// deleted will be caught and will crash.
//
// IdStringPool is not thread-safe.  The returned IdStrings are thread-safe to
// read and copy.
//
// TODO Consider variants, like an interning IdStringPool.
class IdStringPool {
 public:
  // Pass 'arena' to use an existing arena.
  IdStringPool();
  explicit IdStringPool(const std::shared_ptr<googlesql_base::UnsafeArena>& arena);
#ifndef SWIG
  IdStringPool(const IdStringPool&) = delete;
  IdStringPool& operator=(const IdStringPool&) = delete;
#endif  // SWIG
  ~IdStringPool();

  // Make an IdString with contents allocated in this pool.
  IdString Make(absl::string_view str) {
#ifndef NDEBUG
    return IdString(MakeShared(str), pool_id_);
#else
    return IdString(MakeShared(str));
#endif
  }

  // Create an IdString in the global IdStringPool.
  // This function is thread safe.
  //
  // WARNING: Memory allocated this way will never be freed.
  // Do NOT use for any allocations that are done on a per-query basis.
  static IdString MakeGlobal(absl::string_view str);

 private:
  // Make an IdString::Shared for <str>, allocated in the arena.
  const IdString::Shared* MakeShared(absl::string_view str) const {
    static_assert(sizeof(IdString::Shared) % sizeof(int64_t) == 0,
                  "sizeof(IdString::Shared) must be a multiple of 8 bytes");
    if (!absl::GetFlag(FLAGS_googlesql_idstring_allow_unicode_characters) ||
        absl::c_all_of(str, absl::ascii_isascii)) {
      return MakeSharedASCII(str);
    }
    return MakeSharedUnicode(str);
  }

  const IdString::Shared* MakeSharedASCII(absl::string_view str) const {
    // The stored strings are padded to a multiple of 8 bytes so that we can
    // use word-based algorithms on them.
    const int64_t padded_size = (str.size() + 7) & ~7;
    const int64_t padded_size_words = padded_size / sizeof(int64_t);

    // Allocate everything in one arena allocation, because arena allocations
    // are still not cheap.
    char* shared_buf = static_cast<char*>(arena_->AllocAligned(
        sizeof(IdString::Shared) + padded_size * 2, sizeof(int64_t)));
    char* string_buf = shared_buf + sizeof(IdString::Shared);
    uint64_t* string_buf_words = reinterpret_cast<uint64_t*>(string_buf);

    // Copy <str> twice, once in lowercase and once in original case. The
    // lowercase version comes first, because it is used most frequently.
    if (padded_size_words > 0) {
      // For performance, set the entire last word to 0 before the memcpy
      // instead of zeroing out the trailing bytes individually afterwards.
      // Do this for both copies of the string.
      string_buf_words[padded_size_words - 1] = 0;
      string_buf_words[padded_size_words + padded_size_words - 1] = 0;
    }
    // Copy the original into the second part of the buffer first.
    memcpy(string_buf_words + padded_size_words, str.data(), str.size());

    // Create the lowercase version of the string in the first part of the
    // buffer by copying from the second part.
    for (int i = 0; i < str.size(); ++i) {
      string_buf[i] = absl::ascii_tolower(string_buf[padded_size + i]);
    }

    absl::string_view copied_lower_str(string_buf, str.size());
    absl::string_view copied_str(string_buf + padded_size, str.size());
    const IdString::Shared* shared = new (shared_buf) IdString::Shared(
        copied_str, copied_lower_str, padded_size_words, padded_size_words);
    return shared;
  }

  // TODO Add compliance tests once all code is made case
  // insensitive for unicode characters.
  const IdString::Shared* MakeSharedUnicode(absl::string_view str) const {
    // The stored strings are padded to a multiple of 8 bytes so that we can
    // use word-based algorithms on them.
    const int64_t padded_size = (str.size() + 7) & ~7;
    const int64_t padded_size_words = padded_size / sizeof(int64_t);

    const std::string casefolded_str =
        googlesql::GetNormalizedAndCasefoldedString(str);
    const int64_t casefolded_padded_size = (casefolded_str.size() + 7) & ~7;
    const int64_t casefolded_padded_size_words =
        casefolded_padded_size / sizeof(int64_t);

    // Allocate everything in one arena allocation, because arena allocations
    // are still not cheap.
    char* shared_buf = static_cast<char*>(arena_->AllocAligned(
        sizeof(IdString::Shared) + padded_size + casefolded_padded_size,
        sizeof(int64_t)));
    char* string_buf = shared_buf + sizeof(IdString::Shared);
    uint64_t* string_buf_words = reinterpret_cast<uint64_t*>(string_buf);

    // Copy <str> twice, once in lowercase and once in original case. The
    // lowercase version comes first, because it is used most frequently.
    if (padded_size_words > 0) {
      // For performance, set the entire last word to 0 before the memcpy
      // instead of zeroing out the trailing bytes individually afterwards.
      // Do this for both copies of the string.
      string_buf_words[casefolded_padded_size_words - 1] = 0;
      string_buf_words[casefolded_padded_size_words + padded_size_words - 1] =
          0;
    }
    // Copy the casefold into the first part of the buffer and copy the
    // original into the second part of the buffer.
    memcpy(string_buf_words, casefolded_str.data(), casefolded_str.size());
    memcpy(string_buf_words + casefolded_padded_size_words, str.data(),
           str.size());

    absl::string_view copied_lower_str(string_buf, casefolded_str.size());
    absl::string_view copied_str(string_buf + casefolded_padded_size,
                                 str.size());
    const IdString::Shared* shared = new (shared_buf)
        IdString::Shared(copied_str, copied_lower_str, padded_size_words,
                         casefolded_padded_size_words);
    return shared;
  }

  std::shared_ptr<googlesql_base::UnsafeArena> arena_;

#ifndef NDEBUG
  static absl::Mutex global_mutex_;

  // Set of IdStringPools that are currently alive.  This is used so we
  // can store a pool_id in each IdString in debug mode and check that
  // we never access an IdString after its IdStringPool is gone.
  static absl::flat_hash_set<int64_t>* live_pool_ids_
      ABSL_GUARDED_BY(global_mutex_) ABSL_PT_GUARDED_BY(global_mutex_);

  static int64_t max_pool_id_ ABSL_GUARDED_BY(global_mutex_);

  static int64_t AllocatePoolId();

  // Check that <pool_id> is still alive.  Crash if not.
  static void CheckPoolIdAlive(int64_t pool_id);

  // Unique identifier for this IdStringPool.  Used in live_pool_ids_.
  const int64_t pool_id_;
#endif

  friend IdString;
};

inline IdString IdStringPool::MakeGlobal(absl::string_view str) {
  static IdStringPool* global_pool = new IdStringPool;
  ABSL_CONST_INIT static absl::Mutex global_pool_mutex(absl::kConstInit);
  absl::MutexLock lock(&global_pool_mutex);
  return global_pool->Make(str);
}

inline IdString IdString::MakeGlobal(absl::string_view str) {
  return IdStringPool::MakeGlobal(str);
}

// We want to keep one global empty string constant so that we can implement
// the default constructor and clear() as assignment, without allocating
// a new Shared object.
inline const IdString* IdString::GlobalEmptyString() {
  static const IdString* empty = new IdString(IdString::MakeGlobal(""));
  return empty;
}

inline void IdString::CheckAlive() const {
#ifndef NDEBUG
  IdStringPool::CheckPoolIdAlive(pool_id_);
#endif
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ID_STRING_H_
