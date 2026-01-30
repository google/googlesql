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

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace googlesql {

using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
using ::testing::Not;
using ::testing::StartsWith;
using ::absl_testing::StatusIs;

TEST(LanguageOptions, TestAllAcceptingStatementKind) {
  LanguageOptions options;
  options.SetSupportedStatementKinds({});
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_QUERY_STMT));
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_EXPLAIN_STMT));
}

TEST(LanguageOptions, TestSeSupportsAllStatementKinds) {
  LanguageOptions options;
  options.SetSupportsAllStatementKinds();
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_QUERY_STMT));
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_EXPLAIN_STMT));
}

TEST(LanguageOptions, TestStatementKindRestriction) {
  LanguageOptions options;
  options.SetSupportedStatementKinds({RESOLVED_QUERY_STMT});
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_QUERY_STMT));
  EXPECT_FALSE(options.SupportsStatementKind(RESOLVED_EXPLAIN_STMT));
}

TEST(LanguageOptions, TestStatementKindRestrictionWithDefault) {
  LanguageOptions options;  // Default is RESOLVED_QUERY_STMT.
  EXPECT_TRUE(options.SupportsStatementKind(RESOLVED_QUERY_STMT));
  EXPECT_FALSE(options.SupportsStatementKind(RESOLVED_EXPLAIN_STMT));
}

TEST(LanguageOptions, ProtoInProductExternal) {
  // Create external language options.
  LanguageOptions options;
  options.set_product_mode(PRODUCT_EXTERNAL);
  EXPECT_FALSE(options.SupportsProtoTypes());

  // Disable external proto.
  options.DisableLanguageFeature(FEATURE_PROTO_BASE);
  EXPECT_FALSE(options.SupportsProtoTypes());

  // Enable external proto.
  options.EnableLanguageFeature(FEATURE_PROTO_BASE);
  EXPECT_TRUE(options.SupportsProtoTypes());
}

// Get the set of possible enum values for a proto enum type.
// ENUM is the c++ enum type, and <descriptor> is its EnumDescriptor.
template <class ENUM>
static std::set<ENUM> GetEnumValues(const google::protobuf::EnumDescriptor* descriptor) {
  std::set<ENUM> values;
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const ENUM value = static_cast<ENUM>(descriptor->value(i)->number());
    values.insert(value);
  }
  return values;
}

TEST(LanguageOptions, GetLanguageFeaturesForVersion) {
  EXPECT_THAT(LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_0),
              IsEmpty());

  EXPECT_TRUE(LanguageOptions::GetLanguageFeaturesForVersion(VERSION_CURRENT)
                  .contains(FEATURE_V_1_1_ORDER_BY_COLLATE));
  EXPECT_FALSE(LanguageOptions::GetLanguageFeaturesForVersion(VERSION_CURRENT)
                   .contains(FEATURE_TABLESAMPLE));

  LanguageOptions::LanguageFeatureSet features_in_current =
      LanguageOptions::GetLanguageFeaturesForVersion(VERSION_CURRENT);

  // Now do some sanity checks on LanguageVersions vs LanguageFeatures.
  // This is done using the enum names, assuming the existing conventions are
  // followed.  We check that VERSION_x_y will include all features
  // FEATURE_V_a_b where (a,b) <= (x,y).
  for (const LanguageVersion version :
       GetEnumValues<LanguageVersion>(LanguageVersion_descriptor())) {
    if (version == LANGUAGE_VERSION_UNSPECIFIED || version == VERSION_CURRENT ||
        version == __LanguageVersion__switch_must_have_a_default__) {
      continue;
    }

    const std::string version_name = LanguageVersion_Name(version);
    EXPECT_EQ("VERSION_", version_name.substr(0, 8));
    const std::string version_suffix =
        absl::StrCat(version_name.substr(8), "_");

    LanguageOptions::LanguageFeatureSet computed_features;
    int previous_feature = -1;
    for (int i = 0; i < LanguageFeature_descriptor()->value_count(); ++i) {
      const google::protobuf::EnumValueDescriptor* value_desc =
          LanguageFeature_descriptor()->value(i);
      auto feature = static_cast<LanguageFeature>(value_desc->number());
      if (feature == previous_feature) {
        // This is an alias.
        continue;
      }
      previous_feature = feature;
      const LanguageFeatureOptions& options =
          value_desc->options().GetExtension(language_feature_options);
      if (!options.ideally_enabled()) {
        // GetLanguageFeaturesForVersion should only include ideally enabled
        // features.
        continue;
      }
      if (options.in_development()) {
        continue;
      }
      const std::string feature_name = LanguageFeature_Name(feature);
      if (options.has_language_version() &&
          options.language_version() <= version) {
        computed_features.insert(feature);
      }
    }

    EXPECT_THAT(
        computed_features,
        ContainerEq(LanguageOptions::GetLanguageFeaturesForVersion(version)))
        << "for version " << LanguageVersion_Name(version);

    // Also check that all features included in version X are also included in
    // VERSION_CURRENT.
    for (const LanguageFeature feature :
         LanguageOptions::GetLanguageFeaturesForVersion(version)) {
      EXPECT_TRUE(features_in_current.contains(feature))
          << "Features for VERSION_CURRENT does not include feature " << feature
          << " from " << version_name;
    }
  }
}

