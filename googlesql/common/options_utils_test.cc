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

#include "googlesql/common/options_utils.h"

#include <memory>
#include <string>

#include "googlesql/common/testing/proto_matchers.h"  // NOLINT
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/btree_set.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/statusor.h"

namespace googlesql::internal {

// Simple flag type that stores query parameters. Each binary that uses a query
// parameter flag will need to declare its own flag type, since the acceptable
// values is slightly different due to different language options, catalog,
// etc. when converting query parameter text to values.
struct ParameterFlag {
  ParameterValueMap map;
};

bool AbslParseFlag(absl::string_view text, ParameterFlag* flag,
                   std::string* err) {
  AnalyzerOptions analyzer_options;
  auto catalog = std::make_unique<SimpleCatalog>("test");
  catalog->AddBuiltinFunctions(
      GoogleSQLBuiltinFunctionOptions(analyzer_options.language()));

  return ParseQueryParameterFlag(text, analyzer_options, catalog.get(),
                                 &flag->map, err);
}
std::string AbslUnparseFlag(const ParameterFlag& m) {
  return UnparseQueryParameterFlag(m.map);
}
}  // namespace googlesql::internal

ABSL_FLAG(googlesql::internal::EnabledAstRewrites, test_enabled_ast_rewrites,
          googlesql::internal::EnabledAstRewrites{}, "");

ABSL_FLAG(googlesql::internal::EnabledLanguageFeatures,
          test_enabled_language_features,
          googlesql::internal::EnabledLanguageFeatures{}, "");

ABSL_FLAG(googlesql::internal::ParameterFlag, test_parameters,
          googlesql::internal::ParameterFlag{},
          googlesql::internal::kQueryParameterMapHelpstring);

namespace googlesql::internal {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;
using ::absl_testing::StatusIs;

using googlesql::ResolvedASTRewrite;
using RewriteEnumOptionsEntry = EnumOptionsEntry<ResolvedASTRewrite>;
using LanguageFeatureEnumOptionsEntry = EnumOptionsEntry<LanguageFeature>;

static absl::btree_set<ResolvedASTRewrite> GetV1Rewrites() {
  return {REWRITE_PIVOT};
}

static absl::btree_set<ResolvedASTRewrite> GetV2Rewrites() {
  return {REWRITE_PIVOT, REWRITE_TYPEOF_FUNCTION};
}

absl::StatusOr<RewriteEnumOptionsEntry> CallParseEnumOptionsSet(
    absl::string_view options_str) {
  return ParseEnumOptionsSet<ResolvedASTRewrite>({{"NONE", {}},
                                                  {"ALL", GetAllRewrites()},
                                                  {"V1", GetV1Rewrites()},
                                                  {"V2", GetV2Rewrites()}},
                                                 "REWRITE_", "Rewrite",
                                                 options_str);
}

TEST(OptionsUtils, ParseEnumOptionsSet) {
  auto ExpectEmpty = [](absl::string_view str) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(RewriteEnumOptionsEntry result,
                         CallParseEnumOptionsSet(str));
    EXPECT_EQ(result.description, "NONE");
    EXPECT_THAT(result.options, IsEmpty());
  };
  ExpectEmpty("NONE");
  ExpectEmpty("none");
  ExpectEmpty("nonE");
  ExpectEmpty("nONE");
  ExpectEmpty(" nONE ");

  auto ExpectV1 = [](absl::string_view str) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(RewriteEnumOptionsEntry result,
                         CallParseEnumOptionsSet(str));
    EXPECT_EQ(result.description, "V1");
    EXPECT_THAT(result.options, UnorderedElementsAreArray(GetV1Rewrites()));
  };

  ExpectV1("V1");
  ExpectV1("v1");
  ExpectV1(" v1 ");

  auto ExpectAll = [](absl::string_view str) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(RewriteEnumOptionsEntry result,
                         CallParseEnumOptionsSet(str));
    EXPECT_EQ(result.description, "ALL");
    EXPECT_THAT(result.options, UnorderedElementsAreArray(GetAllRewrites()));
  };

  ExpectAll("ALL");
  ExpectAll("all");
  ExpectAll("AlL");
  ExpectAll("aLl  ");
  ExpectAll(" All ");
}

