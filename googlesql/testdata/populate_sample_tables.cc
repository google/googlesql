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

#include "googlesql/testdata/populate_sample_tables.h"

#include <string>

#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "absl/status/status.h"
#include "absl/time/civil_time.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using googlesql_test::KitchenSinkPB;

using googlesql::test_values::Struct;

using googlesql::values::Bytes;
using googlesql::values::Date;
using googlesql::values::Int32;
using googlesql::values::Int64;
using googlesql::values::Proto;
using googlesql::values::String;
using googlesql::values::TimestampFromUnixMicros;
using googlesql::values::Uint32;
using googlesql::values::Uint64;

absl::Status PopulateSampleTables(TypeFactory* type_factory,
                                  SampleCatalog* catalog) {
  // Table TwoIntegers
  catalog->GetTableOrDie("TwoIntegers")
      ->SetContents({{Int64(1), Int64(2)}, {Int64(3), Int64(4)}});

  // Table FourIntegers
  catalog->GetTableOrDie("FourIntegers")
      ->SetContents({{Int64(1), Int64(2), Int64(3), Int64(4)},
                     {Int64(5), Int64(6), Int64(7), Int64(8)}});

  // Table Int64ValueTable
  catalog->GetTableOrDie("Int64ValueTable")
      ->SetContents({{Int64(50)}, {Int64(51)}, {Int64(52)}});

  // Table KeyValue
  catalog->GetTableOrDie("KeyValue")
      ->SetContents({{Int64(1), String("Value1")},
                     {Int64(2), String("Value2")},
                     {Int64(3), String("Value3")},
                     {Int64(4), String("Value4")}});

  // Table KeyValueReadTimeIgnored
  catalog->GetTableOrDie("KeyValueReadTimeIgnored")
      ->SetContents({{Int64(5), String("Value5")},
                     {Int64(6), String("Value6")},
                     {Int64(7), String("Value7")},
                     {Int64(8), String("Value8")}});

  // Table KeyValue2 : Key isn't actually a key.
  catalog->GetTableOrDie("KeyValue2")
      ->SetContents({{Int64(1), String("Value1")},
                     {Int64(1), String("Value2")},
                     {Int64(2), String("Value3")},
                     {Int64(2), String("Value4")}});

  catalog->GetTableOrDie("TableWithDefaultColumn")
      ->SetContents(
          {{Int64(1), Int64(1), Int64(100)}, {Int64(2), Int64(2), Int64(200)}});

  const Type* kitchen_sink_pb_type;
  GOOGLESQL_RETURN_IF_ERROR(catalog->catalog()->FindType(
      {std::string(KitchenSinkPB::descriptor()->full_name())},
      &kitchen_sink_pb_type));

  KitchenSinkPB proto1;
  proto1.set_int64_key_1(1);
  proto1.set_int64_key_2(2);

  KitchenSinkPB proto2;
  proto2.set_int64_key_1(10);
  proto2.set_int64_key_2(20);

  const Value proto1_value = Proto(kitchen_sink_pb_type->AsProto(), proto1);
  const Value proto2_value = Proto(kitchen_sink_pb_type->AsProto(), proto2);

  // Table KitchenSinkValueTable
  catalog->GetTableOrDie("KitchenSinkValueTable")
      ->SetContents({{proto1_value}, {proto2_value}});

  // Table ComplexTypes
  const Type *enum_type, *test_extra_pb_type;
  GOOGLESQL_RETURN_IF_ERROR(catalog->catalog()->FindType(
      {std::string(googlesql_test::TestEnum_descriptor()->full_name())},
      &enum_type));
  GOOGLESQL_RETURN_IF_ERROR(catalog->catalog()->FindType(
      {std::string(googlesql_test::TestExtraPB::descriptor()->full_name())},
      &test_extra_pb_type));
  KitchenSinkPB proto;
  proto.set_int64_key_1(1);
  proto.set_int64_key_2(2);
  catalog->GetTableOrDie("ComplexTypes")
      ->SetContents(
          {{Int32(1), Value::Enum(enum_type->AsEnum(), 2),
            Proto(kitchen_sink_pb_type->AsProto(), proto),
            values::Int32Array({3, 5, 7}),
            Struct({"c", "d"},
                   {Int32(10), Struct({"a", "b"}, {Int32(12), String("Twelve")},
                                      type_factory)},
                   type_factory),
            Value::Null(test_extra_pb_type->AsProto())}});

  catalog->GetTableOrDie("Person")->SetContents(
      {{Int64(1), String("Person1"), String("male"),
        Date(absl::CivilDay(2000, 1, 2)), String("person1@google.com"),
        Uint32(10), Bytes("001")},
       {Int64(2), String("Person2"), String("female"),
        Date(absl::CivilDay(2000, 1, 3)), String("person2@google.com"),
        Uint32(20), Bytes("002")},
       {Int64(3), String("Person3"), String("female"),
        Date(absl::CivilDay(2000, 1, 4)), String("person3@google.com"),
        Uint32(30), Bytes("003")},
       {Int64(4), String("Person4"), String("female"),
        Date(absl::CivilDay(2000, 1, 2)), String("person4@google.com"),
        Uint32(40), Bytes("004")}});

  catalog->GetTableOrDie("Account")->SetContents(
      {{Int64(1), String("Account1"), Uint64(100)},
       {Int64(2), String("Account2"), Uint64(200)},
       {Int64(3), String("Account3"), Uint64(300)},
       {Int64(4), String("Account4"), Uint64(400)},
       {Int64(5), String("Account5"), Uint64(500)},
       {Int64(6), String("Account6"), Uint64(600)},
       {Int64(7), String("Account7"), Uint64(700)}});

  catalog->GetTableOrDie("Syndicate")->SetContents({});

  catalog->GetTableOrDie("PersonOwnAccount")
      ->SetContents({
          {Int64(1), Int64(1), TimestampFromUnixMicros(1000)},
          {Int64(1), Int64(2), TimestampFromUnixMicros(2000)},
          {Int64(2), Int64(2), TimestampFromUnixMicros(3000)},
          {Int64(3), Int64(3), TimestampFromUnixMicros(4000)},
          {Int64(4), Int64(4), TimestampFromUnixMicros(4000)},
          {Int64(4), Int64(5), TimestampFromUnixMicros(4000)},
          {Int64(4), Int64(6), TimestampFromUnixMicros(5000)},
          {Int64(4), Int64(7), TimestampFromUnixMicros(6000)},
      });

  catalog->GetTableOrDie("Transfer")
      ->SetContents({
          {Int64(1), Int64(1), Int64(2), TimestampFromUnixMicros(1000),
           Uint64(10)},
          {Int64(2), Int64(2), Int64(3), TimestampFromUnixMicros(2000),
           Uint64(20)},
          {Int64(3), Int64(3), Int64(4), TimestampFromUnixMicros(3000),
           Uint64(30)},
          {Int64(4), Int64(2), Int64(1), TimestampFromUnixMicros(4000),
           Uint64(40)},
      });
  return absl::OkStatus();
}
}  // namespace googlesql
