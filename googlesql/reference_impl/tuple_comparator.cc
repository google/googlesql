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

#include "googlesql/reference_impl/tuple_comparator.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Populates 'collators' with the GoogleSqlCollators corresponding to the input
// arguments.
static absl::Status GetGoogleSqlCollators(
    absl::Span<const KeyArg* const> keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    CollatorList* collators) {
  for (int i = 0; i < keys.size(); ++i) {
    if (keys.at(i)->collation() != nullptr) {
      TupleSlot collation_slot;
      absl::Status status;
      if (!keys.at(i)->collation()->EvalSimple(params, context, &collation_slot,
                                               &status)) {
        return status;
      }
      const Value& collation_value = collation_slot.value();
      if (collation_value.is_null()) {
        return ::googlesql_base::OutOfRangeErrorBuilder()
               << "COLLATE requires non-NULL collation name";
      }
      if (context->GetLanguageOptions().LanguageFeatureEnabled(
              FEATURE_DISALLOW_LEGACY_UNICODE_COLLATION) &&
          collation_value.type_kind() == TYPE_STRING &&
          absl::StartsWith(collation_value.string_value(), "unicode:")) {
        return ::googlesql_base::OutOfRangeErrorBuilder()
               << "COLLATE has invalid collation name '"
               << collation_value.string_value() << "'";
      }

      std::unique_ptr<const GoogleSqlCollator> collator;
      switch (collation_value.type_kind()) {
        case TYPE_STRING: {
          GOOGLESQL_ASSIGN_OR_RETURN(collator,
                           MakeSqlCollatorLite(collation_value.string_value()));
          break;
        }
        case TYPE_PROTO: {
          GOOGLESQL_ASSIGN_OR_RETURN(
              collator, GetCollatorFromResolvedCollationValue(collation_value));
          break;
        }
        default:
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type kind of collation_value "
                           << Type::TypeKindToString(
                                  collation_value.type_kind(),
                                  PRODUCT_INTERNAL);
      }

      collators->emplace_back(std::move(collator));
    } else {
      collators->emplace_back(nullptr);
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<TupleComparator>> TupleComparator::Create(
    absl::Span<const KeyArg* const> keys, absl::Span<const int> slots_for_keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context) {
  return Create(keys, slots_for_keys,
                /*extra_sort_key_slots=*/{}, params, context);
}

absl::StatusOr<std::unique_ptr<TupleComparator>> TupleComparator::Create(
    absl::Span<const KeyArg* const> keys, absl::Span<const int> slots_for_keys,
    absl::Span<const int> extra_sort_key_slots,
    absl::Span<const TupleData* const> params, EvaluationContext* context) {
  std::shared_ptr<CollatorList> collators =
      std::make_shared<CollatorList>(CollatorList());
  GOOGLESQL_RETURN_IF_ERROR(
      GetGoogleSqlCollators(keys, params, context, collators.get()));
  return absl::WrapUnique(new TupleComparator(keys, slots_for_keys,
                                              extra_sort_key_slots, collators));
}

bool TupleComparator::operator()(const TupleData& t1,
                                 const TupleData& t2) const {
  return Compare(t1, t2, /*compare_floating_point_approximately=*/false,
                 /*has_approximate_comparison=*/nullptr,
                 /*collator_caused_equality=*/nullptr);
}

bool TupleComparator::Compare(const TupleData& t1, const TupleData& t2,
                              bool compare_floating_point_approximately,
                              bool* has_approximate_comparison,
                              bool* collator_caused_equality) const {
  for (int i = 0; i < keys_.size(); ++i) {
    const KeyArg* key = keys_[i];
    const GoogleSqlCollator* collator = (*collators_)[i].get();

    const int slot_idx = slots_for_keys_[i];
    const Value& v1 = t1.slot(slot_idx).value();
    const Value& v2 = t2.slot(slot_idx).value();

    if (v1.is_null() || v2.is_null()) {
      if (v1.is_null() && v2.is_null()) {  // NULLs are considered equal.
        continue;
      }
      if (key->is_descending()) {
        // NULLS LAST is the default for DESC order.
        const bool nulls_last = key->null_order() != KeyArg::kNullsFirst;
        // Non-null sorts after null except with nulls-first ordering.
        return nulls_last ? !v1.is_null() : v1.is_null();
      } else {
        // NULLS FIRST is the default for ASC order.
        const bool nulls_first = key->null_order() != KeyArg::kNullsLast;
        // Null sorts before non-null except with nulls-last ordering.
        return nulls_first ? !v2.is_null() : v2.is_null();
      }
    }

    if (collator != nullptr) {
      ABSL_DCHECK(v1.type()->IsString());
      ABSL_DCHECK(v2.type()->IsString());
      absl::Status status;
      int64_t result =
          collator->CompareUtf8(v1.string_value(), v2.string_value(), &status);
      GOOGLESQL_DCHECK_OK(status);
      if (result != 0) {  // v1 != v2
        if (key->is_descending()) {
          return result > 0;  // v1 > v2
        } else {
          return result < 0;  // v1 < v2
        }
      } else if (collator_caused_equality != nullptr &&
                 v1.string_value() != v2.string_value()) {
        // The strings are unequal, but the collator caused them to be
        // considered equal.
        *collator_caused_equality = true;
      }
    } else if (compare_floating_point_approximately &&
               key->type()->IsFloatingPoint() &&
               !key->value_expr()->IsConstant()) {
      double v1_double = v1.ToDouble();
      if (!kDefaultFloatMargin.Equal(v1_double, v2.ToDouble())) {
        if (key->is_descending()) {
          return v2.LessThan(v1);
        } else {
          return v1.LessThan(v2);
        }
      } else if (std::isfinite(v1_double) &&
                 has_approximate_comparison != nullptr) {
        *has_approximate_comparison = true;
      }
    } else if (!v1.Equals(v2)) {
      if (key->is_descending()) {
        return v2.LessThan(v1);
      } else {
        return v1.LessThan(v2);
      }
    }
  }

  // Sort by extra sort keys.
  for (int i = 0; i < extra_sort_key_slots_.size(); ++i) {
    const int slot_idx = extra_sort_key_slots_[i];
    const Value& v1 = t1.slot(slot_idx).value();
    const Value& v2 = t2.slot(slot_idx).value();

    if (v1.is_null() || v2.is_null()) {
      if (v1.is_null() && v2.is_null()) {  // NULLs are considered equal
        continue;
      }
      // NULLS FIRST is the default behavior
      return !v2.is_null();
    }
    // ASC by default.
    if (!v1.Equals(v2)) {
      return v1.LessThan(v2);
    }
  }
  // The keys are equal.
  return false;
}

// If there is a floating point key, when 2 rows have approximately equal values
// in this key, a computational error can flip the equality and change the row
// order.
//
// Example query 1: SELECT f(x) AS k, v FROM T ORDER BY 1;
// Row number     k                     v
//          1     1.0                   "a"
//          2     1.000000000000001     "b"
// The rows appear to have unique ordering, but if the difference between the
// key values is caused by computational error in f(x), the row order is not
// reliable.
//
// Example query 2: SELECT f(x) AS k1, k2 FROM T ORDER BY 1, 2;
// Row number     k1                    k2
//          1     1.0                   "a"
//          2     1.000000000000001     "b"
// This case is similar to case 1, but we cannot rely on slot_idxs_for_values
// (which is empty) to determine unique ordering.
//
// Example query 3: SELECT f(x) AS k1, k2, v FROM T ORDER BY 1, 2;
// Row number     k1      k2     v
//          1     1.0     "a"    "a"
//          2     1.0     "b"    "b"
// A computational error in f(x) can cause the first row to have a larger k1
// value than the second row and flip the output row order.
//
// To catch the above cases, IsUniquelyOrdered compares floating point keys
// approximately. If two rows have approximately equal values in all keys,
// compare the non-key column values (this handles case 1). If two rows do not
// have approximately equal values in all keys, but at least one floating point
// key was compared and has approximately equal values, IsUniquelyOrdered
// returns false (this handles case 2 and 3).
bool TupleComparator::IsUniquelyOrdered(
    absl::Span<const TupleData* const> tuples,
    absl::Span<const int> slot_idxs_for_values) const {
  for (int i = 1; i < tuples.size(); ++i) {
    const TupleData* a = tuples[i - 1];
    const TupleData* b = tuples[i];

    bool has_approximate_comparison = false;
    bool collator_caused_equality = false;
    bool unequal =
        Compare(*a, *b, /*compare_floating_point_approximately=*/true,
                &has_approximate_comparison, &collator_caused_equality);
    if (unequal) {
      if (has_approximate_comparison) {
        return false;
      }
      continue;
    }

    if (collator_caused_equality) {
      // The tuples appear equal due to the collator, but the compared strings
      // themselves are already different. We do not need to continue, as this
      // alone already tells us the result is *not* uniquely ordered.
      return false;
    }

    for (const int slot_idx : slot_idxs_for_values) {
      if (!a->slot(slot_idx).value().Equals(b->slot(slot_idx).value())) {
        // 'a' and 'b' are unequal when all their values are considered, but
        // this comparator does not yield 'a' < 'b'. Therefore, 'tuples' is
        // either not sorted or the sort order is not unique because 'a' and 'b'
        // can be reversed.
        return false;
      }
    }
  }
  return true;
}

bool TupleComparator::InvolvesUncertainArrayComparisons(
    absl::Span<const TupleData* const> tuples) const {
  if (tuples.empty()) {
    return false;
  }
  // The implementation strategy here is to find a prefix of the sort keys which
  // have no array values with uncertain orders in them. If the tuples have a
  // unique ordering using only that prefix, then the order was not determined
  // by comparing any array values with uncertain orders.
  int safe_slot_count = 0;
  for (const int slot_idx : slots_for_keys_) {
    const Type* slot_type = tuples[0]->slot(slot_idx).value().type();
    // This ABSL_DCHECK should be okay here. Its not. For some reason window scans
    // are including columns in their tuple comparison keys that aren't part
    // of the window definition order by clause.
    // TODO: Stop including struct columns in window sorting and
    //     enable this ABSL_DCHECK.
    // ABSL_DCHECK(!slot_type->IsStruct())
    //    << "Extra work needed in TupleCompartor to support ordering by "
    //    << "structs because they might contain nested arrays with uncertain "
    //    << "orders.";
    if (!slot_type->IsArray()) {
      safe_slot_count++;
      continue;
    }
    bool contains_uncertain_array_order = false;
    for (int i = 0; i < tuples.size(); ++i) {
      if (InternalValue::ContainsArrayWithUncertainOrder(
              tuples[i]->slot(slot_idx).value())) {
        contains_uncertain_array_order = true;
        break;  // Break tuple loop
      }
    }
    if (contains_uncertain_array_order) {
      break;  // Break slot loop, the current value of safe_slot_count is final.
    }
    safe_slot_count++;
  }
  // None of the key columns contains an array value with uncertain order.
  if (safe_slot_count == slots_for_keys_.size()) {
    return false;
  }
  // The first key column contains an array with uncertain order.
  if (safe_slot_count == 0) {
    return true;
  }

  TupleComparator prefix_comparator(
      absl::MakeSpan(keys_).subspan(0, safe_slot_count),
      absl::MakeSpan(slots_for_keys_).subspan(0, safe_slot_count),
      /*extra_sort_key_slots=*/{}, collators_);
  for (int i = 1; i < tuples.size(); ++i) {
    const TupleData* a = tuples[i - 1];
    const TupleData* b = tuples[i];

    if (!prefix_comparator(*a, *b)) {
      return true;
    }
  }
  return false;
}

}  // namespace googlesql