TEST(OptionsUtils, ParseEnumOptionsSet__EmptyIsError) {
  EXPECT_THAT(CallParseEnumOptionsSet("GARBAGE!"),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Rewrite list should always start with one of "
                                 "ALL, NONE, V1, V2")));
}

TEST(OptionsUtils, ParseEnumOptionsSet__GarbageEntryIsError) {
  EXPECT_THAT(CallParseEnumOptionsSet("GARBAGE!"),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Rewrite list should always start with one of "
                                 "ALL, NONE, V1, V2")));
}

TEST(OptionsUtils, ParseEnumOptionsSet__IncludedPrefixIsError) {
  EXPECT_THAT(
      CallParseEnumOptionsSet("NONE,+REWRITE_TYPEOF_FUNCTION"),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("For consistency, do not include the REWRITE_ prefix")));
}

TEST(OptionsUtils, ParseEnumOptionsSet__RedundantOverrideIsError) {
  auto ExpectDuplicateEntryError = [](absl::string_view str) {
    EXPECT_THAT(
        CallParseEnumOptionsSet(str),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Duplicate entry for Rewrite: REWRITE_TYPEOF_FUNCTION")));
  };
  ExpectDuplicateEntryError("NONE,+TYPEOF_FUNCTION,+TYPEOF_FUNCTION");

  ExpectDuplicateEntryError("ALL,-TYPEOF_FUNCTION,-TYPEOF_FUNCTION");
  ExpectDuplicateEntryError("V1,+TYPEOF_FUNCTION,-TYPEOF_FUNCTION");
}

TEST(OptionsUtils, ParseEnumOptionsSet__AllPlusIsError) {
  EXPECT_THAT(
      CallParseEnumOptionsSet("ALL,+TYPEOF_FUNCTION"),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr(
                   "Attempting to add Rewrite, but already started from ALL")));
}

TEST(OptionsUtils, ParseEnumOptionsSet__NoneMinusIsError) {
  EXPECT_THAT(
      CallParseEnumOptionsSet("NONE,-TYPEOF_FUNCTION"),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Attempting to remove Rewrite, but already started from NONE.")));
}

TEST(OptionsUtils, ParseEnumOptionsSet__WhitespaceCapsInsensitvity) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      RewriteEnumOptionsEntry result,
      CallParseEnumOptionsSet("NONE,  +typeof_function   ,  +Anonymization"));
  EXPECT_EQ(result.description, "NONE,+ANONYMIZATION,+TYPEOF_FUNCTION");
  EXPECT_THAT(result.options,
              UnorderedElementsAre(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION,
                                   ResolvedASTRewrite::REWRITE_ANONYMIZATION));
}

TEST(OptionsUtils, ParseEnumOptionsSet__AdditionAndSubtraction) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      RewriteEnumOptionsEntry result,
      CallParseEnumOptionsSet("V2,-typeof_function,+Anonymization"));
  EXPECT_EQ(result.description, "V2,+ANONYMIZATION,-TYPEOF_FUNCTION");
  EXPECT_THAT(result.options,
              UnorderedElementsAre(ResolvedASTRewrite::REWRITE_ANONYMIZATION,
                                   ResolvedASTRewrite::REWRITE_PIVOT));
}

TEST(OptionsUtils, ParseEnumOptionsSet__AddAlreadyIncludedOption) {
  // This is okay. Even though the entry is already in V2, we don't require
  // users to know exactly which features are currently in a given base. This
  // Also allows those sets to change without updating all the test,
  // which is mostly pointless toil.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(RewriteEnumOptionsEntry result,
                       CallParseEnumOptionsSet("V2,+TYPEOF_FUNCTION"));

  EXPECT_EQ(result.description, "V2,+TYPEOF_FUNCTION");

  EXPECT_THAT(result.options, UnorderedElementsAreArray(GetV2Rewrites()));
}

