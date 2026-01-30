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

#include "googlesql/public/functions/string_format.h"

#include <stddef.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/functions/convert_proto.h"
#include "googlesql/public/functions/format_max_output_width.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "unicode/utf8.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace string_format_internal {

// For floating point values it's necessary to cap the precision value to avoid
// arbitrary size memory allocations from the library we use to format them.
// The smallest possible double value is 2^-1074 (in binary format it's
// 0b0000000000000000000000000000000000000000000000000000000000000001), and it
// has 1074 precision digits after decimal point when formatted. Thus 1074 is
// enough for any IEEE 754 floating point values.
constexpr int kMaxPrecisionForFloatingPoint = 1074;
constexpr absl::string_view kMaxPrecisionForFloatingPointString = ".1074";

// Return capacity - used, clipped to a minimum of 0.
static size_t Excess(size_t used, size_t capacity) {
  return used < capacity ? capacity - used : 0;
}

// Check the precision value against limit for different specifiers.
static absl::Status ValidatePrecisionValue(int64_t precision, char specifier) {
  // Precision value for string, integer etc. uses the same limit as the whole
  // output string.
  int64_t cap = absl::GetFlag(FLAGS_googlesql_format_max_output_width);
  switch (specifier) {
    case 'e':
    case 'E':
    case 'f':
    case 'F':
      cap = kMaxPrecisionForFloatingPoint;
      break;
    case 'g':
    case 'G':
      // For 'g'/'G' the trailing zeros aren't printed. A large precision
      // value is OK.
      cap = std::numeric_limits<int32_t>::max();
      break;
    default:
      break;
  }
  if (precision > cap) {
    return googlesql_base::OutOfRangeErrorBuilder()
           << "Precision value is too big" << cap;
  }
  return absl::OkStatus();
}

bool StringFormatEvaluator::ValueAsString(const Value& value,
                                          int64_t var_index) {
  if (value.is_null()) {
    cord_buffer_.Append("NULL");
    return true;
  }
  switch (value.type_kind()) {
    case TYPE_INT32:
    case TYPE_INT64:
    case TYPE_UINT32:
    case TYPE_UINT64:
    case TYPE_BIGNUMERIC:
    case TYPE_NUMERIC:
    case TYPE_BOOL:
    case TYPE_DATE:
    case TYPE_TIMESTAMP:
    case TYPE_TIME:
    case TYPE_DATETIME:
    case TYPE_INTERVAL:
      cord_buffer_.Append(value.DebugString());
      break;
    case TYPE_ARRAY: {
      cord_buffer_.Append("[");
      bool first = true;
      for (const Value& field : value.elements()) {
        if (!first) {
          cord_buffer_.Append(", ");
        }
        if (!ValueAsString(field, var_index)) {
          return false;
        }
        first = false;
      }
      cord_buffer_.Append("]");
      break;
    }
    case TYPE_STRUCT: {
      cord_buffer_.Append("(");
      bool first = true;
      for (const Value& field : value.fields()) {
        if (!first) {
          cord_buffer_.Append(", ");
        }
        if (!ValueAsString(field, var_index)) {
          return false;
        }
        first = false;
      }
      cord_buffer_.Append(")");
      break;
    }
    case TYPE_FLOAT:
    case TYPE_DOUBLE: {
      if (!std::isfinite(value.ToDouble())) {
        cord_buffer_.Append(value.DebugString());
      } else {
        // This adds .0 for integral values, which we want, but adds
        // CASTs on NaNs, etc, which we don't want.
        // We always use external typing for some reason.
        cord_buffer_.Append(value.GetSQLLiteral(ProductMode::PRODUCT_EXTERNAL,
                                                use_external_float32_));
      }
      break;
    }
    case TYPE_STRING:
      if (!IsWellFormedUTF8(value.string_value())) {
        status_ = ValueError(var_index, "STRING value contains invalid UTF-8");
        return false;
      }
      cord_buffer_.Append(value.string_value());
      break;
    case TYPE_BYTES:
      cord_buffer_.Append(EscapeBytes(value.bytes_value()));
      break;
    case TYPE_ENUM:
      cord_buffer_.Append(value.EnumDisplayName());
      break;
    case TYPE_PROTO:
      if (!PrintProto(value, /*single_line=*/true, /*print_null=*/false,
                      /*quote=*/false, var_index)) {
        return false;
      }
      break;
    case TYPE_JSON:
      if (!PrintJson(value, /*single_line=*/true, var_index)) {
        return false;
      }
      break;
    default:
      status_.Update(googlesql_base::InternalErrorBuilder()
                     << "No support for type while: "
                     << TypeKind_Name(value.type_kind()));
      return false;
  }
  return true;
}

StringFormatEvaluator::StringFormatEvaluator(ProductMode product_mode,
                                             bool canonicalize_zero,
                                             bool use_external_float32)
    : product_mode_(product_mode),
      use_external_float32_(use_external_float32),
      type_resolver_(nullptr),
      status_(),
      canonicalize_zero_(canonicalize_zero) {}

absl::Status StringFormatEvaluator::SetPattern(absl::string_view pattern) {
  if (pattern_.empty() || pattern != pattern_) {
    pattern_ = std::string(pattern);
    ProcessPattern();
  }
  return status_;
}

absl::Status StringFormatEvaluator::SetTypes(
    std::vector<const Type*> arg_types,
    google::protobuf::DynamicMessageFactory* factory) {
  arg_types_ = std::move(arg_types);
  type_resolver_ = factory;
  ProcessTypes();
  return status_;
}

bool StringFormatEvaluator::ProcessType(const Type* arg_type) {
  if (arg_type->IsArray()) {
    if (!ProcessType(arg_type->AsArray()->element_type())) {
      return false;
    }
  } else if (arg_type->IsStruct()) {
    for (const StructField& field : arg_type->AsStruct()->fields()) {
      if (!ProcessType(field.type)) {
        return false;
      }
    }
  } else if (arg_type->IsProto()) {
    const ProtoType* proto_type = arg_type->AsProto();
    const google::protobuf::Descriptor* descriptor = proto_type->descriptor();

    if (type_resolver_ == nullptr) {
      status_ =
          absl::Status(absl::StatusCode::kInternal, "Type Resolver Not Set ");
      return false;
    }
    const google::protobuf::Message* prototype = type_resolver_->GetPrototype(descriptor);
    if (prototype == nullptr) {
      status_ = absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("Cannot find type information for", descriptor->name()));
      return false;
    }
    if (descriptor->is_placeholder()) {
      status_ = absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("Cannot format proto with placeholder descriptor ",
                       descriptor->name()));
      return false;
    }
  } else if (arg_type->IsGraphElement()) {
    status_ =
        absl::Status(absl::StatusCode::kUnimplemented,
                     absl::StrCat("Cannot format type ",
                                  arg_type->ShortTypeName(product_mode_)));
    return false;
  }
  return true;
}

void StringFormatEvaluator::ProcessTypes() {
  for (int i = 0; i < arg_types_.size(); ++i) {
    if (!ProcessType(arg_types_[i])) {
      return;
    }
  }
}

