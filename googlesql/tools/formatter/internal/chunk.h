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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_H_

#include <stddef.h>

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

class ChunkBlock;

// A formatter chunk is a non-breakeable part of the query.
//
// It consists of one or more ParseTokens separated by zero or more spaces. For
// example "IMPORT" and "MODULE" would be 2 ParseTokens, but because we will
// never break the line between them, the formatter will treat "IMPORT MODULE"
// as a single Chunk.
class Chunk {
 public:
  // Returns an empty chunk that will contain (at least) the given token. Note
  // that the token is not added to the chunk; it is only used to find chunk
  // column offset.
  static absl::StatusOr<Chunk> CreateEmptyChunk(
      bool starts_with_space, int position_in_query,
      const ParseLocationTranslator& location_translator,
      const Token& first_token);

  // Creates a new chunk that <starts_with_space> (or not) that is the
  // <position_in_query>-th chunk in the query.
  Chunk(bool starts_with_space, int position_in_query, int original_column)
      : starts_with_space_(starts_with_space),
        position_in_query_(position_in_query),
        original_column_(original_column) {}

  // Add a Token to this chunk.
  void AddToken(Token* token) { tokens_.Add(token); }

  // Returns Tokens in this chunk.
  const TokensView& Tokens() const { return tokens_; }

  // Returns true if the chunk is empty.
  bool Empty() const { return tokens_.All().empty(); }

  // Returns true if the chunk should be printed with a preceding whitespace,
  // if and only if the chunk is not at the beginning of the line.
  bool StartsWithSpace() const { return starts_with_space_; }

  // Returns the position of this chunk in the sequence of chunks that represent
  // the whole query.
  int PositionInQuery() const { return position_in_query_; }

  // Get the string representation of this chunk that can be displayed to a
  // human, using <options> and with the chunk starting at <column>.
  // <is_line_end> tells whether this chunk is the last one on the line: this
  // influences how many spaces are added before trailing comment, if any.
  std::string PrintableString(const FormatterOptions& options, int column,
                              bool is_line_end) const;

  // Returns the length of this chunk in characters.
  // <is_line_end> tells whether this chunk is the last one on the line: this
  // influences how many spaces are added before trailing comment, if any.
  int PrintableLength(bool is_line_end) const;

  // Returns a normalized string representation of this chunk that can be used
  // in tests or log lines. It's similar to PrintableString, but doesn't take
  // into account the information about token location on the line.
  std::string DebugString(bool verbose = false) const;

  // Prints a <chunk> into a stream (especially in googletest).
  friend std::ostream& operator<<(std::ostream& os, const Chunk& chunk) {
    return os << chunk.DebugString();
  }
  friend std::ostream& operator<<(std::ostream& os, const Chunk* chunk);

  // Returns the i'th non-comment token in this chunk.
  // If i < 0, then the tokens are searched from end, with i = -1 corresponding
  // to the last token.
  // If the token with specified index doesn't exist, returns an empty Token.
  const Token& NonCommentToken(int i) const;

  // Same as above, but returns a keyword string of the given token.
  // Returns "" if the token with specified index doesn't exist or is an
  // identifier. Note, that some keywords can appear to be unquoted identifiers
  // (see table in parse_token.h).
  absl::string_view NonCommentKeyword(int i) const;

  // Syntactic sugar to fetch i'th keyword or token from a chunk. See above.
  absl::string_view FirstKeyword() const;
  absl::string_view SecondKeyword() const;
  absl::string_view LastKeyword() const;
  const Token& FirstToken() const;
  const Token& LastToken() const;

  // Set the inner-most chunk block for this chunk.
  void SetChunkBlock(ChunkBlock* chunk_block) { chunk_block_ = chunk_block; }

  // Returns the inner-most chunk block this chunk belongs to.
  class ChunkBlock* ChunkBlock() const { return chunk_block_; }

  // Returns true if the chunk ends with a comment.
  bool EndsWithComment() const;

  // Returns true if the chunk ends with a single-line comment (not a
  // /*comment*/). No other tokens can follow a single-line comment on the same
  // line (otherwise they become a part of the same comment).
  bool EndsWithSingleLineComment() const;

