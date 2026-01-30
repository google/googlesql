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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_H_

// This file provides utility functions for use with STL map-like data
// structures, such as std::map and hash_map. Some functions will also work with
// sets, such as ContainsKey().
//
// The main functions in this file fall into the following categories:
//
// - Find*()
// - Contains*()
// - Insert*()
// - Lookup*()
//
// These functions often have "...OrDie" or "...OrDieNoPrint" variants. These
// variants will crash the process with a ABSL_CHECK() failure on error, including
// the offending key/data in the log message. The NoPrint variants will not
// include the key/data in the log output under the assumption that it's not a
// printable type.
//
// Most functions are fairly self explanatory from their names, with the
// exception of Find*() vs Lookup*(). The Find functions typically use the map's
// .find() member function to locate and return the map's value type. The
// Lookup*() functions typically use the map's .insert() (yes, insert) member
// function to insert the given value if necessary and returns (usually a
// reference to) the map's value type for the found item.
//
// See the per-function comments for specifics.
//
// There are also a handful of functions for doing other miscellaneous things.
//
// A note on terminology:
//
// In this file, `m` and `M` represent a map and its type.
//
// Map-like containers are collections of pairs. Like all STL containers they
// contain a few standard typedefs identifying the types of data they contain.
// Given the following map declaration:
//
//   std::map<string, int> my_map;
//
// the notable typedefs would be as follows:
//
//   - key_type    -- string
//   - value_type  -- std::pair<const string, int>
//   - mapped_type -- int
//
// Note that the map above contains two types of "values": the key-value pairs
// themselves (value_type) and the values within the key-value pairs
// (mapped_type). A value_type consists of a key_type and a mapped_type.
//
// The documentation below is written for programmers thinking in terms of keys
// and the (mapped_type) values associated with a given key.  For example, the
// statement
//
//   my_map["foo"] = 3;
//
// has a key of "foo" (type: string) with a value of 3 (type: int).
//

#include <stddef.h>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/meta/type_traits.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/map_traits.h"
#include "googlesql/base/no_destructor.h"

