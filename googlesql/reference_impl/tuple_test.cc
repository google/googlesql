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

#include "googlesql/reference_impl/tuple.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/tuple_test_util.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_builder.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

TEST(TupleSchemaTest, BasicTests) {
  const VariableId foo("foo");
  const VariableId bar("bar");
  const VariableId baz("baz");
  TupleSchema schema({foo, bar});

  EXPECT_EQ(2, schema.num_variables());
  EXPECT_THAT(schema.variables(), ElementsAre(foo, bar));
  EXPECT_EQ(schema.variable(0), foo);
  EXPECT_EQ(schema.variable(1), bar);

  std::optional<int> i = schema.FindIndexForVariable(foo);
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(i.value(), 0);

  i = schema.FindIndexForVariable(bar);
  ASSERT_TRUE(i.has_value());
  EXPECT_EQ(i.value(), 1);

  EXPECT_FALSE(schema.FindIndexForVariable(baz).has_value());

  EXPECT_EQ("<foo,bar>", schema.DebugString());
}

TEST(TupleSlotTest, GetPhysicalByteSize) {
  const TupleSlot empty_slot;
  EXPECT_GT(empty_slot.GetPhysicalByteSize(), sizeof(Value));

  TupleSlot int64_slot;
  int64_slot.SetValue(Int64(10));
  EXPECT_GT(int64_slot.GetPhysicalByteSize(), Int64(10).physical_byte_size());

  TypeFactory type_factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  const absl::Cord raw_bytes("garbage");
  const Value proto_value = Value::Proto(proto_type, raw_bytes);

  TupleSlot proto_slot;
  proto_slot.SetValue(proto_value);
  EXPECT_GT(proto_slot.GetPhysicalByteSize(), proto_value.physical_byte_size());

  TupleSlot shared_state_slot;
  shared_state_slot.SetValue(proto_value);
  EXPECT_EQ(shared_state_slot.GetPhysicalByteSize(),
            proto_slot.GetPhysicalByteSize());
  int64_t last_byte_size = shared_state_slot.GetPhysicalByteSize();
  std::shared_ptr<TupleSlot::SharedProtoState> shared_state =
      *shared_state_slot.mutable_shared_proto_state();
  auto value_list = std::make_unique<ProtoFieldValueList>();
  value_list->push_back(googlesql_base::InternalErrorBuilder() << "foo");
  value_list->push_back(Int64(10));
  value_list->push_back(Int64(20));
  ProtoFieldValueMap value_map;
  value_map.emplace(ProtoFieldValueMapKey(), std::move(value_list));
  *shared_state = std::move(value_map);
  EXPECT_GT(shared_state_slot.GetPhysicalByteSize(), last_byte_size);
}

TEST(TupleDataTest, BasicTests) {
  TupleData data(/*num_slots=*/3);
  EXPECT_EQ(3, data.num_slots());

  data.mutable_slot(0)->SetValue(Int64(10));
  data.mutable_slot(1)->SetValue(Int64(20));
  data.mutable_slot(2)->SetValue(Int64(30));

  EXPECT_EQ(Int64(10), data.slot(0).value());
  EXPECT_EQ(Int64(20), data.slot(1).value());
  EXPECT_EQ(Int64(30), data.slot(2).value());

  const std::vector<TupleSlot>& slots = data.slots();
  EXPECT_EQ(Int64(10), slots[0].value());
  EXPECT_EQ(Int64(20), slots[1].value());
  EXPECT_EQ(Int64(30), slots[2].value());

  data.Clear();
  EXPECT_EQ(0, data.num_slots());

  data.AddSlots(2);
  EXPECT_EQ(2, data.num_slots());
  data.mutable_slot(0)->SetValue(Int64(100));
  data.mutable_slot(1)->SetValue(Int64(200));

  data.AddSlots(3);
  EXPECT_EQ(5, data.num_slots());
  data.mutable_slot(2)->SetValue(Int64(300));
  data.mutable_slot(3)->SetValue(Int64(400));
  data.mutable_slot(4)->SetValue(Int64(500));
  EXPECT_EQ(Int64(100), data.slot(0).value());
  EXPECT_EQ(Int64(200), data.slot(1).value());

  data.mutable_slot(7)->SetValue(Int64(800));
  EXPECT_EQ(8, data.num_slots());
  EXPECT_EQ(Int64(100), data.slot(0).value());
  EXPECT_EQ(Int64(200), data.slot(1).value());
  EXPECT_EQ(Int64(300), data.slot(2).value());
  EXPECT_EQ(Int64(400), data.slot(3).value());
  EXPECT_EQ(Int64(500), data.slot(4).value());
  EXPECT_EQ(Int64(800), data.slot(7).value());

  TupleData empty_tuple;
  EXPECT_EQ(0, empty_tuple.num_slots());

  TupleData value_tuple =
      CreateTupleDataFromValues({Int64(10), Int64(20), Int64(30)});
  EXPECT_EQ(3, value_tuple.num_slots());
  EXPECT_EQ(Int64(10), value_tuple.slot(0).value());
  EXPECT_EQ(Int64(20), value_tuple.slot(1).value());
  EXPECT_EQ(Int64(30), value_tuple.slot(2).value());
}

