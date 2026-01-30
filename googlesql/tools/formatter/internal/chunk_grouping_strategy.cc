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

#include "googlesql/tools/formatter/internal/chunk_grouping_strategy.h"

#include <iterator>
#include <vector>

#include "googlesql/tools/formatter/internal/chunk.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

namespace {

void AddBlockForTopLevelClause(ChunkBlock* const chunk_block,
                               Chunk* top_level_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf() || (*i)->Chunk()->Empty()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      // Found an opening parenthesis -> indent inside parentheses.
      if ((chunk->OpensParenOrBracketBlock() &&
           !chunk->HasMatchingClosingChunk()) ||
          // Sometimes, user omit parentheses after DEFINE MACRO, but we should
          // indent nevertheless.
          chunk->IsStartOfDefineMacroStatement()) {
        (*i)->AddIndentedChunk(top_level_chunk);
        return;
      } else if (chunk->IsTopLevelClauseChunk()) {
        // Found another top-level clause -> group together.
        t->AddChildChunk(top_level_chunk);
        return;
      } else if (chunk->FirstKeyword() == "|>") {
        ChunkBlock* new_block = (*i)->AddIndentedChunk(top_level_chunk);
        // Pipe token |> introduces indent of 3 for all top level clauses.
        new_block->SetMinOffset(3);
        return;
      }
    }
    t = t->Parent();
  }

  t->AddChildChunk(top_level_chunk);
}

void AddBlockForPipeOperator(ChunkBlock* const chunk_block, Chunk* pipe_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf() || (*i)->Chunk()->Empty()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      // Found an opening parenthesis -> indent inside parentheses.
      if ((chunk->OpensParenBlock() && !chunk->HasMatchingClosingChunk()) ||
          // Sometimes, users omit parentheses after DEFINE MACRO, but we should
          // indent nevertheless.
          chunk->IsStartOfDefineMacroStatement()) {
        (*i)->AddIndentedChunk(pipe_chunk);
        return;
      } else if (chunk->FirstKeyword() == "|>") {
        // Found another |> -> group together.
        t->AddChildChunk(pipe_chunk);
        return;
      }
    }
    t = t->Parent();
  }

  t->AddChildChunk(pipe_chunk);
}

void AddBlockForSelectOrWith(Chunk* const previous_chunk,
                             Chunk* select_or_with) {
  // Special handling for CREATE ... AS SELECT statement.
  // We want SELECT to be on the same level with AS, but not to have common
  // parent. This allows format like:
  //   CREATE TABLE Foo
  //   AS
  //   SELECT a FROM Bar;
  // If AS and SELECT had common parent, then new line before AS forces new
  // lines before all top level clauses of the following SELECT.
  if (previous_chunk->FirstKeyword() == "AS" &&
      previous_chunk->IsTopLevelClauseChunk() &&
      !previous_chunk->OpensParenBlock()) {
    previous_chunk->ChunkBlock()->AddSameLevelCousinChunk(select_or_with);
    return;
  } else if (previous_chunk->FirstKeyword() == "EXPLAIN") {
    // EXPLAIN indents the query that follows.
    previous_chunk->ChunkBlock()->AddIndentedChunk(select_or_with);
    return;
  }

  return AddBlockForTopLevelClause(previous_chunk->ChunkBlock(),
                                   select_or_with);
}

void AddBlockForClosingBracket(Chunk* const previous_chunk,
                               Chunk* closing_bracket_chunk) {
  // This function handles multiple types of brackets: "()", "[]" and "{}".
  const absl::string_view opening_bracket =
      CorrespondingOpenBracket(closing_bracket_chunk->FirstKeyword());
  Chunk* chunk = previous_chunk;
  while (chunk != nullptr) {
    if (chunk->LastKeyword() == opening_bracket &&
        !chunk->HasMatchingClosingChunk()) {
      // Found an opening bracket which doesn't have a closing one yet.
      ChunkBlock* parent =
          chunk->JumpToTopOpeningBracketIfAny()->ChunkBlock()->Parent();
      parent->AddChildChunk(closing_bracket_chunk);
      chunk->SetMatchingClosingChunk(closing_bracket_chunk);
      closing_bracket_chunk->SetMatchingOpeningChunk(chunk);
      return;
    }
    // If the chunk is a closing bracket - jump to the opening one.
    chunk = chunk->JumpToTopOpeningBracketIfAny();
    chunk = chunk->PreviousChunk();
  }

  // Did not find matching bracket.
  previous_chunk->ChunkBlock()->AddSameLevelChunk(closing_bracket_chunk);
}

void AddBlockForSetOperator(ChunkBlock* const chunk_block,
                            Chunk* set_operator_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if ((*i)->IsLeaf()) {
        Chunk* chunk = (*i)->Chunk();
        if (chunk->OpensParenBlock() && !chunk->HasMatchingClosingChunk()) {
          // Set operator is inside parentheses. Indent relative to parentheses.
          (*i)->AddIndentedChunk(set_operator_chunk);
          return;
        }
        // Chain of set operators should be on the same level.
        if (chunk->IsSetOperator() || chunk->IsStartOfNewQuery()) {
          t->AddChildChunk(set_operator_chunk);
          return;
        }
        if (chunk->LastKeyword() == "AS") {
          (*i)->AddIndentedChunk(set_operator_chunk);
          return;
        }
      }
    }
    t = t->Parent();
  }

  t->AddChildChunk(set_operator_chunk);
}

