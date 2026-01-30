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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_DISTANCE_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_DISTANCE_H_

// This code contains implementations for distance functions.

#include <optional>

#include "googlesql/public/value.h"
#include "absl/status/statusor.h"

namespace googlesql {
namespace functions {

// Implementation of:
// * COSINE_DISTANCE(ARRAY<DOUBLE>, ARRAY<DOUBLE>) -> DOUBLE
// * COSINE_DISTANCE(ARRAY<FLOAT>, ARRAY<FLOAT>) -> DOUBLE
absl::StatusOr<Value> CosineDistanceDense(Value vector1, Value vector2);

// Implementation of:
// COSINE_DISTANCE(ARRAY<STRUCT<INT64, DOUBLE>>, ARRAY<STRUCT<INT64,
// DOUBLE>>) -> DOUBLE
absl::StatusOr<Value> CosineDistanceSparseInt64Key(Value vector1,
                                                   Value vector2);

// Implementation of:
// COSINE_DISTANCE(ARRAY<STRUCT<STRING, DOUBLE>>, ARRAY<STRUCT<STRING,
// DOUBLE>>) -> DOUBLE
absl::StatusOr<Value> CosineDistanceSparseStringKey(Value vector1,
                                                    Value vector2);

// Implementation of:
// * EUCLIDEAN_DISTANCE(ARRAY<DOUBLE>, ARRAY<DOUBLE>) -> DOUBLE
// * EUCLIDEAN_DISTANCE(ARRAY<FLOAT>, ARRAY<FLOAT>) -> DOUBLE
absl::StatusOr<Value> EuclideanDistanceDense(Value vector1, Value vector2);

// Implementation of:
// EUCLIDEAN_DISTANCE(ARRAY<STRUCT<INT64, DOUBLE>>, ARRAY<STRUCT<INT64,
// DOUBLE>>) -> DOUBLE
absl::StatusOr<Value> EuclideanDistanceSparseInt64Key(Value vector1,
                                                      Value vector2);

// Implementation of:
// EUCLIDEAN_DISTANCE(ARRAY<STRUCT<STRING, DOUBLE>>, ARRAY<STRUCT<STRING,
// DOUBLE>>) -> DOUBLE
absl::StatusOr<Value> EuclideanDistanceSparseStringKey(Value vector1,
                                                       Value vector2);

// Implementation of:
// * DOT_PRODUCT(ARRAY<INT64>, ARRAY<INT64>) -> DOUBLE
// * DOT_PRODUCT(ARRAY<FLOAT>, ARRAY<FLOAT>) -> DOUBLE
// * DOT_PRODUCT(ARRAY<DOUBLE>, ARRAY<DOUBLE>) -> DOUBLE
absl::StatusOr<Value> DotProduct(Value vector1, Value vector2);

// Implementation of:
// * MANHATTAN_DISTANCE(ARRAY<INT64>, ARRAY<INT64>) -> DOUBLE
// * MANHATTAN_DISTANCE(ARRAY<FLOAT>, ARRAY<FLOAT>) -> DOUBLE
// * MANHATTAN_DISTANCE(ARRAY<DOUBLE>, ARRAY<DOUBLE>) -> DOUBLE
absl::StatusOr<Value> ManhattanDistance(Value vector1, Value vector2);

// Implementation of:
// * L1_NORM(ARRAY<INT64>) -> DOUBLE
// * L1_NORM(ARRAY<FLOAT>) -> DOUBLE
// * L1_NORM(ARRAY<DOUBLE>) -> DOUBLE
absl::StatusOr<Value> L1Norm(Value vector);

// Implementation of:
// * L2_NORM(ARRAY<INT64>) -> DOUBLE
// * L2_NORM(ARRAY<FLOAT>) -> DOUBLE
// * L2_NORM(ARRAY<DOUBLE>) -> DOUBLE
absl::StatusOr<Value> L2Norm(Value vector);

// Implementation of:
// EDIT_DISTANCE(STRING, STRING) -> INT64
absl::StatusOr<int64_t> EditDistance(absl::string_view s0, absl::string_view s1,
                                     std::optional<int64_t> max_distance);

// Implementation of:
// EDIT_DISTANCE(BYTES, BYTES) -> INT64
// The difference between this and the EditDistance above is that this function
// does not consider Unicode text and compares the string byte to byte.
absl::StatusOr<int64_t> EditDistanceBytes(absl::string_view s0,
                                          absl::string_view s1,
                                          std::optional<int64_t> max_distance);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_DISTANCE_H_