namespace {
// This test actually overlaps quite a bit with the previous test, but
// is checking subtally different constraints. In particular, we check in this
// case that the tag number ranges associated with each version are respected
// by the features and that each feature in the associated tag range is
// named and grouped appropriately. The previous test doesn't consider the tag
// numbers.
struct VersionDetails {
  LanguageVersion version_num;
  std::string feature_name_prefix;
  int tag_lower_bound;  // inclusive
  int tag_upper_bound;  // exclusive
  absl::flat_hash_set<LanguageFeature> features;
};

std::vector<VersionDetails> GetKnownVersionDetails() {
  return {
      {VERSION_1_0, "FEATURE_V_1_0_", 10000, 11000,
       LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_0)},
      {VERSION_1_1, "FEATURE_V_1_1_", 11000, 12000,
       LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_1)},
      {VERSION_1_2, "FEATURE_V_1_2_", 12000, 13000,
       LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_2)},
      {VERSION_1_3, "FEATURE_V_1_3_", 13000, 14000,
       LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_3)},
      {VERSION_1_4, "FEATURE_V_1_4_", 14000, 15000,
       LanguageOptions::GetLanguageFeaturesForVersion(VERSION_1_4)},
  };
}
}  // namespace

TEST(LanguageOptions, LanguageFeaturesVersionTagRangeIntegrity) {
  std::vector<VersionDetails> known_versions = GetKnownVersionDetails();

  int num_versions = LanguageVersion_descriptor()->value_count();
  num_versions--;  // VERSION_CURRENT
  num_versions--;  // LANGUAGE_VERSION_UNSPECIFIED
  num_versions--;  // Do not use this in a switch
  EXPECT_EQ(num_versions, known_versions.size())
      << "Did you add a version and forget to update language_options_test?";

  // Count expected features for EnableMaximumLanguageFeatures[ForDevelopment].
  int num_max_features = 0;
  int num_max_dev_features = 0;

  absl::flat_hash_set<LanguageFeature> seen_features;

  for (int i = 0; i < LanguageFeature_descriptor()->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_desc =
        LanguageFeature_descriptor()->value(i);
    int tag_number = value_desc->number();
    absl::string_view name = value_desc->name();
    LanguageFeature feature = static_cast<LanguageFeature>(tag_number);

    EXPECT_FALSE(seen_features.contains(feature))
        << "Duplicate tag number " << tag_number << " on feature " << name;
    seen_features.insert(feature);

    const LanguageFeatureOptions& options =
        value_desc->options().GetExtension(language_feature_options);
    const bool ideally_enabled = options.ideally_enabled();
    const bool in_development = options.in_development();

    if (feature != __LanguageFeature__switch_must_have_a_default__) {
      if (ideally_enabled) {
        ++num_max_dev_features;
        if (!in_development) {
          ++num_max_features;
        }
      }

      EXPECT_THAT(name, StartsWith("FEATURE_"));
      EXPECT_THAT(name, Not(StartsWith("FEATURE_V_")))
          << "Primary (non-alias) feature names should not include version "
             "numbers like FEATURE_V_1_x";
    }

    bool found_version = false;
    LanguageVersion found_version_number = VERSION_CURRENT;
    for (const VersionDetails& version : known_versions) {
      if (tag_number < version.tag_upper_bound &&
          tag_number >= version.tag_lower_bound) {
        EXPECT_TRUE(!found_version);
        found_version = true;
        found_version_number = version.version_num;
        if (ideally_enabled && !in_development) {
          EXPECT_THAT(version.features, Contains(feature));
        } else {
          EXPECT_THAT(version.features, Not(Contains(feature)));
        }
        if (options.has_language_version()) {
          EXPECT_EQ(options.language_version(), version.version_num)
              << "Bad version_num tag on enum " << name;
        }
      } else {
        if (found_version) {
          // Two features were in development when V 1.3 was "frozen".
          // TODO: Remove once features are not in development.
          bool in_development_exception =
              tag_number == 13027 || tag_number == 13038 || tag_number == 13021;
          EXPECT_TRUE(!in_development || in_development_exception) << name;
        }
        if (found_version && ideally_enabled && !in_development) {
          // This feature was in a previous version and should still be
          // included.
          EXPECT_THAT(version.features, Contains(feature));
        } else {
          EXPECT_THAT(version.features, Not(Contains(feature)));
        }
        EXPECT_THAT(name, Not(StartsWith(version.feature_name_prefix)));
      }
    }
    if (options.has_language_version()) {
      EXPECT_TRUE(found_version) << "Version not found for " << name;
      EXPECT_EQ(found_version_number, options.language_version());
    }

    // Check if we have an alias for this feature.
    // The alias must immediately follow the primary definition for that tag.
    if (i + 1 < LanguageFeature_descriptor()->value_count()) {
      const google::protobuf::EnumValueDescriptor* next_value_desc =
          LanguageFeature_descriptor()->value(i + 1);
      int next_tag_number = next_value_desc->number();
      if (next_tag_number == tag_number) {
        absl::string_view next_name = next_value_desc->name();
        EXPECT_THAT(next_name, StartsWith("FEATURE_V_"));
        EXPECT_TRUE(options.has_language_version())
            << "Only versioned features should have aliases: " << next_name
            << " wtih tag " << tag_number;
        std::string modified_name = std::string(next_name);
        EXPECT_TRUE(RE2::Replace(&modified_name, "_V_1_[0-9]", ""))
            << "Enum name " << name << " does not have a version number?";
        EXPECT_EQ(modified_name, name)
            << "Expected enum alias " << next_name
            << " to be a versioned alias of enum " << name << " for tag number "
            << tag_number;
        EXPECT_FALSE(
            next_value_desc->options().HasExtension(language_feature_options))
            << "Alias enum " << next_name << " with tag " << tag_number
            << " should not have its own options";
        EXPECT_TRUE(next_value_desc->options().deprecated())
            << "Alias enum " << next_name << " with tag " << tag_number
            << " should be marked deprecated";
        // Tag 14100 was the highest one with version number in the name when
        // we stopped putting versions in names.
        EXPECT_LE(tag_number, 14100)
            << "Newly added LanguageFeature enum " << next_name << " with tag "
            << tag_number << " should not have a versioned alias";

        // Skip over the alias enum entry since it's already checked.
        ++i;
      }
    }
  }

  {
    LanguageOptions options;
    options.EnableMaximumLanguageFeatures();
    EXPECT_EQ(options.GetEnabledLanguageFeatures().size(), num_max_features);
  }
  {
    LanguageOptions options;
    options.EnableMaximumLanguageFeaturesForDevelopment();
    EXPECT_EQ(options.GetEnabledLanguageFeatures().size(),
              num_max_dev_features);
  }
}

