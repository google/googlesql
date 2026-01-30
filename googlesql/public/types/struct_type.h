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

#ifndef GOOGLESQL_PUBLIC_TYPES_STRUCT_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_STRUCT_TYPE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/list_backed_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/base/case.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace googlesql {

// Field contained in a struct, representing a name and type.
// The SWIG compiler does not understand nested classes, so this cannot be
// defined inside the scope of StructType.
class LanguageOptions;
class TypeFactory;
class TypeParameterValue;
class TypeParameters;
class ValueContent;
class ValueProto;

struct StructField {
  StructField(std::string name_in, const Type* type_in)
      : name(std::move(name_in)), type(type_in) {}

  std::string name;  // Empty string means this is an unnamed field.
  const Type* type;
};

// A struct type.
// Structs are allowed to have zero fields, but this is not normal usage.
// Field names do not have to be unique.
// Empty field names are used to indicate anonymous fields - such fields are
// unnamed and cannot be looked up by name.
class StructType : public ListBackedType {
 public:
#ifndef SWIG
  StructType(const StructType&) = delete;
  StructType& operator=(const StructType&) = delete;

  // StructField is declared here for compatibility with existing code.
  using StructField = ::googlesql::StructField;
#endif  // SWIG

  int num_fields() const { return fields_.size(); }
  const StructField& field(int i) const { return fields_[i]; }
  const std::vector<StructField>& fields() const { return fields_; }

  const StructType* AsStruct() const override { return this; }

  std::vector<const Type*> ComponentTypes() const override {
    std::vector<const Type*> component_types;
    component_types.reserve(fields_.size());
    for (const StructField& field : fields_) {
      component_types.push_back(field.type);
    }
    return component_types;
  }

  // Look up a field by name.
  // Returns NULL if <name> is not found (uniquely).
  // Returns in <*is_ambiguous> whether this lookup was ambiguous.
  // If found_idx is non-NULL, returns position of found field in <*found_idx>.
  const StructField* FindField(absl::string_view name, bool* is_ambiguous,
                               int* found_idx = nullptr) const;

  // Check if structure has some fields.
  bool HasAnyFields() const override;

  // Helper functions for determining Equals() or Equivalent() for struct
  // types. For structs, Equals() means that the fields have the same name
  // and Equals() types.  Struct Equivalent() means that the fields have
  // Equivalent() types (but not necessarily the same names).
  static bool EqualsImpl(const StructType* type1, const StructType* type2,
                         bool equivalent);
  static bool FieldEqualsImpl(const StructField& field1,
                              const StructField& field2, bool equivalent);

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;

  // Struct types support equality iff all of the field types support equality.
  bool SupportsEquality() const override;

  bool UsingFeatureV12CivilTimeType() const override;

  std::string ShortTypeName(ProductMode mode,
                            bool use_external_float32) const override;
  std::string ShortTypeName(ProductMode mode) const override {
    return ShortTypeName(mode, /*use_external_float32=*/false);
  };
  std::string TypeName(ProductMode mode,
                       bool use_external_float32) const override;
  std::string TypeName(ProductMode mode) const override {
    return TypeName(mode, /*use_external_float32=*/false);
  }

  // Same as above, but the type modifier values are appended to the SQL name
  // for this StructType.
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32) const override;
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    return TypeNameWithModifiers(type_modifiers, mode,
                                 /*use_external_float32=*/false);
  }

  std::string CapitalizedName() const override;

  int nesting_depth() const override { return nesting_depth_; }

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  // Validate and resolve type parameters for struct type, currently always
  // return error since struct type itself doesn't support type parameters.
  absl::StatusOr<TypeParameters> ValidateAndResolveTypeParameters(
      const std::vector<TypeParameterValue>& type_parameter_values,
      ProductMode mode) const override;

  // Validates resolved type parameters for struct subfields recursively.
  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

 protected:
  // Return estimated size of memory owned by this type. Owned memory includes
  // field names, but not the memory associated with field types (which are
  // owned by some TypeFactory).
  int64_t GetEstimatedOwnedMemoryBytesSize() const override
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

 private:
  // Caller must enforce that <nesting_depth> is accurate. No verification is
  // done.
  StructType(const TypeFactoryBase* factory, std::vector<StructField> fields,
             int nesting_depth);
  ~StructType() override;

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override;

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override;

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  absl::StatusOr<std::string> TypeNameImpl(
      int field_limit,
      const std::function<absl::StatusOr<std::string>(
          const googlesql::Type*, int field_index)>& field_debug_fn) const;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  HasFieldResult HasFieldImpl(const std::string& name, int* field_id,
                              bool include_pseudo_fields) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;
  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;
  void FormatValueContentDebugModeImpl(
      const internal::ValueContentOrderedList* container,
      const FormatValueContentOptions& options, std::string* result) const;
  void FormatValueContentSqlModeImpl(
      const internal::ValueContentOrderedList* container,
      const FormatValueContentOptions& options, std::string* result) const;

  const std::vector<StructField> fields_;

  // The deepest nesting depth in the type tree rooted at this StructType, i.e.,
  // the maximum nesting_depth of the field types, plus 1 for the StructType
  // itself. If all fields are simple types, then this is 1. This field is not
  // serialized. It is recalculated during deserialization.
  const int nesting_depth_;

  // Lazily built map from name to struct field index. Ambiguous lookups are
  // designated with an index of -1. This is only built if FindField is called.
  // The key to the map is casefolded (lowercase) normalized representation of
  // the field name so it should not be exposed outside this class.
  mutable absl::Mutex mutex_;
  mutable absl::flat_hash_map<std::string, int> field_name_to_index_map_
      ABSL_GUARDED_BY(mutex_);

  friend class TypeFactory;
  friend class MeasureType;
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructDebugMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructWithAnonymousFieldsDebugMode);
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_STRUCT_TYPE_H_
