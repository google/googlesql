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

//
// Copyright 2002 Google Inc.
// All Rights Reserved.
//
// Authors: Jing Yee Lim, Daniel Dulitz

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/node_hash_set.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/arena.h"
#include "googlesql/base/arena_allocator.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {
namespace {

const int numobjects = 500000;
const int iterations = 2;

}  // namespace

//------------------------------------------------------------------------
// Write random data to allocated memory
static void TestMemory(void* mem, int size) {
  // Do some memory allocation to check that the arena doesn't mess up
  // the internal memory allocator
  char* tmp[100];
  for (int i = 0; i < ABSL_ARRAYSIZE(tmp); i++) {
    tmp[i] = new char[i * i + 1];
  }

  memset(mem, 0xcc, size);

  // Free up the allocated memory;
  for (char* s : tmp) {
    delete[] s;
  }
}

//------------------------------------------------------------------------
// Check memory ptr
static void CheckMemory(void* mem, int size) {
  ABSL_CHECK(mem != nullptr);
  TestMemory(mem, size);
}

//------------------------------------------------------------------------
// Check memory ptr and alignment
static void CheckAlignment(void* mem, int size, int alignment) {
  ABSL_CHECK(mem != nullptr);
  ASSERT_EQ(0, (reinterpret_cast<uintptr_t>(mem) & (alignment - 1)))
    << "mem=" << mem << " alignment=" << alignment;
  TestMemory(mem, size);
}

//------------------------------------------------------------------------
template<class A>
void TestArena(const char* name, A* a, int blksize) {
  ABSL_LOG(INFO) << "Testing arena '" << name << "': blksize = " << blksize
            << ": actual blksize = " << a->block_size();

  int s;
  blksize = a->block_size();

  // Allocate zero bytes
  ABSL_CHECK(a->is_empty());
  a->Alloc(0);
  ABSL_CHECK(a->is_empty());

  // Allocate same as blksize
  CheckMemory(a->Alloc(blksize), blksize);
  ABSL_CHECK(!a->is_empty());

  // Allocate some chunks adding up to blksize
  s = blksize / 4;
  CheckMemory(a->Alloc(s), s);
  CheckMemory(a->Alloc(s), s);
  CheckMemory(a->Alloc(s), s);

  int s2 = blksize - (s * 3);
  CheckMemory(a->Alloc(s2), s2);

  // Allocate large chunk
  CheckMemory(a->Alloc(blksize * 2), blksize * 2);
  CheckMemory(a->Alloc(blksize * 2 + 1), blksize * 2 + 1);
  CheckMemory(a->Alloc(blksize * 2 + 2), blksize * 2 + 2);
  CheckMemory(a->Alloc(blksize * 2 + 3), blksize * 2 + 3);

  // Allocate aligned
  s = blksize / 2;
  CheckAlignment(a->AllocAligned(s, 1), s, 1);
  CheckAlignment(a->AllocAligned(s + 1, 2), s + 1, 2);
  CheckAlignment(a->AllocAligned(s + 2, 2), s + 2, 2);
  CheckAlignment(a->AllocAligned(s + 3, 4), s + 3, 4);
  CheckAlignment(a->AllocAligned(s + 4, 4), s + 4, 4);
  CheckAlignment(a->AllocAligned(s + 5, 4), s + 5, 4);
  CheckAlignment(a->AllocAligned(s + 6, 4), s + 6, 4);

  // Free
  for (int i = 0; i < 100; i++) {
    int i2 = i * i;
    a->Free(a->Alloc(i2), i2);
  }

  // Memdup
  char mem[500];
  for (int i = 0; i < 500; i++)
    mem[i] = i & 255;
  char* mem2 = a->Memdup(mem, sizeof(mem));
  ABSL_CHECK_EQ(0, memcmp(mem, mem2, sizeof(mem)));

  // MemdupPlusNUL
  const char* msg_mpn = "won't use all this length";
  char* msg2_mpn = a->MemdupPlusNUL(msg_mpn, 10);
  ABSL_CHECK_EQ(0, strcmp(msg2_mpn, "won't use "));
  a->Free(msg2_mpn, 11);

  // Strdup
  const char* msg = "arena unit test is cool...";
  char* msg2 = a->Strdup(msg);
  ABSL_CHECK_EQ(0, strcmp(msg, msg2));
  a->Free(msg2, strlen(msg) + 1);

  // Strndup
  char* msg3 = a->Strndup(msg, 10);
  ABSL_CHECK_EQ(0, strncmp(msg3, msg, 10));
  a->Free(msg3, 10);
  ABSL_CHECK(!a->is_empty());

  // Reset
  a->Reset();
  ABSL_CHECK(a->is_empty());

  // Realloc
  char* m1 = a->Alloc(blksize/2);
  CheckMemory(m1, blksize / 2);
  ABSL_CHECK(!a->is_empty());
  CheckMemory(a->Alloc(blksize / 2), blksize / 2);  // Allocate another block
  m1 = a->Realloc(m1, blksize/2, blksize);
  CheckMemory(m1, blksize);
  m1 = a->Realloc(m1, blksize, 23456);
  CheckMemory(m1, 23456);

  // Shrink
  m1 = a->Shrink(m1, 200);
  CheckMemory(m1, 200);
  m1 = a->Shrink(m1, 100);
  CheckMemory(m1, 100);
  m1 = a->Shrink(m1, 1);
  CheckMemory(m1, 1);
  a->Free(m1, 1);
  ABSL_CHECK(!a->is_empty());

  // Calloc
  char* m2 = a->Calloc(2000);
  for (int i = 0; i < 2000; ++i) {
    ABSL_CHECK_EQ(0, m2[i]);
  }

  // bytes_until_next_allocation
  a->Reset();
  ABSL_CHECK(a->is_empty());
  int alignment = blksize - a->bytes_until_next_allocation();
  ABSL_LOG(INFO) << "Alignment overhead in initial block = " << alignment;

  s = a->bytes_until_next_allocation() - 1;
  CheckMemory(a->Alloc(s), s);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), 1);
  CheckMemory(a->Alloc(1), 1);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), 0);

  CheckMemory(a->Alloc(2*blksize), 2*blksize);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), 0);

  CheckMemory(a->Alloc(1), 1);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), blksize - 1);

  s = blksize / 2;
  char* m0 = a->Alloc(s);
  CheckMemory(m0, s);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), blksize - s - 1);
  m0 = a->Shrink(m0, 1);
  CheckMemory(m0, 1);
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), blksize - 2);

  a->Reset();
  ABSL_CHECK(a->is_empty());
  ABSL_CHECK_EQ(a->bytes_until_next_allocation(), blksize - alignment);
}