TEST(LanguageOptions, MaximumFeatures) {
  const LanguageOptions options = LanguageOptions::MaximumFeatures();

  // Some features that are ideally enabled and released.
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_GROUP_BY_ROLLUP));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_ORDER_BY_COLLATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_GROUP_BY_STRUCT));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_GROUP_BY_ARRAY));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_SELECT_STAR_EXCEPT_REPLACE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_ORDER_BY_IN_AGGREGATE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_CAST_DIFFERENT_ARRAY_TYPES));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_ARRAY_EQUALITY));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_LIMIT_IN_AGGREGATE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_HAVING_IN_AGGREGATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(
      FEATURE_V_1_1_NULL_HANDLING_MODIFIER_IN_ANALYTIC));
  EXPECT_TRUE(options.LanguageFeatureEnabled(
      FEATURE_V_1_1_NULL_HANDLING_MODIFIER_IN_AGGREGATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_CIVIL_TIME));

  // Some features that are released but not ideally enabled.
  EXPECT_FALSE(options.LanguageFeatureEnabled(FEATURE_DISALLOW_GROUP_BY_FLOAT));

  EXPECT_FALSE(options.LanguageFeatureEnabled(FEATURE_TEST_IDEALLY_DISABLED));

  // A feature that is ideally enabled but under development.
  EXPECT_FALSE(options.LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT));

  // A feature that is not ideally enabled and is under development.
  EXPECT_FALSE(options.LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_DISABLED_AND_IN_DEVELOPMENT));

  EXPECT_FALSE(options.LanguageFeatureEnabled(
      __LanguageFeature__switch_must_have_a_default__));
}

