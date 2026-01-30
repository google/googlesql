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

#ifndef GOOGLESQL_PUBLIC_TYPES_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_TYPE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/timestamp_util.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

class ArrayType;
class AnnotationMap;
class EnumType;
class ExtendedType;
class GraphElementType;
class GraphPathType;
class LanguageOptions;
class MapType;
class MeasureType;
class ProtoType;
class RangeType;
class RowType;
class StructType;
class Type;
class TypeFactory;
class TypeFactoryBase;
class TypeModifiers;
class TypeParameterValue;
class TypeParameters;
class Value;
class ValueContent;
class ValueProto;
class ValueTest;

namespace internal {

class TypeStore;
class TypeStoreHelper;

}  // namespace internal

typedef std::vector<Type*> TypeList;
typedef absl::Span<const Type* const> TypeListView;

// In-memory representation of a GoogleSQL type.
// See (broken link) for more information on the type system.
//
// Types are always const pointers, and always owned by a TypeFactory.
// Types can only be constructed by a TypeFactory, and their lifetime extends
// until that TypeFactory is destroyed.
class Type {
 public:
#ifndef SWIG
  Type(const Type&) = delete;
  Type& operator=(const Type&) = delete;
#endif  // SWIG

  TypeKind kind() const { return kind_; }

  bool IsInt32() const { return kind_ == TYPE_INT32; }
  bool IsInt64() const { return kind_ == TYPE_INT64; }
  bool IsUint32() const { return kind_ == TYPE_UINT32; }
  bool IsUint64() const { return kind_ == TYPE_UINT64; }
  bool IsBool() const { return kind_ == TYPE_BOOL; }
  bool IsFloat() const { return kind_ == TYPE_FLOAT; }
  bool IsDouble() const { return kind_ == TYPE_DOUBLE; }
  bool IsString() const { return kind_ == TYPE_STRING; }
  bool IsBytes() const { return kind_ == TYPE_BYTES; }
  bool IsDate() const { return kind_ == TYPE_DATE; }
  bool IsTimestamp() const { return kind_ == TYPE_TIMESTAMP; }

  bool IsTime() const { return kind_ == TYPE_TIME; }
  bool IsDatetime() const { return kind_ == TYPE_DATETIME; }
  bool IsInterval() const { return kind_ == TYPE_INTERVAL; }
  bool IsNumericType() const { return kind_ == TYPE_NUMERIC; }
  bool IsBigNumericType() const { return kind_ == TYPE_BIGNUMERIC; }
  bool IsJsonType() const { return kind_ == TYPE_JSON; }
  bool IsTokenListType() const { return kind_ == TYPE_TOKENLIST; }
  bool IsRangeType() const { return kind_ == TYPE_RANGE; }
  bool IsMapType() const { return kind_ == TYPE_MAP; }
  bool IsMeasureType() const { return kind_ == TYPE_MEASURE; }

  // DEPRECATED, use UsingFeatureV12CivilTimeType() instead.
  //
  // Civil time types are TIME and DATETIME, which are controlled by the
  // language option FEATURE_CIVIL_TIME.
  // Technically, DATE is also a "civil time" type, but it's always available
  // and not controlled by FEATURE_CIVIL_TIME.
  bool IsFeatureV12CivilTimeType() const {
    return kind_ == TYPE_TIME || kind_ == TYPE_DATETIME;
  }

  // ArrayType and StructType will override this function to reflect that
  // TIME or DATETIME appears in the array or the struct.
  //
  // EnumType will return false directly.
  //
  // ProtoType will always return false. The proto itself is always a valid
  // type to pass around as a value, even if it contains civil time fields.
  // Extracting those fields may give an error if FEATURE_CIVIL_TIME is
  // not enabled.
  virtual bool UsingFeatureV12CivilTimeType() const {
    return kind_ == TYPE_TIME || kind_ == TYPE_DATETIME;
  }

  // Return true if the type is DATE, TIME or DATETIME.
  bool IsCivilDateOrTimeType() const {
    return kind_ == TYPE_DATE || kind_ == TYPE_TIME || kind_ == TYPE_DATETIME;
  }

  bool IsGeography() const { return kind_ == TYPE_GEOGRAPHY; }
  bool IsJson() const { return kind_ == TYPE_JSON; }
  bool IsTokenList() const { return kind_ == TYPE_TOKENLIST; }
  bool IsEnum() const { return kind_ == TYPE_ENUM; }
  bool IsArray() const { return kind_ == TYPE_ARRAY; }
  bool IsStruct() const { return kind_ == TYPE_STRUCT; }
  bool IsProto() const { return kind_ == TYPE_PROTO; }
  bool IsStructOrProto() const { return IsStruct() || IsProto(); }
  bool IsRange() const { return kind_ == TYPE_RANGE; }
  bool IsGraphElement() const { return kind_ == TYPE_GRAPH_ELEMENT; }
  bool IsGraphPath() const { return kind_ == TYPE_GRAPH_PATH; }
  bool IsMap() const { return kind_ == TYPE_MAP; }

