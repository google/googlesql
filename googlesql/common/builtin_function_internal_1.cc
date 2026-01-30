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

#include <ctype.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/functions/string_format.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/no_destructor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class AnalyzerOptions;

using ::googlesql::functions::DateTimestampPartToSQL;

bool ArgumentsAreComparable(absl::Span<const InputArgumentType> arguments,
                            const LanguageOptions& language_options,
                            int* bad_argument_idx) {
  *bad_argument_idx = -1;
  for (int idx = 0; idx < arguments.size(); ++idx) {
    if (arguments[idx].type() == nullptr) {
      *bad_argument_idx = idx;
      return false;
    }
    if (!arguments[idx].type()->SupportsOrdering(
            language_options, /*type_description=*/nullptr)) {
      *bad_argument_idx = idx;
      return false;
    }
  }
  return true;
}

// Checks whether all arguments are/are not arrays depending on the value of the
// 'is_array' flag.
bool ArgumentsArrayType(const std::vector<InputArgumentType>& arguments,
                        bool is_array, int* bad_argument_idx) {
  *bad_argument_idx = -1;
  for (int idx = 0; idx < arguments.size(); ++idx) {
    if ((arguments[idx].type() != nullptr &&
         arguments[idx].type()->IsArray()) != is_array) {
      *bad_argument_idx = idx;
      return false;
    }
  }
  return true;
}

// The argument <display_name> in the below ...FunctionSQL represents the
// operator and should include space if inputs and operator needs to be space
// separated for pretty printing.
// TODO: Consider removing this callback, since Function now knows
// whether it is operator, and has correct sql_name to print.
std::string InfixFunctionSQL(absl::string_view display_name,
                             absl::Span<const std::string> inputs) {
  std::string sql;
  for (const std::string& text : inputs) {
    if (!sql.empty()) {
      absl::StrAppend(&sql, " ", absl::AsciiStrToUpper(display_name), " ");
    }
    // Check if input contains only alphanumeric characters or '_', in which
    // case enclosing ( )'s are not needed.
    if (std::find_if(text.begin(), text.end(), [](char c) {
          return !(isalnum(c) || (c == '_'));
        }) == text.end()) {
      absl::StrAppend(&sql, text);
    } else {
      absl::StrAppend(&sql, "(", text, ")");
    }
  }
  return sql;
}
std::string PreUnaryFunctionSQL(absl::string_view display_name,
                                absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 1);
  return absl::StrCat(display_name, "(", inputs[0], ")");
}
std::string PostUnaryFunctionSQL(absl::string_view display_name,
                                 absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 1);
  return absl::StrCat("(", inputs[0], ")", display_name);
}
std::string DateAddOrSubFunctionSQL(absl::string_view display_name,
                                    absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 3);
  return absl::StrCat(display_name, "(", inputs[0], ", INTERVAL ", inputs[1],
                      " ", inputs[2], ")");
}

std::string CountStarFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_LE(inputs.size(), 1);

  if (inputs.size() == 1) {
    return absl::StrCat("COUNT(* ", inputs[0], ")");
  } else {
    return "COUNT(*)";
  }
}

std::string AnonCountStarFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.empty() || inputs.size() == 2);
  return absl::StrCat(
      "ANON_COUNT(*",
      inputs.size() == 2
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[0], " AND ", inputs[1])
          : "",
      ")");
}
std::string SignatureTextForAnonCountStarFunction() {
  return "ANON_COUNT(* [CLAMPED BETWEEN INT64 AND INT64])";
}

std::string SignatureTextForAnonCountStarFunction(
    const LanguageOptions& language_options, const Function& function,
    const FunctionSignature& signature) {
  return SignatureTextForAnonCountStarFunction();
}

std::string SignatureTextForAnonCountStarWithReportFunction(
    absl::string_view report_format) {
  return absl::StrCat(
      "ANON_COUNT(* [CLAMPED BETWEEN INT64 AND INT64] WITH "
      "REPORT(FORMAT=",
      report_format, "))");
}

std::string SignatureTextForAnonCountStarWithReportFunction(
    absl::string_view report_format, const LanguageOptions& language_options,
    const Function& function, const FunctionSignature& signature) {
  return SignatureTextForAnonCountStarWithReportFunction(report_format);
}

std::string SignatureTextForAnonQuantilesWithReportFunction(
    absl::string_view report_format) {
  return absl::StrCat(
      "ANON_QUANTILES(DOUBLE, INT64 CLAMPED BETWEEN DOUBLE AND DOUBLE WITH "
      "REPORT(FORMAT=",
      report_format, "))");
}

std::string SignatureTextForAnonQuantilesWithReportFunction(
    absl::string_view report_format, const LanguageOptions& language_options,
    const Function& function, const FunctionSignature& signature) {
  return SignatureTextForAnonQuantilesWithReportFunction(report_format);
}

std::string AnonSumWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_SUM(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=JSON))");
}

std::string AnonSumWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_SUM(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=PROTO))");
}

std::string AnonAvgWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_AVG(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=JSON))");
}

std::string AnonAvgWithReportProtoFunctionSQL(
    const std::vector<std::string>& inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_AVG(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=PROTO))");
}

std::string AnonCountWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_COUNT(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=JSON))");
}

std::string AnonCountWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 1 || inputs.size() == 3);
  return absl::StrCat(
      "ANON_COUNT(", inputs[0],
      inputs.size() == 3
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[1], " AND ", inputs[2])
          : "",
      " WITH REPORT(FORMAT=PROTO))");
}

std::string AnonCountStarWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.empty() || inputs.size() == 2);
  return absl::StrCat(
      "ANON_COUNT(*",
      inputs.size() == 2
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[0], " AND ", inputs[1])
          : "",
      " WITH REPORT(FORMAT=JSON))");
}

std::string AnonCountStarWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.empty() || inputs.size() == 2);
  return absl::StrCat(
      "ANON_COUNT(*",
      inputs.size() == 2
          ? absl::StrCat(" CLAMPED BETWEEN ", inputs[0], " AND ", inputs[1])
          : "",
      " WITH REPORT(FORMAT=PROTO))");
}

std::string AnonQuantilesWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs) {
  // TODO: Once CLAMPED BETWEEN arguments are no longer required,
  // this function should also support only 2 args.
  ABSL_DCHECK_EQ(inputs.size(), 4);
  return absl::StrCat("ANON_QUANTILES(", inputs[0], ", ", inputs[1],
                      " CLAMPED BETWEEN ", inputs[2], " AND ", inputs[3],
                      " WITH REPORT(FORMAT=JSON))");
}

std::string AnonQuantilesWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs) {
  // TODO: Once CLAMPED BETWEEN arguments are no longer required,
  // this function should also support only 2 args.
  ABSL_DCHECK_EQ(inputs.size(), 4);
  return absl::StrCat("ANON_QUANTILES(", inputs[0], ", ", inputs[1],
                      " CLAMPED BETWEEN ", inputs[2], " AND ", inputs[3],
                      " WITH REPORT(FORMAT=PROTO))");
}

