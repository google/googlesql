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

#include "googlesql/analyzer/container_hash_equals.h"

#include <vector>

#include "googlesql/public/id_string.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {

// Shorthand to construct (and leak) a global IdString.
static IdString ID(absl::string_view str) { return IdString::MakeGlobal(str); }

TEST(ContainerHash, StdUnorderedMapOfVector) {
  std::vector<IdString> values1 = {ID("abc"), ID("def")};
  std::vector<IdString> values2 = {ID("Abc"), ID("Def")};
  std::vector<IdString> values3 = {ID("abc"), ID("defg")};

  absl::flat_hash_map<
      std::vector<IdString>, int,
      ContainerHash<std::vector<IdString>, IdStringCaseHash>,
      ContainerEquals<std::vector<IdString>, IdStringCaseEqualFunc>>
      m;

  m[values1] = 1;
  ASSERT_TRUE(m.contains(values1));
  ASSERT_TRUE(m.contains(values2));
  ASSERT_FALSE(m.contains(values3));
  ASSERT_EQ(m.at(values2), 1);
  m.erase(values2);
  ASSERT_FALSE(m.contains(values1));
}

TEST(ContainerHash, AbslFlatHashMapOfStdVector) {
  std::vector<IdString> values1 = {ID("abc"), ID("def")};
  std::vector<IdString> values2 = {ID("Abc"), ID("Def")};
  std::vector<IdString> values3 = {ID("abc"), ID("defg")};

  absl::flat_hash_map<
      std::vector<IdString>, int,
      ContainerHash<std::vector<IdString>, IdStringCaseHash>,
      ContainerEquals<std::vector<IdString>, IdStringCaseEqualFunc>>
      m;

  m[values1] = 1;
  ASSERT_TRUE(m.contains(values1));
  ASSERT_TRUE(m.contains(values2));
  ASSERT_FALSE(m.contains(values3));
  ASSERT_EQ(m.at(values2), 1);
  m.erase(values2);
  ASSERT_FALSE(m.contains(values1));
}

TEST(ContainerHash, AbslFlatHashMapOfAbslSpan) {
  std::vector<IdString> values1 = {ID("abc"), ID("def")};
  std::vector<IdString> values2 = {ID("Abc"), ID("Def")};
  std::vector<IdString> values3 = {ID("abc"), ID("defg")};

  absl::Span<IdString> span1 = absl::MakeSpan(values1);
  absl::Span<IdString> span2 = absl::MakeSpan(values2);
  absl::Span<IdString> span3 = absl::MakeSpan(values3);

  absl::flat_hash_map<
      absl::Span<IdString>, int,
      ContainerHash<absl::Span<IdString>, IdStringCaseHash>,
      ContainerEquals<absl::Span<IdString>, IdStringCaseEqualFunc>>
      m;

  m[span1] = 1;
  ASSERT_TRUE(m.contains(span1));
  ASSERT_TRUE(m.contains(span2));
  ASSERT_FALSE(m.contains(span3));
  ASSERT_EQ(m.at(span2), 1);
  m.erase(span2);
  ASSERT_FALSE(m.contains(span1));
}

TEST(ContainerHash, AbslFlatHashMapOfAbslConstSpan) {
  std::vector<IdString> values1 = {ID("abc"), ID("def")};
  std::vector<IdString> values2 = {ID("Abc"), ID("Def")};
  std::vector<IdString> values3 = {ID("abc"), ID("defg")};

  absl::Span<const IdString> span1 = absl::MakeConstSpan(values1);
  absl::Span<const IdString> span2 = absl::MakeConstSpan(values2);
  absl::Span<const IdString> span3 = absl::MakeConstSpan(values3);

  absl::flat_hash_map<
      absl::Span<const IdString>, int,
      ContainerHash<absl::Span<const IdString>, IdStringCaseHash>,
      ContainerEquals<absl::Span<const IdString>, IdStringCaseEqualFunc>>
      m;

  m[span1] = 1;
  ASSERT_TRUE(m.contains(span1));
  ASSERT_TRUE(m.contains(span2));
  ASSERT_FALSE(m.contains(span3));
  ASSERT_EQ(m.at(span2), 1);
  m.erase(span2);
  ASSERT_FALSE(m.contains(span1));
}

TEST(ContainerHash, CustomHashButDefaultEquals) {
  std::vector<IdString> values1 = {ID("abc"), ID("def")};
  std::vector<IdString> values2 = {ID("Abc"), ID("Def")};
  std::vector<IdString> values3 = {ID("abc"), ID("defg")};

  // operator==() uses case-sensitive comparison. Try that with overridden
  // case-insensitive hash to make sure that the custom hash and default
  // Equals() combination works.
  absl::flat_hash_map<std::vector<IdString>, int,
                      ContainerHash<std::vector<IdString>, IdStringCaseHash>>
      m;

  m[values1] = 1;
  ASSERT_TRUE(m.contains(values1));
  ASSERT_FALSE(m.contains(values2));
  ASSERT_FALSE(m.contains(values3));
  m[values2] = 2;
  ASSERT_EQ(m.at(values1), 1);
  ASSERT_EQ(m.at(values2), 2);
  m.erase(values2);
  ASSERT_TRUE(m.contains(values1));
}

}  // namespace
}  // namespace googlesql
