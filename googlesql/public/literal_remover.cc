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

// Implements ReplaceLiteralsByParameters method declared in analyzer.h.
// Tested by parse_locations.test.

#include "googlesql/public/literal_remover.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/container/node_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Compares ResolvedLiterals by parse location. The ParseLocationRange must be
// set on all compared literals.
struct LiteralParseLocationComparator {
  bool operator()(const ResolvedLiteral* l1, const ResolvedLiteral* l2)
      const {
    ABSL_DCHECK(l1->GetParseLocationRangeOrNULL() != nullptr);
    ABSL_DCHECK(l2->GetParseLocationRangeOrNULL() != nullptr);
    return l1->GetParseLocationRangeOrNULL()->start() <
           l2->GetParseLocationRangeOrNULL()->start();
  }
};

// Returns true if the given literals occur at the same location and have the
// same value (and hence type). Such literals are created in analytical
// functions. The ParseLocationRange must be set on all compared literals.
bool IsSameLiteral(const ResolvedLiteral* a, const ResolvedLiteral* b) {
  ABSL_DCHECK(a->GetParseLocationRangeOrNULL() != nullptr);
  ABSL_DCHECK(b->GetParseLocationRangeOrNULL() != nullptr);
  const ParseLocationRange& location_a = *a->GetParseLocationRangeOrNULL();
  const ParseLocationRange& location_b = *b->GetParseLocationRangeOrNULL();
  return location_a == location_b && a->value() == b->value();
}

absl::StatusOr<std::string> GenerateParameterName(
    const ResolvedLiteral* literal, const AnalyzerOptions& analyzer_options,
    const ParameterNameOverrideCallback& override_callback,
    GeneratedParameterMap* generated_parameters, int* index) {
  GOOGLESQL_RET_CHECK(generated_parameters != nullptr);
  const QueryParametersMap& parameters = analyzer_options.query_parameters();
  auto has_conflict = [&parameters,
                       generated_parameters](absl::string_view param_name) {
    return googlesql_base::ContainsKey(parameters, absl::AsciiStrToLower(param_name)) ||
           generated_parameters->contains(param_name);
  };
  if (override_callback != nullptr) {
    std::optional<std::string> name = override_callback(literal);
    if (name.has_value()) {
      if (has_conflict(*name)) {
        return googlesql_base::InvalidArgumentErrorBuilder()
               << "Generated parameter name from the override callback is "
                  "conflict with an existing parameter: "
               << *name << " for literal: " << literal->DebugString();
      }
      return *name;
    }
  }
  std::string type_name = Type::TypeKindToString(
      literal->type()->kind(), analyzer_options.language().product_mode());
  std::string param_name;
  do {
    // User parameters are less likely to start with underscores.
    param_name = absl::StrCat("_p", (*index)++, "_", type_name);
  } while (has_conflict(param_name));
  return param_name;
}

template <typename T>
absl::Status AddCollationLiteralToIgnoringSetIfPresent(
    const T* node,
    absl::flat_hash_set<const ResolvedLiteral*>& ignore_literals) {
  if (node->collation_name() != nullptr) {
    GOOGLESQL_RET_CHECK_EQ(node->collation_name()->node_kind(), RESOLVED_LITERAL);
    // <collation_name> may be generated from CTAS query and doesn't have a
    // parse location attached.
    if (node->collation_name()->GetParseLocationRangeOrNULL() != nullptr) {
      ignore_literals.insert(
          node->collation_name()->template GetAs<ResolvedLiteral>());
    }
  }
    return absl::OkStatus();
}

absl::Status AddAnnotationLiteralsToIgnoringSet(
    const std::vector<const ResolvedNode*>& annotation_nodes,
    absl::flat_hash_set<const ResolvedLiteral*>& ignore_literals) {
    std::queue<const ResolvedColumnAnnotations*> annotation_nodes_queue;
    for (const ResolvedNode* node : annotation_nodes) {
    const ResolvedColumnAnnotations* annotation_node =
        node->GetAs<ResolvedColumnAnnotations>();
    annotation_nodes_queue.push(annotation_node);
    }
  std::vector<const ResolvedNode*> tmp_vector;
  while (!annotation_nodes_queue.empty()) {
    const ResolvedColumnAnnotations* node = annotation_nodes_queue.front();
    annotation_nodes_queue.pop();
    if (node != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          AddCollationLiteralToIgnoringSetIfPresent<ResolvedColumnAnnotations>(
              node, ignore_literals));
    }
    tmp_vector.clear();
    node->GetChildNodes(&tmp_vector);
    for (const ResolvedNode* tmp_node : tmp_vector) {
      if (tmp_node->node_kind() == RESOLVED_COLUMN_ANNOTATIONS)
        annotation_nodes_queue.push(
            tmp_node->GetAs<ResolvedColumnAnnotations>());
    }
  }
  return absl::OkStatus();
}

