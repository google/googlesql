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

#include <memory>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/builtins_output_properties.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

class GetVectorSearchTableValuedFunctionsTest : public ::testing::Test {
 protected:
  GetVectorSearchTableValuedFunctionsTest() {
    language_options_.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    options_ =
        std::make_unique<GoogleSQLBuiltinFunctionOptions>(language_options_);
  }

  TypeFactory type_factory_;
  BuiltinsOutputProperties output_properties_;
  LanguageOptions language_options_;
  std::unique_ptr<GoogleSQLBuiltinFunctionOptions> options_;
};

TEST_F(GetVectorSearchTableValuedFunctionsTest, VectorSearchFunction) {
  NameToTableValuedFunctionMap functions;
  GOOGLESQL_ASSERT_OK(GetVectorSearchTableValuedFunctions(
      &type_factory_, *options_, &functions, output_properties_));
  constexpr absl::string_view kVectorSearch = "vector_search";
  ASSERT_TRUE(functions.contains(kVectorSearch));
  EXPECT_EQ(
      functions[kVectorSearch]->DebugString(),
      "GoogleSQL:vector_search\n  (ANY TABLE, STRING column_to_search, ANY "
      "TABLE, optional "
      "STRING query_column_to_search, optional INT64 top_k, optional STRING "
      "distance_type, optional DOUBLE max_distance) "
      "-> "
      "ANY TABLE");
  EXPECT_EQ(output_properties_.SupportsSuppliedArgumentType(
                FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS, 4),
            true);
}

}  // namespace
}  // namespace googlesql
