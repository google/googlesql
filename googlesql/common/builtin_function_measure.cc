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


#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"

namespace googlesql {

void GetMeasureFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions) {
  InsertFunction(
      functions, options, "AGG", Function::AGGREGATE,
      {{ARG_TYPE_ANY_1,
        {ARG_MEASURE_TYPE_ANY_1},
        FN_AGG,
        FunctionSignatureOptions().set_rewrite_options(
            FunctionSignatureRewriteOptions().set_enabled(true).set_rewriter(
                REWRITE_MEASURE_TYPE))}},
      DefaultAggregateFunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_ENABLE_MEASURES)
          .set_supports_order_by(false)
          .set_supports_limit(false)
          .set_supports_null_handling_modifier(false)
          .set_supports_safe_error_mode(false)
          .set_supports_over_clause(false)
          .set_supports_distinct_modifier(false)
          .set_supports_window_framing(false)
          .set_window_ordering_support(FunctionOptions::ORDER_UNSUPPORTED)
          .set_supports_having_modifier(false)
          .set_supports_group_by_modifier(false)
          .set_supports_clamped_between_modifier(false)
          .set_supports_where_modifier(false)
          .set_supports_having_filter_modifier(false));
}

}  // namespace googlesql
