//
// Copyright 2000 Google LLC
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
// A trie is a 256-ary tree that represents a set of strings. GeneralTrie is a
// class that uses a trie to map each string in some set to a piece of data
// of type T. GeneralTrie comes in two flavors:
//
// * GeneralTrie<T, T NULL_VALUE>, where NULL_VALUE is a value of type
// T used as a placeholder at intermediate nodes in the tree, and T must be an
// integral type.
//
// * ClassGeneralTrie<T>, where a default constructed T is used as the null
// value, and T must have copy and default constructor, assignment and equality
// operators (used only to check against the null value). Hint: Wrapping your
// class with linked_ptr is enough to satisfy the requirements, but remember
// that a linked_ptr copy is a read-write operation (see linked_ptr.h), so you
// should prefer manipulating the returned references to avoid concurrency
// problems, in the presence of multiple reading threads.
//
// Both classes offer exactly the same interface of the GeneralClassImpl class
// seen in the beginning of this file.
//
// Please note that GeneralTrie is not thread safe.

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_GENERAL_TRIE_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_GENERAL_TRIE_H_

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "absl/base/attributes.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql_base {

// The GeneralTrieImpl receives two template parameters to be able to model
// both the GeneralTrie<class T, T NULL_VALUE> for integral types and
// ClassGeneralTrie<T>. This is an implementation trick to add the
// ClassGeneralTrie functionality without breaking existing uses of
// GeneralTrie<class T, T NULL_VALUE>. You probably don't want to instantiate
// GeneralTrieImpl directly, but rather one of these other two classes, defined
// in the end of the file.
template <class T, class NullValuePolicy>  // NullValuePolicy implements Null()
class GeneralTrieImpl {
 public:
  // Abstract class for objects that can be passed to PreorderTraverse() and
  // PostorderTraverse().
  class Traverser {
   public:
    virtual ~Traverser();
    virtual void Process(const std::string& s, const T& data) = 0;
  };

  typedef T value_type;

  typedef std::pair<std::string, T> TrieData;

  GeneralTrieImpl();
  ~GeneralTrieImpl();

  // Inserts the given string into the trie (or finds it if it's already
  // there) and associates a copy of the given data with it.
  void Insert(absl::string_view key, const T& data);

  // Returns the data associated with key in the trie, or
  // NullValuePolicy::Null() if key is not in the trie.
  const T& GetData(absl::string_view key) const;

  // Returns a reference to the data associated with key in the trie, or
  // NullValuePolicy::Null() if key is not in the trie.
  T& GetData(absl::string_view key);

  // If `key` is present in the trie, updates the data associated with key and
  // returns true; else, it makes no change in the trie and returns false.
  bool SetData(absl::string_view key, const T& data);

  // Finds the greatest number n such that:
  //  1. the first n characters of key form a string in the trie;
  //  2. (n == key.length()) or is_terminator[key[n]] is true.
  // If there is no n that satisfies these conditions, the method returns
  // NullValuePolicy::Null().  Otherwise, it returns the data associated with
  // the first n characters of key and sets *chars_matched to n.  is_terminator
  // should point to an array of 256 bools.  You can also pass a null pointer
  // for is_terminator, in which case the function treats every character as a
  // terminator, so condition 2 is always satisfied.
  const T& GetDataForMaximalPrefix(absl::string_view key, int* chars_matched,
                                   const bool* is_terminator) const;
  const T& GetDataForMaximalPrefix(absl::string_view key, int& chars_matched,
                                   absl::Span<const bool> is_terminator) const {
    return GetDataForMaximalPrefix(key, &chars_matched, is_terminator.data());
  }

  // Gets all strings (and associated data) matching the given
  // string. The given string must match in its entirety.  Note: empty
  // input string matches everything in the trie
  void GetAllMatchingStrings(absl::string_view key,
                             std::vector<TrieData>* outdata) const;

  // Calls traverser->Process() for each string in the trie.
  void PreorderTraverse(Traverser* traverser) const {
    PreorderTraverseDepth(traverser, -1);
  }

  // Like PreorderTraverse(), but you can specify the depth of
  // traversal.
  void PreorderTraverseDepth(Traverser* traverser, int depth) const {
    std::string s;
    Traverse(traverser, &s, depth, true);
  }