std::string CaseWithValueFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GE(inputs.size(), 2);
  ABSL_DCHECK_EQ((inputs.size() - 2) % 2, 0);

  std::string case_with_value;
  absl::StrAppend(&case_with_value, "CASE (", inputs[0], ")");
  for (int i = 1; i < inputs.size() - 1; i += 2) {
    absl::StrAppend(&case_with_value, " WHEN (", inputs[i], ") THEN (",
                    inputs[i + 1], ")");
  }
  absl::StrAppend(&case_with_value, " ELSE (", inputs[inputs.size() - 1], ")");
  absl::StrAppend(&case_with_value, " END");

  return case_with_value;
}
std::string CaseNoValueFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GE(inputs.size(), 1);
  ABSL_DCHECK_EQ((inputs.size() - 1) % 2, 0);

  std::string case_no_value;
  absl::StrAppend(&case_no_value, "CASE");
  for (int i = 0; i < inputs.size() - 1; i += 2) {
    absl::StrAppend(&case_no_value, " WHEN (", inputs[i], ") THEN (",
                    inputs[i + 1], ")");
  }
  absl::StrAppend(&case_no_value, " ELSE (", inputs[inputs.size() - 1], ")");
  absl::StrAppend(&case_no_value, " END");

  return case_no_value;
}
std::string ParenthesizedArrayFunctionSQL(const std::string& input) {
  if (std::find_if(input.begin(), input.end(),
                   [](char c) { return c == '|'; }) == input.end()) {
    return input;
  } else {
    return "(" + input + ")";
  }
}
std::string ArrayAtFunctionSQL(absl::string_view inner_function_name,
                               absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(ParenthesizedArrayFunctionSQL(inputs[0]), "[",
                      inner_function_name, "(", inputs[1], ")]");
}
std::string ArrayAtOffsetFunctionSQL(const std::vector<std::string>& inputs) {
  return ArrayAtFunctionSQL("OFFSET", inputs);
}
std::string ArrayAtOrdinalFunctionSQL(const std::vector<std::string>& inputs) {
  return ArrayAtFunctionSQL("ORDINAL", inputs);
}
std::string SafeArrayAtOffsetFunctionSQL(absl::Span<const std::string> inputs) {
  return ArrayAtFunctionSQL("SAFE_OFFSET", inputs);
}
std::string SubscriptFunctionSQL(const std::vector<std::string>& inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(inputs[0], "[", inputs[1], "]");
}
std::string SubscriptWithKeyFunctionSQL(bool safe,
                                        absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(inputs[0], "[", safe ? "SAFE_" : "", "KEY(", inputs[1],
                      ")]");
}
std::string SubscriptWithOffsetFunctionSQL(
    bool safe, absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(inputs[0], "[", safe ? "SAFE_" : "", "OFFSET(", inputs[1],
                      ")]");
}
std::string SubscriptWithOrdinalFunctionSQL(
    bool safe, absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(inputs[0], "[", safe ? "SAFE_" : "", "ORDINAL(",
                      inputs[1], ")]");
}
std::string SafeArrayAtOrdinalFunctionSQL(
    const std::vector<std::string>& inputs) {
  return ArrayAtFunctionSQL("SAFE_ORDINAL", inputs);
}
std::string ProtoMapAtKeySQL(const std::vector<std::string>& inputs) {
  return ArrayAtFunctionSQL("KEY", inputs);
}
std::string SafeProtoMapAtKeySQL(absl::Span<const std::string> inputs) {
  return ArrayAtFunctionSQL("SAFE_KEY", inputs);
}
std::string GenerateDateTimestampArrayFunctionSQL(
    absl::string_view function_name, absl::Span<const std::string> inputs) {
  ABSL_DCHECK(inputs.size() == 2 || inputs.size() == 4);
  std::string sql =
      absl::StrCat(function_name, "(", inputs[0], ", ", inputs[1]);
  if (inputs.size() == 4) {
    if (inputs[2][0] == '[') {
      // Fix the function signature text:
      // INTERVAL [INT64] [DATE_TIME_PART] --> [INTERVAL INT64 DATE_TIME_PART]
      ABSL_DCHECK_EQ(inputs[3][0], '[');
      absl::StrAppend(&sql, ", [INTERVAL ",
                      inputs[2].substr(1, inputs[2].size() - 2), " ",
                      inputs[3].substr(1, inputs[3].size() - 2), "]");
    } else {
      absl::StrAppend(&sql, ", INTERVAL ", inputs[2], " ", inputs[3]);
    }
  }
  absl::StrAppend(&sql, ")");
  return sql;
}

// For MakeArray we explicitly prepend the array type to the function sql, thus
// array-type-name is expected to be passed as the first element of inputs.
std::string MakeArrayFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GT(inputs.size(), 0);
  const std::string& type_name = inputs[0];
  const std::vector<std::string> args(inputs.begin() + 1, inputs.end());
  return absl::StrCat(type_name, "[", absl::StrJoin(args, ", "), "]");
}

std::string ExtractFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GT(inputs.size(), 1);
  std::string prefix = absl::StrCat("EXTRACT(", inputs[1], " FROM ", inputs[0]);
  if (inputs.size() > 2) {
    absl::StrAppend(&prefix, " AT TIME ZONE ", inputs[2]);
  }
  return absl::StrCat(prefix, ")");
}

std::string ExtractDateOrTimeFunctionSQL(absl::string_view date_part,
                                         absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GT(inputs.size(), 0);
  std::string prefix = absl::StrCat("EXTRACT(", date_part, " FROM ", inputs[0]);
  if (inputs.size() > 1) {
    absl::StrAppend(&prefix, " AT TIME ZONE ", inputs[1]);
  }
  return absl::StrCat(prefix, ")");
}

bool ArgumentIsStringLiteral(const InputArgumentType& argument) {
  if (argument.type() != nullptr && argument.type()->IsString() &&
      argument.is_literal()) {
    return true;
  }
  return false;
}

absl::Status EnsureArgumentsHaveType(
    absl::string_view function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  for (const InputArgumentType& arg : arguments) {
    if (arg.type() == nullptr) {
      return MakeSqlError()
             << function_name << " does not support arguments of type "
             << arg.UserFacingName(product_mode);
    }
  }
  return absl::OkStatus();
}

absl::Status EnsureArgumentsHaveType(
    absl::string_view function_name,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  return EnsureArgumentsHaveType(function_name, arguments,
                                 language_options.product_mode());
}