namespace googlesql_base {

// These helper template aliases are implementation details of map_util,
// provided for notational convenience. Despite the file-level documentation
// about map typedefs, map_util doesn't actually use them.
// It uses the Map's value_type, and the value_type's first_type and
// second_type. This can matter for nonconformant containers.
template <typename M>
using MapUtilValueT = typename M::value_type;
template <typename M>
using MapUtilKeyT = typename MapUtilValueT<M>::first_type;
template <typename M>
using MapUtilMappedT = typename MapUtilValueT<M>::second_type;

namespace internal_map_util {

template <typename M, typename = void>
struct HasTryEmplace : std::false_type {};

template <typename M>
struct HasTryEmplace<M, absl::void_t<decltype(std::declval<M>().try_emplace(
                            std::declval<const MapUtilKeyT<M>&>()))>>
    : std::true_type {};

template <typename M, typename = void>
struct InitType {
  using type = MapUtilValueT<M>;
};

template <typename M>
struct InitType<M, typename std::enable_if<!std::is_convertible<
                       typename M::init_type, MapUtilValueT<M>>::value>::type> {
  using type = typename M::init_type;
};

template <typename V>
const V& ValueInitializedDefault() {
  static const googlesql_base::NoDestructor<V> value_initialized_default{};
  return *value_initialized_default;
}

}  // namespace internal_map_util

template <typename M>
using MapUtilInitT = typename internal_map_util::InitType<M>::type;

//
// Find*()
//

// Returns a const reference to the value associated with the given key if it
// exists. Crashes otherwise.
//
// This is intended as a replacement for operator[] as an rvalue (for reading)
// when the key is guaranteed to exist.
//
// operator[] for lookup is discouraged for several reasons (note that these
// reasons may apply to only some map types):
//  * It has a side-effect of inserting missing keys
//  * It is not thread-safe (even when it is not inserting, it can still
//      choose to resize the underlying storage)
//  * It invalidates iterators (when it chooses to resize)
//  * It default constructs a value object even if it doesn't need to
//
// This version assumes the key is printable, and includes it in the fatal log
// message.
template <typename M, typename KeyType = MapUtilKeyT<M>>
const MapUtilMappedT<M>& FindOrDie(const M& m, const KeyType& key) {
  auto it = m.find(key);
  ABSL_CHECK(it != m.end()) << "Map key not found: " << key;
  return googlesql_base::subtle::GetMapped(*it);
}

// Same as above, but returns a non-const reference.
template <typename M, typename KeyType = MapUtilKeyT<M>>
MapUtilMappedT<M>& FindOrDie(M& m,  // NOLINT
                             const KeyType& key) {
  auto it = m.find(key);
  ABSL_CHECK(it != m.end()) << "Map key not found: " << key;
  return googlesql_base::subtle::GetMapped(*it);
}

// Same as FindOrDie above, but doesn't log the key on failure.
template <typename M, typename KeyType = MapUtilKeyT<M>>
const MapUtilMappedT<M>& FindOrDieNoPrint(const M& m, const KeyType& key) {
  auto it = m.find(key);
  ABSL_CHECK(it != m.end()) << "Map key not found";
  return googlesql_base::subtle::GetMapped(*it);
}

// Same as above, but returns a non-const reference.
template <typename M, typename KeyType = MapUtilKeyT<M>>
MapUtilMappedT<M>& FindOrDieNoPrint(M& m,  // NOLINT
                                    const KeyType& key) {
  auto it = m.find(key);
  ABSL_CHECK(it != m.end()) << "Map key not found";
  return googlesql_base::subtle::GetMapped(*it);
}

// Returns a const reference to the value associated with the given key if it
// exists, otherwise returns a const reference to a value-initialized object
// that is never destroyed.
template <typename M, typename KeyType = MapUtilKeyT<M>>
const MapUtilMappedT<M>& FindWithDefault(const M& m, const KeyType& key) {
  auto it = m.find(key);
  if (it != m.end()) return googlesql_base::subtle::GetMapped(*it);
  return internal_map_util::ValueInitializedDefault<MapUtilMappedT<M>>();
}

// Returns a const reference to the value associated with the given key if it
// exists, otherwise returns a const reference to the provided default value.
//
// Prefer the two-argument form unless you need to specify a custom default
// value (i.e., one that is not equal to a value-initialized instance).
//
// WARNING: If a temporary object is passed as the default "value,"
// this function will return a reference to that temporary object,
// which will be destroyed at the end of the statement. A common
// example: if you have a map with string values, and you pass a char*
// as the default "value," either use the returned value immediately
// or store it in a string (not string&).
//
// TODO: Stop using this.
template <typename M>
const MapUtilMappedT<M>& FindWithDefault(const M& m, const MapUtilKeyT<M>& key,
                                         const MapUtilMappedT<M>& value) {
  auto it = m.find(key);
  if (it != m.end()) return googlesql_base::subtle::GetMapped(*it);
  return value;
}

// Returns a pointer to the const value associated with the given key if it
// exists, or null otherwise.
template <typename M, typename KeyType = MapUtilKeyT<M>>
const MapUtilMappedT<M>* FindOrNull(const M& m, const KeyType& key) {
  auto it = m.find(key);
  if (it == m.end()) return nullptr;
  return &googlesql_base::subtle::GetMapped(*it);
}

// Returns a pointer to the non-const value associated with the given key if it
// exists, or null otherwise.
template <typename M, typename KeyType = MapUtilKeyT<M>>
MapUtilMappedT<M>* FindOrNull(M& m,  // NOLINT
                              const KeyType& key) {
  auto it = m.find(key);
  if (it == m.end()) return nullptr;
  return &googlesql_base::subtle::GetMapped(*it);
}

// Returns the pointer value associated with the given key. If none is found,
// null is returned. The function is designed to be used with a map of keys
// to pointers.
//
// This function does not distinguish between a missing key and a key mapped
// to a null value.
template <typename M, typename KeyType = MapUtilKeyT<M>>
MapUtilMappedT<M> FindPtrOrNull(const M& m, const KeyType& key) {
  auto it = m.find(key);
  if (it == m.end()) return MapUtilMappedT<M>();
  return googlesql_base::subtle::GetMapped(*it);
}

// Same as above, except takes non-const reference to m.
//
// This function is needed for containers that propagate constness to the
// pointee, such as boost::ptr_map.
template <typename M, typename KeyType = MapUtilKeyT<M>>
MapUtilMappedT<M> FindPtrOrNull(M& m,  // NOLINT
                                const KeyType& key) {
  auto it = m.find(key);
  if (it == m.end()) return MapUtilMappedT<M>();
  return googlesql_base::subtle::GetMapped(*it);
}

// Finds the value associated with the given key and copies it to *value (if
// non-null). Returns false if the key was not found, true otherwise.
template <typename M, typename Key, typename Value>
bool FindCopy(const M& m, const Key& key, Value* value) {
  auto it = m.find(key);
  if (it == m.end()) return false;
  if (value) *value = googlesql_base::subtle::GetMapped(*it);
  return true;
}

//
// Contains*()
//

// Returns true if and only if the given m contains the given key.
template <typename M, typename Key>
bool ContainsKey(const M& m, const Key& key) {
  return m.find(key) != m.end();
}

// Returns true if and only if the given m contains the given key-value
// pair.
template <typename M, typename Key, typename Value>
bool ContainsKeyValuePair(const M& m, const Key& key, const Value& value) {
  auto range = m.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (googlesql_base::subtle::GetMapped(*it) == value) {
      return true;
    }
  }
  return false;
}