static void EnsureNoAddressInRangeIsPoisoned(void* buffer, size_t range_size) {
#ifdef ADDRESS_SANITIZER
  ABSL_CHECK_EQ(nullptr, __asan_region_is_poisoned(buffer, range_size));
#endif
}

static void DoTest(const char* label, int blksize, char* buffer) {
  {
    UnsafeArena ua(buffer, blksize);
    TestArena((std::string("UnsafeArena") + label).c_str(), &ua, blksize);
  }
  EnsureNoAddressInRangeIsPoisoned(buffer, blksize);

  {
    SafeArena sa(buffer, blksize);
    TestArena((std::string("SafeArena") + label).c_str(), &sa, blksize);
  }
  EnsureNoAddressInRangeIsPoisoned(buffer, blksize);

#ifdef GOOGLE_PROTECTABLE_UNSAFE_ARENA_SUPPORTED
  if (((blksize % kPageSize) == 0) &&
      ((reinterpret_cast<uintptr_t>(buffer) % kPageSize) == 0)) {
    {
      ProtectableUnsafeArena pua(buffer, blksize);
      TestArena((string("ProtectableUnsafeArena") + label).c_str(), &pua,
                blksize);
    }
    EnsureNoAddressInRangeIsPoisoned(buffer, blksize);
  }
#endif  // GOOGLE_PROTECTABLE_UNSAFE_ARENA_SUPPORTED
}

//------------------------------------------------------------------------
class BasicTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(AllSizes, BasicTest,
                         ::testing::Values(BaseArena::kDefaultAlignment + 1, 10,
                                           100, 1024, 12345, 123450, 131072,
                                           1234500));

TEST_P(BasicTest, DoTest) {
  const int blksize = GetParam();

  // Allocate some memory from heap first
  char* tmp[100];
  for (int i = 0; i < ABSL_ARRAYSIZE(tmp); i++) {
    tmp[i] = new char[i * i];
  }

  // Initial buffer for testing pre-allocated arenas
  char* buffer = new char[blksize + BaseArena::kDefaultAlignment];

  DoTest("",     blksize, nullptr);
  DoTest("(p0)", blksize, buffer+0);
  DoTest("(p1)", blksize, buffer+1);
  DoTest("(p2)", blksize, buffer+2);
  DoTest("(p3)", blksize, buffer+3);
  DoTest("(p4)", blksize, buffer+4);
  DoTest("(p5)", blksize, buffer+5);

  // Free up the allocated heap memory
  for (char* s : tmp) {
    delete[] s;
  }

  delete[] buffer;
}

