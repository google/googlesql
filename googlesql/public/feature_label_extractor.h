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

#ifndef GOOGLESQL_PUBLIC_FEATURE_LABEL_EXTRACTOR_H_
#define GOOGLESQL_PUBLIC_FEATURE_LABEL_EXTRACTOR_H_

#include <string>

#include "googlesql/compliance/compliance_label.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/feature_label_dictionary.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/base/attributes.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// A class for compressing sets of feature labels using dictionary encoding.
// This is useful when extracting feature labels from a large number of
// queries, for instance when analyzing engine query logs.
class FeatureLabelDictionary {
 public:
  // Deserializes a FeatureLabelDictionaryProto into FeatureLabelDictionary.
  static absl::StatusOr<FeatureLabelDictionary> Deserialize(
      const FeatureLabelDictionaryProto& dictionary_proto);

  explicit FeatureLabelDictionary(bool is_correlations_stored = false,
                                  int max_size = 1000)
      : is_correlations_stored_(is_correlations_stored), max_size_(max_size) {}
  FeatureLabelDictionary& operator=(const FeatureLabelDictionary&) = delete;

  // Encodes a single set of feature labels. If the feature label set
  // contains previously unseen labels, a new encoding will be created for it,
  // then the encoded label set is appended to all_encoded_labels_.
  // The encoding will be stored in the map all_encoded_labels_, keyed by id.
  absl::Status Encode(const absl::btree_set<std::string>& labels, int id);

  // Decodes the feature label set associated with id and returns it.
  absl::StatusOr<absl::btree_set<std::string>> Decode(int id) const;

  // Decodes all encoded feature label sets stored internally in
  // all_encoded_labels_ and returns them.
  absl::StatusOr<absl::flat_hash_map<int, absl::btree_set<std::string>>>
  DecodeAll() const;

  // Serializes the FeatureLabelDictionary into a FeatureLabelDictionary
  // protocol buffer.
  absl::Status Serialize(FeatureLabelDictionaryProto* dictionary_proto) const;

  // Creates a dictionary that counts the number of occurrences of each
  // feature label across all the queries supplied to this dictionary.
  absl::StatusOr<absl::btree_map<std::string, int>> GetLabelCounts();

  // Creates a dictionary that counts the number of occurrences of pairwise
  // tuples of encoded FeatureLabels.
  absl::StatusOr<absl::flat_hash_map<int, int>> GetLabelCorrelations();

  // Returns a key representing two encoded labels. The key is calculated as
  // a*kMaxCode + b where a > b.
  int GetCorrelationKey(int a, int b) const;

  // Decodes `key` to two labels and returns a comma separated string
  // representation. If either label is not found in feature_correlations,
  // returns an error.
  absl::StatusOr<std::string> DecodeCorrelationKey(int key) const;

  // Merges this dictionary with 'other', only this dictionary is modified.
  // If there are any key conflicts on 'id', this dictionary takes precedence.
  absl::Status Merge(const FeatureLabelDictionary& other);

  // Returns the number of encoded labels stored in this dictionary.
  int size() const;

 private:
  // In order to prevent OOM, the caller should periodically clear the
  // dictionary and cache intermediate results.
  const bool is_correlations_stored_;
  const int max_size_;
  absl::flat_hash_map<std::string, int> label_to_code_map_;
  absl::flat_hash_map<int, std::string> code_to_label_map_;
  absl::flat_hash_map<int, int> code_to_frequency_map_;
  absl::flat_hash_map<int, absl::flat_hash_set<int>> all_encoded_labels_;
  // Stores the count of pairwise occurrences of labels using an integer key.
  // If a and b are the codes corresponding to the label pair, and a>b, then the
  // key is calculated as a*kMaxCode + b, where kMaxCode should be larger
  // than the maximum possible number of feature labels.
  static constexpr int kMaxCode = 10000;
  absl::flat_hash_map<int, int> label_correlations_;
};

// Extracts feature labels from a single sql statement.
// These labels are a subset of the labels generated in the compliance testing
// framework as they only include labels derived from the ResolvedAST.
// They represent metadata about the original shape of the ResolvedAST, before
// any rewrites are applied. Therefore it is required that AnalyzerOptions has
// enabled_rewrites set to empty.
// This will fail if the input query does not generate a valid ResolvedAST with
// the supplied Catalog and AnalyzerOptions.
// 'label_dictionary' will store the encoded labels, and will only be
// modified if the label extraction succeeds.
absl::Status ExtractFeatureLabelsFromStatement(
    absl::string_view sql, const AnalyzerOptions& options, Catalog* catalog,
    int query_id, FeatureLabelDictionary& label_dictionary);

ABSL_DEPRECATED("Use overload taking AnalyzerOutput instead to get all labels.")
absl::Status ExtractFeatureLabelsFromResolvedAST(
    const ResolvedStatement* stmt, int query_id,
    FeatureLabelDictionary& label_dictionary);

// Extracts feature labels from the statement in the AnalyzerOutput.
// 'label_dictionary' will store the encoded labels, and will only be
// modified if the label extraction succeeds.
absl::Status ExtractFeatureLabelsFromResolvedAST(
    const AnalyzerOutput& output, int query_id,
    FeatureLabelDictionary& label_dictionary);

// A convenience method for serializing a set of extracted feature labels,
// into a ComplianceTestCaseLabels proto. Only field compliance_labels is set.
// Note that many of the optional proto fields specific to compliance
// testing metadata are omitted e.g. test_shard/location, test_error_mode.
absl::StatusOr<ComplianceTestCaseLabels> SerializeFeatureLabels(
    absl::btree_set<std::string>& labels);

// Extracts feature labels from the `statement()` in the `AnalyzerOutput`
// without using a `FeatureLabelDictionary`. This function is useful when the
// caller wants to manage label compression and storage themselves. Appends to
// `labels`.
absl::Status ExtractComplianceLabelsNoCompression(
    const AnalyzerOutput& output, absl::btree_set<std::string>& labels);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FEATURE_LABEL_EXTRACTOR_H_