absl::Status AddDefaultCollationLiteralToIgnoringSet(
    const ResolvedStatement* stmt,
    absl::flat_hash_set<const ResolvedLiteral*>& ignore_literals) {
  switch (stmt->node_kind()) {
    case RESOLVED_CREATE_TABLE_STMT: {
      const ResolvedCreateTableStmt* create_table_stmt =
          stmt->GetAs<ResolvedCreateTableStmt>();
      GOOGLESQL_RETURN_IF_ERROR(
          AddCollationLiteralToIgnoringSetIfPresent<ResolvedCreateTableStmt>(
              create_table_stmt, ignore_literals));
      break;
    }
    case RESOLVED_CREATE_EXTERNAL_TABLE_STMT: {
      const ResolvedCreateExternalTableStmt* create_external_table_stmt =
          stmt->GetAs<ResolvedCreateExternalTableStmt>();
      GOOGLESQL_RETURN_IF_ERROR(AddCollationLiteralToIgnoringSetIfPresent<
                      ResolvedCreateExternalTableStmt>(
          create_external_table_stmt, ignore_literals));
      break;
    }
    case RESOLVED_CREATE_SCHEMA_STMT: {
      const ResolvedCreateSchemaStmt* create_schema_stmt =
          stmt->GetAs<ResolvedCreateSchemaStmt>();
      GOOGLESQL_RETURN_IF_ERROR(
          AddCollationLiteralToIgnoringSetIfPresent<ResolvedCreateSchemaStmt>(
              create_schema_stmt, ignore_literals));
      break;
    }
    case RESOLVED_ALTER_TABLE_STMT: {
      const ResolvedAlterTableStmt* alter_table_stmt =
          stmt->GetAs<ResolvedAlterTableStmt>();
      for (const std::unique_ptr<const googlesql::ResolvedAlterAction>& action :
           alter_table_stmt->alter_action_list()) {
        if (action->node_kind() == RESOLVED_SET_COLLATE_CLAUSE) {
          const ResolvedSetCollateClause* set_collate_clause =
              action->GetAs<ResolvedSetCollateClause>();
          GOOGLESQL_RETURN_IF_ERROR(AddCollationLiteralToIgnoringSetIfPresent<
                          ResolvedSetCollateClause>(set_collate_clause,
                                                    ignore_literals));
        }
      }
      break;
    }
    case RESOLVED_ALTER_SCHEMA_STMT: {
      const ResolvedAlterSchemaStmt* alter_schema_stmt =
          stmt->GetAs<ResolvedAlterSchemaStmt>();
      for (const std::unique_ptr<const googlesql::ResolvedAlterAction>& action :
           alter_schema_stmt->alter_action_list()) {
        if (action->node_kind() == RESOLVED_SET_COLLATE_CLAUSE) {
          const ResolvedSetCollateClause* set_collate_clause =
              action->GetAs<ResolvedSetCollateClause>();
          GOOGLESQL_RETURN_IF_ERROR(AddCollationLiteralToIgnoringSetIfPresent<
                          ResolvedSetCollateClause>(set_collate_clause,
                                                    ignore_literals));
        }
      }
      break;
    }
    default:
      // Ignore the other statements.
      break;
  }
  return absl::OkStatus();
}

