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

#ifndef GOOGLESQL_PUBLIC_TYPES_TYPE_FACTORY_H_
#define GOOGLESQL_PUBLIC_TYPES_TYPE_FACTORY_H_

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/public/annotation.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/graph_path_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/node_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
class Column;
class TypeFactory;
}  // namespace googlesql

ABSL_DECLARE_FLAG(int32_t, googlesql_type_factory_nesting_depth_limit);

namespace googlesql {

class ValueTest;
class Table;

struct TypeFactoryOptions {
  // If this option is enabled, types allocated by a TypeFactory will not be
  // deleted until all Value instances that belong to types created by this
  // TypeFactory get released. Reference counting affects performance, since
  // each Value's set or clear operation comes with atomic
  // increment/decrement operation which could be expensive.
  // If performance is an issue, this option can be disabled: however, in this
  // case, the user needs to ensure that TypeFactory is released only after all
  // Value instances that reference its types are released too. This condition
  // is enforced in debug mode.
  // Caveat: under both cases user is responsible for making sure that the
  // DescriptorPool used to provide proto descriptors for proto and enum types
  // stays alive during the whole lifetime of Value objects belonging to these
  // types.
  bool keep_alive_while_referenced_from_value = true;

  // Maximum nesting depth for types supported by this TypeFactory. Any attempt
  // to create a type with a nesting_depth() greater than this will return an
  // error. If a limit is not set, the GoogleSQL analyzer may create types that
  // it cannot destruct. Use kint32max for no limit (the default).
  // The limit value must be >= 0.
  int nesting_depth_limit =
      absl::GetFlag(FLAGS_googlesql_type_factory_nesting_depth_limit);

  // Disables keep_alive_while_referenced_from_value.
  TypeFactoryOptions& IgnoreValueLifeCycle() {
    keep_alive_while_referenced_from_value = false;
    return *this;
  }
};

namespace internal {  // For internal use only

// Chain of the catalog names that reference TypeProto or TypeEnum. Prepended to
// the type name.
struct CatalogName {
  absl::InlinedVector<std::string, 1> path;
  // Backticked path components.
  const std::string* path_string = nullptr;
};

// Class to keep certain certain functions on TypeFactory internal to googlesql.
class TypeFactoryHelper {
 public:
#ifndef SWIG
  TypeFactoryHelper() = delete;
#endif
  static absl::Status MakeOpaqueEnumType(
      class googlesql::TypeFactory* type_factory,
      const google::protobuf::EnumDescriptor* enum_descriptor, const EnumType** result,
      absl::Span<const std::string> catalog_name_path);
};

}  // namespace internal

// A TypeFactory creates and owns Type objects.
//
// Created Type objects live until the TypeFactory is destroyed, with these
// exceptions:
//  * If keep_alive_while_referenced_from_value is true (the default), then
//  created Types will live longer than the TypeFactory as long as they are
//  referenced by Value objects.
//
// The TypeFactory may return the same Type object from multiple calls that
// request equivalent types.
//
// When a compound Type (array or struct) or an AnnotationMap is constructed
// referring to a Type from a separate TypeFactory, the constructed type may
// refer to the Type from the separate TypeFactory, so that TypeFactory must
// outlive this one.
//
// This class is thread-safe.
class TypeFactory : public TypeFactoryBase {
 public:
  explicit TypeFactory(const TypeFactoryOptions& options);
  TypeFactory() : TypeFactory(TypeFactoryOptions{}) {}
#ifndef SWIG
  TypeFactory(const TypeFactory&) = delete;
  TypeFactory& operator=(const TypeFactory&) = delete;
#endif  // SWIG
  ~TypeFactory() override;

