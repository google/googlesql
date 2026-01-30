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

#include "googlesql/common/type_visitors.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/testing/proto_matchers.h"  
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsNull;

namespace googlesql {

MATCHER_P2(TypeIs, expected_kind, components_matcher, "") {
  bool success = true;
  if (!ExplainMatchResult(Eq(expected_kind), arg->kind(), result_listener)) {
    success = false;
  }

  if (!ExplainMatchResult(components_matcher, arg->ComponentTypes(),
                          result_listener)) {
    success = false;
  }

  return success;
}

MATCHER_P(TypeIs, expected_kind, "") {
  return ExplainMatchResult(TypeIs(expected_kind, IsEmpty()), arg,
                            result_listener);
}

MATCHER(IsString, "") {
  return ExplainMatchResult(Eq(TYPE_STRING), arg->kind(), result_listener);
}

MATCHER(IsBytes, "") {
  return ExplainMatchResult(Eq(TYPE_BYTES), arg->kind(), result_listener);
}

MATCHER(IsArrayOfStrings, "") {
  return ExplainMatchResult(Eq(types::StringArrayType()), arg, result_listener);
}

MATCHER(IsArrayOfBytes, "") {
  return ExplainMatchResult(Eq(types::BytesArrayType()), arg, result_listener);
}

static absl::StatusOr<const AnnotationMap*> MakeCollationAnnotation(
    absl::string_view collation, TypeFactory& type_factory) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto annotation_map,
      Collation::MakeScalar(collation).ToAnnotationMap(types::StringType()));
  return type_factory.TakeOwnership(std::move(annotation_map));
}

static absl::StatusOr<StructAnnotationMap*> MakeCompositeAnnotation(
    const Type* type, TypeFactory& type_factory) {
  GOOGLESQL_RET_CHECK(!type->ComponentTypes().empty());
  auto owner = AnnotationMap::Create(type);

  GOOGLESQL_RET_CHECK(owner->IsStructMap());
  StructAnnotationMap* composite_annotation_map = owner->AsStructMap();

  GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* ptr,
                   type_factory.TakeOwnership(std::move(owner)));
  GOOGLESQL_RET_CHECK_EQ(ptr, composite_annotation_map);
  return composite_annotation_map;
}

static absl::StatusOr<AnnotatedType> MakeRichType(TypeFactory& type_factory) {
  // ARRAY<STRING{und:ci}>
  const Type* array_type = types::StringArrayType();
  GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* und_ci,
                   MakeCollationAnnotation("und:ci", type_factory));
  GOOGLESQL_ASSIGN_OR_RETURN(StructAnnotationMap * array_annotation,
                   MakeCompositeAnnotation(array_type, type_factory));
  GOOGLESQL_RETURN_IF_ERROR(array_annotation->CloneIntoField(0, und_ci));

  // MAP<ARRAY<STRING>, ARRAY<STRING{und:ci}>>
  // The key map is nullptr.
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type,
      type_factory.MakeMapType(array_type, array_type, language_options));
  GOOGLESQL_ASSIGN_OR_RETURN(StructAnnotationMap * map_annotation,
                   MakeCompositeAnnotation(map_type, type_factory));
  GOOGLESQL_RETURN_IF_ERROR(map_annotation->CloneIntoField(1, array_annotation));

  // MEASURE<STRING{de:ci}>
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* measure_type,
                   type_factory.MakeMeasureType(types::StringType()));
  GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* de_ci,
                   MakeCollationAnnotation("de:ci", type_factory));
  GOOGLESQL_ASSIGN_OR_RETURN(StructAnnotationMap * measure_annotation,
                   MakeCompositeAnnotation(measure_type, type_factory));
  GOOGLESQL_RETURN_IF_ERROR(measure_annotation->CloneIntoField(0, de_ci));

  // prototype
  googlesql_test::KitchenSinkPB kitchen_sink;
  const ProtoType* proto_type;
  GOOGLESQL_RET_CHECK_OK(
      type_factory.MakeProtoType(kitchen_sink.GetDescriptor(), &proto_type));

  // STRUCT<f1 MEASURE<STRING{de:ci}>, f2 MAP<STRING, ARRAY<STRING{und:ci}>>>
  const StructType* struct_type;
  std::vector<StructField> fields;
  fields.emplace_back("f1", measure_type);
  fields.emplace_back("f2", map_type);
  fields.emplace_back("f3", proto_type);
  GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(fields, &struct_type));

  GOOGLESQL_ASSIGN_OR_RETURN(StructAnnotationMap * struct_annotation,
                   MakeCompositeAnnotation(struct_type, type_factory));
  GOOGLESQL_RETURN_IF_ERROR(struct_annotation->CloneIntoField(0, measure_annotation));
  GOOGLESQL_RETURN_IF_ERROR(struct_annotation->CloneIntoField(1, map_annotation));

  return AnnotatedType(struct_type, struct_annotation);
}

