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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_ALLOCATOR_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_ALLOCATOR_H_

// In this file we define the arena template code.  This includes the
// ArenaAllocator, which is meant only to be used with STL, and also
// the Gladiator (which needs to know how to new and delete various
// types of objects).
//
// If you're only using the MALLOC-LIKE functionality of the arena,
// you don't need to include this file at all!  You do need to include
// it (in your own .cc file) if you want to use the STRING, STL, or
// NEW aspects of the arena.  See arena.h for details on these types.
//
// ArenaAllocator is an STL allocator, but because it relies on unequal
// instances, it may not work with all standards-conforming STL
// implementations.  But it works with SGI STL so we're happy.
//
// Here's an example of how the ArenaAllocator would be used.
// Say we have a vector of ints that we want to have use the arena
// for memory allocation.  Here's one way to do it:
//    UnsafeArena* arena = new UnsafeArena(1024); // or SafeArena
//    std::vector<int, ArenaAllocator<int, UnsafeArena> > v(arena);
//
// Note that every STL type always allows the allocator (in this case,
// the arena, which is automatically promoted to an allocator) as the last
// arg to the constructor.  So if you would normally do
//    std::vector<...> v(foo, bar),
// with the arena you can do
//    std::vector<...> v(foo, bar, arena);

#include <assert.h>
#include <stddef.h>

#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "googlesql/base/arena.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {

// T is the type we want to allocate, and C is the type of the arena.
// ArenaAllocator has the thread-safety characteristics of C.
template <class T, class C> class ArenaAllocator {
 public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  // These are required for gcc-4.7 / crosstoolv16 to match default behavior
  // of gcc-4.6, so e.g.
  //   Arena a1, a2;
  //   astring s1(&a1), s2(&a2);
  //   s1.swap(s2);  // s1 should now use a2, s2 should now use a1.
  //   s2 = s1;      // s2 should now use a2, s1 should still use a2.
  typedef std::true_type propagate_on_container_copy_assignment;
  typedef std::true_type propagate_on_container_move_assignment;
  typedef std::true_type propagate_on_container_swap;

  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef std::false_type is_always_equal;

  size_type max_size() const {
    return size_t(std::numeric_limits<size_type>::max()) / sizeof(value_type);
  }

  pointer address(reference x) const noexcept {
    return std::addressof(x);
  }

  const_pointer address(const_reference x) const noexcept {
    return std::addressof(x);
  }

  template <class U, class... Args>
  void construct(U* u, Args&&... args) {
    new(static_cast<void*>(u)) U(std::forward<Args>(args)...);
  }

  template <class U>
  void destroy(U* u) { u->~U(); }

  // This is not an explicit constructor!  So you can pass in an arena*
  // to functions needing an ArenaAllocator (like the astring constructor)
  // and everything will work ok.
  ArenaAllocator(C* arena) : arena_(arena) { }  // NOLINT

  pointer allocate(size_type n,
                   const void* /*hint*/ = nullptr) {
    assert(arena_ && "No arena to allocate from!");
    return reinterpret_cast<T*>(arena_->AllocAligned(n * sizeof(T),
                                                     kAlignment));
  }
  void deallocate(pointer p, size_type n) {
    arena_->Free(p, n * sizeof(T));
  }

  C* arena() const { return arena_; }

  template<class U> struct rebind {
    typedef ArenaAllocator<U, C> other;
  };

  template<class U> ArenaAllocator(const ArenaAllocator<U, C>& other)
    : arena_(other.arena()) { }

  template<class U> bool operator==(const ArenaAllocator<U, C>& other) const {
    return arena_ == other.arena();
  }

  template<class U> bool operator!=(const ArenaAllocator<U, C>& other) const {
    return arena_ != other.arena();
  }

 protected:
  static const int kAlignment;
  C* arena_;
};

template <class T, class C>
const int ArenaAllocator<T, C>::kAlignment = alignof(T);

// Ordinarily in C++, one allocates all instances of a class from an
// arena.  If that's what you want to do, you don't need Gladiator.
// (However you may find ArenaOnlyGladiator useful.)
//
// However, for utility classes that are used by multiple clients, the
// everything-in-one-arena model may not work.  Some clients may wish
// not to use an arena at all.  Or perhaps a composite structure
// (tree) will contain multiple objects (nodes) and some of those
// objects will be created by a factory, using an arena, while other
// objects will be created on-the-fly by an unsuspecting user who
// doesn't know anything about the arena.
//
// To support that, have the arena-allocated class inherit from
// Gladiator.  The ordinary operator new will continue to allocate
// from the heap.  To allocate from an arena, do
//     Myclass * m = new (AllocateInArena, a) Myclass (args, to, constructor);
// where a is either an arena or an allocator.  Now you can call
// delete on all the objects, whether they are allocated from an arena
// or on the heap.  Heap memory will be released, while arena memory will
// not be.
//
// If a client knows that no objects were allocated on the heap, it
// need not delete any objects (but it may if it wishes).  The only
// objects that must be deleted are those that were actually allocated
// from the heap.
//
// NOTE: an exception to the google C++ style guide rule for "No multiple
// implementation inheritance" is granted for this class: you can treat this
// class as an "Interface" class, and use it in a multiple inheritance context,
// even though it implements operator new/delete.