  // Returns true if the chunk contains only comment tokens.
  bool IsCommentOnly() const;

  // Returns true if the chunk is an import.
  bool IsImport() const;

  // Returns true if the chunk is a module declaration.
  bool IsModuleDeclaration() const;

  // Returns true if the chunk starts with a query top-level keyword, e.g.
  // "SELECT" or "FROM" or "WHERE".
  bool IsTopLevelClauseChunk() const;

  // Returns true if the chunk is a set operator, e.g. "UNION ALL".
  bool IsSetOperator() const;

  // Returns true if the last two keywords in the chunk match the provided ones.
  // The function ignores inline comments that might be anywhere in the chunk.
  bool LastKeywordsAre(absl::string_view k1, absl::string_view k2) const;

  // Returns true if the chunk may be a function signature modifier (or may be a
  // normal identifier). E.g., "RETURNS" might appear both as a function
  // signature and as a modifier.
  bool MayBeFunctionSignatureModifier() const;

  // Returns true if the chunk is a function signature modifier, e.g.,
  // "DETERMINISTIC", "LANGUAGE", "RETURNS", etc.
  bool IsFunctionSignatureModifier() const;

  // Returns true if the chunk is a start of a new query or sub-query.
  bool IsStartOfNewQuery() const;

  // Returns true if the chunk is a start of a UDF/TVF definition or a
  // materialization statement, e.g. "CREATE TABLE [FUNCTION] () AS ..."
  bool IsStartOfCreateStatement() const;

  // Returns true if the chunk is a start of an EXPORT DATA statement.
  bool IsStartOfExportDataStatement() const;

  // Returns true if the chunk is "DEFINE MACRO <name>".
  bool IsStartOfDefineMacroStatement() const;

  // Returns true if the current chunk starts a list. E.g. in "SELECT a, b;"
  // SELECT starts a list.
  bool IsStartOfList() const;

  // Returns true if the chunk is a start of a clause which should be indented
  // with respect to a parent CREATE or EXPORT DATA clause.
  bool IsCreateOrExportIndentedClauseChunk() const;

  // Returns true if this chunk contains a multiline token.
  bool IsMultiline() const;

  // Returns true if this chunk starts a top level WITH clause.
  bool IsTopLevelWith() const;

  // Returns true if the chunk ends with a opening parenthesis, i.e., starts a
  // new parentheses block.
  bool OpensParenBlock() const;

  // Returns true if the chunk ends with a opening parenthesis (, square
  // bracket [, or curly brace {.
  bool OpensParenOrBracketBlock() const;

  // Returns true if the chunk starts with a closing parenthesis.
  bool ClosesParenBlock() const;

  // Returns true if the chunk starts with a closing parenthesis ), square
  // bracket ], or curly brace }.
  bool ClosesParenOrBracketBlock() const;

  // Returns true if the chunk is a beginning of a type declaration. E.g., for
  // type "STRUCT<INT64>", the chunk "STRUCT<" will be a type declaration start.
  bool IsTypeDeclarationStart() const;

  // Returns true if the chunk is an end of a type declaration, i.e., contains
  // a closing bracket (">") of a type like "STRUCT<INT64>".
  bool IsTypeDeclarationEnd() const;

  // Returns true if this chunk should be never followed by a new line.
  // Normally the following token would be merged into this chunk, but in some
  // cases tokens are annotated only during building the block tree.
  bool ShouldNeverBeFollowedByNewline() const;

  // Returns true if the chunk has a known chunk with matching opening/closing
  // parenthesis or bracket.
  bool HasMatchingOpeningChunk() const {
    return matching_open_chunk_ != nullptr;
  }
  bool HasMatchingClosingChunk() const {
    return matching_close_chunk_ != nullptr;
  }

  // Returns the chunk with matching opening/closing parenthesis or bracket.
  // If there are no matching chunks, returns nullptr.
  Chunk* MatchingOpeningChunk() const { return matching_open_chunk_; }
  Chunk* MatchingClosingChunk() const { return matching_close_chunk_; }

  // If the current chunk has closing bracket/parenthesis, returns the first
  // opening bracket in a chain. For instance, in query "FOO(1)[0]", for chunk
  // "]" will jump to "Foo(".
  Chunk* JumpToTopOpeningBracketIfAny();

