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

#include "googlesql/parser/unparser.h"

#include <ctype.h>

#include <new>
#include <set>
#include <string>

#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"

ABSL_DECLARE_FLAG(bool, output_asc_explicitly);

namespace googlesql {

std::string Unparse(const ASTNode* node) {
  std::string unparsed_;
  parser::Unparser unparser(&unparsed_);
  node->Accept(&unparser, nullptr);
  unparser.FlushLine();
  return unparsed_;
}

namespace parser {

void Formatter::Indent(int spaces) {
  absl::StrAppend(&indentation_, std::string(spaces, ' '));
}

void Formatter::Dedent(int spaces) {
  ABSL_CHECK_GE(indentation_.size(), spaces)  // Crash OK
      << "Impossible to dedent: has reached to the beginning of the line.";
  indentation_.resize(indentation_.size() - spaces);
}

void Formatter::Format(absl::string_view s) {
  if (s.empty()) return;
  suppress_next_newline_ = false;
  if (buffer_.empty()) {
    // This is treated the same as the case below when starting a new line.
    absl::StrAppend(&buffer_, indentation_, s);
    indentation_length_in_buffer_ = indentation_.size();
  } else {
    // Formats according to the last char in buffer_ and first char in s.
    char last_char = buffer_.back();
    switch (last_char) {
      case '\n':
        // Prepends indentation when starting a new line.
        absl::StrAppend(&buffer_, indentation_, s);
        indentation_length_in_buffer_ = indentation_.size();
        break;
      case '(':
      case '[':
      case '@':
      case '.':
      case '~':
      case ' ':
        // When seeing these characters, appends s directly.
        absl::StrAppend(&buffer_, s);
        break;
      default: {
        char curr_char = s[0];
        if (last_was_single_char_unary_) {
          absl::StrAppend(&buffer_, s);
        } else if (curr_char == '(') {
          // Inserts a space if last token is a separator, otherwise regards
          // it as a function call.
          if (LastTokenIsSeparator()) {
            absl::StrAppend(&buffer_, " ", s);
          } else {
            absl::StrAppend(&buffer_, s);
          }
        } else if (curr_char == ')' || curr_char == '[' || curr_char == ']' ||
                   // To avoid case like "SELECT 1e10,.1e10".
                   (curr_char == '.' && last_char != ',') || curr_char == ',') {
          // If s starts with these characters, appends s directly.
          absl::StrAppend(&buffer_, s);
        } else {
          // By default, separate s from anything before with a space.
          absl::StrAppend(&buffer_, " ", s);
        }
        break;
      }
    }
  }

  if (buffer_.size() >= indentation_length_in_buffer_ + kNumColumnLimit &&
      LastTokenIsSeparator()) {
    FlushLine();
  }

  // Reset last_was_single_char_unary_. If this was called with a unary it'll
  // get set after this returns.
  last_was_single_char_unary_ = false;
}

void Formatter::FormatLine(absl::string_view s) {
  if (!s.empty() || !suppress_next_newline_) {
    Format(s);
    FlushLine();
  }
  suppress_next_newline_ = false;
}

void Formatter::AddUnary(absl::string_view s) {
  if (last_was_single_char_unary_ && absl::EndsWith(buffer_, "-") && s == "-") {
    // Pretend it's not a unary so we don't get '--' which is a comment.
    last_was_single_char_unary_ = false;
  }
  Format(s);
  last_was_single_char_unary_ = s.size() == 1;
}

bool Formatter::LastTokenIsSeparator() {
  // These are keywords emitted in uppercase in Unparser, so don't need to make
  // them case insensitive.
  static const std::set<std::string>& kWordSperarator =
      *new std::set<std::string>({"AND", "OR", "ON", "IN", "BY"});
  static const std::set<char>& kNonWordSperarator =
      *new std::set<char>({',', '<', '>', '-', '+', '=', '*', '/', '%'});
  if (buffer_.empty()) return false;
  // When last token is not a word.
  if (!isalnum(buffer_.back())) {
    return googlesql_base::ContainsKey(kNonWordSperarator, buffer_.back());
  }

  ssize_t last_token_index = buffer_.size() - 1;
  while (last_token_index >= 0 && isalnum(buffer_[last_token_index])) {
    --last_token_index;
  }
  std::string last_token = buffer_.substr(last_token_index + 1);
  return googlesql_base::ContainsKey(kWordSperarator, last_token);
}

void Formatter::FlushLine() {
  suppress_next_newline_ = false;
  if ((unparsed_->empty() || unparsed_->back() == '\n') && buffer_.empty()) {
    return;
  }
  absl::StrAppend(unparsed_, buffer_, "\n");
  buffer_.clear();
}

// Unparser -------------------------------------------------------------------

// Helper functions.
void Unparser::PrintOpenParenIfNeeded(const ASTNode* node) {
  ABSL_DCHECK(node->IsExpression() || node->IsQueryExpression())
      << "Parenthesization is not allowed for " << node->GetNodeKindString();
  if (node->IsExpression() &&
      node->GetAsOrDie<ASTExpression>()->parenthesized()) {
    print("(");
  } else if (node->IsQueryExpression() &&
             node->GetAsOrDie<ASTQueryExpression>()->parenthesized()) {
    print("(");
  }
}

void Unparser::PrintCloseParenIfNeeded(const ASTNode* node) {
  ABSL_DCHECK(node->IsExpression() || node->IsQueryExpression())
      << "Parenthesization is not allowed for " << node->GetNodeKindString();
  if (node->IsExpression() &&
      node->GetAsOrDie<ASTExpression>()->parenthesized()) {
    print(")");
  } else if (node->IsQueryExpression() &&
             node->GetAsOrDie<ASTQueryExpression>()->parenthesized()) {
    print(")");
  }
}

void Unparser::UnparseLeafNode(const ASTPrintableLeaf* leaf_node) {
  print(leaf_node->image());
}

// NOLINTNEXTLINE(google-default-arguments)
void Unparser::UnparseChildrenWithSeparator(const ASTNode* node, void* data,
                                            const std::string& separator,
                                            bool break_line) {
  UnparseChildrenWithSeparator(node, data, 0, node->num_children(), separator,
                               break_line);
}

// Unparse children of <node> from indices in the range [<begin>, <end>)
// putting <separator> between them.
// NOLINTNEXTLINE(google-default-arguments)
void Unparser::UnparseChildrenWithSeparator(const ASTNode* node, void* data,
                                            int begin, int end,
                                            const std::string& separator,
                                            bool break_line) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  for (int i = begin; i < end; i++) {
    if (i > begin) {
      if (break_line) {
        println(separator);
      } else {
        print(separator);
      }
    }
    node->child(i)->Accept(this, data);
  }
}

// Visitor implementation.

void Unparser::visitASTHintedStatement(const ASTHintedStatement* node,
                                       void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTStatementWithPipeOperators(
    const ASTStatementWithPipeOperators* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTExplainStatement(const ASTExplainStatement* node,
                                        void* data) {
  print("EXPLAIN");
  node->statement()->Accept(this, data);
}

void Unparser::visitASTQueryStatement(const ASTQueryStatement* node,
                                      void* data) {
  visitASTQuery(node->query(), data);
}

void Unparser::visitASTSubpipelineStatement(const ASTSubpipelineStatement* node,
                                            void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTFunctionParameter(const ASTFunctionParameter* node,
                                         void* data) {
  print(ASTFunctionParameter::ProcedureParameterModeToString(
      node->procedure_parameter_mode()));
  if (node->name() != nullptr) {
    node->name()->Accept(this, data);
  }
  if (node->type() != nullptr) {
    node->type()->Accept(this, data);
  }
  if (node->templated_parameter_type() != nullptr) {
    node->templated_parameter_type()->Accept(this, data);
  }
  if (node->tvf_schema() != nullptr) {
    node->tvf_schema()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  if (node->default_value() != nullptr) {
    print("DEFAULT");
    node->default_value()->Accept(this, data);
  }
  if (node->is_not_aggregate()) {
    print("NOT AGGREGATE");
  }
}

void Unparser::visitASTTemplatedParameterType(
    const ASTTemplatedParameterType* node, void* data) {
  switch (node->kind()) {
    case ASTTemplatedParameterType::UNINITIALIZED:
      print("UNINITIALIZED");
      break;
    case ASTTemplatedParameterType::ANY_TYPE:
      print("ANY TYPE");
      break;
    case ASTTemplatedParameterType::ANY_PROTO:
      print("ANY PROTO");
      break;
    case ASTTemplatedParameterType::ANY_ENUM:
      print("ANY ENUM");
      break;
    case ASTTemplatedParameterType::ANY_STRUCT:
      print("ANY STRUCT");
      break;
    case ASTTemplatedParameterType::ANY_ARRAY:
      print("ANY ARRAY");
      break;
    case ASTTemplatedParameterType::ANY_TABLE:
      print("ANY TABLE");
      break;
  }
}

void Unparser::visitASTFunctionParameters(const ASTFunctionParameters* node,
                                          void* data) {
  print("(");
  for (int i = 0; i < node->num_children(); ++i) {
    if (i > 0) {
      print(", ");
    }
    node->child(i)->Accept(this, data);
  }
  print(")");
}

void Unparser::visitASTFunctionDeclaration(const ASTFunctionDeclaration* node,
                                           void* data) {
  node->name()->Accept(this, data);
  node->parameters()->Accept(this, data);
}

void Unparser::visitASTSqlFunctionBody(const ASTSqlFunctionBody* node,
                                       void* data) {
  node->expression()->Accept(this, data);
}

void Unparser::visitASTTableClause(const ASTTableClause* node, void* data) {
  print("TABLE ");
  if (node->table_path() != nullptr) {
    node->table_path()->Accept(this, data);
  }
  if (node->tvf() != nullptr) {
    node->tvf()->Accept(this, data);
  }
  if (node->where_clause() != nullptr) {
    node->where_clause()->Accept(this, data);
  }
}

void Unparser::visitASTModelClause(const ASTModelClause* node, void* data) {
  print("MODEL ");
  node->model_path()->Accept(this, data);
}

void Unparser::visitASTConnectionClause(const ASTConnectionClause* node,
                                        void* data) {
  print("CONNECTION ");
  node->connection_path()->Accept(this, data);
}

void Unparser::visitASTTVF(const ASTTVF* node, void* data) {
  if (node->is_lateral()) {
    print("LATERAL ");
  }
  node->name()->Accept(this, data);
  print("(");
  UnparseVectorWithSeparator(node->argument_entries(), data, ",");
  print(")");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }

  for (const auto* op : node->postfix_operators()) {
    op->Accept(this, data);
  }
}

void Unparser::visitASTTVFArgument(const ASTTVFArgument* node, void* data) {
  if (node->expr() != nullptr) {
    node->expr()->Accept(this, data);
  }
  if (node->table_clause() != nullptr) {
    node->table_clause()->Accept(this, data);
  }
  if (node->model_clause() != nullptr) {
    node->model_clause()->Accept(this, data);
  }
  if (node->connection_clause() != nullptr) {
    node->connection_clause()->Accept(this, data);
  }
  if (node->descriptor() != nullptr) {
    node->descriptor()->Accept(this, data);
  }
}

void Unparser::visitASTTVFSchema(const ASTTVFSchema* node, void* data) {
  print("TABLE<");
  UnparseChildrenWithSeparator(node, data, ",");
  print(">");
}

void Unparser::visitASTTVFSchemaColumn(const ASTTVFSchemaColumn* node,
                                       void* data) {
  UnparseChildrenWithSeparator(node, data, "");
}

std::string Unparser::GetCreateStatementPrefix(
    const ASTCreateStatement* node, absl::string_view create_object_type) {
  std::string output("CREATE");
  if (node->is_or_replace()) absl::StrAppend(&output, " OR REPLACE");
  if (node->is_private()) absl::StrAppend(&output, " PRIVATE");
  if (node->is_public()) absl::StrAppend(&output, " PUBLIC");
  if (node->is_temp()) absl::StrAppend(&output, " TEMP");
  auto create_view = node->GetAsOrNull<ASTCreateViewStatementBase>();
  if (create_view != nullptr && create_view->recursive()) {
    absl::StrAppend(&output, " RECURSIVE");
  }
  absl::StrAppend(&output, " ", create_object_type);
  if (node->is_if_not_exists()) absl::StrAppend(&output, " IF NOT EXISTS");
  return output;
}

void Unparser::visitASTCreateConnectionStatement(
    const ASTCreateConnectionStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "CONNECTION"));
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTAlterConnectionStatement(
    const ASTAlterConnectionStatement* node, void* data) {
  print("ALTER CONNECTION");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTCreateConstantStatement(
    const ASTCreateConstantStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "CONSTANT"));
  node->name()->Accept(this, data);
  print("=");
  node->expr()->Accept(this, data);
}

void Unparser::visitASTCreateDatabaseStatement(
    const ASTCreateDatabaseStatement* node, void* data) {
  print("CREATE DATABASE");
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateFunctionStatement(
    const ASTCreateFunctionStatement* node, void* data) {
  const std::string create_object_type =
      absl::StrCat((node->is_aggregate() ? "AGGREGATE " : ""), "FUNCTION");
  print(GetCreateStatementPrefix(node, create_object_type));
  node->function_declaration()->Accept(this, data);
  println();
  if (node->return_type() != nullptr) {
    print("RETURNS");
    node->return_type()->Accept(this, data);
  }
  if (node->sql_security() != ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForSqlSecurity());
  }
  if (node->determinism_level() !=
      ASTCreateFunctionStmtBase::DETERMINISM_UNSPECIFIED) {
    print(node->GetSqlForDeterminismLevel());
  }
  if (node->language() != nullptr) {
    print("LANGUAGE");
    node->language()->Accept(this, data);
  }
  if (node->is_remote()) {
    print("REMOTE");
  }
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  // TODO Improve the readability of the "OPTIONS" and "AS" clauses
  if (node->options_list() != nullptr) {
    println("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
  }
  if (node->code() != nullptr) {
    print("AS");
    node->code()->Accept(this, data);
  } else if (node->sql_function_body() != nullptr) {
    println("AS (");
    {
      Formatter::Indenter indenter(&formatter_);
      node->sql_function_body()->Accept(this, data);
    }
    println();
    println(")");
  }
}

void Unparser::visitASTCreateSchemaStatement(
    const ASTCreateSchemaStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "SCHEMA"));
  node->name()->Accept(this, data);
  if (node->collate() != nullptr) {
    print("DEFAULT");
    visitASTCollate(node->collate(), data);
  }
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateExternalSchemaStatement(
    const ASTCreateExternalSchemaStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "EXTERNAL SCHEMA"));
  node->name()->Accept(this, data);
  if (node->with_connection_clause() != nullptr) {
    visitASTWithConnectionClause(node->with_connection_clause(), data);
  }
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "TABLE FUNCTION"));
  node->function_declaration()->Accept(this, data);
  println();
  if (node->return_tvf_schema() != nullptr &&
      !node->return_tvf_schema()->columns().empty()) {
    print("RETURNS");
    node->return_tvf_schema()->Accept(this, data);
  }
  if (node->sql_security() != ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForSqlSecurity());
  }
  if (node->language() != nullptr) {
    print("LANGUAGE");
    node->language()->Accept(this, data);
  }
  // TODO Improve the readability of the "OPTIONS" and "AS" clauses
  if (node->options_list() != nullptr) {
    println("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
  }
  if (node->code() != nullptr) {
    print("AS");
    node->code()->Accept(this, data);
  } else if (node->query() != nullptr) {
    println("AS");
    {
      Formatter::Indenter indenter(&formatter_);
      node->query()->Accept(this, data);
    }
    println();
  }
}

void Unparser::visitASTAuxLoadDataFromFilesOptionsList(
    const ASTAuxLoadDataFromFilesOptionsList* node, void* data) {
  node->options_list()->Accept(this, data);
}

void Unparser::visitASTAuxLoadDataPartitionsClause(
    const ASTAuxLoadDataPartitionsClause* node, void* data) {
  if (node->is_overwrite()) print("OVERWRITE");
  print("PARTITIONS");
  print("(");
  node->partition_filter()->Accept(this, data);
  print(")");
}

void Unparser::visitASTAuxLoadDataStatement(const ASTAuxLoadDataStatement* node,
                                            void* data) {
  print("LOAD DATA");
  switch (node->insertion_mode()) {
    case ASTAuxLoadDataStatement::InsertionMode::APPEND:
      print("INTO");
      break;
    case ASTAuxLoadDataStatement::InsertionMode::OVERWRITE:
      print("OVERWRITE");
      break;
    default:
      break;
  }
  if (node->is_temp_table()) {
    print("TEMP TABLE");
  }
  node->name()->Accept(this, data);
  if (node->table_element_list() != nullptr) {
    node->table_element_list()->Accept(this, data);
  }
  if (node->load_data_partitions_clause() != nullptr) {
    node->load_data_partitions_clause()->Accept(this, data);
  }
  if (node->collate() != nullptr) {
    visitASTCollate(node->collate(), data);
  }
  if (node->partition_by() != nullptr) {
    println();
    node->partition_by()->Accept(this, data);
  }
  if (node->cluster_by() != nullptr) {
    println();
    node->cluster_by()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  println();
  print("FROM FILES");
  node->from_files()->Accept(this, data);
  if (node->with_partition_columns_clause() != nullptr) {
    println();
    node->with_partition_columns_clause()->Accept(this, data);
  }
  if (node->with_connection_clause() != nullptr) {
    println();
    node->with_connection_clause()->Accept(this, data);
  }
}

void Unparser::visitASTSequenceArg(const ASTSequenceArg* node, void* data) {
  print("SEQUENCE ");
  node->sequence_path()->Accept(this, data);
}

void Unparser::visitASTCreateTableStatement(const ASTCreateTableStatement* node,
                                            void* data) {
  print(GetCreateStatementPrefix(node, "TABLE"));
  node->name()->Accept(this, data);
  if (node->table_element_list() != nullptr) {
    println();
    node->table_element_list()->Accept(this, data);
  }
  if (node->collate() != nullptr) {
    print("DEFAULT");
    visitASTCollate(node->collate(), data);
  }
  if (node->like_table_name() != nullptr) {
    println("LIKE");
    node->like_table_name()->Accept(this, data);
  }
  if (node->spanner_options() != nullptr) {
    node->spanner_options()->Accept(this, data);
  }
  if (node->clone_data_source() != nullptr) {
    println("CLONE");
    node->clone_data_source()->Accept(this, data);
  }
  if (node->copy_data_source() != nullptr) {
    println("COPY");
    node->copy_data_source()->Accept(this, data);
  }
  if (node->partition_by() != nullptr) {
    node->partition_by()->Accept(this, data);
  }
  if (node->cluster_by() != nullptr) {
    node->cluster_by()->Accept(this, data);
  }
  if (node->ttl() != nullptr) {
    node->ttl()->Accept(this, data);
  }
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    println("AS");
    node->query()->Accept(this, data);
  }
}

void Unparser::visitASTCreateEntityStatement(
    const ASTCreateEntityStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, node->type()->GetAsString()));
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->json_body() != nullptr) {
    println();
    print("AS ");
    node->json_body()->Accept(this, data);
  }
  if (node->text_body() != nullptr) {
    println();
    print("AS");
    node->text_body()->Accept(this, data);
  }
}

void Unparser::visitASTAlterEntityStatement(const ASTAlterEntityStatement* node,
                                            void* data) {
  print("ALTER ");
  node->type()->Accept(this, data);
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTCreateModelStatement(const ASTCreateModelStatement* node,
                                            void* data) {
  print(GetCreateStatementPrefix(node, "MODEL"));
  node->name()->Accept(this, data);
  if (node->input_output_clause() != nullptr) {
    node->input_output_clause()->Accept(this, data);
  }
  if (node->transform_clause() != nullptr) {
    print("TRANSFORM");
    node->transform_clause()->Accept(this, data);
  }
  if (node->is_remote()) {
    print("REMOTE");
  }
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    println("AS");
    node->query()->Accept(this, data);
  }
  if (node->aliased_query_list() != nullptr) {
    println("AS");
    println("(");
    node->aliased_query_list()->Accept(this, data);
    println(")");
  }
}

void Unparser::visitASTTableElementList(const ASTTableElementList* node,
                                        void* data) {
  println("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",", true /* break_line */);
  }
  println();
  print(")");
}

void Unparser::visitASTNotNullColumnAttribute(
    const ASTNotNullColumnAttribute* node, void* data) {
  print("NOT NULL");
}

void Unparser::visitASTHiddenColumnAttribute(
    const ASTHiddenColumnAttribute* node, void* data) {
  print("HIDDEN");
}

void Unparser::visitASTPrimaryKeyColumnAttribute(
    const ASTPrimaryKeyColumnAttribute* node, void* data) {
  print("PRIMARY KEY");
  if (!node->enforced()) {
    print("NOT ENFORCED");
  }
}