TEST(TupleDataTest, Equals) {
  const TupleData data1 =
      CreateTupleDataFromValues({Int64(1), Int64(2), Int64(3)});
  const TupleData data2 =
      CreateTupleDataFromValues({Int64(1), Int64(2), Int64(3)});
  const TupleData data3 =
      CreateTupleDataFromValues({Int64(1), Int64(2), Int64(4)});
  const TupleData data4 =
      CreateTupleDataFromValues({Int64(1), Int64(2), Int64(3), Int64(4)});

  EXPECT_TRUE(data1.Equals(data2));
  EXPECT_FALSE(data1.Equals(data3));
  EXPECT_FALSE(data1.Equals(data4));
}

TEST(Tuple, DebugString) {
  TupleSchema schema({VariableId("foo"), VariableId("bar")});
  TupleData data = CreateTupleDataFromValues({Int64(10), NullInt64()});
  Tuple tuple(&schema, &data);
  EXPECT_EQ("<foo:10,bar:NULL>", tuple.DebugString());
  EXPECT_EQ("Tuple<foo:Int64(10),bar:Int64(NULL)>",
            tuple.DebugString(/*verbose=*/true));
}

class ConcatTuplesTest : public ::testing::Test {
 protected:
  ConcatTuplesTest()
      : schema1_({VariableId("foo"), VariableId("bar")}),
        schema2_({VariableId("baz")}),
        schema3_({VariableId("boo")}),
        data1_(CreateTupleDataFromValues({Int64(1), Int64(2)})),
        data2_(CreateTupleDataFromValues({Int64(3)})),
        data3_(CreateTupleDataFromValues({Int64(4)})) {}

  Tuple Concat() {
    const Tuple t1(&schema1_, &data1_);
    const Tuple t2(&schema2_, &data2_);
    const Tuple t3(&schema3_, &data3_);
    return ConcatTuples({t1, t2, t3}, &concat_schema_, &concat_data_);
  }

  const TupleSchema schema1_;
  const TupleSchema schema2_;
  const TupleSchema schema3_;

  TupleData data1_;
  TupleData data2_;
  TupleData data3_;

  std::unique_ptr<TupleSchema> concat_schema_;
  std::unique_ptr<TupleData> concat_data_;
};

TEST_F(ConcatTuplesTest, BasicTest) {
  const Tuple t = Concat();
  EXPECT_EQ(t.DebugString(), "<foo:1,bar:2,baz:3,boo:4>");
  EXPECT_EQ(t.data->num_slots(), 4);
}

TEST_F(ConcatTuplesTest, IgnoreExtraSlots) {
  data2_.AddSlots(2);
  data3_.AddSlots(3);

  const Tuple t = Concat();
  EXPECT_EQ(t.DebugString(), "<foo:1,bar:2,baz:3,boo:4>");
  EXPECT_EQ(t.data->num_slots(), 4);
}