absl::Status StringFormatEvaluator::Format(absl::Span<const Value> values,
                                           std::string* output, bool* is_null) {
  output->clear();
  *is_null = false;

  if (!status_.ok()) {
    return status_;
  }

  values_ = std::move(values);
  GOOGLESQL_RET_CHECK_EQ(values_.size(), arg_types_.size());
  for (int i = 0; i < values_.size(); ++i) {
    if (!values_[i].type()->Equals(arg_types_[i])) {
      return googlesql_base::InternalErrorBuilder()
             << "Expected type does not match value type for argument " << i + 2
             << ". expected type: " << arg_types_[i]->DebugString()
             << " value type: " << values_[i].type()->DebugString();
    }
  }

  absl::Cord out;
  absl::Status status = FormatString(raw_parts_, format_parts_, &out, is_null);
  status_ = absl::OkStatus();
  if (status.ok() && !(*is_null)) {
    *output = std::string(out);
  }
  return status;
}

absl::Status StringFormatEvaluator::FormatString(
    absl::Span<const absl::string_view> raw_parts,
    absl::Span<const FormatPart> format_parts, absl::Cord* out,
    bool* set_null) {
  GOOGLESQL_DCHECK_OK(status_);
  ABSL_DCHECK_GE(raw_parts.size(), format_parts.size());
  for (int i = 0; i < format_parts.size(); ++i) {
    if (out->size() > absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
      status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                     << "Output string too long while evaluating FORMAT; limit "
                     << absl::GetFlag(FLAGS_googlesql_format_max_output_width));
      return status_;
    }
    out->Append(raw_parts[i]);
    const FormatPart& part = format_parts[i];
    if (part.argument_index < 0) {
      out->Append("%");
      continue;
    }

    size_t num_args = 0;
    bool is_null = false;
    std::vector<absl::FormatArg> args(3, absl::FormatArg(""));

    if (part.width_index != -1) {
      is_null = is_null || !part.set_width(this, part, &args[num_args++]);
    }
    if (part.precision_index != -1) {
      is_null = is_null || !part.set_precision(this, part, &args[num_args++]);
    }
    is_null = is_null || !part.set_arg(this, part, &args[num_args++]);

    // Error takes precedence over NULL.
    if (!status_.ok()) {
      return status_;
    }

    if (is_null) {
      *set_null = true;
      return absl::OkStatus();
    }
    if (!absl::FormatUntyped(
            out, absl::UntypedFormatSpec(part.util_format_pattern), args)) {
      status_.Update(googlesql_base::InternalErrorBuilder()
                     << "Failure in absl::StrFormat.");
      return status_;
    }
  }
  if (raw_parts.size() > format_parts.size()) {
    ABSL_DCHECK_EQ(raw_parts.size(), format_parts.size() + 1);
    out->Append(raw_parts.back());
  }
  if (out->size() > absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
    status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                   << "Output string too long while evaluating FORMAT; limit "
                   << absl::GetFlag(FLAGS_googlesql_format_max_output_width));
  }
  *set_null = false;
  return status_;
}

FormatPart::SetterFn StringFormatEvaluator::MakeCopyValueSetter(int64_t index) {
  switch (arg_types_[index]->kind()) {
    case TYPE_INT32:
      return &StringFormatEvaluator::CopyValueSetter<int32_t>;
    case TYPE_INT64:
      return &StringFormatEvaluator::CopyValueSetter<int64_t>;
    case TYPE_UINT32:
      return &StringFormatEvaluator::CopyValueSetter<uint32_t>;
    case TYPE_UINT64:
      return &StringFormatEvaluator::CopyValueSetter<uint64_t>;
    case TYPE_DOUBLE:
      return &StringFormatEvaluator::CopyValueSetter<double>;
    case TYPE_FLOAT:
      return &StringFormatEvaluator::CopyValueSetter<float>;
    case TYPE_BOOL:
      return &StringFormatEvaluator::CopyValueSetter<bool>;
    case TYPE_STRING:
      return &StringFormatEvaluator::CopyStringSetter;
    default:
      status_.Update(googlesql_base::InternalErrorBuilder()
                     << "Invalid type for MakeCopyValueSetter: "
                     << TypeKind_Name(arg_types_[index]->kind()));
      return &StringFormatEvaluator::NoopSetter;
  }
}

bool StringFormatEvaluator::ValueAsStringSetter(const FormatPart& part,
                                                absl::FormatArg* arg) {
  const Value* value_var = &values_[part.var_index];
  if (value_var->is_null()) {
    *arg = absl::FormatArg("NULL");
    return true;
  }
  cord_buffer_.Clear();
  if (!ValueAsString(*value_var, part.var_index)) {
    return false;
  }
  absl::CopyCordToString(cord_buffer_, &string_buffer_);
  // Note, we do _not_ check for invalid UTF8 here, because we perform this
  // check inside ValueAsString.
  fmt_string_.view = string_buffer_;
  *arg = absl::FormatArg(fmt_string_);
  return true;
}

bool StringFormatEvaluator::ValueLiteralSetter(const FormatPart& part,
                                               absl::FormatArg* arg) {
  const Value* value_var = &values_[part.var_index];
  if (value_var->is_null()) {
    *arg = absl::FormatArg("NULL");
    return true;
  }
  // GetSQLLiteral() for JSON value can return an invalid JSON SQL literal
  // if the original JSON value is invalid.
  if (value_var->type_kind() == TYPE_JSON && value_var->is_unparsed_json()) {
    absl::StatusOr<JSONValue> json =
        JSONValue::ParseJSONString(value_var->json_value_unparsed());
    if (!json.ok()) {
      status_ = ValueError(part.var_index, json.status().message());
      return false;
    }
  }

  // FLOAT/DOUBLE always use FLOAT32/FLOAT64 in Format function.
  string_buffer_ = value_var->GetSQLLiteral(ProductMode::PRODUCT_EXTERNAL,
                                            /*use_external_float32=*/true);
  fmt_string_.view = string_buffer_;
  // Check for invalid UTF8.
  // This is necessary because while engines are required to validate utf-8,
  // They do not always do so (also, protos can contain invalid utf-8)
  if (!IsWellFormedUTF8(fmt_string_.view)) {
    status_ = ValueError(part.var_index, "STRING value contains invalid UTF-8");
    return false;
  }
  *arg = absl::FormatArg(fmt_string_);
  return true;
}

template <typename T>
bool StringFormatEvaluator::CopyValueSetter(const FormatPart& part,
                                            absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];
  if (value->is_null()) {
    return false;
  }
  auto value_var = value->template Get<T>();
  *arg = absl::FormatArg(value_var);
  return true;
}

bool StringFormatEvaluator::CopyStringSetter(const FormatPart& part,
                                             absl::FormatArg* arg) {
  const Value* value_var = &values_[part.var_index];
  if (value_var->is_null()) {
    return false;
  }
  // Grab the content to the buffer for UTF8 operations.
  // There may be a '\0' embedded in the string.  We truncate the string in this
  // case to match previous behavior. To accomplish this truncation we use
  // std::string.c_str(). This has a performance penalty (needing to
  // search for the '\0', which is almost never there).
  // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
  fmt_string_.view = value_var->string_value().c_str();
  // Check for invalid UTF8.
  // This is necessary because while engines are required to validate utf-8,
  // They do not always do so.
  if (!IsWellFormedUTF8(fmt_string_.view)) {
    status_.Update(
        ValueError(part.var_index, "STRING value contains invalid UTF-8"));
    return false;
  }
  *arg = absl::FormatArg(fmt_string_);
  return true;
}

