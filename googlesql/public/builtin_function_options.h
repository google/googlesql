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

#ifndef GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_OPTIONS_H_
#define GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_OPTIONS_H_

#include <utility>

#include "googlesql/proto/options.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/type.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"

namespace googlesql {

struct FunctionSignatureIdHasher {
  size_t operator()(FunctionSignatureId id) const;
};

// Controls which builtin functions and function signatures are returned by
// GoogleSQL's `GetBuiltinFunctionsAndTypes` APIs.
//
// `LanguageOptions` are applied to determine the set of candidate functions
// before inclusion/exclusion lists are applied.
struct BuiltinFunctionOptions {
  // Construct a built-in function options object without specifying language
  // options. This will use product mode `PRODUCT_INTERNAL` and will enable
  // all language features that are `ideally_enabled` and not `in_development`.
  // The set of `LanguageFeatures` used by this constructor will change over
  // time. Prefer to always supply explicit `LanguageOptions`, especially when
  // this will be used to set up a catalog for an analyzer call that uses
  // explicit `LanguageOptions`.
  ABSL_DEPRECATED("Use AllReleasedFunctions factory function.")
  BuiltinFunctionOptions();

  // Construct a built-in function options object with the specified language
  // `options`.
  explicit BuiltinFunctionOptions(const LanguageOptions& options);

  // Construct a built-in function options from a serialized form in `proto`.
  explicit BuiltinFunctionOptions(
      const GoogleSQLBuiltinFunctionOptionsProto& proto);

  // Returns an instance of `BuiltinFunctionOptions` that will returned all
  // released builtin GoogleSQL functions and types that are part of the
  // `PRODUCT_INTERNAL` language dialect. This will not return 'in_development'
  // or `PRODUCT_EXTERNAL`-only functions or types. For those cases, use the
  // constructor that takes an explicit `LanguageOptions`.
  //
  // Prefer to always supply explicit `LanguageOptions` when returned functions
  // or types will be used to set up a catalog for an analyzer call that uses
  // explicit `LanguageOptions`. Returning all functions and types is a useful
  // thing to do only in tools that need to be as generic as possible and aren't
  // attached to any particular query execution engine.
  static BuiltinFunctionOptions AllReleasedFunctions();

  // Specifies the ProductMode and set of LanguageFeatures that affect which
  // function signatures are loaded. Restrictions implied by `language_options`
  // are applied first, before the include/exclude lists below. For example, if
  // `FEATURE_ANALYTIC_FUNCTIONS` is not enabled, all analytic functions are
  // skipped even if they are included in `include_function_ids`.
  LanguageOptions language_options;

  // If not empty, and a `FunctionSignatureId` is not in the set, then
  // the corresponding `FunctionSignature` is ignored.
  absl::flat_hash_set<FunctionSignatureId, FunctionSignatureIdHasher>
      include_function_ids;

  // Ignore `FunctionSignatures` for `FunctionSignatureIds` in this set.
  absl::flat_hash_set<FunctionSignatureId, FunctionSignatureIdHasher>
      exclude_function_ids;

  // Rewrite overrides. For each function signature included in the map, the
  // boolean value indicates whether the rewrite implementation is enabled for
  // that function signature. For function signatures not specified in the map,
  // a default value is used.
  absl::flat_hash_map<FunctionSignatureId, bool> rewrite_enabled;

  // Designates the concrete types for function signatures that require a
  // caller-defined type when calling `GetBuiltinFunctionsAndTypes`.
  // Function signatures that expect a custom argument type are only returned by
  // `GetBuiltinFunctionsAndTypes` if a Type was supplied for that signature.
  // Each map entry is keyed on a pair `<FunctionSignatureId, ArgumentIndex>`
  // (ArgumentIndex is zero-based) and stores a Type that should correspond to
  // the specified argument index in that function signature.
  // The Type must outlive the function objects returned from a call
  // to `GetBuiltinFunctionsAndTypes` with these options.
  //
  // - For each function signature in `include_function_ids`,
  //   `argument_types` must contain a supplied Type for every
  //   argument index which supports a supplied Type for that signature.
  // - For each function signature in `exclude_function_ids`,
  //   `argument_types` may not supply any Types for that signature.
  absl::flat_hash_map<std::pair<FunctionSignatureId, int>, const Type*>
      argument_types;
};

// Provides backward compatibility with a redundantly repetitive name.
using GoogleSQLBuiltinFunctionOptions = BuiltinFunctionOptions;

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_OPTIONS_H_