absl::Status CheckDateDatetimeTimeTimestampDiffArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 3) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(function_name, arguments, language_options));

  // TODO: Add a test where the date part is an enum via a normal
  // function call, but not the expected enum type.
  // The argument should be non-NULL because of
  // Resolver::MakeDatePartEnumResolvedLiteralFromName, but check again for
  // safety so we can't crash.
  if (arguments[2].type()->IsEnum() && arguments[2].is_literal() &&
      !arguments[2].is_literal_null()) {
    switch (arguments[2].literal_value()->enum_value()) {
      case functions::YEAR:
      case functions::ISOYEAR:
      case functions::QUARTER:
      case functions::MONTH:
      case functions::WEEK:
      case functions::WEEK_MONDAY:
      case functions::WEEK_TUESDAY:
      case functions::WEEK_WEDNESDAY:
      case functions::WEEK_THURSDAY:
      case functions::WEEK_FRIDAY:
      case functions::WEEK_SATURDAY:
      case functions::ISOWEEK:
        if (!arguments[0].type()->IsTimestamp() &&
            !arguments[0].type()->IsTime()) {
          return absl::OkStatus();
        }
        break;
      case functions::DAY:
        if (!arguments[0].type()->IsTime()) {
          return absl::OkStatus();
        }
        break;
      case functions::HOUR:
      case functions::MINUTE:
      case functions::SECOND:
      case functions::MILLISECOND:
      case functions::MICROSECOND:
        // DATE doesn't support time parts
        if (!arguments[0].type()->IsDate()) {
          return absl::OkStatus();
        }
        break;
      case functions::NANOSECOND:
        if (!arguments[0].type()->IsDate() &&
            language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
          return absl::OkStatus();
        }
        break;
      case functions::PICOSECOND:
        if (arguments[0].type()->IsTimestamp() &&
            language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
          return absl::OkStatus();
        }
        break;
      default:
        break;
    }
    std::string extended_signature_error;
    if (language_options.LanguageFeatureEnabled(
            FEATURE_EXTENDED_DATE_TIME_SIGNATURES)) {
      extended_signature_error = absl::StrCat(
          " when the argument is ",
          arguments[0].UserFacingName(language_options.product_mode()),
          " type");
    }
    return MakeSqlError() << function_name << " does not support the "
                          << DateTimestampPartToSQL(
                                 arguments[2].literal_value()->enum_value())
                          << " date part" << extended_signature_error;
  }
  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckBitwiseOperatorArgumentsHaveSameType(
    absl::string_view operator_string,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  // We do not want non-literal arguments to implicitly coerce.  We currently
  // have signatures for all the integer/BYTES types, so we only need to check
  // that the arguments are both BYTES, or they are both integers and either
  // they are both the same type or at least one is a literal.
  if (arguments.size() != 2) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(operator_string, arguments, language_options));

  if (arguments[0].type()->IsBytes() && arguments[1].type()->IsBytes()) {
    return absl::OkStatus();
  }
  if (!arguments[0].type()->IsInteger() || !arguments[1].type()->IsInteger() ||
      (!arguments[0].type()->Equals(arguments[1].type()) &&
       !arguments[0].is_literal() && !arguments[1].is_literal())) {
    return MakeSqlError()
           << "Bitwise operator " << operator_string
           << " requires two integer/BYTES arguments of the same type, "
           << "but saw " << arguments[0].type()->DebugString() << " and "
           << arguments[1].type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status CheckBitwiseOperatorFirstArgumentIsIntegerOrBytes(
    absl::string_view operator_string,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  // We do not want the first argument to implicitly coerce to integer.
  // We currently have signatures for all the integer types and BYTES for the
  // first argument, so we only need to check that it is an integer or BYTES
  // and it is guaranteed to find an exact matching type for the first argument.
  if (arguments.empty()) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(operator_string, arguments, language_options));

  if (!arguments[0].type()->IsInteger() && !arguments[0].type()->IsBytes()) {
    return MakeSqlError() << "The first argument to bitwise operator "
                          << operator_string
                          << " must be an integer or BYTES but saw "
                          << arguments[0].type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status CheckDateDatetimeTimeTimestampTruncArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() < 2) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(function_name, arguments, language_options));

  if (arguments[1].type()->IsEnum() && arguments[1].is_literal() &&
      !arguments[1].is_null()) {
    switch (arguments[1].literal_value()->enum_value()) {
      case functions::YEAR:
      case functions::ISOYEAR:
      case functions::QUARTER:
      case functions::MONTH:
      case functions::WEEK:
      case functions::ISOWEEK:
      case functions::WEEK_MONDAY:
      case functions::WEEK_TUESDAY:
      case functions::WEEK_WEDNESDAY:
      case functions::WEEK_THURSDAY:
      case functions::WEEK_FRIDAY:
      case functions::WEEK_SATURDAY:
      case functions::DAY:
        if (!arguments[0].type()->IsTime()) {
          return absl::OkStatus();
        }
        break;
      case functions::HOUR:
      case functions::MINUTE:
      case functions::SECOND:
      case functions::MILLISECOND:
      case functions::MICROSECOND:
        if (!arguments[0].type()->IsDate()) {
          return absl::OkStatus();
        }
        break;
      case functions::NANOSECOND:
        if (!arguments[0].type()->IsDate() &&
            language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
          return absl::OkStatus();
        }
        break;
      case functions::PICOSECOND:
        if (arguments[0].type()->IsTimestamp() &&
            language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
          return absl::OkStatus();
        }
        break;
      default:
        break;
    }
    return MakeSqlError() << function_name << " does not support the "
                          << DateTimestampPartToSQL(
                                 arguments[1].literal_value()->enum_value())
                          << " date part";
  }

  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckLastDayArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() < 2) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(function_name, arguments, language_options));

  // LAST_DAY only supports date parts WEEK, MONTH, QUARTER,
  // YEAR.  If the second argument is a literal then check the date part.
  if (arguments[1].type()->IsEnum() && arguments[1].is_literal()) {
    switch (arguments[1].literal_value()->enum_value()) {
      case functions::YEAR:
      case functions::ISOYEAR:
      case functions::QUARTER:
      case functions::MONTH:
      case functions::WEEK:
      case functions::ISOWEEK:
      case functions::WEEK_MONDAY:
      case functions::WEEK_TUESDAY:
      case functions::WEEK_WEDNESDAY:
      case functions::WEEK_THURSDAY:
      case functions::WEEK_FRIDAY:
      case functions::WEEK_SATURDAY:
        return absl::OkStatus();
      default:
        return MakeSqlError() << function_name << " does not support the "
                              << DateTimestampPartToSQL(
                                     arguments[1].literal_value()->enum_value())
                              << " date part";
    }
  }
  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckExtractPreResolutionArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  // The first argument to the EXTRACT function cannot be a string literal
  // since that causes overloading issues.  The argument can be either DATE,
  // TIMESTAMP, or TIMESTAMP_* and a string literal coerces to all.
  if (arguments.empty()) {
    return MakeSqlError() << "EXTRACT's arguments cannot be empty.";
  }
  if (ArgumentIsStringLiteral(arguments[0])) {
    return MakeSqlError()
           << "EXTRACT does not support literal STRING arguments";
  }
  return absl::OkStatus();
}