class Gladiator {
 public:
  Gladiator() { }
  virtual ~Gladiator() { }

  // We do not override the array allocators, so array allocation and
  // deallocation will always be from the heap.  Typically, arrays are
  // larger, and thus the costs of arena allocation are higher and the
  // benefits smaller.  Since arrays are typically allocated and deallocated
  // very differently from scalars, this may not interfere too much with
  // the arena concept.  If it does pose a problem, flesh out the
  // ArrayGladiator class below.

  void* operator new(const size_t size) {
    void* ret = ::operator new(1 + size);
    static_cast<char *>(ret)[size] = 1;     // mark as heap-allocated
    return ret;
  }
  // the ignored parameter keeps us from stepping on placement new
  template<class T> void* operator new(const size_t size, const int ignored,
                                       T* allocator) {
    if (allocator) {
      void* ret = allocator->AllocAligned(1 + size,
                                          BaseArena::kDefaultAlignment);
      static_cast<char*>(ret)[size] = 0;  // mark as arena-allocated
      return ret;
    } else {
      return operator new(size);          // this is the function above
    }
  }
  void operator delete(void* memory, const size_t size) {
    if (static_cast<char*>(memory)[size]) {
      assert (1 == static_cast<char *>(memory)[size]);
      ::operator delete(memory);
    } else {
      // We never call the allocator's Free method.  If we need to do
      // that someday, we can store a pointer to the arena instead of
      // the Boolean marker flag.
    }
  }
  template<class T> void operator delete(void* memory, const size_t size,
                                         const int ign, T* allocator) {
    // This "placement delete" can only be called if the constructor
    // throws an exception.
    if (allocator) {
      allocator->Free(memory, 1 + size);
    } else {
      ::operator delete(memory);
    }
  }

 private:
  virtual void UnusedKeyMethod();  // Dummy key method to avoid weak vtable.
};

// ArenaOnlyGladiator is a base class of types intended to be allocated only in
// arenas. It enables allocation with googlesql_base::NewInArea and prevents accidental
// use of the global operator new and delete with derived types.
//
// Brief example (see documentation for googlesql_base::NewInArena / googlesql_base::DeleteInArena
// for full details):
//
//   class MyType : public ArenaOnlyGladiator {
//     // ...
//   };
//
//   UnsafeArena arena(1024);
//   MyType* t1 = googlesql_base::NewInArena<MyType>(&arena);
//
//   MyType* t2 = new MyType();  // ERROR
//
class ArenaOnlyGladiator {
 public:
  ArenaOnlyGladiator() { }
  // No virtual destructor is needed because we ignore the size
  // parameter in all the delete functions.
  // virtual ~ArenaOnlyGladiator() { }

  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;

  // the ignored parameter keeps us from stepping on placement new
  template <class T>
  ABSL_DEPRECATED("use googlesql_base::NewInArena from base/arena_allocator.h instead")
  void* operator new(const size_t size, const int ignored, T* allocator) {
    assert(allocator);
    return allocator->AllocAligned(size, BaseArena::kDefaultAlignment);
  }

  void operator delete(void* /*memory*/, const size_t /*size*/) {}

  template <class T>
  ABSL_DEPRECATED("use googlesql_base::DeleteInArena from base/arena_allocator.h instead")
  void operator delete(void* memory, const size_t size, const int ign,
                       T* allocator) {}

  void operator delete[](void* /*memory*/) {}

  template <class T>
  ABSL_DEPRECATED("use googlesql_base::DeleteInArena from base/arena_allocator.h instead")
  void operator delete(void* memory, const int ign, T* allocator) {}
};

#if 0  // ********** for example purposes only; 100% untested.

// Note that this implementation incurs an overhead of kHeaderSize for
// every array that is allocated.  *Before* the space is returned to the
// user, we store the address of the Arena that owns the space, and
// the length of th space itself.

class ArrayGladiator : public Gladiator {
 public:
  void * operator new[] (const size_t size) {
    const int sizeplus = size + kHeaderSize;
    void * const ret = ::operator new(sizeplus);
    *static_cast<Arena **>(ret) = nullptr;  // mark as heap-allocated
    *static_cast<size_t *>(ret + sizeof(Arena *)) = sizeplus;
    return ret + kHeaderSize;
  }
  // the ignored parameter keeps us from stepping on placement new
  template<class T> void * operator new[] (const size_t size,
                                           const int ignored, T * allocator) {
    if (allocator) {
      const int sizeplus = size + kHeaderSize;
      void * const ret =
          allocator->AllocAligned(sizeplus, BaseArena::kDefaultAlignment);
      *static_cast<Arena **>(ret) = allocator->arena();
      *static_cast<size_t *>(ret + sizeof(Arena *)) = sizeplus;
      return ret + kHeaderSize;
    } else {
      return operator new[](size);  // this is the function above
    }
  }
  void operator delete [] (void * memory) {
    memory -= kHeaderSize;
    Arena * const arena = *static_cast<Arena **>(memory);
    const size_t sizeplus = *static_cast<size_t *>(memory + sizeof(arena));
    if (arena) {
      arena->SlowFree(memory, sizeplus);
    } else {
      ::operator delete (memory);
    }
  }
  template<class T> void * operator delete (void * memory,
                                            const int ign, T * allocator) {
    // This "placement delete" can only be called if the constructor
    // throws an exception.
    memory -= kHeaderSize;
    const size_t sizeplus = *static_cast<size_t *>(memory + sizeof(Arena *));
    if (allocator) {
      allocator->Free(memory, 1 + size);
    } else {
      operator delete (memory);
    }
  }

