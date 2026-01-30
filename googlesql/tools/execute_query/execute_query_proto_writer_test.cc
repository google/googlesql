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

#include "googlesql/tools/execute_query/execute_query_proto_writer.h"

#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/text_format.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using googlesql_test::IteratorProto;
using googlesql_test::TestStatusPayload;
using testing::EqualsProto;

namespace {

MATCHER_P(JsonEq, expected, expected.ToString()) {
  *result_listener << arg.ToString();
  return arg.NormalizedEquals(expected);
}

absl::Status RunWriter(
    const SimpleTable& table, const std::vector<int> col_idxs,
    const std::function<absl::Status(const google::protobuf::Message& msg)> fn) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableIterator> iter,
                   table.CreateEvaluatorTableIterator(col_idxs));

  ExecuteQueryStreamProtobufWriter writer{
      google::protobuf::DescriptorPool::generated_pool(), fn};

  return writer.executed(*MakeResolvedLiteral(), std::move(iter));
}

}  // namespace

TEST(ExecuteQueryStreamProtobufWriter, Executed) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"int64_val", types::Int64Type()},
      {"string_val", types::StringType()},
  };
  SimpleTable table{"DataTable", columns};
  table.SetContents({
      {values::Int64(123), values::String("test")},
      {values::Int64(456), values::String("test2")},
      {values::Int64(789), values::String("test3")},
  });

  // Select the columns out of order and with duplicates
  GOOGLESQL_EXPECT_OK(RunWriter(table, {1, 1, 0, 0}, [](const google::protobuf::Message& msg) {
    const google::protobuf::Descriptor* table_desc = msg.GetDescriptor();
    EXPECT_EQ(table_desc->field_count(), 1);

    const google::protobuf::FieldDescriptor* row_field =
        table_desc->FindFieldByName("row");
    EXPECT_NE(row_field, nullptr);

    const google::protobuf::Reflection* table_ref = msg.GetReflection();
    const auto rows =
        table_ref->GetRepeatedFieldRef<google::protobuf::Message>(msg, row_field);

    EXPECT_EQ(rows.size(), 3);

    for (const google::protobuf::Message& row : rows) {
      const google::protobuf::Descriptor* row_desc = row.GetDescriptor();

      EXPECT_EQ(row_desc->field_count(), 4);
      EXPECT_EQ(row_desc->field(0)->name(), "string_val");
      EXPECT_EQ(row_desc->field(0)->type(),
                google::protobuf::FieldDescriptor::TYPE_STRING);
      EXPECT_EQ(row_desc->field(1)->name(), "_field_2");
      EXPECT_EQ(row_desc->field(1)->type(),
                google::protobuf::FieldDescriptor::TYPE_STRING);
      EXPECT_EQ(row_desc->field(2)->name(), "int64_val");
      EXPECT_EQ(row_desc->field(2)->type(),
                google::protobuf::FieldDescriptor::TYPE_INT64);
      EXPECT_EQ(row_desc->field(3)->name(), "_field_4");
      EXPECT_EQ(row_desc->field(3)->type(),
                google::protobuf::FieldDescriptor::TYPE_INT64);
    }

    return absl::OkStatus();
  }));
}

TEST(ExecuteQueryWriteTextprotoTest, Basic) {
  const std::vector<SimpleTable::NameAndType> columns{
      {"key", types::Int32Type()},
      {"name", types::StringType()},
      {"tag", types::StringArrayType()},
  };
  SimpleTable table{"DataTable", columns};
  table.SetContents({
      {values::Int32(0), values::String(""),
       values::EmptyArray(types::StringArrayType())},
      {values::Int32(100), values::String("foo"),
       values::StringArray({"aaa", "zzz"})},
      {values::Int32(200), values::String("bar"), values::StringArray({"bbb"})},
  });

  constexpr char want[] = R"pb(
    row { key: 0 name: "" }
    row { key: 100 name: "foo" tag: "aaa" tag: "zzz" }
    row { key: 200 name: "bar" tag: "bbb" }
  )pb";

  std::ostringstream buf;
  GOOGLESQL_EXPECT_OK(RunWriter(table, {0, 1, 2}, [&buf](const google::protobuf::Message& msg) {
    return ExecuteQueryWriteTextproto(msg, buf);
  }));

  IteratorProto got;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(buf.str(), &got));
  EXPECT_THAT(got, testing::EqualsProto(want));
}

TEST(ExecuteQueryJsonWriterTest, Basic) {
  TypeFactory type_factory;

  const ProtoType* proto_type;
  GOOGLESQL_EXPECT_OK(
      type_factory.MakeProtoType(TestStatusPayload::descriptor(), &proto_type));

  const ProtoType* any_type;
  GOOGLESQL_EXPECT_OK(type_factory.MakeProtoType(google::protobuf::Any::descriptor(),
                                       &any_type));

  TestStatusPayload proto_type_msg;
  proto_type_msg.set_value("foobar");

  google::protobuf::Any any_msg;
  any_msg.set_type_url(
      absl::StrCat("type.googleapis.com/", proto_type_msg.GetTypeName()));
  any_msg.set_value(proto_type_msg.SerializeAsString());

  const std::vector<SimpleTable::NameAndType> columns{
      {"key", types::Int32Type()},
      {"name", types::StringType()},
      {"tag", types::StringArrayType()},
      {"date", types::DateType()},
      {"msg", proto_type},
      {"any", any_type},
  };

  SimpleTable table{"DataTable", columns};
  table.SetContents({
      {
          values::Int32(0),
          values::String(""),
          values::Null(types::StringArrayType()),
          values::Date(123),
          values::Proto(proto_type, proto_type_msg),
          values::Null(any_type),
      },
      {
          values::Int32(0),
          values::String(""),
          values::EmptyArray(types::StringArrayType()),
          values::Date(0),
          values::Proto(proto_type, TestStatusPayload{}),
          values::Null(any_type),
      },
      {
          values::Int32(100),
          values::String("foo"),
          values::StringArray({"aaa", "zzz"}),
          values::Null(types::DateType()),
          values::Null(proto_type),
          values::Proto(any_type, any_msg),
      },
      {
          values::Int32(200),
          values::String("bar"),
          values::StringArray({"bbb"}),
          values::NullDate(),
          values::Proto(proto_type, TestStatusPayload{}),
          values::Null(any_type),
      },
  });

  std::ostringstream buf;
  GOOGLESQL_EXPECT_OK(
      RunWriter(table, {5, 0, 2, 1, 4, 3}, [&buf](const google::protobuf::Message& msg) {
        return ExecuteQueryWriteJson(msg, buf);
      }));

  const std::string want = R"json(
      {
        "row": [
          {
            "key": 0,
            "name": "",
            "tag": [],
            "date": 123,
            "msg": {
              "value": "foobar"
            }
          },
          {
            "key": 0,
            "name": "",
            "tag": [],
            "date": 0,
            "msg": {}
          },
          {
            "key": 100,
            "name": "foo",
            "tag": [
              "aaa",
              "zzz"
            ],
            "any": {
              "@type": "type.googleapis.com/googlesql_test.TestStatusPayload",
              "value": "foobar"
            }
          },
          {
            "key": 200,
            "name": "bar",
            "tag": [
              "bbb"
            ],
            "msg": {}
          }
        ]
      }
      )json";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const JSONValue wantValue,
                       JSONValue::ParseJSONString(want));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const JSONValue gotValue,
                       JSONValue::ParseJSONString(buf.str()));

  EXPECT_THAT(gotValue.GetConstRef(), JsonEq(wantValue.GetConstRef()));
}

}  // namespace googlesql