  // Helpers to get simple scalar types directly.
  const Type* get_int32();
  const Type* get_int64();
  const Type* get_uint32();
  const Type* get_uint64();
  const Type* get_string();
  const Type* get_bytes();
  const Type* get_bool();
  const Type* get_float();
  const Type* get_double();
  const Type* get_date();
  const Type* get_timestamp();
  const Type* get_time();
  const Type* get_datetime();
  const Type* get_interval();
  const Type* get_geography();
  const Type* get_numeric();
  const Type* get_bignumeric();
  const Type* get_json();
  const Type* get_tokenlist();
  const Type* get_uuid();

  // Return a Type object for a simple type.  This works for all
  // non-parameterized scalar types.  Enums, arrays, structs and protos must
  // use the parameterized constructors.
  const Type* MakeSimpleType(TypeKind kind);

  // Make an array type.
  // Arrays of arrays are not supported and will fail with an error.
  // If <element_type> is not created by this TypeFactory, the TypeFactory that
  // created the <type> must outlive this TypeFactory.
  absl::Status MakeArrayType(const Type* element_type,
                             const ArrayType** result);
  absl::Status MakeArrayType(const Type* element_type, const Type** result);

  // Make a struct type.
  // The field names must be valid.
  // If StructField.type is not created by this TypeFactory, the TypeFactory
  // that created the type must outlive this TypeFactory.
  absl::Status MakeStructType(absl::Span<const StructType::StructField> fields,
                              const StructType** result);
  absl::Status MakeStructType(absl::Span<const StructType::StructField> fields,
                              const Type** result);
  absl::Status MakeStructTypeFromVector(
      std::vector<StructType::StructField> fields, const StructType** result);
  absl::Status MakeStructTypeFromVector(
      std::vector<StructType::StructField> fields, const Type** result);

  // Make a ROW<Table> type (for a non-join RowType).
  // The `table` must outlive this TypeFactory.
  // `table->FullName()` must be passed as the second argument.
  // This avoids a circular dependency with the Catalog headers.
  absl::Status MakeRowType(const Table* table, const std::string& table_name,
                           const RowType** result);
  absl::Status MakeRowType(const Table* table, const std::string& table_name,
                           const Type** result);

  // Make a ROW<Table> or MULTIROW<Table> for a join RowType.
  // All bound column arguments must be non-empty.
  absl::Status MakeRowType(const Table* table, const std::string& table_name,
                           bool multi_row,
                           std::vector<const Column*> bound_columns,
                           const Table* bound_source_table,
                           std::vector<const Column*> bound_source_columns,
                           const RowType** result);
  absl::Status MakeRowType(const Table* table, const std::string& table_name,
                           bool multi_row,
                           std::vector<const Column*> bound_columns,
                           const Table* bound_source_table,
                           std::vector<const Column*> bound_source_columns,
                           const Type** result);

  // Make a proto type.
  // The <descriptor> must outlive this TypeFactory.
  // The <catalog_name> if provided is prepended to type's FullName.
  //
  // This always constructs a ProtoType, even for protos that are
  // annotated with googlesql.is_struct or googlesql.is_wrapper,
  // which normally indicate the proto should be interpreted as
  // a different type.  Use MakeUnwrappedTypeFromProto instead
  // to get the unwrapped type.
  absl::Status MakeProtoType(
      const google::protobuf::Descriptor* descriptor, const ProtoType** result,
      absl::Span<const std::string> catalog_name_path = {});
  absl::Status MakeProtoType(
      const google::protobuf::Descriptor* descriptor, const Type** result,
      absl::Span<const std::string> catalog_name_path = {});

  // Make a range type.
  // <element_type> must be one of Date, DateTime, or Timestamp.
  // If <element_type> is not created by this TypeFactory, the TypeFactory that
  // created the <type> must outlive this TypeFactory.
  absl::Status MakeRangeType(const Type* element_type,
                             const RangeType** result);
  absl::Status MakeRangeType(const Type* element_type, const Type** result);
  absl::Status MakeRangeType(const google::protobuf::FieldDescriptor* field,
                             const Type** result);