  // Calls traverser->Process() for each matching string in the trie.
  void PreorderTraverseAllMatchingStrings(absl::string_view key,
                                          Traverser* traverser) const {
    PreorderTraverseAllMatchingStringsDepth(key, traverser, -1);
  }

  // Like PreorderTraverseAllMatchingStrings(), but you can specify
  // the depth of traversal.  Note the semantics of the depth of
  // traversal here.  An exact match is depth 0.  If there are no
  // exact matches, a "root" is still assumed at this exact match, and
  // the depth is counted from there.
  void PreorderTraverseAllMatchingStringsDepth(absl::string_view key,
                                               Traverser* traverser,
                                               int depth) const {
    TraverseAllMatchingStrings(key, traverser, depth, true);
  }

  // Postorder versions of the traversal functions.
  void PostorderTraverse(Traverser* traverser) const {
    PostorderTraverseDepth(traverser, -1);
  }
  void PostorderTraverseDepth(Traverser* traverser, int depth) const {
    std::string s;
    Traverse(traverser, &s, depth, false);
  }
  void PostorderTraverseAllMatchingStrings(const char* s, int len,
                                           Traverser* traverser) const {
    TraverseAllMatchingStrings(absl::string_view(s, len), traverser, -1, false);
  }

  // Calls traverser->Process() for each string in the trie that is a substring
  // of s.
  void TraverseAlongString(const absl::string_view key,
                           Traverser* traverser) const;

  void Print(int indent) const;

  // An iterator with an interface identical to the iterators in CompactTrie.
  class TraverseIterator {
   public:
    bool Done() const { return stack_.empty(); }

    // Note: the referenced key is mutated by each call to Next().
    const std::string& Key() const {
      ABSL_CHECK(!Done());
      return key_;
    }

    const T& Value() const {
      ABSL_CHECK(!Done());
      return stack_.back().first->data_;
    }

    void Next();

   private:
    friend class GeneralTrieImpl;
    typedef GeneralTrieImpl<T, NullValuePolicy> NodeT;

    explicit TraverseIterator(const GeneralTrieImpl<T, NullValuePolicy>* trie);

    // The stack stores the current path's nodes, deepest node on top (back).
    // For each node, the index of the next branch to take is stored.
    std::vector<std::pair<const NodeT*, int> > stack_;
    std::string key_;
  };

  // Returns an iterator over all keys in lexicographical order.
  TraverseIterator Traverse() const { return TraverseIterator(this); }

 private:
  typedef GeneralTrieImpl<T, NullValuePolicy> NodeT;

  std::string comppath_;  // string compression: must match to continue
  T data_;
  const T null_value_instance_;  // allows return by reference
  int min_next_;                 // next_ array goes from min to max-1
  int max_next_;
  NodeT** next_;  // array of "next level of the trie"

  inline NodeT* Next(int index) const;
  NodeT* SetNext(int index, NodeT* value);

  // Calls traverser->Process() for each string in either preorder or postorder.
  void Traverse(Traverser* traverser, std::string* s, int depth,
                bool preorder) const;

  // Calls traverser->Process() for each matching string in either preorder or
  // postorder.
  void TraverseAllMatchingStrings(absl::string_view key, Traverser* traverser,
                                  int depth, bool preorder) const;

  // get a ptr to the data corresponding to a given string.
  // returns NullValuePolicy::Null() if the string is not present in the trie.
  // Used as a helper method to get and set data
  const T* GetDataPtr(absl::string_view key) const;

  // Disallow copy constructor and operator=
  GeneralTrieImpl(const GeneralTrieImpl<T, NullValuePolicy>&);
  void operator=(const GeneralTrieImpl<T, NullValuePolicy>&);
};

template <class T, class NullValuePolicy>
GeneralTrieImpl<T, NullValuePolicy>::Traverser::~Traverser() {}

// NullValuePolicy for integral types
template <typename T, T NULL_VALUE>
struct IntegralNullValuePolicy {
  static T Null() { return NULL_VALUE; }
};

// NullValuePolicy for default constructed classes.
template <typename T>
struct DefaultConstructedNullValuePolicy {
  static T Null() { return T(); }
};

//
// Actual classes that matter to the API users
//

