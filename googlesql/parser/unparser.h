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

#ifndef GOOGLESQL_PARSER_UNPARSER_H_
#define GOOGLESQL_PARSER_UNPARSER_H_

#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace parser {

class Formatter {
 public:
  // Use it as a scoped object. It will indent and dedent automatically in the
  // scope.
  // Example usage:
  // {
  //   Formatter::Indenter indenter(&formatter);
  //   formatter.Format(...);
  // }
  class Indenter {
   public:
    explicit Indenter(Formatter* formatter) : formatter_(formatter) {
      formatter_->Indent();
    }

    Indenter(const Indenter&) = delete;
    Indenter& operator=(const Indenter&) = delete;

    ~Indenter() { formatter_->Dedent(); }

   private:
    Formatter* formatter_;
  };

  // This prints a newline and the pipe symbol, and then indents.
  // It's used as a scoped object inside each pipe operator unparse method.
  class PipeAndIndent {
   public:
    explicit PipeAndIndent(Formatter* formatter);
    ~PipeAndIndent();

    PipeAndIndent(const PipeAndIndent&) = delete;
    PipeAndIndent& operator=(const PipeAndIndent&) = delete;

   private:
    Formatter* formatter_;
  };

  explicit Formatter(std::string* unparsed) : unparsed_(unparsed) {}
  Formatter(const Formatter&) = delete;
  Formatter& operator=(const Formatter&) = delete;

  // Formats the string automatically according to the context.
  // 1. Inserts necessary space between tokens.
  // 2. Calls FlushLine() when a line (excluding indentation) reachs column
  //    limit and it is at some point appropriate to break.
  // Param string should not contain any leading or trailing whitespace,
  // like ' ', '\n'.
  void Format(absl::string_view s);

  // Like Format, except always calls FlushLine().
  // Use this if you explicitly wants to break the line after this string.
  // For example,
  // 1. To put a newline after SELECT:
  //    FormatLine("SELECT");
  // 2. To put close paren on a separate line:
  //    FormatLine("");
  //    FormatLine(")");
  void FormatLine(absl::string_view s);

  // Adds a unary operator to the output. This prevents a space between it and
  // the next format call if it's a single character unary like '+' or '-' with
  // the exception that when two unary '-' appear in a row, we add a space to
  // avoid it becoming '--' which marks a comment.
  void AddUnary(absl::string_view s);

  // Flushes buffer_ to unparsed_, with a line break at the end.
  // It will do nothing if it's a new line and buffer_ is empty, to avoid empty
  // lines.
  // Remember to call FlushLine() once after the whole process is over in case
  // some content remains in buffer_.
  void FlushLine();

  // Set a flag so that if the next character is a newline, it'll be skipped.
  void SetSuppressNextNewline() { suppress_next_newline_ = true; }

 private:
  // Checks if last token in buffer_ is a separator, where it is appropriate to
  // insert a line break or a space before open paren.
  bool LastTokenIsSeparator();

  static const int kNumColumnLimit = 100;
  static const int kDefaultNumIndentSpaces = 2;

  void Indent(int spaces = kDefaultNumIndentSpaces);
  void Dedent(int spaces = kDefaultNumIndentSpaces);

  // Indentation that will be prepended to a new line.
  std::string indentation_;

  // Appended to unparsed_ with a line break at the end in FlushLine().
  std::string buffer_;

  // If the last call to the formatter was AddUnary with a single character.
  bool last_was_single_char_unary_ = false;

  // If true and the next character is a newline, skip it.
  bool suppress_next_newline_ = false;

  // The length of indentation at the beginning of buffer_. We have to save it
  // in a variable since indentation_ is dynamically changing.
  size_t indentation_length_in_buffer_;

  // Unparsed result, not owned.
  std::string* unparsed_;
};

class Unparser : public ParseTreeVisitor {
 public:
  explicit Unparser(std::string* unparsed) : formatter_(unparsed) {}
  Unparser(const Unparser&) = delete;
  Unparser& operator=(const Unparser&) = delete;
  ~Unparser() override = default;

  virtual void defaultVisit(const ASTNode* node, void* data) {
    ABSL_LOG(FATAL) << "Unimplemented node: " << node->SingleNodeDebugString();
  }

  void visitASTChildren(const ASTNode* node, void* data) {
    if (!ThreadHasEnoughStack()) {
      println("<Complex nested expression truncated>");
      return;
    }
    node->ChildrenAccept(this, data);
  }

  void visit(const ASTNode* node, void* data) override {
    visitASTChildren(node, data);
  }

  // Shorthand for calling methods in formatter_.
  void print(absl::string_view s) { formatter_.Format(s); }

  void println(absl::string_view s = "") { formatter_.FormatLine(s); }

  void FlushLine() { formatter_.FlushLine(); }

