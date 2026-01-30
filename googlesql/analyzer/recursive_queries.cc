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

#include "googlesql/analyzer/recursive_queries.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/container_hash_equals.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/parser/visit_result.h"
#include "googlesql/public/id_string.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// This visitor implements a subset of what TableNameResolver does.
// It seems possible to use TableNameResolver instead of this, with some API
// changes. This may be worthwhile later if significant work updating this
// visitor is required.

// Helper class for examining table references under a series of "root" nodes,
// determining which "root" nodes are referenced by other root nodes.
//   <NodeType> denotes the type of each root node being examined, and must
//   derive from ASTNode.
template <typename NodeType>
class FindTableReferencesVisitor : public NonRecursiveParseTreeVisitor {
 public:
  // Map which associates each node with a set of nodes referenced by it.
  // All nodes contained within any value sets are also keys in the map.
  using NodeReferenceMap =
      absl::flat_hash_map<const NodeType*,
                          absl::flat_hash_set<const NodeType*>>;

  // Map which associates each node with a table name used to reference that
  // node within a query.
  using NodeNameMap =
      absl::flat_hash_map<const NodeType*, std::vector<IdString>>;

  // Searches the descendants of each node in 'roots', returning a map
  // describing which nodes each nodes are referenced by which other nodes.
  //
  // <roots> specifies the nodes to search. The key is the node, the value
  //    is the qualified name associated with the node. Any table reference
  //    which matches the value name is assumed to be a reference to the node
  //    key, unless the name is shadowed by an inner WITH alias.
  //
  //    For example, if a key is an ASTWithEntry, the value should be
  //    single-element vector corresponding to the WITH alias.
  //    TODO: If the key is the subquery of an
  //    ASTCreateViewStatement, the value should be the name of the view being
  //    created.
  //
  //    Table references are matched to values using case-insensitive semantics.
  //
  // Returns a map associating every node in <roots> with a set of nodes in
  // <roots> which it references.
  static absl::StatusOr<NodeReferenceMap> Run(const NodeNameMap& roots) {
    FindTableReferencesVisitor visitor;
    return visitor.RunInternal(roots);
  }