// GeneralTrie version for integral types (and pointers, which aren't integral
// types but are similar in some ways)
template <class T, T NULL_VALUE>
class GeneralTrie
    : public GeneralTrieImpl<T, IntegralNullValuePolicy<T, NULL_VALUE> > {
 public:
  // A static const member variable can only be initialized in the class
  // definition if it is an integral type, and we also want to support the case
  // where T is a pointer. Please see standard, clause 9.4.2, paragraph 4.
  static const T kNullValue;
  typedef IntegralNullValuePolicy<T, NULL_VALUE> null_value_policy;
};
// See comments on member declaration
template <class T, T NULL_VALUE>
const T GeneralTrie<T, NULL_VALUE>::kNullValue = NULL_VALUE;

// GeneralTrie version that supports classes, such as string,
// and linked_ptr<YourClass> (but see hint on top of header).
template <class T>
class ClassGeneralTrie
    : public GeneralTrieImpl<T, DefaultConstructedNullValuePolicy<T> > {
 public:
  typedef DefaultConstructedNullValuePolicy<T> null_value_policy;
};

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::GeneralTrieImpl()
// GeneralTrieImpl<T, NullValuePolicy>::~GeneralTrieImpl()
//    We just make sure everything is 0, and delete things that might
//    have been new-ed when we're done.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
GeneralTrieImpl<T, NullValuePolicy>::GeneralTrieImpl()
    : data_(NullValuePolicy::Null()),
      null_value_instance_(NullValuePolicy::Null()),
      min_next_(0),
      max_next_(0),
      next_(nullptr) {}