//------------------------------------------------------------------------

// FYI, Gladiator has a virtual destructor, which adds 4 bytes to the size
class Test : public Gladiator {
 public:
  double d, d2;
  void verify(void) { ABSL_CHECK(d2 == 3.0 * d); }
  explicit Test(const double inp) {
    d = inp;
    d2 = 3.0 * inp;
  }
};

class Test2 : public Gladiator {
 public:
  int d, d2;
  explicit Test2(const int inp) { d = inp; }
};

class NonGladiator {
 public:
  virtual ~NonGladiator() { }
};

class Test3 : public NonGladiator {
 public:
  int d, d2;
  explicit Test3(const int inp) { d = inp; }
};

class Test4 {
 public:
  virtual ~Test4() { }
  int d, d2;
  explicit Test4(const int inp) { d = inp; }
};

// NOTE: these stats will only be accurate in non-debug mode (otherwise
// they'll all be 0).  So: if you want accurate timing, run in "normal"
// or "opt" mode.  If you want accurate stats, run in "debug" mode.
void ShowStatus(const char* const header, const BaseArena::Status& status) {
  printf("\n--- status: %s\n", header);
  printf("  %zu bytes allocated\n", status.bytes_allocated());
}

// This just tests the arena code proper, without use of allocators of
// gladiators or STL or anything like that
void TestArena2(UnsafeArena* const arena) {
  const char sshort[] = "This is a short string";
  char slong[3000];
  memset(slong, 'a', sizeof(slong));
  slong[sizeof(slong)-1] = '\0';

  char* s1 = arena->Strdup(sshort);
  char* s2 = arena->Strdup(slong);
  char* s3 = arena->Strndup(sshort, 100);
  char* s4 = arena->Strndup(slong, 100);
  char* s5 = arena->Memdup(sshort, 10);
  char* s6 = arena->Realloc(s5, 10, 20);
  arena->Shrink(s5, 10);       // get s5 back to using 10 bytes again
  char* s7 = arena->Memdup(slong, 10);
  char* s8 = arena->Realloc(s7, 10, 5);
  char* s9 = arena->Strdup(s1);
  char* s10 = arena->Realloc(s4, 100, 10);
  char* s11 = arena->Realloc(s4, 10, 100);
  char* s12 = arena->Strdup(s9);
  char* s13 = arena->Realloc(s9, sizeof(sshort)-1, 100000);  // won't fit :-)

  ABSL_CHECK_EQ(0, strcmp(s1, sshort));
  ABSL_CHECK_EQ(0, strcmp(s2, slong));
  ABSL_CHECK_EQ(0, strcmp(s3, sshort));
  // s4 was realloced so it is not safe to read from
  ABSL_CHECK_EQ(0, strncmp(s5, sshort, 10));
  ABSL_CHECK_EQ(0, strncmp(s6, sshort, 10));
  ABSL_CHECK_EQ(s5, s6);      // Should have room to grow here
  // only the first 5 bytes of s7 should match; the realloc should have
  // caused the next byte to actually go to s9
  ABSL_CHECK_EQ(0, strncmp(s7, slong, 5));
  ABSL_CHECK_EQ(s7, s8);      // Realloc-smaller should cause us to realloc in place
  // s9 was realloced so it is not safe to read from
  ABSL_CHECK_EQ(s10, s4);     // Realloc-smaller should cause us to realloc in place
  // Even though we're back to prev size, we had to move the pointer.  Thus
  // only the first 10 bytes are known since we grew from 10 to 100
  ABSL_CHECK_NE(s11, s4);
  ABSL_CHECK_EQ(0, strncmp(s11, slong, 10));
  ABSL_CHECK_EQ(0, strcmp(s12, s1));
  ABSL_CHECK_NE(s12, s13);    // too big to grow-in-place, so we should move
}

//--------------------------------------------------------------------
// Test some fundamental STL containers

static std::hash<std::string> string_hash;
static std::equal_to<std::string> string_eq;
static std::hash<uint32_t> int_hash;
static std::equal_to<uint32_t> int_eq;
static std::less<std::string> string_less;
static std::less<uint32_t> int_less;

template <typename T>
struct test_hash {
  size_t operator()(const T&) const{ return 0; }
  inline bool operator()(const T& s1, const T& s2) const { return s1 < s2; }
};
template <>
struct test_hash<const char*> {
  size_t operator()(const char*) const { return 0; }