absl::Status CheckExtractPostResolutionArguments(
    const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() > 1) {
    GOOGLESQL_RET_CHECK(functions::DateTimestampPart_IsValid(
        arguments[1].literal_value()->enum_value()));
    functions::DateTimestampPart date_part =
        static_cast<googlesql::functions::DateTimestampPart>(
            arguments[1].literal_value()->enum_value());

    using DateTimePartSet = absl::flat_hash_set<functions::DateTimestampPart>;
    if (arguments[0].type()->IsDate()) {
      static googlesql_base::NoDestructor<DateTimePartSet> valid_parts(DateTimePartSet(
          {functions::YEAR, functions::ISOYEAR, functions::QUARTER,
           functions::MONTH, functions::WEEK, functions::WEEK_MONDAY,
           functions::WEEK_TUESDAY, functions::WEEK_WEDNESDAY,
           functions::WEEK_THURSDAY, functions::WEEK_FRIDAY,
           functions::WEEK_SATURDAY, functions::ISOWEEK, functions::DAY,
           functions::DAYOFWEEK, functions::DAYOFYEAR}));
      if (!valid_parts->contains(date_part)) {
        return MakeSqlError() << ExtractingNotSupportedDatePart(
                   "DATE", DateTimestampPartToSQL(
                               arguments[1].literal_value()->enum_value()));
      }
    }
    if (arguments[0].type()->IsTime()) {
      static googlesql_base::NoDestructor<DateTimePartSet> valid_parts(
          DateTimePartSet({functions::NANOSECOND, functions::MICROSECOND,
                           functions::MILLISECOND, functions::SECOND,
                           functions::MINUTE, functions::HOUR}));
      if (!valid_parts->contains(date_part)) {
        return MakeSqlError() << ExtractingNotSupportedDatePart(
                   "TIME", DateTimestampPartToSQL(
                               arguments[1].literal_value()->enum_value()));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status CheckDateDatetimeTimestampAddSubArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 3) {
    // Let validation happen normally. It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(function_name, arguments, language_options));

  // For Date, the only supported date parts are DAY, WEEK, MONTH, QUARTER,
  // YEAR.  For Timestamp, additional parts are supported.  If the third
  // parameter is a literal string then check the date part.
  if (!arguments[2].type()->IsEnum() || !arguments[2].is_literal()) {
    // Let validation happen normally. It will return an error later.
    return absl::OkStatus();
  }

  switch (arguments[2].literal_value()->enum_value()) {
    case functions::YEAR:
    case functions::QUARTER:
    case functions::MONTH:
    case functions::WEEK:
      // Only TIMESTAMP type doesn't support these dateparts
      if (!arguments[0].type()->IsTimestamp()) {
        return absl::OkStatus();
      }
      break;
    case functions::DAY:
      // All functions support DAY
      return absl::OkStatus();
    case functions::HOUR:
    case functions::MINUTE:
    case functions::SECOND:
    case functions::MILLISECOND:
    case functions::MICROSECOND:
      // Only DATE type doesn't support these dateparts
      if (!arguments[0].type()->IsDate()) {
        return absl::OkStatus();
      }
      break;
    case functions::NANOSECOND:
      if (!arguments[0].type()->IsDate() &&
          language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
        return absl::OkStatus();
      }
      break;
    case functions::PICOSECOND:
      if (arguments[0].type()->IsTimestamp() &&
          language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
        return absl::OkStatus();
      }
      break;
    default:
      break;
  }
  std::string extended_signature_error;
  if (language_options.LanguageFeatureEnabled(
          FEATURE_EXTENDED_DATE_TIME_SIGNATURES)) {
    extended_signature_error = absl::StrCat(
        " when the argument is ",
        arguments[0].UserFacingName(language_options.product_mode()), " type");
  }
  return MakeSqlError() << function_name << " does not support the "
                        << DateTimestampPartToSQL(
                               arguments[2].literal_value()->enum_value())
                        << " date part" << extended_signature_error;
}

absl::Status CheckTimeAddSubArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 3) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(function_name, arguments, language_options));

  if (arguments[2].type()->IsEnum() && arguments[2].is_literal()) {
    switch (arguments[2].literal_value()->enum_value()) {
      case functions::HOUR:
      case functions::MINUTE:
      case functions::SECOND:
      case functions::MILLISECOND:
      case functions::MICROSECOND:
        return absl::OkStatus();
      case functions::NANOSECOND: {
        if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
          return absl::OkStatus();
        }
        break;
      }
      default:
        break;
    }
    return MakeSqlError() << function_name << " does not support the "
                          << DateTimestampPartToSQL(
                                 arguments[2].literal_value()->enum_value())
                          << " date part";
  }
  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckGenerateDateArrayArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 4) {
    // GENERATE_DATE_ARRAY(start, end, INTERVAL int) is not valid; if the
    // interval is present, the date part must be as well.
    if (arguments.size() != 2) {
      return MakeSqlError()
             << "Expected either GENERATE_DATE_ARRAY(start, end) or "
                "GENERATE_DATE_ARRAY(start, end, INTERVAL int [date_part])";
    }
    return absl::OkStatus();
  }

  // For Date, the only supported date parts are DAY, WEEK, MONTH, QUARTER,
  // YEAR.
  if (arguments[3].is_literal()) {
    switch (arguments[3].literal_value()->enum_value()) {
      case functions::YEAR:
      case functions::QUARTER:
      case functions::MONTH:
      case functions::WEEK:
      case functions::DAY:
        return absl::OkStatus();
      default:
        break;
    }
    return MakeSqlError() << "GENERATE_DATE_ARRAY does not support the "
                          << DateTimestampPartToSQL(
                                 arguments[3].literal_value()->enum_value())
                          << " date part";
  }
  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckGenerateTimestampArrayArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 4) {
    // Wrong number of arguments; let validation continue normally.
    return absl::OkStatus();
  }

  if (arguments[3].is_literal()) {
    switch (arguments[3].literal_value()->enum_value()) {
      case functions::MICROSECOND:
      case functions::MILLISECOND:
      case functions::SECOND:
      case functions::MINUTE:
      case functions::HOUR:
      case functions::DAY:
        return absl::OkStatus();
      case functions::NANOSECOND:
        if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
          return absl::OkStatus();
        }
        break;
      case functions::PICOSECOND:
        if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
          return absl::OkStatus();
        }
        break;
      default:
        break;
    }
    return MakeSqlError() << "GENERATE_TIMESTAMP_ARRAY does not support the "
                          << DateTimestampPartToSQL(
                                 arguments[3].literal_value()->enum_value())
                          << " date part";
  }
  // Let validation of other arguments happen normally.
  return absl::OkStatus();
}

absl::Status CheckFormatPostResolutionArguments(
    const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_GE(arguments.size(), 1);
  GOOGLESQL_RET_CHECK(arguments[0].type()->IsString() || arguments[0].is_untyped_null());

  if (arguments[0].is_literal() && !arguments[0].is_literal_null()) {
    const std::string& pattern = arguments[0].literal_value()->string_value();
    std::vector<const Type*> value_types;
    for (int i = 1; i < arguments.size(); ++i) {
      value_types.push_back(arguments[i].type());
    }
    absl::Status status = functions::CheckStringFormatUtf8ArgumentTypes(
    pattern, value_types, language_options.product_mode());
    if (status.code() == absl::StatusCode::kOutOfRange) {
      return MakeSqlError() << status.message();
    } else {
      return status;
    }
  }

  return absl::OkStatus();
}

absl::Status CheckIsSupportedKeyType(
    absl::string_view function_name,
    const std::set<std::string>& supported_key_types,
    int key_type_argument_index, absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() <= key_type_argument_index) {
    // Wrong number of arguments; let validation happen normally.
    return absl::OkStatus();
  }

  const absl::string_view argument_index_name =
      key_type_argument_index == 0 ? "First" : "Second";

  const InputArgumentType& argument = arguments[key_type_argument_index];
  if (!argument.is_literal() && !argument.is_query_parameter()) {
    return MakeSqlError() << argument_index_name
                          << " argument (key type) in function "
                          << function_name
                          << " must be a string literal or query parameter";
  }

  // If the type is wrong (or no type is assigned), or the argument is a query
  // parameter, let validation continue.
  if (!argument.type()->IsString() || !argument.is_literal()) {
    return absl::OkStatus();
  }

  // Check the key type for string literals.
  const std::string& key_type = argument.literal_value()->string_value();
  if (!googlesql_base::ContainsKey(supported_key_types, key_type)) {
    const std::string key_type_list =
        supported_key_types.size() == 1
            ? absl::StrCat("'", *supported_key_types.begin(), "'")
            : absl::StrCat(
                  "one of ",
                  absl::StrJoin(
                      supported_key_types, ", ",
                      [](std::string* out, const absl::string_view key_type) {
                        absl::StrAppend(out, "'", key_type, "'");
                      }));
    return MakeSqlError() << argument_index_name
                          << " argument (key type) in function "
                          << function_name << " must be " << key_type_list
                          << ", but is '" << key_type << "'";
  }
  return absl::OkStatus();
}

const std::set<std::string>& GetSupportedKeyTypes() {
  static const auto* kSupportedKeyTypes = new std::set<std::string>{
      "AEAD_AES_GCM_256", "DETERMINISTIC_AEAD_AES_SIV_CMAC_256"};
  return *kSupportedKeyTypes;
}

const std::set<std::string>& GetSupportedRawKeyTypes() {
  static const auto* kSupportedKeyTypes =
      new std::set<std::string>{"AES_GCM", "AES_CBC_PKCS"};
  return *kSupportedKeyTypes;
}

bool IsStringLiteralComparedToBytes(const InputArgumentType& lhs_arg,
                                    const InputArgumentType& rhs_arg) {
  return lhs_arg.type() != nullptr && rhs_arg.type() != nullptr &&
         ((lhs_arg.type()->IsString() && lhs_arg.is_literal() &&
           rhs_arg.type()->IsBytes()) ||
          (lhs_arg.type()->IsBytes() && rhs_arg.type()->IsString() &&
           rhs_arg.is_literal()));
}

