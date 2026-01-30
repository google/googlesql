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

#include "googlesql/analyzer/rewriters/registration.h"

#include <memory>
#include <string>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {
namespace {

// A base rewriter that does nothing, but serves as the base class for our dummy
// rewriters below.
class Base : public Rewriter {
 public:
  ~Base() override = default;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    return absl::UnimplementedError("");
  }

  std::string Name() const override { return "Test rewriter"; }
};

// These fakes are only used to verify whether we are returning the correct
// object from the registry.
class FakeAnonRewriter : public Base {
 public:
  ~FakeAnonRewriter() override = default;
};
class FakeBuiltinFunctionInlinerRewriter : public Base {
 public:
  ~FakeBuiltinFunctionInlinerRewriter() override = default;
};

TEST(RegistrationTest, Test) {
  RewriteRegistry& r = RewriteRegistry::global_instance();

  FakeAnonRewriter anon_instance;
  r.Register(ResolvedASTRewrite::REWRITE_ANONYMIZATION, &anon_instance);
  FakeBuiltinFunctionInlinerRewriter fn_inliner_instance;
  r.Register(ResolvedASTRewrite::REWRITE_BUILTIN_FUNCTION_INLINER,
             &fn_inliner_instance);

  {
    const Rewriter* anon = r.Get(ResolvedASTRewrite::REWRITE_ANONYMIZATION);
    ASSERT_EQ(anon, &anon_instance);
  }

  {
    const Rewriter* fn_inliner =
        r.Get(ResolvedASTRewrite::REWRITE_BUILTIN_FUNCTION_INLINER);
    ASSERT_EQ(fn_inliner, &fn_inliner_instance);
  }

  EXPECT_DEBUG_DEATH(
      {
        EXPECT_EQ(r.Get(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION), nullptr);
      },
      ".*Rewriter was not registered.*");

  EXPECT_DEBUG_DEATH(
      { r.Register(ResolvedASTRewrite::REWRITE_ANONYMIZATION, nullptr); },
      ".*Key conflict for GoogleSQL Rewriter.*");
}

}  // namespace
}  // namespace googlesql
