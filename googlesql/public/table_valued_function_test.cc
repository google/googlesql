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

#include "googlesql/public/table_valued_function.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type_deserializer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace googlesql {

using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;
using ::absl_testing::StatusIs;

void ExpectEqualTVFSchemaColumn(const TVFSchemaColumn& column1,
                                const TVFSchemaColumn& column2) {
  EXPECT_EQ(column1.name, column2.name);
  EXPECT_EQ(column1.is_pseudo_column, column2.is_pseudo_column);
  EXPECT_TRUE(column1.type->Equals(column2.type));
  AnnotationMap::Equals(column1.annotation_map, column2.annotation_map);
  EXPECT_EQ(column1.name_parse_location_range,
            column2.name_parse_location_range);
  EXPECT_EQ(column1.type_parse_location_range,
            column2.type_parse_location_range);
}

void ExpectEqualTVFRelations(const TVFRelation& relation1,
                             const TVFRelation& relation2) {
  EXPECT_EQ(relation1.is_value_table(), relation2.is_value_table());
  ASSERT_EQ(relation1.num_columns(), relation2.num_columns());
  for (int i = 0; i < relation1.num_columns(); ++i) {
    ExpectEqualTVFSchemaColumn(relation1.column(i), relation2.column(i));
  }
}

// Serialize and deserialize the given TVFRelation, and then expect that it's
// equal to the input.
void SerializeDeserializeAndCompare(const TVFRelation& relation) {
  FileDescriptorSetMap file_descriptor_set_map;
  TVFRelationProto tvf_relation_proto;
  GOOGLESQL_ASSERT_OK(relation.Serialize(&file_descriptor_set_map, &tvf_relation_proto));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TVFRelation result,
      TVFRelation::Deserialize(tvf_relation_proto,
                               TypeDeserializer(&type_factory, pools)));
  ExpectEqualTVFRelations(relation, result);
}

// Serialize and deserialize the given TVFSchemaColumn, and then expect that
// it's equal to the input.
void SerializeDeserializeAndCompare(const TVFSchemaColumn& column) {
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TVFRelationColumnProto tvf_schema_column,
                       column.ToProto(&file_descriptor_set_map));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TVFSchemaColumn result,
      TVFSchemaColumn::FromProto(tvf_schema_column,
                                 TypeDeserializer(&type_factory, pools)));

  ExpectEqualTVFSchemaColumn(column, result);
}

// Serialize and deserialize the given TableValuedFunctionOptions, and then
// expect that it's equal to the input.
void SerializeDeserializeAndCompare(const TableValuedFunctionOptions& options) {
  TableValuedFunctionOptionsProto options_proto;
  options.Serialize(&options_proto);
  std::unique_ptr<TableValuedFunctionOptions> deserialized_options;
  GOOGLESQL_ASSERT_OK(TableValuedFunctionOptions::Deserialize(options_proto,
                                                    &deserialized_options));
  EXPECT_EQ(options, *deserialized_options);
}

// Check serialization and deserialization of TVFRelation.
TEST(TVFTest, TVFRelationSerializationAndDeserialization) {
  TVFRelation::Column column("Col1", googlesql::types::DoubleType());
  TVFRelation relation({column});

  SerializeDeserializeAndCompare(relation);
}

TEST(TVFTest, TVFRelationSerializationAndDeserializationWithColumnLocations) {
  ParseLocationRange location_range1, location_range2;
  location_range1.set_start(ParseLocationPoint::FromByteOffset("file1", 17));
  location_range1.set_end(ParseLocationPoint::FromByteOffset("file1", 25));

  location_range2.set_start(ParseLocationPoint::FromByteOffset("file1", 29));
  location_range2.set_end(ParseLocationPoint::FromByteOffset("file1", 32));

  TVFRelation::Column column("Col1", googlesql::types::DoubleType());
  // Set parse locations for TVFSchema Column.
  column.name_parse_location_range = location_range1;
  column.type_parse_location_range = location_range2;
  TVFRelation relation({column});

  SerializeDeserializeAndCompare(relation);
  // Test for relation with collated column in value table.
  TypeFactory type_factory;
  std::unique_ptr<AnnotationMap> annotation =
      AnnotationMap::Create(type_factory.get_string());
  annotation->SetAnnotation(static_cast<int>(AnnotationKind::kSampleAnnotation),
                            SimpleValue::String("abc"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const AnnotationMap* annotation_map,
                       type_factory.TakeOwnership(std::move(annotation)));
  TVFSchemaColumn pseudo_column =
      TVFSchemaColumn("pseudo_column", googlesql::types::Int64Type(), true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TVFRelation value_table_relation,
      TVFRelation::ValueTable({googlesql::types::DoubleType(), annotation_map},
                              {pseudo_column}));
  SerializeDeserializeAndCompare(relation);
}

// Check serialization and deserialization of TVFSchemaColumn
TEST(TVFTest,
     TVFSchemaColumnSerializationAndDeserializationWithColumnLocations) {
  ParseLocationRange location_range1, location_range2;
  location_range1.set_start(ParseLocationPoint::FromByteOffset("file1", 17));
  location_range1.set_end(ParseLocationPoint::FromByteOffset("file1", 25));

  location_range2.set_start(ParseLocationPoint::FromByteOffset("file1", 29));
  location_range2.set_end(ParseLocationPoint::FromByteOffset("file1", 32));

  TVFRelation::Column column("Col1", googlesql::types::DoubleType());
  // Set parse locations for TVFSchema Column.
  column.name_parse_location_range = location_range1;
  column.type_parse_location_range = location_range2;

  SerializeDeserializeAndCompare(column);

  // Test for column with non-empty <annotation_map>.
  TypeFactory type_factory;
  std::unique_ptr<AnnotationMap> annotation =
      AnnotationMap::Create(type_factory.get_string());
  annotation->SetAnnotation(static_cast<int>(AnnotationKind::kSampleAnnotation),
                            SimpleValue::String("abc"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const AnnotationMap* annotation_map,
                       type_factory.TakeOwnership(std::move(annotation)));
  TVFRelation::Column column_with_annotations(
      "Col2", {googlesql::types::DoubleType(), annotation_map});
  column_with_annotations.name_parse_location_range = location_range1;
  column_with_annotations.type_parse_location_range = location_range2;

  SerializeDeserializeAndCompare(column_with_annotations);
}

TEST(TVFTest, TestInvalidColumnNameForTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_column_empty_name"},
          {FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION}, -1)},
          {TVFSchemaColumn("", googlesql::types::Int64Type())})),
      "invalid empty column name in extra columns");
}