void Unparser::visitASTForeignKeyColumnAttribute(
    const ASTForeignKeyColumnAttribute* node, void* data) {
  if (node->constraint_name() != nullptr) {
    print("CONSTRAINT");
    node->constraint_name()->Accept(this, data);
  }
  node->reference()->Accept(this, data);
}

void Unparser::visitASTColumnAttributeList(const ASTColumnAttributeList* node,
                                           void* data) {
  UnparseChildrenWithSeparator(node, data, "", /*break_line=*/false);
}

void Unparser::visitASTColumnDefinition(const ASTColumnDefinition* node,
                                        void* data) {
  if (node->name() != nullptr) {
    node->name()->Accept(this, data);
  }
  if (node->schema() != nullptr) {
    node->schema()->Accept(this, data);
  }
}

void Unparser::visitASTCreateViewStatement(const ASTCreateViewStatement* node,
                                           void* data) {
  print(GetCreateStatementPrefix(node, "VIEW"));
  node->name()->Accept(this, data);
  if (node->column_with_options_list() != nullptr) {
    node->column_with_options_list()->Accept(this, data);
  }
  if (node->sql_security() != ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForSqlSecurity());
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  println("AS");
  node->query()->Accept(this, data);
}

void Unparser::visitASTCreateApproxViewStatement(
    const ASTCreateApproxViewStatement* node, void* data) {
  print("CREATE");

  if (node->is_or_replace()) print("OR REPLACE");
  print("APPROX");
  if (node->recursive()) {
    print("RECURSIVE");
  }
  print("VIEW");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->column_with_options_list() != nullptr) {
    node->column_with_options_list()->Accept(this, data);
  }
  if (node->sql_security() != ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForSqlSecurity());
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  println("AS");
  node->query()->Accept(this, data);
}

void Unparser::visitASTCreateMaterializedViewStatement(
    const ASTCreateMaterializedViewStatement* node, void* data) {
  print("CREATE");

  if (node->is_or_replace()) print("OR REPLACE");
  print("MATERIALIZED");
  if (node->recursive()) {
    print("RECURSIVE");
  }
  print("VIEW");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->column_with_options_list() != nullptr) {
    node->column_with_options_list()->Accept(this, data);
  }
  if (node->sql_security() != ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForSqlSecurity());
  }
  if (node->partition_by() != nullptr) {
    node->partition_by()->Accept(this, data);
  }
  if (node->cluster_by() != nullptr) {
    node->cluster_by()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    println("AS");
    node->query()->Accept(this, data);
  } else if (node->replica_source() != nullptr) {
    println("AS REPLICA OF");
    node->replica_source()->Accept(this, data);
  }
}