  // Visitor implementation.
  void visitASTHintedStatement(const ASTHintedStatement* node,
                               void* data) override;
  void visitASTStatementWithPipeOperators(
      const ASTStatementWithPipeOperators* node, void* data) override;
  void visitASTExplainStatement(const ASTExplainStatement* node,
                                void* data) override;
  void visitASTQueryStatement(const ASTQueryStatement* node,
                              void* data) override;
  void visitASTSubpipelineStatement(const ASTSubpipelineStatement* node,
                                    void* data) override;
  void visitASTTableClause(const ASTTableClause* node, void* data) override;
  void visitASTModelClause(const ASTModelClause* node, void* data) override;
  void visitASTConnectionClause(const ASTConnectionClause* node,
                                void* data) override;
  void visitASTTVF(const ASTTVF* node, void* data) override;
  void visitASTTVFArgument(const ASTTVFArgument* node, void* data) override;
  void visitASTTVFSchema(const ASTTVFSchema* node, void* data) override;
  void visitASTTVFSchemaColumn(const ASTTVFSchemaColumn* node,
                               void* data) override;
  void visitASTCreateConnectionStatement(
      const ASTCreateConnectionStatement* node, void* data) override;
  void visitASTAlterConnectionStatement(const ASTAlterConnectionStatement* node,
                                        void* data) override;
  void visitASTCreateConstantStatement(const ASTCreateConstantStatement* node,
                                       void* data) override;
  void visitASTCreateDatabaseStatement(const ASTCreateDatabaseStatement* node,
                                       void* data) override;
  void visitASTCreateFunctionStatement(const ASTCreateFunctionStatement* node,
                                       void* data) override;
  void visitASTCreateIndexStatement(const ASTCreateIndexStatement* node,
                                    void* data) override;
  void visitASTCreateModelStatement(const ASTCreateModelStatement* node,
                                    void* data) override;
  void visitASTCreateSchemaStatement(const ASTCreateSchemaStatement* node,
                                     void* data) override;
  void visitASTCreateExternalSchemaStatement(
      const ASTCreateExternalSchemaStatement* node, void* data) override;
  void visitASTSequenceArg(const ASTSequenceArg* node, void* data) override;
  void visitASTCreateTableStatement(const ASTCreateTableStatement* node,
                                    void* data) override;
  void visitASTCreateSnapshotStatement(const ASTCreateSnapshotStatement* node,
                                       void* data) override;
  void visitASTCreateSnapshotTableStatement(
      const ASTCreateSnapshotTableStatement* node, void* data) override;
  void visitASTCreateEntityStatement(const ASTCreateEntityStatement* node,
                                     void* data) override;
  void visitASTAlterEntityStatement(const ASTAlterEntityStatement* node,
                                    void* data) override;
  void visitASTCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* node, void* data) override;
  void visitASTCreateViewStatement(const ASTCreateViewStatement* node,
                                   void* data) override;
  void visitASTCreateApproxViewStatement(
      const ASTCreateApproxViewStatement* node, void* data) override;
  void visitASTCreateMaterializedViewStatement(
      const ASTCreateMaterializedViewStatement* node, void* data) override;
  void visitASTColumnWithOptions(const ASTColumnWithOptions* node,
                                 void* data) override;
  void visitASTColumnWithOptionsList(const ASTColumnWithOptionsList* node,
                                     void* data) override;
  void visitASTWithPartitionColumnsClause(
      const ASTWithPartitionColumnsClause* node, void* data) override;
  void visitASTCreateExternalTableStatement(
      const ASTCreateExternalTableStatement* node, void* data) override;
  void visitASTCreatePrivilegeRestrictionStatement(
      const ASTCreatePrivilegeRestrictionStatement* node, void* data) override;
  void visitASTCreateRowAccessPolicyStatement(
      const ASTCreateRowAccessPolicyStatement* node, void* data) override;
  void visitASTUndropStatement(const ASTUndropStatement* node,
                               void* data) override;
  void visitASTExportDataStatement(const ASTExportDataStatement* node,
                                   void* data) override;
  void visitASTExportModelStatement(const ASTExportModelStatement* node,
                                    void* data) override;
  void visitASTExportMetadataStatement(const ASTExportMetadataStatement* node,
                                       void* data) override;
  void visitASTCallStatement(const ASTCallStatement* node, void* data) override;
  void visitASTDefineTableStatement(const ASTDefineTableStatement* node,
                                    void* data) override;
  void visitASTCreateLocalityGroupStatement(
      const ASTCreateLocalityGroupStatement* node, void* data) override;
  void visitASTDescribeStatement(const ASTDescribeStatement* node,
                                 void* data) override;
  void visitASTShowStatement(const ASTShowStatement* node, void* data) override;
  void visitASTBeginStatement(const ASTBeginStatement* node,
                              void* data) override;
  void visitASTTransactionIsolationLevel(
      const ASTTransactionIsolationLevel* node, void* data) override;
  void visitASTTransactionReadWriteMode(const ASTTransactionReadWriteMode* node,
                                        void* data) override;
  void visitASTTransactionModeList(const ASTTransactionModeList* node,
                                   void* data) override;
  void visitASTSetTransactionStatement(const ASTSetTransactionStatement* node,
                                       void* data) override;

  void visitASTCommitStatement(const ASTCommitStatement* node,
                               void* data) override;
  void visitASTRollbackStatement(const ASTRollbackStatement* node,
                                 void* data) override;
  void visitASTStartBatchStatement(const ASTStartBatchStatement* node,
                                   void* data) override;
  void visitASTRunBatchStatement(const ASTRunBatchStatement* node,
                                 void* data) override;
  void visitASTAbortBatchStatement(const ASTAbortBatchStatement* node,
                                   void* data) override;
  void visitASTAlterColumnOptionsAction(const ASTAlterColumnOptionsAction* node,
                                        void* data) override;
  void visitASTAlterColumnDropNotNullAction(
      const ASTAlterColumnDropNotNullAction* node, void* data) override;

  void visitASTAlterColumnTypeAction(const ASTAlterColumnTypeAction* node,
                                     void* data) override;
  void visitASTAlterColumnSetDefaultAction(
      const ASTAlterColumnSetDefaultAction* node, void* data) override;
  void visitASTAlterColumnDropDefaultAction(
      const ASTAlterColumnDropDefaultAction* node, void* data) override;
  void visitASTAlterColumnDropGeneratedAction(
      const ASTAlterColumnDropGeneratedAction* node, void* data) override;
  void visitASTAlterColumnSetGeneratedAction(
      const ASTAlterColumnSetGeneratedAction* node, void* data) override;
  void visitASTDropColumnAction(const ASTDropColumnAction* node,
                                void* data) override;
  void visitASTRenameColumnAction(const ASTRenameColumnAction* node,
                                  void* data) override;
  void visitASTDropStatement(const ASTDropStatement* node, void* data) override;
  void visitASTDropEntityStatement(const ASTDropEntityStatement* node,
                                   void* data) override;
  void visitASTDropFunctionStatement(const ASTDropFunctionStatement* node,
                                     void* data) override;
  void visitASTDropTableFunctionStatement(
      const ASTDropTableFunctionStatement* node, void* data) override;
  void visitASTDropPrivilegeRestrictionStatement(
      const ASTDropPrivilegeRestrictionStatement* node, void* data) override;
  void visitASTDropRowAccessPolicyStatement(
      const ASTDropRowAccessPolicyStatement* node, void* data) override;
  void visitASTDropAllRowAccessPoliciesStatement(
      const ASTDropAllRowAccessPoliciesStatement* node, void* data) override;
  void visitASTDropMaterializedViewStatement(
      const ASTDropMaterializedViewStatement* node, void* data) override;
  void visitASTDropSnapshotTableStatement(
      const ASTDropSnapshotTableStatement* node, void* data) override;
  void visitASTDropSearchIndexStatement(const ASTDropSearchIndexStatement* node,
                                        void* data) override;
  void visitASTDropVectorIndexStatement(const ASTDropVectorIndexStatement* node,
                                        void* data) override;
  void visitASTRenameStatement(const ASTRenameStatement* node,
                               void* data) override;
  void visitASTImportStatement(const ASTImportStatement* node,
                               void* data) override;
  void visitASTModuleStatement(const ASTModuleStatement* node,
                               void* data) override;
  void visitASTWithClause(const ASTWithClause* node, void* data) override;
  void visitASTWithClauseEntry(const ASTWithClauseEntry* node,
                               void* data) override;
  void visitASTAliasedGroupRows(const ASTAliasedGroupRows* node,
                                void* data) override;
  void visitASTQuery(const ASTQuery* node, void* data) override;
  void visitASTAliasedQueryExpression(const ASTAliasedQueryExpression* node,
                                      void* data) override;
  void visitASTFromQuery(const ASTFromQuery* node, void* data) override;
  void visitASTSubpipeline(const ASTSubpipeline* node, void* data) override;
  void visitASTPipeWhere(const ASTPipeWhere* node, void* data) override;
  void visitASTPipeSelect(const ASTPipeSelect* node, void* data) override;
  void visitASTPipeLimitOffset(const ASTPipeLimitOffset* node,
                               void* data) override;
  void visitASTPipeOrderBy(const ASTPipeOrderBy* node, void* data) override;
  void visitASTPipeExtend(const ASTPipeExtend* node, void* data) override;
  void visitASTPipeRenameItem(const ASTPipeRenameItem* node,
                              void* data) override;
  void visitASTPipeRename(const ASTPipeRename* node, void* data) override;
  void visitASTPipeAggregate(const ASTPipeAggregate* node, void* data) override;
  void visitASTPipeSetOperation(const ASTPipeSetOperation* node,
                                void* data) override;
  void visitASTPipeJoin(const ASTPipeJoin* node, void* data) override;
  void visitASTPipeJoinLhsPlaceholder(const ASTPipeJoinLhsPlaceholder* node,
                                      void* data) override;
  void visitASTPipeCall(const ASTPipeCall* node, void* data) override;
  void visitASTPipeWindow(const ASTPipeWindow* node, void* data) override;
  void visitASTPipeDistinct(const ASTPipeDistinct* node, void* data) override;
  void visitASTPipeTablesample(const ASTPipeTablesample* node,
                               void* data) override;
  void visitASTPipeMatchRecognize(const ASTPipeMatchRecognize* node,
                                  void* data) override;
  void visitASTPipeAs(const ASTPipeAs* node, void* data) override;
  void visitASTPipeDescribe(const ASTPipeDescribe* node, void* data) override;
  void visitASTPipeStaticDescribe(const ASTPipeStaticDescribe* node,
                                  void* data) override;
  void visitASTPipeAssert(const ASTPipeAssert* node, void* data) override;
  void visitASTPipeLog(const ASTPipeLog* node, void* data) override;
  void visitASTPipeDrop(const ASTPipeDrop* node, void* data) override;
  void visitASTPipeSetItem(const ASTPipeSetItem* node, void* data) override;
  void visitASTPipeSet(const ASTPipeSet* node, void* data) override;
  void visitASTPipePivot(const ASTPipePivot* node, void* data) override;
  void visitASTPipeUnpivot(const ASTPipeUnpivot* node, void* data) override;
  void visitASTPipeIf(const ASTPipeIf* node, void* data) override;
  void visitASTPipeIfCase(const ASTPipeIfCase* node, void* data) override;
  void visitASTPipeFork(const ASTPipeFork* node, void* data) override;
  void visitASTPipeTee(const ASTPipeTee* node, void* data) override;
  void visitASTPipeWith(const ASTPipeWith* node, void* data) override;
  void visitASTPipeExportData(const ASTPipeExportData* node,
                              void* data) override;
  void visitASTPipeCreateTable(const ASTPipeCreateTable* node,
                               void* data) override;
  void visitASTPipeInsert(const ASTPipeInsert* node, void* data) override;
  void visitASTMatchRecognizeClause(const ASTMatchRecognizeClause* node,
                                    void* data) override;
  void visitASTAfterMatchSkipClause(const ASTAfterMatchSkipClause* node,
                                    void* data) override;
  void visitASTRowPatternVariable(const ASTRowPatternVariable* node,
                                  void* data) override;
  void visitASTRowPatternOperation(const ASTRowPatternOperation* node,
                                   void* data) override;
  void visitASTEmptyRowPattern(const ASTEmptyRowPattern* node,
                               void* data) override;
  void visitASTRowPatternAnchor(const ASTRowPatternAnchor* node,
                                void* data) override;
  void visitASTRowPatternQuantification(const ASTRowPatternQuantification* node,
                                        void* data) override;
  void visitASTSymbolQuantifier(const ASTSymbolQuantifier* node,
                                void* data) override;
  void visitASTBoundedQuantifier(const ASTBoundedQuantifier* node,
                                 void* data) override;
  void visitASTFixedQuantifier(const ASTFixedQuantifier* node,
                               void* data) override;
  void visitASTQuantifierBound(const ASTQuantifierBound* node,
                               void* data) override;
  void visitASTSetOperation(const ASTSetOperation* node, void* data) override;
  void visitASTSelect(const ASTSelect* node, void* data) override;
  void visitASTSelectAs(const ASTSelectAs* node, void* data) override;
  void visitASTSelectList(const ASTSelectList* node, void* data) override;
  void visitASTWithModifier(const ASTWithModifier* node, void* data) override;
  void visitASTSelectColumn(const ASTSelectColumn* node, void* data) override;
  void visitASTAlias(const ASTAlias* node, void* data) override;
  void visitASTAliasedQuery(const ASTAliasedQuery* node, void* data) override;
  void visitASTAliasedQueryList(const ASTAliasedQueryList* node,
                                void* data) override;
  void visitASTAliasedQueryModifiers(const ASTAliasedQueryModifiers* node,
                                     void* data) override;
  void visitASTRecursionDepthModifier(const ASTRecursionDepthModifier* node,
                                      void* data) override;
  void visitASTIntOrUnbounded(const ASTIntOrUnbounded* node,
                              void* data) override;
  void visitASTIntoAlias(const ASTIntoAlias* node, void* data) override;
  void visitASTFromClause(const ASTFromClause* node, void* data) override;
  void visitASTTransformClause(const ASTTransformClause* node,
                               void* data) override;
  void visitASTWithOffset(const ASTWithOffset* node, void* data) override;
  void visitASTUnnestExpression(const ASTUnnestExpression* node,
                                void* data) override;
  void visitASTUnnestExpressionWithOptAliasAndOffset(
      const ASTUnnestExpressionWithOptAliasAndOffset* node,
      void* data) override;
  void visitASTTableElementList(const ASTTableElementList* node,
                                void* data) override;
  void visitASTTablePathExpression(const ASTTablePathExpression* node,
                                   void* data) override;
  void visitASTForSystemTime(const ASTForSystemTime* node, void* data) override;
  void visitASTTableSubquery(const ASTTableSubquery* node, void* data) override;
  void visitASTJoin(const ASTJoin* node, void* data) override;
  void visitASTParenthesizedJoin(const ASTParenthesizedJoin* node,
                                 void* data) override;
  void visitASTOnClause(const ASTOnClause* node, void* data) override;
  void visitASTOnOrUsingClauseList(const ASTOnOrUsingClauseList* node,
                                   void* data) override;
  void visitASTUsingClause(const ASTUsingClause* node, void* data) override;
  void visitASTWhereClause(const ASTWhereClause* node, void* data) override;
  void visitASTRollup(const ASTRollup* node, void* data) override;
  void visitASTCube(const ASTCube* node, void* data) override;
  void visitASTGeneratedColumnInfo(const ASTGeneratedColumnInfo* node,
                                   void* data) override;
  void visitASTIdentityColumnInfo(const ASTIdentityColumnInfo* node,
                                  void* data) override;
  void visitASTIdentityColumnStartWith(const ASTIdentityColumnStartWith* node,
                                       void* data) override;
  void visitASTIdentityColumnIncrementBy(
      const ASTIdentityColumnIncrementBy* node, void* data) override;
  void visitASTIdentityColumnMaxValue(const ASTIdentityColumnMaxValue* node,
                                      void* data) override;
  void visitASTIdentityColumnMinValue(const ASTIdentityColumnMinValue* node,
                                      void* data) override;
  void visitASTGqlMatch(const ASTGqlMatch* node, void* data) override;
  void visitASTGqlQuery(const ASTGqlQuery* node, void* data) override;
  void visitASTGqlGraphPatternQuery(const ASTGqlGraphPatternQuery* node,
                                    void* data) override;
  void visitASTGqlLinearOpsQuery(const ASTGqlLinearOpsQuery* node,
                                 void* data) override;
  void visitASTGqlReturn(const ASTGqlReturn* node, void* data) override;
  void visitASTGqlWith(const ASTGqlWith* node, void* data) override;
  void visitASTGqlFor(const ASTGqlFor* node, void* data) override;
  void visitASTGqlInlineSubqueryCall(const ASTGqlInlineSubqueryCall* node,
                                     void* data) override;
  void visitASTGqlNamedCall(const ASTGqlNamedCall* node, void* data) override;
  void visitASTYieldItemList(const ASTYieldItemList* node, void* data) override;
  void visitASTGqlLet(const ASTGqlLet* node, void* data) override;
  void visitASTGqlLetVariableDefinitionList(
      const ASTGqlLetVariableDefinitionList* node, void* data) override;
  void visitASTGqlLetVariableDefinition(const ASTGqlLetVariableDefinition* node,
                                        void* data) override;
  void visitASTGqlFilter(const ASTGqlFilter* node, void* data) override;
  void visitASTGqlOperatorList(const ASTGqlOperatorList* node,
                               void* data) override;
  void visitASTGqlPageLimit(const ASTGqlPageLimit* node, void* data) override;
  void visitASTGqlPageOffset(const ASTGqlPageOffset* node, void* data) override;
  void visitASTGqlPage(const ASTGqlPage* node, void* data) override;
  void visitASTGqlOrderByAndPage(const ASTGqlOrderByAndPage* node,
                                 void* data) override;
  void visitASTGqlSetOperation(const ASTGqlSetOperation* node,
                               void* data) override;
  void visitASTGqlSample(const ASTGqlSample* node, void* data) override;
  void visitASTCreatePropertyGraphStatement(
      const ASTCreatePropertyGraphStatement* node, void* data) override;
  void visitASTGraphElementTableList(const ASTGraphElementTableList* node,
                                     void* data) override;
  void visitASTGraphElementTable(const ASTGraphElementTable* node,
                                 void* data) override;
  void visitASTGraphDerivedPropertyList(const ASTGraphDerivedPropertyList* node,
                                        void* data) override;
  void visitASTGraphDerivedProperty(const ASTGraphDerivedProperty* node,
                                    void* data) override;
  void visitASTGraphNodeTableReference(const ASTGraphNodeTableReference* node,
                                       void* data) override;
  void visitASTGraphElementLabelAndPropertiesList(
      const ASTGraphElementLabelAndPropertiesList* node, void* data) override;
  void visitASTGraphElementLabelAndProperties(
      const ASTGraphElementLabelAndProperties* node, void* data) override;
  void visitASTGraphDynamicLabel(const ASTGraphDynamicLabel* node,
                                 void* data) override;
  void visitASTGraphDynamicProperties(const ASTGraphDynamicProperties* node,
                                      void* data) override;
  void visitASTGraphProperties(const ASTGraphProperties* node,
                               void* data) override;
  void visitASTGraphTableQuery(const ASTGraphTableQuery* node,
                               void* data) override;
  void visitASTGraphElementPatternFiller(
      const ASTGraphElementPatternFiller* node, void* data) override;
  void visitASTGraphPathMode(const ASTGraphPathMode* node, void* data) override;
  void visitASTGraphEdgePattern(const ASTGraphEdgePattern* node,
                                void* data) override;
  void visitASTGraphNodePattern(const ASTGraphNodePattern* node,
                                void* data) override;
  void visitASTGraphLhsHint(const ASTGraphLhsHint* node, void* data) override;
  void visitASTGraphRhsHint(const ASTGraphRhsHint* node, void* data) override;
  void visitASTGraphLabelFilter(const ASTGraphLabelFilter* node,
                                void* data) override;
  void visitASTGraphElementLabel(const ASTGraphElementLabel* node,
                                 void* data) override;
  void visitASTGraphWildcardLabel(const ASTGraphWildcardLabel* node,
                                  void* data) override;
  void visitASTGraphLabelOperation(const ASTGraphLabelOperation* node,
                                   void* data) override;
  void visitASTGraphPathPattern(const ASTGraphPathPattern* node,
                                void* data) override;
  void visitASTGraphPathSearchPrefix(const ASTGraphPathSearchPrefix* node,
                                     void* data) override;
  void visitASTGraphPathSearchPrefixCount(
      const ASTGraphPathSearchPrefixCount* node, void* data) override;
  void visitASTGraphPattern(const ASTGraphPattern* node, void* data) override;
  void visitASTGraphPropertySpecification(
      const ASTGraphPropertySpecification* node, void* data) override;
  void visitASTGraphPropertyNameAndValue(
      const ASTGraphPropertyNameAndValue* node, void* data) override;
  void visitASTGraphIsLabeledPredicate(const ASTGraphIsLabeledPredicate* node,
                                       void* data) override;
  void visitASTGroupingItemOrder(const ASTGroupingItemOrder* node,
                                 void* data) override;
  void visitASTGroupingItem(const ASTGroupingItem* node, void* data) override;
  void visitASTGroupingSet(const ASTGroupingSet* node, void* data) override;
  void visitASTGroupingSetList(const ASTGroupingSetList* node,
                               void* data) override;
  void visitASTGroupBy(const ASTGroupBy* node, void* data) override;
  void visitASTGroupByAll(const ASTGroupByAll* node, void* data) override;
  void visitASTHaving(const ASTHaving* node, void* data) override;
  void visitASTQualify(const ASTQualify* node, void* data) override;
  void visitASTCollate(const ASTCollate* node, void* data) override;
  void visitASTNullOrder(const ASTNullOrder* node, void* data) override;
  void visitASTColumnPosition(const ASTColumnPosition* node,
                              void* data) override;
  void visitASTOrderBy(const ASTOrderBy* node, void* data) override;
  void visitASTLambda(const ASTLambda* node, void* data) override;
  void visitASTLimitOffset(const ASTLimitOffset* node, void* data) override;
  void visitASTLimit(const ASTLimit* node, void* data) override;
  void visitASTLimitAll(const ASTLimitAll* node, void* data) override;
  void visitASTHavingModifier(const ASTHavingModifier* node,
                              void* data) override;
  void visitASTClampedBetweenModifier(const ASTClampedBetweenModifier* node,
                                      void* data) override;
  void visitASTWithReportModifier(const ASTWithReportModifier* node,
                                  void* data) override;
  void visitASTOrderingExpression(const ASTOrderingExpression* node,
                                  void* data) override;
  void visitASTIdentifier(const ASTIdentifier* node, void* data) override;
  void visitASTNewConstructorArg(const ASTNewConstructorArg* node,
                                 void* data) override;
  void visitASTNewConstructor(const ASTNewConstructor* node,
                              void* data) override;
  void visitASTBracedConstructorLhs(const ASTBracedConstructorLhs* node,
                                    void* data) override;
  void visitASTBracedConstructorFieldValue(
      const ASTBracedConstructorFieldValue* node, void* data) override;
  void visitASTBracedConstructorField(const ASTBracedConstructorField* node,
                                      void* data) override;
  void visitASTBracedConstructor(const ASTBracedConstructor* node,
                                 void* data) override;
  void visitASTBracedNewConstructor(const ASTBracedNewConstructor* node,
                                    void* data) override;
  void visitASTStructBracedConstructor(const ASTStructBracedConstructor* node,
                                       void* data) override;
  void visitASTExtendedPathExpression(const ASTExtendedPathExpression* node,
                                      void* data) override;
  void visitASTUpdateConstructor(const ASTUpdateConstructor* node,
                                 void* data) override;

  void visitASTInferredTypeColumnSchema(const ASTInferredTypeColumnSchema* node,
                                        void* data) override;
  void visitASTArrayConstructor(const ASTArrayConstructor* node,
                                void* data) override;
  void visitASTStructConstructorArg(const ASTStructConstructorArg* node,
                                    void* data) override;
  void visitASTStructConstructorWithParens(
      const ASTStructConstructorWithParens* node, void* data) override;
  void visitASTStructConstructorWithKeyword(
      const ASTStructConstructorWithKeyword* node, void* data) override;
  void visitASTIntLiteral(const ASTIntLiteral* node, void* data) override;
  void visitASTNumericLiteral(const ASTNumericLiteral* node,
                              void* data) override;
  void visitASTAuxLoadDataFromFilesOptionsList(
      const ASTAuxLoadDataFromFilesOptionsList* node, void* data) override;
  void visitASTAuxLoadDataPartitionsClause(
      const ASTAuxLoadDataPartitionsClause* node, void* data) override;
  void visitASTAuxLoadDataStatement(const ASTAuxLoadDataStatement* node,
                                    void* data) override;
  void visitASTBigNumericLiteral(const ASTBigNumericLiteral* node,
                                 void* data) override;
  void visitASTJSONLiteral(const ASTJSONLiteral* node, void* data) override;
  void visitASTFloatLiteral(const ASTFloatLiteral* node, void* data) override;
  void visitASTStringLiteral(const ASTStringLiteral* node, void* data) override;
  void visitASTStringLiteralComponent(const ASTStringLiteralComponent* node,
                                      void* data) override;
  void visitASTBytesLiteral(const ASTBytesLiteral* node, void* data) override;
  void visitASTBytesLiteralComponent(const ASTBytesLiteralComponent* node,
                                     void* data) override;
  void visitASTBooleanLiteral(const ASTBooleanLiteral* node,
                              void* data) override;
  void visitASTNullLiteral(const ASTNullLiteral* node, void* data) override;
  void visitASTDateOrTimeLiteral(const ASTDateOrTimeLiteral* node,
                                 void* data) override;
  void visitASTRangeColumnSchema(const ASTRangeColumnSchema* node,
                                 void* data) override;
  void visitASTMapColumnSchema(const ASTMapColumnSchema* node,
                               void* data) override;
  void visitASTRangeLiteral(const ASTRangeLiteral* node, void* data) override;
  void visitASTRangeType(const ASTRangeType* node, void* data) override;
  void visitASTStar(const ASTStar* node, void* data) override;
  void visitASTStarExceptList(const ASTStarExceptList* node,
                              void* data) override;
  void visitASTStarReplaceItem(const ASTStarReplaceItem* node,
                               void* data) override;
  void visitASTStarModifiers(const ASTStarModifiers* node, void* data) override;
  void visitASTStarWithModifiers(const ASTStarWithModifiers* node,
                                 void* data) override;
  void visitASTPathExpression(const ASTPathExpression* node,
                              void* data) override;
  void visitASTPathExpressionList(const ASTPathExpressionList* node,
                                  void* data) override;
  void visitASTParameterExpr(const ASTParameterExpr* node, void* data) override;
  void visitASTSystemVariableExpr(const ASTSystemVariableExpr* node,
                                  void* data) override;
  void visitASTIntervalExpr(const ASTIntervalExpr* node, void* data) override;
  void visitASTDotIdentifier(const ASTDotIdentifier* node, void* data) override;
  void visitASTDotGeneralizedField(const ASTDotGeneralizedField* node,
                                   void* data) override;
  void visitASTDotStar(const ASTDotStar* node, void* data) override;
  void visitASTDotStarWithModifiers(const ASTDotStarWithModifiers* node,
                                    void* data) override;
  void visitASTOrExpr(const ASTOrExpr* node, void* data) override;
  void visitASTAndExpr(const ASTAndExpr* node, void* data) override;
  void visitASTConcatExpr(const ASTConcatExpr* node, void* data) override;
  void visitASTUnaryExpression(const ASTUnaryExpression* node,
                               void* data) override;
  void visitASTCaseNoValueExpression(const ASTCaseNoValueExpression* node,
                                     void* data) override;
  void visitASTCaseValueExpression(const ASTCaseValueExpression* node,
                                   void* data) override;
  void visitASTFormatClause(const ASTFormatClause* node, void* data) override;
  void visitASTCastExpression(const ASTCastExpression* node,
                              void* data) override;
  void visitASTExtractExpression(const ASTExtractExpression* node,
                                 void* data) override;
  void visitASTBinaryExpression(const ASTBinaryExpression* node,
                                void* data) override;
  void visitASTBitwiseShiftExpression(const ASTBitwiseShiftExpression* node,
                                      void* data) override;
  void visitASTInExpression(const ASTInExpression* node, void* data) override;
  void visitASTInList(const ASTInList* node, void* data) override;
  void visitASTLikeExpression(const ASTLikeExpression* node,
                              void* data) override;
  void visitASTAnySomeAllOp(const ASTAnySomeAllOp* node, void* data) override;
  void visitASTQuantifiedComparisonExpression(
      const ASTQuantifiedComparisonExpression* node, void* data) override;
  void visitASTIndexAllColumns(const ASTIndexAllColumns* node,
                               void* data) override;
  void visitASTIndexItemList(const ASTIndexItemList* node, void* data) override;
  void visitASTIndexStoringExpressionList(
      const ASTIndexStoringExpressionList* node, void* data) override;
  void visitASTIndexUnnestExpressionList(
      const ASTIndexUnnestExpressionList* node, void* data) override;
  void visitASTBetweenExpression(const ASTBetweenExpression* node,
                                 void* data) override;
  void visitASTExpressionWithAlias(const ASTExpressionWithAlias* node,
                                   void* data) override;
  void visitASTFunctionCall(const ASTFunctionCall* node, void* data) override;
  void visitASTChainedBaseExpr(const ASTChainedBaseExpr* node,
                               void* data) override;
  void visitASTArrayElement(const ASTArrayElement* node, void* data) override;
  void visitASTExpressionSubquery(const ASTExpressionSubquery* node,
                                  void* data) override;
  void visitASTTemplatedParameterType(const ASTTemplatedParameterType* node,
                                      void* data) override;
  void visitASTFunctionParameter(const ASTFunctionParameter* node,
                                 void* data) override;
  void visitASTFunctionParameters(const ASTFunctionParameters* node,
                                  void* data) override;
  void visitASTFunctionDeclaration(const ASTFunctionDeclaration* node,
                                   void* data) override;
  void visitASTSqlFunctionBody(const ASTSqlFunctionBody* node,
                               void* data) override;
  void visitASTHint(const ASTHint* node, void* data) override;
  void visitASTHintEntry(const ASTHintEntry* node, void* data) override;
  void visitASTOptionsList(const ASTOptionsList* node, void* data) override;
  void visitASTOptionsEntry(const ASTOptionsEntry* node, void* data) override;
  void visitASTMaxLiteral(const ASTMaxLiteral* node, void* data) override;
  void visitASTTypeParameterList(const ASTTypeParameterList* node,
                                 void* data) override;
  void visitASTSimpleType(const ASTSimpleType* node, void* data) override;
  void visitASTArrayType(const ASTArrayType* node, void* data) override;
  void visitASTStructType(const ASTStructType* node, void* data) override;
  void visitASTStructField(const ASTStructField* node, void* data) override;
  void visitASTFunctionType(const ASTFunctionType* node, void* data) override;

  void visitASTFunctionTypeArgList(const ASTFunctionTypeArgList* node,
                                   void* data) override;

  void visitASTMapType(const ASTMapType* node, void* data) override;

  void visitASTSimpleColumnSchema(const ASTSimpleColumnSchema* node,
                                  void* data) override;
  void visitASTArrayColumnSchema(const ASTArrayColumnSchema* node,
                                 void* data) override;
  void visitASTStructColumnSchema(const ASTStructColumnSchema* node,
                                  void* data) override;
  void visitASTStructColumnField(const ASTStructColumnField* node,
                                 void* data) override;
  void visitASTAnalyticFunctionCall(const ASTAnalyticFunctionCall* node,
                                    void* data) override;
  void visitASTWindowClause(const ASTWindowClause* node, void* data) override;
  void visitASTWindowDefinition(const ASTWindowDefinition* node,
                                void* data) override;
  void visitASTWindowSpecification(const ASTWindowSpecification* node,
                                   void* data) override;
  void visitASTPartitionBy(const ASTPartitionBy* node, void* data) override;
  void visitASTClusterBy(const ASTClusterBy* node, void* data) override;
  void visitASTCopyDataSource(const ASTCopyDataSource* node,
                              void* data) override {
    UnparseASTTableDataSource(node, data);
  }
  void visitASTCloneDataSource(const ASTCloneDataSource* node,
                               void* data) override {
    UnparseASTTableDataSource(node, data);
  }
  void visitASTCloneDataSourceList(const ASTCloneDataSourceList* node,
                                   void* data) override;
  void visitASTCloneDataStatement(const ASTCloneDataStatement* node,
                                  void* data) override;
  void visitASTWindowFrame(const ASTWindowFrame* node, void* data) override;
  void visitASTWindowFrameExpr(const ASTWindowFrameExpr* node,
                               void* data) override;

  void visitASTDefaultLiteral(const ASTDefaultLiteral* node,
                              void* data) override;
  void visitASTAnalyzeStatement(const ASTAnalyzeStatement* node,
                                void* data) override;
  void visitASTTableAndColumnInfo(const ASTTableAndColumnInfo* node,
                                  void* data) override;
  void visitASTTableAndColumnInfoList(const ASTTableAndColumnInfoList* node,
                                      void* data) override;
  void visitASTAssertRowsModified(const ASTAssertRowsModified* node,
                                  void* data) override;
  void visitASTAssertStatement(const ASTAssertStatement* node,
                               void* data) override;
  void visitASTReturningClause(const ASTReturningClause* node,
                               void* data) override;
  void visitASTDeleteStatement(const ASTDeleteStatement* node,
                               void* data) override;
  void visitASTColumnAttributeList(const ASTColumnAttributeList* node,
                                   void* data) override;
  void visitASTNotNullColumnAttribute(const ASTNotNullColumnAttribute* node,
                                      void* data) override;
  void visitASTHiddenColumnAttribute(const ASTHiddenColumnAttribute* node,
                                     void* data) override;
  void visitASTPrimaryKeyColumnAttribute(
      const ASTPrimaryKeyColumnAttribute* node, void* data) override;
  void visitASTForeignKeyColumnAttribute(
      const ASTForeignKeyColumnAttribute* node, void* data) override;
  void visitASTColumnDefinition(const ASTColumnDefinition* node,
                                void* data) override;
  void visitASTColumnList(const ASTColumnList* node, void* data) override;
  void visitASTInsertValuesRow(const ASTInsertValuesRow* node,
                               void* data) override;
  void visitASTInsertValuesRowList(const ASTInsertValuesRowList* node,
                                   void* data) override;
  void visitASTInsertStatement(const ASTInsertStatement* node,
                               void* data) override;
  void visitASTOnConflictClause(const ASTOnConflictClause* node,
                                void* data) override;
  void visitASTUpdateSetValue(const ASTUpdateSetValue* node,
                              void* data) override;
  void visitASTUpdateItem(const ASTUpdateItem* node, void* data) override;
  void visitASTUpdateItemList(const ASTUpdateItemList* node,
                              void* data) override;
  void visitASTUpdateStatement(const ASTUpdateStatement* node,
                               void* data) override;
  void visitASTTruncateStatement(const ASTTruncateStatement* node,
                                 void* data) override;
  void visitASTMergeAction(const ASTMergeAction* node, void* data) override;
  void visitASTMergeWhenClause(const ASTMergeWhenClause* node,
                               void* data) override;
  void visitASTMergeWhenClauseList(const ASTMergeWhenClauseList* node,
                                   void* data) override;
  void visitASTMergeStatement(const ASTMergeStatement* node,
                              void* data) override;

  void visitASTPrimaryKeyElement(const ASTPrimaryKeyElement* node,
                                 void* data) override;
  void visitASTPrimaryKeyElementList(const ASTPrimaryKeyElementList* node,
                                     void* data) override;
  void visitASTPrimaryKey(const ASTPrimaryKey* node, void* data) override;
  void visitASTPrivilege(const ASTPrivilege* node, void* data) override;
  void visitASTPrivileges(const ASTPrivileges* node, void* data) override;
  void visitASTGranteeList(const ASTGranteeList* node, void* data) override;
  void visitASTGrantStatement(const ASTGrantStatement* node,
                              void* data) override;
  void visitASTRevokeStatement(const ASTRevokeStatement* node,
                               void* data) override;

  void visitASTRepeatableClause(const ASTRepeatableClause* node,
                                void* data) override;
  void visitASTReplaceFieldsArg(const ASTReplaceFieldsArg* node,
                                void* data) override;
  void visitASTReplaceFieldsExpression(const ASTReplaceFieldsExpression* node,
                                       void* data) override;
  void visitASTFilterFieldsArg(const ASTFilterFieldsArg* node,
                               void* data) override;
  void visitASTSampleSize(const ASTSampleSize* node, void* data) override;
  void visitASTSampleSuffix(const ASTSampleSuffix* node, void* data) override;
  void visitASTWithWeight(const ASTWithWeight* node, void* data) override;
  void visitASTWithConnectionClause(const ASTWithConnectionClause* node,
                                    void* data) override;
  void visitASTSampleClause(const ASTSampleClause* node, void* data) override;
  void visitASTPivotExpression(const ASTPivotExpression* node,
                               void* data) override;
  void visitASTPivotExpressionList(const ASTPivotExpressionList* node,
                                   void* data) override;
  void visitASTPivotValue(const ASTPivotValue* node, void* data) override;
  void visitASTPivotValueList(const ASTPivotValueList* node,
                              void* data) override;
  void visitASTPivotClause(const ASTPivotClause* node, void* data) override;
  void visitASTUnpivotInItem(const ASTUnpivotInItem* node, void* data) override;
  void visitASTUnpivotInItemList(const ASTUnpivotInItemList* node,
                                 void* data) override;
  void visitASTUnpivotInItemLabel(const ASTUnpivotInItemLabel* node,
                                  void* data) override;
  void visitASTUnpivotClause(const ASTUnpivotClause* node, void* data) override;
  void visitASTAlterApproxViewStatement(const ASTAlterApproxViewStatement* node,
                                        void* data) override;
  void visitASTAlterMaterializedViewStatement(
      const ASTAlterMaterializedViewStatement* node, void* data) override;
  void visitASTAlterModelStatement(const ASTAlterModelStatement* node,
                                   void* data) override;
  void visitASTAlterDatabaseStatement(const ASTAlterDatabaseStatement* node,
                                      void* data) override;
  void visitASTAlterSchemaStatement(const ASTAlterSchemaStatement* node,
                                    void* data) override;
  void visitASTAlterExternalSchemaStatement(
      const ASTAlterExternalSchemaStatement* node, void* data) override;
  void visitASTAlterTableStatement(const ASTAlterTableStatement* node,
                                   void* data) override;
  void visitASTAlterViewStatement(const ASTAlterViewStatement* node,
                                  void* data) override;
  void visitASTAlterIndexStatement(const ASTAlterIndexStatement* node,
                                   void* data) override;
  void visitASTRebuildAction(const ASTRebuildAction* node, void* data) override;
  void visitASTSetOptionsAction(const ASTSetOptionsAction* node,
                                void* data) override;
  void visitASTSetAsAction(const ASTSetAsAction* node, void* data) override;
  void visitASTAddConstraintAction(const ASTAddConstraintAction* node,
                                   void* data) override;
  void visitASTDropConstraintAction(const ASTDropConstraintAction* node,
                                    void* data) override;
  void visitASTDropPrimaryKeyAction(const ASTDropPrimaryKeyAction* node,
                                    void* data) override;
  void visitASTAlterConstraintEnforcementAction(
      const ASTAlterConstraintEnforcementAction* node, void* data) override;
  void visitASTAlterConstraintSetOptionsAction(
      const ASTAlterConstraintSetOptionsAction* node, void* data) override;
  void visitASTAddColumnAction(const ASTAddColumnAction* node,
                               void* data) override;
  void visitASTAddColumnIdentifierAction(
      const ASTAddColumnIdentifierAction* node, void* data) override;
  void visitASTGrantToClause(const ASTGrantToClause* node, void* data) override;
  void visitASTRestrictToClause(const ASTRestrictToClause* node,
                                void* data) override;
  void visitASTAddToRestricteeListClause(
      const ASTAddToRestricteeListClause* node, void* data) override;
  void visitASTRemoveFromRestricteeListClause(
      const ASTRemoveFromRestricteeListClause* node, void* data) override;
  void visitASTFilterUsingClause(const ASTFilterUsingClause* node,
                                 void* data) override;
  void visitASTRevokeFromClause(const ASTRevokeFromClause* node,
                                void* data) override;
  void visitASTRenameToClause(const ASTRenameToClause* node,
                              void* data) override;
  void visitASTSetCollateClause(const ASTSetCollateClause* node,
                                void* data) override;
  void visitASTAlterActionList(const ASTAlterActionList* node,
                               void* data) override;
  void visitASTDescriptorColumn(const ASTDescriptorColumn* node,
                                void* data) override;
  void visitASTDescriptorColumnList(const ASTDescriptorColumnList* node,
                                    void* data) override;
  void visitASTDescriptor(const ASTDescriptor* node, void* data) override;
  void visitASTAlterPrivilegeRestrictionStatement(
      const ASTAlterPrivilegeRestrictionStatement* node, void* data) override;
  void visitASTAlterRowAccessPolicyStatement(
      const ASTAlterRowAccessPolicyStatement* node, void* data) override;

  void visitASTAlterAllRowAccessPoliciesStatement(
      const ASTAlterAllRowAccessPoliciesStatement* node, void* data) override;

  void visitASTForeignKey(const ASTForeignKey* node, void* data) override;
  void visitASTForeignKeyReference(const ASTForeignKeyReference* node,
                                   void* data) override;
  void visitASTForeignKeyActions(const ASTForeignKeyActions* node,
                                 void* data) override;

  void visitASTExceptionHandler(const ASTExceptionHandler* node,
                                void* data) override;
  void visitASTExceptionHandlerList(const ASTExceptionHandlerList* node,
                                    void* data) override;
  void visitASTStatementList(const ASTStatementList* node, void* data) override;
  void visitASTIfStatement(const ASTIfStatement* node, void* data) override;
  void visitASTElseifClause(const ASTElseifClause* node, void* data) override;
  void visitASTElseifClauseList(const ASTElseifClauseList* node,
                                void* data) override;
  void visitASTWhenThenClause(const ASTWhenThenClause* node,
                              void* data) override;
  void visitASTWhenThenClauseList(const ASTWhenThenClauseList* node,
                                  void* data) override;
  void visitASTCaseStatement(const ASTCaseStatement* node, void* data) override;
  void visitASTBeginEndBlock(const ASTBeginEndBlock* node, void* data) override;
  void visitASTIdentifierList(const ASTIdentifierList* node,
                              void* data) override;
  void visitASTVariableDeclaration(const ASTVariableDeclaration* node,
                                   void* data) override;
  void visitASTParameterAssignment(const ASTParameterAssignment* node,
                                   void* data) override;
  void visitASTSystemVariableAssignment(const ASTSystemVariableAssignment* node,
                                        void* data) override;
  void visitASTSingleAssignment(const ASTSingleAssignment* node,
                                void* data) override;

  void visitASTCheckConstraint(const ASTCheckConstraint* node,
                               void* data) override;
  void visitASTScript(const ASTScript* node, void* data) override;
  void visitASTWhileStatement(const ASTWhileStatement* node,
                              void* data) override;
  void visitASTUntilClause(const ASTUntilClause* node, void* data) override;
  void visitASTRepeatStatement(const ASTRepeatStatement* node,
                               void* data) override;
  void visitASTForInStatement(const ASTForInStatement* node,
                              void* data) override;
  void visitASTLabel(const ASTLabel* node, void* data) override;
  void visitASTBreakStatement(const ASTBreakStatement* node,
                              void* data) override;
  void visitASTContinueStatement(const ASTContinueStatement* node,
                                 void* data) override;
  void visitASTReturnStatement(const ASTReturnStatement* node,
                               void* data) override;
  void visitASTAssignmentFromStruct(const ASTAssignmentFromStruct* node,
                                    void* data) override;
  void visitASTCreateProcedureStatement(const ASTCreateProcedureStatement* node,
                                        void* data) override;
  void visitASTNamedArgument(const ASTNamedArgument* node, void* data) override;
  void visitASTInputTableArgument(const ASTInputTableArgument* node,
                                  void* data) override;
  void visitASTExecuteIntoClause(const ASTExecuteIntoClause* node,
                                 void* data) override;
  void visitASTExecuteUsingArgument(const ASTExecuteUsingArgument* node,
                                    void* data) override;
  void visitASTExecuteUsingClause(const ASTExecuteUsingClause* node,
                                  void* data) override;
  void visitASTExecuteImmediateStatement(
      const ASTExecuteImmediateStatement* node, void* data) override;
  void visitASTRaiseStatement(const ASTRaiseStatement* node,
                              void* data) override;
  void visitASTAlterSubEntityAction(const ASTAlterSubEntityAction* node,
                                    void* data) override;
  void visitASTAddSubEntityAction(const ASTAddSubEntityAction* node,
                                  void* data) override;
  void visitASTDropSubEntityAction(const ASTDropSubEntityAction* node,
                                   void* data) override;
  void visitASTWithExpression(const ASTWithExpression* node,
                              void* data) override;
  void visitASTTtlClause(const ASTTtlClause* node, void* data) override;
  void visitASTAddTtlAction(const ASTAddTtlAction* node, void* data) override;
  void visitASTReplaceTtlAction(const ASTReplaceTtlAction* node,
                                void* data) override;
  void visitASTDropTtlAction(const ASTDropTtlAction* node, void* data) override;
  void visitASTInputOutputClause(const ASTInputOutputClause* node,
                                 void* data) override;
  // By default, just do nothing.
  void visitASTLocation(const ASTLocation* node, void* data) override {}

  void visitASTDefineMacroStatement(const ASTDefineMacroStatement* node,
                                    void* data) override;
  void visitASTMacroBody(const ASTMacroBody* node, void* data) override;

  void visitASTSetOperationMetadataList(const ASTSetOperationMetadataList* node,
                                        void* data) override;

  void visitASTSetOperationMetadata(const ASTSetOperationMetadata* node,
                                    void* data) override;

  void visitASTSetOperationAllOrDistinct(
      const ASTSetOperationAllOrDistinct* node, void* data) override;

  void visitASTSetOperationType(const ASTSetOperationType* node,
                                void* data) override;

  void visitASTSetOperationColumnMatchMode(
      const ASTSetOperationColumnMatchMode* node, void* data) override;

  void visitASTSetOperationColumnPropagationMode(
      const ASTSetOperationColumnPropagationMode* node, void* data) override;

  void visitASTExpressionWithOptAlias(const ASTExpressionWithOptAlias* node,
                                      void* data) override;

  void visitASTLockMode(const ASTLockMode* node, void* data) override;

  void visitASTPipeRecursiveUnion(const ASTPipeRecursiveUnion* node,
                                  void* data) override;
  void visitASTCreateSequenceStatement(const ASTCreateSequenceStatement* node,
                                       void* data) override;
  void visitASTAlterSequenceStatement(const ASTAlterSequenceStatement* node,
                                      void* data) override;

  // Spanner-related nodes
  void visitASTSpannerAlterColumnAction(const ASTSpannerAlterColumnAction* node,
                                        void* data) override;
  void visitASTSpannerInterleaveClause(const ASTSpannerInterleaveClause* node,
                                       void* data) override;
  void visitASTSpannerSetOnDeleteAction(const ASTSpannerSetOnDeleteAction* node,
                                        void* data) override;
  void visitASTSpannerTableOptions(const ASTSpannerTableOptions* node,
                                   void* data) override;
  // End of Spanner-related nodes

  void visitASTRunStatement(const ASTRunStatement* node, void* data) override;

 protected:
  // Set break_line to true if you want to print each child on a separate line.
  // NOLINTNEXTLINE(google-default-arguments)
  virtual void UnparseChildrenWithSeparator(const ASTNode* node, void* data,
                                            const std::string& separator,
                                            bool break_line = false);
  // NOLINTNEXTLINE(google-default-arguments)
  virtual void UnparseChildrenWithSeparator(const ASTNode* node, void* data,
                                            int begin, int end,
                                            const std::string& separator,
                                            bool break_line = false);

  template <class NodeType>
  void UnparseVectorWithSeparator(absl::Span<const NodeType* const> node_vector,
                                  void* data, absl::string_view separator,
                                  bool break_line = false) {
    if (!ThreadHasEnoughStack()) {
      println("<Complex nested expression truncated>");
      return;
    }
    bool first = true;
    for (const NodeType* node : node_vector) {
      if (first) {
        first = false;
      } else {
        if (!break_line) {
          print(separator);
        } else {
          println(separator);
        }
      }
      node->Accept(this, data);
    }
  }

  void PrintOpenParenIfNeeded(const ASTNode* node);
  void PrintCloseParenIfNeeded(const ASTNode* node);
  void PrintGraphQuantifierIfNeeded(const ASTGraphPathBase* node, void* data);

  static std::string GetCreateStatementPrefix(
      const ASTCreateStatement* node, absl::string_view create_object_type);

  Formatter formatter_;

 private:
  void UnparseASTTableDataSource(const ASTTableDataSource* node, void* data);
  void VisitCheckConstraintSpec(const ASTCheckConstraint* node, void* data);
  void VisitForeignKeySpec(const ASTForeignKey* node, void* data);
  void UnparseLeafNode(const ASTPrintableLeaf* leaf_node);
  void UnparseColumnSchema(const ASTColumnSchema* node, void* data);
  void VisitAlterStatementBase(const ASTAlterStatementBase* node, void* data);
  void VisitASTDropIndexStatement(const ASTDropIndexStatement* node,
                                  void* data);
  void VisitASTGqlCallBase(const ASTGqlCallBase* node, void* data);
};

}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_UNPARSER_H_
