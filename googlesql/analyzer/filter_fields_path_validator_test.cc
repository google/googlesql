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

#include "googlesql/analyzer/filter_fields_path_validator.h"

#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

using googlesql_test::KitchenSinkPB;
using google::protobuf::DescriptorPool;
using absl_testing::StatusIs;

class FilterFieldsPathValidatorTest : public ::testing::Test {};

TEST_F(FilterFieldsPathValidatorTest, ValidateIncludeFieldPaths) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  // Repeated field
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("repeated_string_val")}));

  // Nested field
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("value")}));

  // Extension
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       DescriptorPool::generated_pool()->FindExtensionByName(
           "googlesql_test.KitchenSinkPB.nested_extension_int64")}));
}

TEST_F(FilterFieldsPathValidatorTest, ValidateExcludeFieldPaths) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  // Repeated field
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("repeated_string_val")}));

  // Nested field
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("value")}));

  // Extension
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       DescriptorPool::generated_pool()->FindExtensionByName(
           "googlesql_test.KitchenSinkPB.nested_extension_int64")}));
}

TEST_F(FilterFieldsPathValidatorTest, ValidateOverridingFieldPaths) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value")}));

  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("value")}));

  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       DescriptorPool::generated_pool()->FindExtensionByName(
           "googlesql_test.KitchenSinkPB.nested_extension_int64")}));
}

TEST_F(FilterFieldsPathValidatorTest,
       FailWhenAddingDuplicatePathsWithSameSign) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  EXPECT_THAT(validator.ValidateFieldPath(
                  /*include=*/true,
                  {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest,
       FailWhenAddingDuplicatePathsWithReverseSign) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  EXPECT_THAT(validator.ValidateFieldPath(
                  /*include=*/false,
                  {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenPathOrderIsWrong) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("value")}));

  EXPECT_THAT(
      validator.ValidateFieldPath(
          /*include=*/true, {KitchenSinkPB::descriptor()->FindFieldByName(
                                "nested_repeated_value")}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenNotOverridingParent) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value")}));

  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName("nested_repeated_value"),
       DescriptorPool::generated_pool()->FindExtensionByName(
           "googlesql_test.KitchenSinkPB.nested_extension_int64")}));

  EXPECT_THAT(
      validator.ValidateFieldPath(
          /*include=*/true,
          {KitchenSinkPB::descriptor()->FindFieldByName(
               "nested_repeated_value"),
           KitchenSinkPB::Nested::descriptor()->FindFieldByName("value")}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenNotOverridingTopLevelMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int64_val")}));

  EXPECT_THAT(
      validator.ValidateFieldPath(
          /*include=*/false, {KitchenSinkPB::descriptor()->FindFieldByName(
                                 "nested_repeated_value")}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenClearingRequiredFields) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(
      validator.ValidateFieldPath(
          /*include=*/false,
          {KitchenSinkPB::descriptor()->FindFieldByName("int64_key_1")}));
  EXPECT_THAT(validator.FinalValidation(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenClearingRequiredMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_required_value")}));
  EXPECT_THAT(validator.FinalValidation(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, ClearingNestedFieldsInRequiredMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_required_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("nested_int64")}));
  GOOGLESQL_EXPECT_OK(validator.FinalValidation());
}

TEST_F(FilterFieldsPathValidatorTest,
       IncludeNestedFieldsInExcludedRequiredMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_required_value")}));
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_required_value"),
       KitchenSinkPB::Nested::descriptor()->FindFieldByName("nested_int64")}));
  GOOGLESQL_EXPECT_OK(validator.FinalValidation());
}

TEST_F(FilterFieldsPathValidatorTest,
       ExcludeMessageWhoHasRequiredFields) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/false, {KitchenSinkPB::descriptor()->FindFieldByName(
                             "nested_with_required_fields")}));
  GOOGLESQL_EXPECT_OK(validator.FinalValidation());
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenSkippingRequiredFields) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName("int32_val")}));
  EXPECT_THAT(validator.FinalValidation(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, FailWhenSkippingRequiredMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_int32_val")}));
  EXPECT_THAT(validator.FinalValidation(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(FilterFieldsPathValidatorTest, OkWhenResetClearedRequiredMessage) {
  FilterFieldsPathValidator validator(KitchenSinkPB::descriptor());
  GOOGLESQL_EXPECT_OK(validator.ValidateFieldPath(
      /*include=*/true,
      {KitchenSinkPB::descriptor()->FindFieldByName(
           "nested_with_required_fields"),
       KitchenSinkPB::NestedWithRequiredMessageFields::descriptor()
           ->FindFieldByName("nested_int32_val")}));
  GOOGLESQL_EXPECT_OK(validator.FinalValidation(true));
}

}  // namespace googlesql
