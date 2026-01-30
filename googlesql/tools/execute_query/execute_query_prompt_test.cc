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

#include "googlesql/tools/execute_query/execute_query_prompt.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/status_payload_matchers.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/tools/execute_query/execute_query.pb.h"
#include "googlesql/tools/execute_query/execute_query_prompt_testutils.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

using testing::AllOf;
using testing::Eq;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;
using absl_testing::IsOk;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;

namespace googlesql {

using execute_query::ParserErrorContext;
using googlesql::testing::StatusHasPayload;
using testing::EqualsProto;

namespace {

using ReadResultType = absl::StatusOr<std::optional<std::string>>;

struct CompletionReq {
  size_t cursor_position;

  ::testing::Matcher<absl::Status> status_matcher = IsOk();
  size_t want_prefix_start = 0;
  ::testing::Matcher<std::vector<std::string>> matcher = IsEmpty();

  void Check(ExecuteQueryStatementPrompt& prompt, std::string body) const;
};

void CompletionReq::Check(ExecuteQueryStatementPrompt& prompt,
                          const std::string body) const {
  const absl::StatusOr<ExecuteQueryCompletionResult> res =
      prompt.Autocomplete(PrepareCompletionReq(body, cursor_position));

  EXPECT_THAT(res.status(), status_matcher);

  if (res.ok()) {
    EXPECT_THAT(res.value(),
                CompletionResponseMatcher(Eq(want_prefix_start), matcher));
  }
}

struct StmtPromptInput final {
  ReadResultType ret;
  bool want_continuation = false;
  std::vector<CompletionReq> completions;
};

// Run ExecuteQueryStatementPrompt returning the given inputs and expecting the
// given return values or parser errors. All inputs, return values and parser
// errors must be consumed.
void TestStmtPrompt(absl::Span<const StmtPromptInput> inputs,
                    absl::Span<const ::testing::Matcher<ReadResultType>> want,
                    const ExecuteQueryConfig* config = nullptr) {
  std::unique_ptr<ExecuteQueryStatementPrompt> prompt;

  auto cur_input = inputs.cbegin();
  auto readfunc = [&prompt, inputs,
                   &cur_input](bool continuation) -> ReadResultType {
    EXPECT_NE(cur_input, inputs.cend()) << "Can't read beyond input";
    EXPECT_EQ(continuation, cur_input->want_continuation);

    EXPECT_THAT(cur_input->completions, IsEmpty());
    CompletionReq{.cursor_position = 0}.Check(*prompt, "");

    return (cur_input++)->ret;
  };

  ExecuteQueryConfig default_config;
  if (config == nullptr) {
    config = &default_config;
  }
  prompt = std::make_unique<ExecuteQueryStatementPrompt>(*config, readfunc);

  for (const auto& matcher : want) {
    EXPECT_THAT(prompt->Read(), matcher);
  }

  EXPECT_EQ(cur_input, inputs.cend()) << "Not all inputs have been consumed";
}

}  // namespace

TEST(ExecuteQueryStatementPrompt, Empty) { TestStmtPrompt({}, {}); }

TEST(ExecuteQueryStatementPrompt, EmptyInput) {
  TestStmtPrompt(
      {
          {.ret = ""},
          {.ret = ""},
          {.ret = ""},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, SingleLine) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1;"},
          {.ret = "My query 2;"},
          {.ret = "SELECT 3;"},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1;"),
          IsOkAndHolds("My query 2;"),
          IsOkAndHolds("SELECT 3;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, MultipleSelects) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1;"},
          {.ret = "SELECT 2;"},
          {.ret = "SELECT ("},
          {.ret = "foo,\n", .want_continuation = true},
          {.ret = "bar);", .want_continuation = true},
          {.ret = "SELECT 4;"},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1;"),
          IsOkAndHolds("SELECT 2;"),
          IsOkAndHolds("SELECT (foo,\nbar);"),
          IsOkAndHolds("SELECT 4;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, FaultyMixedWithValid) {
  TestStmtPrompt(
      {
          {.ret = ""},
          {.ret = "SELECT 1, (2 +\n"},
          {.ret = "  3);", .want_continuation = true},

          // Unclosed string literal followed by legal statement (the latter is
          // dropped due to the error)
          {.ret = "\";\nSELECT 123; "},

          {.ret = "SELECT\nsomething;"},

          {.ret = "DROP TABLE MyTable;"},

          // Just some whitespace
          {.ret = "\n"},
          {.ret = "\t"},
          {.ret = ""},

          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1, (2 +\n  3);"),
          AllOf(StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(": Unclosed string literal")),
                StatusHasPayload<ParserErrorContext>(EqualsProto(R"pb(
                  text: "\";\nSELECT 123;"
                )pb"))),
          IsOkAndHolds("SELECT\nsomething;"),
          IsOkAndHolds("DROP TABLE MyTable;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, TripleQuoted) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 99, \"\"\"hello\n"},
          {.ret = "\n", .want_continuation = true},
          {.ret = "line\n", .want_continuation = true},
          {.ret = "", .want_continuation = true},
          {.ret = "\n", .want_continuation = true},
          {.ret = "world\"\"\", 101;", .want_continuation = true},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 99, \"\"\"hello\n\nline\n\nworld\"\"\", 101;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, TerminatedByEOF) {
  TestStmtPrompt(
      {
          {.ret = ""},
          {.ret = "SELECT 1, (2 +"},
          // Statement not terminated with semicolon
          {.ret = "  3)", .want_continuation = true},
          {.ret = std::nullopt, .want_continuation = true},
      },
      {
          IsOkAndHolds("SELECT 1, (2 +  3)"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, ReadAfterEOF) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1;"},
          {.ret = ""},
          {.ret = ""},
          {.ret = ""},
          {.ret = "SELECT 2;"},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1;"),
          IsOkAndHolds("SELECT 2;"),
          IsOkAndHolds(std::nullopt),
          IsOkAndHolds(std::nullopt),
          IsOkAndHolds(std::nullopt),
          IsOkAndHolds(std::nullopt),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, MultipleOnSingleLine) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1; SELECT 2;\tSELECT\n"},
          {.ret = "  3;", .want_continuation = true},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1;"),
          IsOkAndHolds("SELECT 2;"),
          IsOkAndHolds("SELECT\n  3;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, SemicolonOnly) {
  TestStmtPrompt(
      {
          {.ret = ";"},
          {.ret = ";;;"},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds(";"),
          IsOkAndHolds(";"),
          IsOkAndHolds(";"),
          IsOkAndHolds(";"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, ReadError) {
  TestStmtPrompt(
      {
          {.ret = absl::NotFoundError("test")},
          {.ret = "SELECT 500;"},
          {.ret = absl::CancelledError("again")},
      },
      {
          StatusIs(absl::StatusCode::kNotFound, "test"),
          IsOkAndHolds("SELECT 500;"),
          StatusIs(absl::StatusCode::kCancelled, "again"),
      });
}

TEST(ExecuteQueryStatementPrompt, SplitKeyword) {
  TestStmtPrompt(
      {
          {.ret = "SEL"},
          {.ret = "ECT 100;", .want_continuation = true},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 100;"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, SplitString) {
  TestStmtPrompt(
      {
          {.ret = "\tSELECT \"val"},
          {.ret = "ue"},
          {.ret = "\" ;", .want_continuation = true},
          {.ret = std::nullopt},
      },
      {
          AllOf(StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(": Unclosed string literal")),
                StatusHasPayload<ParserErrorContext>(EqualsProto(R"pb(
                  text: "SELECT \"val"
                )pb"))),
          AllOf(StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(": Unclosed string literal")),
                StatusHasPayload<ParserErrorContext>(EqualsProto(R"pb(
                  text: "ue\" ;"
                )pb"))),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, StatementWithoutSemicolonAtEOF) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 100; SELECT 200"},
          {.ret = std::nullopt, .want_continuation = true},
      },
      {
          IsOkAndHolds("SELECT 100;"),
          IsOkAndHolds("SELECT 200"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, UnfinishedStatement) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1; SELECT ("},
          {.ret = std::nullopt, .want_continuation = true},
      },
      {
          IsOkAndHolds("SELECT 1;"),
          IsOkAndHolds("SELECT ("),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, ContinuedParenthesis) {
  TestStmtPrompt(
      {
          {.ret = "(\n"},
          {.ret = ");\n", .want_continuation = true},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("(\n);"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, UnfinishedParenthesis) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 10; \n"},
          {.ret = "(\n"},
          {.ret = std::nullopt, .want_continuation = true},
      },
      {
          IsOkAndHolds("SELECT 10;"),
          IsOkAndHolds("("),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, SemicolonInParenthesis) {
  TestStmtPrompt(
      {
          {.ret = "SELECT 1, (;); \n"},
          {.ret = std::nullopt},
      },
      {
          IsOkAndHolds("SELECT 1, (;"),
          IsOkAndHolds(");"),
          IsOkAndHolds(std::nullopt),
      });
}

TEST(ExecuteQueryStatementPrompt, RecoverAfterUnclosedString) {
  TestStmtPrompt(
      {
          {.ret = "\";"},
          {.ret = "SELECT 123;"},
          {.ret = std::nullopt},
      },
      {
          AllOf(StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(": Unclosed string literal")),
                StatusHasPayload<ParserErrorContext>(EqualsProto(R"pb(
                  text: "\";"
                )pb"))),
          IsOkAndHolds("SELECT 123;"),
          IsOkAndHolds(std::nullopt),
      });
}
TEST(ExecuteQueryStatementPrompt, LargeInput) {
  const std::string large(32, 'A');
  unsigned int count = 0;

  ExecuteQueryConfig config;
  ExecuteQueryStatementPrompt prompt{config,
                                     [&large, &count](bool continuation) {
                                       EXPECT_EQ(continuation, ++count > 1);
                                       return large;
                                     }};

  EXPECT_EQ(prompt.max_length_, ExecuteQueryStatementPrompt::kMaxLength);

  // First prime number larger than 1 KiB to ensure that division by the length
  // of the "large" string doesn't result in an integral value.
  prompt.max_length_ = 1031;

  for (int i = 0; i < 10; ++i) {
    count = 0;
    EXPECT_THAT(prompt.Read(),
                StatusIs(absl::StatusCode::kResourceExhausted,
                         "Reached maximum statement length of 1 KiB"));
    EXPECT_EQ(count, 1 + (prompt.max_length_ / large.size()));
  }
}

}  // namespace googlesql