  // Make a map type.
  // <key_type> must support grouping for the type to be supported.
  // <value_type> can be any type.
  ABSL_DEPRECATED("Use MakeMapType(key, value, language_options) instead.")
  absl::StatusOr<const Type*> MakeMapType(const Type* key_type,
                                          const Type* value_type);

  // Make a map type.
  // <key_type> must support grouping for the type to be supported.
  // <value_type> can be any type.
  // <language_options> the language options to use when determining if the type
  // can be created.
  absl::StatusOr<const Type*> MakeMapType(
      const Type* key_type, const Type* value_type,
      const LanguageOptions& language_options);

  // Make a graph element type.
  // `graph_reference` is path to the graph to which this type belongs,
  //  can be used for looking up the property graph in the catalog;
  // `element_kind` must be node or edge;
  // `property_types`:
  //  - property type name is case-insensitive;
  //  - property types with the same name must have same value type;
  //  - duplicate property types are removed:
  //    e.g. {{"a", string}, {"a", string}} `property_types` produces the
  //    same type as {{"a", string}}.
  // If value_types of `property_types` are not created by this TypeFactory,
  // the TypeFactory that created the value_types must outlive this
  // TypeFactory.
  absl::Status MakeGraphElementType(
      absl::Span<const std::string> graph_reference,
      GraphElementType::ElementKind element_kind,
      absl::Span<const GraphElementType::PropertyType> property_types,
      const GraphElementType** result);

  // Make a dynamic graph element type.
  // `graph_reference` is a path to the graph to which this type belongs. It can
  //   be used for looking up the property graph in the catalog;
  // `element_kind` must be node or edge;
  // `static_property_types`:
  //  - property type name is case-insensitive;
  //  - property types with the same name must have same value type;
  //  - duplicate property types are removed:
  //    e.g. {{"a", string}, {"a", string}} `static_property_types` produces the
  //    same type as {{"a", string}};
  //  - static property type shadows dynamic property type with the same name.
  // If value_types of `static_property_types` are not created by this
  // TypeFactory, the TypeFactory that created the value_types must outlive this
  // TypeFactory.
  absl::Status MakeDynamicGraphElementType(
      absl::Span<const std::string> graph_reference,
      GraphElementType::ElementKind element_kind,
      absl::Span<const GraphElementType::PropertyType> static_property_types,
      const GraphElementType** result);

  // Make a graph path type. <node_type> is the supertype of all nodes in the
  // path; <edge_type> is the supertype of all edges in the path.
  absl::Status MakeGraphPathType(const GraphElementType* node_type,
                                 const GraphElementType* edge_type,
                                 const GraphPathType** result);

  // Make a Measure type. `result_type` is the type produced when the Measure is
  // aggregated.
  absl::StatusOr<const Type*> MakeMeasureType(const Type* result_type);

  // Stores the unique copy of an ExtendedType in the TypeFactory. If such
  // extended type already exists in the cache, frees `extended_type` and
  // returns a pointer to existing type. Otherwise, returns a pointer to added
  // type. Type equality is checked with TypeEquals and TypeHash functions.
  // These rely on correct implementations of Type::HashTypeParameter and
  // Type::EqualsForSameKind on the ExtendedType.
  absl::StatusOr<const ExtendedType*> InternalizeExtendedType(
      std::unique_ptr<const ExtendedType> extended_type);

  // Make a googlesql type from a proto, honoring googlesql.is_struct and
  // googlesql.is_wrapper annotations.
  // These annotations allow creating a proto representation of any googlesql
  // type, including structs and arrays, with nullability.
  // Such protos can be created with methods in convert_type_to_proto.h.
  // This method converts protos back to the represented googlesql type.
  absl::Status MakeUnwrappedTypeFromProto(const google::protobuf::Descriptor* message,
                                          const Type** result_type) {
    return MakeUnwrappedTypeFromProto(message, /*use_obsolete_timestamp=*/false,
                                      result_type);
  }
  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the method above.
  ABSL_DEPRECATED("Obsolete timestamp types are deprecated")
  absl::Status MakeUnwrappedTypeFromProto(const google::protobuf::Descriptor* message,
                                          bool use_obsolete_timestamp,
                                          const Type** result_type);

