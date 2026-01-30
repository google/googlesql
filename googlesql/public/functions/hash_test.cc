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

#include "googlesql/public/functions/hash.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql {
namespace functions {
namespace {

TEST(HashTest, BasicCorrectness) {
  // Verify a few properties for each algorithm:
  // * Consistency of results.
  // * Length of the output.
  // * Different hashes for different inputs.
  const std::vector<std::pair<Hasher::Algorithm, int>> algorithms_and_lengths =
      {{Hasher::kMd5, 16},
       {Hasher::kSha1, 20},
       {Hasher::kSha256, 32},
       {Hasher::kSha512, 64}};

  const char kFirstInput[] = "abc [] +-";
  const char kSecondInput[] = "123456";
  const char kEmptyInput[] = "";
  // Two invalid codepoints: 0xD8FF and 0x11FFFF.
  std::string input_with_invalid_utf8;
  input_with_invalid_utf8.push_back('\xD8');
  input_with_invalid_utf8.push_back('\xFF');
  input_with_invalid_utf8.push_back('\x11');
  input_with_invalid_utf8.push_back('\xFF');
  input_with_invalid_utf8.push_back('\xFF');
  for (const auto& algorithm_and_length : algorithms_and_lengths) {
    const int expected_length = algorithm_and_length.second;
    SCOPED_TRACE(absl::Substitute("Algorithm $0", algorithm_and_length.first));
    const std::unique_ptr<Hasher> hasher =
        Hasher::Create(algorithm_and_length.first);

    const std::string first_hash = hasher->Hash(kFirstInput);
    const std::string first_hash2 = hasher->Hash(kFirstInput);
    EXPECT_EQ(first_hash, first_hash2);
    EXPECT_EQ(expected_length, first_hash.size());

    const std::string second_hash = hasher->Hash(kSecondInput);
    EXPECT_NE(second_hash, first_hash);
    EXPECT_EQ(expected_length, second_hash.size());

    const std::string empty_hash = hasher->Hash(kEmptyInput);
    EXPECT_NE(empty_hash, first_hash);
    EXPECT_EQ(expected_length, empty_hash.size());

    const std::string first_hash3 = hasher->Hash(kFirstInput);
    EXPECT_EQ(first_hash, first_hash3);
    EXPECT_EQ(expected_length, first_hash.size());

    const std::string invalid_utf8_hash = hasher->Hash(input_with_invalid_utf8);
    EXPECT_EQ(expected_length, invalid_utf8_hash.size());
  }
}

TEST(HashTest, ComplianceTests) {
  std::unique_ptr<Hasher> md5 = Hasher::Create(Hasher::Algorithm::kMd5);
  std::unique_ptr<Hasher> sha1 = Hasher::Create(Hasher::Algorithm::kSha1);
  std::unique_ptr<Hasher> sha256 = Hasher::Create(Hasher::Algorithm::kSha256);
  std::unique_ptr<Hasher> sha512 = Hasher::Create(Hasher::Algorithm::kSha512);
  for (const FunctionTestCall& test : GetFunctionTestsHash()) {
    ASSERT_EQ(1, test.params.params().size());
    const std::string& function = test.function_name;

    const googlesql::Value& input_value = test.params.params()[0];
    if (input_value.is_null()) {
      continue;
    }

    const Type* input_type = input_value.type();
    ASSERT_TRUE(input_type->IsString() || input_type->IsBytes())
        << input_type->DebugString();
    absl::string_view input;
    if (test.params.params()[0].type()->IsString()) {
      input = input_value.string_value();
    } else {
      input = input_value.bytes_value();
    }
    if (function == "md5") {
      EXPECT_EQ(test.params.result().bytes_value(), md5->Hash(input))
          << input_value;
    } else if (function == "sha1") {
      EXPECT_EQ(test.params.result().bytes_value(), sha1->Hash(input))
          << input_value;
    } else if (function == "sha256") {
      EXPECT_EQ(test.params.result().bytes_value(), sha256->Hash(input))
          << input_value;
    } else if (function == "sha512") {
      EXPECT_EQ(test.params.result().bytes_value(), sha512->Hash(input))
          << input_value;
    } else {
      FAIL() << "Unexpected hash function: " << function;
    }
  }
}

TEST(FingerprintTest, ComplianceTests) {
  for (const FunctionTestCall& test : GetFunctionTestsFarmFingerprint()) {
    ASSERT_EQ(1, test.params.params().size());
    const googlesql::Value& input_value = test.params.params()[0];
    if (input_value.is_null()) {
      continue;
    }

    const Type* input_type = input_value.type();
    ASSERT_TRUE(input_type->IsString() || input_type->IsBytes())
        << input_type->DebugString();
    absl::string_view input;
    if (test.params.params()[0].type()->IsString()) {
      input = input_value.string_value();
    } else {
      input = input_value.bytes_value();
    }

    EXPECT_EQ(test.params.result().int64_value(), FarmFingerprint(input))
        << input_value;
  }
}

}  // namespace
}  // namespace functions
}  // namespace googlesql
