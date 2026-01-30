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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_H_

//
// Sometimes it is necessary to allocate a large number of small
// objects.  Doing this the usual way (malloc, new) is slow,
// especially for multithreaded programs.  A BaseArena provides a
// mark/release method of memory management: it asks for a large chunk
// from the operating system and doles it out bit by bit as required.
// Then you free all the memory at once by calling BaseArena::Reset().
//
// Use SafeArena for multi-threaded programs where multiple threads
// could access the same arena at once.  Use UnsafeArena otherwise.
// Usually you'll want UnsafeArena.
//
// There are four ways to use the arena.  arena.h and arena.cc are
// sufficient for the MALLOC and STRINGS uses.  For a NEW equivalent and STL
// you'll also need to include arena_allocator.h in the appropriate .cc file.
// However, we do *declare* (but not define) the template types here.
//
// LIKE MALLOC: --Uses UnsafeArena (or SafeArena)--
//    This is the simplest way.  Just create an arena, and whenever you
//    need a block of memory to put something in, call BaseArena::Alloc().  eg
//        s = arena.Alloc(100);
//        snprintf(s, 100, "%s:%d", host, port);
//        arena.Shrink(strlen(s)+1);     // optional; see below for use
//
//    You'll probably use the convenience routines more often:
//        s = arena.Strdup(host);        // a copy of host lives in the arena
//        s = arena.Strndup(host, 100);  // we guarantee to NUL-terminate!
//        s = arena.Memdup(protobuf, sizeof(protobuf);
//
//    If you go the Alloc() route, you'll probably allocate too-much-space.
//    You can reclaim the extra space by calling Shrink() before the next
//    Alloc() (or Strdup(), or whatever), with the #bytes you actually used.
//       If you use this method, memory management is easy: just call Alloc()
//    and friends a lot, and call Reset() when you're done with the data.
//
// FOR STRINGS: --Uses UnsafeArena (or SafeArena)--
//    This is a special case of STL (below), but is simpler.  Use an
//    astring, which acts like a string but allocates from the passed-in
//    arena:
//       astring s(arena);             // or "sastring" to use a SafeArena
//       s.assign(host);
//       astring s2(host, hostlen, arena);
//
// WITH googlesql_base::NewInArena<T>: Use this to allocate a C++ object of type T, which
//    must inherit from ArenaOnlyGladiator. Uses ArenaOnlyGladiator and any
//    arena type having AllocAlligned() and Free() methods that are API
//    compatible with those of UnsafeArena.
//
//    To allocate an object of this class in the arena, use
//
//        UnsafeArena arena(1024);
//        T* obj = googlesql_base::NewInArena<T>(&arena, constructor_args...);
//
//    The file also provides googlesql_base::DeleteInArena<T>, which must be called before
//    releasing the arena.
//
//      googlesql_base::DeleteInArena(&arena, obj);
//
//    Note that any memory allocated from inside T will still come from the heap
//    unless it is also allocated using googlesql_base::NewInArena() or the class uses an
//    ArenaAllocator.
//
// WITH NEW: --Uses BaseArena, Gladiator --
//    Use this to allocate a C++ class object (or any other object you
//    have to get via new/delete rather than malloc/free).
//    There are several things you have to do in this case:
//       1) Your class (the one you new) must inherit from Gladiator.
//       2) To actually allocate this class on the arena, use
//             myclass = new (AllocateInArena, arena) MyClass(constructor, args)
//
//    Note that MyClass doesn't need to have the arena passed in.
//    But if it, in turn, wants to call "new" on some of its member
//    variables, and you want those member vars to be on the arena
//    too, you better pass in an arena so it can call new(0,arena).
//
//    If you allocate myclass using new(0,arena), and MyClass only
//    does memory management in the destructor, it's not necessary
//    to even call "delete myclass;", you can just call arena.Reset();
//    If the destructor does something else (closes a file, logs
//    a message, whatever), you'll have to call destructor and Reset()
//    both: "delete myclass; arena.Reset();"
//
//    Note that you can not allocate an array of classes this way:
//         noway = new (AllocateInArena, arena) MyClass[5];   // not supported!
//    It's not difficult to program, we just haven't done it.  Arrays
//    are typically big and so there's little point to arena-izing them.
//
// WITH googlesql_base::NewInArena: uses any arena type defining AllocAligned() and Free()
//    There are cases where you can't inherit the class from Gladiator,
//    or inheriting would be too expensive.  Examples of this include
//    plain-old-data and third-party classes (such as STL containers).
//    arena_allocator.h provides googlesql_base::NewInArena and that can be used as
//    follows:
//
//      #include "googlesql/base/arena_allocator.h"
//
//      UnsafeArena arena(1024);
//      Foo* foo = googlesql_base::NewInArena<Foo>(&arena);
//      Foo* foo_array = googlesql_base::NewInArena<Foo[]>(&arena, 10);
//
//    The file also provides googlesql_base::DeleteInArena, which is necessary to call if
//    Foo has a nontrivial destructor. Most code will only make use of
//    NewInArena as Foo will be trivially destructible and ~Arena() or
//    Arena::Reset() will be used to clean up memory.
//
//      googlesql_base::DeleteInArena(&arena, foo);
//      googlesql_base::DeleteInArena(&arena, foo_array, 10);
//
// WITH NEW: --Uses UnsafeArena-- (DEPRECATED: use googlesql_base::NewInArena as above)
//    There are cases where you can't inherit the class from Gladiator,
//    or inheriting would be too expensive.  Examples of this include
//    plain-old-data (allocated using new) and third-party classes (such
//    as STL containers).  arena_allocator.h provides a global operator new
//    that can be used as follows:
//
//    #include "googlesql/base/arena_allocator.h"
//
//      UnsafeArena arena(1024);
//      Foo* foo = new (AllocateInArena, &arena) Foo;
//      Foo* foo_array = new (AllocateInArena, &arena) Foo[10];
//
// IN STL: --Uses BaseArena, ArenaAllocator--
//    All STL containers (vector, unordered_map, etc) take an allocator.
//    You can use the arena as an allocator.  Then whenever the vector
//    (or whatever) wants to allocate memory, it will take it from the
//    arena.  To use, you just indicate in the type that you want to use the
//    arena, and then actually give a pointer to the arena as the last
//    constructor arg:
//       std::vector<int, ArenaAllocator<int, UnsafeArena> > v(&arena);
//       v.push_back(3);
//
// WARNING: Careless use of STL within an arena-allocated object can
//    result in memory leaks if you rely on arena.Reset() to free
//    memory and do not call the object destructor.  This is actually
//    a subclass of a more general hazard: If an arena-allocated
//    object creates (and owns) objects that are not also
//    arena-allocated, then the creating object must have a
//    destructor that deletes them, or they will not be deleted.
//    However, since the outer object is arena allocated, it's easy to
//    forget to call delete on it, and needing to do so may seem to
//    negate much of the benefit of arena allocation.  A specific
//    example is use of std::vector<string> in an arena-allocated object,
//    since type string is not atomic and is always allocated by the
//    default runtime allocator.  The arena definition provided here
//    allows for much flexibility, but you ought to carefully consider
//    before defining arena-allocated objects which in turn create
//    non-arena allocated objects.
//
//
// PUTTING IT ALL TOGETHER
//    Here's a program that uses all of the above.  Note almost all the
//    examples are the various ways to use "new" and STL.  Using the
//    malloc-like features and the string type are much easier!
//
// Class A : public Gladiator {
//  public:
//   int i;
//   std::vector<int> v1;
//   std::vector<int, ArenaAllocator<int, UnsafeArena> >* v3;
//   std::vector<int, ArenaAllocator<int, UnsafeArena> >* v4;
//   std::vector<int>* v5;
//   std::vector<string> vs;
//   std::vector<astring> va;
//   char *s;
//   A() : v1(), v3(nullptr), v4(nullptr), vs(), va(), s(nullptr) {
//     // v1 is allocated on the arena whenever A is.  Its ints never are.
//     v5 = new std::vector<int>;
//     // v5 is not allocated on the arena, and neither are any of its ints.
//   }
//   ~A() {
//     delete v5;          // needed since v5 wasn't allocated on the arena
//     printf("I'm done!\n");
//   }
// };
//
// class B : public A {    // we inherit from Gladiator, but indirectly
//  public:
//   UnsafeArena* arena_;
//   std::vector<int, ArenaAllocator<int, UnsafeArena> > v2;
//   std::vector<A> va1;
//   std::vector<A, ArenaAllocator<A, UnsafeArena> > va2;
//   std::vector<A>* pva;
//   std::vector<A, ArenaAllocator<A, UnsafeArena> >* pva2;
//   astring a;
//
//   B(UnsafeArena * arena)
//     : arena_(arena), v2(arena_), va1(), va2(arena_), a("initval", arena_) {
//     v3 = new std::vector<int, ArenaAllocator<int, UnsafeArena> >(arena_);
//     v4 = googlesql_base::NewInArena<
//         std::vector<int, ArenaAllocator<int, UnsafeArena>>>(arena_, arena_);
//     v5 = googlesql_base::NewInArena<std::vector<int>>(arena_);
//     // v2 is allocated on the arena whenever B is.  Its ints always are.
//     // v3 is not allocated on the arena, but the ints you give it are
//     // v4 is allocated on the arena, and so are the ints you give it
//     // v5 is allocated on the arena, but the ints you give it are not
//     // va1 is allocated on the arena whenever B is.  No A ever is.
//     // va2 is allocated on the arena whenever B is.  Its A's always are.
//     pva = googlesql_base::NewInArena<std::vector<A>>(arena_);
//     pva2 = googlesql_base::NewInArena<
//         std::vector<A, ArenaAllocator<A, UnsafeArena>>>(arena_, arena_);
//     // pva is allocated on the arena, but its A's are not
//     // pva2 is allocated on the arena, and so are its A's.
//     // a's value "initval" is stored on the arena.  If we reassign a,
//     // the new value will be stored on the arena too.
//   }
//   ~B() {
//      delete v3;   // necessary to free v3's memory, though not its ints'
//      // googlesql_base::DeleteInArena(arena_, v4) -- not necessary arena_.Reset() will
//      // do as good
//      googlesql_base::DeleteInArena(arena_, v5);  // necessary to free v5's ints memory
//      googlesql_base::DeleteInArena(arena_, pva); // necessary to make sure you reclaim
//                                        // space used by A's
//      googlesql_base::DeleteInArena(arena_, pva2);  // safe to call this; needed if you
//                                          // want to see the printfs
//   }
// };
//
// main() {
//   UnsafeArena arena(1024);
//   A a1;                               // a1 is not on the arena
//   a1.vs.push_back(string("hello"));   // hello is not copied onto the arena
//   a1.va.push_back(astring("hello", &arena));      // hello is on the arena,
//                                                   // astring container isn't
//   a1.s = arena.Strdup("hello");       // hello is on the arena
//
//   A* a2 = new (AllocateInArena, arena) A;         // a2 is on the arena
//   a2.vs.push_back(string("hello"));   // hello is *still* not on the arena
//   a2.s = arena.Strdup("world");       // world is on the arena.  a1.s is ok
//
//   B b1(&arena);                       // B is not allocated on the arena
//   b1.a.assign("hello");               // hello is on the arena
//   b1.pva2.push_back(a1);              // our copy of a1 will be stored on
//                                       // the arena, though a1 itself wasn't
//   arena.Reset();                      // all done with our memory!
// }

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <string>  // so we can define astring
#include <vector>
#ifdef ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {

// This class is "thread-compatible": different threads can access the
// arena at the same time without locking, as long as they use only
// const methods.
class BaseArena {
 protected:         // You can't make an arena directly; only a subclass of one
  BaseArena(char* first_block, const size_t block_size, bool align_to_page);

 public:
  virtual ~BaseArena();

  virtual void Reset();

  // they're "slow" only 'cause they're virtual (subclasses define "fast" ones)
  virtual char* SlowAlloc(size_t size) = 0;
  virtual void  SlowFree(void* memory, size_t size) = 0;
  virtual char* SlowRealloc(char* memory, size_t old_size, size_t new_size) = 0;

  class Status {
   private:
    friend class BaseArena;
    size_t bytes_allocated_;
   public:
    Status() : bytes_allocated_(0) { }
    size_t bytes_allocated() const {
      return bytes_allocated_;
    }
  };

  // Accessors and stats counters
  // This accessor isn't so useful here, but is included so we can be
  // type-compatible with ArenaAllocator (in arena_allocator.h).  That is,
  // we define arena() because ArenaAllocator does, and that way you
  // can template on either of these and know it's safe to call arena().
  virtual BaseArena* arena()  { return this; }
  size_t block_size() const   { return block_size_; }
  int block_count() const;
  bool is_empty() const {
    // must check block count in case we allocated a block larger than blksize
    return freestart_ == freestart_when_empty_ && 1 == block_count();
  }