  inline bool operator()(const char* s1, const char* s2) const {
    return (s1 != s2) &&
        (s2 == nullptr || (s1 != nullptr && strcmp(s1, s2) < 0));
  }
};

static test_hash<const char*> cstring_hash;

// temp definitions from strutil.h, until the compiler error
// generated by #including that file is fixed.
struct streq {
  bool operator()(const char* s1, const char* s2) const {
    return ((s1 == nullptr && s2 == nullptr) ||
            (s1 && s2 && *s1 == *s2 && strcmp(s1, s2) == 0));
  }
};
struct strlt {
  bool operator()(const char* s1, const char* s2) const {
    return (s1 != s2) &&
        (s2 == nullptr || (s1 != nullptr && strcmp(s1, s2) < 0));
  }
};

typedef absl::node_hash_set<std::string, std::hash<std::string>,
                            std::equal_to<std::string>,
                            ArenaAllocator<std::string, UnsafeArena> >
    StringHSet;
typedef absl::node_hash_set<const char*, test_hash<const char*>, streq,
                            ArenaAllocator<const char*, UnsafeArena> >
    CStringHSet;
typedef absl::node_hash_set<uint32_t, std::hash<uint32_t>,
                            std::equal_to<uint32_t>,
                            ArenaAllocator<uint32_t, UnsafeArena> >
    IntHSet;
typedef std::set<std::string, std::less<std::string>,
                 ArenaAllocator<std::string, UnsafeArena> >
    StringSet;
typedef std::set<char*, strlt, ArenaAllocator<char*, UnsafeArena> > CStringSet;
typedef std::set<uint32_t, std::less<uint32_t>,
                 ArenaAllocator<uint32_t, UnsafeArena> >
    IntSet;
typedef std::vector<std::string, ArenaAllocator<std::string, UnsafeArena> >
    StringVec;
typedef std::vector<const char*, ArenaAllocator<const char*, UnsafeArena> >
    CStringVec;
typedef std::vector<char, ArenaAllocator<char, UnsafeArena> > CharVec;
typedef std::vector<uint32_t, ArenaAllocator<uint32_t, UnsafeArena> > IntVec;

