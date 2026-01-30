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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_STRING_FORMAT_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_STRING_FORMAT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"

namespace googlesql {
namespace functions {
namespace string_format_internal {

class StringFormatEvaluator;

// This class encapsulates all the metadata required to format a specific value
// according to one specifier in the format string.
struct FormatPart {
  using SetterFn = std::function<bool(StringFormatEvaluator*, const FormatPart&,
                                      absl::FormatArg*)>;

  // The index of the formatted argument (not the width or precision arguments)
  // into the vectors (e.g. arg_types_) initialized at construction or during
  // OnExecutionBegin. An negative part index indicates an escape specifier '%%'
  int64_t argument_index = -1;

  // This is the pattern string for formatting a single value.
  std::string util_format_pattern;

  // The specifier letter, e.g., 'i', 'f', 's' etc.
  char specifier_char;

  // The runtime variable for the value to be formatted.
  int64_t var_index = -1;
  // A function to convert from var to an input for absl::StrFormat.
  SetterFn set_arg;

  // If width is specified as an argument, rather than a constant, this will
  // be set to the variable supplying that argument.
  int64_t width_index = -1;
  // A function to convert from width_var to an input for absl::StrFormat.
  SetterFn set_width;

  // If precision is specified as an argument, rather than a constant, this will
  // be set to the variable supplying that argument.
  int64_t precision_index = -1;
  // A function to convert from precision_var to an input for absl::StrFormat.
  SetterFn set_precision;
};

// Wraps a string_view with type that will tell absl::StrFormat to use our
// custom UTF8 string conversion that measures string length according to UTF8
// code-points.
struct FormatGsqlString {
  absl::string_view view;
};

// Wraps a signed integer value with a type that will tell absl::StrFormat to
// use our custom formatting for hex and octal values. The template on this type
// also enables the grouping flag (which is otherwise not supported by
// absl::StrFormat).
template <bool GROUPING>
struct FormatGsqlInt64 {
  int64_t value;
};

// Wraps an unsigned integer value with a type that will tell absl::StrFormat to
// use our custom formatting for hex and octal values. The template on this
// type also enables the grouping flag (which is otherwise not supported by
// absl::StrFormat).
template <bool GROUPING>
struct FormatGsqlUint64 {
  uint64_t value;
};

// Wraps an double value with a type that will tell absl::StrFormat to use our
// custom formatting for double values with grouping characters.
template <bool GROUPING>
struct FormatGsqlDouble {
  double value;
};

// Wraps a Numeric or BigNumeric value with a type that will tell
// absl::StrFormat to use our custom formatting for numeric values with grouping
// characters (or not).
template <typename NumericType, bool GROUPING>
struct FormatGsqlNumeric {
  NumericType value;
};

// Implements the GoogleSQL string FORMAT function.
class StringFormatEvaluator {
 public:
  explicit StringFormatEvaluator(ProductMode product_mode,
                                 bool canonicalize_zero = false,
                                 bool use_external_float32 = false);
  StringFormatEvaluator(const StringFormatEvaluator&) = delete;
  StringFormatEvaluator& operator=(const StringFormatEvaluator&) = delete;

  // Set the input types. Lifetime of pointers in `types` and `factory` must
  // exceed the lifetime of evaluator.  `factory` is optional if proto types are
  // not specified. This should be called only once.
  // Performs type validation (in particular, for protos).
  absl::Status SetTypes(std::vector<const Type*> types,
                        google::protobuf::DynamicMessageFactory* factory);

  // Set the pattern.  This must be called after SetTypes. Validates that the
  // pattern matches the types provided in SetTypes.
  absl::Status SetPattern(absl::string_view new_pattern);

  // This must be called after SetPattern.  Validates that the types in
  // `values` match the types provided in SetTypes (via Type::Equals).
  absl::Status Format(absl::Span<const Value> values, std::string* output,
                      bool* is_null);

 private:
  const ProductMode product_mode_;
  const bool use_external_float32_;
  google::protobuf::DynamicMessageFactory* type_resolver_ = nullptr;

  std::string pattern_;
  // Two vectors that are the result of processing a pattern.