  // The alignment that ArenaAllocator uses except for 1-byte objects.
  static constexpr int kDefaultAlignment = 8;

 protected:
  bool SatisfyAlignment(const size_t alignment);
  void MakeNewBlock(const uint32_t alignment);
  void* GetMemoryFallback(const size_t size, const int align);
  void* GetMemory(const size_t size, const int align) {
    assert(remaining_ <= block_size_);          // an invariant
    if ( size > 0 && size <= remaining_ && align == 1 ) {       // common case
      last_alloc_ = freestart_;
      freestart_ += size;
      remaining_ -= size;
#ifdef ADDRESS_SANITIZER
      ASAN_UNPOISON_MEMORY_REGION(last_alloc_, size);
#endif
      return reinterpret_cast<void*>(last_alloc_);
    }
    return GetMemoryFallback(size, align);
  }

  // This doesn't actually free any memory except for the last piece allocated
  void ReturnMemory(void* memory, const size_t size) {
    if (memory == last_alloc_ &&
        size == static_cast<size_t>(freestart_ - last_alloc_)) {
      remaining_ += size;
      freestart_ = last_alloc_;
    }
#ifdef ADDRESS_SANITIZER
    ASAN_POISON_MEMORY_REGION(memory, size);
#endif
  }

  // This is used by Realloc() -- usually we Realloc just by copying to a
  // bigger space, but for the last alloc we can realloc by growing the region.
  bool AdjustLastAlloc(void* last_alloc, const size_t newsize);

