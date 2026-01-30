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

#include "googlesql/reference_impl/functions/map.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/optional_ref.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

class MapFromArrayFunction : public SimpleBuiltinScalarFunction {
 public:
  MapFromArrayFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    const Value& array_arg = args[0];

    GOOGLESQL_RET_CHECK(array_arg.type()->IsArray());
    const ArrayType* array_type = array_arg.type()->AsArray();

    GOOGLESQL_RET_CHECK(array_type->element_type()->IsStruct());
    const StructType* struct_type = array_type->element_type()->AsStruct();

    GOOGLESQL_RET_CHECK_EQ(struct_type->fields().size(), 2);
    TypeFactory type_factory;
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* map_type,
                     type_factory.MakeMapType(struct_type->fields()[0].type,
                                              struct_type->fields()[1].type));

    if (array_arg.is_null()) {
      return Value::Null(map_type);
    }

    std::vector<std::pair<const Value, const Value>> map_entries;
    map_entries.reserve(array_arg.elements().size());
    for (const auto& struct_val : array_arg.elements()) {
      map_entries.push_back(
          std::make_pair(struct_val.fields()[0], struct_val.fields()[1]));
    }

    return Value::MakeMap(map_type, std::move(map_entries));
  }
};

// Defines implementation for kMapEntriesSorted and kMapEntriesUnsorted. The
// functions are nearly identical, since the internal map representation in the
// reference implementation is always sorted. The one differentiation is that
// the kMapEntriesUnsorted output array does not preserve order.
class MapEntriesFunction : public SimpleBuiltinScalarFunction {
 public:
  MapEntriesFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    const Value& map_arg = args[0];

    GOOGLESQL_RET_CHECK(map_arg.type()->IsMap());
    GOOGLESQL_RET_CHECK(output_type()->IsArray());
    const ArrayType* output_array_type = output_type()->AsArray();

    GOOGLESQL_RET_CHECK(output_array_type->element_type()->IsStruct());
    const StructType* output_array_struct_element_type =
        output_array_type->element_type()->AsStruct();
    GOOGLESQL_RET_CHECK(output_array_struct_element_type->fields().size() == 2);

    if (map_arg.is_null()) {
      return Value::Null(output_array_type);
    }

    std::vector<Value> struct_array_entries;
    struct_array_entries.reserve(map_arg.num_elements());

    // No additional sorting necessary, since reference impl MAP<> data is
    // sorted.
    for (const auto& map_val : map_arg.map_entries()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const Value struct_val,
                       Value::MakeStruct(output_array_struct_element_type,
                                         {map_val.first, map_val.second}));
      struct_array_entries.push_back(struct_val);
    }

    // Output array has a known order if:
    //  - the function is sorted, or
    //  - the array's order is unambiguous because it has 0 or 1 elements.
    InternalValue::OrderPreservationKind array_order_kind;
    switch (kind()) {
      case FunctionKind::kMapEntriesUnsorted:
        array_order_kind = map_arg.num_elements() > 1
                               ? InternalValue::kIgnoresOrder
                               : InternalValue::kPreservesOrder;
        break;
      case FunctionKind::kMapEntriesSorted:
        array_order_kind = InternalValue::kPreservesOrder;
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unknown map function kind. Expected "
                            "kMapEntriesSorted or kMapEntriesUnsorted.";
    }

    return InternalValue::MakeArray(output_array_type, array_order_kind,
                                    std::move(struct_array_entries));
  }
};

// Checks that `type` is equivalent to the key type of `map_type`.
inline absl::Status CheckTypeEquivalentToMapKeyType(const Type* type,
                                                    const MapType* map_type) {
  GOOGLESQL_RET_CHECK(type->Equivalent(map_type->key_type()))
      << "Map key type mismatch. Expected: "
      << map_type->key_type()->DebugString()
      << " but got: " << type->DebugString();
  return absl::OkStatus();
}