  // Sets <matching_chunk> to this chunk. If a matching chunk is already added,
  // it will be overwritten.
  void SetMatchingOpeningChunk(Chunk* matching_chunk);
  void SetMatchingClosingChunk(Chunk* matching_chunk);

  // Checks whether current or previous token in this chunk can be unary
  // operators and updates token types if needed.
  void UpdateUnaryOperatorTypeIfNeeded(const Chunk* previous_chunk,
                                       const Token* previous_token);

  // Pointers to the next and previous chunks in the input.
  Chunk* NextChunk() const { return next_chunk_; }
  Chunk* PreviousChunk() const { return prev_chunk_; }

  // Returns a pointer to the next non-comment chunk or nullptr if there is
  // none.
  const Chunk* NextNonCommentChunk() const;
  // Returns a pointer to the previous non-comment chunk or nullptr if there is
  // none.
  const Chunk* PreviousNonCommentChunk() const;

  // Checks if there is previous/next chunk.
  bool HasNextChunk() const { return next_chunk_ != nullptr; }
  bool HasPreviousChunk() const { return prev_chunk_ != nullptr; }

  // Set the previous/next chunks.
  void SetPreviousChunk(Chunk* previous) { prev_chunk_ = previous; }
  void SetNextChunk(Chunk* next) { next_chunk_ = next; }

  // Returns true if the chunk is the start of a FROM clause, either just "FROM"
  // or maybe "FROM (" or other such parenthesis cases.
  bool IsFromKeyword() const;

  // Returns true if the chunk is a JOIN clause. All JOIN clauses contain the
  // keyword "JOIN" optionally preceded by the join type, e.g. "INNER".
  bool IsJoin() const;

  // Returns true if the chunk is a `WITH OFFSET [AS] <id>` sequence in the
  // FROM clause.
  bool IsWithOffsetAsClause() const;

  // Returns true if the chunk can be a part of a continuous expression (e.g. "1
  // + 2 + 3" or "a AND b AND c").
  bool CanBePartOfExpression() const;

  // Returns true if the chunk starts with a chainable operator, which is any
  // operator that can be followed by another operator in the same expression,
  // e.g.: =, +, IN().
  bool StartsWithChainableOperator() const;

  // Returns true if the chunk starts with a repeatable operator. Repeatable
  // operator returns a compatible value type to the one it accepts and thus can
  // be repeated multiple times, e.g.: +, -, AND, OR.
  bool StartsWithRepeatableOperator() const;

  // Returns true if the chunk starts with AND or OR.
  bool StartsWithChainableBooleanOperator() const;

  // Returns true if the chunk ends with a macro call, e.g. "+ $FOO(arg)".
  bool EndsWithMacroCall() const;

  // Returns true if the space is needed between two tokens. Can be used to
  // decide whether a space is needed for tokens within the same chunk, or
  // between two chunks. In the latter case, the function should be called on a
  // previous chunk with <token_before> pointing to its last token and
  // <token_after> pointing to the first token of the next chunk.
  //
  // If the layout element is not a newline then the number of spaces we need to
  // insert between the two parser tokens is either 0 or 1. Inserting more would
  // be a waste of horizontal space.
  //
  // To decide whether a space is needed or not, we need to look at the previous
  // tokens (at maximum 1 chunk back).
  bool SpaceBetweenTokens(const Token& token_before,
                          const Token& token_after) const;

  // Returns the amount of empty lines there were before the first token of this
  // chunk in the original query.
  int GetLineBreaksBefore() const;

  // Returns the amount of empty lines there were after the last token of this
  // chunk in the original query.
  int GetLineBreaksAfter() const;

 private:
  // Same as `SpaceBetweenTokens` above, but accepts the token index within the
  // current chunk (while `SpaceBetweenTokens` can be used to check the space
  // between two adjacent chunks).
  bool SpaceBeforeToken(int index) const;

  // Should the chunk be printed with a preceding whitespace, if not at the
  // start of a line.
  bool starts_with_space_;

  // The position of this Chunk in the query.
  int position_in_query_;