TEST(TVFTest, TestDuplicateColumnNameForTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  TVFSchemaColumn int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_column_with_duplicated_names"},
          {FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION}, -1)},
          {int64_col, int64_col})),
      "extra columns have duplicated column names: int64_col");
}

TEST(TVFTest, TestInvalidNonTemplatedArgumentForTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_int64_value_table =
      TVFRelation::ValueTable(googlesql::types::Int64Type());
  TVFSchemaColumn int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::ColumnList columns = {int64_col};
  TVFRelation tvf_relation(columns);

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_column_with_value_table"},
          {FunctionSignature(
              FunctionArgumentType::RelationWithSchema(
                  tvf_relation,
                  /*extra_relation_input_columns_allowed=*/false),
              {FunctionArgumentType::RelationWithSchema(
                   output_schema_int64_value_table,
                   /*extra_relation_input_columns_allowed=*/false),
               FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                    FunctionArgumentType::REQUIRED)},
              -1)},
          {int64_col})),
      HasSubstr("ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF must "
                "have a templated relation as the first argument"));
}

TEST(TVFTest, FullNameShouldNotPrependEmptyGroupName) {
  TypeFactory factory;
  std::vector<std::string> name_path = {"tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path, "",
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, {}, -1)},
      TableValuedFunctionOptions().set_uses_upper_case_sql_name(false));
  EXPECT_EQ(tvf->FullName(), "tvf");
  EXPECT_FALSE(tvf->IsGoogleSQLBuiltin());
  EXPECT_EQ(tvf->SQLName(), "tvf");
}

TEST(TVFTest, FullNameShouldPrependGroupName) {
  TypeFactory factory;
  std::vector<std::string> name_path = {"tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path, "group",
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, {}, -1)},
      TableValuedFunctionOptions().set_uses_upper_case_sql_name(false));
  EXPECT_EQ(tvf->FullName(), "group:tvf");
  EXPECT_FALSE(tvf->IsGoogleSQLBuiltin());
  EXPECT_EQ(tvf->SQLName(), "group:tvf");
}

TEST(TVFTest, GoogleSQLFunctionGroupNameIsGoogleSQLBuiltin) {
  TypeFactory factory;
  std::vector<std::string> name_path = {"builtin_tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path, Function::kGoogleSQLFunctionGroupName,
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, {}, -1)},
      TableValuedFunctionOptions().set_uses_upper_case_sql_name(false));
  EXPECT_TRUE(tvf->IsGoogleSQLBuiltin());
  EXPECT_EQ(tvf->SQLName(), "builtin_tvf");
}

TEST(TVFTest, TVFResolveCallsComputeResultCallBack) {
  TypeFactory factory;
  TVFSchemaColumn int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::ColumnList columns = {int64_col};
  TVFRelation tvf_relation(columns);

  TVFComputeResultTypeCallback result_type_callback =
      [&tvf_relation](Catalog* catalog, TypeFactory* type_factory,
                      const FunctionSignature& signature,
                      const std::vector<TVFInputArgumentType>& arguments,
                      const AnalyzerOptions& analyzer_options) {
        TVFSignatureOptions tvf_signature_options;
        tvf_signature_options.additional_deprecation_warnings =
            signature.AdditionalDeprecationWarnings();
        return TVFSignature::Create(arguments, tvf_relation,
                                    tvf_signature_options);
      };

  FunctionSignature signature(ARG_TYPE_RELATION, {}, -1);
  std::vector<std::string> name_path = {"tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path, /*group=*/"", std::vector<FunctionSignature>{signature},
      TableValuedFunctionOptions().set_compute_result_type_callback(
          result_type_callback));

  AnalyzerOptions options;
  std::vector<TVFInputArgumentType> arguments;
  std::shared_ptr<TVFSignature> tvf_signature;
  GOOGLESQL_ASSERT_OK(tvf->Resolve(&options, arguments, signature, nullptr, &factory,
                         &tvf_signature));
  ASSERT_THAT(tvf_signature, NotNull());
  ASSERT_EQ(tvf_signature->result_schema(), tvf_relation);
}