  // Like the method above, but starting from a googlesql::Type.
  // If the Type is not a proto, it will be returned unchanged.
  absl::Status UnwrapTypeIfAnnotatedProto(const Type* input_type,
                                          const Type** result_type) {
    return UnwrapTypeIfAnnotatedProto(
        input_type, /*use_obsolete_timestamp=*/false, result_type);
  }
  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the method above.
  ABSL_DEPRECATED("Obsolete timestamp types are deprecated")
  absl::Status UnwrapTypeIfAnnotatedProto(const Type* input_type,
                                          bool use_obsolete_timestamp,
                                          const Type** result_type);

  // Make an enum type from a protocol buffer EnumDescriptor.
  // The <enum_descriptor> must outlive this TypeFactory.
  // The <catalog_name> if provided is prepended to type's FullName.
  absl::Status MakeEnumType(
      const google::protobuf::EnumDescriptor* enum_descriptor, const EnumType** result,
      absl::Span<const std::string> catalog_name_path = {});
  absl::Status MakeEnumType(
      const google::protobuf::EnumDescriptor* enum_descriptor, const Type** result,
      absl::Span<const std::string> catalog_name_path = {});

  // Get the Type for a proto field.
  // If <ignore_annotations> is false, this looks at format annotations on the
  // field and possibly its parent message to help select the Type. If
  // <ignore_annotations> is true, annotations on the field are not considered
  // and the returned type is that of which GoogleSQL sees before applying any
  // annotations or automatic conversions. This function always ignores (does
  // not unwrap) is_struct and is_wrapper annotations.
  absl::Status GetProtoFieldType(
      bool ignore_annotations, const google::protobuf::FieldDescriptor* field_descr,
      absl::Span<const std::string> catalog_name_path, const Type** type);

  // Get the Type for a proto field.
  // This is the same as the above signature with ignore_annotations = false.
  //
  // NOTE: There is a similar method GetProtoFieldTypeAndDefault in proto_util.h
  // that also extracts the default value.
  absl::Status GetProtoFieldType(
      const google::protobuf::FieldDescriptor* field_descr,
      absl::Span<const std::string> catalog_name_path, const Type** type) {
    return GetProtoFieldType(/*ignore_annotations=*/false, field_descr,
                             catalog_name_path, type);
  }

  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the method above.
  ABSL_DEPRECATED("Obsolete timestamp types are deprecated")
  absl::Status GetProtoFieldType(
      const google::protobuf::FieldDescriptor* field_descr, bool use_obsolete_timestamp,
      absl::Span<const std::string> catalog_name_path, const Type** type);
  ABSL_DEPRECATED("Inline me!")
  absl::Status GetProtoFieldType(const google::protobuf::FieldDescriptor* field_descr,
                                 bool use_obsolete_timestamp,
                                 const Type** type) {
    return GetProtoFieldType(field_descr, use_obsolete_timestamp,
                             /*catalog_name_path=*/{}, type);
  }

  // Deserializes and creates an instance of AnnotationMap from <proto>.
  absl::Status DeserializeAnnotationMap(const AnnotationMapProto& proto,
                                        const AnnotationMap** annotation_map);

  // Takes ownership of <annotation_map> and returns a raw pointer owned by this
  // TypeFactory. The output pointer may be different from the input.
  // `normalize` parameter indicates whether to call normalize on annotation map
  // before transferring ownership or not. Normalize function internally
  // erases the annotation map if it is empty. Set `normalize` to false if the
  // internal annotation map should not be erased.
  absl::StatusOr<const AnnotationMap*> TakeOwnership(
      std::unique_ptr<AnnotationMap> annotation_map, bool normalize = true);

