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

#ifndef GOOGLESQL_PUBLIC_CYCLE_DETECTOR_H_
#define GOOGLESQL_PUBLIC_CYCLE_DETECTOR_H_

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "googlesql/base/status.h"

namespace googlesql {

// CycleDetector logically maintains a stack of object references, where
// objects are pushed onto the stack if they do not create a cycle, and
// objects are popped from the stack when they are destructed.
//
// CycleDetector ObjectInfos are RAII - they remove themselves from the
// related cycle detector upon destruction.
//
// Example usage:
//
// CycleDetector cycle_detector;
// {
//   const void* object = ...
//   CycleDetector::ObjectInfo info("object_name", object, &cycle_detector);
//   GOOGLESQL_RETURN_IF_ERROR(info.DetectCycle("object_type_name"));
//   ...
// }  // 'info' removed from 'cycle_detector' upon destruction
//
// Note that each distinct object will be added to the cycle detector at
// most once, so any object with (cycle_detected() == true) will not be
// added.
//
// Not thread-safe.
class CycleDetector {
 public:
  CycleDetector() = default;

  class ObjectInfo {
   public:
    // Object information for cycle detection, including its <name> and a
    // pointer to the <object>, where <object> is used as a unique identifier
    // for detecting cycles.  Construction pushes the ObjectInfo onto the
    // <cycle_detector> if not already present.  The caller can determine
    // if the ObjectInfo introduces a cycle by calling cycle_detected().
    // Does not take ownership of <object> or <cycle_detector>, and
    // <cycle_detector> must outlive this ObjectInfo.
    ObjectInfo(absl::string_view name, const void* object,
               CycleDetector* cycle_detector);
    ObjectInfo(const ObjectInfo&) = delete;
    ObjectInfo& operator=(const ObjectInfo&) = delete;
    ~ObjectInfo();

    // Returns an error if the creation of this object introduced a cycle,
    // otherwise returns OK.  The <object_type> is used in the construction
    // of the returned error message, if applicable.
    absl::Status DetectCycle(absl::string_view object_type) const;

    std::string name() const { return name_; }
    const void* object() const { return object_; }

   private:
    // Returns true if pushing this object onto the <cycle_detector> would
    // create a cycle.  Otherwise returns false, and pushes this object onto
    // <cycle_detector> and keeps a pointer to <cycle_detector> so that
    // this object can pop itself from <cycle_detector> upon destruction.
    // Does not own <cycle_detector>, and <cycle_detector> must outlive this
    // object.
    ABSL_MUST_USE_RESULT bool DetectCycleOrAddToCycleDetector(
        CycleDetector* cycle_detector);

    const std::string name_;
    const void* object_ = nullptr;
    bool cycle_detected_ = true;

    // Identifies the CycleDetector that this object was created for.
    CycleDetector* cycle_detector_ = nullptr;
  };

  // Returns whether or not the CycleDetector is empty.
  ABSL_MUST_USE_RESULT bool IsEmpty() const { return object_deque_.empty(); }

  // Returns the number of objects the CycleDetector is tracking.
  int32_t Size() const { return object_deque_.size(); }

  // Returns the deque of objects in the CycleDetector, where the deque
  // order matches the order in which the objects were added to the
  // cycle detector.
  const std::deque<const ObjectInfo*>& object_deque() const {
    return object_deque_;
  }

  // Returns a list of object names in the CycleDetector, where the list
  // order matches the order in which the objects were added to the
  // cycle detector.
  std::vector<std::string> ObjectNames() const;

  std::string DebugString() const;

 private:
  // Detects if a cycle exists given the new object.  If the new object does
  // not introduce a cycle, then pushes the object onto the cycle detector.
  // Returns true if a cycle is detected for the given <object_info>,
  // in which case <object_info> is not added to the cycle detector.
  // Otherwise returns OK.
  // Does not take ownership of <object_info>.
  ABSL_MUST_USE_RESULT bool DetectCycleOrPushObject(
      const ObjectInfo* object_info);

  // Pops an object from the cycle detector. In non-production code, the
  // <expected_object_info> is checked against the popped object.
  void PopObject(const ObjectInfo* expected_object_info);

  // Stack of objects currently being resolved (with the implementation
  // detail that a deque is used).  Does not own the ObjectInfos.
  std::deque<const ObjectInfo*> object_deque_;

  // Comparison function for ObjectInfo.
  struct ObjectInfoLess {
    bool operator()(const CycleDetector::ObjectInfo* object1,
                    const CycleDetector::ObjectInfo* object2) const {
      return object1->object() < object2->object();
    }
  };

  // Set of objects currently being tracked, to provide fast cycle detection
  // after inserting a new element into the <object_deque_>.  Does not own
  // ObjectInfos.
  std::set<const ObjectInfo*, ObjectInfoLess> objects_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_CYCLE_DETECTOR_H_