  // The format parts are each derived from a format specifier in the pattern
  // string -- in order.
  std::vector<FormatPart> format_parts_;
  // The raw parts are the string content of the pattern that comes before,
  // between, and after the format specifiers. The views are backed by
  // pattern_.
  std::vector<absl::string_view> raw_parts_;

  // The algebra types of the format value arguments. These are constant for
  // the lifetime of the query. We need specific type information here for each
  // column so that we can, for example, print the appropriate literal for
  // different proto/enum or even to differentiate STRING/BYTES. We also use
  // the types to print better error messages than we otherwise could.
  std::vector<const Type*> arg_types_;

  absl::Span<const Value> values_;

  // A buffer into which text protos and SQL literals are written while pending
  // further modification and eventual consumption by absl::StrFormat.
  absl::Cord cord_buffer_;
  // Used as a temporary buffer to back the view stored in fmt_string_;
  std::string string_buffer_;

  // absl::StrFormat::Arg takes these classes by reference instead of
  // by value. In opt builds, an rvalue string_view (such as what is returned
  // from buffer.data()) might get cleaned up before the format library uses it.
  // This member lets us cache the string_view so that its lifetime promises
  // to live until the value is converted.
  FormatGsqlString fmt_string_;
  FormatGsqlInt64<true> fmt_grouped_int_;
  FormatGsqlUint64<true> fmt_grouped_uint_;
  FormatGsqlDouble<true> fmt_grouped_double_;
  FormatGsqlNumeric<NumericValue, true> fmt_grouped_numeric_;
  FormatGsqlNumeric<BigNumericValue, true> fmt_grouped_bignumeric_;
  FormatGsqlInt64<false> fmt_int_;
  FormatGsqlUint64<false> fmt_uint_;
  FormatGsqlDouble<false> fmt_double_;
  FormatGsqlNumeric<NumericValue, false> fmt_numeric_;
  FormatGsqlNumeric<BigNumericValue, false> fmt_bignumeric_;

  // Status of function execution. It's initialized as absl::OkStatus() in
  // OnExecutionBegin. It's used for setting the error_reporter status.
  absl::Status status_;

  // If true, the sign on a signed zero is removed when converting numeric type
  // to string.
  // TODO : remove this flag when all engines have
  // rolled out this new behavior.
  const bool canonicalize_zero_ = false;

  // The number of arguments provided to the invocation of FORMAT.
  int64_t provided_arg_count() { return arg_types_.size(); }

  // Bulk of the work.
  absl::Status FormatString(absl::Span<const absl::string_view> raw_parts,
                            absl::Span<const FormatPart> format_parts,
                            absl::Cord* out, bool* set_null);

  absl::Status TypeError(int64_t index, absl::string_view expected,
                         const Type* actual) const;
  absl::Status ValueError(int64_t index, absl::string_view error) const;

  bool PrintProto(const Value& value, bool single_line, bool print_null,
                  bool quote, int64_t var_index);

  bool PrintJson(const Value& value, bool single_line, int64_t var_index);

  // Process and typecheck a new formatting pattern. If a pattern error is
  // discovered, an error is set in the error resolver and this function returns
  // false. Otherwise, format_parts_ and raw_parts_ are populated and this
  // function returns true.
  bool ProcessPattern();

  // Performs basic type validation. In particular, validates that if a proto
  // type is present, it is not a placeholder, and that type_resolver_ is set.
  bool ProcessType(const Type* arg_type);

  // As ProcessType, but for all types in arg_types_.
  void ProcessTypes();

  // Does nothing. Returned from factor methods after setting an error.
  bool NoopSetter(const FormatPart& part, absl::FormatArg* arg) {
    return false;
  }

  // Does dynamic dispatch based on type to set the right template of
  // 'CopyValueSetter' for 'index'. The dynamic dispatch occurs while processing
  // a format string, while the evaluation of template code occurs per row.
  FormatPart::SetterFn MakeCopyValueSetter(int64_t index);

  template <typename T>
  bool CopyValueSetter(const FormatPart& part, absl::FormatArg* arg);

  bool CopyStringSetter(const FormatPart& part, absl::FormatArg* arg);

  // Like MakeCopyValueSetter, but makes a setter with logic for bounds checking
  // with arguments.
  FormatPart::SetterFn MakeCopyWidthSetter(int64_t index);