TEST(LanguageOptions, EnableMaximumLanguageFeaturesForDevelopment) {
  LanguageOptions options;
  options.EnableMaximumLanguageFeaturesForDevelopment();

  // Some features that are ideally enabled and released.
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_GROUP_BY_ROLLUP));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_ORDER_BY_COLLATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_GROUP_BY_STRUCT));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_GROUP_BY_ARRAY));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_SELECT_STAR_EXCEPT_REPLACE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_ORDER_BY_IN_AGGREGATE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_CAST_DIFFERENT_ARRAY_TYPES));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_ARRAY_EQUALITY));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_1_LIMIT_IN_AGGREGATE));
  EXPECT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_HAVING_IN_AGGREGATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(
      FEATURE_V_1_1_NULL_HANDLING_MODIFIER_IN_ANALYTIC));
  EXPECT_TRUE(options.LanguageFeatureEnabled(
      FEATURE_V_1_1_NULL_HANDLING_MODIFIER_IN_AGGREGATE));
  EXPECT_TRUE(options.LanguageFeatureEnabled(FEATURE_V_1_2_CIVIL_TIME));

  // Some features that are released but not ideally enabled.
  EXPECT_FALSE(options.LanguageFeatureEnabled(FEATURE_DISALLOW_GROUP_BY_FLOAT));

  EXPECT_FALSE(options.LanguageFeatureEnabled(FEATURE_TEST_IDEALLY_DISABLED));

  // A feature that is ideally enabled but under development.
  EXPECT_TRUE(options.LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT));

  // A feature that is not ideally enabled and is under development.
  EXPECT_FALSE(options.LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_DISABLED_AND_IN_DEVELOPMENT));

  EXPECT_FALSE(options.LanguageFeatureEnabled(
      __LanguageFeature__switch_must_have_a_default__));
}

TEST(LanguageOptions, FeatureSetSubsetting) {
  std::vector<LanguageOptions::LanguageFeatureSet> feature_sets;
  LanguageOptions opts;
  opts.EnableMaximumLanguageFeaturesForDevelopment();
  feature_sets.push_back(opts.GetEnabledLanguageFeatures());
  opts.DisableAllLanguageFeatures();
  opts.EnableMaximumLanguageFeatures();
  feature_sets.push_back(opts.GetEnabledLanguageFeatures());
  feature_sets.push_back(
      LanguageOptions::GetLanguageFeaturesForVersion(VERSION_CURRENT));
  std::vector<VersionDetails> known_versions = GetKnownVersionDetails();
  // Iterate backward to get descending version order.
  for (size_t i = known_versions.size() - 1; i < known_versions.size(); --i) {
    feature_sets.push_back(known_versions[i].features);
  }

  for (size_t i = 0; i < feature_sets.size(); ++i) {
    for (size_t j = i + 1; j < feature_sets.size(); ++j) {
      EXPECT_THAT(feature_sets[i], IsSupersetOf(feature_sets[j]));
    }
  }
}