// Returns the value associated with `key` in `map`. If `key` is not present,
// return `result_if_missing`, or error if `result_if_missing` is null.
absl::StatusOr<Value> ValueLookupImpl(
    const Value& map, const Value& key,
    const Value* result_if_missing = nullptr) {
  GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();
  const MapType* map_type = map.type()->AsMap();

  if (map.is_null()) {
    return Value::Null(map_type->value_type());
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckTypeEquivalentToMapKeyType(key.type(), map_type));
  if (result_if_missing != nullptr) {
    GOOGLESQL_RET_CHECK(result_if_missing->type()->Equivalent(map_type->value_type()))
        << "Map value type mismatch. Expected: "
        << map_type->value_type()->DebugString()
        << " but got: " << result_if_missing->type()->DebugString();
  }

  auto it = map.map_entries().find(key);
  if (it != map.map_entries().end()) {
    return it->second;
  }

  if (result_if_missing == nullptr) {
    return MakeEvalError() << "Key not found in map: "
                           << key.Format(/*print_top_level_type=*/false);
  }
  return *result_if_missing;
}

class MapGetFunction : public SimpleBuiltinScalarFunction {
 public:
  MapGetFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK(args.size() == 2 || args.size() == 3) << args.size();

    const Value& result_if_missing =
        args.size() == 3 ? args[2] : Value::Null(output_type());
    return ValueLookupImpl(args[0], args[1], &result_if_missing);
  }
};

class MapSubscriptFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit MapSubscriptFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
    const Value result_if_missing = Value::Null(output_type());
    return ValueLookupImpl(args[0], args[1], &result_if_missing);
  }
};

class MapSubscriptWithKeyFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit MapSubscriptWithKeyFunction(FunctionKind kind,
                                       const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
    auto result = ValueLookupImpl(args[0], args[1]);
    if (kind() == FunctionKind::kMapSubscriptWithKey) {
      return result;
    }
    GOOGLESQL_RET_CHECK(kind() == FunctionKind::kMapSafeSubscriptWithKey);
    if (result.ok()) {
      return result;
    }
    if (ShouldSuppressError(result.status(),
                            ResolvedFunctionCallBase::SAFE_ERROR_MODE)) {
      return Value::Null(output_type());
    }

    return result.status();
  }
};

class MapContainsKeyFunction : public SimpleBuiltinScalarFunction {
 public:
  MapContainsKeyFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 2) << args.size();
    const Value& map = args[0];
    const Value& key = args[1];

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();
    const MapType* map_type = map.type()->AsMap();
    GOOGLESQL_RETURN_IF_ERROR(CheckTypeEquivalentToMapKeyType(key.type(), map_type));

    return Value::Bool(map.map_entries().contains(key));
  }
};

// Defines which part of the map entry to return for functions returning a
// list of map keys or values.
enum class KeyOrValueSelector {
  kUseKey,
  kUseValue,
};

// Defines which part of the map entry to order by for functions returning a
// list of map keys or values.
enum class OrderBy {
  kNone,
  kByKey,
  kByValue,
};

absl::StatusOr<Value> MapKeysOrValuesListFunctionImpl(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context, const Type* output_type, OrderBy order_by,
    KeyOrValueSelector use_key_or_value) {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  const Value& map = args[0];

  if (map.is_null()) {
    return Value::Null(output_type);
  }

  GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();
  GOOGLESQL_RET_CHECK(output_type->IsArray()) << output_type->DebugString();

  std::vector<Value> result;
  result.reserve(map.num_elements());
  if (use_key_or_value == KeyOrValueSelector::kUseKey) {
    for (const auto& [key, unused] : map.map_entries()) {
      result.push_back(key);
    }
  } else {
    for (const auto& [unused, value] : map.map_entries()) {
      result.push_back(value);
    }
  }

  if (order_by != OrderBy::kNone) {
    // Unorderable types should have been rejected during function signature
    // matching, but we still verify the precondition.
    std::string no_ordering_type;
    GOOGLESQL_RET_CHECK((order_by == OrderBy::kByKey ? map.type()->AsMap()->key_type()
                                           : map.type()->AsMap()->value_type())
                  ->SupportsOrdering(context->GetLanguageOptions(),
                                     &no_ordering_type))
        << no_ordering_type;

    if (order_by == OrderBy::kByValue) {
      // MAP is always ordered by key in the reference implementation, so an
      // additional sort operation is only necessary when ordering by value.
      absl::c_sort(result, [](auto& a, auto& b) { return a.LessThan(b); });
    }
  }

  // Output array has a known order if:
  //  - the function is sorted, or
  //  - the array's order is unambiguous because it has 0 or 1 elements.
  InternalValue::OrderPreservationKind array_order_kind =
      (map.num_elements() <= 1 || order_by != OrderBy::kNone)
          ? InternalValue::kPreservesOrder
          : InternalValue::kIgnoresOrder;
  return InternalValue::MakeArray(output_type->AsArray(), array_order_kind,
                                  std::move(result));
}

