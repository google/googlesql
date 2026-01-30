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

#include "googlesql/public/cycle_detector.h"

#include <cstdint>
#include <deque>
#include <string>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace googlesql {

class CycleDetectorTest : public ::testing::Test {
 public:
  CycleDetectorTest() = default;
  ~CycleDetectorTest() override = default;

  const void* object1() const { return &object1_; }
  const void* object2() const { return &object2_; }
  const void* object3() const { return &object3_; }
  const void* object4() const { return &object4_; }

  CycleDetector* cycle_detector() {
    return &cycle_detector_;
  }

  static inline void ObjectInfoNameFormatter(
      std::string* out, const CycleDetector::ObjectInfo* info) {
    absl::StrAppend(out, info->name());
  }

  std::string ObjectNames(
      const std::deque<const CycleDetector::ObjectInfo*>& object_deque) {
    return absl::StrJoin(object_deque, ", ", ObjectInfoNameFormatter);
  }

 private:
  CycleDetector cycle_detector_;

  int32_t object1_;
  int64_t object2_;
  std::string object3_;
  bool object4_;
};

TEST_F(CycleDetectorTest, CycleDetectorTests) {
  {
    CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());
  }

  {
    // Add same object again, this is ok since the other instance of this
    // object was removed upon destruction.
    CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());
  }

  {
    // Add same object again twice.  The first time this is ok.
    CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());

    // Add this same object again with the same name.  This time, a cycle
    // is detected upon construction.  The name doesn't matter.
    CycleDetector::ObjectInfo obj1b("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_FALSE(obj1b.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());

    // Add this same object again (with a different name).  This time, a cycle
    // is detected upon construction.  The name doesn't matter.
    CycleDetector::ObjectInfo obj1c("obj1c", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_FALSE(obj1b.DetectCycle("object").ok());
    EXPECT_FALSE(obj1c.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());

    // Add this same object yet again.  At this point, the behavior stays
    // the same as the previous object creation.
    CycleDetector::ObjectInfo obj1d("obj1d", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_FALSE(obj1b.DetectCycle("object").ok());
    EXPECT_FALSE(obj1c.DetectCycle("object").ok());
    EXPECT_FALSE(obj1d.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());
    EXPECT_EQ("obj1", absl::StrJoin(cycle_detector()->ObjectNames(), ", "));
  }

  {
    // Add a few objects.
    CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());

    CycleDetector::ObjectInfo obj2("obj2", object2(), cycle_detector());
    EXPECT_TRUE(obj2.DetectCycle("object").ok());
    EXPECT_EQ(2, cycle_detector()->Size());

    CycleDetector::ObjectInfo obj3("obj3", object3(), cycle_detector());
    EXPECT_TRUE(obj3.DetectCycle("object").ok());
    EXPECT_EQ(3, cycle_detector()->Size());

    CycleDetector::ObjectInfo obj4("obj4", object4(), cycle_detector());
    EXPECT_TRUE(obj4.DetectCycle("object").ok());
    EXPECT_EQ(4, cycle_detector()->Size());

    EXPECT_EQ("obj1, obj2, obj3, obj4",
              absl::StrJoin(cycle_detector()->ObjectNames(), ", "));
    EXPECT_EQ("obj1, obj2, obj3, obj4",
              ObjectNames(cycle_detector()->object_deque()));
    EXPECT_EQ("obj1, obj2, obj3, obj4",
              cycle_detector()->DebugString());

    // Adding any of these objects again detects a cycle.
    CycleDetector::ObjectInfo obj1b("obj1", object1(), cycle_detector());
    EXPECT_FALSE(obj1b.DetectCycle("object").ok());
    CycleDetector::ObjectInfo obj2b("obj2", object2(), cycle_detector());
    EXPECT_FALSE(obj2b.DetectCycle("object").ok());
    CycleDetector::ObjectInfo obj3b("obj3", object3(), cycle_detector());
    EXPECT_FALSE(obj3b.DetectCycle("object").ok());
    CycleDetector::ObjectInfo obj4b("obj4b", object4(), cycle_detector());
    EXPECT_FALSE(obj4b.DetectCycle("object").ok());
  }

  // Nested destruction
  {
    CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
    EXPECT_TRUE(obj1.DetectCycle("object").ok());
    EXPECT_EQ(1, cycle_detector()->Size());
    {
      CycleDetector::ObjectInfo obj1("obj1", object1(), cycle_detector());
      EXPECT_FALSE(obj1.DetectCycle("object").ok());
      EXPECT_EQ(1, cycle_detector()->Size());

      CycleDetector::ObjectInfo obj2("obj2", object2(), cycle_detector());
      EXPECT_TRUE(obj2.DetectCycle("object").ok());
      EXPECT_EQ(2, cycle_detector()->Size());
      {
        CycleDetector::ObjectInfo obj2("obj2", object2(), cycle_detector());
        EXPECT_FALSE(obj2.DetectCycle("object").ok());
        EXPECT_EQ(2, cycle_detector()->Size());

        CycleDetector::ObjectInfo obj3("obj3", object3(), cycle_detector());
        EXPECT_TRUE(obj3.DetectCycle("object").ok());
        EXPECT_EQ(3, cycle_detector()->Size());

        EXPECT_EQ("obj1, obj2, obj3",
                  ObjectNames(cycle_detector()->object_deque()));
      }
      EXPECT_EQ("obj1, obj2",
                ObjectNames(cycle_detector()->object_deque()));

      CycleDetector::ObjectInfo obj3("obj3", object3(), cycle_detector());
      EXPECT_TRUE(obj3.DetectCycle("object").ok());
      EXPECT_EQ(3, cycle_detector()->Size());

      EXPECT_EQ("obj1, obj2, obj3",
                ObjectNames(cycle_detector()->object_deque()));
    }
    EXPECT_EQ("obj1", ObjectNames(cycle_detector()->object_deque()));
  }
  EXPECT_TRUE(cycle_detector()->IsEmpty());
}

}  // namespace googlesql