bool ChunkStartsUnfinishedTypeDeclarationExpression(const Chunk& chunk) {
  if (!chunk.IsTypeDeclarationStart() || !chunk.HasMatchingClosingChunk()) {
    return false;
  }

  const Chunk* type_declaration_end = chunk.MatchingClosingChunk();
  if (type_declaration_end->ChunkBlock() == nullptr) {
    // We are inside type declaration, e.g. "STRUCT<STRING, {here}>".
    return true;
  }

  // We are inside array or struct literal constructor, e.g.:
  // "STRUCT<STRING, INT64>('s', {here})"
  return type_declaration_end->OpensParenOrBracketBlock() &&
         !type_declaration_end->HasMatchingClosingChunk();
}

void AddBlockFollowingAComma(Chunk* const previous_chunk, Chunk* comma_chunk) {
  ChunkBlock* t = previous_chunk->ChunkBlock();
  for (; !t->IsTopLevel(); t = t->Parent()) {
    if (t->children().size() < 2) {
      // We look for a block with at least two children: a comma-separated
      // expression, and the previous block that starts it. For instance:
      // parent block                         <-- `t`
      //  +-- SELECT                          <-- `previous_leaf`
      //  +-- block for comma-separated list  <-- `last_child`
      //       +-- a,
      continue;
    }
    ChunkBlock::Children::reverse_iterator last_child = t->children().rbegin();
    // Find previous leaf child if any.
    auto previous_leaf = last_child + 1;
    while (previous_leaf != t->children().rend() &&
           !(*previous_leaf)->IsLeaf()) {
      previous_leaf++;
    }
    if (previous_leaf == t->children().rend() || !(*previous_leaf)->IsLeaf()) {
      continue;
    }
    Chunk* chunk = (*previous_leaf)->Chunk();
    if (chunk->IsTopLevelClauseChunk() ||
        (chunk->OpensParenOrBracketBlock() &&
         !chunk->HasMatchingClosingChunk()) ||
        ChunkStartsUnfinishedTypeDeclarationExpression(*chunk) ||
        chunk->IsCreateOrExportIndentedClauseChunk() ||
        chunk->FirstToken().Is(Token::Type::DDL_KEYWORD)) {
      (*last_child)->AddChildChunk(comma_chunk);
      return;
    }
  }

  t->AddChildChunk(comma_chunk);
}

void AddBlockForAsKeyword(ChunkBlock* const chunk_block, Chunk* as_chunk) {
  ChunkBlock* t = chunk_block;

  bool stop = false;
  ChunkBlock* expression_block = nullptr;
  while (!t->IsTopLevel() && !stop) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->Empty()) {
        continue;
      }
      if (chunk->IsStartOfCreateStatement() ||
          chunk->IsStartOfExportDataStatement() ||
          chunk->IsFunctionSignatureModifier() ||
          (chunk->IsTopLevelClauseChunk() &&
           // Legacy MATERIALIZE statement.
           (chunk->FirstKeyword() == "MATERIALIZE" ||
            // Macro call that is top level clause, e.g.: $EXPORT_DATA(..) AS
            chunk->FirstToken().IsMacroCall() ||
            // Legacy ASSERT statement.
            (chunk->FirstKeyword() == "ASSERT" &&
             !chunk->OpensParenBlock())))) {
        // Formatting for "CREATE ... AS ..."
        (*i)->AddSameLevelChunk(as_chunk);
        // "AS" token acts here as a top level keyword.
        as_chunk->Tokens().WithoutComments()[0]->SetType(
            Token::Type::TOP_LEVEL_KEYWORD);
        return;
      }
      if (chunk->IsTopLevelWith() || chunk->LastKeyword() == "END" ||
          chunk->LastKeyword() == "WINDOW") {
        // Formatting for "WITH Table1 AS ..." & "CASE ... END"
        chunk_block->AddSameLevelChunk(as_chunk);
        return;
      }
      if (chunk->OpensParenOrBracketBlock() &&
          !chunk->HasMatchingClosingChunk()) {
        // Formatting for "CAST(foo AS STRING)": AS on the same level with foo.
        if (chunk->LastKeywordsAre("CAST", "(") ||
            chunk->LastKeywordsAre("SAFE_CAST", "(")) {
          // Broken query, e.g. "CAST( AS STRING".
          if (i == t->children().rbegin()) {
            (*i)->AddIndentedChunk(as_chunk);
            return;
          }
          // *(i - 1) is a block for expression inside parentheses.
          (*(i - 1))->AddChildChunk(as_chunk);
          as_chunk->Tokens().WithoutComments().front()->SetType(
              Token::Type::CAST_AS);
          return;
        }
        // AS is within parentheses, stop searching for the special cases.
        if (i != t->children().rbegin()) {
          expression_block = *(i - 1);
        }
        stop = true;
        break;
      }
      if (chunk->FirstKeyword() == "ASSERT") {
        // ASSERT (..) AS '..'
        (*i)->AddIndentedChunk(as_chunk);
        return;
      }
      if (chunk->IsTopLevelClauseChunk() &&
          !chunk->OpensParenOrBracketBlock() &&
          !chunk->IsCreateOrExportIndentedClauseChunk()) {
        // Reached top level clause, stop searching for the special cases.
        if (i != t->children().rbegin()) {
          expression_block = *(i - 1);
        }
        stop = true;
        break;
      }
    }
    t = t->Parent();
  }

  // Default behavior if we didn't find a parent expression.
  if (expression_block == nullptr) {
    chunk_block->AddIndentedChunk(as_chunk);
    return;
  }

  // Found expression is empty.
  if (expression_block->FirstChunkUnder() == nullptr) {
    expression_block->AddChildChunk(as_chunk);
    return;
  }

  // Indent by 2 from the expression start.
  Chunk* expression_start = expression_block->FirstChunkUnder();
  expression_start->ChunkBlock()->AddIndentedChunk(as_chunk);
}