void Unparser::visitASTColumnWithOptions(const ASTColumnWithOptions* node,
                                         void* data) {
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTColumnWithOptionsList(
    const ASTColumnWithOptionsList* node, void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTWithPartitionColumnsClause(
    const ASTWithPartitionColumnsClause* node, void* data) {
  print("WITH PARTITION COLUMNS");
  if (node->table_element_list() != nullptr) {
    node->table_element_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateExternalTableStatement(
    const ASTCreateExternalTableStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "EXTERNAL TABLE"));
  node->name()->Accept(this, data);
  if (node->table_element_list() != nullptr) {
    println();
    node->table_element_list()->Accept(this, data);
  }
  if (node->collate() != nullptr) {
    print("DEFAULT");
    visitASTCollate(node->collate(), data);
  }
  if (node->like_table_name() != nullptr) {
    println("LIKE");
    node->like_table_name()->Accept(this, data);
  }
  if (node->with_partition_columns_clause() != nullptr) {
    node->with_partition_columns_clause()->Accept(this, data);
  }
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateSnapshotTableStatement(
    const ASTCreateSnapshotTableStatement* node, void* data) {
  print("CREATE");
  if (node->is_or_replace()) print("OR REPLACE");
  print("SNAPSHOT TABLE");
  if (node->is_if_not_exists()) print("IF NOT EXISTS");
  node->name()->Accept(this, data);
  print("CLONE");
  node->clone_data_source()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTCreateSnapshotStatement(
    const ASTCreateSnapshotStatement* node, void* data) {
  print("CREATE");
  if (node->is_or_replace()) print("OR REPLACE");
  print("SNAPSHOT ");
  print(SchemaObjectKindToName(node->schema_object_kind()));
  if (node->is_if_not_exists()) print("IF NOT EXISTS");
  node->name()->Accept(this, data);
  print("CLONE");
  node->clone_data_source()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTGrantToClause(const ASTGrantToClause* node, void* data) {
  if (node->has_grant_keyword_and_parens()) {
    print("GRANT");
  }
  print("TO ");
  if (node->has_grant_keyword_and_parens()) {
    print("(");
  }
  node->grantee_list()->Accept(this, data);
  if (node->has_grant_keyword_and_parens()) {
    print(")");
  }
}

void Unparser::visitASTRestrictToClause(const ASTRestrictToClause* node,
                                        void* data) {
  print("RESTRICT TO (");
  node->restrictee_list()->Accept(this, data);
  print(")");
}

void Unparser::visitASTAddToRestricteeListClause(
    const ASTAddToRestricteeListClause* node, void* data) {
  print("ADD ");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS ");
  }
  print("(");
  node->restrictee_list()->Accept(this, data);
  print(")");
}

void Unparser::visitASTRemoveFromRestricteeListClause(
    const ASTRemoveFromRestricteeListClause* node, void* data) {
  print("REMOVE ");
  if (node->is_if_exists()) {
    print("IF EXISTS ");
  }
  print("(");
  node->restrictee_list()->Accept(this, data);
  print(")");
}

void Unparser::visitASTFilterUsingClause(const ASTFilterUsingClause* node,
                                         void* data) {
  if (node->has_filter_keyword()) {
    print("FILTER");
  }
  print("USING (");
  node->predicate()->Accept(this, data);
  print(")");
}

void Unparser::visitASTCreatePrivilegeRestrictionStatement(
    const ASTCreatePrivilegeRestrictionStatement* node, void* data) {
  print("CREATE");
  if (node->is_or_replace()) {
    print("OR REPLACE");
  }
  print("PRIVILEGE RESTRICTION");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  print("ON");
  node->privileges()->Accept(this, data);
  print("ON");
  node->object_type()->Accept(this, data);
  node->name_path()->Accept(this, data);
  if (node->restrict_to() != nullptr) {
    node->restrict_to()->Accept(this, data);
  }
}

void Unparser::visitASTCreateRowAccessPolicyStatement(
    const ASTCreateRowAccessPolicyStatement* node, void* data) {
  print("CREATE");
  if (node->is_or_replace()) {
    print("OR REPLACE");
  }
  print("ROW");
  if (node->has_access_keyword()) {
    print("ACCESS");
  }
  print("POLICY");
  if (node->is_if_not_exists()) print("IF NOT EXISTS");
  if (node->name() != nullptr) {
    node->name()->Accept(this, data);
  }
  print("ON");
  node->target_path()->Accept(this, data);
  if (node->grant_to() != nullptr) {
    node->grant_to()->Accept(this, data);
  }
  node->filter_using()->Accept(this, data);
}

void Unparser::visitASTExportDataStatement(const ASTExportDataStatement* node,
                                           void* data) {
  print("EXPORT DATA");
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    println("AS");
    node->query()->Accept(this, data);
  }
}

void Unparser::visitASTExportModelStatement(const ASTExportModelStatement* node,
                                            void* data) {
  print("EXPORT MODEL");
  if (node->model_name_path() != nullptr) {
    node->model_name_path()->Accept(this, data);
  }

  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }

  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTExportMetadataStatement(
    const ASTExportMetadataStatement* node, void* data) {
  print("EXPORT");
  print(SchemaObjectKindToName(node->schema_object_kind()));
  print("METADATA FROM");
  ABSL_DCHECK(node->name_path() != nullptr);
  node->name_path()->Accept(this, data);

  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }

  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTWithConnectionClause(const ASTWithConnectionClause* node,
                                            void* data) {
  print("WITH");
  node->connection_clause()->Accept(this, data);
}

void Unparser::visitASTCallStatement(const ASTCallStatement* node, void* data) {
  print("CALL");
  node->procedure_name()->Accept(this, data);
  print("(");
  UnparseVectorWithSeparator(node->arguments(), data, ",");
  print(")");
}

void Unparser::visitASTDefineTableStatement(const ASTDefineTableStatement* node,
                                            void* data) {
  print("DEFINE TABLE");
  node->name()->Accept(this, data);
  node->options_list()->Accept(this, data);
}

void Unparser::visitASTCreateLocalityGroupStatement(
    const ASTCreateLocalityGroupStatement* node, void* data) {
  print("CREATE LOCALITY GROUP");
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTDescribeStatement(const ASTDescribeStatement* node,
                                         void* data) {
  print("DESCRIBE");
  if (node->optional_identifier() != nullptr) {
    node->optional_identifier()->Accept(this, data);
  }
  node->name()->Accept(this, data);
  if (node->optional_from_name() != nullptr) {
    print("FROM");
    node->optional_from_name()->Accept(this, data);
  }
}

void Unparser::visitASTDescriptorColumn(const ASTDescriptorColumn* node,
                                        void* data) {
  node->name()->Accept(this, data);
}

void Unparser::visitASTDescriptorColumnList(const ASTDescriptorColumnList* node,
                                            void* data) {
  UnparseChildrenWithSeparator(node, data, ", ");
}

void Unparser::visitASTDescriptor(const ASTDescriptor* node, void* data) {
  print("DESCRIPTOR(");
  node->columns()->Accept(this, data);
  print(")");
}

void Unparser::visitASTShowStatement(const ASTShowStatement* node, void* data) {
  print("SHOW");
  node->identifier()->Accept(this, data);
  if (node->optional_name() != nullptr) {
    print("FROM");
    node->optional_name()->Accept(this, data);
  }
  if (node->optional_like_string() != nullptr) {
    print("LIKE");
    node->optional_like_string()->Accept(this, data);
  }
}

void Unparser::visitASTBeginStatement(const ASTBeginStatement* node,
                                      void* data) {
  print("BEGIN TRANSACTION");
  if (node->mode_list() != nullptr) {
    node->mode_list()->Accept(this, data);
  }
}

void Unparser::visitASTTransactionIsolationLevel(
    const ASTTransactionIsolationLevel* node, void* data) {
  if (node->identifier1() != nullptr) {
    print("ISOLATION LEVEL");
    node->identifier1()->Accept(this, data);
  }
  if (node->identifier2() != nullptr) {
    node->identifier2()->Accept(this, data);
  }
}

void Unparser::visitASTTransactionReadWriteMode(
    const ASTTransactionReadWriteMode* node, void* data) {
  switch (node->mode()) {
    case ASTTransactionReadWriteMode::READ_ONLY:
      print("READ ONLY");
      break;
    case ASTTransactionReadWriteMode::READ_WRITE:
      print("READ WRITE");
      break;
    case ASTTransactionReadWriteMode::INVALID:
      ABSL_LOG(ERROR) << "invalid read write mode";
      break;
  }
}

void Unparser::visitASTTransactionModeList(const ASTTransactionModeList* node,
                                           void* data) {
  bool first = true;
  for (const ASTTransactionMode* mode : node->elements()) {
    if (!first) {
      print(",");
    } else {
      first = false;
    }
    mode->Accept(this, data);
  }
}

void Unparser::visitASTSetTransactionStatement(
    const ASTSetTransactionStatement* node, void* data) {
  print("SET TRANSACTION");
  node->mode_list()->Accept(this, data);
}

void Unparser::visitASTCommitStatement(const ASTCommitStatement* node,
                                       void* data) {
  print("COMMIT");
}

void Unparser::visitASTRollbackStatement(const ASTRollbackStatement* node,
                                         void* data) {
  print("ROLLBACK");
}

void Unparser::visitASTStartBatchStatement(const ASTStartBatchStatement* node,
                                           void* data) {
  print("START BATCH");
  if (node->batch_type() != nullptr) {
    node->batch_type()->Accept(this, data);
  }
}

void Unparser::visitASTRunBatchStatement(const ASTRunBatchStatement* node,
                                         void* data) {
  print("RUN BATCH");
}

void Unparser::visitASTAbortBatchStatement(const ASTAbortBatchStatement* node,
                                           void* data) {
  print("ABORT BATCH");
}

void Unparser::visitASTUndropStatement(const ASTUndropStatement* node,
                                       void* data) {
  print("UNDROP");
  print(SchemaObjectKindToName(node->schema_object_kind()));
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->for_system_time() != nullptr) {
    println();
    Formatter::Indenter indenter(&formatter_);
    node->for_system_time()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTDropStatement(const ASTDropStatement* node, void* data) {
  print("DROP");
  print(SchemaObjectKindToName(node->schema_object_kind()));
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  print(node->GetSQLForDropMode(node->drop_mode()));
}

void Unparser::visitASTDropEntityStatement(const ASTDropEntityStatement* node,
                                           void* data) {
  print("DROP ");
  node->entity_type()->Accept(this, data);
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
}

void Unparser::visitASTDropFunctionStatement(
    const ASTDropFunctionStatement* node, void* data) {
  print("DROP FUNCTION");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->parameters() != nullptr) {
    node->parameters()->Accept(this, data);
  }
}

void Unparser::visitASTDropTableFunctionStatement(
    const ASTDropTableFunctionStatement* node, void* data) {
  print("DROP TABLE FUNCTION");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
}

void Unparser::visitASTDropPrivilegeRestrictionStatement(
    const ASTDropPrivilegeRestrictionStatement* node, void* data) {
  print("DROP PRIVILEGE RESTRICTION");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  print("ON");
  node->privileges()->Accept(this, data);
  print("ON");
  node->object_type()->Accept(this, data);
  node->name_path()->Accept(this, data);
}

void Unparser::visitASTDropRowAccessPolicyStatement(
    const ASTDropRowAccessPolicyStatement* node, void* data) {
  print("DROP ROW ACCESS POLICY");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  print("ON");
  node->table_name()->Accept(this, data);
}

void Unparser::visitASTDropAllRowAccessPoliciesStatement(
    const ASTDropAllRowAccessPoliciesStatement* node, void* data) {
  print("DROP ALL ROW");
  if (node->has_access_keyword()) {
    print("ACCESS");
  }
  print("POLICIES ON");
  node->table_name()->Accept(this, data);
}

void Unparser::VisitASTDropIndexStatement(const ASTDropIndexStatement* node,
                                          void* data) {
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->table_name() != nullptr) {
    print("ON");
    node->table_name()->Accept(this, data);
  }
}

void Unparser::visitASTDropSearchIndexStatement(
    const ASTDropSearchIndexStatement* node, void* data) {
  print("DROP SEARCH INDEX");
  VisitASTDropIndexStatement(node, data);
}

void Unparser::visitASTDropVectorIndexStatement(
    const ASTDropVectorIndexStatement* node, void* data) {
  print("DROP VECTOR INDEX");
  VisitASTDropIndexStatement(node, data);
}

void Unparser::visitASTDropMaterializedViewStatement(
    const ASTDropMaterializedViewStatement* node, void* data) {
  print("DROP MATERIALIZED VIEW");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
}

void Unparser::visitASTDropSnapshotTableStatement(
    const ASTDropSnapshotTableStatement* node, void* data) {
  print("DROP SNAPSHOT TABLE");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
}

void Unparser::visitASTRenameStatement(const ASTRenameStatement* node,
                                       void* data) {
  print("RENAME");
  if (node->identifier() != nullptr) {
    node->identifier()->Accept(this, data);
  }
  node->old_name()->Accept(this, data);
  print("TO");
  node->new_name()->Accept(this, data);
}

void Unparser::visitASTImportStatement(const ASTImportStatement* node,
                                       void* data) {
  print("IMPORT");
  if (node->import_kind() == ASTImportStatement::MODULE) {
    print("MODULE");
  } else if (node->import_kind() == ASTImportStatement::PROTO) {
    print("PROTO");
  } else {
    print("<invalid import type>");
  }

  if (node->name() != nullptr) {
    node->name()->Accept(this, data);
  }
  if (node->string_value() != nullptr) {
    node->string_value()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  if (node->into_alias() != nullptr) {
    node->into_alias()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTModuleStatement(const ASTModuleStatement* node,
                                       void* data) {
  print("MODULE");
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTWithClause(const ASTWithClause* node, void* data) {
  if (node->recursive()) {
    println("WITH RECURSIVE");
  } else {
    println("WITH");
  }
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
}

void Unparser::visitASTWithClauseEntry(const ASTWithClauseEntry* node,
                                       void* data) {
  ABSL_DCHECK_EQ(node->num_children(), 1);
  if (node->aliased_query() != nullptr) {
    node->aliased_query()->Accept(this, data);
  } else if (node->aliased_group_rows() != nullptr) {
    node->aliased_group_rows()->Accept(this, data);
  }
}

void Unparser::visitASTAliasedGroupRows(const ASTAliasedGroupRows* node,
                                        void* data) {
  node->alias()->Accept(this, data);
  print("() AS GROUP ROWS");
}

void Unparser::visitASTQuery(const ASTQuery* node, void* data) {
  PrintOpenParenIfNeeded(node);
  if (node->is_nested()) {
    println();
    print("(");
    {
      Formatter::Indenter indenter(&formatter_);
      visitASTChildren(node, data);
    }
    println();
    print(")");
  } else {
    visitASTChildren(node, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTAliasedQueryExpression(
    const ASTAliasedQueryExpression* node, void* data) {
  println();
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    node->query()->Accept(this, data);
  }
  println();
  print(")");
  node->alias()->Accept(this, data);
}

void Unparser::visitASTFromQuery(const ASTFromQuery* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTSubpipeline(const ASTSubpipeline* node, void* data) {
  // The parenthesized case is for when this is used inside other operators.
  // The unparenthesized case is for standalone subpipelines.
  if (node->parenthesized()) {
    Formatter::Indenter indenter(&formatter_);
    print("(");
    visitASTChildren(node, data);
  } else {
    visitASTChildren(node, data);
  }
  if (node->num_children() > 0) {
    println();
  }
  if (node->parenthesized()) {
    print(")");
  }
}

Formatter::PipeAndIndent::PipeAndIndent(Formatter* formatter)
    : formatter_(formatter) {
  formatter_->FormatLine("");
  formatter_->Format("|>");
  // Several of the pipe operator unparsers call the regular operator
  // unparser, like "WHERE" or "LIMIT", which call println, but we want
  // to skip that newline after the pipe.
  formatter_->SetSuppressNextNewline();

  // Indent any extra lines inside this pipe operator so they line up.
  formatter_->Indent(3);  // 3 is size of "|> ".
}

Formatter::PipeAndIndent::~PipeAndIndent() { formatter_->Dedent(3); }

void Unparser::visitASTPipeWhere(const ASTPipeWhere* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeSelect(const ASTPipeSelect* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeLimitOffset(const ASTPipeLimitOffset* node,
                                       void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeOrderBy(const ASTPipeOrderBy* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeExtend(const ASTPipeExtend* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("EXTEND");
  // Only the SELECT should be present.  The ASTSelect visitor
  // would print "SELECT" which we don't want, but visiting the children
  // will find just the one clause that exists.
  visitASTChildren(node->select(), data);
}

void Unparser::visitASTPipeRenameItem(const ASTPipeRenameItem* node,
                                      void* data) {
  node->old_name()->Accept(this, data);
  print("AS");
  node->new_name()->Accept(this, data);
}

void Unparser::visitASTPipeRename(const ASTPipeRename* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("RENAME");
  UnparseVectorWithSeparator(node->rename_item_list(), data, ",");
}

void Unparser::visitASTPipeAggregate(const ASTPipeAggregate* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("AGGREGATE");
  if (node->with_modifier() != nullptr) {
    node->with_modifier()->Accept(this, data);
  }
  // Only the SELECT and GROUP BY should be present.  The ASTSelect visitor
  // would print "SELECT" which we don't want, but we can visit all the
  // other children the normal way, and we'll get the two clauses we want.
  visitASTChildren(node->select(), data);
}

void Unparser::visitASTPipeSetOperation(const ASTPipeSetOperation* node,
                                        void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  node->metadata()->Accept(this, data);
  println();
  UnparseVectorWithSeparator(node->inputs(), data, ",\n");
}

void Unparser::visitASTPipeJoin(const ASTPipeJoin* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeJoinLhsPlaceholder(
    const ASTPipeJoinLhsPlaceholder* node, void* data) {
  // Nothing to print.
}

void Unparser::visitASTPipeCall(const ASTPipeCall* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("CALL");
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeWindow(const ASTPipeWindow* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("WINDOW");
  // Only the SELECT should be present.  The ASTSelect visitor
  // would print "SELECT" which we don't want, but visiting the children
  // will find just the one clause that exists.
  visitASTChildren(node->select(), data);
}

void Unparser::visitASTPipeDistinct(const ASTPipeDistinct* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("DISTINCT");
  // Don't visit children, because by definition, pipe DISTINCT doesn't have
  // any children.
}

void Unparser::visitASTPipeTablesample(const ASTPipeTablesample* node,
                                       void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeMatchRecognize(const ASTPipeMatchRecognize* node,
                                          void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeAs(const ASTPipeAs* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeDescribe(const ASTPipeDescribe* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("DESCRIBE");
}

void Unparser::visitASTPipeStaticDescribe(const ASTPipeStaticDescribe* node,
                                          void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("STATIC_DESCRIBE");
}

void Unparser::visitASTPipeAssert(const ASTPipeAssert* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("ASSERT");
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTPipeLog(const ASTPipeLog* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("LOG");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->subpipeline() != nullptr) {
    // Add a space between ABSL_LOG and the subpipeline so it doesn't look
    // like a function call. This ends up printing a double space before
    // the "(" because of details somewhere else. One space would be preferred.
    print(" ");
    node->subpipeline()->Accept(this, data);
  }
}

void Unparser::visitASTPipeDrop(const ASTPipeDrop* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("DROP");
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeSetItem(const ASTPipeSetItem* node, void* data) {
  node->column()->Accept(this, data);
  print("=");
  node->expression()->Accept(this, data);
}

void Unparser::visitASTPipeSet(const ASTPipeSet* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("SET");
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTPipePivot(const ASTPipePivot* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeUnpivot(const ASTPipeUnpivot* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeIf(const ASTPipeIf* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("IF");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }

  UnparseVectorWithSeparator(node->if_cases(), data, /*separator=*/"ELSEIF");

  if (node->else_subpipeline() != nullptr) {
    println();
    print("ELSE");
    node->else_subpipeline()->Accept(this, data);
  }
}

void Unparser::visitASTPipeIfCase(const ASTPipeIfCase* node, void* data) {
  // The IF or ELSEIF keyword has been emitted already.
  node->condition()->Accept(this, data);
  // Add a space so it doesn't format as "THEN(" like a function.
  print("THEN ");
  node->subpipeline()->Accept(this, data);
  println();
}

void Unparser::visitASTSetOperation(const ASTSetOperation* node, void* data) {
  PrintOpenParenIfNeeded(node);
  for (int i = 0; i < node->inputs().size(); ++i) {
    if (i > 0) {
      // The i-th query is preceded by the (i-1)-th operator.
      node->metadata()->set_operation_metadata_list(i - 1)->Accept(this, data);
    }
    node->inputs(i)->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTPipeFork(const ASTPipeFork* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  // Add a space after FORK so it doesn't look like a function call. This ends
  // up printing a double space before the "(" because of details somewhere
  // else. One space would be preferred.
  print("FORK ");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  UnparseVectorWithSeparator(node->subpipeline_list(), data, ",");
}

void Unparser::visitASTPipeTee(const ASTPipeTee* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  // Add a space after TEE so it doesn't look like a function call. This ends
  // up printing a double space before the "(" because of details somewhere
  // else. One space would be preferred.
  print("TEE ");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  UnparseVectorWithSeparator(node->subpipeline_list(), data, ",");
}

void Unparser::visitASTPipeWith(const ASTPipeWith* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  node->with_clause()->Accept(this, data);
}

void Unparser::visitASTPipeExportData(const ASTPipeExportData* node,
                                      void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeCreateTable(const ASTPipeCreateTable* node,
                                       void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTPipeInsert(const ASTPipeInsert* node, void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  visitASTChildren(node, data);
}

void Unparser::visitASTSetAsAction(const ASTSetAsAction* node, void* data) {
  print("SET AS ");
  if (node->json_body() != nullptr) {
    node->json_body()->Accept(this, data);
  }
  if (node->text_body() != nullptr) {
    node->text_body()->Accept(this, data);
  }
}

void Unparser::visitASTSelect(const ASTSelect* node, void* data) {
  PrintOpenParenIfNeeded(node);
  println();
  print("SELECT");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->with_modifier() != nullptr) {
    node->with_modifier()->Accept(this, data);
  }
  if (node->distinct()) {
    print("DISTINCT");
  } else if (node->with_modifier() != nullptr) {
    // Omitting 'ALL' when the query uses `SELECT WITH kind OPTIONS()` can
    // cause the unparser to change the meaning of the query.
    // See: b/333428605
    print("ALL");
  }

  // Visit all children except `hint`, `select_with_clause_identifier` and
  // `select_with_clause_options`, which we processed above.  We can't just use
  // visitASTChildren(node, data) because we need to insert the DISTINCT
  // modifier after the hint and anonymization nodes and before everything else.
  for (int i = 0; i < node->num_children(); ++i) {
    const ASTNode* child = node->child(i);
    if (child != node->hint() && child != node->with_modifier()) {
      child->Accept(this, data);
    }
  }

  println();
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTSelectAs(const ASTSelectAs* node, void* data) {
  if (node->as_mode() != ASTSelectAs::TYPE_NAME) {
    print(absl::StrCat(
        "AS ", node->as_mode() == ASTSelectAs::VALUE ? "VALUE" : "STRUCT"));
  } else {
    print("AS");
  }
  visitASTChildren(node, data);
}

void Unparser::visitASTSelectList(const ASTSelectList* node, void* data) {
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",", true /* break_line */);
  }
}

void Unparser::visitASTWithModifier(const ASTWithModifier* node, void* data) {
  print("WITH");
  if (node->identifier() != nullptr) {
    node->identifier()->Accept(this, data);
    if (node->options() != nullptr) {
      print("OPTIONS");
      node->options()->Accept(this, data);
    }
  }
}

void Unparser::visitASTSelectColumn(const ASTSelectColumn* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTAlias(const ASTAlias* node, void* data) {
  print(absl::StrCat("AS ",
                     ToIdentifierLiteral(node->identifier()->GetAsIdString())));
}

void Unparser::visitASTAliasedQuery(const ASTAliasedQuery* node, void* data) {
  println();
  node->alias()->Accept(this, data);
  println("AS (");
  {
    Formatter::Indenter indenter(&formatter_);
    visitASTQuery(node->query(), data);
  }
  println();
  print(")");
  if (node->modifiers() != nullptr) {
    node->modifiers()->Accept(this, data);
  }
}

void Unparser::visitASTAliasedQueryList(const ASTAliasedQueryList* node,
                                        void* data) {
  UnparseVectorWithSeparator(node->aliased_query_list(), data, ",");
}

void Unparser::visitASTAliasedQueryModifiers(
    const ASTAliasedQueryModifiers* node, void* data) {
  if (node->recursion_depth_modifier() != nullptr) {
    node->recursion_depth_modifier()->Accept(this, data);
  }
}

void Unparser::visitASTRecursionDepthModifier(
    const ASTRecursionDepthModifier* node, void* data) {
  print("WITH DEPTH");
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  print("BETWEEN");
  node->lower_bound()->Accept(this, data);
  print("AND");
  node->upper_bound()->Accept(this, data);
}

void Unparser::visitASTIntOrUnbounded(const ASTIntOrUnbounded* node,
                                      void* data) {
  if (node->bound() != nullptr) {
    node->bound()->Accept(this, data);
  } else {
    print("UNBOUNDED");
  }
}

void Unparser::visitASTIntoAlias(const ASTIntoAlias* node, void* data) {
  print(absl::StrCat("INTO ",
                     ToIdentifierLiteral(node->identifier()->GetAsIdString())));
}

void Unparser::visitASTFromClause(const ASTFromClause* node, void* data) {
  println();
  println("FROM");
  {
    Formatter::Indenter indenter(&formatter_);
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTTransformClause(const ASTTransformClause* node,
                                       void* data) {
  println("(");
  visitASTChildren(node, data);
  println(")");
}

void Unparser::visitASTWithOffset(const ASTWithOffset* node, void* data) {
  print("WITH OFFSET");
  visitASTChildren(node, data);
}

void Unparser::visitASTUnnestExpression(const ASTUnnestExpression* node,
                                        void* data) {
  print("UNNEST(");
  for (int i = 0; i < node->expressions().size(); i++) {
    if (i > 0) {
      print(", ");
    }
    node->expressions(i)->Accept(this, data);
  }
  if (node->array_zip_mode() != nullptr) {
    print(", ");
    node->array_zip_mode()->Accept(this, data);
  }
  print(")");
}

void Unparser::visitASTUnnestExpressionWithOptAliasAndOffset(
    const ASTUnnestExpressionWithOptAliasAndOffset* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTTablePathExpression(const ASTTablePathExpression* node,
                                           void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTPathExpressionList(const ASTPathExpressionList* node,
                                          void* data) {
  UnparseVectorWithSeparator(node->path_expression_list(), data, ", ");
}

void Unparser::visitASTForSystemTime(const ASTForSystemTime* node, void* data) {
  print("FOR SYSTEM_TIME AS OF ");
  visitASTChildren(node, data);
}

void Unparser::visitASTTableSubquery(const ASTTableSubquery* node, void* data) {
  if (node->is_lateral()) {
    println("LATERAL");
  }
  visitASTChildren(node, data);
}

void Unparser::visitASTJoin(const ASTJoin* node, void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  node->child(0)->Accept(this, data);

  if (node->join_type() == ASTJoin::COMMA) {
    print(",");
  } else {
    println();
    if (node->natural()) {
      print("NATURAL");
    }
    print(node->GetSQLForJoinType());
    print(node->GetSQLForJoinHint());

    print("JOIN");
  }
  println();

  // This will print hints, the rhs, and the ON or USING clause.
  for (int i = 1; i < node->num_children(); i++) {
    node->child(i)->Accept(this, data);
  }
}

void Unparser::visitASTParenthesizedJoin(const ASTParenthesizedJoin* node,
                                         void* data) {
  println();
  println("(");
  {
    Formatter::Indenter indenter(&formatter_);
    node->join()->Accept(this, data);
  }
  println();
  print(")");

  for (const auto* op : node->postfix_operators()) {
    op->Accept(this, data);
  }
}

void Unparser::visitASTOnClause(const ASTOnClause* node, void* data) {
  println();
  print("ON");
  {
    Formatter::Indenter indenter(&formatter_);
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTOnOrUsingClauseList(const ASTOnOrUsingClauseList* node,
                                           void* data) {
  for (const ASTNode* clause : node->on_or_using_clause_list()) {
    clause->Accept(this, data);
    println();
  }
}

void Unparser::visitASTUsingClause(const ASTUsingClause* node, void* data) {
  println();
  print("USING(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTWhereClause(const ASTWhereClause* node, void* data) {
  println();
  println("WHERE");
  {
    Formatter::Indenter indenter(&formatter_);
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTRollup(const ASTRollup* node, void* data) {
  print("ROLLUP(");
  UnparseVectorWithSeparator(node->expressions(), data, ",");
  print(")");
}

void Unparser::visitASTCube(const ASTCube* node, void* data) {
  print("CUBE(");
  UnparseVectorWithSeparator(node->expressions(), data, ",");
  print(")");
}

void Unparser::visitASTGroupingSet(const ASTGroupingSet* node, void* data) {
  ABSL_DCHECK_LE((node->expression() != nullptr) + (node->rollup() != nullptr) +
                (node->cube() != nullptr),
            1)
      << "at most one of expressions, rollup, and cube can exist";

  if (node->expression() != nullptr) {
    node->expression()->Accept(this, data);
  } else if (node->rollup() != nullptr) {
    node->rollup()->Accept(this, data);
  } else if (node->cube() != nullptr) {
    node->cube()->Accept(this, data);
  } else {
    // Indicate this is an empty grouping set.
    print("()");
  }
}

void Unparser::visitASTGroupingSetList(const ASTGroupingSetList* node,
                                       void* data) {
  print("GROUPING SETS(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->grouping_sets(), data, ",");
  }
  print(")");
}

void Unparser::visitASTGroupingItemOrder(const ASTGroupingItemOrder* node,
                                         void* data) {
  switch (node->ordering_spec()) {
    case ASTOrderingExpression::ASC:
      print("ASC");
      break;
    case ASTOrderingExpression::DESC:
      print("DESC");
      break;
    default:
      break;
  }
  visitASTChildren(node, data);
}

void Unparser::visitASTGroupingItem(const ASTGroupingItem* node, void* data) {
  ABSL_DCHECK_LE((node->expression() != nullptr) + (node->rollup() != nullptr) +
                (node->cube() != nullptr) +
                (node->grouping_set_list() != nullptr),
            1)
      << "At most one of expression, rollup, cube, and grouping_set_list exist";
  if (node->expression() != nullptr) {
    node->expression()->Accept(this, data);
    if (node->alias() != nullptr) {
      node->alias()->Accept(this, data);
    }
    if (node->grouping_item_order() != nullptr) {
      node->grouping_item_order()->Accept(this, data);
    }
  } else if (node->rollup() != nullptr) {
    node->rollup()->Accept(this, data);
  } else if (node->cube() != nullptr) {
    node->cube()->Accept(this, data);
  } else if (node->grouping_set_list() != nullptr) {
    node->grouping_set_list()->Accept(this, data);
  } else {
    // This is an empty grouping item ()
    print("()");
  }
}

void Unparser::visitASTGroupBy(const ASTGroupBy* node, void* data) {
  println();
  print("GROUP");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->and_order_by()) {
    print("AND ORDER");
  }
  print("BY");
  if (node->all() != nullptr) {
    node->all()->Accept(this, data);
  } else {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->grouping_items(), data, ",");
  }
}

void Unparser::visitASTGroupByAll(const ASTGroupByAll* node, void* data) {
  print("ALL");
}

void Unparser::visitASTHaving(const ASTHaving* node, void* data) {
  println();
  print("HAVING");
  visitASTChildren(node, data);
}

void Unparser::visitASTQualify(const ASTQualify* node, void* data) {
  println();
  print("QUALIFY");
  visitASTChildren(node, data);
}

void Unparser::visitASTCollate(const ASTCollate* node, void* data) {
  print("COLLATE");
  visitASTChildren(node, data);
}

void Unparser::visitASTOrderBy(const ASTOrderBy* node, void* data) {
  println();
  print("ORDER");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  print("BY");
  UnparseVectorWithSeparator(node->ordering_expressions(), data, ",");
}

void Unparser::visitASTNullOrder(const ASTNullOrder* node, void* data) {
  if (node->nulls_first()) {
    print("NULLS FIRST");
  } else {
    print("NULLS LAST");
  }
}

void Unparser::visitASTOrderingExpression(const ASTOrderingExpression* node,
                                          void* data) {
  node->expression()->Accept(this, data);
  if (node->collate()) node->collate()->Accept(this, data);
  if (node->descending()) {
    print("DESC");
  } else if (node->ordering_spec() == ASTOrderingExpression::ASC &&
             absl::GetFlag(FLAGS_output_asc_explicitly)) {
    print("ASC");
  }
  if (node->null_order()) node->null_order()->Accept(this, data);
  if (node->option_list()) {
    print("OPTIONS");
    node->option_list()->Accept(this, data);
  }
}

void Unparser::visitASTLimitOffset(const ASTLimitOffset* node, void* data) {
  println();
  visitASTLimit(node->limit(), data);
  if (node->offset() != nullptr) {
    print("OFFSET");
    node->offset()->Accept(this, data);
  }
}

void Unparser::visitASTLimit(const ASTLimit* node, void* data) {
  print("LIMIT");
  visitASTChildren(node, data);
}

void Unparser::visitASTLimitAll(const ASTLimitAll* node, void* data) {
  print("ALL");
}

void Unparser::visitASTHavingModifier(const ASTHavingModifier* node,
                                      void* data) {
  println();
  print("HAVING ");
  if (node->modifier_kind() == ASTHavingModifier::ModifierKind::MAX) {
    print("MAX");
  } else {
    print("MIN");
  }
  node->expr()->Accept(this, data);
}

void Unparser::visitASTClampedBetweenModifier(
    const ASTClampedBetweenModifier* node, void* data) {
  println();
  print("CLAMPED BETWEEN");
  UnparseChildrenWithSeparator(node, data, 0, node->num_children(), "AND");
}

void Unparser::visitASTWithReportModifier(const ASTWithReportModifier* node,
                                          void* data) {
  println();
  print("WITH REPORT");
  if (node->options_list() != nullptr) {
    node->options_list()->Accept(this, data);
  }
}

void Unparser::UnparseASTTableDataSource(const ASTTableDataSource* node,
                                         void* data) {
  node->path_expr()->Accept(this, data);
  if (node->for_system_time() != nullptr) {
    println();
    Formatter::Indenter indenter(&formatter_);
    node->for_system_time()->Accept(this, data);
  }
  if (node->where_clause() != nullptr) {
    Formatter::Indenter indenter(&formatter_);
    node->where_clause()->Accept(this, data);
  }
  println();
}

void Unparser::visitASTCloneDataSourceList(const ASTCloneDataSourceList* node,
                                           void* data) {
  UnparseChildrenWithSeparator(node, data, "UNION ALL");
}

void Unparser::visitASTCloneDataStatement(const ASTCloneDataStatement* node,
                                          void* data) {
  print("CLONE DATA INTO");
  node->target_path()->Accept(this, data);
  println();
  print("FROM");
  node->data_source_list()->Accept(this, data);
}

void Unparser::visitASTIdentifier(const ASTIdentifier* node, void* data) {
  print(ToIdentifierLiteral(node->GetAsIdString()));
}

void Unparser::visitASTNewConstructorArg(const ASTNewConstructorArg* node,
                                         void* data) {
  node->expression()->Accept(this, data);
  if (node->optional_identifier() != nullptr) {
    print("AS ");
    node->optional_identifier()->Accept(this, data);
  }
  if (node->optional_path_expression() != nullptr) {
    print("AS (");
    node->optional_path_expression()->Accept(this, data);
    print(")");
  }
}

void Unparser::visitASTNewConstructor(const ASTNewConstructor* node,
                                      void* data) {
  print("NEW");
  node->type_name()->Accept(this, data);
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->arguments(), data, ",");
  }
  print(")");
}

void Unparser::visitASTBracedConstructorFieldValue(
    const ASTBracedConstructorFieldValue* node, void* data) {
  print(node->colon_prefixed() ? ": " : " ");
  if (node->expression()) {
    node->expression()->Accept(this, data);
  }
}

void Unparser::visitASTBracedConstructorField(
    const ASTBracedConstructorField* node, void* data) {
  if (node->braced_constructor_lhs()) {
    node->braced_constructor_lhs()->Accept(this, data);
  }
  node->value()->Accept(this, data);
}

void Unparser::visitASTBracedConstructor(const ASTBracedConstructor* node,
                                         void* data) {
  print("{");
  for (auto* field : node->fields()) {
    if (field->comma_separated()) {
      print(",");
    }
    println();
    Formatter::Indenter indenter(&formatter_);
    field->Accept(this, data);
  }
  if (!node->fields().empty()) {
    println();
  }
  print("}");
}

void Unparser::visitASTBracedNewConstructor(const ASTBracedNewConstructor* node,
                                            void* data) {
  print("NEW");
  node->type_name()->Accept(this, data);
  node->braced_constructor()->Accept(this, data);
}

void Unparser::visitASTStructBracedConstructor(
    const ASTStructBracedConstructor* node, void* data) {
  if (node->type_name() != nullptr) {
    node->type_name()->Accept(this, data);
  } else {
    print("STRUCT");
  }
  node->braced_constructor()->Accept(this, data);
}

void Unparser::visitASTExtendedPathExpression(
    const ASTExtendedPathExpression* node, void* data) {
  node->parenthesized_path()->Accept(this, data);
  print(".");
  node->generalized_path_expression()->Accept(this, data);
}

void Unparser::visitASTBracedConstructorLhs(const ASTBracedConstructorLhs* node,
                                            void* data) {
  node->extended_path_expr()->Accept(this, data);
  switch (node->operation()) {
    case googlesql::ASTBracedConstructorLhs::UPDATE_SINGLE:
      break;
    case googlesql::ASTBracedConstructorLhs::UPDATE_MANY:
      print("*");
      break;
    case googlesql::ASTBracedConstructorLhs::UPDATE_SINGLE_NO_CREATION:
      print("?");
      break;
  }
}

void Unparser::visitASTUpdateConstructor(const ASTUpdateConstructor* node,
                                         void* data) {
  node->function()->Accept(this, data);
  node->braced_constructor()->Accept(this, data);
}

void Unparser::visitASTInferredTypeColumnSchema(
    const ASTInferredTypeColumnSchema* node, void* data) {
  UnparseColumnSchema(node, data);
}

void Unparser::visitASTArrayConstructor(const ASTArrayConstructor* node,
                                        void* data) {
  if (node->type() != nullptr) {
    node->type()->Accept(this, data);
  } else {
    print("ARRAY");
  }
  print("[");
  UnparseVectorWithSeparator(node->elements(), data, ",");
  print("]");
}

void Unparser::visitASTStructConstructorArg(const ASTStructConstructorArg* node,
                                            void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTStructConstructorWithParens(
    const ASTStructConstructorWithParens* node, void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->field_expressions(), data, ",");
  }
  print(")");
}

void Unparser::visitASTStructConstructorWithKeyword(
    const ASTStructConstructorWithKeyword* node, void* data) {
  if (node->struct_type() != nullptr) {
    node->struct_type()->Accept(this, data);
  } else {
    print("STRUCT");
  }
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->fields(), data, ",");
  }
  print(")");
}

void Unparser::visitASTIntLiteral(const ASTIntLiteral* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTNumericLiteral(const ASTNumericLiteral* node,
                                      void* data) {
  print("NUMERIC");
  node->string_literal()->Accept(this, data);
}

void Unparser::visitASTBigNumericLiteral(const ASTBigNumericLiteral* node,
                                         void* data) {
  print("BIGNUMERIC");
  node->string_literal()->Accept(this, data);
}

void Unparser::visitASTJSONLiteral(const ASTJSONLiteral* node, void* data) {
  print("JSON");
  node->string_literal()->Accept(this, data);
}

void Unparser::visitASTFloatLiteral(const ASTFloatLiteral* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTStringLiteral(const ASTStringLiteral* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTStringLiteralComponent(
    const ASTStringLiteralComponent* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTBytesLiteral(const ASTBytesLiteral* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTBytesLiteralComponent(
    const ASTBytesLiteralComponent* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTBooleanLiteral(const ASTBooleanLiteral* node,
                                      void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTNullLiteral(const ASTNullLiteral* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTDateOrTimeLiteral(const ASTDateOrTimeLiteral* node,
                                         void* data) {
  print(Type::TypeKindToString(node->type_kind(), PRODUCT_INTERNAL));
  UnparseChildrenWithSeparator(node, data, "");
}

void Unparser::visitASTRangeColumnSchema(const ASTRangeColumnSchema* node,
                                         void* data) {
  print("RANGE<");
  node->element_schema()->Accept(this, data);
  print(">");
  UnparseColumnSchema(node, data);
}

void Unparser::visitASTRangeLiteral(const ASTRangeLiteral* node, void* data) {
  node->type()->Accept(this, data);
  node->range_value()->Accept(this, data);
}

void Unparser::visitASTRangeType(const ASTRangeType* node, void* data) {
  print("RANGE<");
  node->element_type()->Accept(this, data);
  print(">");
}

void Unparser::visitASTStar(const ASTStar* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTStarExceptList(const ASTStarExceptList* node,
                                      void* data) {
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTStarReplaceItem(const ASTStarReplaceItem* node,
                                       void* data) {
  UnparseChildrenWithSeparator(node, data, "AS");
}

void Unparser::visitASTStarModifiers(const ASTStarModifiers* node, void* data) {
  if (node->except_list() != nullptr) {
    print("EXCEPT (");
    node->except_list()->Accept(this, data);
    print(")");
  }
  if (!node->replace_items().empty()) {
    print("REPLACE (");
    UnparseVectorWithSeparator(node->replace_items(), data, ",");
    print(")");
  }
}

void Unparser::visitASTStarWithModifiers(const ASTStarWithModifiers* node,
                                         void* data) {
  print("*");
  node->modifiers()->Accept(this, data);
}

void Unparser::visitASTPathExpression(const ASTPathExpression* node,
                                      void* data) {
  PrintOpenParenIfNeeded(node);
  UnparseChildrenWithSeparator(node, data, ".");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTParameterExpr(const ASTParameterExpr* node, void* data) {
  if (node->name() == nullptr) {
    print("?");
  } else {
    print("@");
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTSystemVariableExpr(const ASTSystemVariableExpr* node,
                                          void* data) {
  PrintOpenParenIfNeeded(node);
  print("@@");
  visitASTChildren(node, data);
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTIntervalExpr(const ASTIntervalExpr* node, void* data) {
  print("INTERVAL");
  node->interval_value()->Accept(this, data);
  node->date_part_name()->Accept(this, data);
  if (node->date_part_name_to() != nullptr) {
    print("TO");
    node->date_part_name_to()->Accept(this, data);
  }
}

static bool NeedsInnerParenBeforeDotQualifer(const ASTExpression* node) {
  // We need an inner paren to avoid the dot (.) binding tightly to the inner
  // expression for @@ variables or numbers. Without brackets, the dot turns
  // integers into floats.
  bool non_parenthesized_system_variable =
      node->node_kind() == AST_SYSTEM_VARIABLE_EXPR && !node->parenthesized();
  return non_parenthesized_system_variable ||
         // Literals do not honour the parenthesized flag.
         node->node_kind() == AST_INT_LITERAL ||
         node->node_kind() == AST_FLOAT_LITERAL;
}

void Unparser::visitASTDotIdentifier(const ASTDotIdentifier* node, void* data) {
  // The dot identifier actually can be self-recursive in long expressions like
  // a.b.c.d.e.f, and so needs to be protected here.
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  PrintOpenParenIfNeeded(node);

  bool print_inner_paren = NeedsInnerParenBeforeDotQualifer(node->expr());
  if (print_inner_paren) {
    print("(");
  }
  node->expr()->Accept(this, data);
  if (print_inner_paren) {
    print(")");
  }
  print(".");
  node->name()->Accept(this, data);
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTDotGeneralizedField(const ASTDotGeneralizedField* node,
                                           void* data) {
  PrintOpenParenIfNeeded(node);

  // We need an inner paren to avoid the dot (.) binding tightly to the inner
  // expression for numbers. E.g. `SELECT (1).(x)` should unparse as the same,
  // and not as `SELECT 1.(x)`, which looks like a function call on a float
  // literal.
  bool print_inner_paren = node->expr()->node_kind() == AST_INT_LITERAL ||
                           node->expr()->node_kind() == AST_FLOAT_LITERAL;
  if (print_inner_paren) {
    print("(");
  }
  node->expr()->Accept(this, data);
  if (print_inner_paren) {
    print(")");
  }
  print(".(");
  node->path()->Accept(this, data);
  print(")");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTDotStar(const ASTDotStar* node, void* data) {
  bool print_inner_paren = NeedsInnerParenBeforeDotQualifer(node->expr());
  if (print_inner_paren) {
    print("(");
  }
  node->expr()->Accept(this, data);
  if (print_inner_paren) {
    print(")");
  }
  print(".*");
}

void Unparser::visitASTDotStarWithModifiers(const ASTDotStarWithModifiers* node,
                                            void* data) {
  bool print_inner_paren = NeedsInnerParenBeforeDotQualifer(node->expr());
  if (print_inner_paren) {
    print("(");
  }
  node->expr()->Accept(this, data);
  if (print_inner_paren) {
    print(")");
  }
  print(".*");
  node->modifiers()->Accept(this, data);
}

void Unparser::visitASTOrExpr(const ASTOrExpr* node, void* data) {
  PrintOpenParenIfNeeded(node);
  UnparseChildrenWithSeparator(node, data, "OR");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTAndExpr(const ASTAndExpr* node, void* data) {
  PrintOpenParenIfNeeded(node);
  UnparseChildrenWithSeparator(node, data, "AND");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTConcatExpr(const ASTConcatExpr* node, void* data) {
  PrintOpenParenIfNeeded(node);
  UnparseChildrenWithSeparator(node, data, "||");
  PrintCloseParenIfNeeded(node);
}

static bool IsPostfix(ASTUnaryExpression::Op op) {
  return op == ASTUnaryExpression::IS_UNKNOWN ||
         op == ASTUnaryExpression::IS_NOT_UNKNOWN;
}

void Unparser::visitASTUnaryExpression(const ASTUnaryExpression* node,
                                       void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }

  PrintOpenParenIfNeeded(node);
  if (IsPostfix(node->op())) {
    node->operand()->Accept(this, data);
    formatter_.AddUnary(node->GetSQLForOperator());
  } else {
    formatter_.AddUnary(node->GetSQLForOperator());
    node->operand()->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTFormatClause(const ASTFormatClause* node, void* data) {
  print("FORMAT");
  node->format()->Accept(this, data);
  if (node->time_zone_expr() != nullptr) {
    print("AT TIME ZONE");
    node->time_zone_expr()->Accept(this, data);
  }
}

void Unparser::visitASTCastExpression(const ASTCastExpression* node,
                                      void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  print(node->is_safe_cast() ? "SAFE_CAST(" : "CAST(");
  node->expr()->Accept(this, data);
  print("AS");
  node->type()->Accept(this, data);
  if (node->format()) {
    node->format()->Accept(this, data);
  }
  print(")");
}

void Unparser::visitASTExtractExpression(const ASTExtractExpression* node,
                                         void* data) {
  print("EXTRACT(");
  node->lhs_expr()->Accept(this, data);
  print("FROM");
  node->rhs_expr()->Accept(this, data);
  if (node->time_zone_expr() != nullptr) {
    print("AT TIME ZONE");
    node->time_zone_expr()->Accept(this, data);
  }
  print(")");
}

void Unparser::visitASTCaseNoValueExpression(
    const ASTCaseNoValueExpression* node, void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }

  println();
  print("CASE");
  int i;
  {
    Formatter::Indenter indenter(&formatter_);
    for (i = 0; i < node->num_children() - 1; i += 2) {
      println();
      print("WHEN");
      node->child(i)->Accept(this, data);
      print("THEN");
      node->child(i + 1)->Accept(this, data);
    }
    if (i < node->num_children()) {
      println();
      print("ELSE");
      node->child(i)->Accept(this, data);
    }
  }
  println();
  print("END");
}

void Unparser::visitASTCaseValueExpression(const ASTCaseValueExpression* node,
                                           void* data) {
  print("CASE");
  node->child(0)->Accept(this, data);
  int i;
  {
    Formatter::Indenter indenter(&formatter_);
    for (i = 1; i < node->num_children() - 1; i += 2) {
      println();
      print("WHEN");
      node->child(i)->Accept(this, data);
      print("THEN");
      node->child(i + 1)->Accept(this, data);
    }
    if (i < node->num_children()) {
      println();
      print("ELSE");
      node->child(i)->Accept(this, data);
    }
  }
  println();
  print("END");
}

void Unparser::visitASTBinaryExpression(const ASTBinaryExpression* node,
                                        void* data) {
  PrintOpenParenIfNeeded(node);
  UnparseChildrenWithSeparator(node, data, node->GetSQLForOperator());
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTBitwiseShiftExpression(
    const ASTBitwiseShiftExpression* node, void* data) {
  PrintOpenParenIfNeeded(node);
  node->child(0)->Accept(this, data);
  print(node->is_left_shift() ? "<<" : ">>");
  node->child(2)->Accept(this, data);
  PrintCloseParenIfNeeded(node);
}

struct Parens {
  std::string left = "(";
  std::string right = ")";
  static Parens GetParensOrBraces(const ASTQuery* query) {
    Parens parens;
    const auto node_kind = query->query_expr()->node_kind();
    if (node_kind == AST_GQL_QUERY ||
        node_kind == AST_GQL_GRAPH_PATTERN_QUERY ||
        node_kind == AST_GQL_LINEAR_OPS_QUERY) {
      parens.left = "{";
      parens.right = "}";
    }
    return parens;
  }
};

void Unparser::visitASTInExpression(const ASTInExpression* node, void* data) {
  PrintOpenParenIfNeeded(node);
  node->lhs()->Accept(this, data);
  print(absl::StrCat(node->is_not() ? "NOT " : "", "IN"));
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    Parens parens;
    parens = Parens::GetParensOrBraces(node->query());
    print(parens.left);
    {
      Formatter::Indenter indenter(&formatter_);
      node->query()->Accept(this, data);
    }
    print(parens.right);
  }
  if (node->in_list() != nullptr) {
    node->in_list()->Accept(this, data);
  }
  if (node->unnest_expr() != nullptr) {
    node->unnest_expr()->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTInList(const ASTInList* node, void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTLikeExpression(const ASTLikeExpression* node,
                                      void* data) {
  PrintOpenParenIfNeeded(node);
  node->lhs()->Accept(this, data);
  print(absl::StrCat(node->is_not() ? "NOT " : "", "LIKE"));
  node->op()->Accept(this, data);
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    print("(");
    {
      Formatter::Indenter indenter(&formatter_);
      node->query()->Accept(this, data);
    }
    print(")");
  }
  if (node->in_list() != nullptr) {
    node->in_list()->Accept(this, data);
  }
  if (node->unnest_expr() != nullptr) {
    node->unnest_expr()->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTAnySomeAllOp(const ASTAnySomeAllOp* node, void* data) {
  switch (node->op()) {
    case ASTAnySomeAllOp::kUninitialized:
      print("UNINITIALIZED");
      break;
    case ASTAnySomeAllOp::kAny:
      print("ANY");
      break;
    case ASTAnySomeAllOp::kSome:
      print("SOME");
      break;
    case ASTAnySomeAllOp::kAll:
      print("ALL");
      break;
  }
}

void Unparser::visitASTQuantifiedComparisonExpression(
    const ASTQuantifiedComparisonExpression* node, void* data) {
  PrintOpenParenIfNeeded(node);
  node->lhs()->Accept(this, data);
  print(node->GetSQLForOperator());
  node->quantifier()->Accept(this, data);
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    print("(");
    {
      Formatter::Indenter indenter(&formatter_);
      node->query()->Accept(this, data);
    }
    print(")");
  }
  if (node->in_list() != nullptr) {
    node->in_list()->Accept(this, data);
  }
  if (node->unnest_expr() != nullptr) {
    node->unnest_expr()->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTBetweenExpression(const ASTBetweenExpression* node,
                                         void* data) {
  PrintOpenParenIfNeeded(node);
  node->child(0)->Accept(this, data);
  print(absl::StrCat(node->is_not() ? "NOT " : "", "BETWEEN"));
  UnparseChildrenWithSeparator(node, data, 2, node->num_children(), "AND");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTExpressionWithAlias(const ASTExpressionWithAlias* node,
                                           void* data) {
  node->expression()->Accept(this, data);
  node->alias()->Accept(this, data);
}

void Unparser::visitASTFunctionCall(const ASTFunctionCall* node, void* data) {
  PrintOpenParenIfNeeded(node);
  absl::Span<const ASTExpression* const> arguments = node->arguments();
  if (node->is_chained_call() && !arguments.empty()) {
    const ASTExpression* base_expr = arguments[0];
    arguments = arguments.subspan(1);

    // Int and float literals don't work before chained calls without
    // using parentheses.  This avoids weirdness with the dots.
    bool force_paren = (base_expr->node_kind() == AST_INT_LITERAL ||
                        base_expr->node_kind() == AST_FLOAT_LITERAL);

    if (force_paren) print("(");
    base_expr->Accept(this, data);
    if (force_paren) print(")");
    print(".");
  }
  if (node->is_chained_call() && node->function()->num_names() > 1) {
    // In a chained function call, when calling a function with a multi-part
    // name, we use generalized path syntax. e.g. "(-1).(safe.sqrt)()",
    // instead of "(-1).sqrt()".
    print("(");
    node->function()->Accept(this, data);
    print(")");
  } else {
    node->function()->Accept(this, data);
  }
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    if (node->distinct()) print("DISTINCT");
    UnparseVectorWithSeparator(arguments, data, ",");
    switch (node->null_handling_modifier()) {
      case ASTFunctionCall::DEFAULT_NULL_HANDLING:
        break;
      case ASTFunctionCall::IGNORE_NULLS:
        print("IGNORE NULLS");
        break;
      case ASTFunctionCall::RESPECT_NULLS:
        print("RESPECT NULLS");
        break;
        // No "default:". Let the compilation fail in case an entry is added to
        // the enum without being handled here.
    }
    if (node->where_expr() != nullptr) {
      node->where_expr()->Accept(this, data);
    }
    if (node->having_modifier() != nullptr) {
      node->having_modifier()->Accept(this, data);
    }
    if (node->group_by() != nullptr) {
      node->group_by()->Accept(this, data);
    }
    if (node->having_expr() != nullptr) {
      node->having_expr()->Accept(this, data);
    }
    if (node->clamped_between_modifier() != nullptr) {
      node->clamped_between_modifier()->Accept(this, data);
    }
    if (node->with_report_modifier() != nullptr) {
      node->with_report_modifier()->Accept(this, data);
    }
    if (node->order_by() != nullptr) {
      node->order_by()->Accept(this, data);
    }
    if (node->limit_offset() != nullptr) {
      node->limit_offset()->Accept(this, data);
    }
  }
  print(")");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTChainedBaseExpr(const ASTChainedBaseExpr* node,
                                       void* data) {
  // Int and float literals don't work before chained calls without
  // using parentheses.  This avoids weirdness with the dots.
  bool force_paren = (node->expr()->node_kind() == AST_INT_LITERAL ||
                      node->expr()->node_kind() == AST_FLOAT_LITERAL);

  if (force_paren) print("(");
  node->expr()->Accept(this, data);
  if (force_paren) print(")");
}

void Unparser::visitASTArrayElement(const ASTArrayElement* node, void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  PrintOpenParenIfNeeded(node);
  node->array()->Accept(this, data);
  print("[");
  node->position()->Accept(this, data);
  print("]");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTExpressionSubquery(const ASTExpressionSubquery* node,
                                          void* data) {
  print(ASTExpressionSubquery::ModifierToString(node->modifier()));
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  Parens parens;
  parens = Parens::GetParensOrBraces(node->query());
  print(parens.left);
  {
    Formatter::Indenter indenter(&formatter_);
    node->query()->Accept(this, data);
  }
  print(parens.right);
}

void Unparser::visitASTHint(const ASTHint* node, void* data) {
  if (node->num_shards_hint() != nullptr) {
    print("@");
    node->num_shards_hint()->Accept(this, data);
  }

  if (!node->hint_entries().empty()) {
    print("@{");
    UnparseVectorWithSeparator(node->hint_entries(), data, ",");
    print("}");
  }
}

void Unparser::visitASTHintEntry(const ASTHintEntry* node, void* data) {
  if (node->qualifier() != nullptr) {
    node->qualifier()->Accept(this, data);
    print(".");
  }
  node->name()->Accept(this, data);
  print("=");
  node->value()->Accept(this, data);
}

void Unparser::visitASTOptionsList(const ASTOptionsList* node, void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTOptionsEntry(const ASTOptionsEntry* node, void* data) {
  UnparseChildrenWithSeparator(node, data, node->GetSQLForOperator());
}

void Unparser::visitASTMaxLiteral(const ASTMaxLiteral* node, void* data) {
  print("MAX");
}

void Unparser::visitASTTypeParameterList(const ASTTypeParameterList* node,
                                         void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTSimpleType(const ASTSimpleType* node, void* data) {
  const ASTPathExpression* type_name = node->type_name();
  // 'INTERVAL' is a reserved keyword, but when it is used as a type name, we
  // want to print it without backticks.
  if (type_name->num_names() == 1 &&
      googlesql_base::CaseEqual(type_name->first_name()->GetAsString(),
                             "interval")) {
    print(type_name->first_name()->GetAsStringView());
  } else {
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTArrayType(const ASTArrayType* node, void* data) {
  print("ARRAY<");
  node->element_type()->Accept(this, data);
  print(">");
  if (node->type_parameters() != nullptr) {
    node->type_parameters()->Accept(this, data);
  }

  if (node->collate() != nullptr) {
    node->collate()->Accept(this, data);
  }
}

void Unparser::visitASTStructType(const ASTStructType* node, void* data) {
  print("STRUCT<");
  UnparseVectorWithSeparator(node->struct_fields(), data, ",");
  print(">");
  if (node->type_parameters() != nullptr) {
    node->type_parameters()->Accept(this, data);
  }

  if (node->collate() != nullptr) {
    node->collate()->Accept(this, data);
  }
}

void Unparser::visitASTStructField(const ASTStructField* node, void* data) {
  UnparseChildrenWithSeparator(node, data, "");
}

void Unparser::visitASTFunctionType(const ASTFunctionType* node, void* data) {
  print("FUNCTION<(");
  node->arg_list()->Accept(this, data);
  print(") ->");
  node->return_type()->Accept(this, data);
  print(">");

  if (node->type_parameters() != nullptr) {
    node->type_parameters()->Accept(this, data);
  }

  if (node->collate() != nullptr) {
    node->collate()->Accept(this, data);
  }
}

void Unparser::visitASTFunctionTypeArgList(const ASTFunctionTypeArgList* node,
                                           void* data) {
  UnparseVectorWithSeparator(node->args(), data, ",");
}

void Unparser::visitASTSimpleColumnSchema(const ASTSimpleColumnSchema* node,
                                          void* data) {
  const ASTPathExpression* type_name = node->type_name();
  // 'INTERVAL' is a reserved keyword, but when it is used as a type name, we
  // want to print it without backticks.
  if (type_name->num_names() == 1 &&
      googlesql_base::CaseEqual(type_name->first_name()->GetAsString(),
                             "interval")) {
    print(type_name->first_name()->GetAsString());
  } else {
    type_name->Accept(this, data);
    UnparseColumnSchema(node, data);
  }
}

void Unparser::visitASTArrayColumnSchema(const ASTArrayColumnSchema* node,
                                         void* data) {
  print("ARRAY<");
  node->element_schema()->Accept(this, data);
  print(">");
  UnparseColumnSchema(node, data);
}

void Unparser::visitASTMapColumnSchema(const ASTMapColumnSchema* node,
                                       void* data) {
  print("MAP<");
  node->key_schema()->Accept(this, data);
  print(",");
  node->value_schema()->Accept(this, data);
  print(">");
  UnparseColumnSchema(node, data);
}

void Unparser::visitASTStructColumnSchema(const ASTStructColumnSchema* node,
                                          void* data) {
  print("STRUCT<");
  UnparseVectorWithSeparator(node->struct_fields(), data, ",");
  print(">");
  UnparseColumnSchema(node, data);
}

void Unparser::visitASTIdentityColumnStartWith(
    const ASTIdentityColumnStartWith* node, void* data) {
  ABSL_DCHECK(node->value() != nullptr);
  node->value()->Accept(this, data);
}

void Unparser::visitASTIdentityColumnIncrementBy(
    const ASTIdentityColumnIncrementBy* node, void* data) {
  ABSL_DCHECK(node->value() != nullptr);
  node->value()->Accept(this, data);
}

void Unparser::visitASTIdentityColumnMaxValue(
    const ASTIdentityColumnMaxValue* node, void* data) {
  ABSL_DCHECK(node->value() != nullptr);
  node->value()->Accept(this, data);
}

void Unparser::visitASTIdentityColumnMinValue(
    const ASTIdentityColumnMinValue* node, void* data) {
  ABSL_DCHECK(node->value() != nullptr);
  node->value()->Accept(this, data);
}

void Unparser::visitASTIdentityColumnInfo(const ASTIdentityColumnInfo* node,
                                          void* data) {
  print("IDENTITY(");
  if (node->start_with_value() != nullptr) {
    print("START WITH ");
    node->start_with_value()->Accept(this, data);
  }
  if (node->increment_by_value() != nullptr) {
    print("INCREMENT BY ");
    node->increment_by_value()->Accept(this, data);
  }
  if (node->max_value() != nullptr) {
    print("MAXVALUE ");
    node->max_value()->Accept(this, data);
  }
  if (node->min_value() != nullptr) {
    print("MINVALUE ");
    node->min_value()->Accept(this, data);
  }
  if (node->cycling_enabled()) {
    print("CYCLE");
  }
  print(")");
}

void Unparser::visitASTGeneratedColumnInfo(const ASTGeneratedColumnInfo* node,
                                           void* data) {
  print("GENERATED");
  print(node->GetSqlForGeneratedMode());
  print("AS ");
  if (node->expression() != nullptr) {
    ABSL_DCHECK(node->identity_column_info() == nullptr);
    print("(");
    node->expression()->Accept(this, data);
    print(")");
  } else {
    ABSL_DCHECK(node->identity_column_info() != nullptr);
    node->identity_column_info()->Accept(this, data);
  }
  print(node->GetSqlForStoredMode());
}

void Unparser::visitASTStructColumnField(const ASTStructColumnField* node,
                                         void* data) {
  UnparseChildrenWithSeparator(node, data, "");
}

void Unparser::UnparseColumnSchema(const ASTColumnSchema* node, void* data) {
  if (node->type_parameters() != nullptr) {
    node->type_parameters()->Accept(this, data);
  }
  if (node->collate() != nullptr) {
    visitASTCollate(node->collate(), data);
  }
  if (node->generated_column_info() != nullptr) {
    node->generated_column_info()->Accept(this, data);
  }
  if (node->default_expression() != nullptr) {
    print("DEFAULT ");
    node->default_expression()->Accept(this, data);
  }
  if (node->attributes() != nullptr) {
    node->attributes()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTAnalyticFunctionCall(const ASTAnalyticFunctionCall* node,
                                            void* data) {
  PrintOpenParenIfNeeded(node);
  if (node->function() != nullptr) {
    node->function()->Accept(this, data);
  }
  print("OVER (");
  {
    Formatter::Indenter indenter(&formatter_);
    node->window_spec()->Accept(this, data);
  }
  print(")");
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTWindowClause(const ASTWindowClause* node, void* data) {
  println();
  print("WINDOW");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->windows(), data, ",");
  }
}

void Unparser::visitASTWindowDefinition(const ASTWindowDefinition* node,
                                        void* data) {
  node->name()->Accept(this, data);
  print("AS (");
  node->window_spec()->Accept(this, data);
  print(")");
}

void Unparser::visitASTWindowSpecification(const ASTWindowSpecification* node,
                                           void* data) {
  UnparseChildrenWithSeparator(node, data, "");
}

void Unparser::visitASTPartitionBy(const ASTPartitionBy* node, void* data) {
  print("PARTITION");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  print("BY");
  UnparseVectorWithSeparator(node->partitioning_expressions(), data, ",");
}

void Unparser::visitASTClusterBy(const ASTClusterBy* node, void* data) {
  print("CLUSTER BY");
  UnparseVectorWithSeparator(node->clustering_expressions(), data, ",");
}

void Unparser::visitASTWindowFrame(const ASTWindowFrame* node, void* data) {
  print(node->GetFrameUnitString());
  if (nullptr != node->end_expr()) {
    print("BETWEEN");
  }
  node->start_expr()->Accept(this, data);
  if (nullptr != node->end_expr()) {
    print("AND");
    node->end_expr()->Accept(this, data);
  }
}

void Unparser::visitASTWindowFrameExpr(const ASTWindowFrameExpr* node,
                                       void* data) {
  switch (node->boundary_type()) {
    case ASTWindowFrameExpr::UNBOUNDED_PRECEDING:
    case ASTWindowFrameExpr::CURRENT_ROW:
    case ASTWindowFrameExpr::UNBOUNDED_FOLLOWING:
      print(node->GetBoundaryTypeString());
      break;
    case ASTWindowFrameExpr::OFFSET_PRECEDING:
      node->expression()->Accept(this, data);
      print("PRECEDING");
      break;
    case ASTWindowFrameExpr::OFFSET_FOLLOWING:
      node->expression()->Accept(this, data);
      print("FOLLOWING");
      break;
  }
}

void Unparser::visitASTDefaultLiteral(const ASTDefaultLiteral* node,
                                      void* data) {
  print("DEFAULT");
}

void Unparser::visitASTAnalyzeStatement(const ASTAnalyzeStatement* node,
                                        void* data) {
  println();
  print("ANALYZE");
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
  }
  if (node->table_and_column_info_list() != nullptr) {
    Formatter::Indenter indenter(&formatter_);
    node->table_and_column_info_list()->Accept(this, data);
  }
}

void Unparser::visitASTTableAndColumnInfo(const ASTTableAndColumnInfo* node,
                                          void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTTableAndColumnInfoList(
    const ASTTableAndColumnInfoList* node, void* data) {
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTAssertStatement(const ASTAssertStatement* node,
                                       void* data) {
  print("ASSERT");
  node->expr()->Accept(this, data);
  if (node->description() != nullptr) {
    print("AS");
    node->description()->Accept(this, data);
  }
}

void Unparser::visitASTAssertRowsModified(const ASTAssertRowsModified* node,
                                          void* data) {
  println();
  print("ASSERT_ROWS_MODIFIED");
  visitASTChildren(node, data);
}

void Unparser::visitASTReturningClause(const ASTReturningClause* node,
                                       void* data) {
  println();
  print("THEN RETURN");
  if (node->action_alias() != nullptr) {
    print("WITH ACTION");
    print(absl::StrCat("AS ", node->action_alias()->GetAsStringView()));
  }
  node->select_list()->Accept(this, data);
}

void Unparser::visitASTDeleteStatement(const ASTDeleteStatement* node,
                                       void* data) {
  println();
  print("DELETE");
  // GetTargetPathForNested() is strictly more general than "ForNonNested()".
  node->GetTargetPathForNested()->Accept(this, data);
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  if (node->offset() != nullptr) {
    node->offset()->Accept(this, data);
  }
  if (node->where() != nullptr) {
    println();
    println("WHERE");
    {
      Formatter::Indenter indenter(&formatter_);
      node->where()->Accept(this, data);
    }
  }
  if (node->assert_rows_modified() != nullptr) {
    node->assert_rows_modified()->Accept(this, data);
  }
  if (node->returning() != nullptr) {
    node->returning()->Accept(this, data);
  }
}

void Unparser::visitASTColumnList(const ASTColumnList* node, void* data) {
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTInsertValuesRow(const ASTInsertValuesRow* node,
                                       void* data) {
  println();
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
  print(")");
}

void Unparser::visitASTInsertValuesRowList(const ASTInsertValuesRowList* node,
                                           void* data) {
  print("VALUES");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",");
  }
}

void Unparser::visitASTInsertStatement(const ASTInsertStatement* node,
                                       void* data) {
  println();
  print("INSERT");
  if (node->insert_mode() != ASTInsertStatement::DEFAULT_MODE) {
    print("OR");
    print(node->GetSQLForInsertMode());
  }
  print("INTO");
  // GetTargetPathForNested() is strictly more general than "ForNonNested()".
  node->GetTargetPathForNested()->Accept(this, data);

  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }

  if (node->column_list() != nullptr) {
    node->column_list()->Accept(this, data);
  }

  println();

  if (node->rows() != nullptr) {
    node->rows()->Accept(this, data);
  }

  if (node->query() != nullptr) {
    if (node->on_conflict() != nullptr) {
      print("(");
      node->query()->Accept(this, data);
      print(")");
    } else {
      node->query()->Accept(this, data);
    }
  }

  if (node->on_conflict() != nullptr) {
    node->on_conflict()->Accept(this, data);
  }

  if (node->assert_rows_modified() != nullptr) {
    node->assert_rows_modified()->Accept(this, data);
  }

  if (node->returning() != nullptr) {
    node->returning()->Accept(this, data);
  }
}

void Unparser::visitASTOnConflictClause(const ASTOnConflictClause* node,
                                        void* data) {
  println();
  print("ON CONFLICT");
  if (node->conflict_target() != nullptr) {
    {
      Formatter::Indenter indenter(&formatter_);
      node->conflict_target()->Accept(this, data);
    }
  } else if (node->unique_constraint_name() != nullptr) {
    print("ON UNIQUE CONSTRAINT");
    node->unique_constraint_name()->Accept(this, data);
  }

  print("DO");
  print(node->GetSQLForConflictAction());
  if (node->conflict_action() == ASTOnConflictClause::UPDATE) {
    println();
    print("SET");
  }

  if (node->update_item_list() != nullptr) {
    node->update_item_list()->Accept(this, data);
  }

  if (node->update_where_clause() != nullptr) {
    println();
    print("WHERE");
    node->update_where_clause()->Accept(this, data);
  }
}

void Unparser::visitASTUpdateSetValue(const ASTUpdateSetValue* node,
                                      void* data) {
  UnparseChildrenWithSeparator(node, data, "=");
}

void Unparser::visitASTUpdateItem(const ASTUpdateItem* node, void* data) {
  // If we don't have set_value, we have one of the statement types, which
  // require parentheses.
  if (node->set_value() == nullptr) {
    println();
    println("(");
    {
      Formatter::Indenter indenter(&formatter_);
      visitASTChildren(node, data);
    }
    println();
    print(")");
  } else {
    visitASTChildren(node, data);
  }
}

void Unparser::visitASTUpdateItemList(const ASTUpdateItemList* node,
                                      void* data) {
  UnparseChildrenWithSeparator(node, data, ",", true /* break_line */);
}

void Unparser::visitASTUpdateStatement(const ASTUpdateStatement* node,
                                       void* data) {
  println();
  print("UPDATE");
  // GetTargetPathForNested() is strictly more general than "ForNonNested()".
  node->GetTargetPathForNested()->Accept(this, data);
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  if (node->offset() != nullptr) {
    node->offset()->Accept(this, data);
  }
  println();
  println("SET");
  {
    Formatter::Indenter indenter(&formatter_);
    node->update_item_list()->Accept(this, data);
  }
  if (node->from_clause() != nullptr) {
    node->from_clause()->Accept(this, data);
  }
  if (node->where() != nullptr) {
    println();
    println("WHERE");
    {
      Formatter::Indenter indenter(&formatter_);
      node->where()->Accept(this, data);
    }
  }
  if (node->assert_rows_modified() != nullptr) {
    node->assert_rows_modified()->Accept(this, data);
  }
  if (node->returning() != nullptr) {
    node->returning()->Accept(this, data);
  }
}

void Unparser::visitASTTruncateStatement(const ASTTruncateStatement* node,
                                         void* data) {
  println();
  print("TRUNCATE TABLE");

  node->target_path()->Accept(this, data);

  if (node->where() != nullptr) {
    println();
    println("WHERE");
    {
      Formatter::Indenter indenter(&formatter_);
      node->where()->Accept(this, data);
    }
  }
}

void Unparser::visitASTMergeAction(const ASTMergeAction* node, void* data) {
  println();
  switch (node->action_type()) {
    case ASTMergeAction::INSERT:
      print("INSERT");
      if (node->insert_column_list() != nullptr) {
        node->insert_column_list()->Accept(this, data);
      }
      println();
      ABSL_DCHECK(node->insert_row() != nullptr);
      if (!node->insert_row()->values().empty()) {
        println("VALUES");
        {
          Formatter::Indenter indenter(&formatter_);
          node->insert_row()->Accept(this, data);
        }
      } else {
        println("ROW");
      }
      break;
    case ASTMergeAction::UPDATE:
      print("UPDATE");
      println();
      println("SET");
      {
        Formatter::Indenter indenter(&formatter_);
        node->update_item_list()->Accept(this, data);
      }
      break;
    case ASTMergeAction::DELETE:
      print("DELETE");
      break;
    case ASTMergeAction::NOT_SET:
      ABSL_LOG(ERROR) << "Merge clause action type is not set";
  }
}

void Unparser::visitASTMergeWhenClause(const ASTMergeWhenClause* node,
                                       void* data) {
  const ASTMergeAction* action = node->action();
  switch (node->match_type()) {
    case ASTMergeWhenClause::MATCHED:
      print("WHEN MATCHED");
      break;
    case ASTMergeWhenClause::NOT_MATCHED_BY_SOURCE:
      print("WHEN NOT MATCHED BY SOURCE");
      break;
    case ASTMergeWhenClause::NOT_MATCHED_BY_TARGET:
      print("WHEN NOT MATCHED BY TARGET");
      break;
    case ASTMergeWhenClause::NOT_SET:
      ABSL_LOG(ERROR) << "Match type of merge match clause is not set.";
  }
  if (node->search_condition() != nullptr) {
    print("AND");
    node->search_condition()->Accept(this, data);
  }
  print("THEN");
  Formatter::Indenter indenter(&formatter_);
  action->Accept(this, data);
}

void Unparser::visitASTMergeWhenClauseList(const ASTMergeWhenClauseList* node,
                                           void* data) {
  println();
  UnparseChildrenWithSeparator(node, data, "", true /* break_line */);
}

void Unparser::visitASTMergeStatement(const ASTMergeStatement* node,
                                      void* data) {
  println();
  print("MERGE INTO");
  node->target_path()->Accept(this, data);
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  println();
  print("USING");
  node->table_expression()->Accept(this, data);
  println();
  print("ON");
  node->merge_condition()->Accept(this, data);
  node->when_clauses()->Accept(this, data);
}

void Unparser::visitASTPrimaryKeyElement(const ASTPrimaryKeyElement* node,
                                         void* data) {
  node->column()->Accept(this, data);
  if (node->descending()) {
    print("DESC");
  } else if (node->ascending()) {
    print("ASC");
  }
  if (node->null_order() != nullptr) {
    node->null_order()->Accept(this, data);
  }
}

void Unparser::visitASTPrimaryKeyElementList(
    const ASTPrimaryKeyElementList* node, void* data) {
  print("(");
  UnparseChildrenWithSeparator(node, data, ",", false);
  print(")");
}

void Unparser::visitASTPrimaryKey(const ASTPrimaryKey* node, void* data) {
  print("PRIMARY KEY");
  if (node->element_list() == nullptr) {
    print("()");
  } else {
    node->element_list()->Accept(this, data);
  }
  if (!node->enforced()) {
    print("NOT ENFORCED");
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTPrivilege(const ASTPrivilege* node, void* data) {
  node->privilege_action()->Accept(this, data);
  if (node->paths() != nullptr) {
    print("(");
    node->paths()->Accept(this, data);
    print(")");
  }
}

void Unparser::visitASTPrivileges(const ASTPrivileges* node, void* data) {
  if (node->is_all_privileges()) {
    print("ALL PRIVILEGES");
  } else {
    UnparseChildrenWithSeparator(node, data, ",");
  }
}

void Unparser::visitASTGranteeList(const ASTGranteeList* node, void* data) {
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTGrantStatement(const ASTGrantStatement* node,
                                      void* data) {
  print("GRANT");
  node->privileges()->Accept(this, data);
  print("ON");
  // TODO: target_type_parts should be in upper case in unparsing.
  UnparseVectorWithSeparator(node->target_type_parts(), data, "");
  node->target_path()->Accept(this, data);
  print("TO");
  node->grantee_list()->Accept(this, data);
}

void Unparser::visitASTRevokeStatement(const ASTRevokeStatement* node,
                                       void* data) {
  print("REVOKE");
  node->privileges()->Accept(this, data);
  print("ON");
  // TODO: target_type_parts should be in upper case in unparsing.
  UnparseVectorWithSeparator(node->target_type_parts(), data, "");
  node->target_path()->Accept(this, data);
  print("FROM");
  node->grantee_list()->Accept(this, data);
}

void Unparser::visitASTRepeatableClause(const ASTRepeatableClause* node,
                                        void* data) {
  print("REPEATABLE (");
  node->argument()->Accept(this, data);
  print(")");
}

void Unparser::visitASTReplaceFieldsArg(const ASTReplaceFieldsArg* node,
                                        void* data) {
  node->expression()->Accept(this, data);
  print("AS ");
  node->path_expression()->Accept(this, data);
}

void Unparser::visitASTReplaceFieldsExpression(
    const ASTReplaceFieldsExpression* node, void* data) {
  if (!ThreadHasEnoughStack()) {
    println("<Complex nested expression truncated>");
    return;
  }
  print("REPLACE_FIELDS(");
  node->expr()->Accept(this, data);
  print(", ");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->arguments(), data, ",");
  }
  print(")");
}

void Unparser::visitASTFilterFieldsArg(const ASTFilterFieldsArg* node,
                                       void* data) {
  std::string path_expression = Unparse(node->path_expression());
  ABSL_DCHECK_EQ(path_expression.back(), '\n');
  path_expression.pop_back();
  print(absl::StrCat(node->GetSQLForOperator(), path_expression));
}

void Unparser::visitASTPivotExpression(const ASTPivotExpression* node,
                                       void* data) {
  node->expression()->Accept(this, data);
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
}

void Unparser::visitASTPivotExpressionList(const ASTPivotExpressionList* node,
                                           void* data) {
  UnparseChildrenWithSeparator(node, data, ", ");
}

void Unparser::visitASTPivotValue(const ASTPivotValue* node, void* data) {
  node->value()->Accept(this, data);
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
}
void Unparser::visitASTPivotValueList(const ASTPivotValueList* node,
                                      void* data) {
  UnparseChildrenWithSeparator(node, data, ", ");
}

void Unparser::visitASTPivotClause(const ASTPivotClause* node, void* data) {
  print("PIVOT(");
  node->pivot_expressions()->Accept(this, data);
  print("FOR");
  node->for_expression()->Accept(this, data);
  print("IN (");
  node->pivot_values()->Accept(this, data);
  print("))");

  if (node->output_alias() != nullptr) {
    node->output_alias()->Accept(this, data);
  }
}

void Unparser::visitASTUnpivotInItem(const ASTUnpivotInItem* node, void* data) {
  print("(");
  node->unpivot_columns()->Accept(this, data);
  print(")");
  if (node->alias() != nullptr) {
    print("AS");
    node->alias()->Accept(this, data);
  }
}
void Unparser::visitASTUnpivotInItemList(const ASTUnpivotInItemList* node,
                                         void* data) {
  print("(");
  UnparseChildrenWithSeparator(node, data, ", ");
  print(")");
}

void Unparser::visitASTUnpivotInItemLabel(const ASTUnpivotInItemLabel* node,
                                          void* data) {
  node->label()->Accept(this, data);
}

void Unparser::visitASTUnpivotClause(const ASTUnpivotClause* node, void* data) {
  print("UNPIVOT");
  print(node->GetSQLForNullFilter().empty()
            ? ""
            : absl::StrCat(node->GetSQLForNullFilter(), " "));
  print("(");
  if (node->unpivot_output_value_columns()->path_expression_list().size() > 1) {
    print("(");
  }
  node->unpivot_output_value_columns()->Accept(this, data);
  if (node->unpivot_output_value_columns()->path_expression_list().size() > 1) {
    print(")");
  }
  print("FOR");
  node->unpivot_output_name_column()->Accept(this, data);
  print("IN ");
  node->unpivot_in_items()->Accept(this, data);
  print(")");

  if (node->output_alias() != nullptr) {
    node->output_alias()->Accept(this, data);
  }
}

void Unparser::visitASTMatchRecognizeClause(const ASTMatchRecognizeClause* node,
                                            void* data) {
  println("MATCH_RECOGNIZE(");
  {
    Formatter::Indenter indenter(&formatter_);
    if (node->partition_by() != nullptr) {
      node->partition_by()->Accept(this, data);
      println();
    }
    ABSL_DCHECK(node->order_by() != nullptr);
    node->order_by()->Accept(this, data);
    println();

    if (node->measures() != nullptr) {
      print("MEASURES");
      node->measures()->Accept(this, data);
      println();
    }

    if (node->after_match_skip_clause() != nullptr) {
      node->after_match_skip_clause()->Accept(this, data);
      println();
    }

    ABSL_DCHECK(node->pattern() != nullptr);
    print("PATTERN (");
    node->pattern()->Accept(this, data);
    println(")");

    ABSL_DCHECK(node->pattern_variable_definition_list() != nullptr);
    print("DEFINE");

    bool is_first = true;
    for (const auto& var :
         node->pattern_variable_definition_list()->columns()) {
      if (is_first) {
        is_first = false;
      } else {
        print(",");
      }
      // We accept the identifier here rather than alias to prevent "AS" from
      // being incorrectly printed before the alias.
      println();
      Formatter::Indenter indenter(&formatter_);
      var->alias()->identifier()->Accept(this, data);
      print("AS");
      var->expression()->Accept(this, data);
    }
    println();
    if (node->options_list() != nullptr) {
      print("OPTIONS");
      node->options_list()->Accept(this, data);
    }
  }
  println();
  println(")");
  if (node->output_alias() != nullptr) {
    node->output_alias()->Accept(this, data);
  }
}

void Unparser::visitASTAfterMatchSkipClause(const ASTAfterMatchSkipClause* node,
                                            void* data) {
  print("AFTER MATCH SKIP ");
  switch (node->target_type()) {
    case ASTAfterMatchSkipClauseEnums::PAST_LAST_ROW:
      print("PAST LAST ROW");
      break;
    case ASTAfterMatchSkipClauseEnums::TO_NEXT_ROW:
      print("TO NEXT ROW");
      break;
    case ASTAfterMatchSkipClauseEnums::AFTER_MATCH_SKIP_TARGET_UNSPECIFIED:
      ABSL_LOG(ERROR) << "After match skip target type is not set";
      print("<unspecified after match skip target type>");
      break;
  }
}

void Unparser::visitASTRowPatternVariable(const ASTRowPatternVariable* node,
                                          void* data) {
  node->name()->Accept(this, data);
}

void Unparser::visitASTRowPatternOperation(const ASTRowPatternOperation* node,
                                           void* data) {
  if (node->parenthesized()) {
    print("(");
  }

  switch (node->op_type()) {
    case ASTRowPatternOperation::CONCAT:
      ABSL_DCHECK_GE(node->inputs().size(), 2);
      UnparseChildrenWithSeparator(node, data, " ");
      break;
    case ASTRowPatternOperation::ALTERNATE:
      ABSL_DCHECK_GE(node->inputs().size(), 2);
      UnparseChildrenWithSeparator(node, data, "|");
      break;
    case ASTRowPatternOperation::EXCLUDE:
    case ASTRowPatternOperation::PERMUTE:
      ABSL_LOG(ERROR) << "Row pattern operation not supported: " << node->op_type();
      break;
    case ASTRowPatternOperationEnums::OPERATION_TYPE_UNSPECIFIED:
      ABSL_LOG(ERROR) << "Graph label operation type is not set";
      break;
  }

  if (node->parenthesized()) {
    print(")");
  }
}

void Unparser::visitASTEmptyRowPattern(const ASTEmptyRowPattern* node,
                                       void* data) {
  if (node->parenthesized()) {
    print("(");
  }

  // Empty: nothing to print. We could consider printing a comment.
  print(" ");

  if (node->parenthesized()) {
    print(")");
  }
}

void Unparser::visitASTRowPatternAnchor(const ASTRowPatternAnchor* node,
                                        void* data) {
  if (node->parenthesized()) {
    print("(");
  }
  switch (node->anchor()) {
    case ASTRowPatternAnchorEnums::START:
      print("^");
      break;
    case ASTRowPatternAnchorEnums::END:
      print("$");
      break;
    case ASTRowPatternAnchorEnums::ANCHOR_UNSPECIFIED:
      ABSL_LOG(ERROR) << "Row pattern anchor is not set";
      break;
  }
  if (node->parenthesized()) {
    print(")");
  }
}

void Unparser::visitASTRowPatternQuantification(
    const ASTRowPatternQuantification* node, void* data) {
  if (node->parenthesized()) {
    print("(");
  }

  node->operand()->Accept(this, data);
  node->quantifier()->Accept(this, data);

  if (node->parenthesized()) {
    print(")");
  }
}

void Unparser::visitASTSymbolQuantifier(const ASTSymbolQuantifier* node,
                                        void* data) {
  switch (node->symbol()) {
    case ASTSymbolQuantifier::QUESTION_MARK:
      print("?");
      break;
    case ASTSymbolQuantifier::PLUS:
      print("+");
      break;
    case ASTSymbolQuantifier::STAR:
      print("*");
      break;
    case ASTSymbolQuantifier::SYMBOL_UNSPECIFIED:
      ABSL_LOG(ERROR) << "Quantifier symbol is not set";
      print("<unspecified quantifier symbol>");
      break;
  }

  if (node->is_reluctant()) {
    print("?");
  }
}

void Unparser::visitASTFixedQuantifier(const ASTFixedQuantifier* node,
                                       void* data) {
  print("{");
  node->bound()->Accept(this, data);
  print("}");
}

void Unparser::visitASTBoundedQuantifier(const ASTBoundedQuantifier* node,
                                         void* data) {
  print("{");
  node->lower_bound()->Accept(this, data);
  print(", ");
  node->upper_bound()->Accept(this, data);
  print("}");
  if (node->is_reluctant()) {
    print("?");
  }
}

void Unparser::visitASTQuantifierBound(const ASTQuantifierBound* node,
                                       void* data) {
  if (node->bound() != nullptr) {
    node->bound()->Accept(this, data);
  }
}

void Unparser::visitASTSampleSize(const ASTSampleSize* node, void* data) {
  node->size()->Accept(this, data);
  print(node->GetSQLForUnit());
  if (node->partition_by() != nullptr) {
    node->partition_by()->Accept(this, data);
  }
}

void Unparser::visitASTSampleSuffix(const ASTSampleSuffix* node, void* data) {
  if (node->weight() != nullptr) {
    node->weight()->Accept(this, data);
  }
  if (node->repeat() != nullptr) {
    node->repeat()->Accept(this, data);
  }
}

void Unparser::visitASTWithWeight(const ASTWithWeight* node, void* data) {
  print("WITH WEIGHT");
  visitASTChildren(node, data);
}

void Unparser::visitASTSampleClause(const ASTSampleClause* node, void* data) {
  print("TABLESAMPLE");
  node->sample_method()->Accept(this, data);
  print("(");
  {
    Formatter::Indenter indenter(&formatter_);
    node->sample_size()->Accept(this, data);
  }
  print(")");
  if (node->sample_suffix()) {
    node->sample_suffix()->Accept(this, data);
  }
}

void Unparser::VisitAlterStatementBase(const ASTAlterStatementBase* node,
                                       void* data) {
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  // Path may not exist if FEATURE_ALLOW_MISSING_PATH_EXPRESSION_IN_ALTER_DDL
  // was set during parse time.
  if (node->path()) {
    node->path()->Accept(this, data);
  }
  node->action_list()->Accept(this, data);
}

void Unparser::visitASTAlterApproxViewStatement(
    const ASTAlterApproxViewStatement* node, void* data) {
  print("ALTER APPROX VIEW");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterMaterializedViewStatement(
    const ASTAlterMaterializedViewStatement* node, void* data) {
  print("ALTER MATERIALIZED VIEW");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterModelStatement(const ASTAlterModelStatement* node,
                                           void* data) {
  print("ALTER MODEL");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterDatabaseStatement(
    const ASTAlterDatabaseStatement* node, void* data) {
  print("ALTER DATABASE");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterSchemaStatement(const ASTAlterSchemaStatement* node,
                                            void* data) {
  print("ALTER SCHEMA");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterExternalSchemaStatement(
    const ASTAlterExternalSchemaStatement* node, void* data) {
  print("ALTER EXTERNAL SCHEMA");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterTableStatement(const ASTAlterTableStatement* node,
                                           void* data) {
  print("ALTER TABLE");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterViewStatement(const ASTAlterViewStatement* node,
                                          void* data) {
  print("ALTER VIEW");
  VisitAlterStatementBase(node, data);
}

void Unparser::visitASTAlterIndexStatement(const ASTAlterIndexStatement* node,
                                           void* data) {
  switch (node->index_type()) {
    case ASTAlterIndexStatement::IndexType::INDEX_SEARCH:
      print("ALTER SEARCH INDEX");
      break;
    case ASTAlterIndexStatement::IndexType::INDEX_VECTOR:
      print("ALTER VECTOR INDEX");
      break;
    default:
      ABSL_LOG(FATAL) << "Index type is missing or not supported";  // Crash OK
      break;
  }
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->path()->Accept(this, data);
  if (node->table_name() != nullptr) {
    print("ON");
    node->table_name()->Accept(this, data);
  }
  node->action_list()->Accept(this, data);
}

void Unparser::visitASTRebuildAction(const ASTRebuildAction* node, void* data) {
  print("REBUILD");
}

void Unparser::visitASTSetOptionsAction(const ASTSetOptionsAction* node,
                                        void* data) {
  print("SET OPTIONS");
  node->options_list()->Accept(this, data);
}

void Unparser::VisitCheckConstraintSpec(const ASTCheckConstraint* node,
                                        void* data) {
  print("CHECK");
  print("(");
  node->expression()->Accept(this, data);
  print(")");
  if (!node->is_enforced()) {
    print("NOT");
  }
  print("ENFORCED");
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTAddConstraintAction(const ASTAddConstraintAction* node,
                                           void* data) {
  print("ADD");
  auto* constraint = node->constraint();
  if (constraint->constraint_name() != nullptr) {
    print("CONSTRAINT");
    if (node->is_if_not_exists()) {
      print("IF NOT EXISTS");
    }
    constraint->constraint_name()->Accept(this, data);
  }
  auto node_kind = constraint->node_kind();
  if (node_kind == AST_CHECK_CONSTRAINT) {
    VisitCheckConstraintSpec(constraint->GetAs<ASTCheckConstraint>(), data);
  } else if (node_kind == AST_FOREIGN_KEY) {
    VisitForeignKeySpec(constraint->GetAs<ASTForeignKey>(), data);
  } else if (node_kind == AST_PRIMARY_KEY) {
    constraint->GetAs<ASTPrimaryKey>()->Accept(this, data);
  } else {
    ABSL_LOG(FATAL) << "Unknown constraint node kind: "
               << ASTNode::NodeKindToString(node_kind);
  }
}

void Unparser::visitASTDropConstraintAction(const ASTDropConstraintAction* node,
                                            void* data) {
  print("DROP CONSTRAINT");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->constraint_name()->Accept(this, data);
}

void Unparser::visitASTDropPrimaryKeyAction(const ASTDropPrimaryKeyAction* node,
                                            void* data) {
  print("DROP PRIMARY KEY");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
}

void Unparser::visitASTAlterConstraintEnforcementAction(
    const ASTAlterConstraintEnforcementAction* node, void* data) {
  print("ALTER CONSTRAINT");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->constraint_name()->Accept(this, data);
  if (!node->is_enforced()) {
    print("NOT");
  }
  print("ENFORCED");
}

void Unparser::visitASTAlterConstraintSetOptionsAction(
    const ASTAlterConstraintSetOptionsAction* node, void* data) {
  print("ALTER CONSTRAINT");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->constraint_name()->Accept(this, data);
  print("SET OPTIONS");
  node->options_list()->Accept(this, data);
}

void Unparser::visitASTAddColumnAction(const ASTAddColumnAction* node,
                                       void* data) {
  print("ADD COLUMN");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->column_definition()->Accept(this, data);
  if (node->column_position()) {
    node->column_position()->Accept(this, data);
  }
  if (node->fill_expression()) {
    print("FILL USING");
    node->fill_expression()->Accept(this, data);
  }
}

void Unparser::visitASTAddColumnIdentifierAction(
    const ASTAddColumnIdentifierAction* node, void* data) {
  print("ADD COLUMN");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->column_name()->Accept(this, data);
  if (node->options_list()) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTColumnPosition(const ASTColumnPosition* node,
                                      void* data) {
  print(node->type() == ASTColumnPosition::PRECEDING ? "PRECEDING"
                                                     : "FOLLOWING");
  node->identifier()->Accept(this, data);
}

void Unparser::visitASTDropColumnAction(const ASTDropColumnAction* node,
                                        void* data) {
  print("DROP COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
}

void Unparser::visitASTRenameColumnAction(const ASTRenameColumnAction* node,
                                          void* data) {
  print("RENAME COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("TO");
  node->new_column_name()->Accept(this, data);
}

void Unparser::visitASTAlterColumnOptionsAction(
    const ASTAlterColumnOptionsAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("SET OPTIONS");
  node->options_list()->Accept(this, data);
}

void Unparser::visitASTAlterColumnDropNotNullAction(
    const ASTAlterColumnDropNotNullAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("DROP NOT NULL");
}

void Unparser::visitASTAlterColumnTypeAction(
    const ASTAlterColumnTypeAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("SET DATA TYPE");
  node->schema()->Accept(this, data);
  if (node->collate() != nullptr) {
    visitASTCollate(node->collate(), data);
  }
}

void Unparser::visitASTAlterColumnSetDefaultAction(
    const ASTAlterColumnSetDefaultAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("SET DEFAULT");
  node->default_expression()->Accept(this, data);
}

void Unparser::visitASTAlterColumnDropDefaultAction(
    const ASTAlterColumnDropDefaultAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("DROP DEFAULT");
}

void Unparser::visitASTAlterColumnDropGeneratedAction(
    const ASTAlterColumnDropGeneratedAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("DROP GENERATED");
}

void Unparser::visitASTAlterColumnSetGeneratedAction(
    const ASTAlterColumnSetGeneratedAction* node, void* data) {
  print("ALTER COLUMN");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->column_name()->Accept(this, data);
  print("SET");
  node->generated_column_info()->Accept(this, data);
}

void Unparser::visitASTRevokeFromClause(const ASTRevokeFromClause* node,
                                        void* data) {
  print("REVOKE FROM ");
  if (node->is_revoke_from_all()) {
    print("ALL");
  } else {
    print("(");
    node->revoke_from_list()->Accept(this, data);
    print(")");
  }
}

void Unparser::visitASTRenameToClause(const ASTRenameToClause* node,
                                      void* data) {
  print("RENAME TO");
  node->new_name()->Accept(this, data);
}

void Unparser::visitASTSetCollateClause(const ASTSetCollateClause* node,
                                        void* data) {
  print("SET DEFAULT");
  visitASTCollate(node->collate(), data);
}

void Unparser::visitASTAlterActionList(const ASTAlterActionList* node,
                                       void* data) {
  Formatter::Indenter indenter(&formatter_);
  UnparseChildrenWithSeparator(node, data, ",");
}

void Unparser::visitASTAlterPrivilegeRestrictionStatement(
    const ASTAlterPrivilegeRestrictionStatement* node, void* data) {
  print("ALTER PRIVILEGE RESTRICTION");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  print("ON");
  node->privileges()->Accept(this, data);
  print("ON");
  node->object_type()->Accept(this, data);
  node->path()->Accept(this, data);
  node->action_list()->Accept(this, data);
}

void Unparser::visitASTAlterRowAccessPolicyStatement(
    const ASTAlterRowAccessPolicyStatement* node, void* data) {
  print("ALTER ROW ACCESS POLICY");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  print("ON");
  node->path()->Accept(this, data);
  node->action_list()->Accept(this, data);
}

void Unparser::visitASTAlterAllRowAccessPoliciesStatement(
    const ASTAlterAllRowAccessPoliciesStatement* node, void* data) {
  print("ALTER ALL ROW ACCESS POLICIES ON");
  node->table_name_path()->Accept(this, data);
  node->alter_action()->Accept(this, data);
}

void Unparser::visitASTCreateIndexStatement(const ASTCreateIndexStatement* node,
                                            void* data) {
  print("CREATE");
  if (node->is_or_replace()) print("OR REPLACE");
  if (node->is_unique()) print("UNIQUE");
  if (node->spanner_is_null_filtered()) print("NULL_FILTERED");
  if (node->is_search()) print("SEARCH");
  if (node->is_vector()) print("VECTOR");
  print("INDEX");
  if (node->is_if_not_exists()) print("IF NOT EXISTS");
  node->name()->Accept(this, data);
  print("ON");
  node->table_name()->Accept(this, data);
  if (node->optional_table_alias() != nullptr) {
    node->optional_table_alias()->Accept(this, data);
  }
  if (node->optional_index_unnest_expression_list() != nullptr) {
    println();
    node->optional_index_unnest_expression_list()->Accept(this, data);
    println();
  }
  node->index_item_list()->Accept(this, data);
  if (node->optional_index_storing_expressions() != nullptr) {
    println();
    node->optional_index_storing_expressions()->Accept(this, data);
  }
  if (node->optional_partition_by() != nullptr) {
    println();
    node->optional_partition_by()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    println();
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
  if (node->spanner_interleave_clause() != nullptr) {
    node->spanner_interleave_clause()->Accept(this, data);
  }
}

void Unparser::visitASTStatementList(const ASTStatementList* node, void* data) {
  for (const ASTStatement* statement : node->statement_list()) {
    statement->Accept(this, data);
    println(";");
  }
}

void Unparser::visitASTElseifClause(const ASTElseifClause* node, void* data) {
  print("ELSEIF");
  node->condition()->Accept(this, data);
  print("THEN");
  {
    Formatter::Indenter indenter(&formatter_);
    node->body()->Accept(this, data);
  }
  println();
}

void Unparser::visitASTElseifClauseList(const ASTElseifClauseList* node,
                                        void* data) {
  for (const ASTElseifClause* else_if_clause : node->elseif_clauses()) {
    else_if_clause->Accept(this, data);
  }
}

void Unparser::visitASTIfStatement(const ASTIfStatement* node, void* data) {
  print("IF");
  node->condition()->Accept(this, data);
  println("THEN");
  {
    Formatter::Indenter indenter(&formatter_);
    node->then_list()->Accept(this, data);
  }
  if (node->elseif_clauses() != nullptr) {
    node->elseif_clauses()->Accept(this, data);
  }
  if (node->else_list() != nullptr) {
    println();
    println("ELSE");
    Formatter::Indenter indenter(&formatter_);
    node->else_list()->Accept(this, data);
  }
  println();
  print("END IF");
}

void Unparser::visitASTWhenThenClause(const ASTWhenThenClause* node,
                                      void* data) {
  print("WHEN");
  node->condition()->Accept(this, data);
  println("THEN");
  {
    Formatter::Indenter indenter(&formatter_);
    node->body()->Accept(this, data);
  }
  println();
}

void Unparser::visitASTWhenThenClauseList(const ASTWhenThenClauseList* node,
                                          void* data) {
  for (const ASTWhenThenClause* when_then_clause : node->when_then_clauses()) {
    when_then_clause->Accept(this, data);
  }
}

void Unparser::visitASTCaseStatement(const ASTCaseStatement* node, void* data) {
  print("CASE");
  if (node->expression() != nullptr) {
    node->expression()->Accept(this, data);
  }
  println();
  node->when_then_clauses()->Accept(this, data);
  if (node->else_list() != nullptr) {
    println();
    println("ELSE");
    Formatter::Indenter indenter(&formatter_);
    node->else_list()->Accept(this, data);
  }
  print("END");
  print("CASE");
}

void Unparser::visitASTBeginEndBlock(const ASTBeginEndBlock* node, void* data) {
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
    print(":");
  }
  println("BEGIN");
  {
    Formatter::Indenter indenter(&formatter_);
    node->statement_list_node()->Accept(this, data);
  }
  if (node->handler_list() != nullptr) {
    node->handler_list()->Accept(this, data);
  }
  println("END");
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTIndexAllColumns(const ASTIndexAllColumns* node,
                                       void* data) {
  UnparseLeafNode(node);
  if (node->column_options()) {
    print("WITH COLUMN OPTIONS");
    node->column_options()->Accept(this, data);
  }
}

void Unparser::visitASTIndexItemList(const ASTIndexItemList* node, void* data) {
  print("(");
  UnparseVectorWithSeparator(node->ordering_expressions(), data, ",");
  print(")");
}

void Unparser::visitASTIndexStoringExpressionList(
    const ASTIndexStoringExpressionList* node, void* data) {
  print("STORING(");
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseVectorWithSeparator(node->expressions(), data, ",");
  }
  print(")");
}

void Unparser::visitASTIndexUnnestExpressionList(
    const ASTIndexUnnestExpressionList* node, void* data) {
  UnparseVectorWithSeparator(node->unnest_expressions(), data, "");
}

void Unparser::VisitForeignKeySpec(const ASTForeignKey* node, void* data) {
  print("FOREIGN KEY");
  node->column_list()->Accept(this, data);
  node->reference()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTForeignKey(const ASTForeignKey* node, void* data) {
  if (node->constraint_name() != nullptr) {
    print("CONSTRAINT");
    node->constraint_name()->Accept(this, data);
  }
  VisitForeignKeySpec(node, data);
}

void Unparser::visitASTForeignKeyReference(const ASTForeignKeyReference* node,
                                           void* data) {
  print("REFERENCES");
  node->table_name()->Accept(this, data);
  node->column_list()->Accept(this, data);
  print("MATCH");
  print(node->GetSQLForMatch());
  node->actions()->Accept(this, data);
  if (!node->enforced()) {
    print("NOT");
  }
  print("ENFORCED");
}

void Unparser::visitASTForeignKeyActions(const ASTForeignKeyActions* node,
                                         void* data) {
  print("ON UPDATE");
  print(ASTForeignKeyActions::GetSQLForAction(node->update_action()));
  print("ON DELETE");
  print(ASTForeignKeyActions::GetSQLForAction(node->delete_action()));
}

void Unparser::visitASTCheckConstraint(const ASTCheckConstraint* node,
                                       void* data) {
  if (node->constraint_name() != nullptr) {
    print("CONSTRAINT");
    node->constraint_name()->Accept(this, data);
  }
  VisitCheckConstraintSpec(node, data);
}

void Unparser::visitASTIdentifierList(const ASTIdentifierList* node,
                                      void* data) {
  UnparseVectorWithSeparator(node->identifier_list(), data, ", ");
}

void Unparser::visitASTVariableDeclaration(const ASTVariableDeclaration* node,
                                           void* data) {
  print("DECLARE");
  node->variable_list()->Accept(this, data);
  if (node->type() != nullptr) {
    node->type()->Accept(this, data);
  }
  if (node->default_value() != nullptr) {
    print("DEFAULT");
    node->default_value()->Accept(this, data);
  }
}

void Unparser::visitASTSingleAssignment(const ASTSingleAssignment* node,
                                        void* data) {
  print("SET");
  node->variable()->Accept(this, data);
  print("=");
  node->expression()->Accept(this, data);
}

void Unparser::visitASTParameterAssignment(const ASTParameterAssignment* node,
                                           void* data) {
  print("SET");
  node->parameter()->Accept(this, data);
  print("=");
  node->expression()->Accept(this, data);
}

void Unparser::visitASTSystemVariableAssignment(
    const ASTSystemVariableAssignment* node, void* data) {
  print("SET");
  node->system_variable()->Accept(this, data);
  print("=");
  node->expression()->Accept(this, data);
}

void Unparser::visitASTAssignmentFromStruct(const ASTAssignmentFromStruct* node,
                                            void* data) {
  print("SET");
  print("(");
  for (const ASTIdentifier* variable : node->variables()->identifier_list()) {
    variable->Accept(this, data);
    if (variable != node->variables()->identifier_list().back()) {
      print(",");
    }
  }
  print(")");
  print("=");
  node->struct_expression()->Accept(this, data);
}

void Unparser::visitASTWhileStatement(const ASTWhileStatement* node,
                                      void* data) {
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
    print(":");
  }
  if (node->condition() != nullptr) {
    print("WHILE");
    node->condition()->Accept(this, data);
    println("DO");
    {
      Formatter::Indenter indenter(&formatter_);
      node->body()->Accept(this, data);
    }
    print("END");
    print("WHILE");
  } else {
    println("LOOP");
    {
      Formatter::Indenter indenter(&formatter_);
      node->body()->Accept(this, data);
    }
    print("END");
    print("LOOP");
  }
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTUntilClause(const ASTUntilClause* node, void* data) {
  print("UNTIL");
  node->condition()->Accept(this, data);
}

void Unparser::visitASTRepeatStatement(const ASTRepeatStatement* node,
                                       void* data) {
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
    print(":");
  }
  println("REPEAT");
  {
    Formatter::Indenter indenter(&formatter_);
    node->body()->Accept(this, data);
  }
  node->until_clause()->Accept(this, data);
  println();
  print("END");
  print("REPEAT");
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTForInStatement(const ASTForInStatement* node,
                                      void* data) {
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
    print(":");
  }
  print("FOR");
  node->variable()->Accept(this, data);
  print("IN");
  print("(");
  node->query()->Accept(this, data);
  println(")");
  println("DO");
  {
    Formatter::Indenter indenter(&formatter_);
    node->body()->Accept(this, data);
  }
  print("END");
  print("FOR");
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTLabel(const ASTLabel* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTScript(const ASTScript* node, void* data) {
  node->statement_list_node()->Accept(this, data);
}

void Unparser::visitASTBreakStatement(const ASTBreakStatement* node,
                                      void* data) {
  print(node->GetKeywordText());
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTContinueStatement(const ASTContinueStatement* node,
                                         void* data) {
  print(node->GetKeywordText());
  if (node->label() != nullptr) {
    node->label()->Accept(this, data);
  }
}

void Unparser::visitASTReturnStatement(const ASTReturnStatement* node,
                                       void* data) {
  print("RETURN");
}

void Unparser::visitASTCreateProcedureStatement(
    const ASTCreateProcedureStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "PROCEDURE"));
  node->name()->Accept(this, data);
  node->parameters()->Accept(this, data);
  println();
  if (node->external_security() !=
      ASTCreateStatement::SQL_SECURITY_UNSPECIFIED) {
    print(node->GetSqlForExternalSecurity());
  }
  if (node->with_connection_clause() != nullptr) {
    node->with_connection_clause()->Accept(this, data);
  }
  if (node->options_list() != nullptr) {
    println("OPTIONS");
    Formatter::Indenter indenter(&formatter_);
    node->options_list()->Accept(this, data);
    println();
  }
  if (node->body() != nullptr) {
    // CREATE PROCEDURE statements are constructed so that the body always
    // consists of a single ASTBeginEndBlock statement.
    ABSL_DCHECK_EQ(node->body()->statement_list().size(), 1);
    node->body()->statement_list()[0]->Accept(this, data);
  } else if (node->language() != nullptr) {
    print("LANGUAGE");
    node->language()->Accept(this, data);
    if (node->code() != nullptr) {
      print("AS");
      node->code()->Accept(this, data);
    }
  }
}

void Unparser::visitASTNamedArgument(const ASTNamedArgument* node, void* data) {
  node->name()->Accept(this, data);
  print(" => ");
  node->expr()->Accept(this, data);
}

void Unparser::visitASTInputTableArgument(const ASTInputTableArgument* node,
                                          void* data) {
  print("INPUT TABLE");
}

void Unparser::visitASTLambda(const ASTLambda* node, void* data) {
  const ASTExpression* argument_list = node->argument_list();
  // Check if the parameter list expression will print the parentheses.
  const bool already_parenthesized =
      argument_list->parenthesized() ||
      argument_list->node_kind() == AST_STRUCT_CONSTRUCTOR_WITH_PARENS;
  if (!already_parenthesized) {
    print("(");
  }
  node->argument_list()->Accept(this, data);
  if (!already_parenthesized) {
    print(")");
  }
  print("-> ");
  node->body()->Accept(this, data);
}

void Unparser::visitASTExceptionHandler(const ASTExceptionHandler* node,
                                        void* data) {
  print("WHEN ERROR THEN");
  Formatter::Indenter indenter(&formatter_);
  node->statement_list()->Accept(this, data);
  println();
}
void Unparser::visitASTExceptionHandlerList(const ASTExceptionHandlerList* node,
                                            void* data) {
  println("EXCEPTION");
  for (const ASTExceptionHandler* handler : node->exception_handler_list()) {
    handler->Accept(this, data);
  }
}
void Unparser::visitASTExecuteIntoClause(const ASTExecuteIntoClause* node,
                                         void* data) {
  print("INTO");
  UnparseChildrenWithSeparator(node, data, ", ");
}
void Unparser::visitASTExecuteUsingArgument(const ASTExecuteUsingArgument* node,
                                            void* data) {
  visitASTChildren(node, data);
}
void Unparser::visitASTExecuteUsingClause(const ASTExecuteUsingClause* node,
                                          void* data) {
  print("USING");
  UnparseChildrenWithSeparator(node, data, ", ");
}
void Unparser::visitASTExecuteImmediateStatement(
    const ASTExecuteImmediateStatement* node, void* data) {
  print("EXECUTE IMMEDIATE");
  node->sql()->Accept(this, data);
  if (node->into_clause() != nullptr) {
    node->into_clause()->Accept(this, data);
  }
  if (node->using_clause() != nullptr) {
    node->using_clause()->Accept(this, data);
  }
}

void Unparser::visitASTRaiseStatement(const ASTRaiseStatement* node,
                                      void* data) {
  print("RAISE");
  if (node->message() != nullptr) {
    print("USING MESSAGE =");
    node->message()->Accept(this, data);
  }
}

void Unparser::visitASTAlterSubEntityAction(const ASTAlterSubEntityAction* node,
                                            void* data) {
  print("ALTER ");
  node->type()->Accept(this, data);
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
  node->action()->Accept(this, data);
}

void Unparser::visitASTAddSubEntityAction(const ASTAddSubEntityAction* node,
                                          void* data) {
  print("ADD ");
  node->type()->Accept(this, data);
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTDropSubEntityAction(const ASTDropSubEntityAction* node,
                                           void* data) {
  print("DROP ");
  node->type()->Accept(this, data);
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
  node->name()->Accept(this, data);
}

void Unparser::visitASTTtlClause(const ASTTtlClause* node, void* data) {
  print("ROW DELETION POLICY(");
  node->expression()->Accept(this, data);
  print(")");
}

void Unparser::visitASTAddTtlAction(const ASTAddTtlAction* node, void* data) {
  print("ADD ROW DELETION POLICY");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS ");
  }
  print("(");
  node->expression()->Accept(this, data);
  print(")");
}

void Unparser::visitASTReplaceTtlAction(const ASTReplaceTtlAction* node,
                                        void* data) {
  print("REPLACE ROW DELETION POLICY");
  if (node->is_if_exists()) {
    print("IF EXISTS ");
  }
  print("(");
  node->expression()->Accept(this, data);
  print(")");
}

void Unparser::visitASTDropTtlAction(const ASTDropTtlAction* node, void* data) {
  print("DROP ROW DELETION POLICY");
  if (node->is_if_exists()) {
    print("IF EXISTS");
  }
}

void Unparser::visitASTWithExpression(const ASTWithExpression* node,
                                      void* data) {
  print("WITH(");
  for (const auto& var : node->variables()->columns()) {
    // We accept the identifier here rather than alias to prevent "AS" from
    // being incorrectly printed before the alias.
    var->alias()->identifier()->Accept(this, data);
    print("AS");
    var->expression()->Accept(this, data);
    print(", ");
  }
  node->expression()->Accept(this, data);
  print(")");
}

void Unparser::visitASTInputOutputClause(const ASTInputOutputClause* node,
                                         void* data) {
  println();
  print("INPUT");
  node->input()->Accept(this, data);
  println();
  print("OUTPUT");
  node->output()->Accept(this, data);
}

void Unparser::visitASTSpannerTableOptions(const ASTSpannerTableOptions* node,
                                           void* data) {
  node->primary_key()->Accept(this, data);
  if (node->interleave_clause() != nullptr) {
    node->interleave_clause()->Accept(this, data);
  }
}

void Unparser::visitASTSpannerInterleaveClause(
    const ASTSpannerInterleaveClause* node, void* data) {
  print(", INTERLEAVE IN");
  if (node->type() == ASTSpannerInterleaveClause::IN_PARENT) {
    print("PARENT");
  }
  node->table_name()->Accept(this, data);
  if (node->type() == ASTSpannerInterleaveClause::IN_PARENT) {
    print("ON DELETE");
    print(ASTForeignKeyActions::GetSQLForAction(node->action()));
  }
}

void Unparser::visitASTSpannerAlterColumnAction(
    const ASTSpannerAlterColumnAction* node, void* data) {
  print("ALTER COLUMN");
  node->column_definition()->Accept(this, data);
}

void Unparser::visitASTSpannerSetOnDeleteAction(
    const ASTSpannerSetOnDeleteAction* node, void* data) {
  print("SET ON DELETE");
  print(ASTForeignKeyActions::GetSQLForAction(node->action()));
}

void Unparser::visitASTCreatePropertyGraphStatement(
    const ASTCreatePropertyGraphStatement* node, void* data) {
  print("CREATE");
  if (node->is_or_replace()) {
    print("OR REPLACE");
  }
  if (node->is_private()) {
    print("PRIVATE");
  }
  if (node->is_public()) {
    print("PUBLIC");
  }
  if (node->is_temp()) {
    print("TEMP");
  }
  print("PROPERTY GRAPH");
  if (node->is_if_not_exists()) {
    print("IF NOT EXISTS");
  }
  visitASTPathExpression(node->name(), data);
  println();

  {
    Formatter::Indenter indenter(&formatter_);
    println("NODE TABLES(");
    {
      Formatter::Indenter indenter(&formatter_);
      node->node_table_list()->Accept(this, data);
    }
    println();
    println(")");
  }
  if (node->edge_table_list() != nullptr) {
    Formatter::Indenter indenter(&formatter_);
    println("EDGE TABLES(");
    {
      Formatter::Indenter indenter(&formatter_);
      node->edge_table_list()->Accept(this, data);
    }
    println();
    println(")");
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
    println();
  }
}

void Unparser::visitASTGraphElementTableList(
    const ASTGraphElementTableList* node, void* data) {
  UnparseChildrenWithSeparator(node, data, ",\n", true);
}

void Unparser::visitASTGraphElementTable(const ASTGraphElementTable* node,
                                         void* data) {
  visitASTPathExpression(node->name(), data);
  if (node->alias() != nullptr) {
    visitASTAlias(node->alias(), data);
  }
  if (node->key_list() != nullptr) {
    println();
    Formatter::Indenter indenter(&formatter_);
    print("KEY");
    visitASTColumnList(node->key_list(), data);
  }
  if (node->source_node_reference() != nullptr) {
    println();
    node->source_node_reference()->Accept(this, data);
  }
  if (node->dest_node_reference() != nullptr) {
    println();
    node->dest_node_reference()->Accept(this, data);
  }

  if (node->default_label_options_list() != nullptr) {
    println();
    Formatter::Indenter indenter(&formatter_);
    print("OPTIONS");
    visitASTOptionsList(node->default_label_options_list(), data);
  }
  println();
  visitASTGraphElementLabelAndPropertiesList(node->label_properties_list(),
                                             data);
  if (node->dynamic_label() != nullptr) {
    visitASTGraphDynamicLabel(node->dynamic_label(), data);
  }
  if (node->dynamic_properties() != nullptr) {
    visitASTGraphDynamicProperties(node->dynamic_properties(), data);
  }
}

void Unparser::visitASTGraphDynamicLabel(const ASTGraphDynamicLabel* node,
                                         void* data) {
  if (node->label() == nullptr) {
    return;
  }
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    print("DYNAMIC LABEL (");
    node->label()->Accept(this, data);
    print(")");
  }
}

void Unparser::visitASTGraphDynamicProperties(
    const ASTGraphDynamicProperties* node, void* data) {
  if (node->properties() == nullptr) {
    return;
  }
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    print("DYNAMIC PROPERTIES (");
    node->properties()->Accept(this, data);
    print(")");
  }
}

void Unparser::visitASTGraphNodeTableReference(
    const ASTGraphNodeTableReference* node, void* data) {
  Formatter::Indenter indenter(&formatter_);
  switch (node->node_reference_type()) {
    case ASTGraphNodeTableReference::SOURCE:
      print("SOURCE KEY");
      break;
    case ASTGraphNodeTableReference::DESTINATION:
      print("DESTINATION KEY");
      break;
    case ASTGraphNodeTableReference::NODE_REFERENCE_TYPE_UNSPECIFIED:
      ABSL_LOG(FATAL) << "Node reference type is not set";  // Crash OK
      break;
  }
  visitASTColumnList(node->edge_table_columns(), data);
  print("REFERENCES");
  visitASTIdentifier(node->node_table_identifier(), data);
  if (node->node_table_columns() != nullptr) {
    node->node_table_columns()->Accept(this, data);
  }
}

void Unparser::visitASTGraphElementLabelAndPropertiesList(
    const ASTGraphElementLabelAndPropertiesList* node, void* data) {
  UnparseChildrenWithSeparator(node, data, "", true);
}

void Unparser::visitASTGraphElementLabelAndProperties(
    const ASTGraphElementLabelAndProperties* node, void* data) {
  if (node->label_name() != nullptr) {
    Formatter::Indenter indenter(&formatter_);
    print("LABEL");
    visitASTIdentifier(node->label_name(), data);
  } else {
    Formatter::Indenter indenter(&formatter_);
    print("DEFAULT LABEL");
  }

  if (node->label_options_list() != nullptr) {
    print("OPTIONS");
    visitASTOptionsList(node->label_options_list(), data);
  }

  visitASTGraphProperties(node->properties(), data);
}

void Unparser::visitASTGraphDerivedProperty(const ASTGraphDerivedProperty* node,
                                            void* data) {
  if (node->expression() != nullptr) {
    node->expression()->Accept(this, data);
  }
  if (node->alias() != nullptr) {
    visitASTAlias(node->alias(), data);
  }
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    visitASTOptionsList(node->options_list(), data);
  }
}

void Unparser::visitASTGraphDerivedPropertyList(
    const ASTGraphDerivedPropertyList* node, void* data) {
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    UnparseChildrenWithSeparator(node, data, ",", /*break_line=*/true);
  }
}

void Unparser::visitASTGraphProperties(const ASTGraphProperties* node,
                                       void* data) {
  if (node->no_properties()) {
    print("NO PROPERTIES");
    return;
  }
  if (node->derived_property_list() == nullptr) {
    print("PROPERTIES ALL COLUMNS");
    if (node->all_except_columns() != nullptr) {
      print("EXCEPT");
      visitASTColumnList(node->all_except_columns(), data);
    }
    return;
  }
  print("PROPERTIES(");
  {
    Formatter::Indenter indenter(&formatter_);
    visitASTGraphDerivedPropertyList(node->derived_property_list(), data);
  }
  print(")");
}

void Unparser::visitASTGqlMatch(const ASTGqlMatch* node, void* data) {
  if (node->optional()) {
    print("OPTIONAL ");
  }
  print("MATCH");
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    node->graph_pattern()->Accept(this, data);
  }
}

void Unparser::visitASTGqlReturn(const ASTGqlReturn* node, void* data) {
  print("RETURN");
  if (node->select()->hint() != nullptr) {
    node->select()->hint()->Accept(this, data);
  }
  if (node->select()->distinct()) {
    print("DISTINCT");
  }
  node->select()->select_list()->Accept(this, data);
  if (node->select()->group_by() != nullptr) {
    node->select()->group_by()->Accept(this, data);
  }
  if (node->order_by_page() != nullptr) {
    println();
    node->order_by_page()->Accept(this, data);
  }
}

void Unparser::visitASTGqlWith(const ASTGqlWith* node, void* data) {
  print("WITH");
  if (node->select()->hint() != nullptr) {
    node->select()->hint()->Accept(this, data);
  }
  node->select()->select_list()->Accept(this, data);
}

void Unparser::visitASTGqlFor(const ASTGqlFor* node, void* data) {
  print("FOR");
  node->identifier()->Accept(this, data);
  print("IN ");
  node->expression()->Accept(this, data);
  if (node->with_offset() != nullptr) {
    print("WITH OFFSET");
    if (node->with_offset()->alias() != nullptr) {
      node->with_offset()->alias()->Accept(this, data);
    }
  }
}

void Unparser::VisitASTGqlCallBase(const ASTGqlCallBase* node, void* data) {
  if (node->optional()) {
    print("OPTIONAL ");
  }
  print("CALL ");
  if (node->is_partitioning()) {
    print("PER ");
  }
  if (node->name_capture_list() != nullptr) {
    // The name capture list is required for inline subqueries, but is caught
    // in the analyzer.
    print("(");
    node->name_capture_list()->Accept(this, data);
    print(") ");
  }
}

void Unparser::visitASTGqlInlineSubqueryCall(
    const ASTGqlInlineSubqueryCall* node, void* data) {
  VisitASTGqlCallBase(node, data);
  println("{");

  {
    Formatter::Indenter indenter(&formatter_);
    node->subquery()->Accept(this, data);
  }
  println("}");
}

void Unparser::visitASTGqlNamedCall(const ASTGqlNamedCall* node, void* data) {
  VisitASTGqlCallBase(node, data);
  node->tvf_call()->Accept(this, data);
  if (node->yield_clause() != nullptr) {
    node->yield_clause()->Accept(this, data);
  }
}

void Unparser::visitASTYieldItemList(const ASTYieldItemList* node, void* data) {
  print("YIELD ");
  UnparseVectorWithSeparator(node->yield_items(), data, ", ");
}

void Unparser::visitASTGqlLet(const ASTGqlLet* node, void* data) {
  print("LET");
  println();
  node->variable_definition_list()->Accept(this, data);
}

void Unparser::visitASTGqlQuery(const ASTGqlQuery* node, void* data) {
  println();
  if (node->graph_table()->graph_reference() != nullptr) {
    print("GRAPH ");
    node->graph_table()->graph_reference()->Accept(this, data);
    println();
  }
  ABSL_DCHECK(node->graph_table()->graph_op()->Is<ASTGqlOperatorList>());
  node->graph_table()->graph_op()->Accept(this, data);
  println();
}

void Unparser::visitASTGqlGraphPatternQuery(const ASTGqlGraphPatternQuery* node,
                                            void* data) {
  println();
  if (node->graph_reference() != nullptr) {
    print("GRAPH ");
    node->graph_reference()->Accept(this, data);
    println();
  }
  node->graph_pattern()->Accept(this, data);
  println();
}

void Unparser::visitASTGqlLinearOpsQuery(const ASTGqlLinearOpsQuery* node,
                                         void* data) {
  println();
  if (node->graph_reference() != nullptr) {
    print("GRAPH ");
    node->graph_reference()->Accept(this, data);
    println();
  }
  node->linear_ops()->Accept(this, data);
  println();
}

void Unparser::visitASTGqlLetVariableDefinitionList(
    const ASTGqlLetVariableDefinitionList* node, void* data) {
  Formatter::Indenter indenter(&formatter_);
  UnparseVectorWithSeparator(node->variable_definitions(), data,
                             /*separator=*/",", /*break_line=*/true);
}

void Unparser::visitASTGqlLetVariableDefinition(
    const ASTGqlLetVariableDefinition* node, void* data) {
  node->identifier()->Accept(this, data);
  print("= ");
  node->expression()->Accept(this, data);
}

void Unparser::visitASTGqlFilter(const ASTGqlFilter* node, void* data) {
  print("FILTER");

  {
    Formatter::Indenter indenter(&formatter_);
    node->condition()->Accept(this, data);
  }
}

void Unparser::visitASTGqlPageLimit(const ASTGqlPageLimit* node, void* data) {
  print("LIMIT");
  node->limit()->Accept(this, data);
}

void Unparser::visitASTGqlPageOffset(const ASTGqlPageOffset* node, void* data) {
  print("OFFSET");
  node->offset()->Accept(this, data);
}

void Unparser::visitASTGqlPage(const ASTGqlPage* node, void* data) {
  if (node->offset() != nullptr) {
    node->offset()->Accept(this, data);
  }
  if (node->limit() != nullptr) {
    node->limit()->Accept(this, data);
  }
}

void Unparser::visitASTGqlOrderByAndPage(const ASTGqlOrderByAndPage* node,
                                         void* data) {
  if (node->order_by() != nullptr) {
    node->order_by()->Accept(this, data);
  }
  if (node->page() != nullptr) {
    node->page()->Accept(this, data);
  }
}

void Unparser::visitASTGraphTableQuery(const ASTGraphTableQuery* node,
                                       void* data) {
  print("GRAPH_TABLE(");
  println();
  {
    Formatter::Indenter indenter(&formatter_);
    node->graph_reference()->Accept(this, data);
    println();
    node->graph_op()->Accept(this, data);
    println();
    if (node->graph_table_shape() != nullptr) {
      ABSL_DCHECK(!node->graph_op()->Is<ASTGqlOperatorList>());
      print("COLUMNS(");
      node->graph_table_shape()->Accept(this, data);
      println();
      println(")");
    }
  }
  print(")");
  if (node->alias() != nullptr) {
    node->alias()->Accept(this, data);
  }
  for (const auto* op : node->postfix_operators()) {
    op->Accept(this, data);
  }
}

static bool IsLinearOrCompositeQuery(const ASTGqlOperator* node) {
  return node->Is<ASTGqlOperatorList>() || node->Is<ASTGqlSetOperation>();
}

void Unparser::visitASTGqlOperatorList(const ASTGqlOperatorList* node,
                                       void* data) {
  ABSL_DCHECK(!node->operators().empty());
  if (IsLinearOrCompositeQuery(node->operators()[0])) {
    for (auto it = node->operators().begin(); it != node->operators().end();
         ++it) {
      if (it != node->operators().begin()) {
        println("NEXT");
      }
      ABSL_DCHECK(IsLinearOrCompositeQuery(*it));
      (*it)->Accept(this, data);
      println();
    }
  } else {
    UnparseChildrenWithSeparator(node, data, "", /*break_line=*/true);
  }
}

void Unparser::visitASTGqlSetOperation(const ASTGqlSetOperation* node,
                                       void* data) {
  ABSL_DCHECK_GE(node->inputs().size(), 2);
  ABSL_DCHECK(node->metadata() != nullptr);
  for (int i = 0; i < node->inputs().size(); ++i) {
    if (i > 0) {
      println();
      // The i-th query is preceded by the (i-1)-th operator.
      node->metadata()->set_operation_metadata_list(i - 1)->Accept(this, data);
      println();
    }
    node->inputs(i)->Accept(this, data);
  }
}

void Unparser::visitASTGqlSample(const ASTGqlSample* node, void* data) {
  node->sample()->Accept(this, data);
}

void Unparser::visitASTGraphElementPatternFiller(
    const ASTGraphElementPatternFiller* node, void* data) {
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  if (node->variable_name() != nullptr) {
    node->variable_name()->Accept(this, data);
  }
  if (node->label_filter() != nullptr) {
    node->label_filter()->Accept(this, data);
  }
  if (node->where_clause() != nullptr) {
    ABSL_DCHECK(node->property_specification() == nullptr);
    node->where_clause()->Accept(this, data);
  } else if (node->property_specification() != nullptr) {
    ABSL_DCHECK(node->where_clause() == nullptr);
    node->property_specification()->Accept(this, data);
  }
  if (node->edge_cost() != nullptr) {
    print("COST ");
    node->edge_cost()->Accept(this, data);
  }
}

void Unparser::visitASTGraphNodePattern(const ASTGraphNodePattern* node,
                                        void* data) {
  print("(");
  if (node->filler() != nullptr) {
    node->filler()->Accept(this, data);
  }
  print(")");
}

void Unparser::visitASTGraphPathMode(const ASTGraphPathMode* node, void* data) {
  switch (node->path_mode()) {
    case ASTGraphPathMode::WALK:
      println("WALK");
      break;
    case ASTGraphPathMode::TRAIL:
      println("TRAIL");
      break;
    case ASTGraphPathMode::SIMPLE:
      println("SIMPLE");
      break;
    case ASTGraphPathMode::ACYCLIC:
      println("ACYCLIC");
      break;
    case ASTGraphPathMode::PATH_MODE_UNSPECIFIED:
      ABSL_LOG(ERROR) << "Path mode is not set";
      break;
  }
}

void Unparser::visitASTGraphEdgePattern(const ASTGraphEdgePattern* node,
                                        void* data) {
  std::string left, right, abbreviated;
  if (node->lhs_hint() != nullptr) {
    node->lhs_hint()->Accept(this, data);
  }
  switch (node->orientation()) {
    case ASTGraphEdgePattern::ANY:
      left = "-[";
      right = "]-";
      abbreviated = "-";
      break;
    case ASTGraphEdgePattern::LEFT:
      left = "<-[";
      right = "]-";
      abbreviated = "<-";
      break;
    case ASTGraphEdgePattern::RIGHT:
      left = "-[";
      right = "]->";
      abbreviated = "->";
      break;
    case ASTGraphEdgePattern::EDGE_ORIENTATION_NOT_SET:
      ABSL_LOG(FATAL) << "Edge orientation is not set";  // Crash OK
      break;
  }

  // Abbreviated edge pattern has null filler.
  if (node->filler() != nullptr) {
    print(left);
    node->filler()->Accept(this, data);
    print(right);
  } else {
    print(abbreviated);
  }

  if (node->rhs_hint() != nullptr) {
    node->rhs_hint()->Accept(this, data);
  }
  if (node->quantifier() != nullptr) {
    node->quantifier()->Accept(this, data);
  }
}

void Unparser::visitASTGraphLhsHint(const ASTGraphLhsHint* node, void* data) {
  node->hint()->Accept(this, data);
}

void Unparser::visitASTGraphRhsHint(const ASTGraphRhsHint* node, void* data) {
  node->hint()->Accept(this, data);
}

void Unparser::visitASTGraphLabelFilter(const ASTGraphLabelFilter* node,
                                        void* data) {
  println();
  print("IS ");
  node->label_expression()->Accept(this, data);
}

void Unparser::visitASTGraphElementLabel(const ASTGraphElementLabel* node,
                                         void* data) {
  node->name()->Accept(this, data);
}

void Unparser::visitASTGraphWildcardLabel(const ASTGraphWildcardLabel* node,
                                          void* data) {
  print("%");
}

void Unparser::visitASTGraphLabelOperation(const ASTGraphLabelOperation* node,
                                           void* data) {
  // We never need to parenthesize a label expression except if it is an
  // operation, so these parentheses are not needed for any other concrete
  // class (simple or wildcard)
  if (node->parenthesized()) {
    print("(");
  }

  switch (node->op_type()) {
    case ASTGraphLabelOperation::NOT:
      print("!");
      ABSL_DCHECK_EQ(node->inputs().size(), 1);
      node->inputs().front()->Accept(this, data);
      break;
    case ASTGraphLabelOperation::AND:
      ABSL_DCHECK_GE(node->inputs().size(), 2);
      UnparseChildrenWithSeparator(node, data, "&");
      break;
    case ASTGraphLabelOperation::OR:
      ABSL_DCHECK_GE(node->inputs().size(), 2);
      UnparseChildrenWithSeparator(node, data, "|");
      break;
    case ASTGraphLabelOperation::OPERATION_TYPE_UNSPECIFIED:
      ABSL_LOG(ERROR) << "Graph label operation type is not set";
      break;
  }

  if (node->parenthesized()) {
    print(")");
  }
}

void Unparser::visitASTGraphIsLabeledPredicate(
    const ASTGraphIsLabeledPredicate* node, void* data) {
  PrintOpenParenIfNeeded(node);
  std::string separator = node->is_not() ? "IS NOT LABELED" : "IS LABELED";
  UnparseChildrenWithSeparator(node, data, separator);
  PrintCloseParenIfNeeded(node);
}

void Unparser::visitASTGraphPathPattern(const ASTGraphPathPattern* node,
                                        void* data) {
  // Only parenthesized path patterns can have WHERE clauses.
  ABSL_DCHECK(node->parenthesized() || node->where_clause() == nullptr);
  if (node->hint() != nullptr) {
    // Path-path hint
    node->hint()->Accept(this, data);
    println();
  }

  auto unparse_path_mode_and_input_patterns = [&]() {
    if (node->path_name() != nullptr) {
      node->path_name()->Accept(this, data);
      print("= ");
    }
    if (node->search_prefix() != nullptr) {
      node->search_prefix()->Accept(this, data);
    }
    if (node->path_mode() != nullptr) {
      node->path_mode()->Accept(this, data);
    }
    UnparseVectorWithSeparator(node->input_pattern_list(), data,
                               /*separator=*/"", /*break_line=*/true);
  };

  if (!node->parenthesized()) {
    unparse_path_mode_and_input_patterns();
  } else {
    println("(");
    {
      Formatter::Indenter indenter(&formatter_);
      unparse_path_mode_and_input_patterns();
      if (node->where_clause() != nullptr) {
        node->where_clause()->Accept(this, data);
      }
    }
    println();
    print(")");
  }
  if (node->quantifier() != nullptr) {
    node->quantifier()->Accept(this, data);
  }
}

void Unparser::visitASTGraphPathSearchPrefix(
    const ASTGraphPathSearchPrefix* node, void* data) {
  switch (node->type()) {
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::SHORTEST: {
      if (node->path_count() == nullptr) {
        print("ANY SHORTEST ");
      } else {
        print("SHORTEST ");
        node->path_count()->Accept(this, data);
        print(" ");
      }
      break;
    }
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::ANY: {
      if (node->path_count() == nullptr) {
        print("ANY ");
      } else {
        print("ANY ");
        node->path_count()->Accept(this, data);
        print(" ");
      }
      break;
    }
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::CHEAPEST: {
      if (node->path_count() == nullptr) {
        print("ANY CHEAPEST ");
      } else {
        print("CHEAPEST ");
        node->path_count()->Accept(this, data);
        print(" ");
      }
      break;
    }
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL: {
      print("ALL ");
      break;
    }
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL_SHORTEST: {
      print("ALL SHORTEST ");
      break;
    }
    case ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL_CHEAPEST: {
      print("ALL CHEAPEST ");
      break;
    }
    default: {
      ABSL_LOG(ERROR) << "Path search prefix type is not recognized: "
                  << node->type();
      break;
    }
  }
}

void Unparser::visitASTGraphPathSearchPrefixCount(
    const ASTGraphPathSearchPrefixCount* node, void* data) {
  node->path_count()->Accept(this, data);
}

void Unparser::visitASTGraphPattern(const ASTGraphPattern* node, void* data) {
  UnparseVectorWithSeparator(node->paths(), data, ",\n");
  println();
  if (node->where_clause() != nullptr) {
    node->where_clause()->Accept(this, data);
  }
}

void Unparser::visitASTGraphPropertySpecification(
    const ASTGraphPropertySpecification* node, void* data) {
  ABSL_DCHECK(!node->property_name_and_value().empty());
  print("{");
  UnparseVectorWithSeparator(node->property_name_and_value(), data, ", ");
  print("}");
}

void Unparser::visitASTGraphPropertyNameAndValue(
    const ASTGraphPropertyNameAndValue* node, void* data) {
  ABSL_DCHECK(node->property_name() != nullptr);
  ABSL_DCHECK(node->value() != nullptr);
  visitASTIdentifier(node->property_name(), data);
  print(":");
  node->value()->Accept(this, data);
}

void Unparser::visitASTDefineMacroStatement(const ASTDefineMacroStatement* node,
                                            void* data) {
  print("DEFINE MACRO ");
  node->name()->Accept(this, data);
  node->body()->Accept(this, data);
}

void Unparser::visitASTSetOperationMetadataList(
    const ASTSetOperationMetadataList* node, void* data) {}

void Unparser::visitASTSetOperationMetadata(const ASTSetOperationMetadata* node,
                                            void* data) {
  if (node->column_propagation_mode() != nullptr &&
      node->column_propagation_mode()->value() != ASTSetOperation::STRICT) {
    node->column_propagation_mode()->Accept(this, data);
  }
  node->op_type()->Accept(this, data);
  if (node->hint() != nullptr) {
    node->hint()->Accept(this, data);
  }
  node->all_or_distinct()->Accept(this, data);
  if (node->column_propagation_mode() != nullptr &&
      node->column_propagation_mode()->value() == ASTSetOperation::STRICT) {
    node->column_propagation_mode()->Accept(this, data);
  }
  if (node->column_match_mode() != nullptr) {
    node->column_match_mode()->Accept(this, data);
  }
  if (node->corresponding_by_column_list() != nullptr) {
    node->corresponding_by_column_list()->Accept(this, data);
  }
}

void Unparser::visitASTSetOperationAllOrDistinct(
    const ASTSetOperationAllOrDistinct* node, void* data) {
  switch (node->value()) {
    case ASTSetOperation::ALL:
      print("ALL");
      break;
    case ASTSetOperation::DISTINCT:
      print("DISTINCT");
      break;
    case ASTSetOperation::ALL_OR_DISTINCT_NOT_SET:
      print("<UNKNOWN ALL_OR_DISTINCT>");
  }
}

void Unparser::visitASTSetOperationType(const ASTSetOperationType* node,
                                        void* data) {
  switch (node->value()) {
    case ASTSetOperation::UNION:
      print("UNION");
      break;
    case ASTSetOperation::EXCEPT:
      print("EXCEPT");
      break;
    case ASTSetOperation::INTERSECT:
      print("INTERSECT");
      break;
    case ASTSetOperation::NOT_SET:
      print("<UNKNOWN SET OPERATOR>");
  }
}

void Unparser::visitASTSetOperationColumnMatchMode(
    const ASTSetOperationColumnMatchMode* node, void* data) {
  switch (node->value()) {
    case ASTSetOperation::CORRESPONDING:
      print("CORRESPONDING");
      break;
    case ASTSetOperation::CORRESPONDING_BY:
      print("CORRESPONDING BY");
      break;
    case ASTSetOperation::BY_NAME:
      print("BY NAME");
      break;
    case ASTSetOperation::BY_NAME_ON:
      print("BY NAME ON");
      break;
    case ASTSetOperation::BY_POSITION:
      break;
  }
}

void Unparser::visitASTSetOperationColumnPropagationMode(
    const ASTSetOperationColumnPropagationMode* node, void* data) {
  switch (node->value()) {
    case ASTSetOperation::FULL:
      print("FULL");
      break;
    case ASTSetOperation::LEFT:
      print("LEFT");
      break;
    case ASTSetOperation::STRICT:
      print("STRICT");
      break;
    case ASTSetOperation::INNER:
      print("INNER");
      break;
  }
}

void Unparser::visitASTExpressionWithOptAlias(
    const ASTExpressionWithOptAlias* node, void* data) {
  node->expression()->Accept(this, data);
  if (node->optional_alias() != nullptr) {
    node->optional_alias()->Accept(this, data);
  }
}

void Unparser::visitASTMacroBody(const ASTMacroBody* node, void* data) {
  UnparseLeafNode(node);
}

void Unparser::visitASTMapType(const ASTMapType* node, void* data) {
  print("MAP<");
  node->key_type()->Accept(this, data);
  print(", ");
  node->value_type()->Accept(this, data);
  print(">");
}

void Unparser::visitASTLockMode(const ASTLockMode* node, void* data) {
  switch (node->strength()) {
    case ASTLockMode::UPDATE:
      println();
      print("FOR UPDATE");
      break;
    case ASTLockMode::NOT_SET:
      break;
  }
}

void Unparser::visitASTPipeRecursiveUnion(const ASTPipeRecursiveUnion* node,
                                          void* data) {
  Formatter::PipeAndIndent pipe_and_indent(&formatter_);
  print("RECURSIVE");
  visitASTChildren(node, data);
}

void Unparser::visitASTRunStatement(const ASTRunStatement* node, void* data) {
  print("RUN");
  if (node->target_path_expression() != nullptr) {
    node->target_path_expression()->Accept(this, data);
  }
  if (node->target_string_literal() != nullptr) {
    node->target_string_literal()->Accept(this, data);
  }
  print("(");
  UnparseVectorWithSeparator(node->arguments(), data, ",");
  print(")");
}

void Unparser::visitASTCreateSequenceStatement(
    const ASTCreateSequenceStatement* node, void* data) {
  print(GetCreateStatementPrefix(node, "SEQUENCE"));
  node->name()->Accept(this, data);
  if (node->options_list() != nullptr) {
    print("OPTIONS");
    node->options_list()->Accept(this, data);
  }
}

void Unparser::visitASTAlterSequenceStatement(
    const ASTAlterSequenceStatement* node, void* data) {
  print("ALTER SEQUENCE");
  VisitAlterStatementBase(node, data);
}

}  // namespace parser
}  // namespace googlesql
