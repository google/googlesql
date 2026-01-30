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

#include "googlesql/public/anon_function.h"

#include <string>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

static std::string AnonFunctionSQL(absl::string_view display_name,
                                   absl::Span<const std::string> inputs) {
  const std::string upper_case_display_name =
      absl::AsciiStrToUpper(display_name);
  if (upper_case_display_name == "ANON_PERCENTILE_CONT" ||
      upper_case_display_name == "ANON_QUANTILES") {
    // TODO: Support inputs.size() == 2 once the DP Library's
    //   Quantiles supports automatic/implicit bounds.
    ABSL_DCHECK_EQ(inputs.size(), 4);
    return absl::StrCat(upper_case_display_name, "(", inputs[0], ", ",
                        inputs[1], " CLAMPED BETWEEN ", inputs[2], " AND ",
                        inputs[3], ")");
  } else {
    ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
    return absl::StrCat(upper_case_display_name, "(", inputs[0],
                        inputs.size() == 3
                            ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1],
                                           " AND ", inputs[2], ")")
                            : ")");
  }
}

static std::string SignatureTextForAnonFunction(
    absl::string_view function_name, const LanguageOptions& language_options,
    const Function& function, const FunctionSignature& signature) {
  std::string upper_case_function_name = absl::AsciiStrToUpper(function_name);
  std::string percentile_or_quantiles = "";
  bool is_function_name_percentile_or_quantiles = false;
  bool is_function_name_quantiles =
      upper_case_function_name == "ANON_QUANTILES" ||
      upper_case_function_name == "$ANON_QUANTILES_WITH_REPORT_JSON" ||
      upper_case_function_name == "$ANON_QUANTILES_WITH_REPORT_PROTO";
  if (upper_case_function_name == "ANON_PERCENTILE_CONT" ||
      is_function_name_quantiles) {
    // TODO: Support inputs.size() == 2 once the DP Library's
    //   Quantiles supports automatic/implicit bounds.
    // The expected signatures of ANON_PERCENTILE_CONT and ANON_QUANTILES are
    // that they have two input arguments along with two required clamped
    // bounds arguments (in that order).
    ABSL_DCHECK_EQ(signature.arguments().size(), 4)
        << signature.DebugString(function_name, /*verbose=*/true);
    is_function_name_percentile_or_quantiles = true;
    percentile_or_quantiles = absl::StrCat(
        ", ",
        signature.argument(1).UserFacingName(language_options.product_mode()));
  } else {
    // The expected invariant for the current list of the anonymized aggregate
    // functions other than ANON_PERCENTILE_CONT or ANON_QUANTILES is that
    // they have one input argument along with two optional clamped bounds
    // arguments (in that order).
    ABSL_DCHECK_EQ(signature.arguments().size(), 3)
        << "upper_case_function_name = " << upper_case_function_name << "\n"
        << signature.DebugString(function_name, /*verbose=*/true);
  }
  const std::string base_argument_type =
      signature.argument(0).UserFacingName(language_options.product_mode());
  const std::string lower_bound_type =
      signature.argument(is_function_name_percentile_or_quantiles ? 2 : 1)
          .UserFacingName(language_options.product_mode());
  const std::string upper_bound_type =
      signature.argument(is_function_name_percentile_or_quantiles ? 3 : 2)
          .UserFacingName(language_options.product_mode());
  // TODO: Once the DP Library's Quantiles supports
  //   automatic/implicit bounds and GoogleSQL is ready to support them,
  //   remove the is_quantiles conditionals below when CLAMPED BETWEEN is
  //   optional for ANON_QUANTILES.
  return absl::StrCat(
      absl::AsciiStrToUpper(function_name), "(", base_argument_type,
      percentile_or_quantiles, " ", (is_function_name_quantiles ? "" : "["),
      "CLAMPED BETWEEN ", lower_bound_type, " AND ", upper_bound_type,
      (is_function_name_quantiles ? "" : "]"), ")");
}

static std::string AnonFunctionBadArgumentErrorPrefix(
    absl::string_view display_name, const FunctionSignature& signature,
    int idx) {
  std::string upper_case_display_name = absl::AsciiStrToUpper(display_name);
  bool is_display_name_percentile_cont =
      upper_case_display_name == "ANON_PERCENTILE_CONT";
  if (is_display_name_percentile_cont ||
      upper_case_display_name == "ANON_QUANTILES") {
    switch (idx) {
      case 0:
        return absl::StrCat(signature.NumConcreteArguments() == 3
                                ? "The argument to "
                                : "Argument 1 to ",
                            absl::AsciiStrToUpper(display_name));
      case 1:
        return is_display_name_percentile_cont ? "Percentile" : "Quantiles";
      case 2:
        return "Lower bound on CLAMPED BETWEEN";
      case 3:
        return "Upper bound on CLAMPED BETWEEN";
      default:
        return absl::StrCat("Argument ", idx - 1, " to ",
                            absl::AsciiStrToUpper(display_name));
    }
  } else {
    switch (idx) {
      case 0:
        return absl::StrCat(signature.NumConcreteArguments() == 3
                                ? "The argument to "
                                : "Argument 1 to ",
                            absl::AsciiStrToUpper(display_name));
      case 1:
        return "Lower bound on CLAMPED BETWEEN";
      case 2:
        return "Upper bound on CLAMPED BETWEEN";
      default:
        return absl::StrCat("Argument ", idx - 1, " to ",
                            absl::AsciiStrToUpper(display_name));
    }
  }
}

static FunctionOptions AddDefaultFunctionOptions(absl::string_view name,
                                                 FunctionOptions options) {
  // Anon functions do not support multi-level aggregation or aggregate
  // filtering (i.e. no GROUP BY, WHERE, HAVING filter modifiers).
  options.set_supports_group_by_modifier(false);
  options.set_supports_where_modifier(false);
  options.set_supports_having_filter_modifier(false);
  if (!options.supports_clamped_between_modifier) {
    // Only apply the anon_* callbacks to functions that support CLAMPED BETWEEN
    return options;
  }
  if (options.get_sql_callback == nullptr) {
    options.set_get_sql_callback(
        absl::bind_front(&AnonFunctionSQL, std::string(name)));
  }
  options.set_hide_supported_signatures(options.hide_supported_signatures);
  if (options.signature_text_callback == nullptr) {
    options.set_signature_text_callback(
        absl::bind_front(&SignatureTextForAnonFunction, std::string(name)));
  }
  if (options.bad_argument_error_prefix_callback == nullptr) {
    options.set_bad_argument_error_prefix_callback(absl::bind_front(
        AnonFunctionBadArgumentErrorPrefix, std::string(name)));
  }
  return options;
}

AnonFunction::AnonFunction(
    absl::string_view name, absl::string_view group,
    const std::vector<FunctionSignature>& function_signatures,
    const FunctionOptions& function_options,
    const std::string& partial_aggregate_name)
    : Function(name, group, /*mode=*/Function::AGGREGATE, function_signatures,
               AddDefaultFunctionOptions(name, function_options)),
      partial_aggregate_name_(partial_aggregate_name) {}

const std::string& AnonFunction::GetPartialAggregateName() const {
  return partial_aggregate_name_;
}

}  // namespace googlesql
