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

#include "googlesql/public/types/builtin_declarative_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/string_util.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "googlesql/public/types/declarative_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

static absl::StatusOr<TypeParameters> ResolveVectorTypeParameters(
    const std::vector<TypeParameterValue>& resolved_type_parameter_list,
    ProductMode mode) {
  GOOGLESQL_RET_CHECK(!resolved_type_parameter_list.empty());
  if (resolved_type_parameter_list.size() > 2) {
    return MakeSqlError() << "VECTOR type has too many type parameters. Found "
                          << resolved_type_parameter_list.size()
                          << " parameters";
  }
  const TypeParameterValue& param = resolved_type_parameter_list[0];
  if (param.IsSpecialLiteral() || !param.GetValue().has_int64_value()) {
    return MakeSqlError() << "VECTOR dims parameter must be an integer literal";
  }
  int64_t dims = param.GetValue().int64_value();
  if (dims <= 0) {
    return MakeSqlError() << "VECTOR dims must be greater than 0";
  }
  VectorTypeParametersProto proto;
  proto.set_dims(dims);

  // Try parsing the encoding parameter.
  if (resolved_type_parameter_list.size() > 1) {
    const TypeParameterValue& encoding_param = resolved_type_parameter_list[1];
    if (encoding_param.IsSpecialLiteral() ||
        !encoding_param.GetValue().has_string_value()) {
      return MakeSqlError()
             << "VECTOR encoding parameter must be a string literal";
    }
    std::string encoding_str = encoding_param.GetValue().string_value();
    absl::AsciiStrToUpper(&encoding_str);
    googlesql::VectorEncodingId::Id encoding_enum;
    if (!googlesql::VectorEncodingId_Id_Parse(encoding_str, &encoding_enum) ||
        encoding_enum == googlesql::VectorEncodingId::UNKNOWN_VECTOR_ENCODING) {
      return MakeSqlError()
             << R"(Unrecognized VECTOR encoding: ")"
             << encoding_param.GetValue().string_value() << R"(")";
    }
    proto.set_encoding(encoding_enum);
  }

  return TypeParameters::MakeVectorTypeParameters(proto);
}

static absl::Status ValidateVectorTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) {
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(type_parameters.IsVectorTypeParameters());
  return TypeParameters::ValidateVectorTypeParameters(
      *type_parameters.vector_type_parameters());
}

using FormatOptions =
    DeclarativeTypeDescriptor::FormattingCustom::FormatOptions;

// Renders a VECTOR value, or returns an error if its bytes are malformed.
static absl::StatusOr<std::string> FormatVectorValueOrError(
    const ValueContent& value, const FormatOptions& opts) {
  absl::string_view bytes = value.GetAs<internal::StringRef*>()->value();
  GOOGLESQL_ASSIGN_OR_RETURN(VectorValueProto vector_value, ParseVectorValueBytes(bytes));
  const auto encoding =
      static_cast<VectorEncodingId::Id>(vector_value.encoding());

  // Only a FLOAT32 vector has a renderable element list. Other encodings are
  // quantized, so there is nothing to show but the encoding itself, and no way
  // to spell the value as SQL.
  if (encoding != VectorEncodingId::FLOAT32) {
    switch (opts.mode) {
      case FormatOptions::Mode::kDebug:
        return absl::StrCat("VECTOR(<", VectorEncodingId_Id_Name(encoding),
                            ">)");
      case FormatOptions::Mode::kSQLLiteral:
      case FormatOptions::Mode::kSQLExpression:
        return "ERROR('Unimplemented')";
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<float> elements,
                   DecodeFloat32VectorPayload(vector_value.data()));
  std::string float_array_str =
      absl::StrJoin(elements, ", ", [](std::string* out, float element) {
        absl::StrAppend(out, RoundTripFloatToString(element));
      });

  switch (opts.mode) {
    case FormatOptions::Mode::kDebug:
      return absl::StrCat("VECTOR([", std::move(float_array_str), "])");
    case FormatOptions::Mode::kSQLLiteral:
    case FormatOptions::Mode::kSQLExpression:
      return absl::StrCat("ENCODE_VECTOR([", std::move(float_array_str), "])");
  }
}

// The VECTOR formatting callback. Its signature is fixed by
// DeclarativeTypeDescriptor::FormattingCustom::Callback, which returns a
// std::string and has no way to report an error, so this is the one place an
// error is turned into text.
static std::string FormatVectorValue(const ValueContent& value,
                                     const FormatOptions& opts) {
  absl::StatusOr<std::string> formatted = FormatVectorValueOrError(value, opts);
  if (!formatted.ok()) {
    return "ERROR: Invalid VECTOR value";
  }
  return *std::move(formatted);
}

// Static initialization of opaque callback registries
template <typename T>
using BuiltinOpaqueCallbackMap = absl::flat_hash_map<
    /*local_type_id=*/absl::string_view, T>;

static BuiltinOpaqueCallbackMap<TypeParameterHandlers>
InitBuiltinTypeParameterHandlers() {
  BuiltinOpaqueCallbackMap<TypeParameterHandlers> handlers_map;

  // Add type parameter handlers for built-in types.
  absl::StatusOr<TypeParameterHandlers> vector_handlers =
      TypeParameterHandlers::Create(&ResolveVectorTypeParameters,
                                    &ValidateVectorTypeParameters);
  if (vector_handlers.ok()) {
    handlers_map.emplace(kVectorTypeName, *std::move(vector_handlers));
  }
  return handlers_map;
}

using FormattingCallback =
    DeclarativeTypeDescriptor::FormattingCustom::Callback;

static BuiltinOpaqueCallbackMap<FormattingCallback>
InitBuiltinCustomFormattingCallbacks() {
  BuiltinOpaqueCallbackMap<FormattingCallback> formatting_callbacks_map;

  // Add custom formatting callbacks for built-in types.
  formatting_callbacks_map.emplace(kVectorTypeName, &FormatVectorValue);

  return formatting_callbacks_map;
}

std::optional<TypeParameterHandlers> GetBuiltinTypeParameterHandlers(
    absl::string_view type_id) {
  static const absl::NoDestructor<
      BuiltinOpaqueCallbackMap<TypeParameterHandlers>>
      kHandlers(InitBuiltinTypeParameterHandlers());
  auto it = kHandlers->find(type_id);
  if (it == kHandlers->end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<FormattingCallback> GetBuiltinCustomFormattingCallback(
    absl::string_view type_id) {
  static const absl::NoDestructor<BuiltinOpaqueCallbackMap<FormattingCallback>>
      kCallbacks(InitBuiltinCustomFormattingCallbacks());
  auto it = kCallbacks->find(type_id);
  if (it == kCallbacks->end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace googlesql