FormatPart::SetterFn StringFormatEvaluator::MakeCopyWidthSetter(int64_t index) {
  switch (arg_types_[index]->kind()) {
    case TYPE_INT32:
      return &StringFormatEvaluator::CopyWidthSetter<int32_t>;
    case TYPE_INT64:
      return &StringFormatEvaluator::CopyWidthSetter<int64_t>;
    default:
      status_.Update(googlesql_base::InternalErrorBuilder()
                     << "Invalid type for MakeCopyWidthSetter: "
                     << TypeKind_Name(arg_types_[index]->kind()));
      return &StringFormatEvaluator::NoopSetter;
  }
}

template <typename T>
bool StringFormatEvaluator::CopyWidthSetter(const FormatPart& part,
                                            absl::FormatArg* arg) {
  const Value* value = &values_[part.width_index];
  if (value->is_null()) {
    return false;  // NULL
  }

  T width_value = value->template Get<T>();
  if (width_value > absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
    status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                   << "Output string too long while evaluating FORMAT; limit "
                   << absl::GetFlag(FLAGS_googlesql_format_max_output_width));
    return false;
  }
  *arg = absl::FormatArg(width_value);
  return true;
}

FormatPart::SetterFn StringFormatEvaluator::MakeCopyPrecisionSetter(
    int64_t index) {
  switch (arg_types_[index]->kind()) {
    case TYPE_INT32:
      return &StringFormatEvaluator::CopyPrecisionSetter<int32_t>;
    case TYPE_INT64:
      return &StringFormatEvaluator::CopyPrecisionSetter<int64_t>;
    default:
      status_.Update(absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("Invalid type for MakeCopyPrecisionSetter: ",
                       TypeKind_Name(arg_types_[index]->kind()))));
      return &StringFormatEvaluator::NoopSetter;
  }
}

template <typename T>
bool StringFormatEvaluator::CopyPrecisionSetter(const FormatPart& part,
                                                absl::FormatArg* arg) {
  const Value* value = &values_[part.precision_index];
  ABSL_CHECK_NE(value, nullptr);

  if (value->is_null()) {
    return false;  // NULL
  }
  T precision_value = value->template Get<T>();
  status_.Update(ValidatePrecisionValue(precision_value, part.specifier_char));
  if (!status_.ok()) {
    return false;
  }
  if (part.specifier_char == 'g' || part.specifier_char == 'G') {
    // For 'g' or 'G' a large precision value is accepted, but we cap it to
    // kMaxPrecisionForFloatingPoint and it shouldn't affect the final result.
    if (precision_value > kMaxPrecisionForFloatingPoint) {
      precision_value = kMaxPrecisionForFloatingPoint;
    }
  }
  *arg = absl::FormatArg(precision_value);
  return true;
}

template <bool GROUPING>
FormatPart::SetterFn StringFormatEvaluator::MakeCopyIntCustom(int64_t index) {
  switch (arg_types_[index]->kind()) {
    case TYPE_INT32:
      return &StringFormatEvaluator::CopyIntCustom<int32_t, GROUPING>;
    case TYPE_INT64:
      return &StringFormatEvaluator::CopyIntCustom<int64_t, GROUPING>;
    case TYPE_UINT32:
      return &StringFormatEvaluator::CopyIntCustom<uint32_t, GROUPING>;
    case TYPE_UINT64:
      return &StringFormatEvaluator::CopyIntCustom<uint64_t, GROUPING>;
    case TYPE_DOUBLE:
      if (canonicalize_zero_) {
        return &StringFormatEvaluator::CopyDoubleCanonicalizeZeroCustom<
            double, GROUPING>;
      } else {
        return &StringFormatEvaluator::CopyDoubleCustom<double, GROUPING>;
      }
    case TYPE_FLOAT:
      if (canonicalize_zero_) {
        return &StringFormatEvaluator::CopyDoubleCanonicalizeZeroCustom<
            float, GROUPING>;
      } else {
        return &StringFormatEvaluator::CopyDoubleCustom<float, GROUPING>;
      }
    case TYPE_NUMERIC:
      return &StringFormatEvaluator::CopyNumericCustom<GROUPING>;
    case TYPE_BIGNUMERIC:
      return &StringFormatEvaluator::CopyBigNumericCustom<GROUPING>;
    default:
      status_.Update(
          absl::Status(absl::StatusCode::kInternal,
                       absl::StrCat("Invalid type for MakeCopyIntCustom: ",
                                    TypeKind_Name(arg_types_[index]->kind()))));
      return &StringFormatEvaluator::NoopSetter;
  }
}

template <typename T, bool GROUPING>
bool StringFormatEvaluator::CopyIntCustom(const FormatPart& part,
                                          absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];
  if (value->is_null()) {
    return false;  // NULL
  }
  T value_data = value->template Get<T>();
  if (GROUPING && std::is_signed<T>()) {
    fmt_grouped_int_.value = static_cast<int64_t>(value_data);
    *arg = absl::FormatArg(fmt_grouped_int_);
  } else if (std::is_signed<T>()) {
    fmt_int_.value = static_cast<int64_t>(value_data);
    *arg = absl::FormatArg(fmt_int_);
  } else if (GROUPING) {
    fmt_grouped_uint_.value = static_cast<uint64_t>(value_data);
    *arg = absl::FormatArg(fmt_grouped_uint_);
  } else {
    fmt_uint_.value = static_cast<uint64_t>(value_data);
    *arg = absl::FormatArg(fmt_uint_);
  }
  return true;
}

template <typename T, bool GROUPING>
bool StringFormatEvaluator::CopyDoubleCanonicalizeZeroCustom(
    const FormatPart& part, absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];

  if (value->is_null()) {
    return false;  // NULL
  }
  T value_data = value->template Get<T>();
  if (value_data == -0.0) {
    value_data = 0.0;
  }
  if (std::isnan(value_data)) {
    value_data = std::numeric_limits<T>::quiet_NaN();
  }
  if (GROUPING) {
    fmt_grouped_double_.value = static_cast<double>(value_data);
    *arg = absl::FormatArg(fmt_grouped_double_);
  } else {
    fmt_double_.value = static_cast<double>(value_data);
    *arg = absl::FormatArg(fmt_double_);
  }
  return true;
}

template <typename T, bool GROUPING>
bool StringFormatEvaluator::CopyDoubleCustom(const FormatPart& part,
                                             absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];

  if (value->is_null()) {
    return false;  // NULL
  }
  T value_data = value->template Get<T>();
  if (GROUPING) {
    fmt_grouped_double_.value = static_cast<double>(value_data);
    *arg = absl::FormatArg(fmt_grouped_double_);
  } else {
    fmt_double_.value = static_cast<double>(value_data);
    *arg = absl::FormatArg(fmt_double_);
  }
  return true;
}