  // Makes a GoogleSQL Type from a self-contained GoogleSQL TypeProto.  The
  // <type_proto> FileDescriptorSets are loaded into the pool.  The <pool>
  // must outlive the TypeFactory.  Will return an error if the
  // FileDescriptorSets cannot be deserialized into a single DescriptorPool,
  // i.e. if type_proto.file_descriptor_set_size() > 1.  For serialized types
  // spanning multiple pools, see
  // DeserializeFromSelfContainedProtoWithDistinctFiles below.
  ABSL_DEPRECATED(
      "Use TypeDeserializer calling "
      "DeserializeFromSelfContainedProtoWithDistinctFiles to populate "
      "DesciptorPools and Deserialize for type deserialization")
  absl::Status DeserializeFromSelfContainedProto(const TypeProto& type_proto,
                                                 google::protobuf::DescriptorPool* pool,
                                                 const Type** type);

  // Similar to the above, but supports types referencing multiple
  // DescriptorPools.  The provided pools must match the number of
  // FileDescriptorSets stored in <type_proto>.  Each FileDescriptorSet from
  // <type_proto> is loaded into the DescriptorPool corresponding to its index.
  ABSL_DEPRECATED(
      "Use TypeDeserializer calling "
      "DeserializeFromSelfContainedProtoWithDistinctFiles to populate "
      "DescriptorPools and Deserialize for type deserialization")
  absl::Status DeserializeFromSelfContainedProtoWithDistinctFiles(
      const TypeProto& type_proto,
      const std::vector<google::protobuf::DescriptorPool*>& pools, const Type** type);

  // Make a GoogleSQL Type from a GoogleSQL TypeProto.  All protos referenced
  // by <type_proto> must already have related descriptors in the <pool>.
  // The <pool> must outlive the TypeFactory.  May only be used with a
  // <type_proto> serialized via Type::SerializeToProtoAndFileDescriptors.
  ABSL_DEPRECATED("Use TypeDeserializer instead")
  absl::Status DeserializeFromProtoUsingExistingPool(
      const TypeProto& type_proto, const google::protobuf::DescriptorPool* pool,
      const Type** type);

  // Similar to the above, but expects that all protos and enums referenced by
  // <type_proto> must have related descriptors in the pool corresponding to
  // the ProtoTypeProto or EnumTypeProto's file_descriptor_set_index. May be
  // used with a <type_proto> serialized via
  // Type::SerializeToProtoAndFileDescriptors or
  // Type::SerializeToProtoAndDistinctFileDescriptors.
  ABSL_DEPRECATED("Use TypeDeserializer instead")
  absl::Status DeserializeFromProtoUsingExistingPools(
      const TypeProto& type_proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      const Type** type);

  // Maximum nesting depth for types supported by this TypeFactory. Any attempt
  // to create a type with a nesting_depth() greater than this will return an
  // error. If a limit is not set, the GoogleSQL analyzer may create types that
  // it cannot destruct. Use kint32max for no limit (the default).
  // The limit value must be >= 0. The default value of this field can be
  // overridden with FLAGS_googlesql_type_factory_nesting_depth_limit.
  int nesting_depth_limit() const;

  // Estimate memory size allocated to store TypeFactory's data in bytes
  int64_t GetEstimatedOwnedMemoryBytesSize() const;

 private:
  // This is only for internal uses.
  absl::Status MakeOpaqueEnumType(
      const google::protobuf::EnumDescriptor* enum_descriptor, const EnumType** result,
      absl::Span<const std::string> catalog_name_path = {});

  // Internal method to make a graph element type.
  // `is_dynamic` indicates whether the graph element type is dynamic.
  absl::Status MakeGraphElementTypeFromVector(
      absl::Span<const std::string> graph_reference,
      GraphElementType::ElementKind element_kind,
      std::vector<GraphElementType::PropertyType> property_types,
      bool is_dynamic, const GraphElementType** result);