void AddBlockForWhenElseEndKeywords(ChunkBlock* const chunk_block,
                                    Chunk* new_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf() || (*i)->Chunk()->Empty()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->LastKeyword() == "CASE") {
        if (i == t->children().rbegin() || (*(i - 1))->IsLeaf()) {
          (*i)->AddIndentedChunk(new_chunk);
        } else {
          (*(i - 1))->AddChildChunk(new_chunk);
        }
        return;
      } else if (chunk->FirstKeyword() == "WHEN" ||
                 chunk->FirstKeyword() == "ELSE") {
        (*i)->AddSameLevelChunk(new_chunk);
        return;
      } else if (chunk->FirstKeyword() == "THEN") {
        // Move one level up - WHEN should be there.
        break;
      } else if (chunk->FirstKeyword() == "END") {
        // Found END from a nested CASE clause. Jump 2 levels up to skip it.
        if (!t->Parent()->IsTopLevel()) {
          t = t->Parent();
        }
        break;
      }
    }
    t = t->Parent();
  }

  // This will only trigger if the query is not syntax correct, so it's the best
  // we can do.
  chunk_block->AddIndentedChunk(new_chunk);
}

void AddBlockForThenKeyword(ChunkBlock* const chunk_block, Chunk* new_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf() || (*i)->Chunk()->Empty()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->FirstKeyword() == "WHEN") {
        if (i == t->children().rbegin() || (*(i - 1))->IsLeaf()) {
          (*i)->AddIndentedChunk(new_chunk);
        } else {
          (*(i - 1))->AddChildChunk(new_chunk);
        }
        return;
      } else if (chunk->FirstKeyword() == "END") {
        // Found END from a nested CASE clause. Jump 2 levels up to skip it.
        if (!t->Parent()->IsTopLevel()) {
          t = t->Parent();
        }
        break;
      }
    }
    t = t->Parent();
  }

  // This will only trigger if the query is not syntax correct, so it's the best
  // we can do.
  chunk_block->AddIndentedChunk(new_chunk);
}