template <bool GROUPING>
bool StringFormatEvaluator::CopyNumericCustom(const FormatPart& part,
                                              absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];

  if (value->is_null()) {
    return false;  // NULL
  }
  if (GROUPING) {
    fmt_grouped_numeric_.value = value->numeric_value();
    *arg = absl::FormatArg(fmt_grouped_numeric_);
  } else {
    fmt_numeric_.value = value->numeric_value();
    *arg = absl::FormatArg(fmt_numeric_);
  }
  return true;
}

template <bool GROUPING>
bool StringFormatEvaluator::CopyBigNumericCustom(const FormatPart& part,
                                                 absl::FormatArg* arg) {
  const Value* value = &values_[part.var_index];

  if (value->is_null()) {
    return false;  // NULL
  }
  if (GROUPING) {
    fmt_grouped_bignumeric_.value = value->bignumeric_value();
    *arg = absl::FormatArg(fmt_grouped_bignumeric_);
  } else {
    fmt_bignumeric_.value = value->bignumeric_value();
    *arg = absl::FormatArg(fmt_bignumeric_);
  }
  return true;
}

bool StringFormatEvaluator::PrintProto(
    const Value& value, bool single_line,
    bool print_null,  // if false and value.is_null(), this returns false.
    bool quote, int64_t var_index) {
  if (value.is_null()) {
    if (print_null) {
      cord_buffer_.Append("NULL");
      return true;
    }
    return false;
  }
  if (type_resolver_ == nullptr) {
    status_.Update(
        absl::Status(absl::StatusCode::kInternal, "Type Resolver Not Set "));
    return false;
  }

  std::unique_ptr<google::protobuf::Message> message(value.ToMessage(type_resolver_));
  absl::Status status;
  absl::Cord out;
  if (single_line) {
    if (!ProtoToString(message.get(), &out, &status)) {
      // This branch is not expected, but try to return something.
      out = message->ShortDebugString();
    }
  } else {
    if (!ProtoToMultilineString(message.get(), &out, &status)) {
      // This branch is not expected, but try to return something.
      out = message->DebugString();
    }
  }
  // TODO: Don't convert cord->string twice (here and to the call
  // to AbslFormatConvert).
  if (!IsWellFormedUTF8(std::string(out))) {
    status_ = ValueError(var_index, "PROTO value contains invalid UTF-8");
    return false;
  }

  if (quote) {
    cord_buffer_.Append(ToStringLiteral(std::string(out)));
  } else {
    cord_buffer_.Append(out);
  }
  return true;
}

template <bool single_line, bool print_null, bool quote>
bool StringFormatEvaluator::PrintProtoSetter(const FormatPart& part,
                                             absl::FormatArg* arg) {
  const Value& value = values_[part.var_index];
  cord_buffer_.Clear();
  if (!PrintProto(value, single_line, print_null, quote, part.var_index)) {
    return false;
  }
  absl::CopyCordToString(cord_buffer_, &string_buffer_);
  fmt_string_.view = string_buffer_;
  *arg = absl::FormatArg(fmt_string_);
  return true;
}

bool StringFormatEvaluator::PrintJson(const Value& value, bool single_line,
                                      int64_t var_index) {
  if (value.is_null()) {
    return false;
  }

  std::string out;
  if (value.is_validated_json()) {
    JSONValueConstRef json_ref = value.json_value();
    out = single_line ? json_ref.ToString() : json_ref.Format();
  } else {
    absl::StatusOr<JSONValue> json =
        JSONValue::ParseJSONString(value.json_value_unparsed());
    if (!json.ok()) {
      status_ = ValueError(var_index, json.status().message());
      return false;
    }
    out = single_line ? json->GetConstRef().ToString()
                      : json->GetConstRef().Format();
  }

  if (!IsWellFormedUTF8(out)) {
    status_ = ValueError(var_index, "JSON value contains invalid UTF-8");
    return false;
  }

  cord_buffer_.Append(absl::Cord(out));
  return true;
}

template <bool single_line>
bool StringFormatEvaluator::PrintJsonSetter(const FormatPart& part,
                                            absl::FormatArg* arg) {
  const Value& value = values_[part.var_index];
  cord_buffer_.Clear();
  if (!PrintJson(value, single_line, part.var_index)) {
    return false;
  }
  absl::CopyCordToString(cord_buffer_, &string_buffer_);
  fmt_string_.view = string_buffer_;
  *arg = absl::FormatArg(fmt_string_);
  return true;
}

FormatPart::SetterFn StringFormatEvaluator::MakeValueAsStringSetter(
    int64_t index) {
  const Type* t = arg_types_[index];
  switch (t->kind()) {
    case TYPE_INT32:
    case TYPE_UINT32:
    case TYPE_INT64:
    case TYPE_UINT64:
    case TYPE_BOOL:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_STRING:
    case TYPE_BYTES:
    case TYPE_DATE:
    case TYPE_TIME:
    case TYPE_TIMESTAMP:
    case TYPE_ENUM:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_PROTO:
    case TYPE_DATETIME:
    case TYPE_INTERVAL:
    case TYPE_BIGNUMERIC:
    case TYPE_NUMERIC:
    case TYPE_JSON:
      return &StringFormatEvaluator::ValueAsStringSetter;
    default:
      break;
  }

  status_.Update(
      absl::Status(absl::StatusCode::kInternal,
                   absl::StrCat("Invalid type for MakeValueAsStringSetter: ",
                                t->DebugString())));
  return &StringFormatEvaluator::NoopSetter;
}

FormatPart::SetterFn StringFormatEvaluator::MakeValueLiteralSetter(
    int64_t index) {
  const Type* t = arg_types_[index];
  switch (t->kind()) {
    case TYPE_INT32:
    case TYPE_UINT32:
    case TYPE_INT64:
    case TYPE_UINT64:
    case TYPE_BOOL:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_STRING:
    case TYPE_BYTES:
    case TYPE_DATE:
    case TYPE_TIME:
    case TYPE_TIMESTAMP:
    case TYPE_ENUM:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_PROTO:
    case TYPE_DATETIME:
    case TYPE_INTERVAL:
    case TYPE_BIGNUMERIC:
    case TYPE_NUMERIC:
    case TYPE_JSON:
      return &StringFormatEvaluator::ValueLiteralSetter;

    default:
      break;
  }

  status_.Update(
      absl::Status(absl::StatusCode::kInternal,
                   absl::StrCat("Invalid type for MakeValueLiteralSetter: ",
                                t->DebugString())));
  return &StringFormatEvaluator::NoopSetter;
}

