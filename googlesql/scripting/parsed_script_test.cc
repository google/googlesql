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

#include "googlesql/scripting/parsed_script.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/scripting/control_flow_graph.h"
#include "googlesql/scripting/type_aliases.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

namespace googlesql {
namespace testing {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Pair;
using ::testing::TestWithParam;
using ::testing::UnorderedElementsAre;
using ::testing::ValuesIn;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

class TestInput {
 public:
  explicit TestInput(const googlesql_base::SourceLocation& location,
                     const std::string& sql)
      : location_(location),
        sql_(sql),
        error_(""),
        owned_names_({}),
        query_parameters_check_strict_mode_(false) {}
  explicit TestInput(const googlesql_base::SourceLocation& location,
                     const std::string& sql,
                     const std::vector<std::string>& named_parameters,
                     const std::string& error = "",
                     bool query_parameters_check_strict_mode = false)
      : location_(location),
        sql_(sql),
        error_(error),
        owned_names_(named_parameters),
        query_parameters_check_strict_mode_(
            query_parameters_check_strict_mode) {
    ParsedScript::StringSet ids;
    for (const std::string& name : named_parameters) {
      IdString id = IdString::MakeGlobal(name);
      ids.insert(id.ToStringView());
    }
    parameters_ = ids;
  }

  explicit TestInput(const googlesql_base::SourceLocation& location,
                     const std::string& sql,
                     const std::pair<int64_t, int64_t>& positional_parameters,
                     const std::string& error = "",
                     bool query_parameters_check_strict_mode = false)
      : location_(location),
        sql_(sql),
        error_(error),
        parameters_(positional_parameters),
        query_parameters_check_strict_mode_(
            query_parameters_check_strict_mode) {}

  const std::string& sql() const { return sql_; }
  const std::string& error() const { return error_; }
  bool query_parameters_check_strict_mode() const {
    return query_parameters_check_strict_mode_;
  }

  bool has_named_parameters() const {
    return std::holds_alternative<ParsedScript::StringSet>(parameters_);
  }

  const ParsedScript::StringSet& named_parameters() const {
    return std::get<ParsedScript::StringSet>(parameters_);
  }
  const std::pair<int64_t, int64_t>& positional_parameters() const {
    return std::get<std::pair<int64_t, int64_t>>(parameters_);
  }

  const googlesql_base::SourceLocation& location() const { return location_; }

  // Show a user-friendly name when a failing TestInput appears in a Sponge log.
  friend std::ostream& operator<<(std::ostream& os,
                                  const TestInput& test_input) {
    return os << "TestInput (" << test_input.location().file_name() << ":"
              << test_input.location().line() << "):\n"
              << test_input.sql();
  }

 private:
  googlesql_base::SourceLocation location_;
  const std::string sql_;
  const std::string error_;
  const std::vector<std::string> owned_names_;
  std::variant<ParsedScript::StringSet, std::pair<int64_t, int64_t>>
      parameters_;
  const bool query_parameters_check_strict_mode_;
};

TestInput TestInputWithError(const googlesql_base::SourceLocation& location,
                             const std::string& error, const std::string& sql) {
  return TestInput(location, sql, std::vector<std::string>{}, error);
}

class TestCase {
 public:
  explicit TestCase(const std::vector<TestInput>& inputs) : inputs_(inputs) {}

  TestCase(const std::string& sql, const std::string& error)
      : sql_(sql), error_(error) {}

  const std::vector<TestInput>& inputs() const { return inputs_; }