TEST(OptionsUtils, ParseEnabledAstRewritesBadStringReturnsError) {
  EXPECT_THAT(ParseEnabledAstRewrites("bad-rewrites"),
              StatusIs(absl::StatusCode::kInternal));
}

//////////////////////////////////////////////////////////////////////////
// ResolvedASTRewrite
//////////////////////////////////////////////////////////////////////////

TEST(OptionsUtils, ParseEnabledAstRewrites) {
  auto CheckResult = [](absl::string_view str,
                        absl::btree_set<ResolvedASTRewrite> expected_enabled,
                        absl::btree_set<ResolvedASTRewrite> expected_disabled) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(RewriteEnumOptionsEntry result,
                         ParseEnabledAstRewrites(str));
    for (const ResolvedASTRewrite rewrite : expected_enabled) {
      EXPECT_TRUE(result.options.contains(rewrite))
          << ResolvedASTRewrite_Name(rewrite)
          << " should be enabled when str is '" << str << "'";
    }
    for (const ResolvedASTRewrite rewrite : expected_disabled) {
      EXPECT_FALSE(result.options.contains(rewrite))
          << ResolvedASTRewrite_Name(rewrite)
          << " should be disabled when str is '" << str << "'";
    }
    EXPECT_EQ(result.description, str) << " str="
                                       << "'" << str << "'";
  };
  // We don't need to exhaustively test this, just ensure we are invoking the
  // the 'parser' for this format.
  CheckResult("NONE", {}, {REWRITE_PIVOT});
  CheckResult("ALL", {REWRITE_ANONYMIZATION}, {});

  CheckResult("DEFAULTS", AnalyzerOptions::DefaultRewrites(),
              {REWRITE_ANONYMIZATION});
  CheckResult("DEFAULTS,-PIVOT", {REWRITE_FLATTEN},
              {REWRITE_PIVOT, REWRITE_ANONYMIZATION});
  CheckResult("DEFAULTS,+ANONYMIZATION",
              {REWRITE_FLATTEN, REWRITE_PIVOT, REWRITE_ANONYMIZATION},
              {REWRITE_INVALID_DO_NOT_USE});
  CheckResult("ALL",
              {REWRITE_TEST_DEFAULT_DISABLED, REWRITE_TEST_DEFAULT_ENABLED,
               REWRITE_TEST_DEFAULT_DISABLED_AND_IN_DEVELOPMENT,
               REWRITE_TEST_DEFAULT_ENABLED_AND_IN_DEVELOPMENT},
              {});
  CheckResult("ALL_MINUS_DEV",
              {REWRITE_TEST_DEFAULT_DISABLED, REWRITE_TEST_DEFAULT_ENABLED},
              {REWRITE_TEST_DEFAULT_DISABLED_AND_IN_DEVELOPMENT,
               REWRITE_TEST_DEFAULT_ENABLED_AND_IN_DEVELOPMENT});
  CheckResult("DEFAULTS",
              {REWRITE_TEST_DEFAULT_ENABLED,
               REWRITE_TEST_DEFAULT_ENABLED_AND_IN_DEVELOPMENT},
              {REWRITE_TEST_DEFAULT_DISABLED,
               REWRITE_TEST_DEFAULT_DISABLED_AND_IN_DEVELOPMENT});
  CheckResult("DEFAULTS_MINUS_DEV", {REWRITE_TEST_DEFAULT_ENABLED},
              {REWRITE_TEST_DEFAULT_DISABLED,
               REWRITE_TEST_DEFAULT_DISABLED_AND_IN_DEVELOPMENT,
               REWRITE_TEST_DEFAULT_ENABLED_AND_IN_DEVELOPMENT});
}

