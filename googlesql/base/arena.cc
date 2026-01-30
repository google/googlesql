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
// This approach to arenas overcomes many of the limitations described
// in the "Specialized allocators" section of
//     http://www.pdos.lcs.mit.edu/~dm/c++-new.html
//
// A somewhat similar approach to Gladiator, but for heap-detection, was
// suggested by Ron van der Wal and Scott Meyers at
//     http://www.aristeia.com/BookErrata/M27Comments_frames.html

#include "googlesql/base/arena.h"

#include <assert.h>
#include <stdarg.h>  // for vsnprintf
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <iterator>
#include <new>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {

static void *aligned_malloc(size_t size, int minimum_alignment) {
  void *ptr = nullptr;
  // posix_memalign requires that the requested alignment be at least
  // sizeof(void*). In this case, fall back on malloc which should return memory
  // aligned to at least the size of a pointer.
  const int required_alignment = sizeof(void*);
  if (minimum_alignment < required_alignment)
    return malloc(size);
  if (posix_memalign(&ptr, static_cast<size_t>(minimum_alignment), size) != 0)
    return nullptr;
  else
    return ptr;
}

// The value here doesn't matter until page_aligned_ is supported.
static const int kPageSize = 8192;   // should be getpagesize()

// We used to only keep track of how much space has been allocated in
// debug mode. Now we track this for optimized builds, as well. If you
// want to play with the old scheme to see if this helps performance,
// change this ARENASET() macro to a NOP. However, NOTE: some
// applications of arenas depend on this space information (exported
// via bytes_allocated()).
#define ARENASET(x) (x)

const int BaseArena::kDefaultAlignment;

// ----------------------------------------------------------------------
// BaseArena::BaseArena()
// BaseArena::~BaseArena()
//    Destroying the arena automatically calls Reset()
// ----------------------------------------------------------------------

BaseArena::BaseArena(char* first, const size_t orig_block_size,
                     bool align_to_page)
    : remaining_(0),
      block_size_(orig_block_size),
      freestart_(nullptr),  // set for real in Reset()
      last_alloc_(nullptr),
      overflow_blocks_(nullptr),
      first_block_externally_owned_(first != nullptr),
      page_aligned_(align_to_page),
      blocks_alloced_(1) {
  // Trivial check that aligned objects can actually be allocated.
  ABSL_CHECK_GT(block_size_, static_cast<size_t>(kDefaultAlignment))
      << "orig_block_size = " << orig_block_size;
  if (page_aligned_) {
    // kPageSize must be power of 2, so make sure of this.
    ABSL_CHECK(kPageSize > 0 && 0 == (kPageSize & (kPageSize - 1)))
                              << "kPageSize[ " << kPageSize << "] is not "
                              << "correctly initialized: not a power of 2.";
  }

  if (first) {
    ABSL_CHECK(!page_aligned_ ||
          (reinterpret_cast<uintptr_t>(first) & (kPageSize - 1)) == 0);
    first_blocks_[0].mem = first;
    first_blocks_[0].size = orig_block_size;
  } else {
    if (page_aligned_) {
      // Make sure the blocksize is page multiple, as we need to end on a page
      // boundary.
      ABSL_CHECK_EQ(block_size_ & (kPageSize - 1), 0) << "block_size is not a"
                                                 << "multiple of kPageSize";
      first_blocks_[0].mem = reinterpret_cast<char*>(aligned_malloc(block_size_,
                                                                    kPageSize));
      ABSL_PCHECK(nullptr != first_blocks_[0].mem);
    } else {
      first_blocks_[0].mem = reinterpret_cast<char*>(malloc(block_size_));
    }
    first_blocks_[0].size = block_size_;
  }

  Reset();
}

BaseArena::~BaseArena() {
  FreeBlocks();
  assert(overflow_blocks_ == nullptr);    // FreeBlocks() should do that
#ifdef ADDRESS_SANITIZER
  if (first_block_externally_owned_) {
    ASAN_UNPOISON_MEMORY_REGION(first_blocks_[0].mem, first_blocks_[0].size);
  }
#endif
  // The first X blocks stay allocated always by default.  Delete them now.
  for (int i = first_block_externally_owned_ ? 1 : 0; i < blocks_alloced_; ++i)
    free(first_blocks_[i].mem);
}

// ----------------------------------------------------------------------
// BaseArena::block_count()
//    Only reason this is in .cc file is because it involves STL.
// ----------------------------------------------------------------------

int BaseArena::block_count() const {
  return (blocks_alloced_ +
          (overflow_blocks_ ? static_cast<int>(overflow_blocks_->size()) : 0));
}