  bool IsRow() const { return kind_ == TYPE_ROW; }
  virtual bool IsMultiRow() const { return false; }
  virtual bool IsSingleRow() const { return false; }

  // True for ARRAY and RowTypes with `IsJoin()` true.
  bool IsArrayLike() const;

  // Get the element type for types with `IsArrayLike()` true.
  // Internal error for other types.
  absl::StatusOr<const Type*> GetElementType() const;

  bool IsFloatingPoint() const { return IsFloat() || IsDouble(); }
  bool IsNumerical() const {
    switch (kind_) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
      case TYPE_FLOAT:
      case TYPE_DOUBLE:
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
        return true;
      default:
        return false;
    }
  }
  bool IsInteger() const {
    switch (kind_) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
        return true;
      default:
        return false;
    }
  }
  bool IsInteger32() const {
    switch (kind_) {
      case TYPE_INT32:
      case TYPE_UINT32:
        return true;
      default:
        return false;
    }
  }
  bool IsInteger64() const {
    switch (kind_) {
      case TYPE_INT64:
      case TYPE_UINT64:
        return true;
      default:
        return false;
    }
  }
  bool IsSignedInteger() const { return IsInt32() || IsInt64(); }
  bool IsUnsignedInteger() const { return IsUint32() || IsUint64(); }
  bool IsUuid() const { return kind_ == TYPE_UUID; }

  // Simple types are those builtin types that can be represented with just a
  // TypeKind, with no parameters. This exists instead of IsScalarType because
  // enums act more like scalars, but require parameters.
  bool IsSimpleType() const { return IsSimpleType(kind_); }

  // Extended types are defined outside of GoogleSQL codebase. They have
  // TYPE_EXTENDED type kind and their classes inherit googlesql::ExtendedType
  // class.
  bool IsExtendedType() const { return kind_ == TYPE_EXTENDED; }

  // Return this Type cast to the given subclass, or nullptr if this type
  // is not of the requested type.
  virtual const ArrayType* AsArray() const { return nullptr; }
  virtual const StructType* AsStruct() const { return nullptr; }
  virtual const ProtoType* AsProto() const { return nullptr; }
  virtual const EnumType* AsEnum() const { return nullptr; }
  virtual const ExtendedType* AsExtendedType() const { return nullptr; }
  virtual const RangeType* AsRange() const { return nullptr; }
  virtual const GraphElementType* AsGraphElement() const { return nullptr; }
  virtual const GraphPathType* AsGraphPath() const { return nullptr; }
  virtual const MapType* AsMap() const { return nullptr; }
  virtual const MeasureType* AsMeasure() const { return nullptr; }
  virtual const RowType* AsRow() const { return nullptr; }

  // Returns true if the type supports grouping with respect to the
  // 'language_options'. E.g. struct type supports grouping if the
  // FEATURE_GROUP_BY_STRUCT option is enabled.
  // When this returns false and 'type_description' is not null, also returns in
  // 'type_description' a description of the type that does not support
  // grouping. e.g. "DOUBLE", "STRUCT containing DOUBLE", etc.
  // TODO: Make <type_description> required, and require that it
  // is not nullptr (update the contract so that it crashes if it is
  // nullptr).  Also do this for SupportsPartitioning and SupportsOrdering().
  bool SupportsGrouping(const LanguageOptions& language_options,
                        std::string* type_description = nullptr) const;

  // Returns true of type supports partitioning with respect to the
  // 'language_options'. E.g. struct type supports partitioning if the
  // FEATURE_GROUP_BY_STRUCT option is enabled.
  // When this returns false and 'type_description' is not null, also returns in
  // 'type_description' a description of the type that does not support
  // partitioning. e.g. "DOUBLE", "STRUCT containing DOUBLE", etc.
  // TODO: Make <type_description> required, and require that it
  // is not nullptr (update the contract so that it crashes if it is
  // nullptr).  Also do this for SupportsGrouping and SupportsOrdering().
  bool SupportsPartitioning(const LanguageOptions& language_options,
                            std::string* type_description = nullptr) const;

  // Returns true if the type supports ordering with respect to the
  // 'language_options'.  Determines whether the type is supported for
  // an expression in the ORDER BY clause (of the query, in aggregate function
  // arguments, and in analytic function arguments).  Also determines whether
  // the type supports comparison for non-equality operators like '<'.
  // Is also a prerequisite for supporting the type as an argument for functions
  // MIN/MAX and GREATEST/LEAST.
  //
  // When this returns false and 'type_description' is not null, also returns
  // in 'type_description' a description of the type that does not support
  // ordering. e.g. "ARRAY containing STRUCT", etc.
  //
  // Note - the 'language_options' are currently unused.
  //
  // TODO: Require that <type_description> is not nullptr (update the
  // contract so that it crashes if it is nullptr).  Also do this for
  // SupportsGrouping and SupportsPartitioning().
  virtual bool SupportsOrdering(const LanguageOptions& language_options,
                                std::string* type_description) const;
  // Deprecated signature - use SupportsOrdering defined above that takes
  // LanguageOptions.  This version hardcodes LanguageOptions to be NO options,
  // which is almost certainly not what you want.
  ABSL_DEPRECATED(
      "use SupportsOrdering(language_options, type_description) instead")
  bool SupportsOrdering() const;

  // Whether the type is supported in equality operators, i.e. '=', '!=', IN
  // NOT IN, USING and CASE.
  // Note that this means the GoogleSQL type supports equality, but there are
  // LanguageOptions that may restrict this for particular engines.
  virtual bool SupportsEquality() const {
    if (IsGeography()) {
      return false;
    }
    if (IsTokenList()) {
      return false;
    }
    return true;
  }

  // Returns true if the type can be returned from the top level
  // of any query, function, or other surface boundary with respect to
  // 'language_options'. If the type is a compound type, also recursively
  // check its field types. Specifically, this includes:
  //
  // 1) Output column list of ResolvedQueryStmt
  // 2) Output column list from TVFs and Views
  // 3) Column defs in ResolvedCreateTableStmt/ResolvedCreateTableAsSelectStmt
  // 4) Return type of UDFs
  // 5) Returning clause of DML statements
  //
  // When this returns false and 'type_description' is not null, also returns
  // in 'type_description' a description of the type.
  bool SupportsReturning(const LanguageOptions& language_options,
                         std::string* type_description = nullptr) const;

  // Returns true if type supports equality with respect to the
  // 'language_options'. E.g. array type supports equality if the
  // FEATURE_ARRAY_EQUALITY option is enabled.
  virtual bool SupportsEquality(const LanguageOptions& language_options) const;

  // Compare types for equality.  Equal types can be used interchangeably
  // without any casting.
  //
  // This compares structurally inside structs and arrays.
  // For protos and enums, this does proto descriptor pointer comparison only.
  // Two versions of identical descriptors (from different DescriptorPools)
  // will not be considered equal.
  bool Equals(const Type* other_type) const {
    return EqualsImpl(other_type, /*equivalent=*/false);
  }

  // Compare types for equivalence.  Equivalent types can be used
  // interchangeably in a query, but casts will always be added to convert
  // from one to the other.
  //
  // This differs from Equals in that it treats Enums and Protos as equivalent
  // if their full_name() is equal.  Different versions of the same proto
  // or enum are equivalent in a query, but CASTs will be added to do the
  // conversion.
  //
  // Structs with different field names are considered Equivalent if they
  // have the same number of fields and the corresponding fields have
  // Equivalent types.
  bool Equivalent(const Type* other_type) const {
    return EqualsImpl(other_type, /*equivalent=*/true);
  }

  // Hashes a Type using absl::Hash library.
  template <typename H>
  friend H AbslHashValue(H state, const Type& value) {
    value.Hash(absl::HashState::Create(&state));
    return state;
  }

  // Serialize the Type to a fully self-contained protocol buffer into
  // <type_proto>.  Note that the related FileDescriptorSet is serialized into
  // <type_proto>.  Supports Types depending on descriptors from different
  // DescriptorPools by serializing into multiple FileDescriptorSets within
  // <type_proto>.
  absl::Status SerializeToSelfContainedProto(
      TypeProto* type_proto) const;

  // Serialize the Type to protocol buffer form into <type_proto>.
  // The <type_proto> will *not* contain the related FileDescriptorSet.
  // Either <file_descriptors> or <file_descriptor_set> can be NULL.
  // For each FileDescriptor referenced by the Type, the following will
  // occur:
  //
  // 1) If both <file_descriptors> and <file_descriptor_set> are NULL,
  //    then the FileDescriptor is ignored.
  // 2) If only <file_descriptors> is NULL, then the FileDescriptor
  //    is serialized and stored into <file_descriptor_set>
  // 3) If only <file_descriptor_set> is NULL, then the FileDescriptor*
  //    is added to <file_descriptors> if it is not already there.
  // 4) If neither are NULL, then if the FileDescriptor* is not already in
  //    <file_descriptors> then it is added to <file_descriptors> and it
  //    is serialized and stored into <file_descriptor_set> as well.
  //
  // Returns an error if this Type contains proto or enum types that originate
  // from different DescriptorPools. If contained types may span multiple
  // DescriptorPools, then SerializeToProtoAndDistinctFileDescriptors must be
  // used instead.
  absl::Status SerializeToProtoAndFileDescriptors(
      TypeProto* type_proto,
      google::protobuf::FileDescriptorSet* file_descriptor_set = nullptr,
      std::set<const google::protobuf::FileDescriptor*>* file_descriptors =
          nullptr) const;

  // Stores state associated with type serialization.
  struct FileDescriptorEntry {
    // The unique 0-based index of a particular DescriptorPool within the
    // descriptor map. Indices will be in the range [0, map_size - 1].
    int descriptor_set_index = 0;
    // The file descriptors associated with types from a particular
    // DescriptorPool.
    google::protobuf::FileDescriptorSet file_descriptor_set;
    std::set<const google::protobuf::FileDescriptor*> file_descriptors;
  };

  typedef std::map<const google::protobuf::DescriptorPool*,
          std::unique_ptr<FileDescriptorEntry> > FileDescriptorSetMap;

  // Similar to SerializeToProtoAndFileDescriptors, but supports types from
  // distinct DescriptorPools. The provided map is used to store serialized
  // FileDescriptorSets, which can later be deserialized into separate
  // DescriptorPools in order to reconstruct the Type using
  // DeserializeFromProtoUsingExistingPools. The map may be non-empty and may
  // be used across calls to this method in order to serialize multiple types.
  // The map may not be null.
  //
  // Usage:
  // const Type* type1 = ...;
  // const Type* type2 = ...;
  // TypeProto type_proto1;
  // TypeProto type_proto2;
  // // After serialization, for each proto or enum type that was serialized,
  // // all of its associated FileDescriptors will be stored in the map under
  // // an entry for its originating DescriptorPool.
  // FileDescriptorSetMap file_descriptor_set_map;
  // GOOGLESQL_RETURN_IF_ERROR(
  //     type1->SerializeToProtoAndDistinctFileDescriptors(
  //         &type_proto1, &file_descriptor_set_map));
  // GOOGLESQL_RETURN_IF_ERROR(
  //     type2->SerializeToProtoAndDistinctFileDescriptors(
  //         &type_proto2, &file_descriptor_set_map));
  // ...
  // vector<const proto::DescriptorPool*> pools;
  // // Deserialize the FileDescriptorSets within the file_descriptor_set_map
  // // into separate pools, ordered according to descriptor_set_index.
  // ...
  // const Type* deserialized_type1 = nullptr;
  // const Type* deserialized_type2 = nullptr;
  // GOOGLESQL_RETURN_IF_ERROR(
  //     factory.DeserializeFromProtoUsingExistingPools(
  //         type_proto1, pools, &deserialized_type1));
  // GOOGLESQL_RETURN_IF_ERROR(
  //     factory.DeserializeFromProtoUsingExistingPools(
  //         type_proto2, pools, &deserialized_type2));
  absl::Status SerializeToProtoAndDistinctFileDescriptors(
      TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const;

  // Same as the previous, but also optionally takes a limit on total
  // FileDescriptorSet size in <file_descriptor_sets_max_size_bytes>.
  // Returns an error and aborts if this size limit is exceeded by
  // the FileDescriptorSets in the <file_descriptor_set_map>.
  absl::Status SerializeToProtoAndDistinctFileDescriptors(
      TypeProto* type_proto,
      std::optional<int64_t> file_descriptor_sets_max_size_bytes,
      FileDescriptorSetMap* file_descriptor_set_map) const;

  // Returns the SQL name for this type, which in general is not reparseable as
  // part of a query. This is intended for user-facing informational or error
  // messages; for a parseable type name, use TypeName, and for logging, use
  // DebugString. For proto-based types, this just returns the type name, which
  // does not easily distinguish PROTOs from ENUMs.
  // Setting the optional parameter `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated.
  virtual std::string ShortTypeName(ProductMode mode) const;
  virtual std::string ShortTypeName(ProductMode mode,
                                    bool use_external_float32) const;

  // Same as above, but returns a SQL name that is reparseable as part of a
  // query. This is not intended for user-facing informational or error
  // messages.
  // Setting the optional parameter `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated.
  virtual std::string TypeName(ProductMode mode) const = 0;
  virtual std::string TypeName(ProductMode mode,
                               bool use_external_float32_unused) const {
    return TypeName(mode);
  }

  // Same as above, but if <type_modifiers> contains non-empty modifiers, then
  // these modifiers are included with the SQL name for this type. The output is
  // reparseable as part of a query. If <type_modifiers> contains modifiers that
  // are not invalid for the given Type, then an error status will be returned.
  // Setting the optional parameter `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated.
  virtual absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const = 0;
  virtual absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32_unused) const {
    return TypeNameWithModifiers(type_modifiers, mode);
  }

  // Returns the full description of the type without truncation. This should
  // only be used for logging or tests and not for any user-facing messages. For
  // proto-based types, this will return PROTO<name> or ENUM<name>, which are
  // not valid to parse as SQL.
  // If <details> is true, then the description includes full proto descriptors.
  std::string DebugString(bool details = false) const;

  // Returns type printed as capitalized string.
  virtual std::string CapitalizedName() const = 0;

  // Adds capitalized type name to a given string.
  // TODO Remove this method and use DebugString instead.
  std::string AddCapitalizedTypePrefix(absl::string_view input,
                                       bool is_null) const;

  // Check if this type contains a field with the given name.
  enum HasFieldResult {
    HAS_NO_FIELD,        // No field with that name.
    HAS_FIELD,           // Exactly one field with that name.
    HAS_PSEUDO_FIELD,    // Exactly one virtual field, including
                         // * has_X for field X
    HAS_AMBIGUOUS_FIELD  // Multiple fields with that name.
  };

  // If this method returns HAS_FIELD or HAS_PSEUDO_FIELD and <field_id> is
  // non-NULL, then <field_id> is set to
  // - the field index for STRUCTs;
  // - the field tag number for PROTOs;
  // - the property type index for GRAPH_ELEMENTs;
  // <include_pseudo_fields> specifies whether virtual fields should be
  // returned or used for ambiguity check.
  HasFieldResult HasField(const std::string& name, int* field_id = nullptr,
                          bool include_pseudo_fields = true) const {
    return HasFieldImpl(name, field_id, include_pseudo_fields);
  }

  // Return true if this type has any fields.
  // Will return false for structs or protos with zero fields.
  // Any pseudo-fields are not counted. Always returns false for arrays.
  virtual bool HasAnyFields() const { return false; }

  // Returns the component types of this composite type, in a
  // deterministic order. The type must always return its component types in
  // the same order.
  // Otherwise, returns an empty vector.
  //
  // This function is used to create `AnnotationMap`s with the correct
  // structure.
  //
  // ARRAY and RANGE:           have exactly one component type.
  // MAP and GRAPH_PATH:        have 2 component types.
  // STRUCT and GRAPH_ELEMENT:  have N component types.
  // Non-composite, e.g. INT64: have no component types (return an empty list).
  virtual std::vector<const Type*> ComponentTypes() const { return {}; }

  // Returns true if this type is enabled given 'language_options'.
  // Checks for ProductMode, TimestampMode, and supported LanguageFeatures.
  virtual bool IsSupportedType(
      const LanguageOptions& language_options) const = 0;

  static bool IsSimpleType(TypeKind kind);

  static std::string TypeKindToString(TypeKind kind, ProductMode mode,
                                      bool use_external_float32);
  static std::string TypeKindToString(TypeKind kind, ProductMode mode) {
    return TypeKindToString(kind, mode, /*use_external_float32=*/false);
  }
  static std::string TypeKindListToString(const std::vector<TypeKind>& kinds,
                                          ProductMode mode,
                                          bool use_external_float32);
  static std::string TypeKindListToString(const std::vector<TypeKind>& kinds,
                                          ProductMode mode) {
    return TypeKindListToString(kinds, mode, /*use_external_float32=*/false);
  }

  // Returns comma-separated list of names of given <types>. Type name is
  // generated using Type::ShortTypeName(<mode>).
  static std::string TypeListToString(TypeListView types, ProductMode mode,
                                      bool use_external_float32);
  static std::string TypeListToString(TypeListView types, ProductMode mode) {
    return TypeListToString(types, mode, /*use_external_float32=*/false);
  }

  // Returns the type kind if 'type_name' is a simple type in 'mode', assuming
  // all language features are enabled. Returns TYPE_UNKNOWN otherwise.
  // 'type_name' is case-insensitive. Note that we return a simple type only if
  // 'type_name' is its builtin type name rather than an engine-defined alias
  // name for the type.
  static TypeKind ResolveBuiltinTypeNameToKindIfSimple(
      absl::string_view type_name, ProductMode mode);
  // Returns the type kind if 'type_name' is a simple type given
  // 'language_options', or TYPE_UNKNOWN otherwise.
  // 'type_name' is case-insensitive. Note that we return a simple type only if
  // 'type_name' is its builtin type name rather than an engine-defined alias
  // name for the type.
  static TypeKind ResolveBuiltinTypeNameToKindIfSimple(
      absl::string_view type_name, const LanguageOptions& language_options);

  // Functions below this line are for internal use only.

  // Returns an integer identifying relative specificity, where lower values
  // mean more specific.  Specificity details are defined in:
  //   (broken link)
  static int KindSpecificity(TypeKind kind);

  // Compares whether one TypeKind specificity is less than another.
  // REQUIRES: kind1 != TYPE_EXTENDED && kind2 != TYPE_EXTENDED.
  static bool KindSpecificityLess(TypeKind kind1, TypeKind kind2);
  // Compares whether one built-in Type specificity is less than another.
  // REQUIRES: !t1->IsExtended() && !t2->IsExtended().
  static bool TypeSpecificityLess(const Type* t1, const Type* t2);

  // Returns an integer identifying the relative cost of coercing from one type
  // to another.  Always returns a non-negative result.  When considering
  // coercion from type1 to either type2 or type3, we prefer coercing to the
  // type with the lower cost.
  static int GetTypeCoercionCost(TypeKind kind1, TypeKind kind2);

  // The nesting depth of the tree of types (via StructType and ArrayType) below
  // this type. For simple types this is 0.
  virtual int nesting_depth() const { return 0; }

  // Performs common validation for parameterized types. Returns an empty
  // TypeParameters class unless overridden. TypeParameters are resolved based
  // on the input type and validated literals.
  //
  // <type_parameters_values> is the intermediate representation of type
  // parameters as a vector of resolved TypeParameterValues.
  // The output <TypeParameters> class is the final representation
  // of type parameters in the ResolvedAST, storing the resolved type parameters
  // as a TypeParametersProto.
  virtual absl::StatusOr<TypeParameters> ValidateAndResolveTypeParameters(
      const std::vector<TypeParameterValue>& type_parameter_values,
      ProductMode mode) const;

  // Validates resolved type parameters, used in validator.cc. Errors are
  // returned as internal errors and are not intended to be user-visible.
  virtual absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const;

  // Controls for the building of the FileDescriptorSetMap.
  struct BuildFileDescriptorSetMapOptions {
    // If true, FileDescriptorSetMap is populated with the transitive dependency
    // set of the required types and the FileDescriptorSets are built.
    // Otherwise, FileDescriptorSetMap is populated only with the directly
    // required FileDescriptors and the FileDescriptorSets are not initialized.
    bool build_file_descriptor_sets = true;

    // A limit for the size of the FileDescriptorSets. If the sum of the sizes
    // passes this limit, the next attempt to add an element will cause an
    // error.
    std::optional<int64_t> file_descriptor_sets_max_size_bytes = std::nullopt;
  };

 protected:
  // Types can only be created and destroyed by TypeFactory.
  Type(const TypeFactoryBase* factory, TypeKind kind);
  virtual ~Type() = default;

  bool EqualsImpl(const Type* other_type, bool equivalent) const {
    if (this == other_type) {
      return true;
    }
    if (kind() != other_type->kind()) {
      return false;
    }
    return EqualsForSameKind(other_type, equivalent);
  }

  // Hashes the type. Hash is generated based on a type's kind and (if type is
  // not built-in simple type) on a type's parameter.
  absl::HashState Hash(absl::HashState state) const;

  // Hashes the type's parameter of non-simple (parameterized) types. Simple
  // built-in types should not update the hash state.
  virtual absl::HashState HashTypeParameter(absl::HashState state) const = 0;

  // Applicable to ProtoType only, used from ArrayType::EqualElementMultiSet and
  // other call sites that depend on hashing.
  // Returns true if the ProtoType has any floating point fields.
  // Always returns false for instances which are not ProtoType.
  virtual bool HasFloatingPointFields() const;

  // Internal implementation for Serialize methods.  This will append
  // Type information to <type_proto>, so the caller should make sure
  // that <type_proto> has been initialized properly before invoking.
  // <options> controls whether FileDescriptors are deep-scanned and
  // FileDescriptorSets are generated, and also whether there are any limits
  // placed on the size of the FileDescriptorSets.
  virtual absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const = 0;

  // Returns estimated size of memory owned by this type. Note: type can never
  // own another type, only reference (all types are owned by TypeFactory). So,
  // this function never calls GetEstimatedOwnedMemoryBytesSize for other types
  // (such as element types of arrays or field types of structs).
  virtual int64_t GetEstimatedOwnedMemoryBytesSize() const = 0;

  // Type subclasses should return true if and only if the Type's stringified
  // name is included when formatting the ValueContent in verbose debug mode.
  virtual bool VerboseDebugFormatValueContentHasTypeName() const {
    return false;
  };

  // Formatting options that can be provided to FormatValueContent.
  struct FormatValueContentOptions {
    enum class Mode {
      // Should generate a string value to use for debugging purposes.
      // This mode is used by Value::DebugString: please check the comments to
      // this function for more details.
      kDebug = 0,

      // Should generate a SQL literal that can be used as a literal to produce
      // a value with the content compatible to the given ValueContent object.
      // This mode is used by Value::GetSQLLiteral: please check the comments to
      // this function for more details.
      kSQLLiteral,

      // Should generate a SQL expression that produces a value with the given
      // content and belonging to the current type.
      // This mode is used by Value::GetSQL: please check the comments to this
      // function for more details.
      kSQLExpression,
    };

    template <typename Sink>
    friend void AbslStringify(Sink& sink, Mode m) {
      switch (m) {
        case Mode::kDebug:
          sink.Append("kDebug");
          break;
        case Mode::kSQLLiteral:
          sink.Append("kSQLLiteral");
          break;
        case Mode::kSQLExpression:
          sink.Append("kSQLExpression");
          break;
      }
    }

    // The getters below are here mostly for historical reasons: originally
    // internal googlesql::Value formatting functions were using these two flags
    // to figure out which formatting mode is requested. New types should not
    // use them, but should rely on the "mode" field directly.
    bool as_literal() const { return mode != Mode::kSQLExpression; }
    bool add_simple_type_prefix() const { return mode != Mode::kDebug; }

    ProductMode product_mode = ProductMode::PRODUCT_EXTERNAL;

    // Setting `use_external_float32` to true will return
    // FLOAT32 as the type name for TYPE_FLOAT.
    // TODO: Remove `use_external_float32` once all engines are
    // updated.
    bool use_external_float32 = false;

    Mode mode = Mode::kDebug;
    bool verbose = false;  // Used with debug mode only.

    bool force_type_at_top_level = false;
    bool include_array_ordereness = false;
    int indent = 0;

    bool collapse_identical_tokens = false;

    // If set, TokenList attributes are formatted according to this function.
    // If not set, TokenList attributes are printed as decimal integers.
    std::function<void(std::string&, tokens::TextAttribute)>
        format_token_attribute;

    FormatValueContentOptions IncreaseIndent();

    // Number of columns per indentation.
    static const int kIndentStep = 2;
  };

  // List of DebugStringImpl outputs. Used to serve as a stack in
  // DebugStringImpl to protect from stack overflows.
  // Note: SWIG will fail to process this file if we remove a white space
  // between '>' at the TypeOrStringVector definition or use "using" instead of
  // "typedef".
  typedef std::vector<std::variant<const Type*, std::string> >
      TypeOrStringVector;

  // Returns an error status code in case serialized proto Value representation
  // doesn't belong to the current type.
  absl::Status TypeMismatchError(const ValueProto& value_proto) const;

 private:
  // Recursive implementation of SupportsGrouping, which returns in
  // "no_grouping_type" the contained type that made grouping unsupported.
  virtual bool SupportsGroupingImpl(const LanguageOptions& language_options,
                                    const Type** no_grouping_type) const;

  // Recursive implementation of SupportsPartitioning, which returns in
  // "no_partitioning_type" the contained type that made partitioning
  // unsupported.
  virtual bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const;

  // Compares type instances belonging to the same type kind.
  virtual bool EqualsForSameKind(const Type* that, bool equivalent) const = 0;

  // Outputs elements describing type representation for debugging purposes.
  // Type should append these textual elements to debug_string. However,
  // if type references other types, to prevent possibility of stack overflow it
  // needs to push them into the "stack" instead of calling DebugStringImpl
  // directly. If some other textual information (e.g. brackets) need to be
  // added to the output after the type that is pushed into the stack gets
  // processed, this information can be pushed into the stack before this type.
  // Stack elements will eventually be appended to debug_string.
  virtual void DebugStringImpl(bool details, TypeOrStringVector* stack,
                               std::string* debug_string) const = 0;

  // Checks whether type has field of given name. Is called from HasField.
  // `field_id` can be nullptr.
  virtual HasFieldResult HasFieldImpl(const std::string& name, int* field_id,
                                      bool include_pseudo_fields) const {
    return HAS_NO_FIELD;
  }

  // *ValueContent* functions below are used as an interface between
  // googlesql::Value and googlesql::Type to manage Value objects' content.
  //
  // Caveat: all *ValueContent* functions should be used only by
  // googlesql::Value class, which enforces necessary invariants on their
  // parameters before accessing them.
  //
  // Make Value and ContainerType friends, so it can access *ValueContent*
  // functions.
  friend class Value;
  friend class InternalValue;
  friend class ContainerType;
  friend struct HashableValueContentContainerElementIgnoringFloat;

  FRIEND_TEST(TypeTest, FormatValueContentArraySQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentArraySQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentArrayDebugMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructDebugMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructWithAnonymousFieldsDebugMode);
  FRIEND_TEST(MapTest, FormatValueContentSQLLiteralMode);
  FRIEND_TEST(MapTest, FormatValueContentSQLExpressionMode);
  FRIEND_TEST(MapTestFormatValueContentDebugMode, FormatValueContentDebugMode);
  FRIEND_TEST(MapTest, FormatValueContentDebugModeEmptyMap);

  // Copies value's content to another value. Is called when one value is
  // assigned to another. It's expected that content of destination is empty
  // (doesn't contain any valid content that needs to be destructed): if
  // operation is a replacement, it should have been cleaned with
  // ClearValueContent before this call. Default implementation just copies
  // ValueContent's memory.
  virtual void CopyValueContent(const ValueContent& from,
                                ValueContent* to) const;

  // Releases value's content if it owns some memory allocation. This function
  // is called from Value::Clear when value is replaced with another or freed.
  // The value's content lifetime is managed by Value class, which also ensures
  // that ClearValueContent is called only once for each constructed
  // content. Note: this function must not be used outside of the Value
  // destruction context.
  virtual void ClearValueContent(const ValueContent& value) const {}

  // Checks for equality between the content of two values. It should be
  // expected that the first value content belongs to the current type and the
  // second to a type that is equivalent to the current type:
  // this->Equivalent(other_value_type) == true. The pointer to the later type
  // is stored in ValueEqualityCheckOptions::other_value_type. This function
  // should only be called from the Value::EqualsInternal that enforces these
  // assumptions.
  // Note: this function doesn't perform SQL equality.
  virtual bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const = 0;

  // Returns true when <x> is smaller than <y>. The Type of <y> should be
  // equivalent to the current Type (this->Equivalent(other_value_type) == true)
  // and the pointer to the later Type is provided in <other_type>. Note: this
  // function doesn't perform SQL comparison, but is used by Value::LessThan.
  virtual bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                                const Type* other_type) const = 0;

  // Returns memory size allocated by value's content (outside of Value class
  // memory itself).
  virtual uint64_t GetValueContentExternallyAllocatedByteSize(
      const ValueContent& value) const {
    return 0;
  }

  // Hashes the content of the value. This function is called from
  // Value::HashValueInternal. It's not required that generated hash depends on
  // type's information (like, a kind or a parameter), and from performance
  // considerations it should only be based on a value's content (if dependence
  // on a type is important for a caller, caller can use Type's Hash function
  // after HashValueContent).
  virtual absl::HashState HashValueContent(const ValueContent& value,
                                           absl::HashState state) const = 0;

  // Returns a string representation of the value content based on the given
  // formatting options. This function is called from Value::GetSQLInternal,
  // which is used by Value::GetSQL, Value::GetSQLLiteral and Value::DebugString
  // functions. Please check comments to these functions for the details of
  // expected result representation.
  virtual std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const = 0;

  // Serializes the Value into ValueProto protocol buffer.
  virtual absl::Status SerializeValueContent(const ValueContent& value,
                                             ValueProto* value_proto) const = 0;

  // Deserializes a ValueProto into Value.
  virtual absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                               ValueContent* value) const = 0;

  // Make TypeFactory and TypeStore friend classes to provide an access to
  // type_store_ field.
  friend class TypeFactory;
  friend class internal::TypeStore;

  friend class ArrayType;
  friend class StructType;
  friend class RangeType;
  friend class GraphElementType;
  friend class GraphPathType;
  friend class MapType;
  friend class NullableValueContentComparator;
  friend class MeasureType;

  const internal::TypeStore* type_store_;  // Used for lifetime checking only.
  const TypeKind kind_;
};