TEST(OptionsUtils, TestRewriteFlagSupport) {
  absl::CommandLineFlag* flag =
      absl::FindCommandLineFlag("test_enabled_ast_rewrites");
  EXPECT_EQ(flag->DefaultValue(), "NONE");

  EXPECT_EQ(flag->CurrentValue(), "NONE");
  absl::SetFlag(&FLAGS_test_enabled_ast_rewrites,
                EnabledAstRewrites{{REWRITE_ANONYMIZATION}});
  EXPECT_EQ(flag->CurrentValue(), "NONE,+ANONYMIZATION");

  absl::SetFlag(&FLAGS_test_enabled_ast_rewrites,
                EnabledAstRewrites{AnalyzerOptions::DefaultRewrites()});
  EXPECT_EQ(flag->CurrentValue(), "DEFAULTS");

  // Test roundtripping "ALL", which is special, but we don't have easy access
  // to its value.
  std::string error;
  EXPECT_TRUE(flag->ParseFrom("ALL", &error));
  EnabledAstRewrites all = absl::GetFlag(FLAGS_test_enabled_ast_rewrites);
  EXPECT_GE(all.enabled_ast_rewrites.size(), 12);

  absl::SetFlag(&FLAGS_test_enabled_ast_rewrites, all);
  EXPECT_EQ(flag->CurrentValue(), "ALL");
}

TEST(OptionsUtils, TestFlagSupport_BadFlag) {
  absl::CommandLineFlag* flag =
      absl::FindCommandLineFlag("test_enabled_ast_rewrites");

  std::string error;
  EXPECT_FALSE(flag->ParseFrom("GARBAGE!", &error));
  EXPECT_THAT(error, HasSubstr("Rewrite list should always start with one of "
                               "ALL, ALL_MINUS_DEV, DEFAULTS, "
                               "DEFAULTS_MINUS_DEV, NONE"));
}

//////////////////////////////////////////////////////////////////////////
// LanguageFeature
//////////////////////////////////////////////////////////////////////////

TEST(OptionsUtils, ParseEnabledLanguageFeatures) {
  auto CheckResult = [](absl::string_view str,
                        absl::btree_set<LanguageFeature> expected_enabled,
                        absl::btree_set<LanguageFeature> expected_disabled) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(LanguageFeatureEnumOptionsEntry result,
                         ParseEnabledLanguageFeatures(str));
    for (const LanguageFeature value : expected_enabled) {
      EXPECT_TRUE(result.options.contains(value))
          << LanguageFeature_Name(value) << " should be enabled when str is '"
          << str << "'";
    }
    for (const LanguageFeature value : expected_disabled) {
      EXPECT_FALSE(result.options.contains(value))
          << LanguageFeature_Name(value) << " should be disabled when str is '"
          << str << "'";
    }
    EXPECT_EQ(result.description, str) << " str="
                                       << "'" << str << "'";
  };
  // We don't need to exhaustively test this, just ensure we are invoking the
  // the 'parser' for this format.
  CheckResult("NONE", {}, {FEATURE_TEST_IDEALLY_DISABLED});
  CheckResult("ALL", {FEATURE_NAMED_ARGUMENTS},
              {__LanguageFeature__switch_must_have_a_default__});

  CheckResult("MAXIMUM", {FEATURE_NAMED_ARGUMENTS},
              {FEATURE_TEST_IDEALLY_DISABLED,
               FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT});
  CheckResult("MAXIMUM,-NAMED_ARGUMENTS", {FEATURE_ALLOW_SLASH_PATHS},
              {FEATURE_NAMED_ARGUMENTS});
  CheckResult("MAXIMUM,+TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT",
              {FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT}, {});
  CheckResult("DEV", {FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT},
              {FEATURE_TEST_IDEALLY_DISABLED_AND_IN_DEVELOPMENT});
}