 private:
  const std::vector<TestInput> inputs_;
  const std::string sql_;
  const std::string error_;
};

class ScriptValidationTest
    : public TestWithParam<std::variant<TestCase, TestInput>> {
 public:
  void SetUp() override {
    ValueWithTypeParameter vtp;
    IdString test_predefined_var = IdString::MakeGlobal("test_predefined_var1");
    parsed_script_options_.predefined_variables.insert(
        {test_predefined_var, vtp});
    parsed_script_options_.system_variables_allowed_before_declare.push_back(
        "test_system_variable");
  }
  void CheckStatement(const ParseLocationRange& range,
                      const ParsedScript* parsed, const TestInput& stmt) {
    ParsedScript::StringSet actual_named_params =
        parsed->GetNamedParameters(range);
    PositionalParameterRange actual_pos_params =
        parsed->GetPositionalParameters(range);

    if (stmt.has_named_parameters()) {
      std::set<std::string> expected_named_params;
      for (absl::string_view name : stmt.named_parameters()) {
        expected_named_params.insert(absl::AsciiStrToLower(name));
      }
      std::set<std::string> lower_actual_names;
      for (absl::string_view name : actual_named_params) {
        lower_actual_names.insert(absl::AsciiStrToLower(name));
      }
      EXPECT_EQ(expected_named_params, lower_actual_names);
    } else {
      std::pair<int64_t, int64_t> expected_pos_params =
          stmt.positional_parameters();
      EXPECT_EQ(actual_pos_params.start_param_index, expected_pos_params.first);
      EXPECT_EQ(actual_pos_params.num_params, expected_pos_params.second);
    }
  }

  void CheckTestCase(const TestCase& test_case) {
    std::string script = absl::StrJoin(
        test_case.inputs(), "\n", [](std::string* out, const TestInput& input) {
          absl::StrAppend(out, input.sql());
        });
    SCOPED_TRACE(script);

    ParsedScript::QueryParameters parameters = std::nullopt;

    ParsedScript::StringSet names;
    int positionals = 0;
    for (const TestInput& input : test_case.inputs()) {
      if (input.has_named_parameters()) {
        for (absl::string_view id : input.named_parameters()) {
          names.insert(id);
        }
      } else {
        positionals += input.positional_parameters().second;
      }
    }

    if (!names.empty()) {
      parameters = names;
    } else if (positionals > 0) {
      parameters = positionals;
    }

    ParserOptions options;
    std::unique_ptr<ParsedScript> parsed =
        ParsedScript::Create(script, options, {.mode = ERROR_MESSAGE_ONE_LINE},
                             parsed_script_options_)
            .value();
    GOOGLESQL_EXPECT_OK(parsed->CheckQueryParameters(parameters));

    absl::Span<const ASTStatement* const> stmts =
        parsed->script()->statement_list();
    for (int i = 0; i < stmts.size(); i++) {
      CheckStatement(stmts[i]->location(), parsed.get(), test_case.inputs()[i]);
    }
  }

  void CheckTestInput(const TestInput& test_input) {
    SCOPED_TRACE(test_input.sql());

    ParsedScript::QueryParameters parameters = std::nullopt;
    ParsedScript::StringSet names;
    int positionals = 0;
    if (test_input.has_named_parameters()) {
      for (absl::string_view id : test_input.named_parameters()) {
        names.insert(id);
      }
    } else {
      positionals += test_input.positional_parameters().second;
    }
    if (!names.empty()) {
      parameters = names;
    } else if (positionals > 0) {
      parameters = positionals;
    }

    ParserOptions options;
    absl::StatusOr<std::unique_ptr<ParsedScript>> status_or_parsed =
        ParsedScript::Create(test_input.sql(), options,
                             {.mode = ERROR_MESSAGE_ONE_LINE},
                             parsed_script_options_);
    absl::Status status = status_or_parsed.status();
    std::unique_ptr<ParsedScript> parsed;
    if (status.ok()) {
      parsed = std::move(status_or_parsed.value());
      status = parsed->CheckQueryParameters(
          parameters, test_input.query_parameters_check_strict_mode());
    }

    if (test_input.error().empty()) {
      EXPECT_THAT(status, IsOk());
      if (status.ok()) {
        CheckStatement(parsed->script()->location(), parsed.get(), test_input);
      }
    } else {
      EXPECT_THAT(status, StatusIs(_, HasSubstr(test_input.error())));
    }
  }

  ParsedScriptOptions parsed_script_options_;
};

TEST_P(ScriptValidationTest, ValidateScripts) {
  std::variant<TestCase, TestInput> param = GetParam();

  if (std::holds_alternative<TestCase>(param)) {
    CheckTestCase(std::get<TestCase>(param));
  } else {
    CheckTestInput(std::get<TestInput>(param));
  }
}

std::vector<std::variant<TestCase, TestInput>> GetScripts() {
  std::vector<std::string> empty_named;
  std::pair<int64_t, int64_t> empty_pos;

  std::vector<std::variant<TestCase, TestInput>> result;
  // Simple test case illustrate a BREAK statement without an enclosing
  // loop. As detection of BREAK and CONTINUE statements outside of a loop
  // is implemented as part of building the control-flow graph, this is
  // covered more thoroughly in the control-flow-graph tests.
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "BREAK without label is only allowed inside "
                         "of a loop body [at 3:7]",
                         R"(
      SELECT 1;
      BREAK;
    )"));

