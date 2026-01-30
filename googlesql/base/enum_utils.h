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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENUM_UTILS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENUM_UTILS_H_

// Provides utility functions that help with handling Protocol Buffer enums.
//
// Examples:
//
// A function to easily iterate over all defined values of an enum known at
// compile-time:
//
// for (Proto::Enum e : EnumerateEnumValues<Proto::Enum>()) {
//    ...
// }
//

#include "google/protobuf/descriptor.pb.h"

namespace googlesql_base {

template <typename E>
class ProtoEnumIterator;

template <typename E>
class EnumeratedProtoEnumView;

template <typename E>
bool operator==(const ProtoEnumIterator<E>& a, const ProtoEnumIterator<E>& b);

template <typename E>
bool operator!=(const ProtoEnumIterator<E>& a, const ProtoEnumIterator<E>& b);

// Generic Proto enum iterator.
template <typename E>
class ProtoEnumIterator {
 public:
  typedef E value_type;
  typedef std::input_iterator_tag iterator_category;
  typedef int difference_type;
  typedef E* pointer;
  typedef E& reference;

  ProtoEnumIterator& operator++() {
    ++current_;
    return *this;
  }

  E operator*() const {
    return static_cast<E>(
        google::protobuf::GetEnumDescriptor<E>()->value(current_)->number());
  }

 private:
  explicit ProtoEnumIterator(int current) : current_(current) {}

  int current_;

  // Only EnumeratedProtoEnumView can instantiate ProtoEnumIterator.
  friend class EnumeratedProtoEnumView<E>;
  friend bool operator==
      <>(const ProtoEnumIterator<E>& a, const ProtoEnumIterator<E>& b);
  friend bool operator!=
      <>(const ProtoEnumIterator<E>& a, const ProtoEnumIterator<E>& b);
};

template <typename E>
bool operator==(const ProtoEnumIterator<E>& a,
                const ProtoEnumIterator<E>& b) {
  return a.current_ == b.current_;
}

template <typename E>
bool operator!=(const ProtoEnumIterator<E>& a,
                const ProtoEnumIterator<E>& b) {
  return a.current_ != b.current_;
}

template <typename E>
class EnumeratedProtoEnumView {
 public:
  typedef E value_type;
  typedef ProtoEnumIterator<E> iterator;
  iterator begin() const { return iterator(0); }
  iterator end() const {
    return iterator(google::protobuf::GetEnumDescriptor<E>()->value_count());
  }
};

// Returns an EnumeratedProtoEnumView that can be iterated over:
// for (Proto::Enum e : EnumerateEnumValues<Proto::Enum>()) {
//    ...
// }
template <typename E>
EnumeratedProtoEnumView<E> EnumerateEnumValues() {
  return EnumeratedProtoEnumView<E>();
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENUM_UTILS_H_