bool StringFormatEvaluator::ProcessPattern() {
  if (pattern_.size() >
      absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
    status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                   << "Output string too long while evaluating FORMAT; limit "
                   << absl::GetFlag(FLAGS_googlesql_format_max_output_width));
    return false;
  }
  if (!IsWellFormedUTF8(pattern_)) {
    status_.Update(
        googlesql_base::OutOfRangeErrorBuilder()
        << "Format string contains invalid UTF-8 while evaluating FORMAT");
    return false;
  }

  // Clear the string content so we reuse the allocated memory (unless the new
  // format string is longer than the old).
  format_parts_.clear();
  raw_parts_.clear();

  absl::string_view suffix = pattern_;

  // Regexp lifted from private code in googlesql/public/functions/format.cc.
  // This regex parses "%[flags][width][.precision]specifier".
  static const LazyRE2 pattern_regex{
      "%"
      "([-+ #'0]*)"           // flags
      "(\\*|[0-9]*)"          // width
      "(\\.(?:\\*|[0-9]*))?"  // precision
      "(.)"};                 // specifier
  // To be used for regexp captures.
  absl::string_view flags, width, precision, specifier;

  int64_t next_arg = 0;
  absl::string_view::size_type marker_psn;

  while (!suffix.empty()) {
    marker_psn = suffix.find('%');
    // There are some non-marker bytes before the first marker. Copy to the
    // absl::StrFormat pattern and advance the absl::string_view.
    if (marker_psn != 0) {
      raw_parts_.push_back(suffix.substr(0, marker_psn));
      if (marker_psn == absl::string_view::npos) {
        break;
      }
      suffix.remove_prefix(marker_psn);
    } else {
      raw_parts_.emplace_back("");
    }

    if (!RE2::Consume(&suffix, *pattern_regex, &flags, &width, &precision,
                      &specifier)) {
      status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                     << "Invalid FORMAT string: \"" << pattern_ << "\"");
      break;
    }

    bool grouping = absl::StrContains(flags, '\'');
    std::string stripped_flags;
    if (grouping) {
      for (char c : flags) {
        if (c != '\'') {
          stripped_flags.push_back(c);
        }
      }
      flags = stripped_flags;
    }

    format_parts_.emplace_back();
    FormatPart& part = format_parts_.back();

    char spec_char = specifier[0];
    if (spec_char == '%') {  // This can indicate the escape sequence %%.
      if (!(flags.empty() && width.empty() && precision.empty())) {
        status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                       << "Invalid FORMAT string: \"" << pattern_ << "\"");
        break;
      }
      continue;
    }

    if (width == "*") {
      if (next_arg < provided_arg_count()) {
        TypeCheckSIntArg(next_arg);
        part.set_width = MakeCopyWidthSetter(next_arg);
        part.width_index = next_arg;
      }
      next_arg++;
    } else if (!width.empty()) {
      int64_t width_value;
      if (!absl::SimpleAtoi(width, &width_value) ||
          width_value >
              absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
        status_.Update(
            googlesql_base::OutOfRangeErrorBuilder()
            << "Width " << width_value
            << " exceeds the maximum supported output width of FORMAT. Limit: "
            << absl::GetFlag(FLAGS_googlesql_format_max_output_width));
        break;
      }
    }

    if (precision == ".*") {
      if (next_arg < provided_arg_count()) {
        TypeCheckSIntArg(next_arg);
        part.set_precision = MakeCopyPrecisionSetter(next_arg);
        part.precision_index = next_arg;
      }
      next_arg++;
    } else if (precision.size() > 1) {
      // Note: "." is a legal precision specifier.
      int64_t precision_value;
      if (!absl::SimpleAtoi(absl::ClippedSubstr(precision, 1),
                            &precision_value)) {
        // Overflowing int64.
        precision_value = std::numeric_limits<int64_t>::max();
      }
      status_.Update(ValidatePrecisionValue(precision_value, spec_char));
      if (spec_char == 'g' || spec_char == 'G') {
        // For 'g' or 'G' a large precision value is accepted, but we cap it to
        // kMaxPrecisionForFloatingPoint and it shouldn't affect the final
        // result.
        if (precision_value > kMaxPrecisionForFloatingPoint) {
          precision = kMaxPrecisionForFloatingPointString;
        }
      }
    }

    if (next_arg < provided_arg_count()) {
      switch (spec_char) {
        case 'd':
        case 'i':
          TypeCheckIntOrUintArg(next_arg);
          if (grouping) {
            part.set_arg = MakeCopyIntCustom</*grouping=*/true>(next_arg);
          } else {
            part.set_arg = MakeCopyValueSetter(next_arg);
          }
          break;
        case 'u':
          TypeCheckUintArg(next_arg);
          if (grouping) {
            part.set_arg = MakeCopyIntCustom</*grouping=*/true>(next_arg);
          } else {
            part.set_arg = MakeCopyValueSetter(next_arg);
          }
          break;
        case 'o':
        case 'x':
        case 'X':
          TypeCheckIntOrUintArg(next_arg);
          if (grouping) {
            part.set_arg = MakeCopyIntCustom</*grouping=*/true>(next_arg);
          } else {
            part.set_arg = MakeCopyIntCustom</*grouping=*/false>(next_arg);
          }
          break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
          TypeCheckDoubleOrNumericArg(next_arg);
          if (grouping) {
            part.set_arg = MakeCopyIntCustom</*grouping=*/true>(next_arg);
          } else {
            part.set_arg = MakeCopyIntCustom</*grouping=*/false>(next_arg);
          }
          break;
        case 'g':
        case 'G':
          TypeCheckDoubleOrNumericArg(next_arg);
          if (grouping) {
            part.set_arg = MakeCopyIntCustom</*grouping=*/true>(next_arg);
          } else {
            part.set_arg = MakeCopyIntCustom</*grouping=*/false>(next_arg);
          }
          break;
        case 's':
          TypeCheckStringArg(next_arg);
          part.set_arg = MakeCopyValueSetter(next_arg);
          break;
        case 'p':
        case 'P':
          TypeCheckProtoOrJsonArg(next_arg);
          if (arg_types_[next_arg]->kind() == TYPE_PROTO) {
            if (spec_char == 'p') {
              part.set_arg = &StringFormatEvaluator::PrintProtoSetter<
                  /*single_line=*/true, /*print_null=*/false,
                  /*quote=*/false>;
            } else {
              part.set_arg = &StringFormatEvaluator::PrintProtoSetter<
                  /*single_line=*/false, /*print_null=*/false, /*quote=*/false>;
            }
          } else {
            // This is a JSON.
            if (spec_char == 'p') {
              part.set_arg = &StringFormatEvaluator::PrintJsonSetter<
                  /*single_line=*/true>;
            } else {
              part.set_arg = &StringFormatEvaluator::PrintJsonSetter<
                  /*single_line=*/false>;
            }
          }
          spec_char = 's';  // Pass it as a string to absl::StrFormat
          break;
        case 't':
          part.set_arg = MakeValueAsStringSetter(next_arg);
          spec_char = 's';  // Pass it as a string to absl::StrFormat
          break;
        case 'T':
          part.set_arg = MakeValueLiteralSetter(next_arg);
          spec_char = 's';  // Pass it as a string to absl::StrFormat
          break;
        default:
          status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                         << "Invalid format specifier character \"" << specifier
                         << "\" in FORMAT string: \"" << pattern_ << "\"");
      }
      if (!status_.ok()) {
        break;
      }
      part.specifier_char = spec_char;
      part.util_format_pattern = absl::StrCat("%", flags, width, precision,
                                              absl::string_view(&spec_char, 1));
      part.argument_index = next_arg;
      part.var_index = next_arg;
    }
    next_arg++;
  }

  if (next_arg != provided_arg_count()) {
    if (next_arg > provided_arg_count()) {
      status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                     << "Too few arguments to FORMAT for pattern \"" << pattern_
                     << "\"; Expected " << next_arg + 1 << "; Got "
                     << provided_arg_count() + 1);
    } else {
      status_.Update(googlesql_base::OutOfRangeErrorBuilder()
                     << "Too many arguments to FORMAT for pattern \""
                     << pattern_ << "\"; Expected " << next_arg + 1 << "; Got "
                     << provided_arg_count() + 1);
    }
  }

  return status_.ok();
}