  // Add <type> into <owned_types_>.  Templated so it can return the
  // specific subclass of Type.
  template <class TYPE>
  const TYPE* TakeOwnership(const TYPE* type)
      ABSL_LOCKS_EXCLUDED(store_->mutex_);
  template <class TYPE>
  const TYPE* TakeOwnershipLocked(const TYPE* type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);
  template <class TYPE>
  const TYPE* TakeOwnershipLocked(const TYPE* type,
                                  int64_t type_owned_bytes_size)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);

  // Takes ownership of <annotation_map> and updates estimated used memory.
  const AnnotationMap* TakeOwnershipInternal(
      const AnnotationMap* annotation_map);

  // Mark that <other_type>'s factory must outlive <this>.
  void AddDependency(const Type* /*absl_nonnull*/ other_type)
      ABSL_LOCKS_EXCLUDED(store_->mutex_);

  const ProtoType* MakeProtoTypeImpl(
      const google::protobuf::Descriptor* descriptor,
      absl::Span<const std::string> catalog_name_path)
      ABSL_LOCKS_EXCLUDED(store_->mutex_);

  const EnumType* MakeEnumTypeImpl(
      const google::protobuf::EnumDescriptor* descriptor,
      absl::Span<const std::string> catalog_name_path, bool is_opaque)
      ABSL_LOCKS_EXCLUDED(store_->mutex_);

  absl::StatusOr<const Type*> MakeMapTypeImpl(const Type* key_type,
                                              const Type* value_type);

  const ProtoType*& FindOrCreateCachedType(const google::protobuf::Descriptor* descriptor,
                                           const internal::CatalogName* catalog)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);

  const EnumType*& FindOrCreateCachedType(
      const google::protobuf::EnumDescriptor* descriptor,
      const internal::CatalogName* catalog, bool is_opaque)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);

  // Find or create cached catalog name.
  const internal::CatalogName* FindOrCreateCatalogName(
      absl::Span<const std::string> catalog_name_path)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);

  // Get the Type for a proto field from its corresponding TypeKind. For
  // repeated fields, <kind> must be the base TypeKind for the field (i.e., the
  // TypeKind of the field, ignoring repeatedness), which can be obtained by
  // FieldDescriptorToTypeKindBase().
  absl::Status GetProtoFieldTypeWithKind(
      const google::protobuf::FieldDescriptor* field_descr, TypeKind kind,
      absl::Span<const std::string> catalog_name_path, const Type** type);

  // Returns an ArrayType or RangeType.
  template <class TYPE>
  const auto* MakeTypeWithChildElementType(
      const Type* element_type,
      absl::flat_hash_map<const Type*, const TYPE*>& cache)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(store_->mutex_);

  // Implementation of MakeUnwrappedTypeFromProto above that detects invalid use
  // of type annotations with recursive protos by storing all visited message
  // types in 'ancestor_messages'.
  ABSL_DEPRECATED("Obsolete timestamp types are deprecated")
  absl::Status MakeUnwrappedTypeFromProtoImpl(
      const google::protobuf::Descriptor* message, const Type* existing_message_type,
      bool use_obsolete_timestamp, const Type** result_type,
      std::set<const google::protobuf::Descriptor*>* ancestor_messages);

  // Implementation of UnwrapTypeIfAnnotatedProto above that detects invalid use
  // of type annotations with recursive protos by storing all visited message
  // types in 'ancestor_messages'.
  ABSL_DEPRECATED("Obsolete timestamp types are deprecated")
  absl::Status UnwrapTypeIfAnnotatedProtoImpl(
      const Type* input_type, bool use_obsolete_timestamp,
      const Type** result_type,
      std::set<const google::protobuf::Descriptor*>* ancestor_messages);

  friend class internal::TypeFactoryHelper;

  absl::flat_hash_map<const Type*, const ArrayType*> cached_array_types_
      ABSL_GUARDED_BY(store_->mutex_);
  absl::flat_hash_map<const google::protobuf::Descriptor*, const ProtoType*>
      cached_proto_types_ ABSL_GUARDED_BY(store_->mutex_);

  absl::flat_hash_map<const google::protobuf::EnumDescriptor*, const EnumType*>
      cached_enum_types_ ABSL_GUARDED_BY(store_->mutex_);

  absl::flat_hash_map<const Type*, const RangeType*> cached_range_types_
      ABSL_GUARDED_BY(store_->mutex_);

  absl::flat_hash_map<std::pair<const Type*, const Type*>, const Type*>
      cached_map_types_ ABSL_GUARDED_BY(store_->mutex_);

  // The key is a descriptor and a catalog name path.
  absl::flat_hash_map<
      std::pair<const google::protobuf::Descriptor*, const internal::CatalogName*>,
      const ProtoType*>
      cached_proto_types_with_catalog_name_ ABSL_GUARDED_BY(store_->mutex_);

  absl::flat_hash_map<
      std::tuple<const google::protobuf::EnumDescriptor*, const internal::CatalogName*,
                 bool /*is_opaque*/>,
      const EnumType*>
      cached_enum_types_with_extra_attributes_ ABSL_GUARDED_BY(store_->mutex_);

  // The key is a catalog name path.
  absl::node_hash_map<std::string, internal::CatalogName> cached_catalog_names_
      ABSL_GUARDED_BY(store_->mutex_);

  // Cached extended types.
  TypeFlatHashSet<> cached_extended_types_ ABSL_GUARDED_BY(store_->mutex_);

  // Set in constructor and never changed.
  const int nesting_depth_limit_;

  // Stores estimation of how much memory was allocated by instances
  // of types owned by this TypeFactory (in bytes)
  int64_t estimated_memory_used_by_types_;
};