TEST(ArenaTest, STL) {
  printf("\nBeginning STL allocation test\n");

  static const std::string test_strings[] = {
      "aback",        "abaft",        "abandon",       "abandoned",
      "abandoning",   "abandonment",  "abandons",      "abase",
      "abased",       "abasement",    "abasements",    "abases",
      "abash",        "abashed",      "abashes",       "abashing",
      "abasing",      "abate",        "abated",        "abatement",
      "abatements",   "abater",       "abates",        "abating",
      "abbe",         "abbey",        "abbeys",        "abbot",
      "abbots",       "abbreviate",   "abbreviated",   "abbreviates",
      "abbreviating", "abbreviation", "abbreviations", "abdomen",
      "abdomens",     "abdominal",    "abduct",        "abducted",
      "abduction",    "abductions",   "abductor",      "abductors",
      "abducts",      "Abe",          "abed",          "Abel",
      "Abelian",      "Abelson",      "Aberdeen",      "Abernathy",
      "aberrant",     "aberration",   "aberrations",   "abet",
      "abets",        "abetted",      "abetter",       "abetting",
      "abeyance",     "abhor",        "abhorred",      "abhorrent",
      "abhorrer",     "abhorring",    "abhors",        "abide",
      "abided",       "abides",       "abiding"};

  static const uint32_t test_ints[] = {
      53,        97,        193,       389,       769,       1543,     3079,
      6151,      12289,     24593,     49157,     98317,     196613,   393241,
      786433,    1572869,   3145739,   6291469,   12582917,  25165843, 50331653,
      100663319, 201326611, 402653189, 805306457, 1610612741};

  // Observe that although this definition is natural, used carelessly
  // it can leak memory.  How?  A string is not a atomic type and its
  // memory is allocated by the standard allocation mechanism.  This
  // continues to be true even when it is in an STL container that is
  // otherwise arena allocated.  In order to free the strings, it is
  // not sufficient to reset the arena from which the object was
  // allocated, it necessary to call delete on this object; the
  // default destructor will then delete its member objects which will
  // in turn delete the strings.  The next two examples show how this
  // object can be redefined so that simply resetting the arena frees
  // all memory.
  class MustDelete : public Gladiator {
   public:
    explicit MustDelete(UnsafeArena* const arena)
        : string_vec_(arena),
          char_vec_(arena),
          int_vec_(arena),
          string_hset_(0, string_hash, string_eq, arena),
          int_hset_(0, int_hash, int_eq, arena),
          string_set_(string_less, arena),
          int_set_(int_less, arena) {
    }

    // Note that we're getting the default destructor

    StringVec string_vec_;
    CharVec char_vec_;
    IntVec int_vec_;
    StringHSet string_hset_;
    IntHSet int_hset_;
    StringSet string_set_;
    IntSet int_set_;

    void Test() {
      int string_size = ABSL_ARRAYSIZE(test_strings);
      int char_size = 0;

      for (int i = 0; i < string_size; ++i) {
        std::string s = test_strings[i];
        string_vec_.push_back(s);
        for (int j = 0; j < s.size(); ++j) {
          char_vec_.push_back(s[j]);
          ++char_size;
        }
        string_hset_.insert(s);
        string_set_.insert(s);
      }
      int int_size = ABSL_ARRAYSIZE(test_ints);
      for (int i = 0; i < int_size; ++i) {
        uint32_t r = test_ints[i];
        int_vec_.push_back(r);
        int_hset_.insert(r);
        // Build the set backwards, so that we can later check that
        // it was put into the proper order.
        int_set_.insert(test_ints[int_size - (1 + i)]);
      }
      ABSL_CHECK_EQ(string_vec_.size(), string_size);
      ABSL_CHECK_EQ(char_vec_.size(), char_size);
      ABSL_CHECK_EQ(string_hset_.size(), string_size);
      ABSL_CHECK_EQ(int_vec_.size(), int_size);
      ABSL_CHECK_EQ(int_hset_.size(), int_size);
      uint32_t last = 0;
      for (IntSet::iterator i = int_set_.begin();
           int_set_.end() != i; ++i) {
        ABSL_CHECK_GT(*i, last);
        last = *i;
      }
    }
  };

  // This is pretty much functionally equivalent, but it
  // does explicit char* allocation in the arena.
  class WithCharStar : public ArenaOnlyGladiator {
   public:
    explicit WithCharStar(UnsafeArena* const arena)
        : arena_(arena),
          string_vec_(arena),
          char_vec_(arena),
          int_vec_(arena),
          string_hset_(0, cstring_hash, streq(), arena),
          int_hset_(0, int_hash, int_eq, arena),
          string_set_(strlt(), arena),
          int_set_(int_less, arena) {
    }

    // The destructor does nothing, no need to call it
    ~WithCharStar() {}

    UnsafeArena* arena_;
    CStringVec string_vec_;
    CharVec char_vec_;
    IntVec int_vec_;
    CStringHSet string_hset_;
    IntHSet int_hset_;
    CStringSet string_set_;
    IntSet int_set_;

    void Test() {
      int string_size = ABSL_ARRAYSIZE(test_strings);
      int char_size = 0;

      for (int i = 0; i < string_size; ++i) {
        char* s = arena_->Strdup(test_strings[i].c_str());
        string_vec_.push_back(s);
        string_hset_.insert(s);
        string_set_.insert(s);
        while (*s) {
          char_vec_.push_back(*s++);
          ++char_size;
        }
      }
      int int_size = ABSL_ARRAYSIZE(test_ints);
      for (int i = 0; i < int_size; ++i) {
        uint32_t r = test_ints[i];
        int_vec_.push_back(r);
        int_hset_.insert(r);
        // Build the set backwards, so that we can later check that
        // it was put into the proper order.
        int_set_.insert(test_ints[int_size - (1 + i)]);
      }
      ABSL_CHECK_EQ(string_vec_.size(), string_size);
      ABSL_CHECK_EQ(char_vec_.size(), char_size);
      ABSL_CHECK_EQ(string_hset_.size(), string_size);
      ABSL_CHECK_EQ(int_vec_.size(), int_size);
      ABSL_CHECK_EQ(int_hset_.size(), int_size);
      uint32_t last = 0;
      for (IntSet::iterator i = int_set_.begin();
           int_set_.end() != i; ++i) {
        ABSL_CHECK_GT(*i, last);
        last = *i;
      }
    }
  };

  UnsafeArena* arena = new UnsafeArena(10000);

  for (int i = 0; i < 100; ++i) {
    MustDelete* obj = new(0, arena) MustDelete(arena);
    obj->Test();
    delete obj;    // <-- explicit delete
    arena->Reset();
  }

  for (int i = 0; i < iterations; ++i) {
    WithCharStar* obj = new(0, arena) WithCharStar(arena);
    obj->Test();
    arena->Reset();
  }

  delete arena;
}


//-------------------------------------------------------------

