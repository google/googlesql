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

#include "googlesql/public/feature_label_extractor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_output_properties.h"
#include "googlesql/compliance/compliance_label.pb.h"
#include "googlesql/compliance/compliance_label_extractor.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/feature_label_dictionary.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "absl/base/no_destructor.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static const absl::flat_hash_map<SQLBuildTargetSyntax, absl::string_view>&
GetTargetSyntaxToLabelMap() {
  static const absl::NoDestructor<
      absl::flat_hash_map<SQLBuildTargetSyntax, absl::string_view>>
      kTargetSyntaxToLabelMap({
          {SQLBuildTargetSyntax::kGroupByAll, "SugarSyntax:GROUP_BY_ALL"},
          {SQLBuildTargetSyntax::kChainedFunctionCall,
           "SugarSyntax:CHAINED_FUNCTION_CALLS"},
      });
  return *kTargetSyntaxToLabelMap;
}

static void AddFeatureLabelsFromOutputProperties(
    const AnalyzerOutputProperties& properties,
    absl::btree_set<std::string>& labels) {
  const auto& resolver_labels = properties.feature_labels();
  labels.insert(resolver_labels.begin(), resolver_labels.end());
  const auto& target_syntaxes =
      InternalAnalyzerOutputProperties::GetTargetSyntaxMap(properties);
  for (auto [_, target_syntax] : target_syntaxes) {
    auto it = GetTargetSyntaxToLabelMap().find(target_syntax);
    if (it != GetTargetSyntaxToLabelMap().end()) {
      labels.insert(std::string(it->second));
    }
  }
}

int FeatureLabelDictionary::size() const {
  return static_cast<int>(all_encoded_labels_.size());
}

int FeatureLabelDictionary::GetCorrelationKey(int a, int b) const {
  if (b > a) {
    return b * kMaxCode + a;
  }
  return a * kMaxCode + b;
}

absl::Status FeatureLabelDictionary::Encode(
    const absl::btree_set<std::string>& labels, int id) {
  if (size() >= max_size_) {
    return absl::ResourceExhaustedError(absl::Substitute(
        "Max number of labels reached: $0. Recommend caching intermediate "
        "results and calling Clear().",
        max_size_));
  }
  if (all_encoded_labels_.contains(id)) {
    return absl::InvalidArgumentError(absl::StrCat("Duplicate id: ", id));
  }

  absl::flat_hash_set<int> encoded_labels;
  for (const std::string& label : labels) {
    auto it = label_to_code_map_.find(label);
    if (it == label_to_code_map_.end()) {
      // Creates a new encoding based on the current map size.
      int code = static_cast<int>(label_to_code_map_.size());
      label_to_code_map_[label] = code;
      code_to_label_map_[code] = label;
      encoded_labels.insert(code);
      code_to_frequency_map_[code] = 1;
    } else {
      encoded_labels.insert(it->second);
      code_to_frequency_map_[it->second] += 1;
    }
  }

  if (is_correlations_stored_) {
    std::vector<int> label_list = {encoded_labels.begin(),
                                   encoded_labels.end()};
    for (int i = 0; i < label_list.size(); ++i) {
      for (int j = i + 1; j < label_list.size(); ++j) {
        int key = GetCorrelationKey(label_list[i], label_list[j]);
        label_correlations_[key]++;
      }
    }
  }

  all_encoded_labels_[id] = std::move(encoded_labels);
  return absl::OkStatus();
}

absl::StatusOr<absl::btree_set<std::string>> FeatureLabelDictionary::Decode(
    int id) const {
  if (!all_encoded_labels_.contains(id)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid id: ", id));
  }
  absl::flat_hash_set<int> encoded_labels = all_encoded_labels_.at(id);
  absl::btree_set<std::string> decoded_labels;
  for (const int code : encoded_labels) {
    GOOGLESQL_RET_CHECK(code_to_label_map_.contains(code));
    decoded_labels.insert(code_to_label_map_.at(code));
  }
  return decoded_labels;
}

absl::StatusOr<absl::flat_hash_map<int, absl::btree_set<std::string>>>
FeatureLabelDictionary::DecodeAll() const {
  absl::flat_hash_map<int, absl::btree_set<std::string>> labels;
  labels.reserve(all_encoded_labels_.size());
  for (const auto& [id, encoded_labels] : all_encoded_labels_) {
    absl::btree_set<std::string> decoded_labels;
    for (const int code : encoded_labels) {
      GOOGLESQL_RET_CHECK(code_to_label_map_.contains(code));
      decoded_labels.insert(code_to_label_map_.at(code));
    }
    labels[id] = std::move(decoded_labels);
  }
  return labels;
}

absl::StatusOr<absl::btree_map<std::string, int>>
FeatureLabelDictionary::GetLabelCounts() {
  absl::btree_map<std::string, int> label_counts;
  for (const auto& [code, count] : code_to_frequency_map_) {
    label_counts[code_to_label_map_[code]] = count;
  }
  return label_counts;
}

absl::StatusOr<absl::flat_hash_map<int, int>>
FeatureLabelDictionary::GetLabelCorrelations() {
  if (!is_correlations_stored_) {
    return absl::FailedPreconditionError(
        "The current FeatureLabelDictionary is not configured to store label "
        "correlations");
  }
  return label_correlations_;
}

absl::StatusOr<std::string> FeatureLabelDictionary::DecodeCorrelationKey(
    int key) const {
  int a = key / kMaxCode;
  int b = key % kMaxCode;

  if (!code_to_label_map_.contains(a)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid code: ", a));
  }
  if (!code_to_label_map_.contains(b)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid code: ", b));
  }
  return absl::StrCat(code_to_label_map_.at(a), ",", code_to_label_map_.at(b));
}