TEST(MemoryAccountant, BasicTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/100, "test_limit");
  absl::Status status;

  EXPECT_TRUE(accountant.RequestBytes(50, &status));
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(accountant.remaining_bytes(), 50);

  EXPECT_TRUE(accountant.RequestBytes(30, &status));
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(accountant.remaining_bytes(), 20);

  EXPECT_TRUE(accountant.RequestBytes(20, &status));
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(accountant.remaining_bytes(), 0);

  EXPECT_TRUE(accountant.RequestBytes(0, &status));
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(accountant.remaining_bytes(), 0);

  EXPECT_FALSE(accountant.RequestBytes(1, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("Out of memory for MemoryAccountant(test_limit)")));
  EXPECT_EQ(accountant.remaining_bytes(), 0);
  status = absl::OkStatus();

  accountant.ReturnBytes(50);
  EXPECT_EQ(accountant.remaining_bytes(), 50);

  EXPECT_TRUE(accountant.RequestBytes(50, &status));
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(accountant.remaining_bytes(), 0);

  accountant.ReturnBytes(50);
  EXPECT_EQ(accountant.remaining_bytes(), 50);

  EXPECT_FALSE(accountant.RequestBytes(51, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_EQ(accountant.remaining_bytes(), 50);

  // Bring 'accountant' back to 100 bytes so we can destroy it.
  accountant.ReturnBytes(50);
}

TEST(MemoryAccountantTest,
     RequestUInt64BytesFailsWithResourceExhaustedWhenFailingToCast) {
  MemoryAccountant accountant(/*total_num_bytes=*/100, "test_limit");
  absl::Status status;
  EXPECT_FALSE(accountant.RequestUInt64Bytes(
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(MemoryAccountantTest,
     RequestUInt64BytesFailsWithResourceExhaustedWhenRequestingTooManyBytes) {
  MemoryAccountant accountant(/*total_num_bytes=*/100, "test_limit");
  absl::Status status;
  EXPECT_FALSE(accountant.RequestUInt64Bytes(
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()), &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(MemoryAccountantTest,
     RequestUInt64BytesSucceedsWhenRequestingBytesWithinLimit) {
  MemoryAccountant accountant(/*total_num_bytes=*/100, "test_limit");
  absl::Status status;
  EXPECT_TRUE(accountant.RequestUInt64Bytes(100, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOk));

  // Bring 'accountant' back to 100 bytes so we can destroy it.
  accountant.ReturnBytes(100);
}

TEST(TupleDataDeque, PushAndPopTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");

  TupleDataDeque deque(&accountant);

  // Insert a few tuples.
  int num_tuples = 0;
  while (true) {
    EXPECT_EQ(deque.IsEmpty(), num_tuples == 0);
    EXPECT_EQ(deque.GetSize(), num_tuples);

    const int64_t remaining_bytes = accountant.remaining_bytes();

    TupleData data = CreateTupleDataFromValues({Int64(num_tuples)});

    absl::Status status;
    if (!deque.PushBack(std::make_unique<TupleData>(data), &status)) {
      ASSERT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
      EXPECT_EQ(remaining_bytes, accountant.remaining_bytes());
      break;
    }

    ++num_tuples;

    // The queue has to store the size (as an int64) of each TupleData, so
    // adding an entry costs at least that many bytes.
    EXPECT_LE(accountant.remaining_bytes() + sizeof(int64_t), remaining_bytes);
  }

  // Make sure a few tuples were inserted. 10 is an arbitrary number.
  ASSERT_GE(num_tuples, 10);

  // Test Tuple::GetTuplePtrs().
  std::vector<const TupleData*> tuple_ptrs = deque.GetTuplePtrs();
  EXPECT_EQ(tuple_ptrs.size(), num_tuples);
  for (int i = 0; i < tuple_ptrs.size(); ++i) {
    const TupleData& data = *tuple_ptrs[i];
    ASSERT_EQ(data.num_slots(), 1);
    EXPECT_EQ(data.slot(0).value(), Int64(i));
  }

  // Extract the tuples.
  for (int i = 0; i < num_tuples; ++i) {
    ASSERT_FALSE(deque.IsEmpty());
    const int64_t remaining_bytes = accountant.remaining_bytes();

    std::unique_ptr<TupleData> data = deque.PopFront();
    ASSERT_EQ(data->num_slots(), 1);
    EXPECT_EQ(data->slot(0).value(), Int64(i));

    EXPECT_GE(accountant.remaining_bytes(), remaining_bytes + sizeof(int64_t));
  }

  EXPECT_TRUE(deque.IsEmpty());
  EXPECT_EQ(accountant.remaining_bytes(), 1000);
}

TEST(TupleDataDeque, DestructorTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  {
    TupleDataDeque deque(&accountant);
    TupleData data = CreateTupleDataFromValues({Int64(10)});
    absl::Status status;
    ASSERT_TRUE(deque.PushBack(std::make_unique<TupleData>(data), &status));
    ASSERT_TRUE(deque.PushBack(std::make_unique<TupleData>(data), &status));
    ASSERT_TRUE(deque.PushBack(std::make_unique<TupleData>(data), &status));
    EXPECT_LT(accountant.remaining_bytes(), 1000);
  }
  EXPECT_EQ(accountant.remaining_bytes(), 1000);
}

TEST(TupleDataDeque, SetSlotTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/10000, "test_limit");
  TupleDataDeque deque(&accountant);

  // Build up TupleDatas with unset slots.
  const int num_tuples = 15;
  for (int i = 0; i < num_tuples; ++i) {
    TupleData data(/*num_slots=*/2);
    data.mutable_slot(0)->SetValue(Int64(i));
    absl::Status status;
    ASSERT_TRUE(deque.PushBack(std::make_unique<TupleData>(data), &status));
  }

  int64_t original_remaining_bytes = accountant.remaining_bytes();

  const TupleSlot empty_slot;
  const int64_t empty_size = empty_slot.GetPhysicalByteSize() * num_tuples;

  // Build up new values for the unset slots.
  std::vector<Value> values2;
  int64_t values2_size = 0;
  for (int i = 0; i < num_tuples; ++i) {
    Value value = String(absl::StrCat("foo", i));
    values2.push_back(value);

    TupleSlot slot;
    slot.SetValue(value);
    ASSERT_GT(slot.GetPhysicalByteSize(), empty_slot.GetPhysicalByteSize());
    values2_size += slot.GetPhysicalByteSize();
  }

  // Set the previously unset slots.
  GOOGLESQL_ASSERT_OK(deque.SetSlot(/*slot_idx=*/1, values2));
  EXPECT_EQ(accountant.remaining_bytes(),
            original_remaining_bytes + empty_size - values2_size);

  // Verify the contents of the deque.
  std::vector<const TupleData*> tuples = deque.GetTuplePtrs();
  EXPECT_EQ(tuples.size(), num_tuples);
  for (int i = 0; i < tuples.size(); ++i) {
    const TupleData& data = *tuples[i];
    ASSERT_EQ(data.num_slots(), 2);
    EXPECT_EQ(data.slot(0).value(), Int64(i));
    EXPECT_EQ(data.slot(1).value(), String(absl::StrCat("foo", i)));
  }

  // Now reset the slots to shorter values.
  original_remaining_bytes = accountant.remaining_bytes();
  std::vector<Value> values3;
  int64_t values3_size = 0;
  for (int i = 0; i < num_tuples; ++i) {
    Value value = String(absl::StrCat("f", i));
    values3.push_back(value);

    TupleSlot new_slot;
    new_slot.SetValue(value);

    TupleSlot old_slot;
    old_slot.SetValue(values2[i]);

    ASSERT_LT(new_slot.GetPhysicalByteSize(), old_slot.GetPhysicalByteSize());
    values3_size += new_slot.GetPhysicalByteSize();
  }
  GOOGLESQL_ASSERT_OK(deque.SetSlot(/*slot_idx=*/1, values3));
  EXPECT_EQ(accountant.remaining_bytes(),
            original_remaining_bytes + values2_size - values3_size);

  // Verify the contents of the deque.
  tuples = deque.GetTuplePtrs();
  EXPECT_EQ(tuples.size(), num_tuples);
  for (int i = 0; i < tuples.size(); ++i) {
    const TupleData& data = *tuples[i];
    ASSERT_EQ(data.num_slots(), 2);
    EXPECT_EQ(data.slot(0).value(), Int64(i));
    EXPECT_EQ(data.slot(1).value(), String(absl::StrCat("f", i)));
  }
}

TEST(TupleDataOrderedQueue, InsertAndPopTest) {
  VariableId k1("k1"), k2("k2");
  TupleSchema schema({k1});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> key,
                       DerefExpr::Create(k1, Int64Type()));
  KeyArg key_arg(k2, std::move(key), KeyArg::kDescending);

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleComparator> comparator,
      TupleComparator::Create({&key_arg}, /*slots_for_keys=*/{0},
                              /*params=*/{}, &context));

  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  TupleDataOrderedQueue q(*comparator, &accountant);

  for (const bool pop_back : {false, true}) {
    ABSL_LOG(INFO) << "Testing with pop_back: " << pop_back;

    // Insert a few tuples.
    int num_tuples = 0;
    while (true) {
      EXPECT_EQ(q.IsEmpty(), num_tuples == 0);
      EXPECT_EQ(q.GetSize(), num_tuples);

      const int64_t remaining_bytes = accountant.remaining_bytes();

      TupleData data = CreateTupleDataFromValues({Int64(num_tuples)});

      // The queue should reverse the order of the tuples.
      absl::Status status;
      if (!q.Insert(std::make_unique<TupleData>(data), &status)) {
        ASSERT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
        EXPECT_EQ(remaining_bytes, accountant.remaining_bytes());
        break;
      }

      ++num_tuples;

      EXPECT_LE(accountant.remaining_bytes() + sizeof(int64_t),
                remaining_bytes);
    }

    // Make sure a few tuples were inserted.
    ASSERT_GE(num_tuples, 10);

    for (int i = 0; i < num_tuples; ++i) {
      ASSERT_FALSE(q.IsEmpty());
      const int64_t remaining_bytes = accountant.remaining_bytes();

      if (pop_back) {
        std::unique_ptr<TupleData> data = q.PopBack();
        ASSERT_EQ(data->num_slots(), 1);
        EXPECT_EQ(data->slot(0).value(), Int64(i));
      } else {
        std::unique_ptr<TupleData> data = q.PopFront();
        ASSERT_EQ(data->num_slots(), 1);
        EXPECT_EQ(data->slot(0).value(), Int64(num_tuples - 1 - i));
      }

      EXPECT_GE(accountant.remaining_bytes(),
                remaining_bytes + sizeof(int64_t));
    }

    EXPECT_TRUE(q.IsEmpty());
    EXPECT_EQ(accountant.remaining_bytes(), 1000);
  }
}

TEST(TupleDataOrderedQueue, DestructorTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  {
    VariableId k1("k1"), k2("k2");
    TupleSchema schema({k1});
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> key,
                         DerefExpr::Create(k1, Int64Type()));
    KeyArg key_arg(k2, std::move(key), KeyArg::kAscending);

    EvaluationContext context((EvaluationOptions()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<TupleComparator> comparator,
        TupleComparator::Create({&key_arg}, /*slots_for_keys=*/{0},
                                /*params=*/{}, &context));

    TupleDataOrderedQueue q(*comparator, &accountant);
    TupleData data = CreateTupleDataFromValues({Int64(10)});
    absl::Status status;
    ASSERT_TRUE(q.Insert(std::make_unique<TupleData>(data), &status));
    ASSERT_TRUE(q.Insert(std::make_unique<TupleData>(data), &status));
    ASSERT_TRUE(q.Insert(std::make_unique<TupleData>(data), &status));
    EXPECT_LT(accountant.remaining_bytes(), 1000);
  }
  EXPECT_EQ(accountant.remaining_bytes(), 1000);
}

TEST(ValueHashSet, BasicTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  ValueHashSet set(&accountant);

  int num_tuples = 0;
  while (true) {
    bool inserted;
    absl::Status status;
    if (!set.Insert(Int64(num_tuples), &inserted, &status)) {
      EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
      EXPECT_FALSE(inserted);
      break;
    }
    ++num_tuples;
  }

  // Ensure we got a reasonable number of tuples. 10 is an arbitrary number.
  ASSERT_GE(num_tuples, 10);

  for (int i = 0; i <= num_tuples; ++i) {
    const Value value = Int64(i);
    bool inserted;
    absl::Status status;
    if (i < num_tuples) {
      EXPECT_TRUE(set.Insert(value, &inserted, &status));
      EXPECT_FALSE(inserted);
    } else {
      EXPECT_FALSE(set.Insert(value, &inserted, &status));
      EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
    }
  }

  set.Clear();
  EXPECT_EQ(accountant.remaining_bytes(), 1000);
}

TEST(ValueHashSet, DestructorTest) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  {
    ValueHashSet set(&accountant);
    for (int i = 0; i <= 3; ++i) {
      bool inserted;
      absl::Status status;
      EXPECT_TRUE(set.Insert(Int64(i), &inserted, &status));
      EXPECT_TRUE(inserted);
    }
  }
  EXPECT_EQ(accountant.remaining_bytes(), 1000);
}

TEST(MemoryReservation, Basic) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  MemoryReservation res(&accountant);
  absl::Status status;
  ASSERT_TRUE(res.Increase(400, &status));
  EXPECT_THAT(600, accountant.remaining_bytes());

  ASSERT_TRUE(res.Increase(400, &status));
  EXPECT_THAT(200, accountant.remaining_bytes());

  ASSERT_FALSE(res.Increase(400, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
  EXPECT_THAT(200, accountant.remaining_bytes());
}

TEST(MemoryReservation, Destructor) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");

  {
    MemoryReservation res(&accountant);
    absl::Status status;
    ASSERT_TRUE(res.Increase(400, &status));
    EXPECT_THAT(600, accountant.remaining_bytes());
  }
  EXPECT_THAT(1000, accountant.remaining_bytes());
}

TEST(MemoryReservation, Move) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");

  {
    MemoryReservation res(&accountant);
    absl::Status status;
    ASSERT_TRUE(res.Increase(400, &status));
    ASSERT_THAT(600, accountant.remaining_bytes());

    // Moving the reservation doesn't change the state of the accountant
    MemoryReservation res2(std::move(res));
    ASSERT_THAT(600, accountant.remaining_bytes());
  }
  // After destructors for both 'res' and 'res2', the memory should be released
  // only once.
  EXPECT_THAT(1000, accountant.remaining_bytes());
}

TEST(MemoryReservation, AccountantDestroyedAfterMove) {
  absl::Status status;
  auto accountant = std::make_unique<MemoryAccountant>(/*total_num_bytes=*/1000,
                                                       "test_limit");
  auto res1 = std::make_unique<MemoryReservation>(accountant.get());
  ASSERT_TRUE(res1->Increase(400, &status));
  auto res2 = std::make_unique<MemoryReservation>(std::move(*res1));

  // Make sure that 'res1' being destroyed after the accountant is ok, since it
  // has been moved to 'res2'.
  res2.reset();
  accountant.reset();
  res1.reset();
}

TEST(ArrayBuilder, Basic) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  ArrayBuilder builder(&accountant);

  absl::Status status;
  int64_t int64_value_size = Value::Int64(0).physical_byte_size();
  for (int i = 1; i <= 5; ++i) {
    EXPECT_TRUE(builder.PushBackUnsafe(Value::Int64(i), &status));
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_EQ(1000 - i * int64_value_size, accountant.remaining_bytes());
    EXPECT_FALSE(builder.IsEmpty());
    EXPECT_EQ(i, builder.GetSize());
  }

  {
    ArrayBuilder::TrackedValue arr =
        builder.Build(types::Int64ArrayType(), InternalValue::kPreservesOrder);
    EXPECT_EQ(Int64Array({1, 2, 3, 4, 5}), arr.value);
    EXPECT_EQ(1000 - 5 * int64_value_size, accountant.remaining_bytes());
    EXPECT_EQ(InternalValue::kPreservesOrder,
              InternalValue::order_kind(arr.value));
  }
  EXPECT_EQ(1000, accountant.remaining_bytes());
}