typedef Type::FileDescriptorSetMap FileDescriptorSetMap;
typedef Type::BuildFileDescriptorSetMapOptions BuildFileDescriptorMapOptions;

#ifndef SWIG
// Provides equality comparison operator for Types. This primarily invokes
// Type::Equals().
class TypeEquals {
 public:
  bool operator()(const Type* type1, const Type* type2) const;
};

// Provides equivalence comparison operator for Types. Uses Type::Equivalent().
class TypeEquivalent {
 public:
  bool operator()(const Type* type1, const Type* type2) const;
};

// Provides hashing operator for Types.
class TypeHash {
 public:
  size_t operator()(const Type* type) const;
};

template <class Hash = TypeHash, class Eq = TypeEquals>
using TypeFlatHashSet = absl::flat_hash_set<const Type*, Hash, Eq>;

template <class ValueT, class Hash = TypeHash, class Eq = TypeEquals>
using TypeFlatHashMap = absl::flat_hash_map<const Type*, ValueT, Hash, Eq>;

#endif  // SWIG

typedef std::pair<TypeKind, TypeKind> TypeKindPair;

// Returns true if this is a valid TypeKind. This is stronger than the proto
// provided TypeKind_IsValid(), as it also returns false for the dummy
// __TypeKind__switch_must_have_a_default__ value.
bool IsValidTypeKind(int kind);

