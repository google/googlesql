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

#ifndef GOOGLESQL_PUBLIC_LANGUAGE_OPTIONS_H_
#define GOOGLESQL_PUBLIC_LANGUAGE_OPTIONS_H_

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "googlesql/proto/options.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "gtest/gtest_prod.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/case.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

// This class contains options controlling the language that should be
// accepted, and the desired semantics.  This is used for libraries where
// behavior differs by language version, flags, or other options.
class LanguageOptions {
 public:
  // Represents a set of a language features.
  using LanguageFeatureSet = absl::flat_hash_set<LanguageFeature>;

  // Represents a set of keywords.
  using KeywordSet =
      absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                          googlesql_base::StringViewCaseEqual>;

  LanguageOptions() = default;
  ~LanguageOptions() = default;
  LanguageOptions(const LanguageOptions&) = default;
  LanguageOptions(LanguageOptions&&) = default;
  LanguageOptions& operator=(const LanguageOptions&) = default;
  LanguageOptions& operator=(LanguageOptions&&) = default;

  // Deserialize LanguageOptions from proto.
  explicit LanguageOptions(const LanguageOptionsProto& proto);

  void Serialize(LanguageOptionsProto* proto) const;

  // Returns true if 'kind' is supported.
  //
  // Note: The "supported statement kind" mechanism does not support script
  // statements, as script statements do not exist in the resolved tree, so no
  // ResolvedNodeKind enumeration for them exists. Script statements are gated
  // through language features (see LanguageFeatureEnabled()).
  ABSL_MUST_USE_RESULT bool SupportsStatementKind(
      const ResolvedNodeKind kind) const {
    return supported_statement_kinds_.empty() ||
           googlesql_base::ContainsKey(supported_statement_kinds_, kind);
  }

  // The provided set of ResolvedNodeKind enums indicates the statements
  // supported by the caller. The potentially supported statements are the
  // subclasses of ResolvedStatement. An empty set indicates no restrictions. If
  // GoogleSQL encounters a statement kind that is not supported during
  // analysis, it immediately returns an error.
  //
  // By default, the set includes only RESOLVED_QUERY_STMT, so callers must
  // explicitly opt in to support other statements.
  void SetSupportedStatementKinds(
      const std::set<ResolvedNodeKind>& supported_statement_kinds) {
    supported_statement_kinds_ = supported_statement_kinds;
  }

  // Equivalent to SetSupportedStatementKinds({}).
  void SetSupportsAllStatementKinds() { supported_statement_kinds_.clear(); }

  // Adds <kind> to the set of supported statement kinds.
  void AddSupportedStatementKind(ResolvedNodeKind kind) {
    googlesql_base::InsertIfNotPresent(&supported_statement_kinds_, kind);
  }

  // Returns whether or not <feature> is enabled.
  ABSL_MUST_USE_RESULT bool LanguageFeatureEnabled(
      LanguageFeature feature) const {
    return enabled_language_features_.contains(feature);
  }

  // Set the GoogleSQL LanguageVersion.  This is equivalent to enabling the
  // set of LanguageFeatures defined as part of that version, and disabling
  // all other LanguageFeatures.  The LanguageVersion itself is not stored.
  //
  // Calling this cancels out any previous calls to EnableLanguageFeature, so
  // EnableLanguageFeature would normally be called after SetLanguageVersion.
  void SetLanguageVersion(LanguageVersion version);

  // Get the set of features enabled as of a particular version.
  static LanguageFeatureSet GetLanguageFeaturesForVersion(
      LanguageVersion version);

  // Enables support for the specified <feature>.
  void EnableLanguageFeature(LanguageFeature feature) {
    googlesql_base::InsertIfNotPresent(&enabled_language_features_, feature);
  }
  // Disables support for the specified <feature>.
  void DisableLanguageFeature(LanguageFeature feature) {
    enabled_language_features_.erase(feature);
  }

  void SetEnabledLanguageFeatures(const LanguageFeatureSet& features) {
    enabled_language_features_ = features;
  }

  template <class T>
  void SetEnabledLanguageFeatures(const T& features) {
    SetEnabledLanguageFeatures(
        LanguageFeatureSet(features.begin(), features.end()));
  }