void AddBlockForChainableOperator(ChunkBlock* const chunk_block,
                                  Chunk* operator_chunk) {
  ChunkBlock* t = chunk_block;

  ChunkBlock* skip_target = nullptr;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (skip_target != nullptr) {
        if (*i == skip_target) {
          skip_target = nullptr;
        } else {
          // We continue only if it's not a match because we want to process
          // chunks like "AND (".
          continue;
        }
      }
      Chunk* chunk = (*i)->LastChunkUnder();
      if (chunk == nullptr) {
        continue;
      }

      if (!chunk->CanBePartOfExpression() ||
          (!chunk->HasMatchingClosingChunk() && chunk->OpensParenBlock())) {
        // This is a complex operation. It helps to see it on an example. Say
        // we have SELECT 1 * 3 + 2. The fully newlined format is:
        //
        // SELECT
        //   1
        //     * 3
        //   + 2
        //
        // As we go through the algorithm, after processing "* 3" we end up
        // with:
        //
        // root
        // +
        // +-block for select clause
        // | |
        // | +-leaf block for "SELECT" (<-- *i points here)
        // | |
        // | +-block for select expression
        // |   |
        // |   +-leaf block for "1"
        // |   |
        // +   +-leaf block for "* 3"
        //
        // Now when we add "+ 2", we want it to look like:
        //
        // root
        // +
        // +-block for select clause
        // | |
        // | +-leaf block for "SELECT" (<-- *i points here)
        // | |
        // | +-block for select expression
        // |   |
        // |   +-leaf block for "1"
        // |   |
        // |   +-intermediate block for *
        // |   | |
        // |   | +-leaf block for "* 3"
        // |   |
        // +   +-leaf block for "+ 2"
        //
        // Or in general, we want to find once more group and indent
        // everything that is after the first leaf block in the select
        // expression.
        if (i == t->children().rbegin()) {
          // This is an invalid query. Something of the form SELECT + 2. But
          // we still want to format it "correctly". In this case, we will do
          //
          // SELECT
          //   + 2
          //
          // Which clearly shows the error and makes it easy to insert the
          // missing value.
          (*i)->AddIndentedChunk(operator_chunk);
          return;
        }

        // We want the first "term" in the expression to be aligned with the
        // operator, even though it technically isn't at the same level in
        // terms of precedence. For example, we want:
        //
        // SELECT
        //  a
        //  + b
        //
        // And not:
        //
        // SELECT
        //    a
        //  + b
        //
        // Note however that parenthesis are a corner case:
        //
        // SELECT
        //  foo(
        //    a
        //  )
        //  + b
        // -1 is safe here because of the previous if.
        ChunkBlock* expression_block = *(i - 1);
        ChunkBlock* first_term;
        ChunkBlock::Children::reverse_iterator end_of_first_term;
        if ((*i)->IsLeaf() && (*i)->Chunk()->FirstKeyword() == "CASE" &&
            i != t->children().rbegin() &&
            (*(i - 1))->LastChunkUnder()->FirstKeyword() == "END") {
          // Special case: the first term in the expression is CASE operator.
          // It starts with a block for "CASE" and entire body is in the next
          // block.
          expression_block = t;
          first_term = *i;
          end_of_first_term = i - 1;
        } else if (!expression_block->IsLeaf()) {
          first_term = expression_block->children().front();
          end_of_first_term = expression_block->children().rend() - 1;
        } else {
          // There is no separate block for the current expression. The terms
          // of the expression might share block with another chunks.
          // Consider a query "SELECT a, b + c;". When we add chunk block for
          // "+ c" we get:
          //
          // root
          // +
          // +-block for select clause
          // | |
          // | +-leaf block for "SELECT"
          // | |
          // | +-block for select expression  (<-- t)
          // |   |
          // |   +-leaf block for "a,"        (<-- *i points here)
          // |   |
          // +   +-leaf block for "b"       (<-- *(i-1), expression begin)
          expression_block = t;
          first_term = *(i - 1);
          end_of_first_term = i - 1;
        }
        // Search for the end of the first term - it might take more than one
        // block. For instance "COUNT (*) OVER (...) / 2":
        //
        // +- "COUNT(*)"   (<-- first_term, current *end_of_first_term)
        // |
        // +-block for OVER clause  (<-- *end_of_first_term should point here)
        // |  |
        // |  +- "OVER ("
        //  ...
        // |  +- ")"
        // |
        // +- "/ 2"
        while (end_of_first_term != expression_block->children().rbegin()) {
          const Chunk* maybe_second_term =
              (*(end_of_first_term - 1))->FirstChunkUnder();
          if (maybe_second_term == nullptr ||
              maybe_second_term->StartsWithChainableOperator()) {
            break;
          }
          end_of_first_term--;
        }
        if (end_of_first_term == expression_block->children().rbegin()) {
          // There is only one term, so nothing to indent after it.
          if (expression_block->IsList()) {
            // If the expression appears in a list, it's continuation should be
            // always indented.
            first_term->AddIndentedChunk(operator_chunk);
          } else {
            first_term->AddSameLevelChunk(operator_chunk);
          }
          return;
        }

        // Find the matching parenthesis, if it is in this same subtree. If
        // not, then the query is possibly invalid, so we default to a single
        // chunkblock being kept at the same level.
        if (first_term->IsLeaf() && first_term->Chunk()->OpensParenBlock()) {
          if (first_term->Chunk()->HasMatchingClosingChunk()) {
            for (end_of_first_term = expression_block->children().rbegin();
                 end_of_first_term + 1 != expression_block->children().rend();
                 ++end_of_first_term) {
              if ((*(end_of_first_term))->IsLeaf()) {
                if ((*(end_of_first_term))->Chunk() ==
                    first_term->Chunk()->MatchingClosingChunk()) {
                  break;
                }
              }
            }
          }
        }

        if (expression_block->children().rbegin() != end_of_first_term) {
          expression_block->GroupAndIndentChildrenUnderNewBlock(
              expression_block->children().rbegin(), end_of_first_term);
        }
        if (expression_block->IsList()) {
          // If the expression appears in a list, it's continuation should be
          // always indented.
          first_term->AddIndentedChunk(operator_chunk);
        } else {
          first_term->AddSameLevelChunk(operator_chunk);
        }
        return;
      }
      if ((*i)->IsLeaf()) {
        if (chunk->ClosesParenBlock()) {
          if (!chunk->HasMatchingOpeningChunk()) {
            // This is probably an invalid query. Fallback to going up one
            // level.
            break;
          }
          // Jump over type declaration if it precedes parentheses.
          skip_target = chunk->JumpToTopOpeningBracketIfAny()->ChunkBlock();
          t = skip_target;
          break;
        } else if (chunk->FirstKeyword() == "END") {
          // Skip the CASE statement body - "CASE" keyword should be previous
          // sibling of END's parent block.
          skip_target = t->FindPreviousSiblingBlock();
          break;
        } else if (chunk->StartsWithChainableOperator()) {
          if (OperatorPrecedenceLevel(chunk->FirstToken()) ==
              OperatorPrecedenceLevel(operator_chunk->FirstToken())) {
            (*i)->AddSameLevelChunk(operator_chunk);
            return;
          }
          // It might seem counterintuitive that we check that the precedence is
          // higher, but remember that higher precedence gets evaluated first,
          // so it should be indented _more_.
          if (OperatorPrecedenceLevel(chunk->FirstToken()) >
              OperatorPrecedenceLevel(operator_chunk->FirstToken())) {
            (*i)->AddIndentedChunk(operator_chunk);
            return;
          }
        }
      }
    }
    t = t->Parent();
  }

  t->AddChildChunk(operator_chunk);
}