absl::Status StringFormatEvaluator::TypeError(int64_t arg_index,
                                              absl::string_view expected,
                                              const Type* actual) const {
  // Add 2 to arg index; FORMAT(pattern, argN).
  //   +1 because we're expecting 'pattern' to be the first arg
  //   +1 because we report in 1-index arrays, vs 0-index vectors.
  return googlesql_base::OutOfRangeErrorBuilder()
         << "Invalid type for argument " << arg_index + 2
         << " to FORMAT; Expected " << expected << "; Got "
         << actual->ShortTypeName(product_mode_);
}

absl::Status StringFormatEvaluator::ValueError(int64_t arg_index,
                                               absl::string_view error) const {
  // Add 2 to arg index; FORMAT(pattern, argN).
  //   +1 because we're expecting 'pattern' to be the first arg
  //   +1 because we report in 1-index arrays, vs 0-index vectors.
  return googlesql_base::OutOfRangeErrorBuilder()
         << "Invalid value for argument " << arg_index + 2 << " to FORMAT; "
         << error;
}

bool StringFormatEvaluator::TypeCheckSIntArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  const Type* t = arg_types_[arg_index];
  if (!t->IsSignedInteger()) {
    status_.Update(TypeError(arg_index, "integer", arg_types_[arg_index]));
    return false;
  }
  return true;
}

bool StringFormatEvaluator::TypeCheckUintArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  auto t = arg_types_[arg_index];
  if (!t->IsUnsignedInteger()) {
    status_.Update(TypeError(arg_index, "UINT", arg_types_[arg_index]));
    return false;
  }
  return true;
}

bool StringFormatEvaluator::TypeCheckIntOrUintArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  const Type* t = arg_types_[arg_index];
  if (!t->IsInteger()) {
    status_.Update(TypeError(arg_index, "integer", arg_types_[arg_index]));
    return false;
  }
  return true;
}

bool StringFormatEvaluator::TypeCheckDoubleOrNumericArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  const Type* t = arg_types_[arg_index];
  if (!t->IsFloatingPoint() && !t->IsNumericType() && !t->IsBigNumericType()) {
    status_.Update(TypeError(
        arg_index, googlesql::types::DoubleType()->TypeName(product_mode_), t));
    return false;
  }
  return true;
}

bool StringFormatEvaluator::TypeCheckStringArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  const Type* t = arg_types_[arg_index];
  if (!t->IsString()) {
    status_.Update(TypeError(arg_index, "STRING", arg_types_[arg_index]));
    return false;
  }
  return true;
}

bool StringFormatEvaluator::TypeCheckProtoOrJsonArg(int64_t arg_index) {
  ABSL_DCHECK(arg_index < arg_types_.size());
  const Type* t = arg_types_[arg_index];
  if (!t->IsProto() && !t->IsJson()) {
    status_.Update(
        TypeError(arg_index, "PROTO or JSON", arg_types_[arg_index]));
    return false;
  } else if (t->IsProto() && type_resolver_ == nullptr) {
    status_.Update(googlesql_base::InternalErrorBuilder()
                   << "%p specified for " << arg_index
                   << " but type_resolver_ is not set");
    return false;
  }
  return true;
}

// Extends absl::StrFormat to print a FormatGsqlString object.
absl::FormatConvertResult<absl::FormatConversionCharSet::s> AbslFormatConvert(
    const FormatGsqlString& value, const absl::FormatConversionSpec& conv,
    absl::FormatSink* sink) {
  const size_t min_codepoints = conv.width() > 0 ? conv.width() : 0;
  const size_t max_codepoints = conv.precision() >= 0
                                    ? conv.precision()
                                    : std::numeric_limits<size_t>::max();

  const char* str_data = value.view.data();
  const size_t length = value.view.length();
  int codepoints = 0;
  size_t i = 0;
  while (i < length && codepoints < max_codepoints) {
    // U8_NEXT increments i
    UChar32 character;
    U8_NEXT(str_data, i, length, character);
    if (character < 0) {
      // We should check all inputs prior to calling here.
      return {false};
    } else {
      ++codepoints;
    }
  }
  absl::string_view utf8_view = value.view.substr(0, i);

  size_t padding_size = Excess(codepoints, min_codepoints);

  // Add the padding and the utf8 string content.
  if (padding_size > 0 && !conv.has_left_flag()) {
    sink->Append(padding_size, ' ');
  }
  sink->Append(utf8_view);
  if (padding_size > 0 && conv.has_left_flag()) {
    sink->Append(padding_size, ' ');
  }

  return {true};
}