 protected:
  static const int kMinSize = sizeof size_t + sizeof(Arena *);
  static const int kHeaderSize = kMinSize > BaseArena::kDefaultAlignment ?
    2 * BaseArena::kDefaultAlignment : BaseArena::kDefaultAlignment;
};

#endif  // ********** example

// NewInArena<T>, DeleteInArena<T>: functions for constructing and destroying
// C++ objects inside an Arena. This is useful when doing one-time allocations,
// instead of having to create a persisting ArenaAllocator for each desired
// type. It is also safer and less verbose than dealing with the C-style
// interface of Arena directly.
//
// NewInArena<T> properly supports over-aligned types. These are types with
// alignment requirement greater than that of any fundamental type
// (i.e. alignof(std::max_align_t)). These types can occur as either platform
// specific extension types or as user types defined with an alignas specifier.
//
// NewInArena<T> is a drop-in replacement for the `AllocateInArena` operator
// new overloads that are defined below and as part of `ArenaOnlyGladiator`
// above. It is *not* a replacement for the operator new overloads in
// `Gladiator`, and using the function in this way will result in compile-time
// error.
//
// Example allocation:
//
//   struct MyType {
//     MyType();
//     MyType(int, int);
//   };
//
//   UnsafeArena arena(1024);
//   MyType* p = googlesql_base::NewInArena<MyType>(&arena, 5, 3);
//   MyType* q = googlesql_base::NewInArena<MyType[]>(&arena, 10);
//
// When finished, you must destroy the objects and return memory as follows:
//
//   googlesql_base::DeleteInArena(&arena, p);
//   googlesql_base::DeleteInArena(&arena, q, 10);
//
// NOTE: there are current uses of the `AllocateInArena` operator new that
// do not call delete. These uses rely on Arena::Reset() or ~Arena() to return
// memory, because the types in question have trivial destructors or do not
// perform resource management. Such uses should be updated to use
// DeleteInArena<T> for safety against undefined behavior.
template <typename T, typename ArenaType, typename... Args,
          std::enable_if_t<!std::is_array<T>::value>* = nullptr>
// NOLINTNEXTLINE - suppress ClangTidy raw pointer return.
T* NewInArena(ArenaType* arena, Args&&... args) {
  static_assert(!std::is_base_of<Gladiator, T>::value,
                "Allocating an object with type inheriting from Gladiator is "
                "not supported by NewInArena.");
  return ::new (static_cast<void*>(arena->AllocAligned(sizeof(T), alignof(T))))
      T(std::forward<Args>(args)...);
}

template <typename T, typename ArenaType,
          std::enable_if_t<std::is_array<T>::value &&
                           std::extent<T>::value == 0>* = nullptr>
// NOLINTNEXTLINE - suppress ClangTidy raw pointer return.
std::remove_extent_t<T>* NewInArena(ArenaType* arena, size_t n) {
  static_assert(!std::is_base_of<Gladiator, T>::value,
                "Allocating an object with type inheriting from Gladiator is "
                "not supported by NewInArena.");
  using U = std::remove_extent_t<T>;
  U* p = static_cast<U*>(arena->AllocAligned(n * sizeof(U), alignof(U)));
  for (size_t i = 0; i < n; ++i) ::new (static_cast<void*>(p + i)) U;
  return p;
}

template <typename T, typename... Args,
          std::enable_if_t<std::is_array<T>::value &&
                           std::extent<T>::value != 0>* = nullptr>
// NOLINTNEXTLINE - suppress ClangTidy raw pointer return.
T* NewInArena(Args&&... args) = delete;

template <typename ArenaType, typename T>
void DeleteInArena(ArenaType* arena, T* t) {
  static_assert(!std::is_base_of<Gladiator, T>::value,
                "Destroying an object with type inheriting from Gladiator is "
                "not supported by DeleteInArena.");
  t->~T();
  arena->Free(t, sizeof(T));
}

template <typename ArenaType, typename T>
void DeleteInArena(ArenaType* arena, T* t, size_t n) {
  static_assert(!std::is_base_of<Gladiator, T>::value,
                "Destroying an object with type inheriting from Gladiator is "
                "not supported by DeleteInArena.");
  for (size_t i = 0; i < n; ++i) (t + i)->~T();
  arena->Free(t, n * sizeof(T));
}

enum AllocateInArenaType { AllocateInArena };

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ARENA_ALLOCATOR_H_
