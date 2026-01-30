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

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/deidentify.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/parse_tokens.h"
#include "googlesql/public/testing/test_case_options_util.h"
#include "googlesql/scripting/parse_helpers.h"
#include "googlesql/scripting/script_segment.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/edit_distance.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

ABSL_FLAG(std::string, test_file, "", "location of test data file.");

ABSL_DECLARE_FLAG(bool, output_asc_explicitly);

namespace googlesql {

using parser::MacroExpansionMode;

class RunParserTest : public ::testing::Test {
 public:  // Pointer-to-member-function usage requires public member functions
  // Valid options in the case cases:
  //   mode - "statement" (default) or "expression" or "type"
  const std::string kModeOption = "mode";
  // Strips off the trailing newlines added at the end of a test query when set
  // to true. On false (default) does nothing.
  const std::string kStripTrailingNewline = "strip_trailing_newline";
  // Re-runs the test with all newlines replaced with "\r" or "\r\n", expecting
  // the output to be the same as with \n.
  const std::string kTestNewlineTypes = "test_newline_types";
  // Use ParseNextStatement to iteratively parse multiple statements
  // from one input string.
  const std::string kParseMultiple = "parse_multiple";
  // Disable this to skip testing ParseTokens.  This is necessary in some
  // cases where our regex hacks can't unify the before/after parse trees.
  const std::string kTestGetParseTokens = "test_get_parse_tokens";
  // Disable this to skip testing Unparse.  This is necessary in some
  // cases where identifiers are not (yet) properly escaped when unparsing.
  const std::string kTestUnparse = "test_unparse";
  // Allows a list of generic entity types. Multiple entity types are comma
  // separated and whitespaces are preserved as part of the type string.
  const std::string kSupportedGenericEntityTypes =
      "supported_generic_entity_types";
  // Allows a list of generic sub entity types. Multiple entity types are comma
  // separated and whitespaces are preserved as part of the type string.
  const std::string kSupportedGenericSubEntityTypes =
      "supported_generic_sub_entity_types";
  // Indicates that QUALIFY is a reserved keyword.
  const std::string kQualifyReserved = "qualify_reserved";
  const std::string kReserveMatchRecognize = "reserve_match_recognize";
  // Reserves the GRAPH_TABLE keyword.
  const std::string kReserveGraphTable = "reserve_graph_table";
  // Show the text of the SQL fragment for each parse location, rather than only
  // the integer range.
  const std::string kShowParseLocationText = "show_parse_location_text";

  // Controls macro expansion
  const std::string kMacroExpansionMode = "macro_expansion_mode";

  // Controls the node kind of the statement to be parsed.
  const std::string kNodeKind = "node_kind";

  RunParserTest() {
      test_case_options_.RegisterString(kModeOption, "statement");
      // TODO: 439682125 - This should be changed to ALL, DEV or MAXIMUM.
      test_case_options_.RegisterString(kLanguageFeatures, "NONE");
      test_case_options_.RegisterBool(kStripTrailingNewline, false);
      test_case_options_.RegisterBool(kTestNewlineTypes, true);
      test_case_options_.RegisterBool(kParseMultiple, false);
      test_case_options_.RegisterBool(kTestGetParseTokens, true);
      test_case_options_.RegisterBool(kTestUnparse, true);
      test_case_options_.RegisterBool(kQualifyReserved, true);
      test_case_options_.RegisterBool(kReserveMatchRecognize, true);
      test_case_options_.RegisterBool(kReserveGraphTable, false);
      test_case_options_.RegisterString(kSupportedGenericEntityTypes, "");
      test_case_options_.RegisterString(kSupportedGenericSubEntityTypes, "");
      test_case_options_.RegisterBool(kShowParseLocationText, true);
      test_case_options_.RegisterString(kMacroExpansionMode, "none");
      test_case_options_.RegisterString(kNodeKind, "Fake");

      // Force a blank line at the start of every test case.
      absl::SetFlag(&FLAGS_file_based_test_driver_insert_leading_blank_lines,
                    1);
      absl::SetFlag(&FLAGS_output_asc_explicitly, true);
  }

  void RunTest(absl::string_view test_case_input,
               file_based_test_driver::RunTestCaseResult* test_result) {
    std::string test_case = std::string(test_case_input);
    const absl::Status options_status =
        test_case_options_.ParseTestCaseOptions(&test_case);
    if (!options_status.ok()) {
      test_result->AddTestOutput(absl::StrCat(
          "ERROR: Invalid test case options: ", options_status.ToString()));
      return;
    }

    if (test_case_options_.GetBool(kStripTrailingNewline)) {
      test_case = absl::StripSuffix(test_case, "\n");
    }

    GOOGLESQL_VLOG(1) << "Parsing\n" << test_case;

    std::vector<std::string> test_outputs;
    RunTestForNewlineTypes(test_case, &test_outputs);
    for (const std::string& output : test_outputs) {
      test_result->AddTestOutput(output);
    }
  }