  // Test cases with variables. This checks logic to screen for:
  // - Illegal variable redeclaration or shadowing
  // - Variable declaration outside of the start of the block or script
  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Variable declarations at start
    DECLARE x INT64;
    DECLARE y DEFAULT x;
    DECLARE z INT64 DEFAULT y + 1;
    DECLARE a,b INT64;
    DECLARE c,d INT64 DEFAULT z + 1;
    SELECT a, b, c, d, x, y, z;
  )"));

  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Variable declarations inside BEGIN block
    SELECT 1;
    BEGIN
      DECLARE x INT64;
      DECLARE y DEFAULT x;
      DECLARE z INT64 DEFAULT y + 1;
      DECLARE a,b INT64;
      DECLARE c,d INT64 DEFAULT z + 1;
      SELECT a, b, c, d, x, y, z;
    END;
  )"));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Variable declarations inside nested BEGIN blocks
    SELECT 1;
    BEGIN
      DECLARE x INT64;
      DECLARE y DEFAULT x;
      BEGIN BEGIN
        DECLARE z INT64 DEFAULT y + 1;
        DECLARE a,b INT64;
        DECLARE c,d INT64 DEFAULT z + 1;
        SELECT a, b, c, d, x, y, z;
      END; END;
    END;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Variable declarations are allowed only at the start "
                         "of a block or script [at 4:5]",
                         R"(
    -- Variable declaration after SELECT statement
    SELECT 1;
    DECLARE x INT64;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Variable declarations are allowed only at the start "
                         "of a block or script [at 4:7]",
                         R"(
    -- Variable declaration at start of IF body
    IF TRUE THEN
      DECLARE x INT64;
    END IF;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Variable declarations are allowed only at the start "
                         "of a block or script [at 5:7]",
                         R"(
    -- Variable declaration at start of exception handler without inner BEGIN.
    BEGIN
    EXCEPTION WHEN ERROR THEN
      DECLARE x INT64;
    END;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Variable declarations are allowed only at the start "
                         "of a block or script [at 5:7]",
                         R"(
    -- Variable declaration in middle of BEGIN block
    BEGIN
      SELECT 1;
      DECLARE x INT64;
    END;
  )"));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Variable declarations preceded by allowlisted system variable
    SET @@test_system_variable = "test";
    DECLARE x INT64;
  )"));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Variable declarations preceded by uppercase allowlisted system variable
    SET @@TEST_SYSTEM_VARIABLE = "test";
    DECLARE x INT64;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Variable declarations are allowed only at the start "
                         "of a block or script [at 4:5]",
                         R"(
    -- Variable declarations preceded by system variable that is not allowlisted
    SET @@other_system_variable = "test";
    DECLARE x INT64;
  )"));
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'x' redeclaration [at 3:16]; x "
                                      "previously declared here [at 3:13]",
                                      R"(
    -- Variable redeclaration (same statement)
    DECLARE x, x INT64;
  )"));
  result.push_back(TestInputWithError(
      googlesql_base::SourceLocation::current(),
      "Variable 'test_predefined_var1' redeclaration [at 3:19]",
      R"(
    -- Variable redeclaration with predefined variable
    BEGIN DECLARE test_predefined_var1 INT64; END;
  )"));
  result.push_back(TestInputWithError(
      googlesql_base::SourceLocation::current(),
      "Variable 'test_predefined_var1' redeclaration [at 7:23]",
      R"(
    -- Variable redeclaration with predefined variable
    BEGIN
      IF x < 1 THEN
        LOOP
          BEGIN
              DECLARE test_predefined_var1 INT64;
          END;
        END LOOP;
      END IF;
    END;
  )"));
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'x' redeclaration [at 5:13]; x "
                                      "previously declared here [at 3:13]",
                                      R"(
    -- Variable redeclaration (earlier statement)
    DECLARE x INT64;
    DECLARE y STRING;
    DECLARE x INT64;
  )"));
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'x' redeclaration [at 5:22]; x "
                                      "previously declared here [at 3:17]",
                                      R"(
        -- Variable redeclaration (outer block)
        DECLARE x INT64;
        BEGIN
          DECLARE y, x INT64;
        END;
      )"));
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'X' redeclaration [at 3:20]; X "
                                      "previously declared here [at 3:17]",
                                      R"(
        -- Variable redeclaration (names differ only by case)
        DECLARE x, X INT64;
      )"));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             R"(
    -- Disjoint blocks declaring the same variable is ok.
    BEGIN
      DECLARE x INT64;
      SELECT x;
    END;
    BEGIN
      DECLARE x STRING;
      SELECT x;
    END;
  )"));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             R"(
    -- An EXCEPTION clause uses a different variable scope from its
    -- associated BEGIN clause, so reuse of 'x' here is ok.
    BEGIN
      DECLARE x INT64;
      SELECT x;
    EXCEPTION WHEN ERROR THEN
      BEGIN
        DECLARE x INT64;
      END;
    END;
  )"));

  // Test cases with RAISE.
  result.push_back(TestInput(googlesql_base::SourceLocation::current(), R"(
    -- Legal uses of RAISE
    RAISE USING MESSAGE = "test";
    BEGIN
      DECLARE x INT64;
      SELECT x;
    EXCEPTION WHEN ERROR THEN
      IF x = 1 THEN
        RAISE;
      END IF;
      RAISE;
    END;
  )"));
  result.push_back(
      TestInputWithError(googlesql_base::SourceLocation::current(),
                         "Cannot re-raise an existing exception outside of an "
                         "exception handler [at 2:5]",
                         R"(
    RAISE;
  )"));

  // Test cases with query parameters
  result.push_back(
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_named));
  result.push_back(
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_pos));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             "SELECT @a, @A, @b, @a;", {"a", "b"}));
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             "SELECT @a, @a, @b, @a;", {"a"},
                             "Unknown named query parameter: b"));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_pos),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT ?;", {0, 1}),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT ?;", {0, 1}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_pos),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_pos),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT ?;", {0, 1}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT ?, ?;", {1, 2}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT ?;", {3, 1}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_pos),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_named),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @a;", {"a"}),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT 1;", empty_named),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @a;", {"a"}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @a, @b;", {"a", "b"}),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @a;", {"a"}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @b;", {"b"}),
  }));
  result.push_back(TestCase({
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @A;", {"a"}),
      TestInput(googlesql_base::SourceLocation::current(), "SELECT @B;", {"b"}),
  }));

  // Test cases with CREATE PROCEDURE.

  // Variable inside the procedure doesn't conflict with the variable defined
  // before the procedure.
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             R"(
    DECLARE x INT64;
    CREATE OR REPLACE PROCEDURE abc() BEGIN
        DECLARE x INT64;
    END;
  )"));

  // Same as above but with nested procedure.
  result.push_back(TestInput(googlesql_base::SourceLocation::current(),
                             R"(
    DECLARE x INT64;
    CREATE OR REPLACE PROCEDURE p1() BEGIN
        DECLARE x INT64;
        CREATE OR REPLACE PROCEDURE p2() BEGIN
            DECLARE x INT64;
            CREATE OR REPLACE PROCEDURE p3() BEGIN
                DECLARE x INT64;
            END;
        END;
    END;
  )"));

  // Variables after procedure definition are still checked for redeclaration.
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'x' redeclaration [at 7:17]; x "
                                      "previously declared here [at 2:15]",
                                      R"(
      DECLARE x INT64;
      CREATE OR REPLACE PROCEDURE abc() BEGIN
          DECLARE x INT64;
      END;
      BEGIN
        DECLARE x INT64;
      END;
      )"));

  // Variables inside the procedure are still checked for redeclaration.
  result.push_back(TestInputWithError(googlesql_base::SourceLocation::current(),
                                      "Variable 'x' redeclaration [at 5:21]; x "
                                      "previously declared here [at 3:19]",
                                      R"(
      CREATE OR REPLACE PROCEDURE abc() BEGIN
          DECLARE x INT64;
          BEGIN
            DECLARE x INT64;
          END;
      END;
      )"));

  result.push_back(TestInput(googlesql_base::SourceLocation::current(), "SELECT @a, @b;",
                             std::vector<std::string>(),
                             "Unknown named query parameter: a",
                             /*query_parameters_check_strict_mode=*/true));
  return result;
}

