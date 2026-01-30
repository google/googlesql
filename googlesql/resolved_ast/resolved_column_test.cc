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

#include "googlesql/resolved_ast/resolved_column.h"

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status.h"

namespace googlesql {

static void CreateTestAnnotationMap(const Type* type, TypeFactory* type_factory,
                                    const AnnotationMap** annotation_map) {
  std::unique_ptr<AnnotationMap> annotation = AnnotationMap::Create(type);
  annotation->SetAnnotation(CollationAnnotation::GetId(),
                            SimpleValue::Int64(10000));
  absl::StatusOr<const AnnotationMap*> statusOrMap =
      type_factory->TakeOwnership(std::move(annotation));
  GOOGLESQL_CHECK_OK(statusOrMap.status());
  *annotation_map = statusOrMap.value();
}

TEST(ResolvedColumnTest, Test) {
  TypeFactory type_factory;
  const AnnotationMap* annotation_map;
  CreateTestAnnotationMap(type_factory.get_int64(), &type_factory,
                          &annotation_map);
  ResolvedColumn c1(1, googlesql::IdString::MakeGlobal("T1"),
                    googlesql::IdString::MakeGlobal("C1"),
                    type_factory.get_int32());
  ResolvedColumn c2(2, IdString::MakeGlobal("T1"), IdString::MakeGlobal("C2"),
                    AnnotatedType(type_factory.get_int64(), annotation_map));
  ResolvedColumn c3(3, googlesql::IdString::MakeGlobal("T2"),
                    googlesql::IdString::MakeGlobal("C3"),
                    type_factory.get_uint32());

  EXPECT_EQ(c1, c1);
  EXPECT_FALSE(c1 == c2);
  EXPECT_FALSE(c3 == c2);
  EXPECT_TRUE(c1 != c2);
  EXPECT_TRUE(c3 != c2);
  EXPECT_FALSE(c1 != c1);
  EXPECT_NE(c1, c2);
  EXPECT_NE(c2, c3);

  ResolvedColumn c2_alternate(2, googlesql::IdString::MakeGlobal("XXX"),
                              googlesql::IdString::MakeGlobal("YYY"),
                              type_factory.get_double());
  EXPECT_TRUE(c2 == c2_alternate);  // Equality on column_id only.
  EXPECT_FALSE(c2 != c2_alternate);  // Inequality is based on column_id only.

  EXPECT_EQ("T1.C1#1", c1.DebugString());
  EXPECT_EQ("C1#1", c1.ShortDebugString());

  // Table name gets factored out of ResolvedColumnListToString if we have
  // multiple columns that all come from the same table.
  EXPECT_EQ("[]", ResolvedColumnListToString(ResolvedColumnList{}));
  EXPECT_EQ("[T1.C1#1]", ResolvedColumnListToString(ResolvedColumnList{c1}));
  EXPECT_EQ("T1.[C1#1, C2#2]",
            ResolvedColumnListToString(ResolvedColumnList{c1, c2}));
  EXPECT_EQ("[T1.C1#1, T1.C2#2{Collation:10000}, T2.C3#3]",
            ResolvedColumnListToString(ResolvedColumnList{c1, c2, c3}));

  // Make sure a set of ResolvedColumns works.
  std::set<ResolvedColumn> columns;
  columns.insert(c3);
  columns.insert(c1);
  columns.insert(c2);
  EXPECT_EQ(3, columns.size());
  // Uniqueness computed on column_id only.
  columns.insert(ResolvedColumn(2, googlesql::IdString::MakeGlobal("XX"),
                                googlesql::IdString::MakeGlobal("YY"),
                                type_factory.get_float()));
  EXPECT_EQ(3, columns.size());
}

TEST(ResolvedColumnTest, SaveTo) {
  TypeFactory type_factory;

  const AnnotationMap* annotation_map;
  CreateTestAnnotationMap(type_factory.get_int32(), &type_factory,
                          &annotation_map);

  ResolvedColumn c1(1, IdString::MakeGlobal("T1"), IdString::MakeGlobal("C1"),
                    AnnotatedType(type_factory.get_int32(), annotation_map));

  FileDescriptorSetMap map;
  ResolvedColumnProto proto;
  GOOGLESQL_CHECK_OK(c1.SaveTo(&map, &proto));
  EXPECT_EQ("T1", proto.table_name());
  EXPECT_EQ("C1", proto.name());
  EXPECT_EQ(1, proto.column_id());
  EXPECT_EQ(TypeKind::TYPE_INT32, proto.type().type_kind());
  EXPECT_EQ(1, proto.annotation_map().annotations_size());
  EXPECT_EQ(CollationAnnotation::GetId(),
            proto.annotation_map().annotations(0).id());
  EXPECT_EQ(10000, proto.annotation_map().annotations(0).value().int64_value());

  const ProtoType* proto_type;
  GOOGLESQL_CHECK_OK(type_factory.MakeProtoType(TypeProto::descriptor(), &proto_type));
  ResolvedColumn c2(2, googlesql::IdString::MakeGlobal("T1"),
                    googlesql::IdString::MakeGlobal("C2"), proto_type);
  ResolvedColumnProto proto2;
  GOOGLESQL_CHECK_OK(c2.SaveTo(&map, &proto2));
  EXPECT_EQ("T1", proto2.table_name());
  EXPECT_EQ("C2", proto2.name());
  EXPECT_EQ(2, proto2.column_id());
  EXPECT_EQ(TypeKind::TYPE_PROTO, proto2.type().type_kind());
  EXPECT_EQ("googlesql.TypeProto", proto2.type().proto_type().proto_name());
}

TEST(ResolvedColumnTest, RestoreFrom) {
  TypeFactory type_factory;

  const AnnotationMap* annotation_map;
  CreateTestAnnotationMap(type_factory.get_int32(), &type_factory,
                          &annotation_map);

  ResolvedColumn c1(1, IdString::MakeGlobal("T1"), IdString::MakeGlobal("C1"),
                    AnnotatedType(type_factory.get_int32(), annotation_map));

  FileDescriptorSetMap map;
  ResolvedColumnProto proto;
  GOOGLESQL_CHECK_OK(c1.SaveTo(&map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  for (const auto& elem : map) pools.push_back(elem.first);

  IdStringPool id_string_pool;
  ResolvedNode::RestoreParams params(
      pools, nullptr, &type_factory, &id_string_pool);

  auto c2 = ResolvedColumn::RestoreFrom(proto, params).value();
  EXPECT_EQ(c1.DebugString(), c2.DebugString());
  EXPECT_EQ(c1.type(), c2.type());
}

TEST(ResolvedColumnTest, ClassAndProtoSize) {
  EXPECT_EQ(24, sizeof(ResolvedNode))
      << "The size of ResolvedNode class has changed, please also update the "
      << "proto and serialization code if you added/removed fields in it.";
  // sizeof(ResolvedColumn) is 56 bytes in debug mode, including 4 bytes of
  // padding. (In NDEBUG mode, the size is 40 bytes, since IdString is only 8
  // bytes).
  EXPECT_LE(sizeof(ResolvedColumn), 56)
      << "The size of ResolvedColumn class has changed, please also update the "
      << "proto and serialization code if you added/removed fields in it.";
  EXPECT_EQ(1, ResolvedNodeProto::descriptor()->field_count())
      << "The number of fields in ResolvedNodeProto has changed, please also "
      << "update the serialization code accordingly.";
  EXPECT_EQ(5, ResolvedColumnProto::descriptor()->field_count())
      << "The number of fields in ResolvedColumnProto has changed, please also "
      << "update the serialization code accordingly.";
}

}  // namespace googlesql