void AddBlockForCreateOrExportIndentedClause(Chunk* previous_chunk,
                                             Chunk* indented_chunk) {
  // If the previous chunk ends an `AS (...)` code block, add a same-level
  // chunk instead.  Common case is:
  //
  //   AS (
  //     ...
  //   )
  //   OPTIONS (
  //     ...
  //     ...)
  //
  // The new line will be removed in 'layout.cc::PruneLineBreaks' resulting in:
  //
  //   AS (
  //     ...
  //   ) OPTIONS (
  //     ...
  //     ...)
  Chunk* possible_as_chunk = nullptr;
  if (previous_chunk->ClosesParenBlock() &&
      previous_chunk->HasMatchingOpeningChunk()) {
    possible_as_chunk = previous_chunk->MatchingOpeningChunk();
    if (possible_as_chunk->FirstKeyword() == "AS") {
      possible_as_chunk->ChunkBlock()->AddSameLevelChunk(indented_chunk);
      return;
    }
  }

  // If we're inside a CREATE/EXPORT block, add a indented chunk to that block.
  ChunkBlock* t = previous_chunk->ChunkBlock();
  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf() || (*i)->Chunk()->Empty()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->IsStartOfCreateStatement() ||
          chunk->IsStartOfExportDataStatement()) {
        (*i)->AddIndentedChunk(indented_chunk);
        return;
      } else if (chunk->OpensParenBlock() &&
                 !chunk->HasMatchingClosingChunk()) {
        (*i)->AddIndentedChunk(indented_chunk);
        return;
      } else if (chunk->IsCreateOrExportIndentedClauseChunk()) {
        (*i)->AddSameLevelChunk(indented_chunk);
        return;
      }
    }
    t = t->Parent();
  }

  // Otherwise, use default logic.
  previous_chunk->ChunkBlock()->AddIndentedChunk(indented_chunk);
}

void AddBlockForJoinClause(ChunkBlock* const chunk_block, Chunk* join_chunk) {
  ChunkBlock* t = chunk_block;

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if ((*i)->IsLeaf()) {
        Chunk* chunk = (*i)->Chunk();
        bool with_closing_parenthesis = false;
        if (chunk->ClosesParenBlock()) {
          // We advance the pointer forward until we reach the opening
          // parenthesis, and than use that to determine grouping.
          with_closing_parenthesis = true;
          while (i != t->children().rend()) {
            if ((*i)->IsLeaf()) {
              chunk = (*i)->Chunk();
              if (chunk->LastKeyword() == "(") {
                break;
              }
            }
            ++i;
          }
          if (i == t->children().rend()) {
            break;
          }
        }
        if (chunk->IsFromKeyword()) {
          t->AddChildChunk(join_chunk);
          return;
        }
        if (!with_closing_parenthesis && !chunk->Empty() &&
            chunk->LastKeyword() == "(") {
          (*i)->AddIndentedChunk(join_chunk);
          return;
        }
        if (chunk->IsJoin()) {
          t->AddChildChunk(join_chunk);
          return;
        }
      }
    }
    t = t->Parent();
  }

  // Did not find a place for JOIN (malformed query?)
  chunk_block->AddSameLevelChunk(join_chunk);
}