TEST(LanguageOptions, Serialization) {
  LanguageOptionsProto proto;
  proto.set_product_mode(PRODUCT_EXTERNAL);
  proto.set_name_resolution_mode(NAME_RESOLUTION_STRICT);
  proto.set_error_on_deprecated_syntax(true);
  proto.add_enabled_language_features(FEATURE_V_1_1_SELECT_STAR_EXCEPT_REPLACE);
  proto.add_enabled_language_features(FEATURE_TABLESAMPLE);
  proto.add_supported_statement_kinds(RESOLVED_EXPLAIN_STMT);
  proto.add_supported_generic_entity_types("NEW_TYPE");
  proto.add_supported_generic_sub_entity_types("NEW_SUB_TYPE");
  proto.add_reserved_keywords("QUALIFY");

  LanguageOptions options(proto);
  ASSERT_EQ(PRODUCT_EXTERNAL, options.product_mode());
  ASSERT_EQ(NAME_RESOLUTION_STRICT, options.name_resolution_mode());
  ASSERT_TRUE(options.error_on_deprecated_syntax());
  ASSERT_TRUE(
      options.LanguageFeatureEnabled(FEATURE_V_1_1_SELECT_STAR_EXCEPT_REPLACE));
  ASSERT_TRUE(options.LanguageFeatureEnabled(FEATURE_TABLESAMPLE));
  ASSERT_FALSE(options.LanguageFeatureEnabled(FEATURE_V_1_1_ORDER_BY_COLLATE));
  ASSERT_TRUE(options.SupportsStatementKind(RESOLVED_EXPLAIN_STMT));
  ASSERT_FALSE(options.SupportsStatementKind(RESOLVED_QUERY_STMT));
  ASSERT_TRUE(options.GenericEntityTypeSupported("NEW_TYPE"));
  ASSERT_TRUE(options.GenericEntityTypeSupported("new_type"));
  ASSERT_FALSE(options.GenericEntityTypeSupported("unsupported"));
  ASSERT_TRUE(options.GenericSubEntityTypeSupported("NEW_SUB_TYPE"));
  ASSERT_TRUE(options.GenericSubEntityTypeSupported("new_sub_type"));
  ASSERT_FALSE(options.GenericSubEntityTypeSupported("unsupported_sub_type"));
  ASSERT_TRUE(
      options.GenericEntityTypeSupported(absl::string_view("NEW_TYPE")));
  ASSERT_TRUE(
      options.GenericEntityTypeSupported(absl::string_view("new_type")));
  ASSERT_FALSE(
      options.GenericEntityTypeSupported(absl::string_view("unsupported")));
  ASSERT_TRUE(
      options.GenericSubEntityTypeSupported(absl::string_view("NEW_SUB_TYPE")));
  ASSERT_TRUE(
      options.GenericSubEntityTypeSupported(absl::string_view("new_sub_type")));
  ASSERT_FALSE(options.GenericSubEntityTypeSupported(
      absl::string_view("unsupported_sub_type")));

  ASSERT_TRUE(options.IsReservedKeyword("QUALIFY"));
}

TEST(LanguageOptions, GetEnabledLanguageFeaturesAsString) {
  LanguageOptions options;
  EXPECT_EQ("", options.GetEnabledLanguageFeaturesAsString());
  options.EnableLanguageFeature(FEATURE_TABLESAMPLE);
  EXPECT_EQ("FEATURE_TABLESAMPLE",
            options.GetEnabledLanguageFeaturesAsString());
  options.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_EQ("FEATURE_ANALYTIC_FUNCTIONS, FEATURE_TABLESAMPLE",
            options.GetEnabledLanguageFeaturesAsString());
  options.EnableLanguageFeature(FEATURE_V_1_2_CIVIL_TIME);
  EXPECT_EQ(
      "FEATURE_ANALYTIC_FUNCTIONS, FEATURE_CIVIL_TIME, FEATURE_TABLESAMPLE",
      options.GetEnabledLanguageFeaturesAsString());
}

