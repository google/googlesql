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
#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_VARSETTER_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_VARSETTER_H_

namespace googlesql_base {
//
// Use a VarSetter object to temporarily set an object of some sort to
// a particular value.  When the VarSetter object is destructed, the
// underlying object will revert to its former value.
//
// Sample code:
//  void foo() {
//    bool b = true;
//    {
//      VarSetter<bool> bool_setter(&b, false);
//      // Now b == false.
//    }
//    // Now b == true again.
//  }
template <typename T>
class VarSetter {
 public:
  // Constructor that sets the object to a fixed value.
  VarSetter(T* object, const T& value) : object_(object), old_value_(*object) {
    *object = value;
  }
  VarSetter(const VarSetter&) = delete;
  VarSetter& operator=(const VarSetter&) = delete;

  ~VarSetter() { *object_ = old_value_; }

 private:
  T* const object_;
  T old_value_;
};

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_VARSETTER_H_