absl::Status AddLimitOffsetLiteralsToIgnoringSet(
    const std::vector<const ResolvedNode*>& limit_offset_nodes,
    absl::flat_hash_set<const ResolvedLiteral*>& ignore_literals) {
  for (const ResolvedNode* node : limit_offset_nodes) {
    const auto* limit_offset_scan = node->GetAs<ResolvedLimitOffsetScan>();
    if (const ResolvedNode* limit = limit_offset_scan->limit();
        limit != nullptr && limit->node_kind() == RESOLVED_LITERAL &&
        limit->GetParseLocationRangeOrNULL() != nullptr) {
      ignore_literals.insert(limit->GetAs<ResolvedLiteral>());
    }
    if (const ResolvedNode* offset = limit_offset_scan->offset();
        offset != nullptr && offset->node_kind() == RESOLVED_LITERAL &&
        offset->GetParseLocationRangeOrNULL() != nullptr) {
      ignore_literals.insert(offset->GetAs<ResolvedLiteral>());
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ReplaceLiteralsByParameters(
    absl::string_view sql,
    const LiteralReplacementOptions& literal_replacement_options,
    const AnalyzerOptions& analyzer_options, const ResolvedStatement* stmt,
    LiteralReplacementMap* literal_map,
    GeneratedParameterMap* generated_parameters, std::string* result_sql) {
  ABSL_CHECK(stmt != nullptr);
  literal_map->clear();
  generated_parameters->clear();
  result_sql->clear();

  // Collect all <literals> that are for options (hints) that have parse
  // locations.
  std::vector<const ResolvedNode*> option_nodes;
  stmt->GetDescendantsWithKinds({RESOLVED_OPTION}, &option_nodes);
  absl::flat_hash_set<const ResolvedLiteral*> ignore_literals;
  for (const ResolvedNode* node : option_nodes) {
    const ResolvedOption* option = node->GetAs<ResolvedOption>();
    if (option->value()->node_kind() == RESOLVED_LITERAL &&
        option->value()->GetParseLocationRangeOrNULL() != nullptr) {
      const ResolvedLiteral* option_literal =
          option->value()->GetAs<ResolvedLiteral>();
      if (literal_replacement_options.ignored_option_names.contains(
              option->name())) {
        ignore_literals.insert(option_literal);
      }
    }
  }

  // Collect all <literals> that are for collations that have parse locations.
  std::vector<const ResolvedNode*> annotation_nodes;
  stmt->GetDescendantsWithKinds({RESOLVED_COLUMN_ANNOTATIONS},
                                &annotation_nodes);
  GOOGLESQL_RETURN_IF_ERROR(
      AddAnnotationLiteralsToIgnoringSet(annotation_nodes, ignore_literals));
  GOOGLESQL_RETURN_IF_ERROR(
      AddDefaultCollationLiteralToIgnoringSet(stmt, ignore_literals));

  if (!literal_replacement_options.scrub_limit_offset) {
    // Collect all <literals> that appearing in LIMIT, OFFSET clauses that have
    // parse locations.
    std::vector<const ResolvedNode*> limit_offset_nodes;
    stmt->GetDescendantsSatisfying(&ResolvedNode::Is<ResolvedLimitOffsetScan>,
                                   &limit_offset_nodes);
    GOOGLESQL_RETURN_IF_ERROR(AddLimitOffsetLiteralsToIgnoringSet(limit_offset_nodes,
                                                        ignore_literals));
  }

  // Collect all <literals> that have a parse location and not marked to be
  // preserved.
  std::vector<const ResolvedNode*> literal_nodes;
  stmt->GetDescendantsWithKinds({RESOLVED_LITERAL}, &literal_nodes);
  std::vector<const ResolvedLiteral*> literals;
  for (const ResolvedNode* node : literal_nodes) {
    const ResolvedLiteral* literal = node->GetAs<ResolvedLiteral>();
    if (literal->GetParseLocationRangeOrNULL() != nullptr &&
        !literal->preserve_in_literal_remover() &&
        !ignore_literals.contains(literal)) {
      literals.push_back(literal);
    }
  }
  std::sort(literals.begin(), literals.end(), LiteralParseLocationComparator());

  // <literals> are ordered by parse location. The loop below constructs
  // <result_sql> by appending the <replacement> string for each encountered
  // literal.
  int prefix_offset = 0;  // Offset in <sql> of the text preceding the literal.
  int parameter_index = 0;  // Index used to generate unique parameter names.
  std::string parameter_name;  // Most recently used parameter name.
  for (int i = 0; i < literals.size(); ++i) {
    const ResolvedLiteral* literal = literals[i];
    const ParseLocationRange* location = literal->GetParseLocationRangeOrNULL();
    GOOGLESQL_RET_CHECK(location != nullptr);
    const int first_offset = location->start().GetByteOffset();
    const int last_offset = location->end().GetByteOffset();
    GOOGLESQL_RET_CHECK(first_offset >= 0 && last_offset > first_offset &&
              last_offset <= sql.length());
    // Since literals are ordered by location, literals representing the same
    // input location are guaranteed to be consecutive.
    if (i > 0 && IsSameLiteral(literal, literals[i - 1])) {
      // Each occurrence of a literal maps to the same parameter name.
      GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(literal_map, literal, parameter_name));
      continue;
    }
    GOOGLESQL_RET_CHECK(prefix_offset == 0 || prefix_offset < last_offset)
        << "Parse locations of literals are broken:"
        << "\nQuery: " << sql << "\nResolved AST: " << stmt->DebugString();
    GOOGLESQL_ASSIGN_OR_RETURN(
        parameter_name,
        GenerateParameterName(
            literal, analyzer_options,
            literal_replacement_options.parameter_name_override_callback,
            generated_parameters, &parameter_index));

    absl::StrAppend(result_sql,
                    sql.substr(prefix_offset, first_offset - prefix_offset),
                    "@", parameter_name);
    // Add a space after the parameter name if the original literal was followed
    // by a character that can occur in an identifier or begin of a hint. This
    // is required in expressions like x='foobar'AND <other condition>.
    if (last_offset < sql.size()) {
      char ch = sql[last_offset];
      if (absl::ascii_isalnum(ch) || ch == '_' || ch == '@') {
        absl::StrAppend(result_sql, " ");
      }
    }

    GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(literal_map, literal, parameter_name));
    GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(generated_parameters, parameter_name,
                                      literal->value()))
        << parameter_name;
    prefix_offset = last_offset;
  }
  absl::StrAppend(result_sql, sql.substr(prefix_offset));
  return absl::OkStatus();
}

}  // namespace googlesql