void DoTest(UnsafeArena* const risky, SafeArena* const tame) {
  typedef ArenaAllocator<Test*, SafeArena> MyAlloc;
  typedef ArenaAllocator<char*, SafeArena> MyAlloc2;
  MyAlloc myalloc(tame);
  MyAlloc2 myalloc2(tame);
  std::vector<Test*, MyAlloc> tests(myalloc);
  std::vector<char*, MyAlloc2> chars(myalloc2);

  for (int n = 0; n < numobjects; ++n) {
    tests.push_back(new(0, risky) Test(5.0));
    char* const cstr = risky->Alloc(2);
    cstr[0] = 'Y';
    cstr[1] = '\0';
    chars.push_back(cstr);
  }

  ShowStatus("risky at high-water", risky->status());
  ShowStatus("tame at high-water", tame->status());

  int c = 0;
  while (!tests.empty()) {
    Test* const t = tests.back();
    t->verify();
    delete t;
    tests.pop_back();
    ++c;
  }
  ABSL_CHECK_EQ(numobjects, c);

  c = 0;
  while (!chars.empty()) {
    char* const ch = chars.back();
    ABSL_CHECK_EQ(0, strcmp("Y", ch));
    risky->Free(ch, 2);
    chars.pop_back();
    ++c;
  }
  ABSL_CHECK_EQ(numobjects, c);
}

TEST(ArenaTest, TestInl) {
  UnsafeArena risky(1000000);
  SafeArena tame(1000000);

  printf("object has sizeof %lu, plus 1 for gladiator\n\n", sizeof(Test));

  UnsafeArena small(100);
  UnsafeArena medium(500);
  UnsafeArena large(5000);
  UnsafeArena huge(50000);

  for (int n = 0; n < iterations; ++n) {
    // Test on various size arenas
    TestArena2(&small);
    small.Reset();
    TestArena2(&medium);
    medium.Reset();
    TestArena2(&large);
    large.Reset();
    TestArena2(&huge);
    huge.Reset();

    DoTest(&risky, &tame);
    ShowStatus("risky at low-water", risky.status());
    ShowStatus("tame at low-water", tame.status());
    risky.Reset();
    tame.Reset();
  }
}

// DoTestArenaAllocator, DoTestNewInArenaScalar and DoTestNewInArenaArray are
// structured in the same way to ensure that allocations:
//   1. Request as little memory as possible from the arena.
//   2. Return correctly aligned pointers.
//
// To do this, we use our own buffer for the UnsafeArena so that we can verify
// the addresses of allocated memory. We make sure our buffer is big enough that
// all test cases fit into the single contiguous block and the arena does not
// allocate its own memory.
//
// There are alignof(T) states that the arena can be in when we try to allocate
// a T. The next available address within the arena may be a multiple of
// alignof(T) plus an offset of [1, alignof(T)] bytes.
//
// We loop over these possible offsets and:
//   1. Allocate `i` bytes to create the desired offset.
//   2. Allocate a T and ensure it is allocated at the first address past the
//      offset which meets the aligment requirement of T.
//   3. Allocate a single unaligned byte and check its address to ensure the
//      arena only used sizeof(T) bytes when allocating the previous T.
//
// To make sure we know where addresses will be, we must align in a way that
// allocation can start at the beginning of our buffer. Therefore, we must
// align to the strictest of:
//   1. 2 * alignof(T). This is because we want to make sure that when the
//      arena allocates, it can allocate at a multiple of alignof(T) which is
//      not also a multiple of 2 * alignof(T). This demonstrates that the arena
//      is not using a stricter alignment than necessary.
//   2. std::max_align_t. This is because UnsafeArena will advance into a
//      passed-in buffer to an address satisfying BaseArena::kDefaultAlignment
//      (= 8). Since kDefaultAlignment is in the process of being removed, we
//      align to max_align_t, which is greater than or equal to
//      kDefaultAlignment.
template <class T>
void DoTestArenaAllocator() {
  ABSL_LOG(INFO) << "Testing ArenaAllocator<T, UnsafeArena>: "
            << "sizeof(T) = " << sizeof(T) << ", alignof(T) = " << alignof(T);
  // Create the buffer used by the UnsafeArena.
  alignas(2 * alignof(T)) alignas(std::max_align_t) char buffer[512];
  for (int i = 1; i <= alignof(T); ++i) {
    UnsafeArena arena(buffer, sizeof(buffer));
    // Allocate i bytes from arena to force ArenaAllocator to align.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(i)), static_cast<void*>(buffer));
    ArenaAllocator<T, UnsafeArena> alloc(&arena);
    // Verify that the pointer is aligned but not over-aligned.
    EXPECT_EQ(static_cast<void*>(alloc.allocate(1)),
              static_cast<void*>(buffer + alignof(T)));
    // Verify that ArenaAllocator::allocate requested sizeof(T) bytes for T by
    // allocating a single unaligned byte and checking the address.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(1)),
              static_cast<void*>(buffer + alignof(T) + sizeof(T)));
  }
}