//
// Insert*()
//

// Inserts the given key-value pair into the m. Returns true if and
// only if the key from the given pair didn't previously exist. Otherwise, the
// value in the map is replaced with the value from the given pair.
template <typename M>
bool InsertOrUpdate(M* m, const MapUtilInitT<M>& vt) {
  auto ret = m->insert(vt);
  if (ret.second) return true;
  subtle::GetMapped(*ret.first) = subtle::GetMapped(vt);  // update
  return false;
}

// Same as above, except that the key and value are passed separately.
template <typename M>
bool InsertOrUpdate(M* m, const MapUtilKeyT<M>& key,
                    const MapUtilMappedT<M>& value) {
  return InsertOrUpdate(m, {key, value});
}

// Inserts/updates all the key-value pairs from the range defined by the
// iterators "first" and "last" into the given m.
template <typename M, typename InputIterator>
void InsertOrUpdateMany(M* m, InputIterator first, InputIterator last) {
  for (; first != last; ++first) {
    InsertOrUpdate(m, *first);
  }
}

// Change the value associated with a particular key in a map or hash_map
// of the form std::map<Key, Value*> which owns the objects pointed to by the
// value pointers.  If there was an existing value for the key, it is deleted.
// True indicates an insert took place, false indicates an update + delete.
template <typename M>
bool InsertAndDeleteExisting(M* m, const MapUtilKeyT<M>& key,
                             const MapUtilMappedT<M>& value) {
  auto ret = m->insert(MapUtilValueT<M>(key, value));
  if (ret.second) return true;
  delete ret.first->second;
  ret.first->second = value;
  return false;
}

// Inserts the given key and value into the given m if and only if the
// given key did NOT already exist in the m. If the key previously
// existed in the m, the value is not changed. Returns true if the
// key-value pair was inserted; returns false if the key was already present.
template <typename M>
bool InsertIfNotPresent(M* m, const MapUtilInitT<M>& vt) {
  return m->insert(vt).second;
}