namespace internal {

// Class is used by TypeFactory to store created types.
// TODO: should we consider doing refcounting for Type objects
// instead of refcounting for TypeStores? This requires to add a counter and a
// flag per each non-simple type, but on the other hand, will allow us to have
// more granular memory management and avoid checks for cycles between
// TypeStores.
// TODO: Should TypeStore have a DescriptorPool?
class TypeStore final {
 public:
#ifndef SWIG
  TypeStore(const TypeStore&) = delete;
  TypeStore& operator=(const TypeStore&) = delete;
#endif

 private:
  friend class ::googlesql::TypeFactory;
  friend class ::googlesql::internal::TypeStoreHelper;

  explicit TypeStore(bool keep_alive_while_referenced_from_value);
  ~TypeStore();

  void Ref() const;
  void Unref() const;

  // Use our own ref counter because SimpleReferenceCounted uses int32 for its
  // counter, which could be not enough to count references from all values.
  // Ref count is 1 for type factory that creates it.
  mutable std::atomic<int64_t> ref_count_{1};

  const bool keep_alive_while_referenced_from_value_;

  mutable absl::Mutex mutex_;

  std::vector<const Type*> owned_types_ ABSL_GUARDED_BY(mutex_);

  std::vector<const AnnotationMap*> owned_annotation_maps_
      ABSL_GUARDED_BY(mutex_);