// Implements AbslFormatConvert of FormatGsqlInt64 and FormatGsqlUint64 below.
template <bool GROUPING>
absl::FormatConvertResult<absl::FormatConversionCharSet::kIntegral> ConvertInt(
    uint64_t magnitude, bool negative, const absl::FormatConversionSpec& conv,
    absl::FormatSink* sink) {
  constexpr int kMaxDigits = 23;  // Octal std::numeric_limits<uint64>::max()
                                  // has at most 22 digits + \0
  char buffer[kMaxDigits];
  char sep = ',';
  char prefix1 = '\0';
  char prefix2 = '\0';
  uint8_t group_size = 3;
  int sig_digits = 0;
  switch (conv.conversion_char()) {
    case absl::FormatConversionChar::o:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%o", magnitude);
      prefix1 = '0';
      group_size = 4;
      break;
    case absl::FormatConversionChar::x:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%x", magnitude);
      sep = ':';
      prefix1 = '0';
      prefix2 = 'x';
      group_size = 4;
      break;
    case absl::FormatConversionChar::X:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%X", magnitude);
      sep = ':';
      prefix1 = '0';
      prefix2 = 'X';
      group_size = 4;
      break;
    case absl::FormatConversionChar::d:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%d", magnitude);
      break;
    case absl::FormatConversionChar::i:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%i", magnitude);
      break;
    case absl::FormatConversionChar::u:
      sig_digits = absl::SNPrintF(buffer, kMaxDigits, "%u", magnitude);
      break;
    default:
      return {false};
  }
  ABSL_DCHECK_GE(sig_digits, 1);
  ABSL_DCHECK_LE(sig_digits, 22);

  // The number of digits before the first separator character.
  int leading_digits = ((sig_digits - 1) % group_size) + 1;
  ABSL_DCHECK_GE(leading_digits, 1);
  ABSL_DCHECK_LE(leading_digits, 4);

  // The number of separator characters in printed value.
  int separators = (sig_digits - leading_digits) / group_size;
  // 6 separators for std::numeric_limits<uint64>::max() in base 10
  ABSL_DCHECK_LE(separators, 6);
  // The number of leading zeros that satisfy the required precision.
  int precision_digits = 0;
  // The total number of digits (signficant + precision).
  int total_digits = sig_digits;

  // Update the counts if the precision was supplied in the format string.
  if (conv.precision() >= 0 && conv.precision() > sig_digits) {
    if (conv.conversion_char() == absl::FormatConversionChar::o) {
      prefix1 = '\0';
    }
    total_digits = conv.precision();
    precision_digits = total_digits - sig_digits;
    leading_digits = ((total_digits - 1) % group_size) + 1;
    separators = (total_digits - leading_digits) / group_size;
  }
  ABSL_DCHECK_GE(precision_digits, 0);
  ABSL_DCHECK_GE(total_digits, 1);
  ABSL_DCHECK_GE(leading_digits, 1);
  ABSL_DCHECK_LE(leading_digits, 4);

  // The total number of padding characters (zero or spaces) to satisfy width.
  int padding_size = 0;

  // Update the counts if the with was supplied in the format string.
  if (conv.width() > 0) {
    padding_size = conv.width();
    padding_size -= total_digits;
    if (GROUPING) {
      padding_size -= separators;
    }
    if (negative || conv.has_show_pos_flag() || conv.has_sign_col_flag()) {
      padding_size--;
    }
    if (conv.has_alt_flag()) {
      if (prefix1 != '\0') {
        padding_size--;
      }
      if (prefix2 != '\0') {
        padding_size--;
      }
    }
  }
  ABSL_DCHECK_LT(padding_size, 1 << 30);      // Sanity check.
  ABSL_DCHECK_LT(precision_digits, 1 << 30);  // Sanity check.

  if (padding_size > 0 && !conv.has_left_flag() && !conv.has_zero_flag()) {
    sink->Append(padding_size, ' ');
  }

  if (negative) {
    sink->Append(1, '-');
  } else if (conv.has_show_pos_flag()) {
    sink->Append(1, '+');
  } else if (conv.has_sign_col_flag()) {
    sink->Append(1, ' ');
  }

  if (conv.has_alt_flag()) {
    if (prefix1 != '\0') {
      sink->Append(1, prefix1);
    }
    if (prefix2 != '\0') {
      sink->Append(1, prefix2);
    }
  }

  if (padding_size > 0 && !conv.has_left_flag() && conv.has_zero_flag()) {
    sink->Append(padding_size, '0');
  }

  if (GROUPING) {
    // Append leading precision digits with group separators.
    if (precision_digits > 0) {
      // Number of precision digits before the first separator.
      int leading_precision_digits = std::min(leading_digits, precision_digits);
      sink->Append(leading_precision_digits, '0');
      precision_digits -= leading_precision_digits;
      if (leading_digits > 0 && leading_digits == leading_precision_digits) {
        sink->Append(1, sep);
      }
      while (precision_digits >= group_size) {
        sink->Append(group_size, '0');
        sink->Append(1, sep);
        precision_digits -= group_size;
      }
      ABSL_DCHECK_GE(precision_digits, 0);
      sink->Append(precision_digits, '0');
      // How many significant digits are needed before the *next* separator.
      leading_digits = std::min(sig_digits, group_size - precision_digits);
    }

    // Append the significant digits with group separators.
    char* start = buffer;
    sink->Append(absl::string_view(start, leading_digits));
    start += leading_digits;
    while (start < buffer + sig_digits) {
      sink->Append(1, sep);
      sink->Append(absl::string_view(start, group_size));
      start += group_size;
    }
  } else {
    sink->Append(precision_digits, '0');
    sink->Append(absl::string_view(buffer, sig_digits));
  }

  if (padding_size > 0 && conv.has_left_flag()) {
    sink->Append(padding_size, ' ');
  }
  return {true};
}

// Extends absl::StrFormat to print a FormatGsqlInt64 object.
template <bool GROUPING>
absl::FormatConvertResult<absl::FormatConversionCharSet::kIntegral>
AbslFormatConvert(const FormatGsqlInt64<GROUPING>& value,
                  const absl::FormatConversionSpec& conv,
                  absl::FormatSink* sink) {
  bool negative = value.value < 0;
  uint64_t magnitude = 0;
  if (ABSL_PREDICT_FALSE(value.value ==
                         std::numeric_limits<int64_t>::lowest())) {
    magnitude = uint64_t{0x8000000000000000u};
  } else if (negative) {
    magnitude = -value.value;
  } else {
    magnitude = value.value;
  }
  return ConvertInt<GROUPING>(magnitude, negative, conv, sink);
}

// Extends absl::StrFormat to print a FormatGsqlUint64 object.
template <bool GROUPING>
absl::FormatConvertResult<absl::FormatConversionCharSet::kIntegral>
AbslFormatConvert(const FormatGsqlUint64<GROUPING>& value,
                  const absl::FormatConversionSpec& conv,
                  absl::FormatSink* sink) {
  return ConvertInt<GROUPING>(value.value, /*negative=*/false, conv, sink);
}

// Extends absl::StrFormat to print a FormatGsqlDouble object.
template <bool GROUPING>
absl::FormatConvertResult<absl::FormatConversionCharSet::kFloating>
AbslFormatConvert(const FormatGsqlDouble<GROUPING>& value,
                  const absl::FormatConversionSpec& conv,
                  absl::FormatSink* sink) {
  // The buffer needs to hold both the integer part (up to 308 digits) and the
  // fraction part (up to kMaxPrecisionForFloatingPoint digits), plus the
  // decimal point. This is good for 'f' format. 'e' format requires less.
  constexpr int kBufferSize = 2048;
  char buffer[kBufferSize];
  int fmt_length = 0;
  bool negative = std::signbit(value.value);
  double magnitude = value.value;
  if (negative) {
    magnitude = -magnitude;
  }
  switch (conv.conversion_char()) {
    case absl::FormatConversionChar::e:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*e",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*e",
                                            conv.precision(), magnitude);
      }
      break;
    case absl::FormatConversionChar::E:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*E",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*E",
                                            conv.precision(), magnitude);
      }
      break;
    case absl::FormatConversionChar::f:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*f",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*f",
                                            conv.precision(), magnitude);
      }
      break;
    case absl::FormatConversionChar::F:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*F",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*F",
                                            conv.precision(), magnitude);
      }
      break;
    case absl::FormatConversionChar::g:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*g",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*g",
                                            conv.precision(), magnitude);
      }
      break;
    case absl::FormatConversionChar::G:
      if (conv.has_alt_flag()) {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%#.*G",
                                            conv.precision(), magnitude);
      } else {
        fmt_length = absl::SNPrintF(buffer, kBufferSize, "%.*G",
                                            conv.precision(), magnitude);
      }
      break;
    default:
      return {false};
  }
  ABSL_DCHECK_GE(fmt_length, 1);
  // With max precision limited to kMaxPrecisionForFloatingPoint, we're sure
  // that the buffer provided is big enough for all cases.
  ABSL_DCHECK_LT(fmt_length, kBufferSize);

  // Task 1: Find the integer portion.
  // Before figuring our grouping separators, first we need to find the integer
  // portion of the string absl::StrFormat produced. Only the int portion gets
  // grouping separators.
  bool has_int = absl::ascii_isdigit(buffer[0]);
  // The number of digits in the integer component of the floating point number.
  int int_digits = 0;
  for (; int_digits < fmt_length && absl::ascii_isdigit(buffer[int_digits]);
       ++int_digits) {
    // Nothing to do.
  }

  // Task 2: Calculate number of separators.
  constexpr uint8_t kGroupSize = 3;
  // The number of digits before the first separator character.
  int leading_digits = 0;
  // The number of separator characters in printed value.
  int separators = 0;
  if (has_int) {
    leading_digits = ((int_digits - 1) % kGroupSize) + 1;
    ABSL_DCHECK_GE(leading_digits, 1);
    ABSL_DCHECK_LE(leading_digits, 4);
    if (GROUPING) {
      separators = (int_digits - leading_digits) / kGroupSize;
    }
  }

  // Task 3: Calculate how much padding we need to satisfy width.
  // The total number of padding characters (zero or spaces) to satisfy width.
  int padding_size = 0;
  // Update the counts if the with was supplied in the format string.
  if (conv.width() > 0) {
    padding_size = conv.width();
    padding_size -= fmt_length;
    padding_size -= separators;
    // If there is an integer component, we add the sign char explicitly.
    if (negative || conv.has_show_pos_flag() || conv.has_sign_col_flag()) {
      padding_size -= 1;
    }
  }
  ABSL_DCHECK_LT(padding_size, 1 << 30);  // Sanity check.

  // Task 4: Print the formatted value to the byte sink.
  bool use_zero_padding = conv.has_zero_flag() && has_int;
  if (padding_size > 0 && !conv.has_left_flag() && !use_zero_padding) {
    sink->Append(padding_size, ' ');
  }

  if (negative) {
    sink->Append(1, '-');
  } else if (conv.has_show_pos_flag()) {
    sink->Append(1, '+');
  } else if (conv.has_sign_col_flag()) {
    sink->Append(1, ' ');
  }

  // Pointer to the first formatted character copied to the sink.
  char* start = buffer;

  // Append the integer part with any sign character, zero-padding, and
  // separators.
  constexpr char kSep = ',';
  if (has_int) {
    // Only append zero padding when there is an integer component because it
    // does not apply for 'nan', 'inf', etc.
    if (padding_size > 0 && !conv.has_left_flag() && use_zero_padding) {
      sink->Append(padding_size, '0');
    }

    sink->Append(absl::string_view(start, leading_digits));
    start += leading_digits;
    while (start < buffer + int_digits - 1) {
      if (GROUPING) {
        sink->Append(1, kSep);
      }
      sink->Append(absl::string_view(start, kGroupSize));
      start += kGroupSize;
    }
  }

  // Append whatever is left over after printing integer part.
  sink->Append(absl::string_view(start, buffer + fmt_length - start));

  if (padding_size > 0 && conv.has_left_flag()) {
    sink->Append(padding_size, ' ');
  }
  return {true};
}