TEST(ArrayBuilder, NoPreserveOrder) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  ArrayBuilder builder(&accountant);

  absl::Status status;
  int64_t int64_value_size = Value::Int64(0).physical_byte_size();
  for (int i = 1; i <= 5; ++i) {
    EXPECT_TRUE(builder.PushBackUnsafe(Value::Int64(i), &status));
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_EQ(1000 - i * int64_value_size, accountant.remaining_bytes());
    EXPECT_FALSE(builder.IsEmpty());
    EXPECT_EQ(i, builder.GetSize());
  }

  {
    ArrayBuilder::TrackedValue arr =
        builder.Build(types::Int64ArrayType(), InternalValue::kIgnoresOrder);
    EXPECT_EQ(Int64Array({1, 2, 3, 4, 5}), arr.value);
    EXPECT_EQ(1000 - 5 * int64_value_size, accountant.remaining_bytes());
    EXPECT_EQ(InternalValue::kIgnoresOrder,
              InternalValue::order_kind(arr.value));
  }
  EXPECT_EQ(1000, accountant.remaining_bytes());
}

TEST(ArrayBuilder, PushBackFails) {
  int64_t int64_value_size = Value::Int64(0).physical_byte_size();
  MemoryAccountant accountant(int64_value_size * 2, "test_limit");
  ArrayBuilder builder(&accountant);

  absl::Status status;
  EXPECT_TRUE(builder.PushBackUnsafe(Value::Int64(1), &status));
  EXPECT_TRUE(builder.PushBackUnsafe(Value::Int64(2), &status));
  EXPECT_FALSE(builder.PushBackUnsafe(Value::Int64(3), &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));

  // Make sure builder state did not change on failed push-back
  EXPECT_EQ(2, builder.GetSize());

  // Make sure memory accountant is left in expected state upon failure
  EXPECT_EQ(0, accountant.remaining_bytes());
}

