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

#ifndef GOOGLESQL_PUBLIC_COERCER_H_
#define GOOGLESQL_PUBLIC_COERCER_H_

#include "googlesql/public/function.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// These classes provide the logic for the implicit and explicit type coercions
// allowed by GoogleSQL.  The full specification for casting and coercion is
// at:
//
//   (broken link)
//
// The coercer defines common supertypes for individual types (including
// n-ary supertypes) and whether one type can be coerced to another type.
// Different rules exist for the coercion of literals vs. general expressions,
// and NULL values are handled separately.  Coercion analysis identifies how
// close two types are to each other based on the documented type specificities.

// Returns the cost of coercing <literal_value> to Type <to_type>.  The cost
// to coerce NULL values is 1, while the cost of coercing non-NULL values
// depends on their types as per Type::GetTypeCoercionCost().
int GetLiteralCoercionCost(const Value& literal_value, const Type* to_type);

class ExtendedCompositeCastEvaluator;

class Coercer {
 public:
  // Does not take ownership of <catalog> or <type_factory>.
  // <*language_options> should outlive this Coercer.
  // <catalog> is optional and used only if either source or destination type is
  // extended.
  Coercer(TypeFactory* type_factory, const LanguageOptions* language_options,
          Catalog* catalog = nullptr)
      : type_factory_(type_factory),
        catalog_(catalog),
        language_options_(*language_options) {}

  Coercer(const Coercer&) = delete;
  Coercer& operator=(const Coercer&) = delete;

  ~Coercer() {}

  // The methods below only look at the type of an InputArgument and whether it
  // is a parameter or literal. They do not depend on the value of a
  // literal. That logic (e.g., for detecting that a very large int64 struct
  // field cannot be coerced to an int32 field) is in
  // FunctionResolver::ConvertLiteralToType().

  // Returns the common super type of the types present in <argument_set>.
  // Returns NULL if there is no common supertype for all the argument types in
  // the set.
  //
  // InputArgumentTypeSet has a special property where we can fetch the first
  // non-NULL argument inserted into <argument_set>.
  // This first non-NULL argument is special for computing supertypes for two
  // reasons:
  //  - Struct supertypes always use field aliases from the first non-NULL
  //    argument only.
  //  - For equivalent proto types (e.g. different versions of the same proto),
  //    we consider the first non-NULL proto argument as the supertype.
  absl::Status GetCommonSuperType(const InputArgumentTypeSet& argument_set,
                                  const Type** common_supertype) const;

  ABSL_DEPRECATED("use GetCommonSuperType(argument_set, super_type)")
  const Type* GetCommonSuperType(
      const InputArgumentTypeSet& argument_set) const;

  // Returns whether <from_argument> can be coerced to <to_type>, for
  // either explicit or implicit coercion.  The <result> is updated
  // appropriately depending on whether coercion succeeds or fails.  If
  // failure, <result->non_matched_arguments> is incremented.  If success,
  // the appropriate <result> number of arguments successfully coerced is
  // incremented and the <result> distance is updated to reflect how 'close'
  // the types were (same types have distance 0, lower distance indicates
  // closer types and a better match).
  //
  // Caveat: If a Catalog is not set for this Coercer and extended type is
  // encountered this function will crash in debug mode (ABSL_DCHECK) and return
  // false in release mode. The same approach will be applied if Catalog's
  // FindConversion function returns a Status different from Ok or NotFound
  // (NotFound benignly means "conversion is not found" and thus CoercesTo just
  // returns false when it gets such Status from a Catalog).
  ABSL_DEPRECATED(
      "use CoercesTo(from_argument, to_type, is_explicit, result, "
      "extended_conversion) instead")
  bool CoercesTo(const InputArgumentType& from_argument, const Type* to_type,
                 bool is_explicit, SignatureMatchResult* result) const;

  // Works similarly to the CoercesTo function above. The difference is that it
  // returns an error Status if any internal error occurred (e.g. Catalog is not
  // set when extended type is being resolved). If checked conversion contains
  // extended type and this conversion is valid (based on a result of a call to
  // Catalog::FindConversion), the Catalog's function for this conversion
  // (Conversion::function()) is returned in extended_conversion argument.
  //
  // TODO: retire/deprecate all other *CoerceTo* methods in this
  // class.
  absl::StatusOr<bool> CoercesTo(
      const InputArgumentType& from_argument, const Type* to_type,
      bool is_explicit, SignatureMatchResult* result,
      ExtendedCompositeCastEvaluator* extended_conversion_evaluator) const;

  // Allows everything that CoercesTo allows plus the following two rules:
  // * INT64 -> INT32
  // * UINT64 -> UINT32
  // This is intended to allow statements like
  // "UPDATE Table SET int32_col = int32_col + 1" but as a side effect it will
  // also allow statements like "UPDATE Table SET int32_col = int64_expr".
  bool AssignableTo(const InputArgumentType& from_argument, const Type* to_type,
                    bool is_explicit, SignatureMatchResult* result) const;