  const LanguageFeatureSet& GetEnabledLanguageFeatures() const {
    return enabled_language_features_;
  }

  // Returns a comma-separated string listing enabled LanguageFeatures.
  std::string GetEnabledLanguageFeaturesAsString() const;
  static std::string ToString(const LanguageFeatureSet& features);

  void DisableAllLanguageFeatures() { enabled_language_features_.clear(); }

  // Enable all optional features and reservable keywords that are enabled in
  // the idealized GoogleSQL and are released to users.
  void EnableMaximumLanguageFeatures() {
    EnableMaximumLanguageFeatures(/*for_development=*/false);
  }

  // Enable all optional features and reservable keywords that are enabled in
  // the idealized GoogleSQL, including features that are still under
  // development. For internal GoogleSQL use only.
  void EnableMaximumLanguageFeaturesForDevelopment() {
    EnableMaximumLanguageFeatures(/*for_development=*/true);
  }

  // Helper that returns a LanguageOptions object that is equivalent to what
  // results from calling EnableMaximumLanguageFeatures().
  static LanguageOptions MaximumFeatures();

  void set_name_resolution_mode(NameResolutionMode mode) {
    name_resolution_mode_ = mode;
  }
  NameResolutionMode name_resolution_mode() const {
    return name_resolution_mode_;
  }

  void set_product_mode(ProductMode mode) { product_mode_ = mode; }
  ProductMode product_mode() const { return product_mode_; }

  bool SupportsProtoTypes() const {
    return product_mode_ == ProductMode::PRODUCT_INTERNAL ||
           LanguageFeatureEnabled(FEATURE_PROTO_BASE);
  }

  void set_error_on_deprecated_syntax(bool value) {
    error_on_deprecated_syntax_ = value;
  }
  ABSL_MUST_USE_RESULT bool error_on_deprecated_syntax() const {
    return error_on_deprecated_syntax_;
  }

  void SetSupportedGenericEntityTypes(
      absl::Span<const std::string> entity_types) {
    for (const auto& type : entity_types) {
      supported_generic_entity_types_.insert(type);
    }
  }

  void SetSupportedGenericSubEntityTypes(
      absl::Span<const std::string> entity_types) {
    for (const auto& type : entity_types) {
      supported_generic_sub_entity_types_.insert(type);
    }
  }

  bool GenericEntityTypeSupported(absl::string_view type) const {
    return supported_generic_entity_types_.contains(type);
  }

  bool GenericSubEntityTypeSupported(absl::string_view type) const {
    return supported_generic_sub_entity_types_.contains(type);
  }

  bool operator==(const LanguageOptions& rhs) const {
    return enabled_language_features_ == rhs.enabled_language_features_ &&
           supported_statement_kinds_ == rhs.supported_statement_kinds_ &&
           name_resolution_mode_ == rhs.name_resolution_mode_ &&
           product_mode_ == rhs.product_mode_ &&
           error_on_deprecated_syntax_ == rhs.error_on_deprecated_syntax_ &&
           supported_generic_entity_types_ ==
               rhs.supported_generic_entity_types_ &&
           supported_generic_sub_entity_types_ ==
               rhs.supported_generic_sub_entity_types_ &&
           reserved_keywords_ == rhs.reserved_keywords_;
  }
  bool operator!=(const LanguageOptions& rhs) const { return !operator==(rhs); }

  template <typename H>
  friend H AbslHashValue(H h, const LanguageOptions& value) {
    // absl::flat_hash_set does not support being absl Hash-ed.
    std::vector<LanguageFeature> enabled_language_features;
    enabled_language_features.reserve(value.enabled_language_features_.size());
    enabled_language_features.insert(enabled_language_features.end(),
                                     value.enabled_language_features_.begin(),
                                     value.enabled_language_features_.end());
    std::sort(enabled_language_features.begin(),
              enabled_language_features.end());
    return H::combine(std::move(h), enabled_language_features,
                      value.supported_statement_kinds_,
                      value.name_resolution_mode_, value.product_mode_,
                      value.error_on_deprecated_syntax_,
                      /* we just hash on the size, because this uses
                         a case insensitive comparator, which makes it awkward
                         to get into the hash value */
                      value.supported_generic_entity_types_.size(),
                      value.supported_generic_sub_entity_types_.size(),
                      value.reserved_keywords_.size());
  }

