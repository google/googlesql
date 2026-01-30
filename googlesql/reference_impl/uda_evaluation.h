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

#ifndef GOOGLESQL_REFERENCE_IMPL_UDA_EVALUATION_H_
#define GOOGLESQL_REFERENCE_IMPL_UDA_EVALUATION_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"

namespace googlesql {

// Information about a UDA argument.
struct UdaArgumentInfo {
  std::string argument_name;

  // False if the argument is declared "NOT AGGREGATE".
  bool is_aggregate;

  // The expression evaluator for the argument is set when `!is_aggregate`,
  // otherwise it is nullptr.
  const ValueExpr* expr;
};

std::unique_ptr<AggregateFunctionEvaluator>
MakeUserDefinedAggregateFunctionEvaluator(
    std::unique_ptr<RelationalOp> algebrized_tree,
    std::vector<UdaArgumentInfo> argument_infos);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_UDA_EVALUATION_H_
