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

#include "googlesql/public/types/simple_value.h"

#include <string.h>

#include <cstdint>
#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/public/simple_value.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// static
SimpleValue SimpleValue::String(std::string v) {
  return SimpleValue(TYPE_STRING, std::move(v));
}
SimpleValue SimpleValue::Int64(int64_t v) { return SimpleValue(TYPE_INT64, v); }
SimpleValue SimpleValue::Bool(bool v) { return SimpleValue(TYPE_BOOL, v); }
SimpleValue SimpleValue::Double(double v) {
  return SimpleValue(TYPE_DOUBLE, v);
}
SimpleValue SimpleValue::Bytes(std::string v) {
  return SimpleValue(TYPE_BYTES, std::move(v));
}

SimpleValue::SimpleValue(SimpleValue&& that) {
  // NOLINTNEXTLINE - suppress clang-tidy warning on not TriviallyCopyable.
  memcpy(this, &that, sizeof(SimpleValue));
  // Invalidate 'that' to disable its destructor.
  that.type_ = TYPE_INVALID;
}

SimpleValue& SimpleValue::operator=(const SimpleValue& that) {
  // Self-copying must not clear the contents of the value.
  if (this == &that) {
    return *this;
  }
  Clear();
  CopyFrom(that);
  return *this;
}

SimpleValue& SimpleValue::operator=(SimpleValue&& that) {
  Clear();
  // NOLINTNEXTLINE - suppress clang-tidy warning on not TriviallyCopyable.
  memcpy(this, &that, sizeof(SimpleValue));
  // Invalidate 'that' to disable its destructor.
  that.type_ = TYPE_INVALID;
  return *this;
}

void SimpleValue::Clear() {
  switch (type_) {
    case TYPE_STRING:
    case TYPE_BYTES:
      string_ptr_->Unref();
      break;
    case TYPE_INVALID:
    case TYPE_INT64:
    case TYPE_BOOL:
    case TYPE_DOUBLE:
      // Nothing to clear.
      break;
    default:
      ABSL_CHECK(false) << "All ValueType must be explicitly handled in Clear()";
  }
  type_ = TYPE_INVALID;
}

int64_t SimpleValue::GetEstimatedOwnedMemoryBytesSize() const {
  switch (type_) {
    case TYPE_STRING:
    case TYPE_BYTES:
      return sizeof(SimpleValue) + string_ptr_->physical_byte_size();
    case TYPE_INVALID:
    case TYPE_INT64:
    case TYPE_BOOL:
    case TYPE_DOUBLE:
      return sizeof(SimpleValue);
    default:
      ABSL_CHECK(false) << "All ValueType must be explicitly handled";
  }
}

void SimpleValue::CopyFrom(const SimpleValue& that) {
  // Self-copy check is done in the copy constructor. Here we just ABSL_DCHECK that.
  ABSL_DCHECK_NE(this, &that);
  // NOLINTNEXTLINE - suppress clang-tidy warning on not TriviallyCopyable.
  memcpy(this, &that, sizeof(SimpleValue));
  if (!IsValid()) {
    return;
  }
  switch (type_) {
    case TYPE_STRING:
    case TYPE_BYTES:
      string_ptr_->Ref();
      break;
    case TYPE_INVALID:
    case TYPE_INT64:
    case TYPE_BOOL:
    case TYPE_DOUBLE:
      // memcpy() has copied all the data.
      break;
  }
}

int64_t SimpleValue::int64_value() const {
  ABSL_CHECK(has_int64_value()) << "Not an int64 value";
  return int64_value_;
}

const std::string& SimpleValue::string_value() const {
  ABSL_CHECK(has_string_value()) << "Not a string value";
  return string_ptr_->value();
}

bool SimpleValue::bool_value() const {
  ABSL_CHECK(has_bool_value()) << "Not an bool value";
  return bool_value_;
}

double SimpleValue::double_value() const {
  ABSL_CHECK(has_double_value()) << "Not a double value";
  return double_value_;
}

const std::string& SimpleValue::bytes_value() const {
  ABSL_CHECK(has_bytes_value()) << "Not a bytes value";
  return string_ptr_->value();
}

absl::Status SimpleValue::Serialize(SimpleValueProto* proto) const {
  switch (type_) {
    case SimpleValue::TYPE_INVALID:
      GOOGLESQL_RET_CHECK_FAIL() << "SimpleValue with TYPE_INVALID cannot be serialized";
      break;
    case SimpleValue::TYPE_INT64:
      proto->set_int64_value(int64_value());
      break;
    case SimpleValue::TYPE_STRING:
      proto->set_string_value(string_value());
      break;
    case SimpleValue::TYPE_BOOL:
      proto->set_bool_value(bool_value());
      break;
    case SimpleValue::TYPE_DOUBLE:
      proto->set_double_value(double_value());
      break;
    case SimpleValue::TYPE_BYTES:
      proto->set_bytes_value(bytes_value());
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unknown ValueType: " << type_;
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<SimpleValue> SimpleValue::Deserialize(
    const SimpleValueProto& proto) {
  SimpleValue value;
  switch (proto.value_case()) {
    case SimpleValueProto::kInt64Value:
      value = SimpleValue::Int64(proto.int64_value());
      break;
    case SimpleValueProto::kStringValue:
      value = SimpleValue::String(std::string(proto.string_value()));
      break;
    case SimpleValueProto::kBoolValue:
      value = SimpleValue::Bool(proto.bool_value());
      break;
    case SimpleValueProto::kDoubleValue:
      value = SimpleValue::Double(proto.double_value());
      break;
    case SimpleValueProto::kBytesValue:
      value = SimpleValue::Bytes(std::string(proto.bytes_value()));
      break;
    case SimpleValueProto::VALUE_NOT_SET:
      GOOGLESQL_RET_CHECK_FAIL() << "No value set on SimpleValueProto::value";
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unknown simpleValueProto.value_case():"
                       << proto.value_case();
  }
  return value;
}

bool SimpleValue::Equals(const SimpleValue& that) const {
  if (type_ != that.type_) {
    return false;
  }
  switch (type_) {
    case TYPE_INT64:
      return int64_value_ == that.int64_value();
    case TYPE_STRING:
      return string_value() == that.string_value();
    case TYPE_BOOL:
      return bool_value() == that.bool_value();
    case TYPE_DOUBLE:
      return double_value() == that.double_value();
    case TYPE_BYTES:
      return bytes_value() == that.bytes_value();
    case TYPE_INVALID:
      return true;
  }
}

std::string SimpleValue::DebugString() const {
  switch (type_) {
    case TYPE_INT64:
      return std::to_string(int64_value());
    case TYPE_STRING:
      return absl::StrCat("\"", string_value(), "\"");
    case TYPE_BOOL:
      return std::to_string(bool_value());
    case TYPE_DOUBLE:
      return std::to_string(double_value());
    case TYPE_BYTES:
      return ToBytesLiteral(bytes_value());
    case TYPE_INVALID:
      return "<INVALID>";
  }
}

}  // namespace googlesql