std::string NoMatchingSignatureForCaseNoValueFunction(
    absl::string_view qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  std::string error_message =
      absl::StrCat("No matching signature for ", qualified_function_name);
  // Even number arguments represent WHEN expressions and must be BOOL, except
  // for the last argument (which is an ELSE argument).  Other arguments must
  // all be coercible to a common supertype.
  //
  // We use ordered sets so that the order of types in error messages is
  // deterministic.
  std::set<std::string, googlesql_base::CaseLess> non_bool_when_types;
  std::set<std::string, googlesql_base::CaseLess> then_else_types;
  for (int i = 0; i < arguments.size(); ++i) {
    if (i % 2 == 0 && i < arguments.size() - 1) {
      // This must be a BOOL expression.  If not, remember it.
      if (arguments[i].type() == nullptr || !arguments[i].type()->IsBool()) {
        googlesql_base::InsertIfNotPresent(&non_bool_when_types,
                                arguments[i].UserFacingName(product_mode));
      }
      continue;
    }
    // Otherwise, all THEN/ELSE types must be commonly coercible.  Ignore
    // untyped arguments since they can (mostly) coerce to any type.
    if (!arguments[i].is_untyped()) {
      googlesql_base::InsertIfNotPresent(&then_else_types,
                              arguments[i].UserFacingName(product_mode));
    }
  }
  if (!non_bool_when_types.empty()) {
    absl::StrAppend(&error_message,
                    "; all WHEN argument types must be BOOL but found: ",
                    absl::StrJoin(non_bool_when_types, ", "));
  }
  // Note that we can get a false positive in cases where the argument types
  // are different but coercible (i.e., INT32 and INT64).  If this becomes
  // a confusing issue in practice then we can tweak this logic further.
  if (then_else_types.size() > 1) {
    absl::StrAppend(&error_message,
                    "; all THEN/ELSE arguments must be coercible to a common "
                    "type but found: ",
                    absl::StrJoin(then_else_types, ", "));
  }
  std::string argument_types;
  for (int idx = 0; idx < arguments.size(); ++idx) {
    if (idx % 2 == 0) {
      if (idx < arguments.size() - 1) {
        // WHEN, so add leading '('
        absl::StrAppend(&argument_types, " (");
      } else {
        absl::StrAppend(&argument_types, " ");
      }
      absl::StrAppend(&argument_types,
                      arguments[idx].UserFacingName(product_mode));
    } else {
      // THEN, so add trailing ')'
      absl::StrAppend(&argument_types, " ",
                      arguments[idx].UserFacingName(product_mode), ")");
    }
  }
  absl::StrAppend(&error_message,
                  "; actual argument types (WHEN THEN) ELSE:", argument_types);
  return error_message;
}

std::string NoMatchingSignatureForFunctionUsingInterval(
    const std::string& qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode,
    int index_of_interval_argument) {
  // index_of_interval_argument is the index of the INTERVAL expression in the
  // function's argument list. For example, it is 1 for DATE_SUB(DATE, INTERVAL
  // INT64 DATE_TIME_PART), and it is 2 for GENERATE_DATE_ARRAY(DATE, DATE,
  // INTERVAL INT64 DATE_TIME_PART).
  if (arguments.size() < index_of_interval_argument + 2) {
    // No INTERVAL expression was resolved. This can happen if the INTERVAL
    // expression is optional and unspecified in the function call.
    return Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
        qualified_function_name, arguments, product_mode);
  }
  // The INTERVAL expression was resolved and flattened as 2 arguments in the
  // argument list.
  std::vector<std::string> argument_texts;
  argument_texts.reserve(arguments.size() - 1);
  for (int i = 0; i < index_of_interval_argument; ++i) {
    argument_texts.push_back(arguments[i].UserFacingName(product_mode));
  }
  argument_texts.push_back(absl::StrCat(
      "INTERVAL ",
      arguments[index_of_interval_argument].UserFacingName(product_mode), " ",
      arguments[index_of_interval_argument + 1].UserFacingName(product_mode)));
  for (int i = index_of_interval_argument + 2; i < arguments.size(); ++i) {
    argument_texts.push_back(arguments[i].UserFacingName(product_mode));
  }
  return absl::StrCat(
      "No matching signature for ", qualified_function_name,
      " for argument types: ", absl::StrJoin(argument_texts, ", "));
}

std::string NoMatchingSignatureForDateOrTimeAddOrSubFunction(
    const std::string& qualified_function_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode) {
  return NoMatchingSignatureForFunctionUsingInterval(
      qualified_function_name, arguments, product_mode,
      /*index_of_interval_argument=*/1);
}

std::string NoMatchingSignatureForGenerateDateOrTimestampArrayFunction(
    const std::string& qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  return NoMatchingSignatureForFunctionUsingInterval(
      qualified_function_name, arguments, product_mode,
      /*index_of_interval_argument=*/2);
}

std::string NoMatchingSignatureForSubscript(
    absl::string_view offset_or_ordinal, absl::string_view operator_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode) {
  const std::string element_type =
      (arguments.size() > 1 ? arguments[1].UserFacingName(product_mode) : "");
  const std::string element_string =
      absl::StrCat(offset_or_ordinal, (offset_or_ordinal.empty() ? "" : "("),
                   element_type, (offset_or_ordinal.empty() ? "" : ")"));
  std::string msg = absl::StrCat("Subscript access using [", element_string,
                                 "] is not supported");
  if (!arguments.empty()) {
    absl::StrAppend(&msg, " on values of type ",
                    arguments[0].UserFacingName(product_mode));
  }
  return msg;
}

absl::Status CheckArgumentsSupportEquality(
    absl::string_view comparison_name, const FunctionSignature& signature,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(signature.NumConcreteArguments(), arguments.size());
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(comparison_name, arguments, language_options));

  for (int idx = 0; idx < arguments.size(); ++idx) {
    if (!arguments[idx].type()->SupportsEquality(language_options)) {
      return MakeSqlError()
             << comparison_name << " is not defined for arguments of type "
             << arguments[idx].DebugString();
    }
  }
  return absl::OkStatus();
}

absl::Status CheckArgumentsSupportGrouping(
    absl::string_view comparison_name, const FunctionSignature& signature,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(signature.NumConcreteArguments(), arguments.size());
  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType(comparison_name, arguments, language_options));
  for (int idx = 0; idx < arguments.size(); ++idx) {
    if (!arguments[idx].type()->SupportsGrouping(language_options)) {
      return MakeSqlError()
             << comparison_name << " is not defined for arguments of type "
             << arguments[idx].DebugString();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<const Type*> GetOrMakeEnumValueDescriptorType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle,
    const FunctionSignature& /*signature*/,
    const std::vector<InputArgumentType>& arguments,
    const AnalyzerOptions& analyzer_options) {
  const Type* catalog_type = nullptr;
  absl::Status status =
      catalog->FindType({"proto2.EnumValueDescriptorProto"}, &catalog_type);

  // In order for the catalog type to be used as the return type, its descriptor
  // must be equivalent to google::protobuf::EnumValueDescriptorProto::descriptor(). We
  // check for equivalence by comparing the full name of the descriptors (this
  // mirrors the behavior of ProtoType::Equivalent()).
  if (status.ok() &&
      catalog_type->AsProto()->descriptor()->full_name() ==
          google::protobuf::EnumValueDescriptorProto::descriptor()->full_name()) {
    return catalog_type;
  }

  const ProtoType* default_return_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeProtoType(
      google::protobuf::EnumValueDescriptorProto::descriptor(), &default_return_type));
  return default_return_type;
}

absl::Status PreResolutionCheckArgumentsSupportComparison(
    absl::string_view comparison_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  int bad_argument_idx;
  if (ArgumentsAreComparable(arguments, language_options, &bad_argument_idx)) {
    return absl::OkStatus();
  }
  return MakeSqlError() << comparison_name
                        << " is not defined for arguments of type "
                        << arguments[bad_argument_idx].DebugString();
}

absl::Status CheckArgumentsSupportComparison(
    absl::string_view comparison_name, const FunctionSignature& /*signature*/,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  return PreResolutionCheckArgumentsSupportComparison(
      comparison_name, arguments, language_options);
}

absl::Status CheckMinMaxArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  return PreResolutionCheckArgumentsSupportComparison(function_name, arguments,
                                                      language_options);
}

// Similar to MIN/MAX, but some engines can't yet handle GREATEST/LEAST,
// so we need to check the flag.
absl::Status CheckGreatestLeastArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RETURN_IF_ERROR(PreResolutionCheckArgumentsSupportComparison(
      function_name, arguments, language_options));
  if (!language_options.LanguageFeatureEnabled(
          LanguageFeature::FEATURE_ARRAY_GREATEST_LEAST)) {
    for (auto& arg : arguments) {
      if (arg.type()->IsArray()) {
        return MakeSqlError() << function_name << "() on arrays require the "
                              << "FEATURE_ARRAY_GREATEST_LEAST flag";
      }
    }
  }
  return absl::OkStatus();
}