TEST(TVFTest, TVFResolveComputeResultCallBackNullPtr) {
  TypeFactory factory;
  std::vector<std::string> name_path = {"tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path,
      /*group=*/"",
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, {}, -1)},
      TableValuedFunctionOptions());

  AnalyzerOptions options;
  std::vector<TVFInputArgumentType> arguments;
  FunctionSignature concrete_signature(ARG_TYPE_RELATION, {}, -1);
  std::shared_ptr<TVFSignature> tvf_signature;

  ASSERT_THAT(tvf->Resolve(&options, arguments, concrete_signature, nullptr,
                           &factory, &tvf_signature),
              StatusIs(absl::StatusCode::kInternal));
  ASSERT_THAT(tvf_signature, IsNull());
}

TEST(TVFTest, TestInvalidConcreteSignatureTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  TVFRelation::Column int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::ColumnList columns = {int64_col};
  TVFRelation tvf_relation(columns);
  FunctionArgumentType arg_type = FunctionArgumentType::RelationWithSchema(
      tvf_relation, /*extra_relation_input_columns_allowed=*/false);

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_column_input_table_has_concrete_signature"},
          {FunctionSignature(arg_type, {arg_type}, -1)}, {int64_col})),
      HasSubstr("ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF must "
                "have a templated relation as the first argument"));
}

TEST(TVFTest, TestPseudoColumnForTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  TVFSchemaColumn pseudo_column =
      TVFSchemaColumn("pseudo_column", googlesql::types::Int64Type(), true);

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_pseudo_column"},
          {FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION}, -1)},
          {pseudo_column})),
      "extra columns cannot be pseudo column");
}

TEST(TVFTest, TestInputTableWithPseudoColumnForTVFWithExtraColumns) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  TVFRelation::Column int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::Column double_col =
      TVFSchemaColumn("double_col", googlesql::types::DoubleType());
  TVFRelation::Column pseudo_column =
      TVFSchemaColumn("pseudo_column", googlesql::types::Int64Type(), true);
  TVFRelation::ColumnList columns = {int64_col, pseudo_column};
  TVFRelation tvf_relation(columns);
  FunctionArgumentType arg_type = FunctionArgumentType::RelationWithSchema(
      tvf_relation, /*extra_relation_input_columns_allowed=*/false);

  EXPECT_DEATH(
      tvf.reset(new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_pseudo_column"},
          {FunctionSignature(ARG_TYPE_RELATION, {arg_type}, -1)},
          {double_col})),
      HasSubstr("ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF must "
                "have a templated relation as the first argument"));
}