// An example visitor which detects in any part of the type which collects all
// collation annotation values contained within the type or its components.
class CollationDetectorVisitor : public TypeVisitor {
 public:
  static absl::StatusOr<absl::flat_hash_set<std::string>> Process(
      AnnotatedType annotated_type) {
    absl::flat_hash_set<std::string> collations;
    CollationDetectorVisitor visitor(collations);
    GOOGLESQL_RETURN_IF_ERROR(visitor.Visit(annotated_type));
    return collations;
  }

  absl::Status PostVisit(AnnotatedType annotated_type) override {
    const AnnotationMap* annotation_map = annotated_type.annotation_map;
    if (annotation_map != nullptr) {
      const SimpleValue* collation =
          annotation_map->GetAnnotation(CollationAnnotation::GetId());
      if (collation != nullptr) {
        collations_.insert(collation->string_value());
      }
    }
    return absl::OkStatus();
  }

 private:
  explicit CollationDetectorVisitor(
      absl::flat_hash_set<std::string>& collations)
      : collations_(collations) {}
  absl::flat_hash_set<std::string>& collations_;
};

// An example rewriter which replaces any collated strings with BYTES, and
// removes the annotation.
class CollatedStringReplacer : public TypeRewriter {
 public:
  explicit CollatedStringReplacer(TypeFactory& type_factory)
      : TypeRewriter(type_factory) {}

  absl::StatusOr<AnnotatedType> PostVisit(
      AnnotatedType annotated_type) override {
    const auto& [type, annotation_map] = annotated_type;
    if (!type->IsString() || annotation_map == nullptr) {
      return annotated_type;
    }

    auto owned_annotation_map = annotation_map->Clone();
    owned_annotation_map->UnsetAnnotation(CollationAnnotation::GetId());
    GOOGLESQL_ASSIGN_OR_RETURN(
        const AnnotationMap* new_annotation_map,
        type_factory().TakeOwnership(std::move(owned_annotation_map)));
    return AnnotatedType(types::BytesType(), new_annotation_map);
  }
};

TEST(TypeVisitorTest, VisitsTypeRecursively) {
  auto type_factory = std::make_unique<TypeFactory>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(AnnotatedType rich_type, MakeRichType(*type_factory));

  absl::flat_hash_set<std::string> collations;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(collations,
                       CollationDetectorVisitor::Process(rich_type));
  EXPECT_THAT(collations, ::testing::UnorderedElementsAre("und:ci", "de:ci"));
}

TEST(TypeRewriterTest, RewritesTypeRecursively) {
  auto type_factory = std::make_unique<TypeFactory>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(AnnotatedType rich_type, MakeRichType(*type_factory));

  CollatedStringReplacer rewriter(*type_factory);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(AnnotatedType rewritten_type, rewriter.Visit(rich_type));
  // All collations removed.
  ASSERT_THAT(rewritten_type.annotation_map, IsNull());
  ASSERT_THAT(
      rewritten_type.type,
      TypeIs(TYPE_STRUCT,
             ElementsAre(TypeIs(TYPE_MEASURE, ElementsAre(IsBytes())),
                         // Note: the map's key doesn't change to BYTES as it
                         // has no collation.
                         TypeIs(TYPE_MAP, ElementsAre(IsArrayOfStrings(),
                                                      IsArrayOfBytes())),
                         TypeIs(TYPE_PROTO))));
}

}  // namespace googlesql