class MapKeysSortedFunction : public SimpleBuiltinScalarFunction {
 public:
  MapKeysSortedFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return MapKeysOrValuesListFunctionImpl(
        params, args, context, output_type(),
        /*order_by=*/OrderBy::kByKey,
        /*use_key_or_value=*/KeyOrValueSelector::kUseKey);
  }
};

class MapKeysUnsortedFunction : public SimpleBuiltinScalarFunction {
 public:
  MapKeysUnsortedFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return MapKeysOrValuesListFunctionImpl(
        params, args, context, output_type(),
        /*order_by=*/OrderBy::kNone,
        /*use_key_or_value=*/KeyOrValueSelector::kUseKey);
  }
};

class MapValuesSortedFunction : public SimpleBuiltinScalarFunction {
 public:
  MapValuesSortedFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return MapKeysOrValuesListFunctionImpl(
        params, args, context, output_type(),
        /*order_by=*/OrderBy::kByValue,
        /*use_key_or_value=*/KeyOrValueSelector::kUseValue);
  }
};

class MapValuesUnsortedFunction : public SimpleBuiltinScalarFunction {
 public:
  MapValuesUnsortedFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return MapKeysOrValuesListFunctionImpl(
        params, args, context, output_type(),
        /*order_by=*/OrderBy::kNone,
        /*use_key_or_value=*/KeyOrValueSelector::kUseValue);
  }
};

class MapValuesSortedByKeyFunction : public SimpleBuiltinScalarFunction {
 public:
  MapValuesSortedByKeyFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return MapKeysOrValuesListFunctionImpl(
        params, args, context, output_type(),
        /*order_by=*/OrderBy::kByKey,
        /*use_key_or_value=*/KeyOrValueSelector::kUseValue);
  }
};

class MapEmptyFunction : public SimpleBuiltinScalarFunction {
 public:
  MapEmptyFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    const Value& map = args[0];

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();
    return Value::Bool(map.num_elements() == 0);
  }
};

static inline absl::Status CheckKeyExistsInMap(const Value& map,
                                               const Value& key) {
  if (!map.map_entries().contains(key)) {
    return MakeEvalError() << "Key does not exist in map: "
                           << key.Format(/*print_top_level_type=*/false);
  }
  return absl::OkStatus();
}

enum class KeyExistenceConstraint {
  kNone,          // No constraint on key existence.
  kMustExist,     // The key must already exist in the map.
  kMustNotExist,  // The key must not already exist in the map.
};

// Given a set of keys and a new key, adds the new key to the set if it is not
// already present. Returns an error if the new key is already present. This is
// used to ensure that a key passed into a map modification function is not
// provided more than once. <next_key> is the key to add, <keys> is the set of
// keys already present to be appended with <next_key>.
static inline absl::Status AddMapKeyToInsertionSetOrErrorIfNotUnique(
    const Value& next_key, absl::flat_hash_set<Value>& keys) {
  const auto& [unused_iter, success] = keys.insert(next_key);
  if (!success) {
    return MakeEvalError() << "Key provided more than once as argument: "
                           << next_key.Format(/*print_top_level_type=*/false);
  }
  return absl::OkStatus();
}