INSTANTIATE_TEST_CASE_P(RunScriptValidationTest, ScriptValidationTest,
                        ValuesIn(GetScripts()));

TEST(ParsedScript, VariablesInScopeAtNode_TopLevel) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  ParsedScriptOptions parsed_script_options = {
      .system_variables_allowed_before_declare = {"sysvar1"}};

  constexpr absl::string_view kScript = R"(
    DECLARE x DEFAULT 23;
    SELECT x;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), parsed_script_options));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTStatement* decl_stmt = parsed_script->script()->statement_list()[0];
  const ASTStatement* select_stmt =
      parsed_script->script()->statement_list()[1];
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  IdString var_x = decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, decl_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_CurrentNodeIsVarDecl) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  ParsedScriptOptions parsed_script_options = {
      .system_variables_allowed_before_declare = {"sysvar1"}};

  constexpr absl::string_view kScript = R"(
    DECLARE x INT64;
    DECLARE y INT64; -- We are here
    DECLARE z INT64;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), parsed_script_options));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTStatement* x_decl = parsed_script->script()->statement_list()[0];
  const ASTStatement* y_decl = parsed_script->script()->statement_list()[1];
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(y_decl, ControlFlowNode::Kind::kDefault);

  IdString var_x = x_decl->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));

  // At y's declaration, only variable x is in scope. y is not in scope because
  // y's declaration has not happened yet (it's about to happen), and z's
  // declaration is definitely not reached.
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, x_decl)));
}

