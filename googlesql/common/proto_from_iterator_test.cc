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

#include "googlesql/common/proto_from_iterator.h"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/convert_type_to_proto.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using googlesql_test::IteratorProto;
using googlesql_test::KitchenSinkPB;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

TEST(ConvertIteratorToProtoTest, Basic) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"int64_val", types::Int64Type()},
      {"string_val", types::StringType()},
      {"bool_val", types::BoolType()},
  };

  SimpleTable table{"TestTable", columns};
  table.SetContents({});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const EvaluatorTableIterator> iter,
                       table.CreateEvaluatorTableIterator({2, 0, 1}));

  google::protobuf::DescriptorPool pool{google::protobuf::DescriptorPool::generated_pool()};

  IteratorProtoDescriptorOptions options;
  options.filename = "<generated>";
  options.table_message_name = "Table";
  options.table_row_field_name = "row";
  options.convert_type_to_proto_options.message_name = "TheRow";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const IteratorProtoDescriptors desc,
                       ConvertIteratorToProto(*iter, options, pool));

  EXPECT_NE(desc.table, nullptr);
  EXPECT_NE(desc.row, nullptr);

  EXPECT_EQ(pool.FindMessageTypeByName("Table"), desc.table);
  EXPECT_EQ(pool.FindMessageTypeByName("TheRow"), desc.row);

  EXPECT_EQ(desc.table->file()->name(), "<generated>");
  EXPECT_EQ(desc.table->full_name(), "Table");
  EXPECT_EQ(desc.table->field_count(), 1);
  EXPECT_EQ(desc.table->field(0), desc.table->FindFieldByNumber(1));
  EXPECT_EQ(desc.table->field(0)->name(), "row");
  EXPECT_EQ(desc.table->field(0)->type(),
            google::protobuf::FieldDescriptor::TYPE_MESSAGE);
  EXPECT_EQ(desc.table->field(0)->message_type(), desc.row);
  EXPECT_TRUE(desc.table->field(0)->is_repeated());

  EXPECT_EQ(desc.row->full_name(), "TheRow");
  EXPECT_EQ(desc.row->field_count(), 3);
  EXPECT_EQ(desc.row->field(0)->name(), "bool_val");
  EXPECT_EQ(desc.row->field(1)->name(), "int64_val");
  EXPECT_EQ(desc.row->field(2)->name(), "string_val");

  // Try to add same proto to pool once more
  EXPECT_THAT(ConvertIteratorToProto(*iter, options, pool),
              StatusIs(absl::StatusCode::kUnknown,
                       HasSubstr("Building file descriptor failed")));
}

TEST(ConvertIteratorToProtoTest, WithOptions) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"names", types::StringArrayType()},
  };

  SimpleTable table{"AnotherTable", columns};
  table.SetContents({});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const EvaluatorTableIterator> iter,
                       table.CreateEvaluatorTableIterator({0}));

  google::protobuf::DescriptorPool pool{google::protobuf::DescriptorPool::generated_pool()};

  IteratorProtoDescriptorOptions options;
  options.filename = "foobar.proto";
  options.package = "testPkg";
  options.table_message_name = "testTable";
  options.table_row_field_number = 26831;
  options.table_row_field_name = "testField";
  options.convert_type_to_proto_options.message_name = "testRow";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const IteratorProtoDescriptors desc,
                       ConvertIteratorToProto(*iter, options, pool));

  EXPECT_NE(desc.table, nullptr);
  EXPECT_NE(desc.row, nullptr);

  EXPECT_EQ(desc.table->file()->name(), "foobar.proto");
  EXPECT_EQ(desc.table->full_name(), "testPkg.testTable");
  EXPECT_EQ(desc.table->field_count(), 1);
  EXPECT_EQ(desc.table->field(0)->name(), "testField");
  EXPECT_EQ(desc.table->field(0)->number(), 26831);

  EXPECT_EQ(desc.row->full_name(), "testPkg.testRow");
  EXPECT_EQ(desc.row->field_count(), 1);
  EXPECT_EQ(desc.row->field(0)->name(), "names");
}

TEST(MergeRowToProtoTest, Basic) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"int32_val", types::Int32Type()},
      {"int64_key_2", types::Int64Type()},
      {"int64_key_1", types::Int64Type()},
  };

  SimpleTable table{"WorldTable", columns};
  table.SetContents({
      {values::Int32(100), values::Int64(200), values::Int64(300)},
  });

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       table.CreateEvaluatorTableIterator({2, 1, 0}));

  google::protobuf::DynamicMessageFactory message_factory;
  message_factory.SetDelegateToGeneratedFactory(true);

  KitchenSinkPB msg;

  EXPECT_TRUE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(MergeRowToProto(*iter, message_factory, msg));
  EXPECT_FALSE(iter->NextRow());
  GOOGLESQL_EXPECT_OK(iter->Status());

  EXPECT_THAT(msg, testing::EqualsProto(R"pb(
                int64_key_1: 300
                int64_key_2: 200
                int32_val: 100
              )pb"));
}

TEST(ProtoFromIteratorTest, Basic) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"key", types::Int32Type()},
      {"name", types::StringType()},
      {"tag", types::StringArrayType()},
  };

  SimpleTable table{"DataTable", columns};
  table.SetContents({
      {values::Int32(100), values::String("foo"),
       values::StringArray({"aaa", "zzz"})},
      {values::Int32(200), values::String("bar"), values::StringArray({"bbb"})},
      {values::Int32(0), values::String(""),
       values::EmptyArray(types::StringArrayType())},
  });

  constexpr char want[] = R"pb(
    row { key: 100 name: "foo" tag: "aaa" tag: "zzz" }
    row { key: 200 name: "bar" tag: "bbb" }
    row { key: 0 name: "" }
  )pb";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       table.CreateEvaluatorTableIterator({0, 1, 2}));

  google::protobuf::DescriptorPool pool{google::protobuf::DescriptorPool::generated_pool()};

  IteratorProtoDescriptorOptions options;

  options.filename = "myfile";
  options.filename = "<generated>";
  options.table_message_name = "Table";
  options.table_row_field_name = "row";
  options.convert_type_to_proto_options.message_name = "Row";

  // Disable nested structures for testing against pre-defined protobuf
  {
    auto& cttpo = options.convert_type_to_proto_options;
    cttpo.generate_nullable_array_wrappers = false;
    cttpo.generate_nullable_element_wrappers = false;
  }

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const IteratorProtoDescriptors desc,
                       ConvertIteratorToProto(*iter, options, pool));

  google::protobuf::DynamicMessageFactory message_factory;
  message_factory.SetDelegateToGeneratedFactory(true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<google::protobuf::Message> msg,
                       ProtoFromIterator(*iter, *desc.table, message_factory));

  std::string buf;
  EXPECT_TRUE(google::protobuf::TextFormat::PrintToString(*msg, &buf));

  IteratorProto got;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(buf, &got));
  EXPECT_THAT(got, testing::EqualsProto(want));
}

}  // namespace googlesql