namespace types {
// The following functions do *not* create any new types using the static
// factory.
const Type* Int32Type();
const Type* Int64Type();
const Type* Uint32Type();
const Type* Uint64Type();
const Type* BoolType();
const Type* FloatType();
const Type* DoubleType();
const Type* StringType();
const Type* BytesType();
const Type* DateType();
const Type* TimestampType();
const Type* TimeType();
const Type* DatetimeType();
const Type* IntervalType();
const Type* GeographyType();
const Type* NumericType();
const Type* BigNumericType();
const Type* JsonType();
const Type* TokenListType();
const StructType* EmptyStructType();
const Type* UuidType();

// ArrayTypes
const ArrayType* Int32ArrayType();
const ArrayType* Int64ArrayType();
const ArrayType* Uint32ArrayType();
const ArrayType* Uint64ArrayType();
const ArrayType* BoolArrayType();
const ArrayType* FloatArrayType();
const ArrayType* DoubleArrayType();
const ArrayType* StringArrayType();
const ArrayType* BytesArrayType();
const ArrayType* TimestampArrayType();
const ArrayType* DateArrayType();
const ArrayType* DatetimeArrayType();
const ArrayType* TimeArrayType();
const ArrayType* IntervalArrayType();
const ArrayType* GeographyArrayType();
const ArrayType* NumericArrayType();
const ArrayType* BigNumericArrayType();
const ArrayType* JsonArrayType();
const ArrayType* TokenListArrayType();
const ArrayType* UuidArrayType();

// RangeTypes
const RangeType* DateRangeType();
const RangeType* DatetimeRangeType();
const RangeType* TimestampRangeType();

// Accessor for the GoogleSQL enum Type (functions::DateTimestampPart)
// that represents date parts in function signatures.  Intended
// to be used primarily within the GoogleSQL library, rather than as a
// part of the public GoogleSQL api.
const EnumType* DatePartEnumType();

// Accessor for the GoogleSQL enum Type (functions::NormalizeMode)
// that represents the normalization mode in NORMALIZE and
// NORMALIZE_AND_CASEFOLD.  Intended to be used primarily within the GoogleSQL
// library, rather than as a part of the public GoogleSQL API.
const EnumType* NormalizeModeEnumType();

// Accessor for the GoogleSQL enum Type (functions::RoundingMode)
// that represents the rounding mode to be used as the third optional
// argument of the ROUND function, which determines how the input value
// will be rounded.
// This is an opaque enum type.
const EnumType* RoundingModeEnumType();

// Accessor for the GoogleSQL enum Type
// (functions::ArrayFindEnums::ArrayFindMode) that represents the array find
// mode to be used as the third optional argument of the ARRAY_OFFSET and
// ARRAY_FIND function. When there are multiple array elements that satisfy the
// find condition, the enum controls the behavior of which element to return.
// This is an opaque enum type.
const EnumType* ArrayFindModeEnumType();

// Accessor for the GoogleSQL enum Type
// (functions::DifferentialPrivacyEnums::ReportFormat) that
// represents the report output format for differential privacy functions.
// This is an opaque enum type.
const EnumType* DifferentialPrivacyReportFormatEnumType();

// Accessor for the enum type
// (functions::DifferencialPrivacyEnums::GroupSelectionStrategy) that represents
// different group selection strategies.
// This is an opaque enum type.
const EnumType* DifferentialPrivacyGroupSelectionStrategyEnumType();

// Accessor for the enum type
// (functions::DifferentialPrivacyEnums::CountDistinctContributionBoundingStrategy)
// that represents different contribution bounding strategies for differentially
// private count distinct.
// This is an opaque enum type.
absl::StatusOr<const EnumType*>
DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType();

// Accessor for the enum type (functions::ArrayZipEnums::ArrayZipMode)
// that represents the array zip mode to be used as an optional argument of
// the function ARRAY_ZIP and UNNEST. It decides the row generation behavior
// of ARRAY_ZIP and UNNEST when there are multiple arrays of different
// lengths. This is an opaque enum type.
const EnumType* ArrayZipModeEnumType();

// Accessor for the enum type
// (functions::BitwiseAggEnums::BitwiseAggMode) that represents the bitwise
// aggregation mode to be used as an optional argument of the function
// BIT_AND, BIT_OR, and BIT_XOR. It decides the aggregation behavior when there
// are multiple BYTES values of different lengths. This is an opaque enum type.
const EnumType* BitwiseAggModeEnumType();

// Accessor for the enum type
// (functions::RangeSessionizeEnums::RangeSessionizeMode) that represents the
// sessionize option used as an optional argument of
// RANGE_SESSIONIZE and RANGE_NORMALIZE TVFs. It determines
// sessionization behavior on overlapping/meeting ranges.
// This is an opaque enum type.
const EnumType* RangeSessionizeModeEnumType();

// Accessor for the GoogleSQL enum Type (functions::UnsupportedFields)
// that represents how does TO_JSON handle fields of unsupported types.
const EnumType* UnsupportedFieldsEnumType();

// Accessor for the GoogleSQL enum Type (functions::RankEnums::RankType)
// that specifies how to calculate the relative rank of a value within
// a KLL sketch.
absl::StatusOr<const EnumType*> RankTypeEnumType();

// Returns the opaque enum type, given its SQL name.
absl::StatusOr<const EnumType*> GetOpaqueEnumTypeFromSqlName(
    absl::string_view enum_name);

// Return a type of 'type_kind' if 'type_kind' is a simple type, otherwise
// returns nullptr. This is similar to TypeFactory::MakeSimpleType, but doesn't
// require TypeFactory.
const Type* TypeFromSimpleTypeKind(TypeKind type_kind);

// Returns an array type with element type of 'type_kind' if 'type_kind' is a
// simple type, otherwise returns nullptr.
const ArrayType* ArrayTypeFromSimpleTypeKind(TypeKind type_kind);

// Returns a range type with element type of 'type_kind' if 'type_kind' is a
// valid range type, otherwise returns nullptr.
const RangeType* RangeTypeFromSimpleTypeKind(TypeKind type_kind);

}  // namespace types

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_TYPE_FACTORY_H_