TEST(TVFTest, TestSignatureTextUppercasesNameByDefault) {
  TypeFactory factory;

  const std::vector<std::string> function_path = {"test_tvf_name"};

  ::googlesql::TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("value", factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema =
      std::make_unique<::googlesql::TVFRelation>(tvf_schema_columns);

  std::unique_ptr<TableValuedFunction> deserialized_tvf =
      std::make_unique<FixedOutputSchemaTVF>(
          function_path,
          std::vector<FunctionSignature>{::googlesql::FunctionSignature(
              ::googlesql::ARG_TYPE_RELATION,
              {::googlesql::FunctionArgumentType(
                  ::googlesql::ARG_TYPE_ARBITRARY,
                  ::googlesql::FunctionArgumentType::REPEATED)},
              /*context_id=*/static_cast<int64_t>(0))},
          *tvf_schema);

  EXPECT_EQ(deserialized_tvf->GetSupportedSignaturesUserFacingText(
                LanguageOptions(), /*print_template_and_name_details=*/false),
            "TEST_TVF_NAME([ANY, ...])");
}

TEST(TVFTest, TestSignatureTextLowercasesNameWhenSpecified) {
  TypeFactory factory;

  const std::vector<std::string> function_path = {"test_tvf_name"};

  ::googlesql::TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("value", factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema =
      std::make_unique<::googlesql::TVFRelation>(tvf_schema_columns);

  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;

  std::unique_ptr<TableValuedFunction> deserialized_tvf =
      std::make_unique<FixedOutputSchemaTVF>(
          function_path,
          std::vector<FunctionSignature>{::googlesql::FunctionSignature(
              ::googlesql::ARG_TYPE_RELATION,
              {::googlesql::FunctionArgumentType(
                  ::googlesql::ARG_TYPE_ARBITRARY,
                  ::googlesql::FunctionArgumentType::REPEATED)},
              /*context_id=*/static_cast<int64_t>(0))},
          *tvf_schema, tvf_options);

  EXPECT_EQ(deserialized_tvf->GetSupportedSignaturesUserFacingText(
                LanguageOptions(), /*print_template_and_name_details=*/false),
            "test_tvf_name([ANY, ...])");
}

TEST(TVFTest, GetSupportedSignaturesUserFacingTextWithHiddenSignatures) {
  TypeFactory factory;
  const std::vector<std::string> function_path = {"test_tvf_name"};

  // Signature 1: No required features, not hidden.
  FunctionSignature signature1(ARG_TYPE_RELATION, {ARG_TYPE_RELATION}, -1);

  // Signature 2: Requires FEATURE_NAMED_ARGUMENTS, hidden if not enabled.
  FunctionSignatureOptions options2;
  options2.AddRequiredLanguageFeature(FEATURE_NAMED_ARGUMENTS);
  FunctionSignature signature2(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION, FunctionArgumentType(types::Int64Type())}, -1,
      options2);

  // Signature 3: Always hidden.
  FunctionSignatureOptions options3;
  options3.set_is_hidden(true);
  FunctionSignature signature3(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION, FunctionArgumentType(types::StringType())}, -1,
      options3);

  // Signature 4: Requires FEATURE_ANALYTIC_FUNCTIONS, hidden if not enabled.
  FunctionSignatureOptions options4;
  options4.AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  FunctionSignature signature4(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION, FunctionArgumentType(types::BoolType())}, -1,
      options4);

  // Signature 5: IsInternal is true.
  FunctionSignatureOptions options5;
  options5.set_is_internal(true);
  FunctionSignature signature5(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION, FunctionArgumentType(types::DoubleType())}, -1,
      options5);

  // Signature 6: IsDeprecated is true.
  FunctionSignatureOptions options6;
  options6.set_is_deprecated(true);
  FunctionSignature signature6(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION, FunctionArgumentType(types::FloatType())}, -1,
      options6);

  // Signature 7: HasUnsupportedType because FEATURE_NUMERIC_TYPE is not
  // enabled.
  FunctionSignatureOptions options7;
  FunctionSignature signature7(
      ARG_TYPE_RELATION,
      {ARG_TYPE_RELATION,
       FunctionArgumentType(factory.MakeSimpleType(TYPE_NUMERIC))},
      -1, options7);

  std::vector<FunctionSignature> signatures = {
      signature1, signature2, signature3, signature4,
      signature5, signature6, signature7};
  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;

  auto tvf = std::make_unique<TableValuedFunction>(function_path, /*group=*/"",
                                                   signatures, tvf_options);

  const std::string expected_sig1 = "test_tvf_name(TABLE)";
  const std::string expected_sig2 = "test_tvf_name(TABLE, INT64)";
  const std::string expected_sig4 = "test_tvf_name(TABLE, BOOL)";

  // Test Case 1: No features enabled. Only signature1 is shown.
  LanguageOptions lang_options_none;
  EXPECT_EQ(tvf->GetSupportedSignaturesUserFacingText(
                lang_options_none, /*print_template_and_name_details=*/false),
            expected_sig1);

  // Test Case 2: FEATURE_NAMED_ARGUMENTS enabled. signature1 and signature2 are
  // shown.
  LanguageOptions lang_options_named_args;
  lang_options_named_args.EnableLanguageFeature(FEATURE_NAMED_ARGUMENTS);
  EXPECT_EQ(tvf->GetSupportedSignaturesUserFacingText(
                lang_options_named_args,
                /*print_template_and_name_details=*/false),
            absl::StrCat(expected_sig1, "; ", expected_sig2));

  // Test Case 3: FEATURE_ANALYTIC_FUNCTIONS enabled. signature1 and signature4
  // are shown.
  LanguageOptions lang_options_analytic;
  lang_options_analytic.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_EQ(tvf->GetSupportedSignaturesUserFacingText(
                lang_options_analytic,
                /*print_template_and_name_details=*/false),
            absl::StrCat(expected_sig1, "; ", expected_sig4));

  // Test Case 4: Both FEATURE_NAMED_ARGUMENTS and FEATURE_ANALYTIC_FUNCTIONS
  // enabled. signature1, signature2, and signature4 are shown.
  LanguageOptions lang_options_both;
  lang_options_both.EnableLanguageFeature(FEATURE_NAMED_ARGUMENTS);
  lang_options_both.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_EQ(
      tvf->GetSupportedSignaturesUserFacingText(
          lang_options_both, /*print_template_and_name_details=*/false),
      absl::StrCat(expected_sig1, "; ", expected_sig2, "; ", expected_sig4));
}

