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

#ifndef GOOGLESQL_PUBLIC_VALUE_CONTENT_H_
#define GOOGLESQL_PUBLIC_VALUE_CONTENT_H_

#include <cstdint>
#include <type_traits>


namespace googlesql {

class SimpleType;

// ValueContent class is a typeless represention of googlesql::Value content
// (Value, in comparison, also stores a reference to googlesql::Type instance):
// 1) ValueContent is not stored in Value directly, but serves as a value's
//  content exchange container in a contract between Value and Type for
//  conveying value content from Type to Value class. This helps to decouple
//  Type and Value: ValueContent doesn't depend on either of them and thus
//  allows avoiding circular-dependency between Value (which references Type)
//  and Type (which defines Value content).
// 2) Value class stores value's content within its space together with other
//  Value's fields (like, type identifier) using different layouts depending on
//  whether Value belongs to simple type or not. Simple types are those builtin
//  types that can be represented with just a TypeKind, with no parameters (in
//  GoogleSQL type's classes hierarchy, simple types are represented by
//  SimpleType class).
// 3) This class should only be used inside Value and inside Type subclasses.
// 4) ValueContent provides 8 bytes of value's content.
// 5) For builtin simple types, an extra four bytes of storage is available in
//  Value because the type is stored as an TypeKind enum rather than a Type*
//  pointer.  This extra storage (in simple_type_extended_content_) is usable
//  only in SimpleType.
class ValueContent {
 private:
  template <typename T>
  struct Storage {
    union {
      int64_t base_value;
      T value;
    };
  };

 public:
  // Explicitly copyable and assignable.
  constexpr ValueContent(const ValueContent& other) = default;
  constexpr ValueContent& operator=(const ValueContent& other) = default;

  template <class T>
  static constexpr bool IsTypeSupported() {
    return std::is_trivially_copyable<T>::value &&
           sizeof(Storage<T>) == sizeof(content_);
  }

  template <class T>
  constexpr std::enable_if_t<IsTypeSupported<T>(), void> set(T value) {
    content<T>()->value = value;
  }

  template <class T>
  constexpr std::enable_if_t<IsTypeSupported<T>(), T> GetAs() const {
    return content<T>()->value;
  }

  // Creates a content that stores the given value.
  template <class T>
  constexpr static std::enable_if_t<IsTypeSupported<T>(), ValueContent> Create(
      T value) {
    ValueContent result(/*value=*/0, /*extended_value=*/0);
    result.set(value);
    return result;
  }

 private:
  friend class SimpleType;
  friend class Value;

  template <typename T>
  constexpr Storage<T>* content() {
    static_assert(IsTypeSupported<T>());
    return reinterpret_cast<Storage<T>*>(&content_);
  }

  template <typename T>
  constexpr const Storage<T>* content() const {
    static_assert(IsTypeSupported<T>());
    return reinterpret_cast<const Storage<T>*>(&content_);
  }

  constexpr explicit ValueContent(int64_t value = 0, int32_t extended_value = 0)
      : content_(value), simple_type_extended_content_(extended_value) {}

  // Main content of the value that all types use
  int64_t content_;

  // Field below can be used only by simple types (SimpleType).
  int32_t simple_type_extended_content_;
};

static_assert(sizeof(ValueContent) <= 16);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_VALUE_CONTENT_H_