  template <typename T>
  bool CopyWidthSetter(const FormatPart& part, absl::FormatArg* arg);

  // Like MakeCopyValueSetter, but makes a setter with logic for bounds checking
  // precision arguments.
  FormatPart::SetterFn MakeCopyPrecisionSetter(int64_t index);

  template <typename T>
  bool CopyPrecisionSetter(const FormatPart& part, absl::FormatArg* arg);

  // Like MakeCopyValueSetter, but uses a setter that trigger our custom
  // integer formatting extension for integers with grouping or hex formats.
  template <bool GROUPING>
  FormatPart::SetterFn MakeCopyIntCustom(int64_t index);

  template <typename T, bool GROUPING>
  bool CopyIntCustom(const FormatPart& part, absl::FormatArg* arg);

  template <typename T, bool GROUPING>
  bool CopyDoubleCustom(const FormatPart& part, absl::FormatArg* arg);

  template <typename T, bool GROUPING>
  bool CopyDoubleCanonicalizeZeroCustom(const FormatPart& part,
                                        absl::FormatArg* arg);

  template <bool GROUPING>
  bool CopyNumericCustom(const FormatPart& part, absl::FormatArg* arg);

  template <bool GROUPING>
  bool CopyBigNumericCustom(const FormatPart& part, absl::FormatArg* arg);

  // A setter function for specifiers %p and %P: printing proto message text
  // represenations.
  template <bool single_line, bool print_null, bool quote>
  bool PrintProtoSetter(const FormatPart& part, absl::FormatArg* arg);

  // A setter function for specifiers %p and %P: printing json text
  // represenations.
  template <bool single_line>
  bool PrintJsonSetter(const FormatPart& part, absl::FormatArg* arg);

  // Set the right template instantiation of 'ValueAsStringSetter' for 'index'.
  // These setters are specifically for the "%t" specifier which formats the
  // value like the CAST(value AS STRING) SQL operation.
  // 'MakeValueAsStringSetter' does dynamic dispatch while processing a format
  // string. The evaluation of 'ValueAsStringSetter' code occurs per row.
  FormatPart::SetterFn MakeValueAsStringSetter(int64_t index);

  // A setter function for specifier %t: print any scalar value as if it was
  // CAST to string.
  bool ValueAsStringSetter(const FormatPart& part, absl::FormatArg* arg);

  // Helper function to handle recursively printing. var_index is only used
  // for error messages.
  bool ValueAsString(const Value& value, int64_t var_index);

  // Set the right template instantiation of 'ValueAsStringSetter' for 'index'.
  // These setters are specifically for the "%T" specifier which formats the
  // value like a legal GoogleSQL literal of the same or wider type.
  // The evaluation of 'ValueAsStringSetter' code occurs per row.
  FormatPart::SetterFn MakeValueLiteralSetter(int64_t index);

  // A setter function for specifier %T: print any scalar value as a GoogleSQL
  // literal.
  bool ValueLiteralSetter(const FormatPart& part, absl::FormatArg* arg);

  // This family of type checking arguments is called while compiling the
  // format pattern.
  bool TypeCheckSIntArg(int64_t arg_index);
  bool TypeCheckUintArg(int64_t arg_index);
  bool TypeCheckIntOrUintArg(int64_t arg_index);
  bool TypeCheckDoubleOrNumericArg(int64_t arg_index);
  bool TypeCheckStringArg(int64_t arg_index);
  bool TypeCheckProtoOrJsonArg(int64_t arg_index);
};

}  // namespace string_format_internal

// Shorthand for doing FORMAT in one call.  `format_string` and STRING type
// values are checked for invalid UTF-8 sequences and will result in an error.
// Setting the optional parameter `use_external_float32` to true will return
// FLOAT32 as the type name for TYPE_FLOAT.
// TODO: Remove `use_external_float32` once all engines are
// updated.
absl::Status StringFormatUtf8(absl::string_view format_string,
                              absl::Span<const Value> values,
                              ProductMode product_mode, std::string* output,
                              bool* is_null, bool canonicalize_zero = false,
                              bool use_external_float32 = false);

absl::Status CheckStringFormatUtf8ArgumentTypes(absl::string_view format_string,
                                                std::vector<const Type*> types,
                                                ProductMode product_mode);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_STRING_FORMAT_H_
