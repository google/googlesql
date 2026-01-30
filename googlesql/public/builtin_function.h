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

#ifndef GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_H_
#define GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "googlesql/proto/options.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

namespace googlesql {

// Returns collections of Functions and named Types that are part of the
// GoogleSQL core library. `Type*` that are returned in `types` or are used in
// the FunctionSignatures are either statically allocated or allocated and owned
// by `type_factory`.
//
// The primary intended use of this API is to get `FunctionSignature` and `Type`
// objects to populate a `googlesql::Catalog` instance for Analysis. Other tools
// and utilities can also use this to get the list of built-in Functions and
// Types.
//
// `options` is used to control which function signatures and types are
//    returned. See `BuiltinFunctionOptions` for details on how to configure
//    this parameter.
// `type_factory` is used to allocate types used in function signatures and
//    returned in `types`. The lifetime of `type_factory` must exceed the
//    lifetime of `functions` and `types`.
// `functions` is used to return built-in function signatures. It must be empty.
// `types` is used to return built-in named Types. It must be empty.
// `table_valued_functions` is used to return built-in TVFs. It must be empty.
absl::Status GetBuiltinFunctionsAndTypes(
    const BuiltinFunctionOptions& options, TypeFactory& type_factory,
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>& functions,
    absl::flat_hash_map<std::string, const Type*>& types,
    absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>>&
        table_valued_functions);

ABSL_DEPRECATED(
    "Use GetBuiltinFunctionsAndTypes which accepts the table_valued_functions "
    "argument.")
absl::Status GetBuiltinFunctionsAndTypes(
    const BuiltinFunctionOptions& options, TypeFactory& type_factory,
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>& functions,
    absl::flat_hash_map<std::string, const Type*>& types);

// BuiltinFunctionsAndTypes is container class for returning builtin functions,
// types and table valued functions. This container is only valid for use with
// `GetDefaultBuiltinFunctionsAndTypes` or other contexts where the underlying
// Function, TableValuedFunctions and Type objects are statically allocated, or
// otherwise guaranteed to have a lifespan which exceeds that of this object.
class BuiltinFunctionsAndTypes {
 public:
  const absl::flat_hash_map<std::string, const Function*>& functions() const {
    return functions_;
  }
  const absl::flat_hash_map<std::string, const Type*>& types() const {
    return types_;
  }
  const absl::flat_hash_map<std::string, const TableValuedFunction*>&
  table_valued_functions() const {
    return table_valued_functions_;
  }

 private:
  BuiltinFunctionsAndTypes(
      const absl::flat_hash_map<std::string, const Function*>& functions,
      const absl::flat_hash_map<std::string, const Type*>& types,
      const absl::flat_hash_map<std::string, const TableValuedFunction*>&
          table_valued_functions)
      : functions_(functions),
        types_(types),
        table_valued_functions_(table_valued_functions) {}
  const absl::flat_hash_map<std::string, const Function*>& functions_;
  const absl::flat_hash_map<std::string, const Type*>& types_;
  const absl::flat_hash_map<std::string, const TableValuedFunction*>&
      table_valued_functions_;

  friend BuiltinFunctionsAndTypes GetDefaultBuiltinFunctionsAndTypes();
};

// Returns statically allocated collections of all released FunctionSignatures
// and Types that are part of the GoogleSQL core library, using a reasonable
// default `LanguageOptions`. This includes Functions, Types and
// TableValuedFunctions that are part of fully-implemented features for
// ProductMode `PRODUCT_INTERNAL`. In-development features are excluded.
//
// Returned `Function*`, `Type*` and `TableValuedFunction*` are statically
// allocated and have process lifetime.
//
// This API is convenient for tools and utilities that want the full set of
// possible builtin FunctionSignatures and want to reference a statically
// allocated collection for efficiency. The likely calling pattern is:
// ```
//   auto kAllBuiltins =
//        googlesql::GetDefaultBuiltinFunctionsAndTypes();
//   auto kAllBuiltinFunctions = kAllBuiltins.functions();
//   auto kAllBuiltinTypes = kAllBuiltins.types();
//   auto kAllBuiltinTableValuedFunctions =
//        kAllBuiltins.table_valued_functions();
//  ```
// Generally, this is not an appropriate API for populating a Catalog instance
// for query analysis. For that, use `GetBuiltinFunctionsAndTypes` with an
// `options` object initialized to complement the `AnalyzerOptions` used
// for query analysis.
BuiltinFunctionsAndTypes GetDefaultBuiltinFunctionsAndTypes();

std::string FunctionSignatureIdToName(FunctionSignatureId id);

// If the function allows argument coercion, then checks the function
// signatures to see if they are defined for floating point and
// only one of signed/unsigned integer arguments (but not both integer
// type arguments), and returns true if so.  Otherwise, returns false.
//
// This check is only used in unit tests as a sanity check to ensure that
// the set of signatures for a function collectively make sense; it does
// not get used at analysis time during function resolution.  It is
// intended to verify that function signatures do not allow implicit
// coercion of unsigned integer to a floating point type while signed
// integers have their own signatures (this combination has been incorrectly
// used in the past where such coercions were inadvertently allowed when
// they should not have been).  In the usual case, if signed integers
// have their own signatures then unsigned integers should have their own
// signatures as well (or coercion of unsigned integer arguments should be
// explicitly disabled).
//
// Note that this function does not take into consideration any argument
// constraints that might be present.
bool FunctionMayHaveUnintendedArgumentCoercion(const Function* function);

// DEPRECATED: Use GetBuiltinFunctionsAndTypes
//
ABSL_DEPRECATED("Inline me!")
void GetGoogleSQLFunctions(  // NOLINT
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    std::map<std::string, std::unique_ptr<Function>>* functions);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_BUILTIN_FUNCTION_H_