TEST(ArrayBuilder, VariableSizedElements) {
  // This test uses STRING instead of INT64 to make sure that there are no
  // incorrect assumptions about array elements being the same size.
  MemoryAccountant accountant(/*total_num_bytes=*/150, "test_limit");
  ArrayBuilder builder(&accountant);
  absl::Status status;
  EXPECT_TRUE(builder.PushBackUnsafe(Value::NullString(), &status));
  EXPECT_TRUE(builder.PushBackUnsafe(Value::String("short string"), &status));
  EXPECT_FALSE(builder.PushBackUnsafe(
      Value::String(
          "This is a very very very very very very very very very very very "
          "very very very very very very very very long long long long long "
          "long long long long long long long long long string"),
      &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(ArrayBuilder, Destructor) {
  MemoryAccountant accountant(/*total_num_bytes=*/1000, "test_limit");
  {
    ArrayBuilder builder(&accountant);

    absl::Status status;
    for (int i = 1; i <= 5; ++i) {
      EXPECT_TRUE(builder.PushBackUnsafe(Value::Int64(i), &status));
    }
  }

  // Make sure that used memory is returned to the accountant in the destructor
  EXPECT_EQ(1000, accountant.remaining_bytes());
}

TEST(ArrayBuilder, MoveAndCopyValues) {
  // Make sure that moving values actually moves them, rather than copying them
  MemoryAccountant accountant(1000, "test_limit");
  ArrayBuilder builder(&accountant);
  Value value10 = Value::Int64(10);
  absl::Status status;
  ASSERT_TRUE(builder.PushBackUnsafe(std::move(value10), &status));
  ArrayBuilder::TrackedValue value =
      builder.Build(Int64ArrayType(), InternalValue::kPreservesOrder);
  EXPECT_FALSE(value10.is_valid()) << "Should have been moved";
  EXPECT_EQ(Int64Array({10}), value.value);
}

TEST(ReorderingTupleIterator, BasicTest) {
  for (int size = 0; size <= 500; ++size) {
    for (bool error : {false, true}) {
      ABSL_LOG(INFO) << "Testing with iterator size " << size << " and ending with "
                << (error ? "error" : "success");
      std::vector<Value> values;
      values.reserve(size);
      for (int i = 0; i < size; ++i) {
        values.push_back(Int64(i));
      }

      std::vector<TupleData> data;
      data.reserve(values.size());
      for (const Value& value : values) {
        data.push_back(CreateTupleDataFromValues({value}));
      }

      absl::Status expected_end_status;
      if (error) {
        expected_end_status = googlesql_base::OutOfRangeErrorBuilder()
                              << "Some evaluation error";
      }

      std::unique_ptr<TupleIterator> iter = std::make_unique<TestTupleIterator>(
          std::vector<VariableId>{VariableId("foo")}, data,
          /*preserves_order=*/true, expected_end_status);

      iter = std::make_unique<ReorderingTupleIterator>(std::move(iter));
      EXPECT_EQ(iter->DebugString(),
                "ReorderingTupleIterator(TestTupleIterator)");

      absl::Status end_status;
      std::vector<TupleData> output_data =
          ReadFromTupleIteratorFull(iter.get(), &end_status);
      EXPECT_EQ(end_status, expected_end_status);

      std::vector<Value> output_values;
      std::vector<bool> seen_values(size);
      EXPECT_EQ(output_data.size(), size);
      for (const TupleData& data : output_data) {
        ASSERT_EQ(data.num_slots(), 1);
        const int64_t value = data.slot(0).value().int64_value();
        EXPECT_GE(value, 0);
        EXPECT_LT(value, seen_values.size());
        EXPECT_FALSE(seen_values[value]);
        seen_values[value] = true;
      }
    }
  }
}

TEST(ReorderingTupleIterator, DisableReordering) {
  std::vector<TupleData> values;
  for (int i = 0; i < 3; ++i) {
    values.push_back(CreateTupleDataFromValues({Int64(i)}));
  }
  std::unique_ptr<TupleIterator> iter = std::make_unique<TestTupleIterator>(
      std::vector<VariableId>{VariableId("foo")}, values,
      /*preserves_order=*/true, absl::OkStatus());

  iter = std::make_unique<ReorderingTupleIterator>(std::move(iter));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK(iter->DisableReordering());
  EXPECT_TRUE(iter->PreservesOrder());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> output,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(output.size(), values.size());
  ASSERT_EQ(output[0].slots().size(), 1);
  ASSERT_EQ(output[1].slots().size(), 1);
  ASSERT_EQ(output[2].slots().size(), 1);
  EXPECT_EQ(output[0].slot(0).value(), Int64(0));
  EXPECT_EQ(output[1].slot(0).value(), Int64(1));
  EXPECT_EQ(output[2].slot(0).value(), Int64(2));
}

TEST(ReorderingTupleIterator, DisableReorderingFailsAfterNext) {
  std::vector<TupleData> values;
  for (int i = 0; i < 3; ++i) {
    values.push_back(CreateTupleDataFromValues({Int64(i)}));
  }
  std::unique_ptr<TupleIterator> iter = std::make_unique<TestTupleIterator>(
      std::vector<VariableId>{VariableId("foo")}, values,
      /*preserves_order=*/true, absl::OkStatus());

  iter = std::make_unique<ReorderingTupleIterator>(std::move(iter));
  EXPECT_FALSE(iter->PreservesOrder());
  const TupleData* data = iter->Next();
  ASSERT_TRUE(data != nullptr);
  EXPECT_THAT(
      iter->DisableReordering(),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("DisableReordering() cannot be called after Next()")));
}

TEST(PassThroughTupleIterator, FactoryFails) {
  PassThroughTupleIterator::IteratorFactory iterator_factory = [] {
    return googlesql_base::InternalErrorBuilder() << "Iterator factory failure";
  };
  PassThroughTupleIterator::DebugStringFactory string_factory = [] {
    return "VeryLongString";
  };
  VariableId foo("foo"), bar("bar");
  const TupleSchema schema({foo, bar});

  PassThroughTupleIterator iter(iterator_factory, schema, string_factory);
  EXPECT_EQ(iter.DebugString(),
            "PassThroughTupleIterator(Factory for VeryLongString)");
  EXPECT_THAT(iter.Schema().variables(), ElementsAre(foo, bar));
  GOOGLESQL_EXPECT_OK(iter.Status());
  const TupleData* data = iter.Next();
  EXPECT_TRUE(data == nullptr);
  EXPECT_THAT(iter.Status(), StatusIs(absl::StatusCode::kInternal,
                                      "Iterator factory failure"));
}

TEST(PassThroughTupleIterator, IteratorFails) {
  VariableId foo("foo"), bar("bar");
  const TupleSchema schema({foo, bar});
  PassThroughTupleIterator::IteratorFactory iterator_factory = [&schema]() {
    absl::Status iterator_failure = googlesql_base::InternalErrorBuilder()
                                    << "Iterator failure";
    auto iter = std::make_unique<TestTupleIterator>(
        schema.variables(), std::vector<TupleData>(),
        /*preserves_order=*/true, iterator_failure);
    return absl::StatusOr<std::unique_ptr<TupleIterator>>(std::move(iter));
  };
  PassThroughTupleIterator::DebugStringFactory string_factory = [] {
    return "VeryLongString";
  };

  PassThroughTupleIterator iter(iterator_factory, schema, string_factory);
  EXPECT_EQ(iter.DebugString(),
            "PassThroughTupleIterator(Factory for VeryLongString)");
  EXPECT_THAT(iter.Schema().variables(), ElementsAre(foo, bar));
  GOOGLESQL_EXPECT_OK(iter.Status());
  const TupleData* data = iter.Next();
  EXPECT_TRUE(data == nullptr);
  EXPECT_THAT(iter.Status(),
              StatusIs(absl::StatusCode::kInternal, "Iterator failure"));
}

TEST(PassThroughTupleIterator, Success) {
  VariableId foo("foo"), bar("bar");
  const TupleSchema schema({foo, bar});
  const std::vector<TupleData> values = {
      CreateTupleDataFromValues({Int64(1), Int64(10)}),
      CreateTupleDataFromValues({Int64(2), Int64(20)})};
  PassThroughTupleIterator::IteratorFactory iterator_factory = [&schema,
                                                                &values]() {
    auto iter = std::make_unique<TestTupleIterator>(
        schema.variables(), values,
        /*preserves_order=*/true, /*end_status=*/absl::OkStatus());
    return absl::StatusOr<std::unique_ptr<TupleIterator>>(std::move(iter));
  };
  PassThroughTupleIterator::DebugStringFactory string_factory = [] {
    return "VeryLongString";
  };

  PassThroughTupleIterator iter(iterator_factory, schema, string_factory);
  EXPECT_EQ(iter.DebugString(),
            "PassThroughTupleIterator(Factory for VeryLongString)");
  EXPECT_THAT(iter.Schema().variables(), ElementsAre(foo, bar));
  GOOGLESQL_EXPECT_OK(iter.Status());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(&iter));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&schema, &data[0]).DebugString(), "<foo:1,bar:10>");
  EXPECT_EQ(Tuple(&schema, &data[1]).DebugString(), "<foo:2,bar:20>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
}

}  // namespace
}  // namespace googlesql
