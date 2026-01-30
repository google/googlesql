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

#ifndef GOOGLESQL_PUBLIC_SIGNATURE_MATCH_RESULT_H_
#define GOOGLESQL_PUBLIC_SIGNATURE_MATCH_RESULT_H_

#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "googlesql/base/logging.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class Type;

// Contains statistics and properties related to matching a function signature
// against a set of arguments.  Includes the number of arguments that
// matched, that did not match, and the total coercion distance for the
// matched arguments (for both literals and non-literals).  Coercion
// distance indicates how close types are together, where two same types
// have distance 0 and smaller distances indicate closer types (i.e.,
// int32 is closer to int64 than to double, and the distance between int32 and
// int64 is less than the distance between int32 and double).  Distance is
// computed as the difference between the Type::KindSpecificity() values
// of the types.
class SignatureMatchResult {
 public:
  SignatureMatchResult() : data_(std::make_unique<Data>()) {}
  ~SignatureMatchResult() = default;

  // Move only
  SignatureMatchResult(const SignatureMatchResult&) = delete;
  SignatureMatchResult& operator=(const SignatureMatchResult&) = delete;
  SignatureMatchResult(SignatureMatchResult&&) = default;
  SignatureMatchResult& operator=(SignatureMatchResult&&) = default;

  int non_matched_arguments() const { return data_->non_matched_arguments; }
  void incr_non_matched_arguments() { data_->non_matched_arguments++; }

  int non_literals_coerced() const { return data_->non_literals_coerced; }
  void incr_non_literals_coerced() { data_->non_literals_coerced++; }

  int non_literals_distance() const { return data_->non_literals_distance; }
  void incr_non_literals_distance(int distance = 1) {
    data_->non_literals_distance += distance;
  }

  int literals_coerced() const { return data_->literals_coerced; }
  void incr_literals_coerced() { data_->literals_coerced++; }

  int literals_distance() const { return data_->literals_distance; }
  void incr_literals_distance(int distance = 1) {
    data_->literals_distance += distance;
  }

  // Returns if the signature matcher is allowed to set mismatch message.
  // This is for sanity check that we only generate and set mismatch message
  // when detailed mismatch error message is enabled.
  bool allow_mismatch_message() const { return data_->allow_mismatch_message; }
  void set_allow_mismatch_message(bool allow) {
    data_->allow_mismatch_message = allow;
  }

  // The message about why the signature doesn't match the function call.
  std::string mismatch_message() const { return data_->mismatch_message; }
  void set_mismatch_message(absl::string_view message) {
    ABSL_DCHECK(data_->allow_mismatch_message) << message;
    ABSL_DCHECK(data_->mismatch_message.empty()) << data_->mismatch_message;
    data_->mismatch_message = message;
  }

  // Index of argument that causes the signature to mismatch, or -1 if unknown.
  // For now, it's only set when resolving TVFs.
  // TODO: set this for other mismatch cases for precise error
  // cursor for functions with a single signature.
  int bad_argument_index() const { return data_->bad_argument_index; }
  void set_bad_argument_index(int index) { data_->bad_argument_index = index; }

  struct ArgumentColumnPair {
    int argument_index = 0;
    int column_index = 0;
    bool operator<(const ArgumentColumnPair& rhs) const {
      return std::forward_as_tuple(this->argument_index, this->column_index) <
             std::forward_as_tuple(rhs.argument_index, rhs.column_index);
    }
  };

  // This represents a map from each TVF (argument index, column index) pair to
  // the result type to coerce each relational argument to. For more
  // information, please see the comments for tvf_arg_col_nums_to_coerce_type_.
  using TVFRelationCoercionMap =
      std::map<ArgumentColumnPair, const Type* /* coerce-to type */>;

  const TVFRelationCoercionMap& tvf_relation_coercion_map() const {
    return data_->tvf_relation_coercion_map;
  }
  void AddTVFRelationCoercionEntry(int argument_index, int column_index,
                                   const Type* coerce_type) {
    data_->tvf_relation_coercion_map.emplace(
        ArgumentColumnPair{argument_index, column_index}, coerce_type);
  }

  // Returns whether this result is a better signature match than
  // <other_result>.  Considers in order of preference:
  // 1) The number of non-literal arguments that were coerced.
  // 2) The total coercion distance of non-literal arguments.
  // 3) The total coercion distance of literal arguments.
  bool IsCloserMatchThan(const SignatureMatchResult& other_result) const;

  // Adds the individual results from <other_result> to this.
  void UpdateFromResult(const SignatureMatchResult& other_result);

  std::string DebugString() const;

 private:
  struct Data {
    int non_matched_arguments = 0;  // Number of non-matched args for function.
    int non_literals_coerced = 0;   // Number of non-literal coercions.
    int non_literals_distance = 0;  // How far non-literals were coerced.
    int literals_coerced = 0;       // Number of literal coercions.
    int literals_distance = 0;      // How far literals were coerced.

    // If the function call was invalid because of a particular argument, this
    // zero-based index is updated to indicate which argument was invalid.
    int bad_argument_index = -1;

    bool allow_mismatch_message = false;

    std::string mismatch_message;

    // If the TVF call was valid, this stores type coercions necessary for
    // relation arguments. The key is (argument index, column index) where the
    // argument index indicates which TVF argument contains the relation, and
    // the column index indicates which column within the relation (defined as
    // the offset in the column_list in that input scan). Both are zero-based.
    // The map value is the result type to coerce to.
    TVFRelationCoercionMap tvf_relation_coercion_map;
  };
  std::unique_ptr<Data> data_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SIGNATURE_MATCH_RESULT_H_
