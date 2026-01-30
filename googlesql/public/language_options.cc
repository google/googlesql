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

#include "googlesql/public/language_options.h"

#include <set>
#include <string>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/parser/keywords.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/base/macros.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "googlesql/base/status.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

LanguageOptions::LanguageFeatureSet
LanguageOptions::GetLanguageFeaturesForVersion(LanguageVersion version) {
  if (version == VERSION_CURRENT || version == LANGUAGE_VERSION_UNSPECIFIED) {
    version = VERSION_1_4;
  }

  LanguageFeatureSet features;

  const google::protobuf::EnumDescriptor* descriptor =
      google::protobuf::GetEnumDescriptor<LanguageFeature>();
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_descriptor = descriptor->value(i);
    if (!value_descriptor->options().HasExtension(language_feature_options)) {
      continue;
    }
    const LanguageFeatureOptions& options =
        value_descriptor->options().GetExtension(language_feature_options);

    if (options.in_development()) continue;
    if (!options.ideally_enabled()) continue;
    if (!options.has_language_version()) continue;

    if (version >= options.language_version()) {
      const LanguageFeature feature =
          static_cast<LanguageFeature>(value_descriptor->number());
      features.insert(feature);
    }
  }
  return features;
}

void LanguageOptions::SetLanguageVersion(LanguageVersion version) {
  enabled_language_features_ = GetLanguageFeaturesForVersion(version);
}

LanguageOptions LanguageOptions::MaximumFeatures() {
  LanguageOptions options;
  options.EnableMaximumLanguageFeatures();
  return options;
}

std::string LanguageOptions::GetEnabledLanguageFeaturesAsString() const {
  return ToString(enabled_language_features_);
}

std::string LanguageOptions::ToString(const LanguageFeatureSet& features) {
  absl::btree_set<std::string> strings;
  for (LanguageFeature feature : features) {
    strings.insert(LanguageFeature_Name(feature));
  }
  return absl::StrJoin(strings.begin(), strings.end(), ", ");
}

LanguageOptions::LanguageOptions(const LanguageOptionsProto& proto)
    : name_resolution_mode_(proto.name_resolution_mode()),
      product_mode_(proto.product_mode()),
      error_on_deprecated_syntax_(proto.error_on_deprecated_syntax()) {
  supported_statement_kinds_.clear();
  for (int i = 0; i <  proto.supported_statement_kinds_size(); ++i) {
    supported_statement_kinds_.insert(proto.supported_statement_kinds(i));
  }
  if (proto.enabled_language_features_size() > 0) {
    enabled_language_features_.clear();
    for (int i = 0; i <  proto.enabled_language_features_size(); ++i) {
      enabled_language_features_.insert(proto.enabled_language_features(i));
    }
  }
  if (proto.supported_generic_entity_types_size() > 0) {
    supported_generic_entity_types_.clear();
    for (int i = 0; i <  proto.supported_generic_entity_types_size(); ++i) {
      supported_generic_entity_types_.insert(
          proto.supported_generic_entity_types(i));
    }
  }
  if (proto.supported_generic_sub_entity_types_size() > 0) {
    supported_generic_sub_entity_types_.clear();
    for (int i = 0; i < proto.supported_generic_sub_entity_types_size(); ++i) {
      supported_generic_sub_entity_types_.insert(
          proto.supported_generic_sub_entity_types(i));
    }
  }
  for (absl::string_view keyword : proto.reserved_keywords()) {
    // Failure is possible if the proto is invalid, but a constructor cannot
    // return a status. Crash in debug builds, but silently ignore the malformed
    // keyword in production.
    auto status = EnableReservableKeyword(keyword);
    GOOGLESQL_DCHECK_OK(status);
    status.IgnoreError();
  }
}

