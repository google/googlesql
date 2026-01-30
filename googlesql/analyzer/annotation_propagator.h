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

#ifndef GOOGLESQL_ANALYZER_ANNOTATION_PROPAGATOR_H_
#define GOOGLESQL_ANALYZER_ANNOTATION_PROPAGATOR_H_

#include <memory>
#include <vector>

#include "googlesql/parser/ast_node.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/status.h"

namespace googlesql {

// This is a utility class that contains APIs to propagate annotations for
// a given resolved node. The annotations are propagated from child subtree
// towards the parent.
class AnnotationPropagator {
 public:
  AnnotationPropagator(const AnalyzerOptions& analyzer_options,
                       TypeFactory& type_factory);
  AnnotationPropagator(const AnnotationPropagator&) = delete;
  AnnotationPropagator& operator=(const AnnotationPropagator&) = delete;
  ~AnnotationPropagator() = default;

  // Checks and propagates annotations through <resolved_node>. If there is SQL
  // error thrown, the error will be attached to the location of <error_node>.
  // <error_node> could be nullptr to indicate there is no suitable location to
  // attach the error.
  absl::Status CheckAndPropagateAnnotations(const ASTNode* error_node,
                                            ResolvedNode* resolved_node);

 private:
  // Creates AnnotationSpec based on enabled language features and a list of
  // passed in annotations.
  void InitializeAnnotationSpecs(const AnalyzerOptions& analyzer_options);

  // Checks and propagates annotations through ResolvedRecursiveScan and throw
  // error at location of <error_node>. <error_node> could be nullptr to
  // indicate there is no suitable location to attach the error.
  absl::Status CheckAndPropagateAnnotationsForRecursiveScan(
      ResolvedRecursiveScan* recursive_scan, const ASTNode* error_node);

  // A list of AnnotationSpec owned by GoogleSQL.
  std::vector<std::unique_ptr<AnnotationSpec>> owned_annotation_specs_;

  // A complete list of AnnotationSpec to be used to propagate annotations.
  // Contains GoogleSQL annotations and engine specific annotations.
  // All AnnotationSpecs in <owned_annotation_specs_> are included in
  // <annotation_specs_>.
  // AnnotationSpec* elements in the vector are not owned by this class.
  std::vector<AnnotationSpec*> annotation_specs_;
  const AnalyzerOptions& analyzer_options_;  // Not owned.
  TypeFactory& type_factory_;                // Not owned.
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_ANNOTATION_PROPAGATOR_H_