 private:
  absl::StatusOr<NodeReferenceMap> RunInternal(const NodeNameMap& roots) {
    for (const auto& pair : roots) {
      if (pair.second.size() == 1) {
        inner_aliases_[pair.second.front()] = 0;
      }
      GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&name_to_node_map_, pair.second,
                                        pair.first))
          << "Multiple roots have same name";
      references_[pair.first] = absl::flat_hash_set<const NodeType*>{};
    }
    for (const auto& pair : roots) {
      root_node_ = pair.first;
      root_name_ = pair.second;
      GOOGLESQL_RETURN_IF_ERROR(root_node_->TraverseNonRecursive(this));
    }
    return references_;
  }

  using IdStringVectorNodeMap = absl::flat_hash_map<
      std::vector<IdString>, const NodeType*,
      ContainerHash<std::vector<IdString>, IdStringCaseHash>,
      ContainerEquals<std::vector<IdString>, IdStringCaseEqualFunc>>;

  FindTableReferencesVisitor() = default;
  FindTableReferencesVisitor(const FindTableReferencesVisitor&) = delete;
  FindTableReferencesVisitor& operator=(const FindTableReferencesVisitor&) =
      delete;

  absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) override {
    return VisitResult::VisitChildren(node);
  }

  void AddInnerAlias(const ASTAliasedQuery* entry) {
    IdString alias_name = entry->alias()->GetAsIdString();
    if (inner_aliases_.contains(alias_name)) {
      ++inner_aliases_[alias_name];
    }
  }

  void RemoveInnerAlias(const ASTAliasedQuery* entry) {
    IdString alias_name = entry->alias()->GetAsIdString();
    if (inner_aliases_.contains(alias_name)) {
      --inner_aliases_[alias_name];
    }
  }

  absl::StatusOr<VisitResult> visitASTAliasedQuery(
      const ASTAliasedQuery* node) override {
    return VisitResult::VisitChildren(node, [this, node]() {
      if (node != static_cast<const ASTNode*>(root_node_) &&
          !node->parent()->parent()->GetAsOrDie<ASTWithClause>()->recursive()) {
        // After visiting the entry of an inner, non-recursive WITH, we need
        // to associate the alias as belonging to an inner WITH entry for the
        // duration of the WITH clause containing it.
        //
        // If the inner WITH is recursive, this was already done back in
        // visitASTWithClause(), so we don't want to do it again.
        AddInnerAlias(node);
      }
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTQuery(const ASTQuery* node) override {
    if (node->with_clause() != nullptr) {
      return VisitResult::VisitChildren(node, [this, node]() {
        // Inner WITH entries are now out-of-scope.
        for (const ASTWithClauseEntry* entry : node->with_clause()->entries()) {
          if (entry->aliased_query() != nullptr) {
            RemoveInnerAlias(entry->aliased_query());
          }
        }
        return absl::OkStatus();
      });
    }
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTWithClause(
      const ASTWithClause* node) override {
    // For recursive WITH, add all inner aliases up front, for the entire WITH
    // clause; for non-recursive WITH, inner aliases are added only when we're
    // about to traverse the entry that defines it.
    if (node->recursive()) {
      for (const ASTWithClauseEntry* entry : node->entries()) {
        if (entry->aliased_query() != nullptr) {
          AddInnerAlias(entry->aliased_query());
        }
      }
    }
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTTablePathExpression(
      const ASTTablePathExpression* node) override {
    if (node->path_expr() == nullptr) {
      // Table path expression does not have a direct path. Example: UNNEST(...)
      return VisitResult::VisitChildren(node);
    }
    std::vector<IdString> alias_name = node->path_expr()->ToIdStringVector();
    if (!name_to_node_map_.contains(alias_name)) {
      // Table name does not correspond to one of our root nodes.
      return VisitResult::Empty();
    }
    if (alias_name.size() == 1 && inner_aliases_[alias_name.front()] > 0) {
      // The reference to the root node has been shadowed by an inner WITH
      // alias - ignore.
      return VisitResult::Empty();
    }

    // This is indeed a reference to one of our root nodes.
    references_.at(root_node_).insert(name_to_node_map_.at(alias_name));
    return VisitResult::Empty();
  }

  // Keeps track of internal WITH aliases declared within <root_>, which shadow
  // table names in the search list.
  //   Key = Table name (only names in the search list are present, and only if
  //     they have length 1).
  //   Value = Number of nested WITH clauses, currently being processed, which
  //     shadow the name described in <Key>.
  absl::flat_hash_map<IdString, int, IdStringCaseHash, IdStringCaseEqualFunc>
      inner_aliases_;

  // Root node to traverse.
  const NodeType* root_node_;
  std::vector<IdString> root_name_;

  // Maps each WITH clause entry belonging to the ASTWithClause passed to Run()
  // with a list of other entries in the same WITH clause directly depended
  // upon.
  absl::flat_hash_map<const NodeType*, absl::flat_hash_set<const NodeType*>>
      references_;

  // Maps the name of each root node with the node itself.
  IdStringVectorNodeMap name_to_node_map_;
};

// Helper class which sorts the WITH entries based on the dependency graph.
//
// The implementation is based on the depth-first-search version of the
// topological sort algorithm described at
// https://en.wikipedia.org/wiki/Topological_sorting.
// It is modified to allow for direct self-references and to prevent long
// dependency chains from overflowing the runtime stack.
class WithEntrySorter {
 public:
  WithEntrySorter(const WithEntrySorter&) = delete;
  WithEntrySorter& operator=(const WithEntrySorter&) = delete;

  // Computes the sorted order and saves it for use by result(). Returns an
  // error status if the graph contains any cycles other than direct
  // self-references.
  static absl::StatusOr<WithEntrySortResult> Run(
      const ASTWithClause* with_clause);

 private:
  // Represents a task remaining in the work of sorting the WITH clause entries.
  struct Task {
    // The WITH clause entry to be processed.
    const ASTAliasedQuery* entry;

    // Indicates which processing stage we are in for the given entry.
    enum {
      // Indicates that we are starting the processing of a WITH entry.
      kStart,

      // Indicates that we are finishing up the processing of the current WITH
      // entry, after all its dependencies are fully processed.
      kFinish
    } stage;
  };

  WithEntrySorter() = default;

  absl::StatusOr<WithEntrySortResult> RunInternal(
      const ASTWithClause* with_clause);

  // Processes a WITH entry the first time we see it.
  absl::Status StartWithEntry(const ASTAliasedQuery* entry);

  // Called after StartWithEntry() and FinishWithEntry() have completed for all
  // of the entry's direct and indirect dependencies, excluding itself.
  //
  // Appends <entry> to <sorted_entries_>, while also adding it to
  // <processed_entries_>.
  absl::Status FinishWithEntry(const ASTAliasedQuery* entry);

  absl::Status PushCurrentChain(const ASTAliasedQuery* entry);
  absl::Status PopCurrentChain();

  // The WITH clause whose entries are to be sorted in dependency order.
  const ASTWithClause* with_clause_;

  // List of sorted WITH clause entries. Each entry is added to the end from
  // inside FinishWithEntry(), guaranteeing that every entry appears after all
  // of its dependencies.
  std::vector<const ASTAliasedQuery*> sorted_entries_;

  // WITH clause entries which have already been fully processed. Every element
  // in this set has had StartWithEntry() and FinishWithEntry() called on it,
  // plus all direct and indirect dependencies. These elements are the same as
  // those in <sorted_entries_>, but kept as a hash set for efficient lookup.
  absl::flat_hash_set<const ASTAliasedQuery*> processed_entries_;

  // Set of WITH clause entries which directly reference themselves. Such
  // clauses must resolve to a ResolvedRecursiveScan, rather than a normal query
  // scan.
  absl::flat_hash_set<const ASTAliasedQuery*> self_recursive_entries_;

  // Stack containing each of the pending operations necessary to complete the
  // sort.
  std::stack<Task> stack_;

  // Map associating each WITH clause with a list of WITH-clause entries
  // it directly references.
  absl::flat_hash_map<const ASTAliasedQuery*,
                      std::vector<const ASTAliasedQuery*>>
      references_;

  // Current chain of dependent elements being processed; each element in
  // current_chain_ has a direct dependency on the element following it. Used to
  // detect cycles.
  std::vector<const ASTAliasedQuery*> current_chain_;

  // Set of elements in <current_chain_>, allowing for a quick determinination
  // whether an element is in the current chain (whether we have a cycle).
  absl::flat_hash_set<const ASTAliasedQuery*> current_chain_elements_;
};

absl::StatusOr<WithEntrySortResult> WithEntrySorter::Run(
    const ASTWithClause* with_clause) {
  WithEntrySorter sorter;
  return sorter.RunInternal(with_clause);
}

absl::StatusOr<WithEntrySortResult> WithEntrySorter::RunInternal(
    const ASTWithClause* with_clause) {
  with_clause_ = with_clause;

  using FindRefsInWithClause = FindTableReferencesVisitor<ASTAliasedQuery>;

  absl::flat_hash_map<const ASTAliasedQuery*, std::vector<IdString>> roots;
  for (const ASTWithClauseEntry* entry : with_clause_->entries()) {
    if (entry->aliased_query() != nullptr) {
      const ASTAliasedQuery* aliased_query = entry->aliased_query();
      roots[aliased_query] = {aliased_query->alias()->GetAsIdString()};
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(FindRefsInWithClause::NodeReferenceMap references,
                   FindRefsInWithClause::Run(roots));

  absl::flat_hash_map<const ASTAliasedQuery*, size_t> entry_index_map;
  for (const ASTWithClauseEntry* entry : with_clause_->entries()) {
    if (entry->aliased_query() != nullptr) {
      entry_index_map[entry->aliased_query()] = entry_index_map.size();
    }
  }

  for (const auto& pair : references) {
    // Sort references in the order they appear in the with clause so that the
    // order they are traversed in is stable; while not strictly necessary for
    // correctness, this step is necessary to keep test output stable, which
    // can be affected by the ordering through tree dumps and error messages.
    std::vector<const ASTAliasedQuery*> sorted_references(pair.second.begin(),
                                                          pair.second.end());
    std::sort(sorted_references.begin(), sorted_references.end(),
              [entry_index_map](const ASTAliasedQuery* e1,
                                const ASTAliasedQuery* e2) -> bool {
                return entry_index_map.at(e1) < entry_index_map.at(e2);
              });

    references_[pair.first] = sorted_references;
  }

  for (const ASTWithClauseEntry* entry : with_clause_->entries()) {
    if (entry->aliased_query() != nullptr) {
      stack_.push(Task{entry->aliased_query(), Task::kStart});
    }
  }

  while (!stack_.empty()) {
    Task task = stack_.top();
    stack_.pop();
    switch (task.stage) {
      case Task::kStart:
        GOOGLESQL_RETURN_IF_ERROR(StartWithEntry(task.entry));
        break;
      case Task::kFinish:
        GOOGLESQL_RETURN_IF_ERROR(FinishWithEntry(task.entry));
        break;
    }
  }
  return WithEntrySortResult{std::move(sorted_entries_),
                             std::move(self_recursive_entries_)};
}

absl::Status WithEntrySorter::StartWithEntry(const ASTAliasedQuery* entry) {
  if (processed_entries_.contains(entry)) {
    return absl::OkStatus();  // Node already visited
  }

  if (current_chain_elements_.contains(entry)) {
    return MakeSqlErrorAt(with_clause_)
           << "Unsupported WITH entry dependency cycle: "
           << absl::StrJoin(current_chain_, " => ",
                            [](std::string* out, const ASTAliasedQuery* entry) {
                              absl::StrAppend(out,
                                              entry->alias()->GetAsString());
                            })
           << " => " << entry->alias()->GetAsString();
  }
  GOOGLESQL_RETURN_IF_ERROR(PushCurrentChain(entry));
  stack_.push(Task{entry, Task::kFinish});
  for (const ASTAliasedQuery* ref : references_.at(entry)) {
    if (ref == entry) {
      self_recursive_entries_.insert(ref);
    } else {
      stack_.push(Task{ref, Task::kStart});
    }
  }

  return absl::OkStatus();
}

absl::Status WithEntrySorter::FinishWithEntry(const ASTAliasedQuery* entry) {
  GOOGLESQL_RET_CHECK_EQ(current_chain_.back(), entry);
  GOOGLESQL_RETURN_IF_ERROR(PopCurrentChain());
  processed_entries_.insert(entry);
  sorted_entries_.push_back(entry);
  return absl::OkStatus();
}

absl::Status WithEntrySorter::PushCurrentChain(const ASTAliasedQuery* entry) {
  GOOGLESQL_RET_CHECK(!current_chain_elements_.contains(entry));
  current_chain_.push_back(entry);
  current_chain_elements_.insert(entry);
  return absl::OkStatus();
}

absl::Status WithEntrySorter::PopCurrentChain() {
  GOOGLESQL_RET_CHECK(!current_chain_.empty());
  GOOGLESQL_RET_CHECK(current_chain_elements_.erase(current_chain_.back()));
  current_chain_.pop_back();
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<WithEntrySortResult> SortWithEntries(
    const ASTWithClause* with_clause) {
  return WithEntrySorter::Run(with_clause);
}

absl::StatusOr<bool> IsViewSelfRecursive(
    const ASTCreateViewStatementBase* stmt) {
  if (!stmt->recursive()) {
    return false;
  }
  return ContainsReferenceToTable(stmt->query(),
                                  stmt->name()->ToIdStringVector());
}

template <typename NodeType>
absl::StatusOr<bool> ContainsReferenceToTable(
    const NodeType* node, const std::vector<IdString>& name) {
  using FindViewRefsVisitor = FindTableReferencesVisitor<NodeType>;
  using NodeNameMap = typename FindViewRefsVisitor::NodeNameMap;
  using NodeReferenceMap = typename FindViewRefsVisitor::NodeReferenceMap;

  NodeNameMap roots;
  roots[node] = name;
  GOOGLESQL_ASSIGN_OR_RETURN(NodeReferenceMap references,
                   FindViewRefsVisitor::Run(roots));
  GOOGLESQL_RET_CHECK_EQ(references.size(), 1);
  GOOGLESQL_RET_CHECK(references.contains(node));
  return references.at(node).contains(node);
}

template absl::StatusOr<bool> ContainsReferenceToTable<ASTQueryExpression>(
    const ASTQueryExpression* node, const std::vector<IdString>& name);

}  // namespace googlesql