TEST(TVFTest, TestTableValuedFunctionSerializeAndDeserialize) {
  std::vector<std::string> name_path = {"tvf"};
  auto tvf = std::make_unique<TableValuedFunction>(
      name_path, "group_name",
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION}, -1)});

  FileDescriptorSetMap file_descriptor_set_map;
  TableValuedFunctionProto tvf_proto;
  GOOGLESQL_ASSERT_OK(tvf->Serialize(&file_descriptor_set_map, &tvf_proto));
  TypeFactory type_factory;
  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }
  std::unique_ptr<TableValuedFunction> deserialized_tvf;
  GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
      tvf_proto, TypeDeserializer(&type_factory, pools), &deserialized_tvf));

  EXPECT_TRUE(deserialized_tvf->Is<TableValuedFunction>());
  EXPECT_EQ(typeid(*deserialized_tvf), typeid(TableValuedFunction));
  EXPECT_THAT(deserialized_tvf->function_name_path(),
              tvf->function_name_path());
  EXPECT_THAT(deserialized_tvf->GetGroup(), tvf->GetGroup());
  EXPECT_EQ(deserialized_tvf->signatures().size(), tvf->signatures().size());
  EXPECT_EQ(deserialized_tvf->DebugString(), tvf->DebugString());
}

TEST(TVFTest, TestFixedOutputSchemaTVFSerializeAndDeserialize) {
  TypeFactory factory;

  const std::vector<std::string> function_path = {"test_tvf_name"};

  ::googlesql::TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("value", factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema =
      std::make_unique<::googlesql::TVFRelation>(tvf_schema_columns);

  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;

  std::unique_ptr<TableValuedFunction> tvf =
      std::make_unique<FixedOutputSchemaTVF>(
          function_path,
          std::vector<FunctionSignature>{::googlesql::FunctionSignature(
              FunctionArgumentType::RelationWithSchema(
                  *tvf_schema,
                  /*extra_relation_input_columns_allowed=*/false),
              {::googlesql::FunctionArgumentType(
                  ::googlesql::ARG_TYPE_ARBITRARY,
                  ::googlesql::FunctionArgumentType::REPEATED)},
              /*context_id=*/static_cast<int64_t>(0))},
          *tvf_schema, tvf_options);

  FileDescriptorSetMap file_descriptor_set_map;
  TableValuedFunctionProto tvf_proto;
  GOOGLESQL_ASSERT_OK(tvf->Serialize(&file_descriptor_set_map, &tvf_proto));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  TypeFactory type_factory;
  std::unique_ptr<TableValuedFunction> deserialized_tvf;
  GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
      tvf_proto, TypeDeserializer(&type_factory, pools), &deserialized_tvf));
  EXPECT_TRUE(deserialized_tvf->Is<FixedOutputSchemaTVF>());
  ExpectEqualTVFRelations(
      *tvf_schema,
      deserialized_tvf->GetAs<FixedOutputSchemaTVF>()->result_schema());
}

TEST(TVFTest, TvfSerializationAndDeserializationWithMultipleSignatures) {
  TypeFactory type_factory;
  FileDescriptorSetMap file_descriptor_set_map;
  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }
  const std::vector<std::string> function_path = {"test_tvf_name"};
  TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("column1",
                                  type_factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema = std::make_unique<TVFRelation>(tvf_schema_columns);
  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;
  FunctionArgumentType relation_with_schema_arg =
      FunctionArgumentType::RelationWithSchema(
          *tvf_schema,
          /*extra_relation_input_columns_allowed=*/false);
  FunctionArgumentType any_relation_arg = FunctionArgumentType::AnyRelation();
  FunctionArgumentType any_arg =
      FunctionArgumentType(ARG_TYPE_ARBITRARY, FunctionArgumentType::REQUIRED);

  int64_t context_id = 0;
  FunctionSignature signature_relation_arg(
      relation_with_schema_arg, {relation_with_schema_arg}, ++context_id);
  FunctionSignature signature_relation_and_any_arg(
      relation_with_schema_arg, {relation_with_schema_arg, any_arg},
      ++context_id);
  FunctionSignature signature_any_relation_arg(
      any_relation_arg, {any_relation_arg}, ++context_id);
  FunctionSignature signature_any_relation_and_any_arg(
      any_relation_arg, {any_relation_arg, any_arg}, ++context_id);

  // Test FixedOutputSchemaTVF
  {
    std::vector<FunctionSignature> signatures{signature_relation_arg,
                                              signature_relation_and_any_arg};
    std::unique_ptr<TableValuedFunction> tvf =
        std::make_unique<FixedOutputSchemaTVF>(function_path, signatures,
                                               *tvf_schema, tvf_options);

    TableValuedFunctionProto tvf_proto;
    GOOGLESQL_ASSERT_OK(tvf->Serialize(&file_descriptor_set_map, &tvf_proto));

    std::unique_ptr<TableValuedFunction> deserialized_tvf;
    GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
        tvf_proto, TypeDeserializer(&type_factory, pools), &deserialized_tvf));

    ASSERT_TRUE(deserialized_tvf->Is<FixedOutputSchemaTVF>());
    ExpectEqualTVFRelations(
        *tvf_schema,
        deserialized_tvf->GetAs<FixedOutputSchemaTVF>()->result_schema());
    EXPECT_EQ(deserialized_tvf->NumSignatures(), 2);
    EXPECT_EQ(deserialized_tvf->DebugString(), tvf->DebugString());
  }

  // Test ForwardInputSchemaToOutputSchemaTVF
  {
    std::vector<FunctionSignature> signatures{signature_relation_arg,
                                              signature_relation_and_any_arg,
                                              signature_any_relation_arg};
    std::unique_ptr<TableValuedFunction> tvf =
        std::make_unique<ForwardInputSchemaToOutputSchemaTVF>(
            function_path, signatures, tvf_options);

    TableValuedFunctionProto tvf_proto;
    GOOGLESQL_ASSERT_OK(tvf->Serialize(&file_descriptor_set_map, &tvf_proto));

    std::unique_ptr<TableValuedFunction> deserialized_tvf;
    GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
        tvf_proto, TypeDeserializer(&type_factory, pools), &deserialized_tvf));
    ASSERT_TRUE(deserialized_tvf->Is<ForwardInputSchemaToOutputSchemaTVF>());
    EXPECT_EQ(deserialized_tvf->NumSignatures(), 3);
    EXPECT_EQ(deserialized_tvf->DebugString(), tvf->DebugString());
  }

  // Test ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF
  {
    std::vector<FunctionSignature> signatures{
        signature_any_relation_arg, signature_any_relation_and_any_arg};
    TVFSchemaColumn extra_column("extra_col",
                                 type_factory.MakeSimpleType(TYPE_STRING));
    std::unique_ptr<TableValuedFunction> tvf =
        std::make_unique<ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF>(
            function_path, signatures,
            std::vector<TVFSchemaColumn>{extra_column}, tvf_options);

    TableValuedFunctionProto tvf_proto;
    GOOGLESQL_ASSERT_OK(tvf->Serialize(&file_descriptor_set_map, &tvf_proto));

    std::unique_ptr<TableValuedFunction> deserialized_tvf;
    GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
        tvf_proto, TypeDeserializer(&type_factory, pools), &deserialized_tvf));

    ASSERT_TRUE(
        deserialized_tvf
            ->Is<ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF>());
    EXPECT_EQ(deserialized_tvf->NumSignatures(), 2);
    EXPECT_EQ(deserialized_tvf->DebugString(), tvf->DebugString());
  }
}