// Same as above except the key and value are passed separately.
template <typename M>
bool InsertIfNotPresent(M* m, const MapUtilKeyT<M>& key,
                        const MapUtilMappedT<M>& value) {
  return InsertIfNotPresent(m, {key, value});
}

// Same as above except dies if the key already exists in the m.
template <typename M>
void InsertOrDie(M* m, const MapUtilInitT<M>& value) {
  ABSL_CHECK(InsertIfNotPresent(m, value)) << "duplicate value: " << value;
}

// Same as above except doesn't log the value on error.
template <typename M>
void InsertOrDieNoPrint(M* m, const MapUtilInitT<M>& value) {
  ABSL_CHECK(InsertIfNotPresent(m, value)) << "duplicate value.";
}

// Inserts the key-value pair into the m. Dies if key was already
// present.
template <typename M>
void InsertOrDie(M* m, const MapUtilKeyT<M>& key,
                 const MapUtilMappedT<M>& data) {
  ABSL_CHECK(InsertIfNotPresent(m, key, data)) << "duplicate key: " << key;
}

// Same as above except doesn't log the key on error.
template <typename M>
void InsertOrDieNoPrint(M* m, const MapUtilKeyT<M>& key,
                        const MapUtilMappedT<M>& data) {
  ABSL_CHECK(InsertIfNotPresent(m, key, data)) << "duplicate key.";
}

// Inserts a new key and default-initialized value. Dies if the key was already
// present. Returns a reference to the value. Example usage:
//
// std::map<int, SomeProto> m;
// SomeProto& proto = InsertKeyOrDie(&m, 3);
// proto.set_field("foo");
template <typename M>
auto InsertKeyOrDie(M* m, const MapUtilKeyT<M>& key) ->
    typename std::enable_if<internal_map_util::HasTryEmplace<M>::value,
                            MapUtilMappedT<M>&>::type {
  auto res = m->try_emplace(key);
  ABSL_CHECK(res.second) << "duplicate key: " << key;
  return googlesql_base::subtle::GetMapped(*res.first);
}

// Anything without try_emplace, we support with the legacy code path.
template <typename M>
auto InsertKeyOrDie(M* m, const MapUtilKeyT<M>& key) ->
    typename std::enable_if<!internal_map_util::HasTryEmplace<M>::value,
                            MapUtilMappedT<M>&>::type {
  auto res = m->insert(MapUtilValueT<M>(key, MapUtilMappedT<M>()));
  ABSL_CHECK(res.second) << "duplicate key: " << key;
  return res.first->second;
}


//
// Lookup*()
//

// Looks up a given key and value pair in m and inserts the key-value pair if
// it's not already present. Returns a reference to the value associated with
// the key.
template <typename M>
MapUtilMappedT<M>& LookupOrInsert(M* m, const MapUtilInitT<M>& vt) {
  return subtle::GetMapped(*m->insert(vt).first);
}

// Same as above except the key-value are passed separately.
template <typename M>
MapUtilMappedT<M>& LookupOrInsert(M* m, const MapUtilKeyT<M>& key,
                                  const MapUtilMappedT<M>& value) {
  return LookupOrInsert(m, {key, value});
}

// Returns a reference to the pointer associated with key. If not found, a
// pointee is constructed and added to the map. In that case, the new pointee is
// forwarded constructor arguments; when no arguments are provided the default
// constructor is used.
//
// Useful for containers of the form Map<Key, Ptr>, where Ptr is pointer-like.
template <typename M, class... Args>
MapUtilMappedT<M>& LookupOrInsertNew(M* m, const MapUtilKeyT<M>& key,
                                     Args&&... args) {
  using Mapped = MapUtilMappedT<M>;
  using MappedDeref = decltype(*std::declval<Mapped>());
  using Element = typename std::decay<MappedDeref>::type;
  auto ret = m->insert(MapUtilValueT<M>(key, Mapped()));
  if (ret.second) {
    ret.first->second = Mapped(new Element(std::forward<Args>(args)...));
  }
  return ret.first->second;
}