// Implementation for functions which modify a map by adding or replacing
// key/value pairs. <key_existence_constraint> controls the precondition for
// existence within the map value for the keys provided in the arguments.
static inline absl::StatusOr<Value> PairwiseMapModificationFunctionImpl(
    const KeyExistenceConstraint key_existence_constraint,
    const Type* output_type, absl::Span<const TupleData* const> params,
    absl::Span<const Value> args, EvaluationContext* context) {
  GOOGLESQL_RET_CHECK(args.size() >= 3) << args.size();
  GOOGLESQL_RET_CHECK(args.size() % 2 == 1)
      << args.size()
      << ": after the map argument, arguments should be provided in key/value "
         "pairs.";
  GOOGLESQL_RET_CHECK(args[0].type()->IsMap()) << args[0].type()->DebugString();
  const Value& map = args[0];

  if (map.is_null()) {
    return Value::Null(output_type);
  }

  // (size - 1) because the first argument is the map, which we don't include.
  const size_t keys_to_modify_count = (args.size() - 1) / 2;

  absl::flat_hash_set<Value> keys_to_insert;
  keys_to_insert.reserve(keys_to_modify_count);
  std::vector<std::pair<Value, Value>> map_entries;
  map_entries.reserve(
      (key_existence_constraint == KeyExistenceConstraint::kMustNotExist
           ? map.num_elements() + keys_to_modify_count
           : map.num_elements()));

  // The GOOGLESQL_RET_CHECK above should catch any issue with unexpected size, but just
  // to be safe, check (size - 1) here since the loop increments by 2.
  for (int i = 1; i < args.size() - 1; i += 2) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddMapKeyToInsertionSetOrErrorIfNotUnique(args[i], keys_to_insert));
    map_entries.push_back({args[i], args[i + 1]});
  }

  if (key_existence_constraint == KeyExistenceConstraint::kMustExist) {
    for (const auto& key : keys_to_insert) {
      GOOGLESQL_RETURN_IF_ERROR(CheckKeyExistsInMap(map, key));
    }
  }

  for (const auto& [key, value] : map.map_entries()) {
    if (!keys_to_insert.contains(key)) {
      map_entries.push_back({key, value});
    } else if (key_existence_constraint ==
               KeyExistenceConstraint::kMustNotExist) {
      return MakeEvalError() << "Key already exists in map: "
                             << key.Format(/*print_top_level_type=*/false);
    }
  }

  return Value::MakeMap(output_type, std::move(map_entries));
}

class MapInsertFunction : public SimpleBuiltinScalarFunction {
 public:
  MapInsertFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return PairwiseMapModificationFunctionImpl(
        KeyExistenceConstraint::kMustNotExist, output_type(), params, args,
        context);
  }
};

class MapInsertOrReplaceFunction : public SimpleBuiltinScalarFunction {
 public:
  MapInsertOrReplaceFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return PairwiseMapModificationFunctionImpl(
        KeyExistenceConstraint::kNone, output_type(), params, args, context);
  }
};

class MapReplaceKeysAndValuesFunction : public SimpleBuiltinScalarFunction {
 public:
  MapReplaceKeysAndValuesFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return PairwiseMapModificationFunctionImpl(
        KeyExistenceConstraint::kMustExist, output_type(), params, args,
        context);
  }
};

class MapReplaceKeysAndLambdaFunction : public SimpleBuiltinScalarFunction {
 public:
  MapReplaceKeysAndLambdaFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_GE(args.size(), 2);  // Lambda is not included in args list.

    const Value& map = args[0];
    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    // The last argument in the args list is the lambda.
    const googlesql_base::optional_ref<AlgebraArg>& last_arg =
        extended_args()[extended_args().size() - 1];
    GOOGLESQL_RET_CHECK(last_arg.has_value());
    GOOGLESQL_RET_CHECK(last_arg.value().has_inline_lambda_expr())
        << last_arg.value().DebugString();
    const InlineLambdaExpr* lambda = last_arg.value().inline_lambda_expr();
    LambdaEvaluationContext lambda_eval_context(params, context);

    std::vector<std::pair<Value, Value>> map_entries;
    map_entries.reserve(args.size() - 1);
    absl::flat_hash_set<Value> keys_to_replace;
    keys_to_replace.reserve(args.size() - 1);

    for (int i = 1; i < args.size(); ++i) {
      auto& key = args[i];
      GOOGLESQL_RETURN_IF_ERROR(CheckKeyExistsInMap(map, key));
      GOOGLESQL_RETURN_IF_ERROR(
          AddMapKeyToInsertionSetOrErrorIfNotUnique(key, keys_to_replace));
      GOOGLESQL_ASSIGN_OR_RETURN(Value lambda_transformed_v,
                       lambda_eval_context.EvaluateLambda(
                           lambda, {map.map_entries().at(key)}));
      map_entries.push_back({key, lambda_transformed_v});
    }

    for (const auto& [key, value] : map.map_entries()) {
      if (!keys_to_replace.contains(key)) {
        map_entries.push_back({key, value});
      }
    }

    return Value::MakeMap(output_type(), std::move(map_entries));
  }
};

class MapCardinalityFunction : public SimpleBuiltinScalarFunction {
 public:
  MapCardinalityFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    const Value& map = args[0];
    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    return Value::Int64(map.num_elements());
  }
};

class MapDeleteFunction : public SimpleBuiltinScalarFunction {
 public:
  MapDeleteFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_GE(args.size(), 2);
    const Value& map = args[0];
    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    absl::flat_hash_set<Value> keys_to_delete;
    keys_to_delete.reserve(args.size() - 1);
    for (int i = 1; i < args.size(); ++i) {
      keys_to_delete.insert(args[i]);
    }