// See DoTestArenaAllocator() above.  This differs only in that it calls
// NewInArena<T> instead of using an ArenaAllocator<T>.
template <class T>
void DoTestNewInArenaScalar() {
  ABSL_LOG(INFO) << "Testing NewInArena<T, UnsafeArena>: "
            << "sizeof(T) = " << sizeof(T) << ", alignof(T) = " << alignof(T);
  // Create the buffer used by the UnsafeArena.
  alignas(2 * alignof(T)) alignas(std::max_align_t) char buffer[512];
  for (int i = 1; i <= alignof(T); ++i) {
    UnsafeArena arena(buffer, sizeof(buffer));
    // Allocate i bytes from arena to force a gap for alignment.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(i)), static_cast<void*>(buffer));
    // Verify that the pointer is aligned but not over-aligned.
    EXPECT_EQ(static_cast<void*>(NewInArena<T>(&arena)),
              static_cast<void*>(buffer + alignof(T)));
    // Verify that NewInArena requrested sizeof(T) bytes for T by
    // allocating a single unaligned byte and checking the address.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(1)),
              static_cast<void*>(buffer + alignof(T) + sizeof(T)));
  }
}

// See DoTestArenaAllocator() above.  This differs only in that it calls
// NewInArena<T[]> instead of using an ArenaAllocator<T>.
template <class T>
void DoTestNewInArenaArray(size_t n) {
  ABSL_LOG(INFO) << "Testing NewInArena<T[], UnsafeArena>: "
            << "sizeof(T) = " << sizeof(T) << ", alignof(T) = " << alignof(T)
            << ", array size = " << n;
  // Create the buffer used by the UnsafeArena.
  alignas(2 * alignof(T)) alignas(std::max_align_t) char buffer[512];
  for (int i = 1; i <= alignof(T); ++i) {
    UnsafeArena arena(buffer, sizeof(buffer));
    // Allocate i bytes from arena to force a gap for alignment.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(i)), static_cast<void*>(buffer));
    // Verify that the pointer is aligned but not over-aligned.
    EXPECT_EQ(static_cast<void*>(NewInArena<T[]>(&arena, n)),
              static_cast<void*>(buffer + alignof(T)));
    // Verify that NewInArena requrested sizeof(T) bytes for T by
    // allocating a single unaligned byte and checking the address.
    EXPECT_EQ(static_cast<void*>(arena.Alloc(1)),
              static_cast<void*>(buffer + alignof(T) + n * sizeof(T)));
  }
}

TEST(ArenaTest, TestArenaAllocator) {
  struct alignas(2 * alignof(std::max_align_t)) OverAlignedType {};
  DoTestArenaAllocator<uint8_t>();
  DoTestArenaAllocator<uint16_t>();
  DoTestArenaAllocator<uint32_t>();
  DoTestArenaAllocator<uint64_t>();
  DoTestArenaAllocator<long double>();
  DoTestArenaAllocator<std::max_align_t>();
  DoTestArenaAllocator<OverAlignedType>();
  DoTestArenaAllocator<uint8_t[1]>();
  DoTestArenaAllocator<uint8_t[3]>();
  DoTestArenaAllocator<OverAlignedType[2]>();
  DoTestArenaAllocator<OverAlignedType[3]>();
}

TEST(ArenaTest, TestNewInArena) {
  struct alignas(2 * alignof(std::max_align_t)) OverAlignedType {};
  struct alignas(alignof(std::max_align_t)) ArenaOnlyGladiatorType
      : public ArenaOnlyGladiator {};
  DoTestNewInArenaScalar<uint8_t>();
  DoTestNewInArenaScalar<uint16_t>();
  DoTestNewInArenaScalar<uint32_t>();
  DoTestNewInArenaScalar<uint64_t>();
  DoTestNewInArenaScalar<long double>();
  DoTestNewInArenaScalar<std::max_align_t>();
  DoTestNewInArenaScalar<OverAlignedType>();
  DoTestNewInArenaScalar<ArenaOnlyGladiatorType>();
  DoTestNewInArenaArray<uint8_t>(1);
  DoTestNewInArenaArray<uint8_t>(3);
  DoTestNewInArenaArray<OverAlignedType>(2);
  DoTestNewInArenaArray<OverAlignedType>(3);
  DoTestNewInArenaArray<ArenaOnlyGladiatorType>(5);
}