TEST(ParsedScript, VariablesInScopeAtNode_TopLevelWithSysVarAssignment) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  ParsedScriptOptions parsed_script_options = {
      .system_variables_allowed_before_declare = {"sysvar1"}};

  constexpr absl::string_view kScript = R"(
    SET @@sysvar1 = 3;
    DECLARE x DEFAULT 23;
    SELECT x;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), parsed_script_options));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTStatement* decl_stmt = parsed_script->script()->statement_list()[1];
  const ASTStatement* select_stmt =
      parsed_script->script()->statement_list()[2];
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  IdString var_x = decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, decl_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_BlockScope) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    BEGIN
      DECLARE x DEFAULT 23;
      SELECT x;
    END;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTBeginEndBlock* begin = parsed_script->script()
                                      ->statement_list()[0]
                                      ->GetAsOrDie<ASTBeginEndBlock>();
  const ASTStatement* decl_stmt = begin->statement_list()[0];
  const ASTStatement* select_stmt = begin->statement_list()[1];
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  IdString var_x = decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, decl_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_NestedBlockScope) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    BEGIN
      DECLARE x DEFAULT 23;
      BEGIN
        DECLARE y DEFAULT 24;
        SELECT x, y;
      END;
    END;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTBeginEndBlock* outer_begin = parsed_script->script()
                                            ->statement_list()[0]
                                            ->GetAsOrDie<ASTBeginEndBlock>();
  const ASTStatement* outer_decl_stmt = outer_begin->statement_list()[0];
  const ASTBeginEndBlock* inner_begin =
      outer_begin->statement_list()[1]->GetAsOrDie<ASTBeginEndBlock>();
  const ASTStatement* inner_decl_stmt = inner_begin->statement_list()[0];
  const ASTStatement* select_stmt = inner_begin->statement_list()[1];
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  IdString var_x = outer_decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");
  IdString var_y = inner_decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_y.ToStringView(), "y");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, outer_decl_stmt),
                                         Pair(var_y, inner_decl_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_BlockVariableOutOfScope) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    BEGIN
      DECLARE x DEFAULT 23;
      BEGIN
        DECLARE y DEFAULT 24;
      END;
      SELECT x;
    END;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTBeginEndBlock* outer_begin = parsed_script->script()
                                            ->statement_list()[0]
                                            ->GetAsOrDie<ASTBeginEndBlock>();
  const ASTStatement* outer_decl_stmt = outer_begin->statement_list()[0];
  const ASTBeginEndBlock* inner_begin =
      outer_begin->statement_list()[1]->GetAsOrDie<ASTBeginEndBlock>();
  const ASTStatement* inner_decl_stmt = inner_begin->statement_list()[0];
  const ASTQueryStatement* select_stmt =
      outer_begin->statement_list()[2]->GetAsOrDie<ASTQueryStatement>();
  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  IdString var_x = outer_decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");
  IdString var_y = inner_decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_y.ToStringView(), "y");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, outer_decl_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_ForLoopBody) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    FOR x IN (SELECT 1) DO
      SELECT x;
    END FOR;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTForInStatement* for_stmt = parsed_script->script()
                                          ->statement_list()[0]
                                          ->GetAsOrDie<ASTForInStatement>();
  const ASTQueryStatement* select_stmt =
      for_stmt->body()->statement_list(0)->GetAsOrDie<ASTQueryStatement>();
  IdString var_x = for_stmt->variable()->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");

  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(select_stmt, ControlFlowNode::Kind::kDefault);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_x, for_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_ForLoopAdvancement) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    DECLARE y INT64;
    FOR x IN (SELECT 1) DO
      SELECT x;
    END FOR;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTStatement* decl_stmt = parsed_script->script()->statement_list()[0];
  const ASTForInStatement* for_stmt = parsed_script->script()
                                          ->statement_list()[1]
                                          ->GetAsOrDie<ASTForInStatement>();
  IdString var_x = for_stmt->variable()->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");
  IdString var_y = decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_y.ToStringView(), "y");

  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(for_stmt, ControlFlowNode::Kind::kForAdvance);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_y, decl_stmt),
                                         Pair(var_x, for_stmt)));
}

