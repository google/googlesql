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

#include "googlesql/resolved_ast/resolved_ast_comparator.h"

#include <memory>
#include <string>
#include <utility>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

using ::absl_testing::IsOkAndHolds;

std::unique_ptr<AnnotationMap> MakeSimpleCollation(std::string collation_name) {
  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation<CollationAnnotation>(
      SimpleValue::String(std::move(collation_name)));
  return annotation_map;
}

std::unique_ptr<ResolvedLiteral> MakeStringLiteral(
    const AnnotationMap* annotation_map) {
  auto literal =
      MakeResolvedLiteral(types::StringType(), Value::String("value"));
  if (annotation_map != nullptr) {
    literal->set_type_annotation_map(annotation_map);
  }
  return literal;
}

TEST(ResolvedAstComparatorTest, StringLiteralWithCollationAreEqual) {
  auto map1 = MakeSimpleCollation("und:ci");
  auto map2 = MakeSimpleCollation("und:ci");
  EXPECT_NE(map1.get(), map2.get());  // different instances

  auto literal1 = MakeStringLiteral(map1.get());
  auto literal2 = MakeStringLiteral(map2.get());

  EXPECT_THAT(
      ResolvedASTComparator::CompareResolvedAST(literal1.get(), literal2.get()),
      IsOkAndHolds(true));
}

TEST(ResolvedAstComparatorTest, StringLiteralWithCollationAreNotEqual) {
  auto map1 = MakeSimpleCollation("und:ci");
  auto map2 = MakeSimpleCollation("und:cs");

  auto literal1 = MakeStringLiteral(map1.get());
  auto literal2 = MakeStringLiteral(map2.get());

  EXPECT_THAT(
      ResolvedASTComparator::CompareResolvedAST(literal1.get(), literal2.get()),
      IsOkAndHolds(false));
}

}  // namespace
}  // namespace googlesql