void LanguageOptions::Serialize(LanguageOptionsProto* proto) const {
  proto->set_name_resolution_mode(name_resolution_mode_);
  proto->set_product_mode(product_mode_);
  proto->set_error_on_deprecated_syntax(error_on_deprecated_syntax_);

  for (ResolvedNodeKind kind : supported_statement_kinds_) {
    proto->add_supported_statement_kinds(kind);
  }
  for (LanguageFeature feature : enabled_language_features_) {
    proto->add_enabled_language_features(feature);
  }
  for (const std::string& entity_type : supported_generic_entity_types_) {
    proto->add_supported_generic_entity_types(entity_type);
  }
  for (const std::string& entity_type : supported_generic_sub_entity_types_) {
    proto->add_supported_generic_sub_entity_types(entity_type);
  }
  for (absl::string_view keyword : reserved_keywords_) {
    proto->add_reserved_keywords(std::string(keyword));
  }
}

void LanguageOptions::EnableMaximumLanguageFeatures(bool for_development) {
  const google::protobuf::EnumDescriptor* descriptor =
      google::protobuf::GetEnumDescriptor<LanguageFeature>();
  int previous_feature = -1;
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_descriptor = descriptor->value(i);
    const LanguageFeature feature =
        static_cast<LanguageFeature>(value_descriptor->number());
    if (feature == previous_feature) {
      // This is an alias.
      continue;
    }
    previous_feature = feature;
    const LanguageFeatureOptions& options =
        value_descriptor->options().GetExtension(language_feature_options);
    const bool enabled = options.ideally_enabled() &&
                         (for_development || !options.in_development());
    if (feature != __LanguageFeature__switch_must_have_a_default__ && enabled) {
      EnableLanguageFeature(feature);
    }
  }

  // TODO: This should be fleshed out fully when we have an approved
  // design for keyword maturity
  if (for_development) {
    EnableAllReservableKeywords();
  } else {
    // QUALIFY is the only exception as it's already launched.
    GOOGLESQL_CHECK_OK(EnableReservableKeyword("QUALIFY", /*reserved=*/true));
  }
}

const LanguageOptions::KeywordSet& LanguageOptions::GetReservableKeywords() {
  static auto* reservable_keywords =
      new KeywordSet{"QUALIFY", "MATCH_RECOGNIZE", "GRAPH_TABLE", "PER"};
  return *reservable_keywords;
}

bool LanguageOptions::IsReservedKeyword(absl::string_view keyword) const {
  if (reserved_keywords_.contains(keyword)) {
    return true;
  }
  const parser::KeywordInfo* keyword_info = parser::GetKeywordInfo(keyword);
  return keyword_info != nullptr && keyword_info->IsAlwaysReserved();
}

absl::Status LanguageOptions::EnableReservableKeyword(absl::string_view keyword,
                                                      bool reserved) {
  std::string keyword_uppercase = absl::AsciiStrToUpper(keyword);
  const auto& reservable_keywords = GetReservableKeywords();
  auto it = reservable_keywords.find(keyword_uppercase);
  if (it == reservable_keywords.end()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Invalid keyword " << keyword
           << " passed to LanguageOptions::EnableReservableKeyword()";
  }

  if (reserved) {
    reserved_keywords_.insert(*it);
  } else {
    reserved_keywords_.erase(*it);
  }
  return absl::OkStatus();
}

void LanguageOptions::EnableAllReservableKeywords(bool reserved) {
  if (reserved) {
    reserved_keywords_ = GetReservableKeywords();
  } else {
    reserved_keywords_.clear();
  }
}

LanguageVersion LanguageFeatureVersion(LanguageFeature feature) {
  static const google::protobuf::EnumDescriptor* kLanguageFeatureDescriptor =
      google::protobuf::GetEnumDescriptor<LanguageFeature>();
  const google::protobuf::EnumValueDescriptor* value_descriptor =
      kLanguageFeatureDescriptor->FindValueByNumber(feature);
  ABSL_DCHECK(value_descriptor != nullptr);
  if (value_descriptor == nullptr ||
      !value_descriptor->options().HasExtension(language_feature_options)) {
    return LANGUAGE_VERSION_UNSPECIFIED;
  }
  LanguageFeatureOptions options =
      value_descriptor->options().GetExtension(language_feature_options);
  if (!options.has_language_version()) {
    return LANGUAGE_VERSION_UNSPECIFIED;
  }
  return options.language_version();
}

}  // namespace googlesql