 private:
  // Adds the test outputs in 'test_outputs' to 'annotated_outputs', annotated
  // with 'annotation'.
  void AddAnnotatedTestOutputs(absl::Span<const std::string> test_outputs,
                               absl::string_view annotation,
                               std::vector<std::string>* annotated_outputs) {
    for (const std::string& test_output : test_outputs) {
      annotated_outputs->push_back(absl::StrCat(annotation, test_output));
    }
  }

  // If the output in 'test_outputs' are all equal, adds them to
  // 'merged_outputs' without an annotation. If they are not all equal, adds
  // each set of 'test_outputs' entry separately, annotated with the
  // corresponding annotation from 'annotations'.
  void MergeTestOutputs(absl::Span<const std::vector<std::string>> test_outputs,
                        absl::Span<const std::string> annotations,
                        const RE2* regexp_to_ignore,
                        std::vector<std::string>* merged_outputs) {
    std::vector<std::vector<std::string>> redacted_test_outputs(
        test_outputs.begin(), test_outputs.end());
    if (regexp_to_ignore != nullptr) {
      for (std::vector<std::string>& redacted_test_output :
           redacted_test_outputs) {
        for (std::string& entry : redacted_test_output) {
          RE2::GlobalReplace(&entry, *regexp_to_ignore, "--REDACTED--");
        }
      }
    }
    if (absl::c_all_of(redacted_test_outputs,
                       [&](const std::vector<std::string>& value) {
                         return value == redacted_test_outputs[0];
                       })) {
      // All redacted values are the same. Use the first unredacted output.
      AddAnnotatedTestOutputs(test_outputs[0], "" /* annotation */,
                              merged_outputs);
    } else {
      for (int i = 0; i < test_outputs.size(); ++i) {
        AddAnnotatedTestOutputs(
            test_outputs[i],
            absl::StrCat("[", annotations[i], "]\n") /* annotation */,
            merged_outputs);
      }
    }
  }

  // Runs parser tests with alternate newlines if that option is enabled, and
  // returns merged results if they are the same across newline types, and
  // separate results otherwise.
  void RunTestForNewlineTypes(absl::string_view test_case,
                              std::vector<std::string>* test_outputs) {
    std::vector<std::string> newlines = {"\n", "\r", "\r\n"};
    const std::vector<std::string> newline_annotations = {
        "NEWLINE \\n", "NEWLINE \\r", "NEWLINE \\r\\n"};
    if (!test_case_options_.GetBool(kTestNewlineTypes)) {
      // Only test \n.
      newlines.resize(1);
    }
    std::vector<std::vector<std::string>> test_outputs_by_newline_type(
        newlines.size());
    for (int i = 0; i < newlines.size(); ++i) {
      const std::string modified_test_case =
          absl::StrReplaceAll(test_case, {{"\n", newlines[i]}});
      std::vector<std::string> test_outputs_this_newline_type;
      TestOneOrMulti(modified_test_case, &test_outputs_this_newline_type);
      for (const std::string& newline_test_output :
           test_outputs_this_newline_type) {
        test_outputs_by_newline_type[i].push_back(
            absl::StrReplaceAll(newline_test_output, {{newlines[i], "\n"}}));
      }
    }
    // Ignore the [123-456] location ranges when comparing the outputs. The
    // locations change with the newline type because \r\n is two bytes instead
    // of one.
    static const LazyRE2 regexp_to_ignore = {R"(\[[0-9]+-[0-9]+\])"};
    MergeTestOutputs(test_outputs_by_newline_type, newline_annotations,
                     &(*regexp_to_ignore), test_outputs);
  }

  // Runs 'test_case' in multi-statement or single-statement mode depending on
  // the kParseMultiple option, and adds the outputs to 'test_outputs'.
  void TestOneOrMulti(absl::string_view test_case,
                      std::vector<std::string>* test_outputs) {
    const std::string mode = test_case_options_.GetString(kModeOption);
    if (test_case_options_.GetBool(kParseMultiple)) {
      TestMulti(test_case, mode, test_outputs);
    } else {
      TestOne(test_case, mode, test_outputs);
    }
  }

  // Runs 'test_case' in single-statement mode, in parser mode as indicated by
  // 'mode'. Adds the test outputs to 'test_outputs'.
  void TestOne(absl::string_view test_case, absl::string_view mode,
               std::vector<std::string>* test_outputs) {
    std::unique_ptr<ParserOutput> parser_output;
    const ASTNode* root;
    const absl::Status status =
        ParseWithMode(test_case, mode, &root, &parser_output);
    bool next_statement_is_ctas;
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions guess_parser_options,
                         GetParserOptions());
    const ASTNodeKind guessed_statement_kind = ParseStatementKind(
        test_case, guess_parser_options.language_options(),
        guess_parser_options.macro_expansion_mode(),
        guess_parser_options.macro_catalog(), &next_statement_is_ctas);

    // Ensure that fetching all properties does not fail.
    parser::ASTStatementProperties ast_statement_properties;
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions parser_options, GetParserOptions());
    parser_options.CreateDefaultArenasIfNotSet();
    std::vector<std::unique_ptr<ASTNode>> allocated_ast_nodes;
    GOOGLESQL_ASSERT_OK(ParseNextStatementProperties(
        ParseResumeLocation::FromStringView(test_case), parser_options,
        &allocated_ast_nodes, &ast_statement_properties));