// Returns true iff it advances freestart_ to the first position
// satisfying alignment without exhausting the current block.
bool BaseArena::SatisfyAlignment(size_t alignment) {
  const size_t overage =
      reinterpret_cast<size_t>(freestart_) & (alignment - 1);
  if (overage > 0) {
    const size_t waste = alignment - overage;
    if (waste >= remaining_) {
      return false;
    }
    freestart_ += waste;
    remaining_ -= waste;
  }
  ABSL_DCHECK_EQ(0, reinterpret_cast<size_t>(freestart_) & (alignment - 1));
  return true;
}

// ----------------------------------------------------------------------
// BaseArena::Reset()
//    Clears all the memory an arena is using.
// ----------------------------------------------------------------------

void BaseArena::Reset() {
  FreeBlocks();
  freestart_ = first_blocks_[0].mem;
  remaining_ = first_blocks_[0].size;
  last_alloc_ = nullptr;
#ifdef ADDRESS_SANITIZER
  ASAN_POISON_MEMORY_REGION(freestart_, remaining_);
#endif

  ARENASET(status_.bytes_allocated_ = block_size_);

  // There is no guarantee the first block is properly aligned, so
  // enforce that now.
  ABSL_CHECK(SatisfyAlignment(kDefaultAlignment));

  freestart_when_empty_ = freestart_;
}

// ----------------------------------------------------------------------
// BaseArena::MakeNewBlock()
//    Our sbrk() equivalent.  We always make blocks of the same size
//    (though GetMemory() can also make a new block for really big
//    data.
// ----------------------------------------------------------------------

void BaseArena::MakeNewBlock(const uint32_t alignment) {
  AllocatedBlock *block = AllocNewBlock(block_size_, alignment);
  freestart_ = block->mem;
  remaining_ = block->size;
  ABSL_CHECK(SatisfyAlignment(alignment));
}

// The following simple numeric routines also exist in util/math/mathutil.h
// but we don't want to depend on that library.

// Euclid's algorithm for Greatest Common Denominator.
static uint32_t GCD(uint32_t x, uint32_t y) {
  while (y != 0) {
    uint32_t r = x % y;
    x = y;
    y = r;
  }
  return x;
}

static uint32_t LeastCommonMultiple(uint32_t a, uint32_t b) {
  if (a > b) {
    return (a / GCD(a, b)) * b;
  } else if (a < b) {
    return (b / GCD(b, a)) * a;
  } else {
    return a;
  }
}

// -------------------------------------------------------------
// BaseArena::AllocNewBlock()
//    Adds and returns an AllocatedBlock.
//    The returned AllocatedBlock* is valid until the next call
//    to AllocNewBlock or Reset.  (i.e. anything that might
//    affect overflow_blocks_).
// -------------------------------------------------------------

BaseArena::AllocatedBlock* BaseArena::AllocNewBlock(const size_t block_size,
                                                    const uint32_t alignment) {
  AllocatedBlock *block;
  // Find the next block.
  if (blocks_alloced_ < static_cast<int64_t>(ABSL_ARRAYSIZE(first_blocks_))) {
    // Use one of the pre-allocated blocks
    block = &first_blocks_[blocks_alloced_++];
  } else {  // oops, out of space, move to the vector
    if (overflow_blocks_ == nullptr)
      overflow_blocks_ = new std::vector<AllocatedBlock>;
    // Adds another block to the vector.
    overflow_blocks_->resize(overflow_blocks_->size()+1);
    // block points to the last block of the vector.
    block = &overflow_blocks_->back();
  }

  // NOTE(tucker): this utility is made slightly more complex by
  // not disallowing the case where alignment > block_size.
  // Can we, without breaking existing code?

  // If page_aligned_, then alignment must be a multiple of page size.
  // Otherwise, must be a multiple of kDefaultAlignment, unless
  // requested alignment is 1, in which case we don't care at all.
  const uint32_t adjusted_alignment =
      page_aligned_
          ? LeastCommonMultiple(kPageSize, alignment)
          : (alignment > 1 ? LeastCommonMultiple(alignment, kDefaultAlignment)
                           : 1);
  ABSL_CHECK_LE(adjusted_alignment, 1U << 20)
      << "Alignment on boundaries greater than 1MB not supported.";

  // If block_size > alignment we force block_size to be a multiple
  // of alignment; if block_size < alignment we make no adjustment, unless
  // page_aligned_ is true, in which case it must be a multiple of
  // kPageSize because SetProtect() will assume that.
  size_t adjusted_block_size = block_size;
  if (adjusted_alignment > 1) {
    if (adjusted_block_size > adjusted_alignment) {
      const uint32_t excess = adjusted_block_size % adjusted_alignment;
      adjusted_block_size += (excess > 0 ? adjusted_alignment - excess : 0);
    }
    if (page_aligned_) {
      size_t num_pages = ((adjusted_block_size - 1)/kPageSize) + 1;
      adjusted_block_size = num_pages * kPageSize;
    }
    block->mem = reinterpret_cast<char*>(aligned_malloc(adjusted_block_size,
                                                        adjusted_alignment));
  } else {
    block->mem = reinterpret_cast<char*>(malloc(adjusted_block_size));
  }
  block->size = adjusted_block_size;
  ABSL_PCHECK(nullptr != block->mem)
      << "block_size=" << block_size
      << " adjusted_block_size=" << adjusted_block_size
      << " alignment=" << alignment
      << " adjusted_alignment=" << adjusted_alignment;

  ARENASET(status_.bytes_allocated_ += adjusted_block_size);

#ifdef ADDRESS_SANITIZER
  ASAN_POISON_MEMORY_REGION(block->mem, block->size);
#endif
  return block;
}