//
// Misc Utility Functions
//

// Updates the value associated with the given key. If the key was not already
// present, then the key-value pair are inserted and "previous" is unchanged. If
// the key was already present, the value is updated and "*previous" will
// contain a copy of the old value.
//
// Returns true if and only if there was an already existing value.
//
// InsertOrReturnExisting has complementary behavior that returns the
// address of an already existing value, rather than updating it.

template <typename M>
bool UpdateReturnCopy(M* m, const MapUtilValueT<M>& vt,
                      MapUtilMappedT<M>* previous) {
  auto ret = m->insert(vt);
  if (ret.second) return false;
  if (previous) *previous = ret.first->second;
  ret.first->second = vt.second;  // update
  return true;
}

// Same as above except that the key and mapped value are passed separately.
template <typename M>
bool UpdateReturnCopy(M* m, const MapUtilKeyT<M>& key,
                      const MapUtilMappedT<M>& value,
                      MapUtilMappedT<M>* previous) {
  return UpdateReturnCopy(m, MapUtilValueT<M>(key, value), previous);
}

// Tries to insert the given key-value pair into the m. Returns null
// if the insert succeeds. Otherwise, returns a pointer to the existing value.
//
// This complements UpdateReturnCopy in that it allows to update only after
// verifying the old value and still insert quickly without having to look up
// twice. Unlike UpdateReturnCopy this also does not come with the issue of an
// undefined previous* in case new data was inserted.
template <typename M>
MapUtilMappedT<M>* InsertOrReturnExisting(M* m, const MapUtilValueT<M>& vt) {
  auto ret = m->insert(vt);
  if (ret.second) return nullptr;  // Inserted, no previous value.
  return &ret.first->second;       // Return address of previous value.
}

// Same as above, except for explicit key and data.
template <typename M>
MapUtilMappedT<M>* InsertOrReturnExisting(M* m, const MapUtilKeyT<M>& key,
                                          const MapUtilMappedT<M>& data) {
  return InsertOrReturnExisting(m, MapUtilValueT<M>(key, data));
}

// Saves the reverse mapping into reverse. Returns true if values could all be
// inserted.
template <typename M, typename ReverseM>
bool ReverseMap(const M& m, ReverseM* reverse) {
  ABSL_CHECK(reverse != nullptr);
  bool all_unique = true;
  for (const auto& kv : m) {
    if (!InsertOrUpdate(reverse, kv.second, kv.first)) {
      all_unique = false;
    }
  }
  return all_unique;
}

// Like ReverseMap above, but returns its output m. Return type has to
// be specified explicitly. Example:
// M::M(...) : m_(...), r_(ReverseMap<decltype(r_)>(m_)) {}
template <typename ReverseM, typename M>
ReverseM ReverseMap(const M& m) {
  typename std::remove_const<ReverseM>::type reverse;
  ReverseMap(m, &reverse);
  return reverse;
}

// Erases the m item identified by the given key, and returns the value
// associated with that key. It is assumed that the value (i.e., the
// mapped_type) is a pointer. Returns null if the key was not found in the
// m.
//
// Examples:
//   std::map<string, MyType*> my_map;
//
// One line cleanup:
//     delete EraseKeyReturnValuePtr(&my_map, "abc");
//
// Use returned value:
//     std::unique_ptr<MyType> value_ptr(
//         EraseKeyReturnValuePtr(&my_map, "abc"));
//     if (value_ptr.get())
//       value_ptr->DoSomething();
//
template <typename M>
MapUtilMappedT<M> EraseKeyReturnValuePtr(M* m, const MapUtilKeyT<M>& key) {
  auto it = m->find(key);
  if (it == m->end()) return nullptr;
  MapUtilMappedT<M> v = std::move(googlesql_base::subtle::GetMapped(*it));
  m->erase(it);
  return v;
}