absl::Status FeatureLabelDictionary::Serialize(
    FeatureLabelDictionaryProto* dictionary_proto) const {
  dictionary_proto->Clear();
  for (const auto& [label, code] : code_to_label_map_) {
    dictionary_proto->mutable_label_mapping()->insert({label, code});
  }
  for (const auto& [id, encoded_labels] : all_encoded_labels_) {
    FeatureLabelDictionaryProto_LabelSet label_set;
    for (int code : encoded_labels) {
      label_set.add_label(code);
    }
    dictionary_proto->mutable_encoded_labels()->insert({id, label_set});
  }
  return absl::OkStatus();
}

absl::StatusOr<FeatureLabelDictionary> FeatureLabelDictionary::Deserialize(
    const FeatureLabelDictionaryProto& dictionary_proto) {
  FeatureLabelDictionary label_dictionary;
  for (const auto& label_mapping : dictionary_proto.label_mapping()) {
    label_dictionary.code_to_label_map_[label_mapping.first] =
        label_mapping.second;
    label_dictionary.label_to_code_map_[label_mapping.second] =
        label_mapping.first;
    label_dictionary.code_to_frequency_map_[label_mapping.first] += 1;
  }
  for (const auto& label_set : dictionary_proto.encoded_labels()) {
    label_dictionary.all_encoded_labels_[label_set.first] =
        absl::flat_hash_set<int>(label_set.second.label().begin(),
                                 label_set.second.label().end());
  }
  return label_dictionary;
}

absl::Status FeatureLabelDictionary::Merge(
    const FeatureLabelDictionary& other) {
  if (this->size() + other.size() > max_size_) {
    return absl::InvalidArgumentError(
        absl::Substitute("Cannot combine dictionaries. Combined label count "
                         "would exceed maximum: $0.",
                         max_size_));
  }
  auto status_or_decoded_labels = other.DecodeAll();
  if (!status_or_decoded_labels.ok()) {
    return status_or_decoded_labels.status();
  }
  absl::flat_hash_map<int, absl::btree_set<std::string>> other_labels =
      *status_or_decoded_labels;
  for (auto& [id, labels] : other_labels) {
    if (all_encoded_labels_.find(id) == all_encoded_labels_.end()) {
      GOOGLESQL_RETURN_IF_ERROR(Encode(labels, id));
    }
  }
  return absl::OkStatus();
}

static absl::Status ExtractFeatureLabelsFromStatementImpl(
    absl::string_view sql, const AnalyzerOptions& options, Catalog* catalog,
    absl::btree_set<std::string>& labels) {
  labels.clear();

  // Call AnalyzeStatement on the query with the supplied catalog and options.
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  TypeFactory type_factory;
  GOOGLESQL_RETURN_IF_ERROR(googlesql::AnalyzeStatement(sql, options, catalog,
                                              &type_factory, &analyzer_output));

  AddFeatureLabelsFromOutputProperties(
      analyzer_output->analyzer_output_properties(), labels);

  // Extract the feature labels from the ResolvedAST.
  const ResolvedStatement* statement = nullptr;
  statement = analyzer_output->resolved_statement();
  if (statement != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ExtractComplianceLabels(statement, labels));
  }

  return absl::OkStatus();
}

absl::Status ExtractFeatureLabelsFromStatement(
    absl::string_view sql, const AnalyzerOptions& options, Catalog* catalog,
    int query_id, FeatureLabelDictionary& label_dictionary) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));

  // Feature label extraction should be performed on the ResolvedAST, before
  // any rewrites occur.
  if (!options.enabled_rewrites().empty()) {
    return absl::InvalidArgumentError(
        "The supplied AnalyzerOptions should have enabled_rewrites set to "
        "empty for the purposes of Feature label extraction.");
  }

  absl::btree_set<std::string> labels;
  const absl::Status status =
      ExtractFeatureLabelsFromStatementImpl(sql, options, catalog, labels);
  if (status.ok()) {
    return label_dictionary.Encode(labels, query_id);
  }
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

absl::Status ExtractFeatureLabelsFromResolvedAST(
    const AnalyzerOutput& output, int query_id,
    FeatureLabelDictionary& label_dictionary) {
  absl::btree_set<std::string> labels;
  AddFeatureLabelsFromOutputProperties(output.analyzer_output_properties(),
                                       labels);
  if (output.resolved_statement() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ExtractComplianceLabels(output.resolved_statement(), labels));
  }
  GOOGLESQL_RETURN_IF_ERROR(label_dictionary.Encode(labels, query_id));
  return absl::OkStatus();
}

absl::Status ExtractFeatureLabelsFromResolvedAST(
    const ResolvedStatement* stmt, int query_id,
    FeatureLabelDictionary& label_dictionary) {
  absl::btree_set<std::string> labels;
  if (stmt != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ExtractComplianceLabels(stmt, labels));
  }
  GOOGLESQL_RETURN_IF_ERROR(label_dictionary.Encode(labels, query_id));
  return absl::OkStatus();
}

absl::StatusOr<ComplianceTestCaseLabels> SerializeFeatureLabels(
    absl::btree_set<std::string>& labels) {
  ComplianceTestCaseLabels proto;
  for (const std::string& label : labels) {
    proto.add_compliance_labels(label);
  }
  return proto;
}

absl::Status ExtractComplianceLabelsNoCompression(
    const AnalyzerOutput& output, absl::btree_set<std::string>& labels) {
  AddFeatureLabelsFromOutputProperties(output.analyzer_output_properties(),
                                       labels);
  const ResolvedStatement* statement = output.resolved_statement();
  GOOGLESQL_RET_CHECK(statement != nullptr);
  return ExtractComplianceLabels(statement, labels);
}

}  // namespace googlesql