  // Returns a set of keywords which can be reserved or unreserved through
  // LanguageOptions.
  //
  // This set may grow over time as new keywords are added to GoogleSQL.
  //
  // While lookups into the returned set are case-insensitive, iterating through
  // the set always produces uppercase keywords.
  //
  // string_views in the set have unlimited lifetime.
  static const KeywordSet& GetReservableKeywords();

  // Returns true if <keyword> is reserved.
  //
  // reservable keywords are non-reserved by default, but can be made reserved
  // by calling EnableReservableKeyword().
  //
  // For non-reservable keywords, the return value simply indicates the fixed
  // behavior as to whether the keyword is reserved or not (e.g. true for
  // SELECT, false for DECIMAL).
  //
  // For non-keywords, the return value is false.
  //
  // <keyword> is case-insensitive.
  bool IsReservedKeyword(absl::string_view keyword) const;

  // Indicates whether or not <keyword> should be considered "reserved".
  // reservable keywords are nonreserved by default. When nonreserved, they
  // still exist as keywords, but may also be used in queries as identifiers,
  // without backticks.
  //
  // Returns an error if <keyword> is not reservable.
  //
  // <keyword> is case-insensitive.
  absl::Status EnableReservableKeyword(absl::string_view keyword,
                                       bool reserved = true);

  // Similar to EnableReservableKeyword(), but applies to all reservable
  // keywords.
  void EnableAllReservableKeywords(bool reserved = true);

 private:
  FRIEND_TEST(LanguageOptions, ClassAndProtoSize);

  // Enable all optional features that are enabled in the idealized GoogleSQL.
  // If 'for_development' is false, features that are still under development
  // are excluded.
  void EnableMaximumLanguageFeatures(bool for_development);

  // ======================================================================
  // NOTE: Please update options.proto and LanguageOptions.java accordingly
  // when adding new fields here.
  // ======================================================================

  // The ResolvedNodeKinds supported by the backend, e.g.,
  // googlesql::RESOLVED_QUERY_STMT. An empty set, the default, indicates no
  // restrictions.
  std::set<ResolvedNodeKind> supported_statement_kinds_ = {RESOLVED_QUERY_STMT};

  // This can be used to select strict name resolution mode.
  // In strict mode, implicit column names cannot be used unqualified.
  // This ensures that existing queries will not be broken if additional
  // elements are added to the schema in the future.
  // See (broken link) for full details.
  NameResolutionMode name_resolution_mode_ = NAME_RESOLUTION_DEFAULT;

  // This identifies whether GoogleSQL works in INTERNAL (inside Google) mode,
  // or in EXTERNAL (exposed to non-Goolers in the products such as Cloud).
  // See (broken link) for details.
  ProductMode product_mode_ = PRODUCT_INTERNAL;

  // This set of LanguageFeatures indicates which features are supported and
  // opted into by the caller.  An empty set indicates no optional features
  // are supported.  If a query includes unsupported features an error is
  // returned.
  LanguageFeatureSet enabled_language_features_;

  // If true, return an error on deprecated syntax rather than returning
  // deprecation_warnings.
  bool error_on_deprecated_syntax_ = false;

  // For generic DDLs CREATE/DROP/ALTER <entity_type>, parser will report
  // error unless generic entity type is listed here.
  absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      supported_generic_entity_types_;

  // For generic DDL nested within other ALTER statements, ALTER <entity_type>
  // ALTER ADD/DROP/ALTER <sub_entity_type> will be failed by the parser unless
  // the <sub_entity_type> is added to this set.
  absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      supported_generic_sub_entity_types_;

  // Keywords that the engine has opted into being reserved.
  // All keywords in this set belong to GetReservableKeywords(), are
  // uppercase, and have unlimited lifetime.
  KeywordSet reserved_keywords_;

  // Copyable
};

// Returns the LanguageVersion that a given LanguageFeature is associated with.
// If the feature is not associated with a version, returns
// LANGUAGE_VERSION_UNSPECIFIED.
LanguageVersion LanguageFeatureVersion(LanguageFeature feature);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_LANGUAGE_OPTIONS_H_
