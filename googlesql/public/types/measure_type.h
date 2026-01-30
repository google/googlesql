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

#ifndef GOOGLESQL_PUBLIC_TYPES_MEASURE_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_MEASURE_TYPE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/container_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

class MeasureType : public ContainerType {
 public:
#ifndef SWIG
  MeasureType(const MeasureType&) = delete;
  MeasureType& operator=(const MeasureType&) = delete;
#endif  // SWIG

  const Type* result_type() const { return result_type_; }

  std::vector<const Type*> ComponentTypes() const override {
    return {result_type_};
  }

  const MeasureType* AsMeasure() const override { return this; }

  std::string ShortTypeName(ProductMode mode,
                            bool use_external_float32) const override;
  std::string ShortTypeName(ProductMode mode) const override {
    return ShortTypeName(mode, /*use_external_float32=*/false);
  }
  std::string TypeName(ProductMode mode,
                       bool use_external_float32) const override;
  std::string TypeName(ProductMode mode) const override {
    return TypeName(mode, /*use_external_float32=*/false);
  }
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32) const override;
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    return TypeNameWithModifiers(type_modifiers, mode,
                                 /*use_external_float32=*/false);
  }

  std::string CapitalizedName() const override;

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;
  bool SupportsEquality() const override;

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  int nesting_depth() const override {
    return result_type_->nesting_depth() + 1;
  };

  bool EqualsForSameKind(const Type* that, bool equivalent) const override {
    // Measure type is only equal to itself; two measures with the same result
    // type are not considered equal or equivalent.
    return this == that;
  };

 protected:
  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  // Return estimated size of memory owned by this type. Measure's owned memory
  // does not include its output type's memory (which is owned by TypeFactory).
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

 private:
  // Types can only be created and destroyed by TypeFactory.
  MeasureType(const TypeFactoryBase* factory, const Type* result_type)
      : ContainerType(factory, TYPE_MEASURE), result_type_(result_type) {};

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override;

  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  void ClearValueContent(const ValueContent& value) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;

  // The type produced when evaluating the measure with `AGGREGATE()`.
  const Type* result_type_;

  friend class TypeFactory;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_MEASURE_TYPE_H_