TEST(ParsedScript, VariablesInScopeAtNode_ForLoopInitialization) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  constexpr absl::string_view kScript = R"(
    DECLARE y INT64;
    FOR x IN (SELECT 1) DO
      SELECT x;
    END FOR;
  )";

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(kScript, ParserOptions(language_options),
                           ErrorMessageOptions(), ParsedScriptOptions{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ControlFlowGraph> graph,
      ControlFlowGraph::Create(parsed_script->script(), kScript));

  const ASTStatement* decl_stmt = parsed_script->script()->statement_list()[0];
  const ASTForInStatement* for_stmt = parsed_script->script()
                                          ->statement_list()[1]
                                          ->GetAsOrDie<ASTForInStatement>();
  IdString var_x = for_stmt->variable()->GetAsIdString();
  ASSERT_EQ(var_x.ToStringView(), "x");
  IdString var_y = decl_stmt->GetAsOrDie<ASTVariableDeclaration>()
                       ->variable_list()
                       ->identifier_list()[0]
                       ->GetAsIdString();
  ASSERT_EQ(var_y.ToStringView(), "y");

  const ControlFlowNode* cfg_node =
      graph->GetControlFlowNode(for_stmt, ControlFlowNode::Kind::kForInitial);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParsedScript::VariableCreationMap vars,
                       parsed_script->GetVariablesInScopeAtNode(cfg_node));
  EXPECT_THAT(vars, UnorderedElementsAre(Pair(var_y, decl_stmt)));
}

}  // namespace
}  // namespace testing
}  // namespace googlesql