absl::Status CheckArrayAggArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  int bad_argument_idx;
  if (!ArgumentsArrayType(arguments, false /* is_array */, &bad_argument_idx)) {
    return MakeSqlError() << "The argument to ARRAY_AGG must not be an array "
                          << "type but was "
                          << arguments[bad_argument_idx].DebugString();
  }
  return absl::OkStatus();
}

absl::Status CheckArrayConcatArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  int bad_argument_idx;
  if (!ArgumentsArrayType(arguments, true /* is_array */, &bad_argument_idx)) {
    return MakeSqlError()
           << "The argument to ARRAY_CONCAT (or ARRAY_CONCAT_AGG) "
           << "must be an array type but was "
           << arguments[bad_argument_idx].DebugString();
  }
  return absl::OkStatus();
}

absl::Status CheckArrayIsDistinctArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.empty()) {
    // Let validation happen normally.  It will return an error later.
    return absl::OkStatus();
  }
  const InputArgumentType& arg0 = arguments[0];
  if (arg0.is_null()) {
    return absl::OkStatus();
  }

  if (arg0.type() == nullptr || !arg0.type()->IsArray()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "ARRAY_IS_DISTINCT cannot be used on non-array type "
           << arg0.UserFacingName(language_options.product_mode());
  }

  const ArrayType* array_type = arg0.type()->AsArray();
  GOOGLESQL_RET_CHECK_NE(array_type, nullptr);

  if (!array_type->element_type()->SupportsGrouping(language_options,
                                                    nullptr)) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "ARRAY_IS_DISTINCT cannot be used on argument of type "
           << array_type->ShortTypeName(language_options.product_mode())
           << " because the array's element type does not support "
              "grouping";
  }

  return absl::OkStatus();
}

absl::Status CheckArrayDistinctArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.empty()) {
    // Let validation happen normally. It will return an error later.
    return absl::OkStatus();
  }
  const InputArgumentType& arg0 = arguments[0];
  if (arg0.is_null()) {
    return absl::OkStatus();
  }

  if (arg0.type() == nullptr || !arg0.type()->IsArray()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "ARRAY_DISTINCT cannot be used on non-array type "
           << arg0.UserFacingName(language_options.product_mode());
  }

  const ArrayType* array_type = arg0.type()->AsArray();
  GOOGLESQL_RET_CHECK_NE(array_type, nullptr);

  std::string type_description;
  if (!array_type->element_type()->SupportsGrouping(language_options,
                                                    &type_description)) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "ARRAY_DISTINCT cannot be used on argument of type "
           << array_type->ShortTypeName(language_options.product_mode())
           << " because the array's element type does not support "
              "grouping"
           << (type_description.empty() ? "" : ": ") << type_description;
  }

  return absl::OkStatus();
}

absl::Status CheckRangeBucketArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 2) {
    // Let validation happen normally. It will return an error later.
    return absl::OkStatus();
  }

  GOOGLESQL_RETURN_IF_ERROR(
      EnsureArgumentsHaveType("RANGE_BUCKET", arguments, language_options));
  if (!arguments[0].type()->SupportsOrdering(language_options,
                                             /*type_description=*/nullptr)) {
    return MakeSqlError() << "First argument to RANGE_BUCKET of type "
                          << arguments[0].type()->ShortTypeName(
                                 language_options.product_mode())
                          << " does not support ordering";
  }

  if (arguments[1].type()->IsArray()) {
    if (!arguments[1].type()->AsArray()->element_type()->SupportsOrdering(
            language_options, /*type_description=*/nullptr)) {
      return MakeSqlError()
             << "Second argument to RANGE_BUCKET of type "
             << arguments[1].type()->ShortTypeName(
                    language_options.product_mode())
             << " is not supported because array element type does not support "
             << "ordering";
    }
  } else {
    // Untyped parameter can be coerced to any type.
    if (!arguments[1].is_untyped_query_parameter()) {
      return MakeSqlError()
             << "Second argument of RANGE_BUCKET must be an array but was "
             << arguments[1].type()->ShortTypeName(
                    language_options.product_mode());
    }
  }

  return absl::OkStatus();
}

std::string AnonCountStarBadArgumentErrorPrefix(const FunctionSignature&,
                                                int idx) {
  switch (idx) {
    case 0:
      return "Lower bound on CLAMPED BETWEEN";
    case 1:
      return "Upper bound on CLAMPED BETWEEN";
    default:
      return absl::StrCat("Argument ", idx, " to ANON_COUNT(*)");
  }
}

FunctionOptions DefaultAggregateFunctionOptions() {
  return FunctionOptions(
      /*window_ordering_support_in=*/FunctionOptions::ORDER_OPTIONAL,
      /*window_framing_support_in=*/true);
}

FunctionSignatureOptions SetDefinitionForInlining(absl::string_view sql,
                                                  bool enabled) {
  return FunctionSignatureOptions().set_rewrite_options(
      FunctionSignatureRewriteOptions()
          .set_enabled(enabled)
          .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
          .set_sql(sql));
}

bool CanStringConcatCoerceFrom(const googlesql::Type* arg_type) {
  return arg_type->IsBool() || arg_type->IsNumerical() ||
         arg_type->IsTimestamp() || arg_type->IsCivilDateOrTimeType() ||
         arg_type->IsInterval() || arg_type->IsProto() || arg_type->IsEnum();
}

// Checks if an arithmetic operation has a floating point type as its input.
std::string CheckHasFloatingPointArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  for (const InputArgumentType& argument : arguments) {
    if (argument.type()->IsFloatingPoint()) {
      return "";
    }
  }
  return "At least one argument must have a floating point type";
}

// Checks if at least one input argument has NUMERIC type.
std::string CheckHasNumericTypeArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  for (const InputArgumentType& argument : arguments) {
    if (argument.type()->kind() == TYPE_NUMERIC) {
      return "";
    }
  }
  return "At least one argument must have NUMERIC type";
}

// Checks if all input arguments have NUMERIC or BIGNUMERIC type, including the
// case without input arguments.
std::string CheckAllArgumentsHaveNumericOrBigNumericType(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  for (const InputArgumentType& argument : arguments) {
    if (argument.type()->kind() != TYPE_NUMERIC &&
        argument.type()->kind() != TYPE_BIGNUMERIC) {
      return "All arguments must have NUMERIC or BIGNUMERIC type";
    }
  }
  return "";
}

// Checks if there is at least one input argument and the last argument has
// NUMERIC type or BIGNUMERIC type.
std::string CheckLastArgumentHasNumericOrBigNumericType(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  if (!arguments.empty() &&
      (arguments.back().type()->kind() == TYPE_NUMERIC ||
       arguments.back().type()->kind() == TYPE_BIGNUMERIC)) {
    return "";
  }
  return "Last argument must have NUMERIC or BIGNUMERIC type";
}

// Checks if at least one input argument has BIGNUMERIC type.
std::string CheckHasBigNumericTypeArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  for (const InputArgumentType& argument : arguments) {
    if (argument.type()->kind() == TYPE_BIGNUMERIC) {
      return "";
    }
  }
  return "At least one argument must have BigNumeric type";
}

// Checks if at least one input argument has INTERVAL type.
std::string CheckHasIntervalTypeArgument(
    const FunctionSignature& matched_signature,
    const std::vector<InputArgumentType>& arguments) {
  for (const InputArgumentType& argument : arguments) {
    if (argument.type()->kind() == TYPE_INTERVAL) {
      return "";
    }
  }
  return "At least one argument must have INTERVAL type";
}