// ----------------------------------------------------------------------
// BaseArena::IndexToBlock()
//    Index encoding is as follows:
//    For blocks in the first_blocks_ array, we use index of the block in
//    the array.
//    For blocks in the overflow_blocks_ vector, we use the index of the
//    block in iverflow_blocks_, plus the size of the first_blocks_ array.
// ----------------------------------------------------------------------

const BaseArena::AllocatedBlock *BaseArena::IndexToBlock(int index) const {
  if (index < static_cast<int64_t>(ABSL_ARRAYSIZE(first_blocks_))) {
    return &first_blocks_[index];
  }
  ABSL_CHECK(overflow_blocks_ != nullptr);
  int index_in_overflow_blocks = index - ABSL_ARRAYSIZE(first_blocks_);
  ABSL_CHECK_GE(index_in_overflow_blocks, 0);
  ABSL_CHECK_LT(static_cast<size_t>(index_in_overflow_blocks),
           overflow_blocks_->size());
  return &(*overflow_blocks_)[index_in_overflow_blocks];
}

// ----------------------------------------------------------------------
// BaseArena::GetMemoryFallback()
//    We take memory out of our pool, aligned on the byte boundary
//    requested.  If we don't have space in our current pool, we
//    allocate a new block (wasting the remaining space in the
//    current block) and give you that.  If your memory needs are
//    too big for a single block, we make a special your-memory-only
//    allocation -- this is equivalent to not using the arena at all.
// ----------------------------------------------------------------------

void* BaseArena::GetMemoryFallback(const size_t size, const int alignment) {
  if (0 == size) {
    return nullptr;             // stl/stl_alloc.h says this is okay
  }

  // alignment must be a positive power of 2.
  ABSL_CHECK(alignment > 0 && 0 == (alignment & (alignment - 1)));

  // If the object is more than a quarter of the block size, allocate
  // it separately to avoid wasting too much space in leftover bytes.
  if (block_size_ == 0 || size > block_size_/4) {
    // Use a block separate from all other allocations; in particular
    // we don't update last_alloc_ so you can't reclaim space on this block.
    AllocatedBlock* b = AllocNewBlock(size, alignment);
#ifdef ADDRESS_SANITIZER
    ASAN_UNPOISON_MEMORY_REGION(b->mem, b->size);
#endif
    return b->mem;
  }

  // Enforce alignment on freestart_ then check for adequate space,
  // which may require starting a new block.
  if (!SatisfyAlignment(alignment) || size > remaining_) {
    MakeNewBlock(alignment);
  }
  ABSL_CHECK_LE(size, remaining_);

  remaining_ -= size;
  last_alloc_ = freestart_;
  freestart_ += size;

#ifdef ADDRESS_SANITIZER
  ASAN_UNPOISON_MEMORY_REGION(last_alloc_, size);
#endif
  return reinterpret_cast<void*>(last_alloc_);
}

// ----------------------------------------------------------------------
// BaseArena::ReturnMemoryFallback()
// BaseArena::FreeBlocks()
//    Unlike GetMemory(), which does actual work, ReturnMemory() is a
//    no-op: we don't "free" memory until Reset() is called.  We do
//    update some stats, though.  Note we do no checking that the
//    pointer you pass in was actually allocated by us, or that it
//    was allocated for the size you say, so be careful here!
//       FreeBlocks() does the work for Reset(), actually freeing all
//    memory allocated in one fell swoop.
// ----------------------------------------------------------------------

void BaseArena::FreeBlocks() {
  for ( int i = 1; i < blocks_alloced_; ++i ) {  // keep first block alloced
    free(first_blocks_[i].mem);
    first_blocks_[i].mem = nullptr;
    first_blocks_[i].size = 0;
  }
  blocks_alloced_ = 1;
  if (overflow_blocks_ != nullptr) {
    std::vector<AllocatedBlock>::iterator it;
    for (it = overflow_blocks_->begin(); it != overflow_blocks_->end(); ++it) {
      free(it->mem);
    }
    delete overflow_blocks_;             // These should be used very rarely
    overflow_blocks_ = nullptr;
  }
}