TEST(TVFTest, TestAnonymizationInfo) {
  TypeFactory factory;

  const std::vector<std::string> function_path = {"test_tvf_name"};

  ::googlesql::TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("value", factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema =
      std::make_unique<::googlesql::TVFRelation>(tvf_schema_columns);

  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;

  std::unique_ptr<TableValuedFunction> tvf_with_userid =
      std::make_unique<FixedOutputSchemaTVF>(
          function_path,
          std::vector<FunctionSignature>{::googlesql::FunctionSignature(
              FunctionArgumentType::RelationWithSchema(
                  *tvf_schema,
                  /*extra_relation_input_columns_allowed=*/false),
              {::googlesql::FunctionArgumentType(
                  ::googlesql::ARG_TYPE_ARBITRARY,
                  ::googlesql::FunctionArgumentType::REPEATED)},
              /*context_id=*/static_cast<int64_t>(0))},
          *tvf_schema, tvf_options);

  GOOGLESQL_ASSERT_OK(tvf_with_userid->SetUserIdColumnNamePath({"value"}));

  std::optional<const AnonymizationInfo> anonymization_info =
      tvf_with_userid->anonymization_info();

  EXPECT_TRUE(anonymization_info.has_value());
  EXPECT_EQ(anonymization_info->UserIdColumnNamePath().size(), 1);
  EXPECT_EQ(anonymization_info->UserIdColumnNamePath().at(0), "value");

  FileDescriptorSetMap file_descriptor_set_map;
  TableValuedFunctionProto tvf_proto;
  GOOGLESQL_ASSERT_OK(tvf_with_userid->Serialize(&file_descriptor_set_map, &tvf_proto));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  TypeFactory type_factory;
  std::unique_ptr<TableValuedFunction> deserialized_tvf_with_userid;
  GOOGLESQL_ASSERT_OK(TableValuedFunction::Deserialize(
      tvf_proto, TypeDeserializer(&type_factory, pools),
      &deserialized_tvf_with_userid));
  EXPECT_TRUE(deserialized_tvf_with_userid->Is<TableValuedFunction>());
  std::optional<const AnonymizationInfo> deserialized_anonymization_info =
      deserialized_tvf_with_userid->GetAs<TableValuedFunction>()
          ->anonymization_info();

  EXPECT_TRUE(deserialized_anonymization_info.has_value());
  EXPECT_EQ(deserialized_anonymization_info->UserIdColumnNamePath().size(), 1);
  EXPECT_EQ(deserialized_anonymization_info->UserIdColumnNamePath().at(0),
            "value");
}

TEST(TVFTest, TestTableValueFunctionConstructorWithAnonymizationInfo) {
  TypeFactory factory;

  const std::vector<std::string> function_path = {"test_tvf_name"};

  ::googlesql::TVFRelation::ColumnList tvf_schema_columns;
  tvf_schema_columns.emplace_back("value", factory.MakeSimpleType(TYPE_INT64));
  auto tvf_schema =
      std::make_unique<::googlesql::TVFRelation>(tvf_schema_columns);

  TableValuedFunctionOptions tvf_options;
  tvf_options.uses_upper_case_sql_name = false;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnonymizationInfo> anonymization_info,
                       AnonymizationInfo::Create({"value"}));

  std::unique_ptr<TableValuedFunction> tvf_with_userid =
      std::make_unique<FixedOutputSchemaTVF>(
          function_path,
          std::vector<FunctionSignature>{FunctionSignature(
              FunctionArgumentType::RelationWithSchema(
                  *tvf_schema,
                  /*extra_relation_input_columns_allowed=*/false),
              {::googlesql::FunctionArgumentType(
                  ::googlesql::ARG_TYPE_ARBITRARY,
                  ::googlesql::FunctionArgumentType::REPEATED)},
              /*context_id=*/static_cast<int64_t>(0))},
          std::move(anonymization_info), *tvf_schema, tvf_options);

  std::optional<const AnonymizationInfo> tvf_anonymization_info =
      tvf_with_userid->anonymization_info();

  EXPECT_TRUE(tvf_anonymization_info.has_value());
  EXPECT_EQ(tvf_anonymization_info->UserIdColumnNamePath().size(), 1);
  EXPECT_EQ(tvf_anonymization_info->UserIdColumnNamePath().at(0), "value");
}