void AddBlockForOnOrUsingOrWithOffsetAs(ChunkBlock* const chunk_block,
                                        Chunk* condition_chunk) {
  ChunkBlock* t = chunk_block;
  const bool is_on = (condition_chunk->FirstKeyword() == "ON");
  const bool is_using = (condition_chunk->FirstKeyword() == "USING");
  const bool is_with_offset_as = (condition_chunk->IsWithOffsetAsClause());

  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if ((*i)->IsLeaf()) {
        Chunk* chunk = (*i)->Chunk();
        if (is_with_offset_as && (chunk->IsFromKeyword() || chunk->IsJoin())) {
          // `WITH OFFSET AS` inside a `FROM` or `JOIN` clause.
          if (i != t->children().rbegin() && !(*(i - 1))->IsLeaf()) {
            // *(i-1) points to the indented expression after `FROM` or `JOIN`
            // keyword.
            (*(i - 1))->AddChildChunk(condition_chunk);
          } else {
            (*i)->AddIndentedChunk(condition_chunk);
          }
          return;
        } else if ((is_on || is_using) && chunk->IsJoin()) {
          // `ON` or `USING` inside a `JOIN` clause.  Note that `ON` and `USING`
          // can only appear after `JOIN`, whereas `WITH OFFSET AS` can appear
          // in the `FROM` clause before the first join.
          if (i != t->children().rbegin() && !(*(i - 1))->IsLeaf()) {
            (*(i - 1))->AddChildChunk(condition_chunk);
          } else {
            (*i)->AddIndentedChunk(condition_chunk);
          }
          condition_chunk->Tokens().WithoutComments()[0]->SetType(
              Token::Type::JOIN_CONDITION_KEYWORD);
          return;
        }
      }
    }
    t = t->Parent();
  }

  // By default, `ON` / `USING` clauses that aren't part of JOINs should be
  // added as top-level clause (examples: `ON` inside `GRANT` & `REVOKE`
  // statements, `ON` & `USING` inside `CREATE ROW ACCESS POLICY` statements).
  if (is_on || is_using) {
    AddBlockForTopLevelClause(chunk_block, condition_chunk);
  } else {
    chunk_block->AddSameLevelChunk(condition_chunk);
  }
}

void AddBlockFollowingOpeningBracket(Chunk* opening_bracket_chunk,
                                     Chunk* chunk_to_add) {
  // Jump to the beginning of the type declaration, if it precedes an opening
  // bracket.
  opening_bracket_chunk->JumpToTopOpeningBracketIfAny()
      ->ChunkBlock()
      ->AddIndentedChunk(chunk_to_add);
}

void AddBlockForFunctionSignatureModifier(ChunkBlock* previous_chunk_block,
                                          Chunk* modifier_chunk) {
  ChunkBlock* t = previous_chunk_block->Parent();
  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->ClosesParenBlock() && chunk->HasMatchingOpeningChunk()) {
        chunk = chunk->MatchingOpeningChunk();
      }
      if (chunk->IsStartOfCreateStatement() ||
          chunk->IsFunctionSignatureModifier()) {
        (*i)->AddSameLevelChunk(modifier_chunk);
        modifier_chunk->Tokens().WithoutComments()[0]->SetType(
            Token::Type::TOP_LEVEL_KEYWORD);
        return;
      }
      // Go one level up.
      break;
    }
    t = t->Parent();
  }

  // Many function signature modifiers are non reserved keywords and may act as
  // a normal identifier - use default logic.
  previous_chunk_block->AddIndentedChunk(modifier_chunk);
}

// Returns true if the given `chunk` is a closing parenthesis of an OPTIONS ()
// clause or a closing bracket for a @{query hint}. Bot can appear after a top
// level keyword and require similar formatting.
bool ClosesQueryHintOrOptionsClause(const Chunk& chunk) {
  if (!chunk.ClosesParenOrBracketBlock() || !chunk.HasMatchingOpeningChunk()) {
    return false;
  }

  const Chunk* opening_bracket = chunk.MatchingOpeningChunk();
  if (opening_bracket->LastKeywordsAre("OPTIONS", "(")) {
    return true;
  }
  if (opening_bracket->FirstKeyword() == "@" &&
      opening_bracket->LastKeyword() == "{") {
    return true;
  }

  return false;
}

bool ClosesLastTableInWithClause(const Chunk& chunk) {
  if (chunk.LastKeyword() != ")" && !chunk.EndsWithMacroCall()) {
    return false;
  }
  const ChunkBlock* may_be_with =
      chunk.ChunkBlock()->Parent()->FindPreviousSiblingBlock();
  return may_be_with != nullptr && may_be_with->IsLeaf() &&
         may_be_with->Chunk()->IsTopLevelWith();
}

void AddBlockForDdlChunk(ChunkBlock* previous_block, Chunk* ddl_chunk) {
  ChunkBlock* t = previous_block->Parent();
  while (!t->IsTopLevel()) {
    for (auto i = t->children().rbegin(); i != t->children().rend(); ++i) {
      if (!(*i)->IsLeaf()) {
        continue;
      }
      Chunk* chunk = (*i)->Chunk();
      if (chunk->ClosesParenBlock() && chunk->HasMatchingOpeningChunk()) {
        chunk = chunk->MatchingOpeningChunk();
      }
      if (chunk->FirstToken().Is(Token::Type::DDL_KEYWORD)) {
        (*i)->AddSameLevelChunk(ddl_chunk);
        return;
      } else if (chunk->IsTopLevelClauseChunk()) {
        // Normally, DDL clauses are indented relatively to top level clauses.
        // ALTER TABLE Foo
        // ADD COLUMN Bar     <- top level clause;
        //   FILL USING NULL  <- DDL modifier.
        //
        // "SET OPTIONS" is special, since it can be both top level clause and
        // a DDL modifier:
        // ALTER TABLE Foo                ALTER TABLE Foo
        // SET OPTIONS (...);             ALTER COLUMN bar
        //                                  SET OPTIONS (...);
        if (ddl_chunk->FirstKeyword() == "SET" &&
            chunk->FirstKeyword() == "ALTER" &&
            chunk->SecondKeyword() != "COLUMN" &&
            chunk->SecondKeyword() != "CONSTRAINT") {
          (*i)->AddSameLevelChunk(ddl_chunk);
          ddl_chunk->Tokens().WithoutComments()[0]->SetType(
              Token::Type::TOP_LEVEL_KEYWORD);
        } else {
          (*i)->AddIndentedChunk(ddl_chunk);
        }
        return;
      }
      // Go one level up.
      break;
    }
    t = t->Parent();
  }

  // By default, put DDL keywords on the same level with previous chunk (unlike
  // the default in other statements, where we indent by default).
  previous_block->AddSameLevelChunk(ddl_chunk);
}