TEST(LanguageOptions, ReservedKeywords) {
  // GetReservableKeywords
  EXPECT_TRUE(LanguageOptions::GetReservableKeywords().contains("QUALIFY"));
  EXPECT_TRUE(LanguageOptions::GetReservableKeywords().contains("qualify"));
  EXPECT_TRUE(LanguageOptions::GetReservableKeywords().contains(
      absl::string_view("qualify")));
  EXPECT_FALSE(LanguageOptions::GetReservableKeywords().contains("SELECT"));
  EXPECT_FALSE(LanguageOptions::GetReservableKeywords().contains("DECIMAL"));
  EXPECT_FALSE(LanguageOptions::GetReservableKeywords().contains(""));

  // Initial LanguageOptions
  LanguageOptions options;
  EXPECT_FALSE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_FALSE(options.IsReservedKeyword(absl::string_view("QUALIFY")));
  EXPECT_FALSE(options.IsReservedKeyword("qualify"));
  EXPECT_FALSE(options.IsReservedKeyword(""));
  EXPECT_FALSE(options.IsReservedKeyword("DECIMAL"));
  EXPECT_TRUE(options.IsReservedKeyword("SELECT"));

  // Reserving a keyword (uppercase)
  GOOGLESQL_EXPECT_OK(options.EnableReservableKeyword("QUALIFY", true));
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_TRUE(options.IsReservedKeyword("qualify"));
  EXPECT_TRUE(options.IsReservedKeyword(absl::string_view("qualify")));
  EXPECT_TRUE(options.IsReservedKeyword("SELECT"));
  EXPECT_FALSE(options.IsReservedKeyword("DECIMAL"));

  // Reserving a keyword already reserved earlier is ok
  GOOGLESQL_EXPECT_OK(options.EnableReservableKeyword("QUALIFY", true));
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));

  // Equality test
  EXPECT_TRUE(options == options);
  EXPECT_FALSE(options == LanguageOptions());

  // Unreserving a keyword
  GOOGLESQL_EXPECT_OK(options.EnableReservableKeyword("QUALIFY", false));
  EXPECT_FALSE(options.IsReservedKeyword("QUALIFY"));

  // Unreserving a keyword already unreserved keyword is ok
  EXPECT_FALSE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_FALSE(options.IsReservedKeyword("qualify"));

  // Reserving all reservable keywords
  options.EnableAllReservableKeywords(true);
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_TRUE(options.IsReservedKeyword("SELECT"));
  EXPECT_FALSE(options.IsReservedKeyword("DECIMAL"));

  // Unreserving all reservable keywords
  options.EnableAllReservableKeywords(false);
  EXPECT_FALSE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_TRUE(options.IsReservedKeyword("SELECT"));
  EXPECT_FALSE(options.IsReservedKeyword("DECIMAL"));

  // EnableMaximumLanguageFeatures() also reserves all keywords
  options.EnableMaximumLanguageFeatures();
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));

  // Same with EnableMaximumLanguageFeaturesForDevelopment().
  options.EnableAllReservableKeywords(false);
  options.EnableMaximumLanguageFeaturesForDevelopment();
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));

  // Attempting to configure a keyword that cannot be configured
  EXPECT_THAT(options.EnableReservableKeyword("SELECT", true),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(options.EnableReservableKeyword("SELECT", false),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(options.EnableReservableKeyword("", true),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(options.EnableReservableKeyword("DECIMAL", true),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(options.EnableReservableKeyword("not a keyword", true),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LanguageOptions, ClassAndProtoSize) {
  LanguageOptions options;

  EXPECT_EQ(16, sizeof(options) - sizeof(options.supported_statement_kinds_) -
                    sizeof(options.enabled_language_features_) -
                    sizeof(options.supported_generic_entity_types_) -
                    sizeof(options.supported_generic_sub_entity_types_) -
                    sizeof(options.reserved_keywords_))
      << "The size of LanguageOptions class has changed, please also update "
      << "the proto and serialization code if you added/removed fields in it.";
  EXPECT_EQ(8, LanguageOptionsProto::descriptor()->field_count())
      << "The number of fields in LanguageOptionsProto has changed, please "
      << "also update the serialization code accordingly.";
}

TEST(LanguageOptions, EnableMaximumFeaturesDoesNotReserveGraphTable) {
  LanguageOptions options;
  options.EnableMaximumLanguageFeatures();
  EXPECT_TRUE(options.IsReservedKeyword("QUALIFY"));
  EXPECT_FALSE(options.IsReservedKeyword("GRAPH_TABLE"));
}

TEST(LanguageOptions, LanguageFeatureVersion) {
  EXPECT_EQ(LanguageFeatureVersion(FEATURE_TABLESAMPLE),
            LANGUAGE_VERSION_UNSPECIFIED);
  EXPECT_EQ(LanguageFeatureVersion(FEATURE_ORDER_BY_COLLATE), VERSION_1_1);
  EXPECT_EQ(LanguageFeatureVersion(FEATURE_CIVIL_TIME), VERSION_1_2);
  EXPECT_EQ(LanguageFeatureVersion(FEATURE_INLINE_LAMBDA_ARGUMENT),
            VERSION_1_3);
}

TEST(LanguageOptions, LanguageFeatureDefaultValueTest) {
  LanguageFeatureOptions proto;
  auto features_from_proto_default =
      LanguageOptions::GetLanguageFeaturesForVersion(proto.language_version());
  auto features_from_current =
      LanguageOptions::GetLanguageFeaturesForVersion(VERSION_CURRENT);
  EXPECT_EQ(features_from_proto_default, features_from_current);
  auto features_from_unspecified =
      LanguageOptions::GetLanguageFeaturesForVersion(
          LANGUAGE_VERSION_UNSPECIFIED);
  EXPECT_EQ(features_from_proto_default, features_from_unspecified);
}

}  // namespace googlesql