// Inserts all the keys from m into key_container, which must
// support insert(M::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <typename M, typename KeyContainer>
void InsertKeysFromMap(const M& m, KeyContainer* key_container) {
  ABSL_CHECK(key_container != nullptr);
  for (const auto& kv : m) {
    key_container->insert(kv.first);
  }
}

// Appends all the keys from m into key_container, which must
// support push_back(M::key_type).
//
// Note: any initial contents of the key_container are not cleared.
template <typename M, typename KeyContainer>
void AppendKeysFromMap(const M& m, KeyContainer* key_container) {
  ABSL_CHECK(key_container != nullptr);
  for (const auto& kv : m) {
    key_container->push_back(kv.first);
  }
}

// A more specialized overload of AppendKeysFromMap to optimize reallocations
// for the common case in which we're appending keys to a vector and hence can
// (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// m that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <typename M, typename KeyType>
void AppendKeysFromMap(const M& m, std::vector<KeyType>* key_container) {
  ABSL_CHECK(key_container != nullptr);
  // We now have the opportunity to call reserve(). Calling reserve() every
  // time is a bad idea for some use cases: libstdc++'s implementation of
  // std::vector<>::reserve() resizes the vector's backing store to exactly the
  // given size (unless it's already at least that big). Because of this,
  // the use case that involves appending a lot of small maps (total size
  // N) one by one to a vector would be O(N^2). But never calling reserve()
  // loses the opportunity to improve the use case of adding from a large
  // map to an empty vector (this improves performance by up to 33%). A
  // number of heuristics are possible. Here we use the simplest one.
  if (key_container->empty()) {
    key_container->reserve(m.size());
  }
  for (const auto& kv : m) {
    key_container->push_back(kv.first);
  }
}

// Inserts all the values from m into value_container, which must
// support push_back(M::mapped_type).
//
// Note: any initial contents of the value_container are not cleared.
template <typename M, typename ValueContainer>
void AppendValuesFromMap(const M& m, ValueContainer* value_container) {
  ABSL_CHECK(value_container != nullptr);
  for (const auto& kv : m) {
    value_container->push_back(kv.second);
  }
}

// A more specialized overload of AppendValuesFromMap to optimize reallocations
// for the common case in which we're appending values to a vector and hence
// can (and sometimes should) call reserve() first.
//
// (It would be possible to play SFINAE games to call reserve() for any
// m that supports it, but this seems to get us 99% of what we need
// without the complexity of a SFINAE-based solution.)
template <typename M, typename ValueType>
void AppendValuesFromMap(const M& m, std::vector<ValueType>* value_container) {
  ABSL_CHECK(value_container != nullptr);
  // See AppendKeysFromMap for why this is done.
  if (value_container->empty()) {
    value_container->reserve(m.size());
  }
  for (const auto& kv : m) {
    value_container->push_back(kv.second);
  }
}

// Erases all elements of m where predicate evaluates to true.
// Note: To avoid unnecessary temporary copies of map elements passed to the
// predicate, the predicate must accept 'const M::value_type&'.
// In particular, the value type for a map is 'std::pair<const K, V>', and so a
// predicate accepting 'std::pair<K, V>' will result in temporary copies.
template <typename M, typename Predicate>
auto AssociativeEraseIf(M* m, Predicate predicate) -> typename std::enable_if<
    std::is_same<void, decltype(m->erase(m->begin()))>::value>::type {
  ABSL_CHECK(m != nullptr);
  for (auto it = m->begin(); it != m->end();) {
    if (predicate(*it)) {
      m->erase(it++);
    } else {
      ++it;
    }
  }
}

template <typename M, typename Predicate>
auto AssociativeEraseIf(M* m, Predicate predicate) ->
    typename std::enable_if<std::is_same<
        decltype(m->begin()), decltype(m->erase(m->begin()))>::value>::type {
  ABSL_CHECK(m != nullptr);
  for (auto it = m->begin(); it != m->end();) {
    if (predicate(*it)) {
      it = m->erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_H_