  // The column in the original query of the first token in this chunk.
  int original_column_;

  // The Tokens that form this chunk. They are ordered by the order they appear
  // in the query. Note that the actual tokens are not owned by this chunk and
  // should outlive it.
  TokensView tokens_;

  // The inner-most chunk block this chunk belongs to.
  class ChunkBlock* chunk_block_ = nullptr;

  // A matching chunk for opening/closing parenthesis/bracket, if one exists.
  Chunk* matching_open_chunk_ = nullptr;
  Chunk* matching_close_chunk_ = nullptr;

  // Links to the previous and next chunk in a double-linked list.
  Chunk* next_chunk_ = nullptr;
  Chunk* prev_chunk_ = nullptr;
};

// Generates and owns chunk block objects (see ChunkBlock class description).
class ChunkBlockFactory {
 public:
  explicit ChunkBlockFactory(int indentation_spaces);

  // Factory functions to create new blocks. The returned block must be added
  // manually to any other block's children otherwise it will be lost in the
  // output. If offset is provided, it overwrites the default indentation
  // relative to the parent block.
  ChunkBlock* NewChunkBlock();
  ChunkBlock* NewChunkBlock(ChunkBlock* parent);
  ChunkBlock* NewChunkBlock(ChunkBlock* parent, Chunk* chunk);

  // Returns the top of the ChunkBlock tree created so far.
  ChunkBlock* Top() { return blocks_.front().get(); }
  const ChunkBlock* Top() const { return blocks_.front().get(); }

  // Wipe out all chunk blocks generated so far and start from an empty block
  // tree. Used in testing.
  void Reset();

  // Returns the default number of spaces to indent a block relative to its
  // parent.
  int IndentationSpaces() const { return indentation_spaces_; }

 private:
  const int indentation_spaces_;
  std::vector<std::unique_ptr<ChunkBlock>> blocks_;
};

// A chunk block is a collection of chunks that (might) get indented together.
//
// A chunk block can contain other chunk blocks, or a single Chunk.
//
// For example, the query "SELECT a + b FROM t" chunks into 5 chunks: "SELECT",
// "a", "+ b", "FROM", "t". The chunk blocks for this query form the following
// tree:
//
// root
// +
// +-block for entire select clause
// | |
// | +-leaf block for "SELECT"
// | |
// | +-block for select expression
// |   |
// |   +-leaf block for "a"
// |   |
// |   +-leaf block for "+ b"
// |
// +-block for entire from clause
// | |
// | +-leaf block for "FROM"
// | |
// | +-block for from expression
// |   |
// +   +-leaf block for "t"
//
// One of the important things to observe is that the leaf blocks for "SELECT"
// and "FROM" are 3 nodes deep into the tree (root -> clause block -> leaf). But
// in the query, both would be indented 0 spaces. In the same way, "a", "+ b"
// and "t" are 4 nodes deep, but would be indented 1 space.
class ChunkBlock {
 public:
  // Returns true if the block doesn't have a parent, which means it is at the
  // very top of the file and usually encompases the whole query (or queries).
  bool IsTopLevel() const { return parent_ == nullptr; }

  // Returns true if the block is at the very end of a block nesting and
  // contains exactly one chunk and no child blocks.
  bool IsLeaf() const { return !IsTopLevel() && chunk_ != nullptr; }

  // Marks the current block as a parent block of a list (at least two
  // comma-separated expressions). Lists appear in SELECT body, function
  // signatures and function calls, OPTIONS clause, etc.
  void MarkAsList() { is_list_ = true; }
  // Returns true if this block was marked as containing a list.
  bool IsList() const { return is_list_; }
  // Marks the current block as a start of a line that should be broken close
  // to line length limit.
  void MarkAsBreakCloseToLineLength() { break_close_to_line_length_ = true; }
  // Returns true if the formatter shouldn't try to preserve user line breaks in
  // the line that starts with this block.
  bool BreakCloseToLineLength() const { return break_close_to_line_length_; }

  // Sets the minimum indent for this block compared to the previous level. This
  // can be used to overwrite the default indentation size. If the value is
  // smaller than the default indentation size, it will be ignored. Note that
  // this adds additional indent only for blocks on this same level - children
  // don't get this extra indent.
  void SetMinOffset(int offset);