TEST(OptionsUtils, TestLanguageFeatureFlagSupport) {
  absl::CommandLineFlag* flag =
      absl::FindCommandLineFlag("test_enabled_language_features");
  EXPECT_EQ(flag->DefaultValue(), "NONE");

  EXPECT_EQ(flag->CurrentValue(), "NONE");
  absl::SetFlag(&FLAGS_test_enabled_language_features,
                EnabledLanguageFeatures{{FEATURE_NAMED_ARGUMENTS}});
  EXPECT_EQ(flag->CurrentValue(), "NONE,+NAMED_ARGUMENTS");

  auto ExpectRoundtrip = [flag](absl::string_view base) {
    std::string error;
    EXPECT_TRUE(flag->ParseFrom(base, &error));
    EnabledLanguageFeatures all =
        absl::GetFlag(FLAGS_test_enabled_language_features);
    absl::SetFlag(&FLAGS_test_enabled_language_features, all);
    EXPECT_EQ(flag->CurrentValue(), base);
  };
  ExpectRoundtrip("MAXIMUM");
  ExpectRoundtrip("DEV");
  ExpectRoundtrip("ALL");
  ExpectRoundtrip("NONE");
}

class QueryParameterParsingTest : public ::testing::Test {
 public:
  QueryParameterParsingTest() { CreateCatalog(); }

 protected:
  void CreateCatalog() {
    catalog_ = std::make_unique<SimpleCatalog>("test");
    catalog_->AddBuiltinFunctions(
        GoogleSQLBuiltinFunctionOptions(analyzer_options_.language()));
  }

  AnalyzerOptions analyzer_options_;
  std::unique_ptr<SimpleCatalog> catalog_;
};

TEST_F(QueryParameterParsingTest, EmptyParamList) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("", analyzer_options_, catalog_.get(),
                                      &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "");
}
TEST_F(QueryParameterParsingTest, SingleParam) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=1+2+3", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=6");
}

TEST_F(QueryParameterParsingTest, SingleParamTrailingSemiColon) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=1+2+3;", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=6");
}

TEST_F(QueryParameterParsingTest, CommentsAndWhitespace) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag(
      "p1 /*comment*/= /*comment*/ 1 + 2+3 ;\n p2 = 3.2  ; /*comment*/ ",
      analyzer_options_, catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=6;p2=3.2");
}

TEST_F(QueryParameterParsingTest, MultipleParams) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=1;p2=1+2", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=1;p2=3");
}

TEST_F(QueryParameterParsingTest, MultipleParamsTrailingSemiColon) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=1;p2=1+2;", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=1;p2=3");
}

TEST_F(QueryParameterParsingTest, ParamReferencingPreviousParam) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=1; p2 = @p1 + 2", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=1;p2=3");
}

TEST_F(QueryParameterParsingTest, ProtoTypeParam) {
  catalog_->SetDescriptorPool(google::protobuf::DescriptorPool::generated_pool());

  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag(
      "ks = NEW googlesql_test.KitchenSinkPB("
      "1 AS int64_key_1, 2 AS int64_key_2); key=@ks.int64_key_1",
      analyzer_options_, catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map),
            "key=1;ks=CAST(b\"\\x08\\x01\\x10\\x02\" AS "
            "`googlesql_test.KitchenSinkPB`)");

  // Serialized proto bytes are not easy to read, so add a more human-friendly
  // assertion that the query parameter proto is as expected.
  googlesql_test::KitchenSinkPB kitchen_sink_pb;
  ASSERT_TRUE(kitchen_sink_pb.ParseFromCord(flag.map.at("ks").ToCord()));
  ASSERT_THAT(kitchen_sink_pb, testing::EqualsProto(
                                   R"pb(
                                     int64_key_1: 1 int64_key_2: 2
                                   )pb"));
}

TEST_F(QueryParameterParsingTest, ParamTypeGuardedByLanguageFeature) {
  analyzer_options_.mutable_language()->EnableLanguageFeature(
      FEATURE_INTERVAL_TYPE);
  CreateCatalog();

  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=INTERVAL 10 SECOND",
                                      analyzer_options_, catalog_.get(),
                                      &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map),
            "p1=INTERVAL \"0-0 0 0:0:10\" YEAR TO SECOND");
}