TEST(ArenaTest, TestDeleteInArena) {
  struct TestType {
    TestType() : destroyed(nullptr) {}
    explicit TestType(bool* d) : destroyed(d) {}
    ~TestType() { *destroyed = true; }

    bool* destroyed;
  };

  UnsafeArena arena(256);
  bool destroyed = false;
  TestType* t = NewInArena<TestType>(&arena, &destroyed);
  EXPECT_FALSE(arena.is_empty());
  DeleteInArena(&arena, t);
  EXPECT_TRUE(arena.is_empty());
  EXPECT_TRUE(destroyed);

  const int kArraySize = 4;
  bool destroyed_array[kArraySize];
  t = NewInArena<TestType[]>(&arena, kArraySize);
  for (int i = 0; i < kArraySize; ++i) {
    destroyed_array[i] = false;
    (t + i)->destroyed = destroyed_array + i;
  }
  EXPECT_FALSE(arena.is_empty());
  DeleteInArena(&arena, t, kArraySize);
  EXPECT_TRUE(arena.is_empty());
  for (int i = 0; i < kArraySize; ++i) EXPECT_TRUE(destroyed_array[i]);
}

void DoPoisonTest(BaseArena* b, size_t size) {
#ifdef ADDRESS_SANITIZER
  ABSL_LOG(INFO) << "DoPoisonTest(" << static_cast<void*>(b) << ", " << size << ")";
  char* c1 = b->SlowAlloc(size);
  char* c2 = b->SlowAlloc(size);
  ABSL_CHECK_EQ(nullptr, __asan_region_is_poisoned(c1, size));
  ABSL_CHECK_EQ(nullptr, __asan_region_is_poisoned(c2, size));
  char* c3 = b->SlowRealloc(c2, size, size/2);
  ABSL_CHECK_EQ(nullptr, __asan_region_is_poisoned(c3, size/2));
  ABSL_CHECK_NE(nullptr, __asan_region_is_poisoned(c2, size));
  b->Reset();
  ABSL_CHECK_NE(nullptr, __asan_region_is_poisoned(c1, size));
  ABSL_CHECK_NE(nullptr, __asan_region_is_poisoned(c2, size));
  ABSL_CHECK_NE(nullptr, __asan_region_is_poisoned(c3, size/2));
#endif
}

TEST(ArenaTest, TestPoison) {
  {
    UnsafeArena arena(512);
    DoPoisonTest(&arena, 128);
    DoPoisonTest(&arena, 256);
    DoPoisonTest(&arena, 512);
    DoPoisonTest(&arena, 1024);
  }

  {
    SafeArena arena(512);
    DoPoisonTest(&arena, 128);
    DoPoisonTest(&arena, 256);
    DoPoisonTest(&arena, 512);
    DoPoisonTest(&arena, 1024);
  }

  char* buffer = new char[512];
  {
    UnsafeArena arena(buffer, 512);
    DoPoisonTest(&arena, 128);
    DoPoisonTest(&arena, 256);
    DoPoisonTest(&arena, 512);
    DoPoisonTest(&arena, 1024);
  }
  EnsureNoAddressInRangeIsPoisoned(buffer, 512);

  {
    SafeArena arena(buffer, 512);
    DoPoisonTest(&arena, 128);
    DoPoisonTest(&arena, 256);
    DoPoisonTest(&arena, 512);
    DoPoisonTest(&arena, 1024);
  }
  EnsureNoAddressInRangeIsPoisoned(buffer, 512);
  delete[] buffer;
}

//------------------------------------------------------------------------

template<class A>
void TestStrndupUnterminated() {
  const char kFoo[3] = {'f', 'o', 'o'};
  char* source = new char[3];
  memcpy(source, kFoo, sizeof(kFoo));
  A arena(4096);
  char* dup = arena.Strndup(source, sizeof(kFoo));
  ABSL_CHECK_EQ(0, memcmp(dup, kFoo, sizeof(kFoo)));
  delete[] source;
}

TEST(ArenaTest, StrndupWithUnterminatedStringUnsafe) {
  TestStrndupUnterminated<UnsafeArena>();
}

TEST(ArenaTest, StrndupWithUnterminatedStringSafe) {
  TestStrndupUnterminated<SafeArena>();
}

}  // namespace googlesql_base