  // Returns the indentation level of this block. For leaf blocks, this is equal
  // to the number of indentation spaces the Chunk would have, if all possible
  // newlines were used.
  int Level() const { return level_ + extra_offset_; }

  // Adds a new leaf chunk block for <chunk> that is at the same level as this
  // chunk block. Effectively, this means that the parent of this block will add
  // a new child block to its children.
  //
  // parent block
  // |
  // +--this block
  // |
  // +--new leaf block for <chunk>
  ChunkBlock* AddSameLevelChunk(class Chunk* chunk);

  // Adds a chunk on the same level as this chunk block, but creates a new
  // parent block for it. This way, the chunks have the same indentation level,
  // but are not siblings. This is useful when parts of a statement are on the
  // same level, but one part may stay on the same line, while another splits
  // to multiple lines.
  //
  // grandparent block
  // |
  // +--parent
  // |  |
  // |  +--this block
  // |
  // +--new parent
  //    |
  //    +-- new leaf block for <chunk>
  ChunkBlock* AddSameLevelCousinChunk(class Chunk* chunk);

  // Adds two new chunk blocks, one intermediate chunk block that is at the same
  // level as this block, and then one more leaf chunk block for <chunk> that is
  // a child of the newly added intermediate block. The intermediate is then
  // added as a child to the parent of this block.
  //
  // parent block
  // |
  // +--this block
  // |
  // +--new block
  //    |
  //    +--new leaf block for <chunk>
  ChunkBlock* AddIndentedChunk(class Chunk* chunk);

  // Adds a new leaf chunk block for <chunk> that is at the same level as this
  // block and is immediately before this block. This is used to add comments
  // into a layout.
  //
  // parent block
  // |
  // +--new leaf block for <chunk>
  // |
  // +--this block
  ChunkBlock* AddSameLevelChunkImmediatelyBefore(class Chunk* chunk);

  // Adds a new leaf chunk block for <chunk> that is indented relative to this
  // block and is immediately before this block. This is used to add comments
  // into a layout.
  //
  // parent block
  // |
  // +--new block
  // |  |
  // |  +--new leaf block for <chunk>
  // |
  // +--this block
  ChunkBlock* AddIndentedChunkImmediatelyBefore(class Chunk* chunk);

  // Adds a new leaf chunk block for <chunk> that is a direct child of this
  // chunk block.
  //
  // this block
  // |
  // +--new leaf block for <chunk>
  ChunkBlock* AddChildChunk(class Chunk* chunk);

  // Adds the given <block> as a child of this block. It will also modify the
  // parent, level and all other properties of <block> to reflect it's new
  // position in the tree.
  //
  // this block
  // |
  // +--<block>
  void AdoptChildBlock(ChunkBlock* block);

  // Returns the next/previous sibling chunk block if any.
  //
  // parent block
  // |
  // +--previous block
  // |
  // +--this block
  // |
  // +--next sibling
  ChunkBlock* FindNextSiblingBlock() const;
  ChunkBlock* FindPreviousSiblingBlock() const;

  // Returns the Chunk this block contains. If the block is a leaf block, a
  // chunk is returned. If the block is not a leaf block, nullptr is returned.
  class Chunk* Chunk() const {
    return chunk_;
  }

  // Returns the first Chunk that is "under" this block. I.e. it finds the
  // left-most leaf block child of this block and returns that leaf block's
  // chunk. If there are no leaf blocks under this block, nullptr is returned.
  class Chunk* FirstChunkUnder() const;

  // Same as above, but returns the last chunk "under" this block.
  class Chunk* LastChunkUnder() const;

  ChunkBlock* Parent() const { return parent_; }

  using ChildrenT = ::std::vector<ChunkBlock*>;

  class Children {
   public:
    using iterator = std::vector<ChunkBlock*>::iterator;

    using reverse_iterator = std::vector<ChunkBlock*>::reverse_iterator;

    using const_iterator = std::vector<ChunkBlock*>::const_iterator;

    using const_reverse_iterator =
        std::vector<ChunkBlock*>::const_reverse_iterator;

    explicit Children(ChildrenT* children) : children_ptr_(children) {}