    std::vector<std::pair<Value, Value>> map_entries;
    map_entries.reserve(args.size() - 1);

    for (const auto& [key, value] : map.map_entries()) {
      if (!keys_to_delete.contains(key)) {
        map_entries.push_back({key, value});
      }
    }

    return Value::MakeMap(output_type(), std::move(map_entries));
  }
};

class MapFilterFunction : public SimpleBuiltinScalarFunction {
 public:
  MapFilterFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);  // Lambda is not included in args list.
    GOOGLESQL_RET_CHECK_EQ(extended_args().size(), 2);

    auto& lambda_arg = extended_args()[1];
    GOOGLESQL_RET_CHECK(lambda_arg.has_value() &&
              lambda_arg.value().inline_lambda_expr() != nullptr);

    const InlineLambdaExpr* filter_lambda =
        lambda_arg.value().inline_lambda_expr();
    GOOGLESQL_RET_CHECK_EQ(filter_lambda->num_args(), 2);
    GOOGLESQL_RET_CHECK(filter_lambda->output_type()->IsBool());

    LambdaEvaluationContext lambda_eval_context(params, context);

    const Value& map = args[0];
    GOOGLESQL_RET_CHECK(map.type()->IsMap()) << map.type()->DebugString();

    if (map.is_null()) {
      return Value::Null(output_type());
    }

    std::vector<std::pair<Value, Value>> map_entries;
    map_entries.reserve(args.size() - 1);

    for (const auto& [key, value] : map.map_entries()) {
      GOOGLESQL_ASSIGN_OR_RETURN(Value filter_result, lambda_eval_context.EvaluateLambda(
                                                filter_lambda, {key, value}));
      if (filter_result.bool_value()) {
        map_entries.push_back({key, value});
      }
    }

    return Value::MakeMap(output_type(), std::move(map_entries));
  }
};

}  // namespace

void RegisterBuiltinMapFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapFromArray},
      [](FunctionKind kind, const Type* output_type) {
        return new MapFromArrayFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapEntriesSorted, FunctionKind::kMapEntriesUnsorted},
      [](FunctionKind kind, const Type* output_type) {
        return new MapEntriesFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapGet}, [](FunctionKind kind, const Type* output_type) {
        return new MapGetFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapSubscript},
      [](FunctionKind kind, const Type* output_type) {
        return new MapSubscriptFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapSubscriptWithKey},
      [](FunctionKind kind, const Type* output_type) {
        return new MapSubscriptWithKeyFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapSafeSubscriptWithKey},
      [](FunctionKind kind, const Type* output_type) {
        return new MapSubscriptWithKeyFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapContainsKey},
      [](FunctionKind kind, const Type* output_type) {
        return new MapContainsKeyFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapKeysSorted},
      [](FunctionKind kind, const Type* output_type) {
        return new MapKeysSortedFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapKeysUnsorted},
      [](FunctionKind kind, const Type* output_type) {
        return new MapKeysUnsortedFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapValuesSorted},
      [](FunctionKind kind, const Type* output_type) {
        return new MapValuesSortedFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapValuesUnsorted},
      [](FunctionKind kind, const Type* output_type) {
        return new MapValuesUnsortedFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapValuesSortedByKey},
      [](FunctionKind kind, const Type* output_type) {
        return new MapValuesSortedByKeyFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapEmpty},
      [](FunctionKind kind, const Type* output_type) {
        return new MapEmptyFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapInsert},
      [](FunctionKind kind, const Type* output_type) {
        return new MapInsertFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapInsertOrReplace},
      [](FunctionKind kind, const Type* output_type) {
        return new MapInsertOrReplaceFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapReplaceKeyValuePairs},
      [](FunctionKind kind, const Type* output_type) {
        return new MapReplaceKeysAndValuesFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapReplaceKeysAndLambda},
      [](FunctionKind kind, const Type* output_type) {
        return new MapReplaceKeysAndLambdaFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapCardinality},
      [](FunctionKind kind, const Type* output_type) {
        return new MapCardinalityFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapDelete},
      [](FunctionKind kind, const Type* output_type) {
        return new MapDeleteFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMapFilter},
      [](FunctionKind kind, const Type* output_type) {
        return new MapFilterFunction(kind, output_type);
      });
}

}  // namespace googlesql
