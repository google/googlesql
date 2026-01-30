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

#ifndef GOOGLESQL_PUBLIC_TYPES_CONTAINER_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_CONTAINER_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "googlesql/common/thread_stack.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Represents a type which composites other types.
class ContainerType : public Type {
 public:
#ifndef SWIG
  ContainerType(const ContainerType&) = delete;
  ContainerType& operator=(const ContainerType&) = delete;
#endif  // SWIG

 protected:
  ContainerType(const TypeFactoryBase* factory, TypeKind kind)
      : Type(factory, kind) {}

  struct NullableValueContentEq {
    explicit NullableValueContentEq(const ValueEqualityCheckOptions& options,
                                    const Type* type)
        : options(options), type(type) {}
    size_t operator()(const internal::NullableValueContent x,
                      const internal::NullableValueContent y) const {
      if (x.is_null() != y.is_null()) return false;
      if (x.is_null() && y.is_null()) return true;
      return type->ValueContentEquals(x.value_content(), y.value_content(),
                                      options);
    }
    const ValueEqualityCheckOptions options;
    const Type* type;
  };

  struct HashableNullableValueContent {
    const internal::NullableValueContent element;
    const Type* type;

    template <typename H>
    H Hash(H h) const {
      type->HashValueContent(element.value_content(),
                             absl::HashState::Create(&h));
      return h;
    }

    template <typename H>
    friend H AbslHashValue(H h, const HashableNullableValueContent& v) {
      if (!v.element.is_null()) h = v.Hash(std::move(h));
      return H::combine(std::move(h), v.element.is_null());
    }
  };

  struct NullableValueContentHasher {
    explicit NullableValueContentHasher(const Type* type) : type(type) {}

    size_t operator()(const internal::NullableValueContent& x) const {
      return absl::Hash<HashableNullableValueContent>()(
          HashableNullableValueContent{x, type});
    }

   private:
    const Type* type;
  };

  std::optional<bool> NullableValueContentLess(
      const internal::NullableValueContent& x,
      const internal::NullableValueContent& y, const Type* x_type,
      const Type* y_type) const {
    if (x.is_null() && y.is_null()) return std::nullopt;
    if (x.is_null() && !y.is_null()) {
      return true;
    }
    if (y.is_null()) {
      return false;
    }
    if (x_type->ValueContentLess(x.value_content(), y.value_content(),
                                 y_type)) {
      return true;
    } else if (y_type->ValueContentLess(y.value_content(), x.value_content(),
                                        x_type)) {
      return false;
    }
    return std::nullopt;
  }

  std::string FormatNullableValueContent(
      const internal::NullableValueContent element, const Type* type,
      const FormatValueContentOptions& options) const {
    if (!ThreadHasEnoughStack()) {
      return std::string(kFormatValueContentOutOfStackError);
    }

    if (element.is_null()) {
      return options.as_literal()
                 ? "NULL"
                 : absl::StrCat("CAST(NULL AS ",
                                type->TypeName(options.product_mode,
                                               options.use_external_float32),
                                ")");
    }
    return type->FormatValueContent(element.value_content(), options);
  }

  // Returns a FormatValueContentOptions with debug options set, for use
  // printing ValueContent in error messages.
  FormatValueContentOptions DebugFormatValueContentOptions() const {
    Type::FormatValueContentOptions format_options;
    format_options.product_mode = ProductMode::PRODUCT_INTERNAL;
    format_options.mode = Type::FormatValueContentOptions::Mode::kDebug;
    return format_options;
  }

  // Formats a NullableValueContent in debug mode. If verbose mode is set, the
  // the type name is prepended to the formatted content. Ex: String("abc") or
  // Int(5).
  //
  // Since some types prepend the type name explicitly as part of
  // FormatValueContent in verbose debug mode, this function ensures the type
  // name is not prepended twice.
  std::string DebugFormatNullableValueContentForContainer(
      const internal::NullableValueContent& nullable_content, const Type* type,
      const FormatValueContentOptions& options) const {
    ABSL_DCHECK_EQ(options.mode, Type::FormatValueContentOptions::Mode::kDebug)
        << "This function should only be called in debug formatting mode";

    std::string value_str =
        FormatNullableValueContent(nullable_content, type, options);

    if (!options.verbose) {
      return value_str;
    }

    // If the value is not null, and the type has already included the type
    // name, don't prepend it again.
    if (!nullable_content.is_null() &&
        type->VerboseDebugFormatValueContentHasTypeName()) {
      return value_str;
    }

    return type->AddCapitalizedTypePrefix(value_str,
                                          nullable_content.is_null());
  }

  bool VerboseDebugFormatValueContentHasTypeName() const override {
    return true;
  };

  static constexpr absl::string_view kFormatValueContentOutOfStackError =
      "... <out of stack>";
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_CONTAINER_TYPE_H_