TEST(TVFTest, TestGetSQLDeclarationForValueTable) {
  TVFRelation tvf_relation = TVFRelation::ValueTable(types::Int64Type());

  std::string sql_declaration =
      tvf_relation.GetSQLDeclaration(ProductMode::PRODUCT_EXTERNAL);
  EXPECT_EQ(sql_declaration, "TABLE<INT64>");
}

TEST(TVFTest, TestGetSQLDeclarationForTableWithRegularColumnNames) {
  TVFRelation::Column int64_col =
      TVFSchemaColumn("int64_col", types::Int64Type());
  TVFRelation::Column double_col =
      TVFSchemaColumn("double_col", types::DoubleType());
  TVFRelation::ColumnList columns = {int64_col, double_col};
  TVFRelation tvf_relation(columns);

  std::string sql_declaration =
      tvf_relation.GetSQLDeclaration(ProductMode::PRODUCT_EXTERNAL);
  EXPECT_EQ(sql_declaration, "TABLE<int64_col INT64, double_col FLOAT64>");
}

TEST(TVFTest, TestGetSQLDeclarationForTableWithReservedColumnNames) {
  TVFRelation::Column int64_col = TVFSchemaColumn("window", types::Int64Type());
  TVFRelation::Column double_col = TVFSchemaColumn("from", types::DoubleType());
  TVFRelation::ColumnList columns = {int64_col, double_col};
  TVFRelation tvf_relation(columns);

  std::string sql_declaration =
      tvf_relation.GetSQLDeclaration(ProductMode::PRODUCT_EXTERNAL);
  EXPECT_EQ(sql_declaration, "TABLE<`window` INT64, `from` FLOAT64>");
}

TEST(TVFTest, TestGetSQLDeclarationForTableWithEmptyColumnName) {
  TVFRelation::Column int64_col = TVFSchemaColumn("", types::Int64Type());
  TVFRelation::Column double_col = TVFSchemaColumn("col2", types::DoubleType());
  TVFRelation::ColumnList columns = {int64_col, double_col};
  TVFRelation tvf_relation(columns);

  std::string sql_declaration =
      tvf_relation.GetSQLDeclaration(ProductMode::PRODUCT_EXTERNAL);
  EXPECT_EQ(sql_declaration, "TABLE<INT64, col2 FLOAT64>");
}

TEST(TVFTest, TVFInputArgumentType_Copyable) {
  TVFRelation::Column int64_col = TVFSchemaColumn("window", types::Int64Type());
  TVFInputArgumentType arg = TVFInputArgumentType(TVFRelation({int64_col}));
  TVFInputArgumentType copy = arg;
  EXPECT_EQ(copy.DebugString(), arg.DebugString());
  TVFInputArgumentType moved = std::move(arg);
  EXPECT_EQ(moved.DebugString(), copy.DebugString());
}

TEST(TVFTest, NoGraphSignatureAndJustScalarsOverload) {
  TypeFactory factory;
  std::unique_ptr<TableValuedFunction> tvf;
  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_int64_value_table =
      TVFRelation::ValueTable(googlesql::types::Int64Type());
  TVFSchemaColumn int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::ColumnList columns = {int64_col};
  TVFRelation tvf_relation(columns);

  FixedOutputSchemaTVF fine_tvf(
      {"tvf"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             tvf_relation,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyGraph()}, -1),
       FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             tvf_relation,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation()}, -1)},
      TVFRelation{{int64_col}});

  EXPECT_DEATH(tvf.reset(new FixedOutputSchemaTVF(
                   {"tvf"},
                   {FunctionSignature(
                        FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyGraph()}, -1),
                    FunctionSignature(
                        FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType(googlesql::types::StringType(),
                                              FunctionArgumentType::REQUIRED)},
                        -1)},
                   TVFRelation{{int64_col}})),
               HasSubstr("signature with a graph argument must not have any "
                         "signatures that just take scalars"));
}