  // Store links to and from TypeStores that this TypeStores depends on.
  // This is used as a sanity check to catch incorrect destruction order.
  mutable absl::flat_hash_set<const TypeStore*> depends_on_factories_
      ABSL_GUARDED_BY(mutex_);
  mutable absl::flat_hash_set<const TypeStore*> factories_depending_on_this_
      ABSL_GUARDED_BY(mutex_);
};

// Helper class to work with TypeStore. These internal helpers are usable only
// in the friend classes.
class TypeStoreHelper {
 private:
  friend class ::googlesql::Type;
  friend class ::googlesql::Value;
  friend class ::googlesql::ValueTest;

  static void RefFromValue(const TypeStore* store);
  static void UnrefFromValue(const TypeStore* store);
  static const TypeStore* GetTypeStore(const TypeFactoryBase* factory);
  static int64_t Test_GetRefCount(const TypeStore* store);
};

}  // namespace internal

// Interface for TypeFactory.
class TypeFactoryBase {
 public:
  explicit TypeFactoryBase(internal::TypeStore* store) : store_(store) {}

  virtual ~TypeFactoryBase() = default;

 private:
  friend class ::googlesql::TypeFactory;
  friend class ::googlesql::internal::TypeStoreHelper;
  internal::TypeStore* store_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_TYPE_H_