// Compute the result type for TOP_COUNT and TOP_SUM.
// The output type is
//   ARRAY<
//     STRUCT<`value` <arguments[0].type()>,
//            `<field2_name>` <arguments[1].type()> > >
absl::StatusOr<const Type*> ComputeResultTypeForTopStruct(
    const std::string& field2_name, Catalog* catalog, TypeFactory* type_factory,
    CycleDetector* cycle_detector, const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_GE(arguments.size(), 2);
  const Type* element_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(
      {{"value", arguments[0].type()}, {field2_name, arguments[1].type()}},
      &element_type));
  const Type* result_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(element_type, &result_type));
  return result_type;
}

// Compute the result type for ST_NEAREST_NEIGHBORS.
// The output type is
//   ARRAY<
//     STRUCT<`neighbor` <arguments[0].type>,
//            `distance` Double> >
absl::StatusOr<const Type*> ComputeResultTypeForNearestNeighborsStruct(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  const Type* element_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(
      {{"neighbor", arguments[0].type()}, {"distance", types::DoubleType()}},
      &element_type));
  const Type* result_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(element_type, &result_type));
  return result_type;
}

static bool FunctionIsDisabled(const GoogleSQLBuiltinFunctionOptions& options,
                               const FunctionOptions& function_options) {
  const LanguageOptions& language_options = options.language_options;

  if (language_options.product_mode() == PRODUCT_EXTERNAL &&
      !function_options.allow_external_usage) {
    return true;
  }

  if (!function_options.CheckAllRequiredFeaturesAreEnabled(
          language_options.GetEnabledLanguageFeatures())) {
    return true;
  }

  return false;
}

static bool TableValuedFunctionIsDisabled(
    const GoogleSQLBuiltinFunctionOptions& options,
    const TableValuedFunctionOptions& function_options) {
  const LanguageOptions& language_options = options.language_options;
  if (!function_options.CheckAllRequiredFeaturesAreEnabled(
          language_options.GetEnabledLanguageFeatures())) {
    return true;
  }

  return false;
}

static FunctionSignature ToFunctionSignature(
    const FunctionSignatureOnHeap& signature_on_heap) {
  return FunctionSignature(signature_on_heap.Get());
}

static FunctionSignature ToFunctionSignature(
    const FunctionSignatureProxy& signature_proxy) {
  return FunctionSignature(signature_proxy);
}

static bool FunctionSignatureIsDisabled(
    const GoogleSQLBuiltinFunctionOptions& options,
    const FunctionSignature& signature) {
  const FunctionSignatureId id =
      static_cast<FunctionSignatureId>(signature.context_id());
  return (!options.include_function_ids.empty() &&
          !options.include_function_ids.contains(id)) ||
         options.exclude_function_ids.contains(id) ||
         signature.HasUnsupportedType(options.language_options);
  // TODO: b/333096857 - Also check that the signature has all required features
  // enabled.
}

static bool FunctionSignatureIsDisabled(
    const GoogleSQLBuiltinFunctionOptions& options,
    const FunctionSignatureOnHeap& signature_on_heap) {
  return FunctionSignatureIsDisabled(options, signature_on_heap.Get());
}

static bool FunctionSignatureIsDisabled(
    const GoogleSQLBuiltinFunctionOptions& options,
    const FunctionSignatureProxy& signature) {
  const FunctionSignatureId id =
      static_cast<FunctionSignatureId>(signature.context_id);
  if ((!options.include_function_ids.empty() &&
       !options.include_function_ids.contains(id)) ||
      options.exclude_function_ids.contains(id)) {
    return true;
  }
  if (signature.result_type.type != nullptr &&
      !signature.result_type.type->IsSupportedType(options.language_options)) {
    return true;
  }
  for (const FunctionArgumentTypeProxy& proxy_type : signature.arguments) {
    if (proxy_type.type != nullptr &&
        !proxy_type.type->IsSupportedType(options.language_options)) {
      return true;
    }
  }
  return false;
}

static absl::Status InsertCheckedFunction(
    NameToFunctionMap* functions, std::unique_ptr<Function> function,
    const GoogleSQLBuiltinFunctionOptions& options) {
  // Check if a rewrite override has been specified in the rewrite_enabled
  // map and set the enabled bit in FunctionSignatureRewriteOptions
  // accordingly.
  for (int idx = 0; idx < function->signatures().size(); ++idx) {
    const FunctionSignature& signature = function->signatures()[idx];
    GOOGLESQL_RET_CHECK(function->IsGoogleSQLBuiltin());
    auto found_override = options.rewrite_enabled.find(
        static_cast<FunctionSignatureId>(signature.context_id()));
    if (found_override != options.rewrite_enabled.end()) {
      FunctionSignature& mutable_signature =
          *function->GetMutableSignature(idx);
      auto opt_rewrite_options =
          mutable_signature.mutable_options().mutable_rewrite_options();
      if (opt_rewrite_options.has_value()) {
        mutable_signature.mutable_options()
            .mutable_rewrite_options()
            ->set_enabled(found_override->second);
      } else {
        if (found_override->second) {
          return absl::FailedPreconditionError(absl::StrCat(
              "Attempted to opt into a rewrite for the following function: ",
              function->Name(),
              " but found no valid FunctionSignatureRewriteOptions. Rewrites "
              "may "
              "only be opted into if a valid rewriter has already been "
              "configured for that function. "));
        }
      }
    }
  }
  // Not using IdentifierPathToString to avoid escaping things like '$add' and
  // 'if'.
  std::string name = absl::StrJoin(function->FunctionNamePath(), ".");
  GOOGLESQL_RET_CHECK(functions->emplace(name, std::move(function)).second)
      << name << "already exists";
  return absl::OkStatus();
}

void InsertCreatedFunction(NameToFunctionMap* functions,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           Function* function_in) {
  std::unique_ptr<Function> function(function_in);
  if (FunctionIsDisabled(options, function->function_options())) {
    return;
  }

  // Identify each signature that is unsupported via options checks.
  absl::flat_hash_set<int> signatures_to_remove;
  for (int idx = 0; idx < function->signatures().size(); ++idx) {
    const FunctionSignature& signature = function->signatures()[idx];
    if (FunctionSignatureIsDisabled(options, signature)) {
      signatures_to_remove.insert(idx);
    }
  }

  // Remove unsupported signatures.
  if (!signatures_to_remove.empty()) {
    if (signatures_to_remove.size() == function->signatures().size()) {
      // We are removing all signatures, so we do not need to insert
      // this function.
      GOOGLESQL_VLOG(4) << "Function excluded: " << function->Name();
      return;
    }

    std::vector<FunctionSignature> new_signatures;
    for (int idx = 0; idx < function->signatures().size(); ++idx) {
      const FunctionSignature& signature = function->signatures()[idx];
      if (!signatures_to_remove.contains(idx)) {
        new_signatures.push_back(signature);
      } else {
        GOOGLESQL_VLOG(4) << "FunctionSignature excluded: "
                << signature.DebugString(function->Name());
      }
    }
    function->ResetSignatures(new_signatures);
  }
  GOOGLESQL_CHECK_OK(InsertCheckedFunction(functions, std::move(function), options));
}

// Returns true if the function was actually inserted.
template <typename FunctionSignatureListT>
static absl::StatusOr<bool> InsertFunctionImpl(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options,
    std::vector<std::string> name, Function::Mode mode,
    const FunctionSignatureListT& signature_list,
    FunctionOptions function_options) {
  if (FunctionIsDisabled(options, function_options)) {
    return false;
  }
  std::vector<FunctionSignature> signatures;
  signatures.reserve(signature_list.size());
  for (const auto& signature : signature_list) {
    if (FunctionSignatureIsDisabled(options, signature)) {
      continue;
    }
    signatures.emplace_back(ToFunctionSignature(signature));
  }
  if (signature_list.size() != 0) {
    // When a function has signatures but none of them are enabled via options,
    // then we do not insert the function into the map (it is as if the function
    // does not exist).
    if (signatures.empty()) {
      return false;
    }
  } else if (!options.include_function_ids.empty()) {
    // When a function is defined without signatures then we generally include
    // it.  This case is currently to support '$subscript', where we want to be
    // able to resolve the function and produce a custom error message when
    // someone tries to use the related but unsupported operator syntax (rather
    // than produce a confusing 'no existing function' error).
    //
    // Note, however, that we do not insert the function into the map if the
    // <include_function_ids> list is present.
    return false;
  }

  GOOGLESQL_RETURN_IF_ERROR(InsertCheckedFunction(
      functions,
      std::make_unique<Function>(
          std::move(name), Function::kGoogleSQLFunctionGroupName, mode,
          std::move(signatures), std::move(function_options)),
      options));
  return true;
}