  Status status_;
  size_t remaining_;

 private:
  struct AllocatedBlock {
    char* mem;
    size_t size;
  };

  // Allocate new new block of at least block_size, with the specified
  // alignment.
  // The returned AllocatedBlock* is valid until the next call to AllocNewBlock
  // or Reset (i.e. anything that might affect overflow_blocks_).
  AllocatedBlock* AllocNewBlock(const size_t block_size,
                                const uint32_t alignment);

  const AllocatedBlock* IndexToBlock(int index) const;

  const size_t block_size_;
  char* freestart_;         // beginning of the free space in most recent block
  char* freestart_when_empty_;  // beginning of the free space when we're empty
  char* last_alloc_;         // used to make sure ReturnBytes() is safe
  // if the first_blocks_ aren't enough, expand into overflow_blocks_.
  std::vector<AllocatedBlock>* overflow_blocks_;
  // STL vector isn't as efficient as it could be, so we use an array at first
  const bool first_block_externally_owned_;   // true if they pass in 1st block
  const bool page_aligned_;  // when true, all blocks need to be page aligned
  int8_t blocks_alloced_;    // how many of the first_blocks_ have been alloced
  AllocatedBlock first_blocks_[16];   // the length of this array is arbitrary

  void FreeBlocks();         // Frees all except first block

  BaseArena(const BaseArena&) = delete;
  BaseArena& operator=(const BaseArena&) = delete;
};