// Adds a chunk block for a chained method call, deciding whether to indent it
// or keep it at the same level as the previous part of the chain.
// previous_chunk: The chunk before the method call.
void AddBlockForChainedMethodCalls(Chunk* const previous_chunk,
                                   Chunk* dot_chunk) {
  Chunk* anchor = nullptr;
  if (previous_chunk->FirstToken().Is(
          Token::Type::CHAINED_METHOD_DOT_SEPARATOR)) {
    // Case 1: Previous chunk is .METHOD()
    anchor = previous_chunk;
  } else if (previous_chunk->ClosesParenOrBracketBlock() &&
             previous_chunk->HasMatchingOpeningChunk() &&
             previous_chunk->MatchingOpeningChunk()->FirstToken().Is(
                 Token::Type::CHAINED_METHOD_DOT_SEPARATOR)) {
    // Case 2: Previous chunk is ')' closing .METHOD(...)
    anchor = previous_chunk->MatchingOpeningChunk();
  }

  if (anchor != nullptr) {
    // If we found an anchor, add the new method call at the same level.
    anchor->ChunkBlock()->AddSameLevelChunk(dot_chunk);
  } else {
    // Default case: This is the first method call in a chain, or
    // previous chunk is unrelated. Indent the new method call relative
    // to the expression it's being called on.
    previous_chunk->ChunkBlock()->AddIndentedChunk(dot_chunk);
  }
}
}  // namespace