template <class T, class NullValuePolicy>
GeneralTrieImpl<T, NullValuePolicy>::~GeneralTrieImpl() {
  for (int i = min_next_; i < max_next_; i++)
    delete next_[i - min_next_];  // recursively calls the destructor
  delete[] next_;
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::Next()
// GeneralTrieImpl<T, NullValuePolicy>::SetNext()
//    Indexes into the next_ array.  This is slightly non-trivial
//    because next_ isn't 0-based (it's min_next_-based).  In
//    particular, we may have to move data around if index < min_next_.
//    We can also have to reallocate.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
typename GeneralTrieImpl<T, NullValuePolicy>::NodeT*
GeneralTrieImpl<T, NullValuePolicy>::Next(int index) const {
  if (index < min_next_ || index >= max_next_) return nullptr;
  assert(next_);
  return next_[index - min_next_];
}

template <class T, class NullValuePolicy>
typename GeneralTrieImpl<T, NullValuePolicy>::NodeT*
GeneralTrieImpl<T, NullValuePolicy>::SetNext(int index, NodeT* value) {
  assert(index >= 0 && index < 256);  // index should be a char

  if (min_next_ >= max_next_) {  // inserting first element
    assert(next_ == nullptr);    // or at least, it *should* be
    next_ = new NodeT*[1];
    next_[0] = value;
    min_next_ = index;
    max_next_ = index + 1;

  } else if (index < min_next_) {  // need to move array over
    NodeT** newnext = new NodeT*[max_next_ - index];
    for (int i = index; i < max_next_; i++)  // range of new next_
      newnext[i - index] = Next(i);
    newnext[0] = value;  // do the setting

    delete[] next_;  // replace it with newnext
    next_ = newnext;
    min_next_ = index;

  } else if (index >= max_next_) {  // just need to grow array
    NodeT** newnext = new NodeT*[index + 1 - min_next_];
    for (int i = min_next_; i < index; i++)  // range of new next_
      newnext[i - min_next_] = Next(i);
    newnext[index - min_next_] = value;  // do the setting

    delete[] next_;
    next_ = newnext;
    max_next_ = index + 1;  // it's actually 1+max

  } else {  // happy case: we're in range
    next_[index - min_next_] = value;
  }

  return Next(index);  // should be the same as "value"
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::Insert()
//    Adds a string key to a trie.  Once we've gotten to the leaf, we set
//    its data to the specified data.
//    The only complication is comppath.  Intuitively, comppath is
//    prepended to the "next" array before trying to descend: if comppath
//    is "arc", you can't follow next['h'] unless your string begins with
//    "arch".  If your string begins "bath" instead, we need to break up
//    the compression to insert.  For example: suppose n has comppath
//    = "stro" and key = "strong".  This works fine: we just follow
//    n->next['n'].  But what if key = "state"?  Then the "st" part of
//    comppath is ok, but not the "ro".  We change n->comppath to
//    "st", and then we introduce a new node c between n and its current
//    children, and set n->next['r'] = c, and c->comppath = "o", to finish
//    off the "stro".
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::Insert(absl::string_view key,
                                                 const T& data) {
  int diff;

  if (key.empty()) {  // we're at our leaf
    data_ = data;
    return;
  }

  int slen = key.length();
  // Break up compression if we have to
  if (comppath_.size() >= slen ||                // compression too long
      !absl::StartsWith(key, comppath_)) {       // or doesn't match
    for (diff = 0; diff < key.length(); diff++)  // pos of mismatch
      if (comppath_[diff] != key[diff]) break;
    if (diff == slen) {
      diff--;  // because we don't use '\0' as a child index
    }
    NodeT* child = new NodeT();
    for (int i = min_next_; i < max_next_; i++) {
      if (Next(i)) {
        child->SetNext(i, Next(i));
        SetNext(i, nullptr);  // not my child anymore
      }
    }
    SetNext(comppath_[diff], child);
    child->comppath_.assign(comppath_, diff + 1, comppath_.size() - diff - 1);
    // Remove the end of the comppath
    comppath_.erase(diff, comppath_.size() - diff);
  }
  key.remove_prefix(comppath_.size());
  slen -= comppath_.size();

  // At this point we know compression matches.
  // If root has no children, we can just modify comppath (it must have
  // been empty), and insert the rest of key as a child.  Otherwise we follow
  // the path based on the first char of key (creating a child node first if
  // need be).
  int i;
  for (i = min_next_; i < max_next_; i++)  // does root have any kids?
    if (Next(i)) break;
  if (i == max_next_) {                      // no? take over comppath
    comppath_.assign(key.data(), slen - 1);  // w/o kids, comppath was empty
    NodeT* next = SetNext(key[slen - 1], new NodeT());  // one kid now
    next->Insert(key.substr(slen), data);
  } else {  // has a kid, and a comppath
    NodeT* nextnode = Next(key[0]);
    if (!nextnode)  // but not the right kid
      nextnode = SetNext(key[0], new NodeT());
    nextnode->Insert(key.substr(1), data);  // continue down the trie
  }
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::TraverseAlongString()
//    Calls traverser->Process() for each string in the trie that is a substring
//    of key.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::TraverseAlongString(
    const absl::string_view key, Traverser* traverser) const {
  if (key.empty()) return;

  const NodeT* node = this;
  int next_pos = 0;
  std::string buf;
  buf.reserve(key.length());

  while (node) {
    if (node->data_ != null_value_instance_) {
      traverser->Process(buf, node->data_);
    }

    if (next_pos == key.size()) break;

    // Return if we don't match comppath.
    if (node->comppath_.size() >= (key.length() - next_pos) ||
        !absl::StartsWith(key.substr(next_pos), node->comppath_))
      break;

    // Advance next_pos beyond the comppath.
    next_pos += node->comppath_.size();

    // Update the buf with the traversed portion of s.
    buf.append(node->comppath_);
    buf.append(1, key[next_pos]);

    // Move onto the next node.
    node = node->Next(key[next_pos]);
    next_pos++;
  }
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::Print()
//    Tries to print the trie in a happy format.  Used for debugging.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::Print(int indent) const {
  if (!comppath_.empty()) {
    printf("%*s%s\n", indent, "", comppath_.c_str());
    indent += comppath_.size();
  }
  for (int i = min_next_; i < max_next_; i++) {
    if (Next(i)) {
      printf("%*s%c\n", indent, "", i);
      Next(i)->Print(indent + 1);
    }
  }
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::GetData()
//    Returns the data associated with key in the trie, or
//    NullValuePolicy::Null() if key is not in the trie.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
const T& GeneralTrieImpl<T, NullValuePolicy>::GetData(
    absl::string_view key) const {
  return *GetDataPtr(key);
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::GetData()
//    Returns a reference to the data associated with key in the trie, or
//    NullValuePolicy::Null() if key is not in the trie.
// ----------------------------------------------------------------------

template <class T, class NullValuePolicy>
T& GeneralTrieImpl<T, NullValuePolicy>::GetData(absl::string_view key) {
  return *const_cast<T*>(GetDataPtr(key));
}

template <class T, class NullValuePolicy>
const T* GeneralTrieImpl<T, NullValuePolicy>::GetDataPtr(
    absl::string_view key) const {
  const NodeT* node = this;
  int slen = key.length();
  int next_pos = 0;

  while (node) {
    // Return if we've reached the end of `key`.  Note that node->data_
    // may be NullValuePolicy::Null().
    if (next_pos >= slen) {
      return &(node->data_);
    }

    // Return if we don't match comppath.
    if (node->comppath_.size() >= key.length() - next_pos ||
        !absl::StartsWith(key.substr(next_pos), node->comppath_))
      return &null_value_instance_;        // we're done

    // follow first char after comppath
    next_pos += node->comppath_.size();
    //    std::cerr << "comp: " << node->comppath_ << std::endl;
    node = node->Next(key[next_pos]);
    //    std::cerr << next_pos << ": " << s[next_pos] << std::endl;
    next_pos++;
  }
  return &null_value_instance_;  // node is a null pointer; we're done
}

template <class T, class NullValuePolicy>
bool GeneralTrieImpl<T, NullValuePolicy>::SetData(absl::string_view key,
                                                  const T& data) {
  const T* temp_ptr = GetDataPtr(key);
  if (temp_ptr == &null_value_instance_) return false;
  T* non_const_temp_ptr = const_cast<T*>(temp_ptr);
  *non_const_temp_ptr = data;
  return true;
}

// ----------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::GetDataForMaximalPrefix()
//    Finds the greatest number n such that:
//      1. the first n characters of key form a string in the trie;
//      2. (n == key.length()) or is_terminator[key[n]] is true.
//    If there is no n that satisfies these conditions, the method returns
//    NullValuePolicy::Null().  Otherwise, it returns the data associated
//    with the first n characters of key and sets *chars_matched to n.
//    is_terminator should point to an array of 256 bools.  You can also pass
//    a null pointer for is_terminator, in which case the function treats every
//    character as a terminator, so condition 2 is always satisfied.
// ----------------------------------------------------------------------
template <class T, class NullValuePolicy>
const T& GeneralTrieImpl<T, NullValuePolicy>::GetDataForMaximalPrefix(
    absl::string_view key, int* chars_matched,
    const bool* is_terminator) const {
  const NodeT* node = this;
  int next_pos = 0;
  const T* matched_data = &null_value_instance_;

  while (node) {
    // See whether we have a match here
    if ((node->data_ != null_value_instance_) &&
        ((next_pos >= key.length()) || (is_terminator == nullptr) ||
         (is_terminator[key[next_pos]]))) {
      *chars_matched = next_pos;
      matched_data = &(node->data_);
    }

    // Return if we've reached the end of s
    if (next_pos >= key.length()) {
      return *matched_data;
    }

    // Return if we don't match comppath.
    if (node->comppath_.size() >= (key.length() - next_pos) ||
        !absl::StartsWith(key.substr(next_pos), node->comppath_))
      return *matched_data;  // we're done

    // follow first char after comppath
    next_pos += node->comppath_.size();
    node = node->Next(key[next_pos]);
    next_pos++;
  }
  return *matched_data;  // reached a node that is a null pointer; we're done
}

// ---------------------------------------------------------------------
// GeneralTrieImpl<T, NullValuePolicy>::GetAllMatchingStrings()
// Returns all matching strings and the associated data. If 's' does
// not match in its entirety, nothing is returned.
// ---------------------------------------------------------------------
template <class T, class NullValuePolicy>
class TrieExtractor : public GeneralTrieImpl<T, NullValuePolicy>::Traverser {
 public:
  typedef typename GeneralTrieImpl<T, NullValuePolicy>::TrieData TData;
  explicit TrieExtractor(std::vector<TData>* outdata) : outdata_(outdata) {}
  void Process(const std::string& s, const T& data) override {
    outdata_->push_back(std::make_pair(s, data));
  }

 private:
  std::vector<TData>* outdata_;
};

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::GetAllMatchingStrings(
    absl::string_view key, std::vector<TrieData>* outdata) const {
  // cleanup before we start
  outdata->clear();
  TrieExtractor<T, NullValuePolicy> traverser(outdata);
  PreorderTraverseAllMatchingStringsDepth(key, &traverser, -1);
}

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::TraverseAllMatchingStrings(
    absl::string_view key, Traverser* traverser, int depth,
    bool preorder) const {
  // first try to match the input string in its entirety
  const NodeT* node = this;
  int next_pos = 0;  // next position in s
  int brkpt = 0;     // next position in "node"

  // if we find a mismatch, we return emptyhanded.
  // if we find a match, we break out of the loop with
  // node set to the portion of the tree that has all
  // the matches for 's'.
  while (node) {
    if (next_pos >= key.length()) {
      // done with input string. Note: empty input string
      // matches everything in the trie. A break here means
      // brkpt == 0 which is what we want (the entire
      // comppath_ at this node is a suffix).
      break;
    }

    const int len_to_compare =
        std::min(node->comppath_.size(), (key.length() - next_pos));
    if (memcmp(node->comppath_.data(), key.data() + next_pos, len_to_compare) !=
        0) {
      // mismatch found
      return;
    }

    if ((key.length() - next_pos) <= node->comppath_.size()) {
      // found a match (prefix of comppath_)
      brkpt = len_to_compare;
      break;
    }

    // follow first char after comppath
    next_pos += len_to_compare;
    node = node->Next(key[next_pos]);
    ++next_pos;
  }

  // if we got here with node == nullptr, we have no matches.
  if (node == nullptr) return;

  // we got here => we have one or more matches
  std::string buf(key);  // all of the input string
  if (node->data_ != null_value_instance_ && brkpt == 0 && preorder) {
    // this node is a full match by itself
    traverser->Process(buf, node->data_);
  }

  buf.append(node->comppath_.data() + brkpt, node->comppath_.size() - brkpt);
  for (int i = node->min_next_; i < node->max_next_; i++) {
    NodeT* child = node->Next(i);

    if (child != nullptr) {
      buf.append(1, static_cast<char>(i));
      child->Traverse(traverser, &buf, depth, preorder);
      buf.erase(buf.size() - 1);
    }
  }
  if (node->data_ != null_value_instance_ && brkpt == 0 && !preorder) {
    // this node is a full match by itself
    traverser->Process(buf, node->data_);
  }
}

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::Traverse(Traverser* traverser,
                                                   std::string* s, int depth,
                                                   bool preorder) const {
  if (data_ != null_value_instance_ && preorder) {
    traverser->Process(*s, data_);
  }

  if (depth == 0) return;
  if (depth > 0) --depth;

  s->append(comppath_);
  for (int i = min_next_; i < max_next_; i++) {
    NodeT* child = Next(i);
    if (child != nullptr) {
      s->append(1, static_cast<char>(i));
      child->Traverse(traverser, s, depth, preorder);
      s->erase(s->size() - 1);
    }
  }
  s->erase(s->size() - comppath_.size());
  if (data_ != null_value_instance_ && !preorder) {
    traverser->Process(*s, data_);
  }
}

template <class T, class NullValuePolicy>
GeneralTrieImpl<T, NullValuePolicy>::TraverseIterator::TraverseIterator(
    const GeneralTrieImpl<T, NullValuePolicy>* trie) {
  stack_.push_back(std::make_pair(trie, trie->min_next_));
  if (trie->data_ == trie->null_value_instance_) {
    Next();
  }
}

template <class T, class NullValuePolicy>
void GeneralTrieImpl<T, NullValuePolicy>::TraverseIterator::Next() {
  while (!stack_.empty()) {
    const NodeT* node = stack_.back().first;
    int c = stack_.back().second;
    if (c == node->min_next_) {
      key_.append(node->comppath_);
    }

    // Traverse the next branch if there is one.
    for (; c < node->max_next_; ++c) {
      const NodeT* child = node->Next(c);
      if (child) {
        key_.append(1, static_cast<char>(c));
        stack_.back().second = c + 1;
        stack_.push_back(std::make_pair(child, child->min_next_));
        if (child->data_ != child->null_value_instance_) {
          return;
        }
        // Continue with the top-level loop and process the child node.
        break;
      }
    }

    if (c == node->max_next_) {
      // Leaving the node, so unwind key and stack.
      key_.erase(key_.size() - node->comppath_.size());
      stack_.pop_back();
      if (!stack_.empty()) {
        key_.erase(key_.size() - 1);
      }
    }
  }
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE__GENERAL_TRIE_H_