TEST_F(QueryParameterParsingTest, ParamWithNameOfNonreservedKeyword) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("pivot=3.4", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "pivot=3.4");
}

TEST_F(QueryParameterParsingTest, ParamWithQuotedNameOfReservedKeyword) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("`SELECT`=3.4", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "`select`=3.4");
}

TEST_F(QueryParameterParsingTest, SemiColonInsideParamValue) {
  ParameterFlag flag;
  std::string error;
  ASSERT_TRUE(ParseQueryParameterFlag("p1=';';p2='a;b'", analyzer_options_,
                                      catalog_.get(), &flag.map, &error))
      << error;
  ASSERT_EQ(UnparseQueryParameterFlag(flag.map), "p1=\";\";p2=\"a;b\"");
}

TEST_F(QueryParameterParsingTest, MissingParamName) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("=3.4", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got =3.4"));
}

TEST_F(QueryParameterParsingTest, MissingParamValue_OnlyParam) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=", analyzer_options_, catalog_.get(),
                                       &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got a="));
}
TEST_F(QueryParameterParsingTest, MissingParamValue_FirstParam) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=;b=3", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got a="));
}
TEST_F(QueryParameterParsingTest, ParamNameIsUnquotedReservedKeyword) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("SELECT=1", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got SELECT"));
}
TEST_F(QueryParameterParsingTest, DuplicateParamName) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=1;A=2", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Duplicate query parameter 'a'"));
}
TEST_F(QueryParameterParsingTest, ParameterValueSyntaxError) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=1+", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error: Unexpected end of expression [at 1:3]"));
}
TEST_F(QueryParameterParsingTest, ParameterValueAnalyzerError) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=1+garbage", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Unrecognized name: garbage [at 1:3]"));
}
TEST_F(QueryParameterParsingTest, ParameterValueEvaluatorError) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=1/(SELECT 0)", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("division by zero: 1 / 0"));
}
TEST_F(QueryParameterParsingTest, MissingEqualsSign) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("abcdef", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got abcdef"));
}
TEST_F(QueryParameterParsingTest, StraySemiColonInMiddle) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=1;;b=2", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error; expected name=value; got "));
}
TEST_F(QueryParameterParsingTest, UnclosedComment) {
  ParameterFlag flag;
  std::string error;
  ASSERT_FALSE(ParseQueryParameterFlag("a=/*unclosed", analyzer_options_,
                                       catalog_.get(), &flag.map, &error));
  ASSERT_THAT(error, Eq("Syntax error: Unclosed comment"));
}

TEST(OptionsUtils, TestQueryParameterFlagSupport) {
  absl::CommandLineFlag* flag = absl::FindCommandLineFlag("test_parameters");
  EXPECT_EQ(flag->DefaultValue(), "");
  EXPECT_EQ(flag->CurrentValue(), "");

  ParameterFlag flag_value;
  flag_value.map["p1"] = Value::Int64(1234);
  absl::SetFlag(&FLAGS_test_parameters, flag_value);
  EXPECT_EQ(flag->CurrentValue(), "p1=1234");

  flag_value.map["p2"] = Value::String("abcd");
  absl::SetFlag(&FLAGS_test_parameters, flag_value);
  EXPECT_EQ(flag->CurrentValue(), "p1=1234;p2=\"abcd\"");

  std::string err;
  EXPECT_TRUE(flag->ParseFrom("p3=TRUE;p4=2.3", &err));
  EXPECT_EQ(err, "");
  flag_value = absl::GetFlag(FLAGS_test_parameters);
  EXPECT_THAT(
      flag_value.map,
      ElementsAre(Pair(std::string("p3"), EqualsValue(Value::Bool(true))),
                  Pair(std::string("p4"), EqualsValue(Value::Double(2.3)))));
}
}  // namespace googlesql::internal