absl::Status ComputeChunkBlocksForChunks(ChunkBlockFactory* block_factory,
                                         std::vector<Chunk>* chunks) {
  if (chunks->empty()) {
    return absl::OkStatus();
  }

  ChunkBlock* top = block_factory->Top();
  int chunk_index = 0;
  int previous_chunk_index = 0;

  // We skip all comments at the top of the file.
  for (; chunk_index < chunks->size(); ++chunk_index) {
    Chunk& chunk = chunks->at(chunk_index);
    if (!chunk.IsCommentOnly()) {
      top->AddIndentedChunk(&(chunks->at(chunk_index)));
      previous_chunk_index = chunk_index;
      ++chunk_index;
      break;
    }
  }

  for (; chunk_index < chunks->size(); ++chunk_index) {
    Chunk& chunk = chunks->at(chunk_index);

    // We skip comments in this pass.
    if (chunk.IsCommentOnly()) {
      continue;
    }

    Chunk& previous_chunk = chunks->at(previous_chunk_index);
    previous_chunk_index = chunk_index;

    // We start with the previous level and then go up or down based on the
    // current chunk.
    ChunkBlock* chunk_block = previous_chunk.ChunkBlock();
    ChunkBlock* parent_chunk_block = chunk_block->Parent();

    if (chunk.IsImport()) {
      top->AddIndentedChunk(&chunk);
    } else if (previous_chunk.IsSetOperator()) {
      // SET operators always introduce a new sibling chunk.
      chunk_block->AddSameLevelChunk(&chunk);
    } else if (chunk.IsSetOperator()) {
      AddBlockForSetOperator(chunk_block, &chunk);
    } else if (chunk.IsStartOfNewQuery()) {
      AddBlockForSelectOrWith(&previous_chunk, &chunk);
    } else if (chunk.ClosesParenOrBracketBlock()) {
      AddBlockForClosingBracket(&previous_chunk, &chunk);
    } else if (chunk.IsCreateOrExportIndentedClauseChunk()) {
      AddBlockForCreateOrExportIndentedClause(&previous_chunk, &chunk);
    } else if (chunk.IsTopLevelClauseChunk()) {
      AddBlockForTopLevelClause(chunk_block, &chunk);
    } else if (chunk.FirstKeyword() == "|>") {
      AddBlockForPipeOperator(chunk_block, &chunk);
    } else if (chunk.IsJoin()) {
      AddBlockForJoinClause(chunk_block, &chunk);
    } else if (chunk.FirstKeyword() == "ON" ||
               chunk.FirstKeyword() == "USING" ||
               chunk.IsWithOffsetAsClause()) {
      AddBlockForOnOrUsingOrWithOffsetAs(chunk_block, &chunk);
    } else if (chunk.FirstToken().Is(Token::Type::DDL_KEYWORD)) {
      // Should be before handling top level clause keywords, since some ddl
      // keywords may be top level in other contexts.
      AddBlockForDdlChunk(chunk_block, &chunk);
    } else if (chunk.FirstToken().Is(Token::Type::SELECT_STAR_MODIFIER)) {
      if (parent_chunk_block->FirstChunkUnder() != nullptr &&
          parent_chunk_block->FirstChunkUnder()->FirstToken().Is(
              Token::Type::SELECT_STAR_MODIFIER)) {
        // If there is already a same-level select * modifier, add a sibling.
        // Otherwise, add an indented chunk.
        chunk_block->AddSameLevelChunk(&chunk);
      } else {
        chunk_block->AddIndentedChunk(&chunk);
      }
    } else if (chunk.FirstToken().Is(
                   Token::Type::CHAINED_METHOD_DOT_SEPARATOR)) {
      AddBlockForChainedMethodCalls(&previous_chunk, &chunk);
    } else if (chunk.FirstKeyword() == ".") {
      if (previous_chunk.StartsWithChainableOperator() ||
          previous_chunk.FirstKeyword() == "=") {
        chunk_block->AddIndentedChunk(&chunk);
      } else {
        chunk_block->AddSameLevelChunk(&chunk);
      }
    } else if (chunk.FirstKeyword() == "AS") {
      AddBlockForAsKeyword(chunk_block, &chunk);
    } else if (chunk.FirstToken().IsCaseExprKeyword()) {
      if (chunk.FirstKeyword() == "THEN") {
        AddBlockForThenKeyword(chunk_block, &chunk);
      } else {
        AddBlockForWhenElseEndKeywords(chunk_block, &chunk);
      }
    } else if (previous_chunk.LastKeyword() == ",") {
      AddBlockFollowingAComma(&previous_chunk, &chunk);
    } else if (chunk.StartsWithChainableOperator()) {
      AddBlockForChainableOperator(chunk_block, &chunk);
    } else if (previous_chunk.OpensParenOrBracketBlock()) {
      AddBlockFollowingOpeningBracket(&previous_chunk, &chunk);
    } else if (chunk.FirstToken().Is(Token::Type::BRACED_CONSTR_FIELD)) {
      // In braced constructors commas are optional, but otherwise, fields
      // behave as in the list.
      AddBlockFollowingAComma(&previous_chunk, &chunk);
    } else if (chunk.MayBeFunctionSignatureModifier()) {
      AddBlockForFunctionSignatureModifier(chunk_block, &chunk);
    } else if (ClosesQueryHintOrOptionsClause(previous_chunk)) {
      // Query hints and OPTIONS clause might appear at the beginning of top
      // level clause. In this case the following chunk should be on the same
      // level. For instance:
      // SELECT                    SELECT
      //   OPTIONS (...)             @{query_hint}
      //   a;                        a;
      chunk_block->AddSameLevelChunk(&chunk);
    } else if (ClosesLastTableInWithClause(previous_chunk)) {
      // The first chunk after last table in WITH clause should be on the same
      // level with WITH:
      // WITH
      //   Table AS (
      //     ...
      //   )  # <- Previous chunk.
      // SELECT  # <- This chunk.
      parent_chunk_block->AddSameLevelChunk(&chunk);
      if (chunk.FirstToken().IsMacroCall()) {
        chunk.Tokens().WithoutComments()[0]->SetType(
            Token::Type::TOP_LEVEL_KEYWORD);
      }
    } else {
      // If we don't know what else to do, we nest under the last chunk.
      // TODO: Consider if perhaps same-level is better here. Indenting
      // was chosen just to preserve existing behavior, but once the new
      // formatter is in place, we can decide to change this to same-level.
      chunk_block->AddIndentedChunk(&chunk);
    }

    if (previous_chunk.IsStartOfList()) {
      chunk.ChunkBlock()->Parent()->MarkAsList();
    }
  }

  // Insert all comment chunks into the block tree.
  if (chunks->back().IsCommentOnly()) {
    top->AddChildChunk(&chunks->back());
  }

  // Put comments on the same level as the following chunk:
  //   SELECT
  //     -- comment
  //     foo;
  // except indent if the following chunk is a closing bracket:
  //   (
  //     -- comment
  //   )
  for (chunk_index = static_cast<int>(chunks->size()) - 2; chunk_index >= 0;
       --chunk_index) {
    Chunk& chunk = chunks->at(chunk_index);
    if (chunk.IsCommentOnly()) {
      Chunk& next_chunk = chunks->at(chunk_index + 1);
      if (next_chunk.ClosesParenOrBracketBlock()) {
        next_chunk.ChunkBlock()->AddIndentedChunkImmediatelyBefore(&chunk);
      } else {
        next_chunk.ChunkBlock()->AddSameLevelChunkImmediatelyBefore(&chunk);
      }
    }
  }

  return absl::OkStatus();
}

}  // namespace googlesql::formatter::internal