TEST(TVFTest, NoGraphSignatureAndJustScalarsOverloadAddSignature) {
  TypeFactory factory;
  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_int64_value_table =
      TVFRelation::ValueTable(googlesql::types::Int64Type());
  TVFSchemaColumn int64_col =
      TVFSchemaColumn("int64_col", googlesql::types::Int64Type());
  TVFRelation::ColumnList columns = {int64_col};
  TVFRelation tvf_relation(columns);

  FixedOutputSchemaTVF tvf({"tvf"}, {}, TVFRelation{{int64_col}});
  GOOGLESQL_ASSERT_OK(tvf.AddSignature(
      FunctionSignature(FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyGraph()}, -1)));
  GOOGLESQL_ASSERT_OK(tvf.AddSignature(
      FunctionSignature(FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyRelation()}, -1)));
  GOOGLESQL_ASSERT_OK(tvf.AddSignature(
      FunctionSignature(FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyModel()}, -1)));
  GOOGLESQL_ASSERT_OK(tvf.AddSignature(
      FunctionSignature(FunctionArgumentType::RelationWithSchema(
                            tvf_relation,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyModel(),
                         FunctionArgumentType(googlesql::types::StringType(),
                                              FunctionArgumentType::REQUIRED)},
                        -1)));
  ASSERT_THAT(
      tvf.AddSignature(FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              tvf_relation,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(googlesql::types::StringType(),
                                FunctionArgumentType::REQUIRED)},
          -1)),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("signature with a graph argument must not have any "
                         "signatures that just take scalars")));
}

TEST(TableValuedFunctionOptionsTest, NoRequiredFeatures) {
  TableValuedFunctionOptions options;
  EXPECT_EQ(options.RequiresFeature(FEATURE_ANALYTIC_FUNCTIONS), false);
  EXPECT_EQ(options.CheckAllRequiredFeaturesAreEnabled(
                {FEATURE_ANALYTIC_FUNCTIONS, FEATURE_TABLESAMPLE}),
            true);
  SerializeDeserializeAndCompare(options);
}

TEST(TableValuedFunctionOptionsTest, OneRequiredFeature) {
  TableValuedFunctionOptions options;
  options.AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_EQ(options.RequiresFeature(FEATURE_ANALYTIC_FUNCTIONS), true);
  EXPECT_EQ(options.RequiresFeature(FEATURE_TABLESAMPLE), false);
  EXPECT_EQ(options.CheckAllRequiredFeaturesAreEnabled(
                {FEATURE_ANALYTIC_FUNCTIONS, FEATURE_TABLESAMPLE}),
            true);
  EXPECT_EQ(options.CheckAllRequiredFeaturesAreEnabled(
                {FEATURE_TABLESAMPLE, FEATURE_DISALLOW_GROUP_BY_FLOAT}),
            false);
  SerializeDeserializeAndCompare(options);
}

TEST(TableValuedFunctionOptionsTest, ManyRequiredFeatures) {
  TableValuedFunctionOptions options;
  options.AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS)
      .AddRequiredLanguageFeature(FEATURE_TABLESAMPLE)
      .AddRequiredLanguageFeature(FEATURE_DISALLOW_GROUP_BY_FLOAT)
      .AddRequiredLanguageFeature(FEATURE_TIMESTAMP_NANOS)
      .AddRequiredLanguageFeature(FEATURE_DML_UPDATE_WITH_JOIN);
  EXPECT_EQ(options.RequiresFeature(FEATURE_ANALYTIC_FUNCTIONS), true);
  EXPECT_EQ(options.RequiresFeature(FEATURE_TABLESAMPLE), true);
  EXPECT_EQ(options.RequiresFeature(FEATURE_DISALLOW_GROUP_BY_FLOAT), true);
  EXPECT_EQ(options.RequiresFeature(FEATURE_TIMESTAMP_NANOS), true);
  EXPECT_EQ(options.RequiresFeature(FEATURE_DML_UPDATE_WITH_JOIN), true);
  EXPECT_EQ(options.CheckAllRequiredFeaturesAreEnabled(
                {FEATURE_ANALYTIC_FUNCTIONS, FEATURE_TABLESAMPLE,
                 FEATURE_DISALLOW_GROUP_BY_FLOAT, FEATURE_TIMESTAMP_NANOS,
                 FEATURE_DML_UPDATE_WITH_JOIN}),
            true);
  EXPECT_EQ(options.CheckAllRequiredFeaturesAreEnabled(
                {FEATURE_ANALYTIC_FUNCTIONS, FEATURE_TEMPLATE_FUNCTIONS}),
            false);
  SerializeDeserializeAndCompare(options);
}

}  // namespace googlesql