void InsertFunction(NameToFunctionMap* functions,
                    const GoogleSQLBuiltinFunctionOptions& options,
                    absl::string_view name, Function::Mode mode,
                    const std::vector<FunctionSignatureOnHeap>& signatures,
                    FunctionOptions function_options) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);

  GOOGLESQL_CHECK_OK(InsertFunctionImpl(functions, options, std::move(names), mode,
                              signatures, function_options));
}

// Inserts the given function and types, if the enabled with the given options.
// Also inserts the given type, if at least one signature was added.
absl::Status InsertFunctionAndTypes(
    NameToFunctionMap* functions, NameToTypeMap* types,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode, const std::vector<FunctionSignatureOnHeap>& signatures,
    FunctionOptions function_options,
    std::vector<const Type*> types_to_insert) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);

  GOOGLESQL_ASSIGN_OR_RETURN(bool inserted,
                   InsertFunctionImpl(functions, options, std::move(names),
                                      mode, signatures, function_options));
  if (inserted) {
    for (const Type* type : types_to_insert) {
      GOOGLESQL_RETURN_IF_ERROR(InsertType(types, options, type));
    }
  }
  return absl::OkStatus();
}

// Note: This function is intentionally overloaded to prevent a default
// FunctionOptions object to be allocated on the callers stack.
void InsertFunction(NameToFunctionMap* functions,
                    const GoogleSQLBuiltinFunctionOptions& options,
                    absl::string_view name, Function::Mode mode,
                    const std::vector<FunctionSignatureOnHeap>& signatures) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);

  GOOGLESQL_CHECK_OK(InsertFunctionImpl(functions, options, std::move(names), mode,
                              signatures,
                              /* function_options=*/{}));
}

void InsertSimpleFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);

  GOOGLESQL_CHECK_OK(InsertFunctionImpl<std::initializer_list<FunctionSignatureProxy>>(
      functions, options, std::move(names), mode, signatures,
      /* function_options*/ {}));
}

void InsertSimpleFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures,
    const FunctionOptions& function_options) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);

  GOOGLESQL_CHECK_OK(InsertFunctionImpl<std::initializer_list<FunctionSignatureProxy>>(
      functions, options, std::move(names), mode, signatures,
      function_options));
}

void InsertNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    const std::vector<FunctionSignatureOnHeap>& signatures,
    FunctionOptions function_options) {
  std::vector<std::string> names;
  names.reserve(2);
  names.emplace_back(space);
  names.emplace_back(name);
  GOOGLESQL_CHECK_OK(InsertFunctionImpl(functions, options, std::move(names), mode,
                              signatures, std::move(function_options)));
}

void InsertSimpleNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures) {
  std::vector<std::string> names;
  names.reserve(2);
  names.emplace_back(space);
  names.emplace_back(name);
  GOOGLESQL_CHECK_OK(InsertFunctionImpl<std::initializer_list<FunctionSignatureProxy>>(
      functions, options, std::move(names), mode, signatures,
      /* function_options*/ {}));
}

void InsertSimpleNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures,
    FunctionOptions function_options) {
  std::vector<std::string> names;
  names.reserve(2);
  names.emplace_back(space);
  names.emplace_back(name);
  GOOGLESQL_CHECK_OK(InsertFunctionImpl<std::initializer_list<FunctionSignatureProxy>>(
      functions, options, std::move(names), mode, signatures,
      std::move(function_options)));
}

static absl::Status InsertCheckedTableValuedFunction(
    NameToTableValuedFunctionMap* table_valued_functions,
    std::unique_ptr<TableValuedFunction> table_valued_function,
    const GoogleSQLBuiltinFunctionOptions& options) {
  GOOGLESQL_RET_CHECK(table_valued_function->IsGoogleSQLBuiltin())
      << table_valued_function->Name() << " is not buitin TVF.";
  // Check that builtin TVFs don't have rewrite options.
  // BuiltinFunctionInlinerVisitor doesn't support inlining/rewriting TVFs.
  // Once that support is added this check can be removed.
  for (int idx = 0; idx < table_valued_function->NumSignatures(); ++idx) {
    const FunctionSignature* signature =
        table_valued_function->GetSignature(idx);
    GOOGLESQL_RET_CHECK(!signature->options().rewrite_options().has_value())
        << "Rewrite options are not supported for builtin TVFs.";
    GOOGLESQL_RET_CHECK(!options.rewrite_enabled.contains(
        static_cast<FunctionSignatureId>(signature->context_id())))
        << "Attempted to opt into a rewrite for the following TVF "
        << table_valued_function->Name()
        << " but rewrite options are not supported for builtin TVFs.";
  }
  std::string name =
      IdentifierPathToString(table_valued_function->function_name_path());
  GOOGLESQL_RET_CHECK(
      table_valued_functions->emplace(name, std::move(table_valued_function))
          .second)
      << name << "already exists";
  return absl::OkStatus();
}

// Returns true if the TVF is actually inserted.
template <typename FunctionSignatureListT>
static absl::StatusOr<bool> InsertTableValuedFunctionImpl(
    NameToTableValuedFunctionMap* table_valued_functions,
    const GoogleSQLBuiltinFunctionOptions& options,
    std::vector<std::string> name,
    const std::vector<FunctionSignatureOnHeap>& signatures,
    TableValuedFunctionOptions table_valued_function_options) {
  if (TableValuedFunctionIsDisabled(options, table_valued_function_options)) {
    return false;
  }
  std::vector<FunctionSignature> enabled_signatures;
  enabled_signatures.reserve(signatures.size());
  for (const auto& signature : signatures) {
    if (FunctionSignatureIsDisabled(options, signature)) {
      continue;
    }
    enabled_signatures.emplace_back(ToFunctionSignature(signature));
  }

  // When TVF doesn't have signatures or none of signatures are enabled via
  // options, then we do not insert the TVF into the map (it is as if the TVF
  // does not exist).
  if (enabled_signatures.empty()) {
    return false;
  }

  auto tvf = std::make_unique<TableValuedFunction>(
      std::move(name), Function::kGoogleSQLFunctionGroupName,
      std::move(enabled_signatures), std::move(table_valued_function_options));
  GOOGLESQL_RETURN_IF_ERROR(InsertCheckedTableValuedFunction(table_valued_functions,
                                                   std::move(tvf), options));
  return true;
}

absl::Status InsertSimpleTableValuedFunction(
    NameToTableValuedFunctionMap* table_valued_functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    const std::vector<FunctionSignatureOnHeap>& signatures,
    TableValuedFunctionOptions table_valued_function_options) {
  std::vector<std::string> names;
  names.reserve(1);
  names.emplace_back(name);
  return InsertTableValuedFunctionImpl<
             std::initializer_list<FunctionSignatureProxy>>(
             table_valued_functions, options, names, signatures,
             table_valued_function_options)
      .status();
}

absl::Status InsertType(NameToTypeMap* types,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        const Type* type) {
  if (!type->IsSupportedType(options.language_options)) {
    return absl::OkStatus();
  }

  auto [it, inserted] = types->try_emplace(
      type->ShortTypeName(options.language_options.product_mode()), type);
  if (!inserted) {
    const Type* existing_type = it->second;
    // Double insertion could be totally fine, but it must be the an equal
    // type.
    GOOGLESQL_RET_CHECK(existing_type->Equals(type));
  }
  return absl::OkStatus();
}

}  // namespace googlesql