 private:
  // Returns the common super type of the types present in <argument_set>, if
  // any. During supertype analysis, <treat_parameters_as_literals> determines
  // whether parameters are included with non-literals when identifying
  // common supertype candidates, or whether they are treated like
  // literals and are checked to see if they coerce to the candidate
  // supertypes.
  absl::StatusOr<const Type*> GetCommonSuperTypeImpl(
      const InputArgumentTypeSet& argument_set,
      bool treat_parameters_as_literals) const;

  // Returns whether <struct_argument> can be coerced to <to_type>. We
  // consider <struct_argument> types individually to see whether they can be
  // coerced to <to_type> field types implicitly/explicitly. Field names are
  // irrelevant. The <result> is updated to reflect success or failure.
  //
  // Note that <struct_argument> optionally contains a list of field
  // InputArgumentTypes.  This list is populated for literal or partially
  // literal struct values being coerced.  If this list is not present,
  // then <struct_argument> represents a non-literal and its field types
  // are considered as non-literal field types from the StructType.
  bool StructCoercesTo(const InputArgumentType& struct_argument,
                       const Type* to_type, bool is_explicit,
                       SignatureMatchResult* result) const;

  // Returns whether a parameter of <from_type> can be coerced to <to_type>,
  // for either explicit or implicit coercion.  The <result> is updated
  // appropriately to reflect success or failure as described for CoercesTo().
  bool ParameterCoercesTo(const Type* from_type, const Type* to_type,
                          bool is_explicit, SignatureMatchResult* result) const;

  // Returns whether the literal Value can be coerced to <to_type> based
  // on implicit/explicit conversion rules.  The <result> is updated
  // appropriately to reflect success or failure as described for CoercesTo().
  bool LiteralCoercesTo(const Value& literal_value, const Type* to_type,
                        bool is_explicit, SignatureMatchResult* result) const;

  // Returns the common struct super type of the <argument_set>.
  // When computing super type of struct types, we compute the super type for
  // each struct field individually and the final field aliases are determined
  // by the first non-NULL argument in <argument_set>.
  //
  // Returns NULL if there is no common supertype for all the argument types,
  // or if any of the arguments is a non-struct type.
  absl::StatusOr<const StructType*> GetCommonStructSuperType(
      const InputArgumentTypeSet& argument_set) const;

  // Returns the common super type of <arguments>. Returns NULL if there is no
  // common supertype for all the argument types, or if any of the arguments is
  // a non-array type.
  absl::StatusOr<const ArrayType*> GetCommonArraySuperType(
      const InputArgumentTypeSet& argument_set,
      bool treat_query_parameters_as_literals) const;

  // Returns the common super type of <arguments>. Returns NULL if there is no
  // common supertype for all the argument types, or if any of the arguments is
  // a non-GraphElement type.
  // Supertype of a list of GraphElementTypes has:
  // - same graph name as all the members;
  // - same element kind as all the members;
  // - a union of property types from all the members.
  absl::StatusOr<const GraphElementType*> GetCommonGraphElementSuperType(
      const InputArgumentTypeSet& argument_set) const;

  // Returns the common super type of <arguments>. Returns NULL if there is no
  // common supertype for all the argument types, or if any of the arguments is
  // a non-GraphPath type.
  absl::StatusOr<const GraphPathType*> GetCommonGraphPathSuperType(
      const InputArgumentTypeSet& argument_set) const;


  // Strips off all the field aliases present inside <struct_type> (including
  // nested structs).
  void StripFieldAliasesFromStructType(const Type** struct_type) const;

  class ContextBase;
  class Context;

  TypeFactory* type_factory_;  // Not owned.
  Catalog* catalog_;           // Not owned. Can be null.

  const LanguageOptions& language_options_;  // Not owned.
  friend class CoercerTest;
};

// Returns a list of supertypes (doesn't include itself) of the given <type>
// (built-in or extended). The returned list is sorted according to the order in
// which these types should be considered in a common supertype calculation
// algorithm. If <type> is extended, the <catalog> should point to the Catalog
// that exposes this <type>.
//
// This function accepts only leaf (not compound, like STRUCT or ARRAY) built-in
// types.
//
// REQUIRES: !type->IsExtended() || catalog != nullptr.
// REQUIRES: !type->IsStruct && !type->IsArray().
absl::StatusOr<TypeListView> GetCandidateSuperTypes(const Type* type,
                                                    Catalog* catalog = nullptr);

// Checks that there is a global preference order of supertypes and this order
// is respected for all <types> and their supertypes. Please see the comment to
// Catalog::GetSuperTypes for the details of supertype global preference order
// properties. To ensure correctness, <types> should include both built-in
// simple types and extended types.
// TODO: reference to the documentation describing the preference
// order of simple built-in types when it's available.
absl::Status CheckSuperTypePreferenceGlobalOrder(TypeListView types,
                                                 Catalog* catalog = nullptr);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_COERCER_H_
