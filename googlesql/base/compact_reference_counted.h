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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_COMPACT_REFERENCE_COUNTED_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_COMPACT_REFERENCE_COUNTED_H_

#include <stddef.h>
#include <atomic>
#include <type_traits>

namespace googlesql_base {
namespace refcount {

// Classes that wish to have thread-safe reference counting can simply inherit
// from this CompactReferenceCounted class, templated on their own type.
//
// CompactReferenceCounted additionally provides some functions:
// RefCountIsOne() and OnRefCountIsZero().
// These functions should be used with great care and only by classes that have
// very strict reference count contracts.
// See the documentation on these functions for usage and details.
template <typename Derived, typename RefcountT = int32_t>
class CompactReferenceCounted {
 public:
  // It is important that the ref_count_ is initialized to 1, so the caller
  // owns a reference.  The caller must eventually call Unref() to release it.
  CompactReferenceCounted() : ref_count_(1) {}

  // We delete both the move constructor and copy constructor.
  // A move constructor or any function accepting an rvalue reference to a
  // reference counted object does not add much value. Reference counted objects
  // are always reference objects; it is illegal for any function to return a
  // reference counted object by value. So there is not really such a thing as
  // an rvalue reference counted object.
  // Lacking any obvious benefits, and given the subtleties of move constructors
  // and defining a reference counted object's invariants with move semantics,
  // we disable the default move constructor.
  // A copy constructor has similar (although less subtle) considerations, and
  // as we do not provide a move constructor, we do not provide a copy
  // constructor either.
  // Derived classes can still create their own copy constructors if they desire
  // one. Such a custom constructor should simply invoke the default constructor
  // of CompactReferenceCounted which will initialize the ref count of the new
  // instance to 1. Creating a move constructor is strongly discouraged.
  CompactReferenceCounted(const CompactReferenceCounted&) = delete;
  CompactReferenceCounted(CompactReferenceCounted&&) = delete;

  // Like ctors, we have no use for default assignment operators.
  CompactReferenceCounted& operator=(const CompactReferenceCounted&) = delete;
  CompactReferenceCounted& operator=(CompactReferenceCounted&&) = delete;

  // Take possession of a reference on this, which must eventually be released
  // with Unref().
  void Ref() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  // Drop a reference on this, which ought to have been owned by the caller.
  // WARNING: Unref() may delete the object and it should not be touched once
  // a reference is no longer held.
  void Unref() const {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
      AsDerived()->OnRefCountIsZero();
    }
  }

  // Returns true if the reference count is exactly 1
  //
  // Applications with a strong reference counting contract can use this value
  // to determine they are the sole logical owner holding a reference on a
  // given instance.
  //
  // Such applications typically don't hold raw pointers to instances without
  // also holding a reference, manage all references through ptr classes such as
  // refcount::reffed_ptr, or use other means to ensure that code can not refer
  // to an instance without also holding a reference on that instance.
  //
  // One example how RefCountIsOne() can be used is to implement copy-on-write
  // logic with an optimization that avoids a copy if the reference count is
  // one; i.e., the caller is the sole owner of the instance.
  bool RefCountIsOne() const {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }

 protected:
  // This is protected to prevent its direct invocation from outside of a
  // CompactReferenceCounted object.  Unfortunately, there is still a risk that
  // a subclass will invoke its destructor directly, rather than through
  // Unref().  Apparently, gcc doesn't permit inheritance from a class with a
  // private destructor, so this has to be protected.
  ~CompactReferenceCounted() {}

  // The OnRefCountIsZero() function is invoked by Unref() once the reference
  // count reaches 0, i.e., when the final reference on this instance has been
  // removed and the object can be deleted.
  //
  // The default implementation will simply delete the instance, i.e., call
  // 'delete this'. Derived classes can override this function if they require
  // custom finalization / deallocation functions, or if they otherwise want to
  // control the life cycle of their instances.
  //
  // There is no requirement that the OnRefCountIsZero() method directly
  // destroys the object. For example, a derived class which has a substantial
  // construction and allocation cost may choose to override the
  // OnRefCountIsZero() method and freelist instances for re-use rather then
  // delete them directly.
  //
  // Classes that implement the OnRefCountIsZero() function to freelist objects
  // must make sure to call Ref() on the object before re-using it.
  // The preferred pattern is to call Ref() (and perform other essential class
  // re-initialization) directly inside the OnRefCountIsZero() function before
  // freelisting or otherwise re-using the object.
  //
  // While we explictly allow/require the reference count to be incremented
  // again from the OnRefCountIsZero handler extending the instance's life
  // cycle, calling Unref() directly or indirectly from OnRefCountIsZero()
  // should be avoided as it may trigger the reference count to drop to zero,
  // and cause a recursive loop.
  // Applications with custom OnRefCountIsZero handlers must make sure that the
  // reference count does not reach zero again inside the handler function.
  //
  // Example: (assuming an imaginary MyFreeList class with push / pop semantics)
  //
  //   class MyHeavyBlob : public CompactReferenceCounted<MyHeavyBlob> {
  //    public:
  //     static MyHeavyBlob* New() {
  //        MyHeavyBlob* blob = m_freelist.Pop();
  //        return (blob != nullptr) ? blob : new MyHeavyBlob;
  //     }
  //
  //    private:
  //     MyHeavyBlob() = default;
  //
  //     void FreeList() {
  //        // Reset class to a 'fresh state':
  //        // - Call Ref() to snatch 'this' away from the claws of death.
  //        // - Clear items_ (std::vector's large allocated memory is retained)
  //        Ref();
  //        items_.clear();
  //        if (!m_freelist.Add(this)) delete this;
  //     }
  //
  //     void OnRefCountIsZero() const override {
  //       const_cast<MyHeavyBlob*>(this)->Freelist();
  //     }
  //
  //     std::vector<BigStruct> items_;
  //   };
  void OnRefCountIsZero() const { delete AsDerived(); }

  // Protected accessor for testing/debugging purposes in base classes.
  RefcountT ref_count() const {
    return ref_count_.load(std::memory_order_acquire);
  }

 private:
  const Derived* AsDerived() const {
    static_assert(
        std::is_final_v<Derived>,
        "Please mark the derived class as final to avoid deleting only "
        "part of the object. If your object provides a vtable, "
        "use `SimpleReferenceCounted` instead.");
    return static_cast<const Derived*>(this);
  }

  mutable std::atomic<RefcountT> ref_count_;
};

}  // namespace refcount
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_COMPACT_REFERENCE_COUNTED_H_