class UnsafeArena : public BaseArena {
 public:
  // Allocates a thread-compatible arena with the specified block size.
  explicit UnsafeArena(const size_t block_size)
    : BaseArena(nullptr, block_size, false) { }
  UnsafeArena(const size_t block_size, bool align)
    : BaseArena(nullptr, block_size, align) { }

  // Allocates a thread-compatible arena with the specified block
  // size. "first_block" must have size "block_size". Memory is
  // allocated from "first_block" until it is exhausted; after that
  // memory is allocated by allocating new blocks from the heap.
  UnsafeArena(char* first_block, const size_t block_size)
    : BaseArena(first_block, block_size, false) { }
  UnsafeArena(char* first_block, const size_t block_size, bool align)
    : BaseArena(first_block, block_size, align) { }

  char* Alloc(const size_t size) {
    return reinterpret_cast<char*>(GetMemory(size, 1));
  }
  void* AllocAligned(const size_t size, const int align) {
    return GetMemory(size, align);
  }
  char* Calloc(const size_t size) {
    void* return_value = Alloc(size);
    memset(return_value, 0, size);
    return reinterpret_cast<char*>(return_value);
  }
  void* CallocAligned(const size_t size, const int align) {
    void* return_value = AllocAligned(size, align);
    memset(return_value, 0, size);
    return return_value;
  }
  // Free does nothing except for the last piece allocated.
  void Free(void* memory, size_t size) {
    ReturnMemory(memory, size);
  }
  char* SlowAlloc(size_t size) override {  // "slow" 'cause it's virtual
    return Alloc(size);
  }
  void SlowFree(void* memory,
                size_t size) override {  // "slow" 'cause it's virt
    Free(memory, size);
  }
  char* SlowRealloc(char* memory, size_t old_size, size_t new_size) override {
    return Realloc(memory, old_size, new_size);
  }

  char* Memdup(const char* s, size_t bytes) {
    char* newstr = Alloc(bytes);
    memcpy(newstr, s, bytes);
    return newstr;
  }
  char* MemdupPlusNUL(const char* s, size_t bytes) {  // like "string(s, len)"
    char* newstr = Alloc(bytes+1);
    memcpy(newstr, s, bytes);
    newstr[bytes] = '\0';
    return newstr;
  }
  char* Strdup(const char* s) {
    return Memdup(s, strlen(s) + 1);
  }
  // Unlike libc's strncpy, I always NUL-terminate.  libc's semantics are dumb.
  // This will allocate at most n+1 bytes (+1 is for the nul terminator).
  char* Strndup(const char* s, size_t n) {
    // Use memchr so we don't walk past n.
    // We can't use the one in //strings since this is the base library,
    // so we have to reinterpret_cast from the libc void*.
    const char* eos = reinterpret_cast<const char*>(memchr(s, '\0', n));
    // if no null terminator found, use full n
    const size_t bytes = (eos == nullptr) ? n : eos - s;
    return MemdupPlusNUL(s, bytes);
  }

  // You can realloc a previously-allocated string either bigger or smaller.
  // We can be more efficient if you realloc a string right after you allocate
  // it (eg allocate way-too-much space, fill it, realloc to just-big-enough)
  char* Realloc(char* original, size_t oldsize, size_t newsize);
  // If you know the new size is smaller (or equal), you don't need to know
  // oldsize.  We don't check that newsize is smaller, so you'd better be sure!
  char* Shrink(char* s, size_t newsize) {
    AdjustLastAlloc(s, newsize);       // reclaim space if we can
    return s;                          // never need to move if we go smaller
  }

  // We make a copy so you can keep track of status at a given point in time
  Status status() const { return status_; }

  // Number of bytes remaining before the arena has to allocate another block.
  size_t bytes_until_next_allocation() const { return remaining_; }

 private:
  UnsafeArena(const UnsafeArena&) = delete;
  UnsafeArena& operator=(const UnsafeArena&) = delete;

  virtual void UnusedKeyMethod();  // Dummy key method to avoid weak vtable.
};

// We inherit from BaseArena instead of UnsafeArena so that we don't need
// virtual methods for allocation/deallocation.  This means, however,
// I have to copy the definitions of strdup, strndup, etc. :-(