// Extends absl::StrFormat to print a FormatGsqlNumeric object.
template <typename NumericType, bool GROUPING>
absl::FormatConvertResult<absl::FormatConversionCharSet::kFloating>
AbslFormatConvert(const FormatGsqlNumeric<NumericType, GROUPING>& value,
                  const absl::FormatConversionSpec& conv,
                  absl::FormatSink* sink) {
  using FormatSpec = NumericValue::FormatSpec;
  FormatSpec spec;
  if (conv.width() > 0) {
    spec.minimum_size = conv.width();
  }
  if (conv.precision() >= 0) {
    spec.precision = conv.precision();
  }
  if (conv.conversion_char() == absl::FormatConversionChar::e) {
    spec.mode = FormatSpec::E_NOTATION_LOWER_CASE;
  } else if (conv.conversion_char() == absl::FormatConversionChar::E) {
    spec.mode = FormatSpec::E_NOTATION_UPPER_CASE;
  } else if (conv.conversion_char() == absl::FormatConversionChar::g) {
    spec.mode = FormatSpec::GENERAL_FORMAT_LOWER_CASE;
    if (!conv.has_alt_flag()) {
      spec.format_flags |=
          FormatSpec::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT;
    }
  } else if (conv.conversion_char() == absl::FormatConversionChar::G) {
    spec.mode = FormatSpec::GENERAL_FORMAT_UPPER_CASE;
    if (!conv.has_alt_flag()) {
      spec.format_flags |=
          FormatSpec::REMOVE_TRAILING_ZEROS_AFTER_DECIMAL_POINT;
    }
  }

  if (conv.has_alt_flag()) {
    spec.format_flags |= FormatSpec::ALWAYS_PRINT_DECIMAL_POINT;
  }
  if (conv.has_show_pos_flag()) {
    spec.format_flags |= FormatSpec::ALWAYS_PRINT_SIGN;
  }
  if (conv.has_zero_flag()) {
    spec.format_flags |= FormatSpec::ZERO_PAD;
  }
  if (conv.has_sign_col_flag()) {
    spec.format_flags |= FormatSpec::SIGN_SPACE;
  }
  if (conv.has_left_flag()) {
    spec.format_flags |= FormatSpec::LEFT_JUSTIFY;
  }
  if (GROUPING) {
    spec.format_flags |= FormatSpec::USE_GROUPING_CHAR;
  }

  if (std::max(spec.minimum_size, spec.precision) >=
      absl::GetFlag(FLAGS_googlesql_format_max_output_width)) {
    // This should be unreachable, since these should be checked before
    // we get here.
    return {false};
  }
  std::string output;
  value.value.FormatAndAppend(spec, &output);
  sink->Append(output);
  // Add padding for left justified format.
  if (conv.has_left_flag() && conv.width() > 0 &&
      conv.width() > output.size()) {
    sink->Append(conv.width() - output.size(), ' ');
  }
  return {true};
}

}  // namespace string_format_internal

absl::Status StringFormatUtf8(absl::string_view format_string,
                              absl::Span<const Value> values,
                              ProductMode product_mode, std::string* output,
                              bool* is_null, bool canonicalize_zero,
                              bool use_external_float32) {
  bool maybe_need_proto_factory = false;
  std::vector<const Type*> types;
  for (const Value& value : values) {
    types.push_back(value.type());
    const Type* t = value.type()->IsArray()
                        ? value.type()->AsArray()->element_type()
                        : value.type();

    if (t->IsProto() || t->IsStruct()) {
      // A struct may contain a proto (transitively). It's probably cheaper
      // to just make the factory instead of searching the struct for
      // a proto type.
      maybe_need_proto_factory = true;
    }
  }

  std::unique_ptr<google::protobuf::DynamicMessageFactory> factory;
  if (maybe_need_proto_factory) {
    factory = std::make_unique<google::protobuf::DynamicMessageFactory>();
  }
  string_format_internal::StringFormatEvaluator evaluator(
      product_mode, canonicalize_zero, use_external_float32);
  GOOGLESQL_RETURN_IF_ERROR(evaluator.SetTypes(std::move(types), factory.get()));
  GOOGLESQL_RETURN_IF_ERROR(evaluator.SetPattern(format_string));

  return evaluator.Format(std::move(values), output, is_null);
}

absl::Status CheckStringFormatUtf8ArgumentTypes(absl::string_view format_string,
                                                std::vector<const Type*> types,
                                                ProductMode product_mode) {
  google::protobuf::DynamicMessageFactory factory;
  string_format_internal::StringFormatEvaluator evaluator(product_mode);
  GOOGLESQL_RETURN_IF_ERROR(evaluator.SetTypes(std::move(types), &factory));
  return evaluator.SetPattern(format_string);
}

}  // namespace functions
}  // namespace googlesql
