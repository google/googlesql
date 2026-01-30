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
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/strings/string_view.h"

namespace googlesql {

static FunctionSignatureOptions SetRewriter(ResolvedASTRewrite rewriter) {
  return FunctionSignatureOptions().set_rewrite_options(
      FunctionSignatureRewriteOptions().set_rewriter(rewriter));
}

void GetMatchRecognizeFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions) {
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  auto optional_non_null_non_agg_constant = FunctionArgumentTypeOptions()
                                                .set_cardinality(OPTIONAL)
                                                .set_is_not_aggregate()
                                                .set_must_be_constant()
                                                .set_must_be_non_null();

  FunctionOptions nav_agg_options =
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_supports_distinct_modifier(false)
          .set_supports_null_handling_modifier(false)
          .set_supports_order_by(false)
          .set_supports_limit(false)
          .set_supports_having_modifier(false)
          .set_supports_clamped_between_modifier(false)
          .set_supports_window_framing(false)
          .set_window_ordering_support(FunctionEnums::ORDER_UNSUPPORTED)
          .set_supports_group_by_modifier(false)
          .set_supports_where_modifier(false)
          .set_supports_having_filter_modifier(false)
          .AddRequiredLanguageFeature(FEATURE_MATCH_RECOGNIZE);

  // FIRST<T>(T) -> T
  // This is a navigation function used only inside MATCH_RECOGNZIE.
  InsertFunction(functions, options, "first", Function::AGGREGATE,
                 {{ARG_TYPE_ANY_1,
                   {ARG_TYPE_ANY_1},
                   FN_FIRST_AGG,
                   SetRewriter(REWRITE_MATCH_RECOGNIZE_FUNCTION)}},
                 nav_agg_options);

  // LAST<T>(T) -> T
  // This is a navigation function used only inside MATCH_RECOGNZIE.
  InsertFunction(functions, options, "last", Function::AGGREGATE,
                 {{ARG_TYPE_ANY_1,
                   {ARG_TYPE_ANY_1},
                   FN_LAST_AGG,
                   SetRewriter(REWRITE_MATCH_RECOGNIZE_FUNCTION)}},
                 nav_agg_options);

  // NEXT() is a navigation function used only inside MATCH_RECOGNZIE.
  // Currently operates just like LEAD(), and is resolved as such.
  // Note that it's marked as SCALAR, but not allowed to be nested.
  InsertFunction(
      functions, options, "next", Function::SCALAR,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1,
         {type_factory->get_int64(), optional_non_null_non_agg_constant}},
        FN_NEXT}},
      nav_agg_options);

  // PREV() is a navigation function used only inside MATCH_RECOGNZIE.
  // Currently operates just like LAG(), and is resolved as such.
  // Note that it's marked as SCALAR, but not allowed to be nested.
  InsertFunction(
      functions, options, "prev", Function::SCALAR,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1,
         {type_factory->get_int64(), optional_non_null_non_agg_constant}},
        FN_PREV}},
      nav_agg_options);

  // MATCH_NUMBER() -> int64
  InsertFunction(functions, options, "match_number", Function::SCALAR,
                 {{type_factory->get_int64(), {}, FN_MATCH_NUMBER}},
                 nav_agg_options);

  // MATCH_ROW_NUMBER() -> int64
  InsertFunction(functions, options, "match_row_number", Function::SCALAR,
                 {{type_factory->get_int64(), {}, FN_MATCH_ROW_NUMBER}},
                 nav_agg_options);

  // CLASSIFIER() -> string
  InsertFunction(functions, options, "classifier", Function::SCALAR,
                 {{type_factory->get_string(), {}, FN_CLASSIFIER}},
                 nav_agg_options);
}

}  // namespace googlesql