// ----------------------------------------------------------------------
// BaseArena::AdjustLastAlloc()
//    If you realize you didn't want your last alloc to be for
//    the size you asked, after all, you can fix it by calling
//    this.  We'll grow or shrink the last-alloc region if we
//    can (we can always shrink, but we might not be able to
//    grow if you want to grow too big.
//      RETURNS true if we successfully modified the last-alloc
//    region, false if the pointer you passed in wasn't actually
//    the last alloc or if you tried to grow bigger than we could.
// ----------------------------------------------------------------------

bool BaseArena::AdjustLastAlloc(void *last_alloc, const size_t newsize) {
  // It's only legal to call this on the last thing you alloced.
  if (last_alloc == nullptr || last_alloc != last_alloc_)  return false;
  // last_alloc_ should never point into a "big" block, w/ size >= block_size_
  assert(freestart_ >= last_alloc_ && freestart_ <= last_alloc_ + block_size_);
  assert(remaining_ >= 0);   // should be: it's a size_t!
  if (newsize > (freestart_ - last_alloc_) + remaining_)
    return false;  // not enough room, even after we get back last_alloc_ space
  const char* old_freestart = freestart_;   // where last alloc used to end
  freestart_ = last_alloc_ + newsize;       // where last alloc ends now
  remaining_ -= (freestart_ - old_freestart);  // how much new space we took

#ifdef ADDRESS_SANITIZER
  ASAN_UNPOISON_MEMORY_REGION(last_alloc_, newsize);
  ASAN_POISON_MEMORY_REGION(freestart_, remaining_);
#endif
  return true;
}

// ----------------------------------------------------------------------
// UnsafeArena::Realloc()
// SafeArena::Realloc()
//    If you decide you want to grow -- or shrink -- a memory region,
//    we'll do it for you here.  Typically this will involve copying
//    the existing memory to somewhere else on the arena that has
//    more space reserved.  But if you're reallocing the last-allocated
//    block, we may be able to accommodate you just by updating a
//    pointer.  In any case, we return a pointer to the new memory
//    location, which may be the same as the pointer you passed in.
//       Here's an example of how you might use Realloc():
//
//    compr_buf = arena->Alloc(uncompr_size);  // get too-much space
//    int compr_size;
//    zlib.Compress(uncompr_buf, uncompr_size, compr_buf, &compr_size);
//    compr_buf = arena->Realloc(compr_buf, uncompr_size, compr_size);
// ----------------------------------------------------------------------

char* UnsafeArena::Realloc(char* original, size_t oldsize, size_t newsize) {
  assert(oldsize >= 0 && newsize >= 0);
  // if original happens to be the last allocation we can avoid fragmentation.
  if (AdjustLastAlloc(original, newsize)) {
    return original;
  }

  char* resized = original;
  if (newsize > oldsize) {
    resized = Alloc(newsize);
    memcpy(resized, original, oldsize);
  } else {
    // no need to do anything; we're ain't reclaiming any memory!
  }

#ifdef ADDRESS_SANITIZER
  // Alloc already returns unpoisoned memory, but handling both cases here
  // allows us to poison the old memory without worrying about whether or not it
  // overlaps with the new memory.  Thus, we must poison the old memory first.
  ASAN_POISON_MEMORY_REGION(original, oldsize);
  ASAN_UNPOISON_MEMORY_REGION(resized, newsize);
#endif
  return resized;
}

char* SafeArena::Realloc(char* original, size_t oldsize, size_t newsize) {
  assert(oldsize >= 0 && newsize >= 0);
  // if original happens to be the last allocation we can avoid fragmentation.
  {
    absl::MutexLock lock(&mutex_);
    if (AdjustLastAlloc(original, newsize)) {
      return original;
    }
  }

  char* resized = original;
  if (newsize > oldsize) {
    resized = Alloc(newsize);
    memcpy(resized, original, oldsize);
  } else {
    // no need to do anything; we're ain't reclaiming any memory!
  }

#ifdef ADDRESS_SANITIZER
  // Alloc already returns unpoisoned memory, but handling both cases here
  // allows us to poison the old memory without worrying about whether or not it
  // overlaps with the new memory.  Thus, we must poison the old memory first.
  ASAN_POISON_MEMORY_REGION(original, oldsize);
  ASAN_UNPOISON_MEMORY_REGION(resized, newsize);
#endif
  return resized;
}

// Avoid weak vtables by defining a dummy key method.
void UnsafeArena::UnusedKeyMethod() {}
void SafeArena::UnusedKeyMethod() {}

}  // namespace googlesql_base