class SafeArena : public BaseArena {
 public:
  // Allocates a thread-safe arena with the specified block size.
  explicit SafeArena(const size_t block_size)
    : BaseArena(nullptr, block_size, false) { }

  // Allocates a thread-safe arena with the specified block size.
  // "first_block" must have size "block_size".  Memory is allocated
  // from "first_block" until it is exhausted; after that memory is
  // allocated by allocating new blocks from the heap.
  SafeArena(char* first_block, const size_t block_size)
    : BaseArena(first_block, block_size, false) { }

  void Reset() ABSL_LOCKS_EXCLUDED(mutex_) override {
    absl::MutexLock lock(&mutex_);  // in case two threads Reset() at same time
    BaseArena::Reset();
  }

  char* Alloc(const size_t size) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    return reinterpret_cast<char*>(GetMemory(size, 1));
  }
  void* AllocAligned(const size_t size, const int align)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    return GetMemory(size, align);
  }
  char* Calloc(const size_t size) {
    void* return_value = Alloc(size);
    memset(return_value, 0, size);
    return reinterpret_cast<char*>(return_value);
  }
  void* CallocAligned(const size_t size, const int align) {
    void* return_value = AllocAligned(size, align);
    memset(return_value, 0, size);
    return return_value;
  }
  // Free does nothing except for the last piece allocated.
  void Free(void* memory, size_t size) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    ReturnMemory(memory, size);
  }
  char* SlowAlloc(size_t size) override {  // "slow" 'cause it's virtual
    return Alloc(size);
  }
  void SlowFree(void* memory,
                size_t size) override {  // "slow" 'cause it's virt
    Free(memory, size);
  }
  char* SlowRealloc(char* memory, size_t old_size, size_t new_size) override {
    return Realloc(memory, old_size, new_size);
  }
  char* Memdup(const char* s, size_t bytes) {
    char* newstr = Alloc(bytes);
    memcpy(newstr, s, bytes);
    return newstr;
  }
  char* MemdupPlusNUL(const char* s, size_t bytes) {  // like "string(s, len)"
    char* newstr = Alloc(bytes+1);
    memcpy(newstr, s, bytes);
    newstr[bytes] = '\0';
    return newstr;
  }
  char* Strdup(const char* s) {
    return Memdup(s, strlen(s) + 1);
  }
  // Unlike libc's strncpy, I always NUL-terminate.  libc's semantics are dumb.
  // This will allocate at most n+1 bytes (+1 is for the nul terminator).
  char* Strndup(const char* s, size_t n) {
    // Use memchr so we don't walk past n.
    // We can't use the one in //strings since this is the base library,
    // so we have to reinterpret_cast from the libc void*.
    const char* eos = reinterpret_cast<const char*>(memchr(s, '\0', n));
    // if no null terminator found, use full n
    const size_t bytes = (eos == nullptr) ? n : eos - s;
    return MemdupPlusNUL(s, bytes);
  }

  // You can realloc a previously-allocated string either bigger or smaller.
  // We can be more efficient if you realloc a string right after you allocate
  // it (eg allocate way-too-much space, fill it, realloc to just-big-enough)
  char* Realloc(char* original, size_t oldsize, size_t newsize)
      ABSL_LOCKS_EXCLUDED(mutex_);
  // If you know the new size is smaller (or equal), you don't need to know
  // oldsize.  We don't check that newsize is smaller, so you'd better be sure!
  char* Shrink(char* s, size_t newsize) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    AdjustLastAlloc(s, newsize);   // reclaim space if we can
    return s;                      // we never need to move if we go smaller
  }

  Status status() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    return status_;
  }

  // Number of bytes remaining before the arena has to allocate another block.
  size_t bytes_until_next_allocation() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    return remaining_;
  }

 protected:
  absl::Mutex mutex_;

 private:
  SafeArena(const SafeArena&) = delete;
  SafeArena& operator=(const SafeArena&) = delete;

  virtual void UnusedKeyMethod();  // Dummy key method to avoid weak vtable.
};

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_H_