    // Returns true if the block doesn't contain any child blocks.
    bool empty() const { return children_ptr_->empty(); }

    // Returns the number of child blocks this block contains.
    std::size_t size() const { return children_ptr_->size(); }

    iterator begin() { return children_ptr_->begin(); }
    iterator end() { return children_ptr_->end(); }

    reverse_iterator rbegin() { return children_ptr_->rbegin(); }
    reverse_iterator rend() { return children_ptr_->rend(); }

    ChunkBlock* front() const { return children_ptr_->front(); }

   private:
    ChildrenT* children_ptr_;
  };

  Children children() { return Children(&children_); }

  Children children() const { return Children(&children_); }

  // Creates a new chunk block and adds all children between <start> and <end>
  // to it as direct children, and then adds the new block as a child of this.
  //
  // Effectively, this increases the indentation of all chunks between <start>
  // and <end> by 1.
  ChunkBlock* GroupAndIndentChildrenUnderNewBlock(
      const Children::reverse_iterator& start,
      const Children::reverse_iterator& end);

  // Returns a string representation of the chunk block and its children that
  // can be used for debugging. <indent> tells how much padding should be added
  // to the string.
  //
  // Warning: the string might be multiline. The function prints entire tree of
  //          blocks under the current one.
  //
  // Format: <block_level>: "<chunk_content>"  #<index>  - for leaf blocks;
  //         <block_level>: [(<child_count>)
  //           <child_blocks>                            - for the rest.
  //         ]
  //  where:
  //    <block_level>   - current block's level;
  //    <chunk_content> - chunk content as printed by `Chunk::DebugString`;
  //    <child_count>   - number of direct children;
  //    <index>         - position of the current chunk in the query and the
  //                      matching chunk if any. E.g.: #1->#3=")"
  //    <child_blocks>  - children blocks printed recursively.
  std::string DebugString(int indent = 0) const;

  // Prints a <block> into a stream (especially in googletest).
  friend std::ostream& operator<<(std::ostream& os, const ChunkBlock* block);

 private:
  // Creates a root chunk block. Level is -1 because the root is before anything
  // else.
  explicit ChunkBlock(ChunkBlockFactory* block_factory);

  // Creates an intermediate (not top or leaf) chunk block with the given
  // <parent>.
  explicit ChunkBlock(ChunkBlockFactory* block_factory, ChunkBlock* parent);

  // Creates a leaf chunk block with the given <parent> and <chunk>.
  // Leaf blocks always have the same level as their parent.
  ChunkBlock(ChunkBlockFactory* block_factory, ChunkBlock* parent,
             class Chunk* chunk);

  // Adds a new chunk block that is a direct child of this chunk block, and then
  // adds a new grandchild block that is a leaf block for <chunk> right under
  // the newly created block.
  ChunkBlock* AddChildBlockWithGrandchildChunk(class Chunk* chunk);

  // Inserts a child leaf block for <chunk> right before the given <block>.
  //
  // this block
  // |
  // +--...
  // |
  // +--new leaf block for <chunk>
  // |
  // +--<block>
  ChunkBlock* AddChunkBefore(class Chunk* chunk, ChunkBlock* block);

  // Inserts a new child block with grandchild leaf block for <chunk> right
  // before the given <block>.
  //
  // this block
  // |
  // +--...
  // |
  // +--new block
  // |  |
  // |  +--new leaf block for <chunk>
  // |
  // +--<block>
  ChunkBlock* AddIndentedChunkBefore(class Chunk* chunk, ChunkBlock* block);

  ChunkBlockFactory* const block_factory_;
  ChunkBlock* parent_;
  class Chunk* chunk_;

  int level_ = -1;
  int extra_offset_ = 0;
  bool is_list_ = false;
  bool break_close_to_line_length_ = false;

  mutable ChildrenT children_;

  friend class ChunkBlockFactory;
};

// Groups the given <tokens> into Chunks.
absl::StatusOr<std::vector<Chunk>> ChunksFromTokens(
    const TokensView& tokens,
    const ParseLocationTranslator& location_translator,
    const FormatterOptions& formatter_options = FormatterOptions());

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_H_