    // The statement kinds fetched from ParseStatementKind() and
    // ParseNextStatementProperties() should match.
    ASSERT_EQ(guessed_statement_kind, ast_statement_properties.node_kind);
    std::string node_kind_string =
        ASTNode::NodeKindToString(ast_statement_properties.node_kind);
    if (test_case_options_.GetString(kNodeKind) != "Fake" &&
        node_kind_string != "<UNKNOWN NODE KIND>") {
      ASSERT_EQ(ASTNode::NodeKindToString(ast_statement_properties.node_kind),
                test_case_options_.GetString(kNodeKind));
    }
    ASSERT_EQ(next_statement_is_ctas,
              ast_statement_properties.is_create_table_as_select)
        << test_case;

    HandleOneParseTree(test_case, mode, status, root, ast_statement_properties,
                       true /* is_single */, test_outputs);
  }

  // Runs 'test_case' in multi-statement mode, in parser mode as indicated by
  // 'mode'. Adds the test outputs to 'test_outputs'.
  void TestMulti(absl::string_view test_case, absl::string_view mode,
                 std::vector<std::string>* test_outputs) {
    ASSERT_EQ("statement", mode)
        << kParseMultiple << " only works on statements";

    ParseResumeLocation location =
        ParseResumeLocation::FromStringView(test_case);
    while (true) {
      std::unique_ptr<ParserOutput> parser_output;

      GOOGLESQL_VLOG(2) << "Starting location file: " << location.filename()
              << ", byte_position: " << location.byte_position();

      bool next_statement_is_ctas;
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions ctas_guess_parser_options,
                           GetParserOptions());
      const ASTNodeKind guessed_statement_kind = ParseNextStatementKind(
          location, ctas_guess_parser_options.language_options(),
          ctas_guess_parser_options.macro_expansion_mode(),
          ctas_guess_parser_options.macro_catalog(), &next_statement_is_ctas);

      // Ensure that fetching all properties does not fail.
      parser::ASTStatementProperties ast_statement_properties;
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions properties_parser_options,
                           GetParserOptions());
      properties_parser_options.CreateDefaultArenasIfNotSet();
      std::vector<std::unique_ptr<ASTNode>> allocated_ast_nodes;
      GOOGLESQL_ASSERT_OK(ParseNextStatementProperties(
          location, properties_parser_options, &allocated_ast_nodes,
          &ast_statement_properties));

      // The statement kinds fetched from ParseNextStatementKind() and
      // ParseNextStatementProperties() should match.
      ASSERT_EQ(guessed_statement_kind, ast_statement_properties.node_kind);
      ASSERT_EQ(next_statement_is_ctas,
                ast_statement_properties.is_create_table_as_select);

      ParseResumeLocation location_copy = location;

      bool at_end_of_input = false;
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions parser_options, GetParserOptions());
      const absl::Status status = ParseNextScriptStatement(
          &location, parser_options, &parser_output, &at_end_of_input);

      const ASTStatement* statement =
          status.ok() ? parser_output->statement() : nullptr;
      std::vector<std::string> one_statement_test_outputs;
      HandleOneParseTree(test_case, mode, status, statement,
                         ast_statement_properties, false /* is_single */,
                         &one_statement_test_outputs);
      test_outputs->insert(test_outputs->end(),
                           one_statement_test_outputs.begin(),
                           one_statement_test_outputs.end());

      GOOGLESQL_VLOG(2) << "status: " << status;
      GOOGLESQL_VLOG(2) << "byte_position: " << location.byte_position();
      if (parser_output != nullptr) {
        GOOGLESQL_VLOG(2) << "at_end_of_input: " << at_end_of_input;
      }

      // Stop after EOF or error.
      if (!status.ok() || at_end_of_input) {
        break;
      }
    }
  }

  // Return true if s1 and s2 have error messages that look
  // "substantially similar", according to edit distance.
  static bool IsSimilarError(const absl::Status& s1, const absl::Status& s2) {
    if (s1.code() != s2.code()) {
      return false;
    }
    int max_diff = 5;
    if (absl::StrContains(s2.message(), "but got string literal:")) {
      // Sometimes we get "but got string literal: " vs "but got: ".
      max_diff += 15;
    }
    max_diff = std::max(max_diff, static_cast<int>(s1.message().size() / 3));
    return googlesql_base::CappedLevenshteinDistance(
               s1.message().begin(), s1.message().end(), s2.message().begin(),
               s2.message().end(), std::equal_to<char>(),
               max_diff + 1) < max_diff;
  }

  // Given an AST, return its DebugString with some some string hacks applied
  // to make it more comparable by hiding some diffs we want to ignore.
  static std::string RedactedDebugString(const ASTNode* tree) {
    if (tree == nullptr) {
      return "<RedactedDebugString got NULL>";
    }
    // Literals and identifiers may come out differently because of
    // normalization of quoting and formatting, so we erase the actual value
    // and just compare the shape of the tree for those. We also erase the
    // location information.
    static const RE2 cleanups[] = {
        {R"((StringLiteralComponent)\(('[^']*')\))"},
        {R"((StringLiteralComponent)\(("[^"]*")\))"},
        {R"((StringLiteralComponent)\((""".*""")\))"},
        {R"((StringLiteralComponent)\(('''.*''')\))"},
        {R"((StringLiteralComponent)\([^)]*\))"},
        {R"((BytesLiteralComponent)\([^)]*\))", RE2::Latin1},
        {R"((FloatLiteral)\([^)]*\))"},
        {R"((IntLiteral)\([^)]*\))"},
        {R"((NumericLiteral)\([^)]*\))"},
        {R"((JSONLiteral)\([^)]*\))"},
        {R"((Identifier)\([^)]*\))"},
        // Instead of skipping this check completely for macro parsing, we
        // leverage the fact that test macros do not have parentheses.
        // For now, all our macro tests are related to parsing tokens correctly
        // and finding the end of the statement.
        // If at any point we need to add test cases where the macro body has
        // parentheses, then we will have to introduce a flag and skip this
        // check, since regex-based replacement cannot really handle any
        // arbitrary macro. In fact, not even parsing would be enough since
        // there is no syntax for macros: they can have closing extra
        // parentheses in the body.
        //
        // Alternatively, we could escape closing parentheses ')' in order to
        // find the closing one, but that pollutes the DebugString() just for
        // this test, so we choose to keep redacting this way until we need
        // tests with parentheses.
        {R"((MacroBody)\([^)]*\))"},
    };
    std::string out = tree->DebugString();
    for (const RE2& re2 : cleanups) {
      RE2::GlobalReplace(&out, re2, "\\1(...)");
    }
    static const LazyRE2 clean_up_location = {R"(\[[0-9]+-[0-9]+\])"};
    RE2::GlobalReplace(&out, *clean_up_location, "(...)");
    // Make the string lowercase because we have places where case
    // gets normalized, like "null" vs "NULL".
    absl::AsciiStrToLower(&out);
    return out;
  }

  void BasicValidateParseLocationRange(size_t input_length,
                                       const ParseLocationRange& range) {
    // Verify filename
    EXPECT_EQ(range.start().filename(), "");
    EXPECT_EQ(range.end().filename(), "");

    // Verify range is well-formed (start <= end).
    EXPECT_LE(range.start().GetByteOffset(), range.end().GetByteOffset());

    // Verify range does not exceed the bounds of the input string.
    EXPECT_GE(range.start().GetByteOffset(), 0);
    EXPECT_LE(range.end().GetByteOffset(), input_length);
  }

  // Perform sanity checks on the parse location ranges of all nodes in the
  // tree:
  // - Start and end position with empty file name.
  // - Start position <= end position.
  // - Parse location range must be within the bounds of the input string.
  // - Parse location range must be within the parent node's parse location
  //   range (except for the root).
  //  Statements only:
  //    - Parse location ranges of sibling nodes must be sorted in the order
  //        of the nodes' child indices, and may not overlap.
  //
  void VerifyParseLocationRanges(absl::string_view test_case,
                                 const ASTNode* root) {
    // Using a stack instead of recursion to avoid overflowing the stack when
    // running against stack_overflow.test.
    std::stack<const ASTNode*> stack;

    BasicValidateParseLocationRange(test_case.length(), root->location());
    stack.push(root);

    while (!stack.empty()) {
      const ASTNode* node = stack.top();
      stack.pop();

      const ParseLocationRange& range = node->location();
      ParseLocationRange prev_child_range;

      // Verify that all children are within the range of the parent and come
      // completely after their previous sibling.
      for (int i = 0; i < node->num_children(); i++) {
        const ASTNode* child = node->child(i);

        const ParseLocationRange& child_range = child->location();
        BasicValidateParseLocationRange(test_case.length(), child_range);

        // Verify that the child is contained entirely within its parent.
        EXPECT_GE(child_range.start().GetByteOffset(),
                  range.start().GetByteOffset())
            << node->DebugString() << "(child index: " << i << ")";
        EXPECT_LE(child_range.end().GetByteOffset(),
                  range.end().GetByteOffset())
            << node->DebugString() << "(child index: " << i << ")";

        if (child->IsStatement()) {
          // We don't make any guarantees about the source location relationship
          // of siblings in general, however, statements should be strictly
          // ordered. So, we verify that the child statement appears after the
          // previous sibling, with no overlap.
          if (i > 0) {
            EXPECT_GE(child_range.start().GetByteOffset(),
                      prev_child_range.end().GetByteOffset())
                << node->DebugString() << "(child index: " << i << ")";
          }
          prev_child_range = child_range;
        }

        // Add the child to the stack so that its children get checked also.
        stack.push(child);
      }
    }
  }

  class TestParseNextScriptStatementVisitor : public DefaultParseTreeVisitor {
   public:
    explicit TestParseNextScriptStatementVisitor(
        absl::string_view script_text, const ParserOptions& parser_options)
        : script_text_(script_text), parser_options_(parser_options) {}

    void defaultVisit(const ASTNode* node, void* data) override {
      node->ChildrenAccept(this, data);
    }

    void visitASTStatementList(const ASTStatementList* node,
                               void* data) override {
      if (node->statement_list().empty()) {
        return;
      }

      ParseResumeLocation resume_location =
          ParseResumeLocation::FromStringView(script_text_);
      resume_location.set_byte_position(
          node->statement_list()[0]->location().start().GetByteOffset());
      bool end_of_input = false;
      for (const ASTStatement* statement : node->statement_list()) {
        ASSERT_FALSE(end_of_input)
            << "ParseNextScriptStatement() returned end of input, but there is "
               "still another statement left:\n"
            << statement->DebugString();

        // Re-parse the current statement using ParseNextScriptStatement().
        // For the first statement in the block, the position is initialized
        // to that of the statement.  For subsequent statements, we use the
        // resume location returned by the previous call to
        // ParseNextScriptStatement(); this serves as a test that
        // ParseNextScriptStatement() is properly updating the resume location.
        int resume_position = resume_location.byte_position();
        std::unique_ptr<ParserOutput> parser_output;
        GOOGLESQL_ASSERT_OK(ParseNextScriptStatement(&resume_location, parser_options_,
                                           &parser_output, &end_of_input))
            << "ParseNextScriptStatement() failed for statement within "
               "script."
            << "\n\nParsing at byte offset "
            << resume_position << "\n\n"
            << "statement:\n"
            << ScriptSegment::FromASTNode(script_text_, statement)
                   .GetSegmentText()
            << "\n\n"
            << statement->DebugString() << "\n\nscript:\n"
            << script_text_;

        // Verify statement matches expected value
        ASSERT_NE(parser_output->statement(), nullptr);
        ASSERT_EQ(StripEndPositions(statement->DebugString()),
                  StripEndPositions(parser_output->statement()->DebugString()))
            << "Statement produces different result when re-parsed using "
               "ParseNextScriptStatement()\n"
            << "\n\nParsing at byte offset "
            << resume_position << "\n\n"
            << "statement:\n"
            << ScriptSegment::FromASTNode(script_text_, statement)
                   .GetSegmentText()
            << "\n\n"
            << statement->DebugString() << "\n\nscript:\n"
            << script_text_;

        // Perform some sanity checks on the end position, since it's stripped
        // from the debug string output used in the comparison.
        const ParseLocationRange& reparsed_stmt_range =
            parser_output->statement()->location();
        ASSERT_EQ(reparsed_stmt_range.start().GetByteOffset(),
                  statement->location().start().GetByteOffset());
        ASSERT_GE(reparsed_stmt_range.end().GetByteOffset(),
                  reparsed_stmt_range.start().GetByteOffset());

        // Due to quirks in the tokenizer logic, the lhs will be less than, not
        // equal, when comments or whitespace follows the semi-colon at the end
        // of the statement.  In other cases, it's equal.
        ASSERT_LE(reparsed_stmt_range.end().GetByteOffset(),
                  statement->location().end().GetByteOffset());
      }
    }

    std::string StripEndPositions(absl::string_view node_debug_string) {
      // Replaces end positions of all tree nodes with "??".
      //
      // This function exists as a workaround for the fact that whitespace
      // after a semi-colon is included in the semi-colon token under
      // ParseStatement()/ParseScript(), but not under
      // ParseNextStatement()/ParseNextScriptStatement(), which causes the raw
      // debug strings to not be directly comparable.
      std::string result(node_debug_string);
      RE2::GlobalReplace(&result, R"(\-\d+\])", "-??]");
      return result;
    }

    absl::string_view script_text_;
    ParserOptions parser_options_;
  };

  absl::StatusOr<ParserOptions> GetParserOptions() {
    // Reset the LanguageOptions.
    language_options_ = std::make_unique<LanguageOptions>();

    // Parse the language features first because other checks below may depend
    // on the features that are enabled for the test case.
    GOOGLESQL_ASSIGN_OR_RETURN(LanguageOptions::LanguageFeatureSet features,
                     GetRequiredLanguageFeatures(test_case_options_));
    language_options_->SetEnabledLanguageFeatures(features);

    if (test_case_options_.GetBool(kQualifyReserved)) {
      GOOGLESQL_EXPECT_OK(language_options_->EnableReservableKeyword("QUALIFY"));
    }
    if (test_case_options_.GetBool(kReserveMatchRecognize)) {
      GOOGLESQL_EXPECT_OK(language_options_->EnableReservableKeyword("MATCH_RECOGNIZE"));
    }
    if (test_case_options_.GetBool(kReserveGraphTable)) {
      GOOGLESQL_EXPECT_OK(language_options_->EnableReservableKeyword("GRAPH_TABLE"));
    }

    std::string entity_types_config =
        test_case_options_.GetString(kSupportedGenericEntityTypes);
    std::vector<std::string> entity_types =
        absl::StrSplit(entity_types_config, ',');
    language_options_->SetSupportedGenericEntityTypes(entity_types);

    std::string sub_entity_types_config =
        test_case_options_.GetString(kSupportedGenericSubEntityTypes);
    std::vector<std::string> sub_entity_types =
        absl::StrSplit(sub_entity_types_config, ',');
    language_options_->SetSupportedGenericSubEntityTypes(sub_entity_types);

    absl::string_view expansion_mode_str =
        test_case_options_.GetString(kMacroExpansionMode);
    if (expansion_mode_str.empty()) {
      macro_expansion_mode_ = MacroExpansionMode::kNone;
    } else if (expansion_mode_str == "none") {
      macro_expansion_mode_ = MacroExpansionMode::kNone;
    } else if (expansion_mode_str == "lenient") {
      macro_expansion_mode_ = MacroExpansionMode::kLenient;
    } else if (expansion_mode_str == "strict") {
      macro_expansion_mode_ = MacroExpansionMode::kStrict;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid macro expansion mode: ",
                       test_case_options_.GetString(kMacroExpansionMode)));
    }
    return ParserOptions(/*id_string_pool=*/nullptr, /*arena=*/nullptr,
                         // ParserOptions wants a copy of LanguageOptions.
                         *language_options_, macro_expansion_mode_);
  }

  void CheckExtractedStatementProperties(
      const ASTNode* node, absl::string_view test_case,
      const parser::ASTStatementProperties& extracted_statement_properties,
      std::vector<std::string>* test_outputs) {
    const ASTStatement* statement = node->GetAsOrNull<ASTStatement>();
    if (statement == nullptr) {
      return;
    }

    // Unwrap a HintedStatement if we got one.
    if (statement->node_kind() == AST_HINTED_STATEMENT) {
      const ASTHintedStatement* ast_hinted_statement
          = statement->GetAsOrDie<ASTHintedStatement>();
      statement = ast_hinted_statement->statement();

      absl::flat_hash_map<std::string, std::string> hints_from_hinted_statement;
      GOOGLESQL_EXPECT_OK(ProcessStatementLevelHintsToMap(ast_hinted_statement->hint(),
                                                test_case,
                                                hints_from_hinted_statement));
      EXPECT_THAT(hints_from_hinted_statement,
                  testing::ContainerEq(
                      extracted_statement_properties.statement_level_hints))
          << "\nTest SQL: " << test_case << "\nHinted statement:\n"
          << ast_hinted_statement->DebugString();
    }

    // The NodeKind on the AST should match that in the extracted properties.
    ASTNodeKind found_statement_kind = statement->node_kind();
    if (found_statement_kind == AST_STATEMENT_WITH_PIPE_OPERATORS) {
      // ASTStatementWithPipeOperators isn't detected in GetNextStatementKind
      // because it's a wrapper holding pipe operator suffixes.
      // Unwrap it to get the underlying statement kind which is identified
      // from the statement prefix.
      found_statement_kind =
          statement->GetAsOrDie<ASTStatementWithPipeOperators>()
              ->statement()
              ->node_kind();
    }
    EXPECT_EQ(found_statement_kind, extracted_statement_properties.node_kind);
    if (found_statement_kind != extracted_statement_properties.node_kind) {
      test_outputs->push_back(absl::StrCat(
          "FAILED guessing statement kind. Extracted kind ",
          ASTNode::NodeKindToString(extracted_statement_properties.node_kind),
          ", got ", ASTNode::NodeKindToString(found_statement_kind)));
    }

    // The CREATE scope on the AST should match that in the extracted
    // properties.
    const ASTCreateStatement* create_statement =
        statement->GetAsOrNull<ASTCreateStatement>();
    if (create_statement != nullptr) {
      // Check the extracted Scope.
      EXPECT_EQ(extracted_statement_properties.create_scope,
                create_statement->scope());
      if (extracted_statement_properties.create_scope
          != create_statement->scope()) {
        test_outputs->push_back(absl::StrCat(
            "FAILED extracting create scope. Extracted scope: ",
            extracted_statement_properties.create_scope,
            ", actual scope: ", create_statement->scope()));
      }
    }

    // Whether or not the AST is CREATE TABLE AS SELECT should match that in
    // the extracted properties.
    const ASTCreateTableStatement* create_table_statement =
        statement->GetAsOrNull<ASTCreateTableStatement>();
    if (create_table_statement != nullptr) {
      EXPECT_EQ(extracted_statement_properties.is_create_table_as_select,
                create_table_statement->query() != nullptr)
          << Unparse(create_table_statement);
      if (extracted_statement_properties.is_create_table_as_select
          != (create_table_statement->query() != nullptr)) {
        test_outputs->push_back(absl::StrCat(
            "FAILED extracting is CTAS . Extracted: ",
            extracted_statement_properties.is_create_table_as_select,
            ", actual: ", create_table_statement->query() != nullptr));
      }
    }
  }

  // Returns true if `token1` is adjacent to `token2` and `token1` precedes
  // `token2`.
  static bool IsAdjacentPrecedingToken(const ParseToken& token1,
                                       const ParseToken& token2) {
    const ParseLocationRange& location1 = token1.GetLocationRange();
    const ParseLocationRange& location2 = token2.GetLocationRange();
    // Two tokens must be from the same file to be adjacent.
    if (location1.start().filename() != location2.start().filename()) {
      return false;
    }
    return location1.end().GetByteOffset() == location2.start().GetByteOffset();
  }

  void HandleOneParseTree(
      absl::string_view test_case, absl::string_view mode,
      const absl::Status& status, const ASTNode* parsed_root,
      const parser::ASTStatementProperties& extracted_statement_properties,
      bool is_single, std::vector<std::string>* test_outputs) {
    if (status.ok()) {
      ABSL_CHECK(parsed_root != nullptr);

      // Check that the statement we parsed matches the statement kind we
      // extracted with ParseStatementKind.
      if (mode == "statement") {
        CheckExtractedStatementProperties(parsed_root, test_case,
                                          extracted_statement_properties,
                                          test_outputs);
      } else if (mode == "subpipeline") {
        EXPECT_EQ(AST_SUBPIPELINE, extracted_statement_properties.node_kind);
        EXPECT_EQ(parsed_root->node_kind(),
                  extracted_statement_properties.node_kind);
        EXPECT_FALSE(extracted_statement_properties.is_create_table_as_select);
        EXPECT_TRUE(
            extracted_statement_properties.statement_level_hints.empty());
      }

      const std::string root_debug_string =
          test_case_options_.GetBool(kShowParseLocationText)
              ? parsed_root->DebugString(test_case)
              : parsed_root->DebugString();
      test_outputs->push_back(root_debug_string);
      GOOGLESQL_VLOG(1) << root_debug_string;
    } else {
      EXPECT_TRUE(HasErrorLocation(status))
          << "All parser errors should have an ErrorLocation: " << status;

      // Show an multi-line error with a location string and a caret.
      absl::Status out_status;

      ErrorLocation error_location;
      if (googlesql::GetErrorLocation(status, &error_location) &&
          error_location.error_source_size() != 0) {
        // MaybeUpdateErrorFromPayload() doesn't handle the caret strings
        // for sources, so need to parse the input again, passing in the
        // correct mode.  Only ParseAndValidateScript() supports this, but
        // only ParseAndValidateScript() has the capability of returning
        // errors with sources to begin with.
        EXPECT_EQ(mode, "script") << "Error source without script mode";
        GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions parser_options, GetParserOptions());
        out_status = ParseAndValidateScript(
                         test_case, parser_options,
                         {.mode = ERROR_MESSAGE_MULTI_LINE_WITH_CARET}, {})
                         .status();

      } else {
        out_status = MaybeUpdateErrorFromPayload(
            ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
            /*keep_error_location_payload=*/false, test_case, status);
      }
      test_outputs->push_back(absl::StrCat("ERROR: ", FormatError(out_status)));

      EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument)
          << "All parser errors should return INVALID_ARGUMENT";
    }

    if (parsed_root != nullptr) {
      VerifyParseLocationRanges(test_case, parsed_root);
    }

    // Also verify that round-tripping through Unparser works.
    if (status.ok() && is_single && parsed_root != nullptr &&
        test_case_options_.GetBool(kTestUnparse)) {
      std::string unparse_output;
      TestUnparsing(test_case, mode, parsed_root, &unparse_output);
      test_outputs->push_back(unparse_output);
    }

    if (status.ok() && is_single && mode == "script") {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(ParserOptions parser_options, GetParserOptions());
      TestParseNextScriptStatementVisitor visitor(test_case, parser_options);
      parsed_root->Accept(&visitor, nullptr);
    }
    // Also verify round-tripping through GetParseTokens.
    // This only works for single statements.
    if (is_single && test_case_options_.GetBool(kTestGetParseTokens)) {
      std::vector<ParseToken> parse_tokens;
      ParseResumeLocation location =
          ParseResumeLocation::FromStringView(test_case);
      const absl::Status token_status = GetParseTokens(
          ParseTokenOptions{.language_options = *language_options_}, &location,
          &parse_tokens);
      if (!token_status.ok()) {
        EXPECT_FALSE(status.ok())
            << "Parse succeeded, but GetParseTokens failed with: "
            << token_status;
      } else {
        std::string rebuilt;
        std::optional<ParseToken> last_token;
        for (const ParseToken& parse_token : parse_tokens) {
          // If two tokens are originally adjacent, do not insert a whitespace
          // in between. For example, two adjacent ">"s can represent a single
          // right shift token and should not be printed as "> >".
          if (last_token.has_value() &&
              !IsAdjacentPrecedingToken(*last_token, parse_token)) {
            rebuilt += " ";
          }
          rebuilt += parse_token.GetSQL();
          last_token = parse_token;
        }
        GOOGLESQL_VLOG(1) << "Rebuilt from tokens:\n" << rebuilt;

        const ASTNode* rebuilt_root;
        std::unique_ptr<ParserOutput> parser_output;
        const absl::Status rebuilt_parse_status =
            ParseWithMode(rebuilt, mode, &rebuilt_root, &parser_output);

        // We want to compare the error message, ignoring the error position.
        // There are also some small differences in error strings for things
        // like literal formats (e.g. 0.1 vs .1).  Comparing via edit distance
        // is a hack to ignore small diffs and try to make sure the error
        // message is "close".
        EXPECT_TRUE(IsSimilarError(status, rebuilt_parse_status))
            << "\nOrig error: " << status
            << "\nRebuilt error: " << rebuilt_parse_status
            << "\nRebuilt query: " << rebuilt;

        if (status.ok() && rebuilt_parse_status.ok()) {
          EXPECT_EQ(RedactedDebugString(parsed_root),
                    RedactedDebugString(rebuilt_root));
        }
      }
    }
  }

  void TestDeidentifiedConsistency(absl::string_view test_case,
                                   absl::string_view unparsed) {
    // The deidentified SQL should be the same independent of how many times it
    // was unparsed or deidentified.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parser_options1, GetParserOptions());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::string deidentified_unparsed,
        parser::DeidentifySQLIdentifiersAndLiterals(unparsed, parser_options1));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parser_options2, GetParserOptions());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::string deidentified_deidentified,
                         parser::DeidentifySQLIdentifiersAndLiterals(
                             deidentified_unparsed, parser_options2));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto parser_options3, GetParserOptions());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::string test_case_deidentified,
                         parser::DeidentifySQLIdentifiersAndLiterals(
                             test_case, parser_options3));
    EXPECT_EQ(deidentified_unparsed, test_case_deidentified);
    EXPECT_EQ(deidentified_deidentified, test_case_deidentified);
  }

  void TestUnparsing(absl::string_view test_case, absl::string_view mode,
                     const ASTNode* parsed_root, std::string* output) {
    const std::string unparsed = Unparse(parsed_root);
    if (absl::StrContains(unparsed, "<Complex nested expression truncated>")) {
      // The query had deep nesting which caused unparsing to fail,
      // so log it and end the test since round-tripping will not work.
      GOOGLESQL_VLOG(1) << unparsed;
      *output = "Unparse had truncated nested expression";
      return;
    }

    *output = unparsed;
    const ASTNode* root2;
    std::unique_ptr<ParserOutput> parser_output;
    GOOGLESQL_EXPECT_OK(ParseWithMode(unparsed, mode, &root2, &parser_output))
        << "Parse error while parsing the unparsed version:\n"
        << unparsed << "\nof sql:\n"
        << test_case;

    // Check that the original and unparsed have the same tree except
    // for location.
    if (parsed_root != nullptr && root2 != nullptr) {
      std::string orig_string = parsed_root->DebugString();
      std::string from_unparse_string = root2->DebugString();
      static const LazyRE2 clean_up_location = {R"(\[[0-9]+-[0-9]+\])"};
      RE2::GlobalReplace(&orig_string, *clean_up_location, "(...)");
      RE2::GlobalReplace(&from_unparse_string, *clean_up_location, "(...)");
      EXPECT_TRUE(orig_string == from_unparse_string)
          << "Different trees:\n"
          << "\nfor unparsed vs. original sql.\nUnparsed sql:\n"
          << unparsed << "\nOriginal sql:\n"
          << test_case << "\nTree for original sql :\n"
          << orig_string << "\nTree for unparsed sql:\n"
          << from_unparse_string;
    }

    if (mode == "statement") {
      TestDeidentifiedConsistency(test_case, unparsed);
    }
  }

  absl::Status ParseWithMode(absl::string_view test_case,
                             absl::string_view mode, const ASTNode** root,
                             std::unique_ptr<ParserOutput>* parser_output) {
    GOOGLESQL_ASSIGN_OR_RETURN(ParserOptions parser_options, GetParserOptions());
    *root = nullptr;
    if (mode == "statement") {
      GOOGLESQL_RETURN_IF_ERROR(ParseStatement(test_case, parser_options, parser_output));
    } else if (mode == "script") {
      GOOGLESQL_RETURN_IF_ERROR(
          ParseScript(test_case, parser_options,
                      {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                       .attach_error_location_payload =
                           (ERROR_MESSAGE_WITH_PAYLOAD ==
                            ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD),
                       .stability = GetDefaultErrorMessageStability()},
                      parser_output));
    } else if (mode == "expression") {
      GOOGLESQL_ASSIGN_OR_RETURN(ParserOptions expr_parser_options, GetParserOptions());
      GOOGLESQL_RETURN_IF_ERROR(
          ParseExpression(test_case, expr_parser_options, parser_output));
    } else if (mode == "type") {
      GOOGLESQL_ASSIGN_OR_RETURN(ParserOptions type_parser_options, GetParserOptions());
      GOOGLESQL_RETURN_IF_ERROR(ParseType(test_case, type_parser_options, parser_output));
    } else {
      return ::googlesql_base::UnknownErrorBuilder() << "Invalid parse mode: " << mode;
    }
    *root = (*parser_output)->node();
    return absl::OkStatus();
  }

  file_based_test_driver::TestCaseOptions test_case_options_;
  std::unique_ptr<LanguageOptions> language_options_;
  MacroExpansionMode macro_expansion_mode_;
};

TEST_F(RunParserTest, ParseQueries) {
  EXPECT_TRUE(file_based_test_driver::RunTestCasesFromFiles(
      absl::GetFlag(FLAGS_test_file),
      absl::bind_front(&RunParserTest::RunTest, this)));
}

}  // namespace googlesql
