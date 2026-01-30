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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_REGISTRATION_H_
#define GOOGLESQL_ANALYZER_REWRITERS_REGISTRATION_H_

#include <vector>

#include "googlesql/public/options.pb.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace googlesql {

class Rewriter;

// A registry for Rewriters. Each Rewriter is mapped from the ResolvedASTRewrite
// key that it implements. We look them up when we rewrite a resolved AST
// according to whichever rewrites are enabled in the AnalyzerOptions.
//
// Intuitively, you might expect us just to map the enums directly to the
// rewriters using a switch statement. But we cannot have a direct dependency
// from the analyzer to the rewriters, since the rewriters may depend on the
// analyzer. This class breaks that cycle by allowing the analyzer to access the
// rewriters without depending directly on their implementation libraries.
class RewriteRegistry {
 public:
  // Registers the given rewriter for a given rewrite rule. Does not take
  // ownership. You must not register a given 'key' more than once.
  void Register(ResolvedASTRewrite key, const Rewriter* rewriter);

  // Returns the rewriter associated with a given rewrite rule. Returns nullptr
  // when the rule hasn't been registered. (This is a failure in debug mode.)
  const Rewriter* Get(ResolvedASTRewrite key) const;

  // Return a copy of the rewriter keys in the order that they are applied by
  // the rewriter framework.
  std::vector<ResolvedASTRewrite> registration_order() const;

  static RewriteRegistry& global_instance();

 private:
  // Not to be constructed or destructed.
  RewriteRegistry() = default;
  ~RewriteRegistry() = default;

  mutable absl::Mutex mu_;
  absl::flat_hash_map<ResolvedASTRewrite, const Rewriter*> rewriters_
      ABSL_GUARDED_BY(mu_);
  std::vector<ResolvedASTRewrite> registration_order_ ABSL_GUARDED_BY(mu_);
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_REGISTRATION_H_
