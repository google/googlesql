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

#include "googlesql/public/types/type_factory.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/annotation.pb.h"
#include "googlesql/public/functions/array_find_mode.pb.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/functions/bitwise_agg_mode.pb.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/functions/range_sessionize_mode.pb.h"
#include "googlesql/public/functions/rank_type.pb.h"
#include "googlesql/public/functions/rounding_mode.pb.h"
#include "googlesql/public/functions/unsupported_fields.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/proto/wire_format_annotation.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/node_hash_map.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

ABSL_FLAG(int32_t, googlesql_type_factory_nesting_depth_limit,
          std::numeric_limits<int32_t>::max(),
          "The maximum nesting depth for types that googlesql::TypeFactory "
          "will allow to be created. Set this to a bounded value to avoid "
          "stack overflows.");

namespace googlesql {

namespace internal {

absl::Status TypeFactoryHelper::MakeOpaqueEnumType(
    TypeFactory* type_factory, const google::protobuf::EnumDescriptor* enum_descriptor,
    const EnumType** result, absl::Span<const std::string> catalog_name_path) {
  return type_factory->MakeOpaqueEnumType(enum_descriptor, result,
                                          catalog_name_path);
}

}  // namespace internal

// The set of types for which the static type factory should be used during
// Array and Map type construction.
static const auto* StaticTypeSet() {
  static const auto* kStaticTypeSet = new absl::flat_hash_set<const Type*>{
      types::Int32Type(),
      types::Int64Type(),
      types::Uint32Type(),
      types::Uint64Type(),
      types::BoolType(),
      types::FloatType(),
      types::DoubleType(),
      types::StringType(),
      types::BytesType(),
      types::TimestampType(),
      types::DateType(),
      types::DatetimeType(),
      types::TimeType(),
      types::IntervalType(),
      types::GeographyType(),
      types::NumericType(),
      types::BigNumericType(),
      types::JsonType(),
      types::TokenListType(),
      types::UuidType(),
  };
  return kStaticTypeSet;
}

// Statically initialize a few commonly used types.
static TypeFactory* s_type_factory() {
  static TypeFactory* s_type_factory = new TypeFactory();
  return s_type_factory;
}

TypeFactory::TypeFactory(const TypeFactoryOptions& options)
    : TypeFactoryBase(new internal::TypeStore(
          options.keep_alive_while_referenced_from_value)),
      nesting_depth_limit_(options.nesting_depth_limit),
      estimated_memory_used_by_types_(0) {
  // We don't want to have to check the depth for simple types, so a depth of
  // 0 must be allowed.
  ABSL_DCHECK_GE(nesting_depth_limit_, 0);
  GOOGLESQL_VLOG(2) << "Created TypeFactory " << store_ << ":\n"
          ;
}

TypeFactory::~TypeFactory() {
#ifndef NDEBUG
  // In debug mode, we check that there shouldn't be any values that reference
  // types from this TypeFactory.
  if (!store_->keep_alive_while_referenced_from_value_ &&
      store_->ref_count_.load(std::memory_order_seq_cst) != 1) {
    ABSL_LOG(ERROR)
        << "Type factory is released while there are still some objects "
           "that reference it";
  }
#endif

  store_->Unref();
}

int TypeFactory::nesting_depth_limit() const { return nesting_depth_limit_; }

int64_t TypeFactory::GetEstimatedOwnedMemoryBytesSize() const {
  // While we don't promise exact size (only estimation), we still lock a
  // mutex here in case we may need protection from side effects of multi
  // threaded accesses during concurrent unit tests. Also, function
  // GetExternallyAllocatedMemoryEstimate doesn't declare thread safety (even
  // though current implementation is safe).
  absl::MutexLock l(&store_->mutex_);
  return sizeof(*this) + sizeof(internal::TypeStore) +
         estimated_memory_used_by_types_ +
         internal::GetExternallyAllocatedMemoryEstimate(store_->owned_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(
             store_->depends_on_factories_) +
         internal::GetExternallyAllocatedMemoryEstimate(
             store_->factories_depending_on_this_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_array_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_proto_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_enum_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_range_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_map_types_) +
         internal::GetExternallyAllocatedMemoryEstimate(
             cached_proto_types_with_catalog_name_) +
         internal::GetExternallyAllocatedMemoryEstimate(
             cached_enum_types_with_extra_attributes_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_catalog_names_) +
         internal::GetExternallyAllocatedMemoryEstimate(cached_extended_types_);
}

template <class TYPE>
const TYPE* TypeFactory::TakeOwnership(const TYPE* type) {
  const int64_t type_owned_bytes_size =
      type->GetEstimatedOwnedMemoryBytesSize();
  absl::MutexLock l(&store_->mutex_);
  return TakeOwnershipLocked(type, type_owned_bytes_size);
}

template <class TYPE>
const TYPE* TypeFactory::TakeOwnershipLocked(const TYPE* type) {
  return TakeOwnershipLocked(type, type->GetEstimatedOwnedMemoryBytesSize());
}

template <class TYPE>
const TYPE* TypeFactory::TakeOwnershipLocked(const TYPE* type,
                                             int64_t type_owned_bytes_size) {
  ABSL_DCHECK_EQ(type->type_store_, store_);
  ABSL_DCHECK_GT(type_owned_bytes_size, 0);
  store_->owned_types_.push_back(type);
  estimated_memory_used_by_types_ += type_owned_bytes_size;
  return type;
}

template <class TYPE>
const auto* TypeFactory::MakeTypeWithChildElementType(
    const Type* element_type,
    absl::flat_hash_map<const Type*, const TYPE*>& cache) {
  auto& cached_result = cache[element_type];
  if (cached_result == nullptr) {
    cached_result = TakeOwnershipLocked(new TYPE(this, element_type));
  }
  return cached_result;
}

const Type* TypeFactory::get_int32() { return types::Int32Type(); }
const Type* TypeFactory::get_int64() { return types::Int64Type(); }
const Type* TypeFactory::get_uint32() { return types::Uint32Type(); }
const Type* TypeFactory::get_uint64() { return types::Uint64Type(); }
const Type* TypeFactory::get_string() { return types::StringType(); }
const Type* TypeFactory::get_bytes() { return types::BytesType(); }
const Type* TypeFactory::get_bool() { return types::BoolType(); }
const Type* TypeFactory::get_float() { return types::FloatType(); }
const Type* TypeFactory::get_double() { return types::DoubleType(); }
const Type* TypeFactory::get_date() { return types::DateType(); }
const Type* TypeFactory::get_timestamp() { return types::TimestampType(); }
const Type* TypeFactory::get_time() { return types::TimeType(); }
const Type* TypeFactory::get_datetime() { return types::DatetimeType(); }
const Type* TypeFactory::get_interval() { return types::IntervalType(); }
const Type* TypeFactory::get_geography() { return types::GeographyType(); }
const Type* TypeFactory::get_numeric() { return types::NumericType(); }
const Type* TypeFactory::get_bignumeric() { return types::BigNumericType(); }
const Type* TypeFactory::get_json() { return types::JsonType(); }
const Type* TypeFactory::get_tokenlist() { return types::TokenListType(); }
const Type* TypeFactory::get_uuid() { return types::UuidType(); }

const Type* TypeFactory::MakeSimpleType(TypeKind kind) {
  ABSL_CHECK(Type::IsSimpleType(kind))
      << TypeKind_Name(kind) << " is not a simple type";
  const Type* type = types::TypeFromSimpleTypeKind(kind);
  ABSL_CHECK(type != nullptr);
  return type;
}

absl::Status TypeFactory::MakeArrayType(const Type* element_type,
                                        const ArrayType** result) {
  if (this != s_type_factory() && StaticTypeSet()->contains(element_type)) {
    return s_type_factory()->MakeArrayType(element_type, result);
  }

  *result = nullptr;
  AddDependency(element_type);
  if (element_type->IsArray()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Array of array types are not supported";
  }

  const int depth_limit = nesting_depth_limit();
  if (element_type->nesting_depth() + 1 > depth_limit) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Array type would exceed nesting depth limit of " << depth_limit;
  }
  absl::MutexLock lock(&store_->mutex_);
  *result = MakeTypeWithChildElementType(element_type, cached_array_types_);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeArrayType(const Type* element_type,
                                        const Type** result) {
  return MakeArrayType(element_type,
                       reinterpret_cast<const ArrayType**>(result));
}

absl::Status TypeFactory::MakeStructType(
    absl::Span<const StructType::StructField> fields,
    const StructType** result) {
  std::vector<StructType::StructField> new_fields(fields.begin(), fields.end());
  return MakeStructTypeFromVector(std::move(new_fields), result);
}

absl::Status TypeFactory::MakeStructType(
    absl::Span<const StructType::StructField> fields, const Type** result) {
  return MakeStructType(fields, reinterpret_cast<const StructType**>(result));
}

absl::Status TypeFactory::MakeStructTypeFromVector(
    std::vector<StructType::StructField> fields, const StructType** result) {
  *result = nullptr;
  const int depth_limit = nesting_depth_limit();
  int max_nesting_depth = 0;
  for (const StructType::StructField& field : fields) {
    const int nesting_depth = field.type->nesting_depth();
    max_nesting_depth = std::max(max_nesting_depth, nesting_depth);
    if (ABSL_PREDICT_FALSE(nesting_depth + 1 > depth_limit)) {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "Struct type would exceed nesting depth limit of "
             << depth_limit;
    }
    AddDependency(field.type);
  }
  // We calculate <max_nesting_depth> in the previous loop. We also need to
  // increment it to take into account the struct itself.
  *result = TakeOwnership(
      new StructType(this, std::move(fields), max_nesting_depth + 1));
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeStructTypeFromVector(
    std::vector<StructType::StructField> fields, const Type** result) {
  return MakeStructTypeFromVector(std::move(fields),
                                  reinterpret_cast<const StructType**>(result));
}

const ProtoType*& TypeFactory::FindOrCreateCachedType(
    const google::protobuf::Descriptor* descriptor,
    const internal::CatalogName* catalog) {
  if (catalog == nullptr)
    return cached_proto_types_[descriptor];
  else
    return cached_proto_types_with_catalog_name_[std::make_pair(descriptor,
                                                                catalog)];
}

const EnumType*& TypeFactory::FindOrCreateCachedType(
    const google::protobuf::EnumDescriptor* descriptor,
    const internal::CatalogName* catalog, bool is_opaque) {
  if (catalog == nullptr && !is_opaque) {
    return cached_enum_types_[descriptor];
  } else {
    return cached_enum_types_with_extra_attributes_[std::make_tuple(
        descriptor, catalog, is_opaque)];
  }
}

const ProtoType* TypeFactory::MakeProtoTypeImpl(
    const google::protobuf::Descriptor* descriptor,
    absl::Span<const std::string> catalog_name_path) {
  absl::MutexLock lock(&store_->mutex_);

  const internal::CatalogName* cached_catalog =
      FindOrCreateCatalogName(catalog_name_path);

  const ProtoType*& cached_type =
      FindOrCreateCachedType(descriptor, cached_catalog);

  if (cached_type == nullptr) {
    cached_type =
        TakeOwnershipLocked(new ProtoType(this, descriptor, cached_catalog));
  }
  return cached_type;
}

const EnumType* TypeFactory::MakeEnumTypeImpl(
    const google::protobuf::EnumDescriptor* descriptor,
    absl::Span<const std::string> catalog_name_path, bool is_opaque) {
  absl::MutexLock lock(&store_->mutex_);

  const internal::CatalogName* cached_catalog =
      FindOrCreateCatalogName(catalog_name_path);

  const EnumType*& cached_type =
      FindOrCreateCachedType(descriptor, cached_catalog, is_opaque);

  if (cached_type == nullptr) {
    cached_type = TakeOwnershipLocked(
        new EnumType(this, descriptor, cached_catalog, is_opaque));
  }
  return cached_type;
}

const internal::CatalogName* TypeFactory::FindOrCreateCatalogName(
    absl::Span<const std::string> catalog_name_path) {
  if (catalog_name_path.empty()) return nullptr;

  auto [it, was_inserted] = cached_catalog_names_.try_emplace(
      IdentifierPathToString(catalog_name_path), internal::CatalogName());

  // node_hash_map provides pointer stability for both key and value.
  internal::CatalogName* catalog_name = &it->second;

  if (was_inserted) {
    catalog_name->path_string = &it->first;
    catalog_name->path.reserve(catalog_name_path.size());
    absl::c_copy(catalog_name_path, std::back_inserter(catalog_name->path));
  }

  return catalog_name;
}

absl::Status TypeFactory::MakeProtoType(
    const google::protobuf::Descriptor* descriptor, const ProtoType** result,
    absl::Span<const std::string> catalog_name_path) {
  *result = MakeProtoTypeImpl(descriptor, catalog_name_path);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeProtoType(
    const google::protobuf::Descriptor* descriptor, const Type** result,
    absl::Span<const std::string> catalog_name_path) {
  *result = MakeProtoTypeImpl(descriptor, catalog_name_path);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeEnumType(
    const google::protobuf::EnumDescriptor* enum_descriptor, const EnumType** result,
    absl::Span<const std::string> catalog_name_path) {
  GOOGLESQL_RET_CHECK_NE(enum_descriptor, nullptr);
  *result =
      MakeEnumTypeImpl(enum_descriptor, catalog_name_path, /*is_opaque=*/false);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeEnumType(
    const google::protobuf::EnumDescriptor* enum_descriptor, const Type** result,
    absl::Span<const std::string> catalog_name_path) {
  GOOGLESQL_RET_CHECK_NE(enum_descriptor, nullptr);
  *result =
      MakeEnumTypeImpl(enum_descriptor, catalog_name_path, /*is_opaque=*/false);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeOpaqueEnumType(
    const google::protobuf::EnumDescriptor* enum_descriptor, const EnumType** result,
    absl::Span<const std::string> catalog_name_path) {
  *result =
      MakeEnumTypeImpl(enum_descriptor, catalog_name_path, /*is_opaque=*/true);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeRangeType(const Type* element_type,
                                        const RangeType** result) {
  if (!RangeType::IsValidElementType(element_type)) {
    // TODO: Add a product mode to TypeFactoryOptions and use that
    // here.
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Unsupported type: RANGE<"
           << element_type->ShortTypeName(PRODUCT_EXTERNAL)
           << "> is not supported";
  }

  static const auto* kStaticRangeTypeSet = new absl::flat_hash_set<const Type*>{
      types::TimestampType(),
      types::DateType(),
      types::DatetimeType(),
  };
  ABSL_DCHECK(kStaticRangeTypeSet->contains(element_type));
  if (this != s_type_factory()) {
    return s_type_factory()->MakeRangeType(element_type, result);
  }

  *result = nullptr;
  AddDependency(element_type);

  const int depth_limit = nesting_depth_limit();
  if (element_type->nesting_depth() + 1 > depth_limit) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Range type would exceed nesting depth limit of " << depth_limit;
  }
  absl::MutexLock lock(&store_->mutex_);
  *result = MakeTypeWithChildElementType(element_type, cached_range_types_);
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeRangeType(const Type* element_type,
                                        const Type** result) {
  return MakeRangeType(element_type,
                       reinterpret_cast<const RangeType**>(result));
}

absl::Status TypeFactory::MakeRangeType(const google::protobuf::FieldDescriptor* field,
                                        const Type** result) {
  const Type* element_type = nullptr;
  const FieldFormat::Format format = ProtoType::GetFormatAnnotation(field);
  switch (format) {
    case FieldFormat::RANGE_DATES_ENCODED:
      element_type = types::DateType();
      break;
    case FieldFormat::RANGE_DATETIMES_ENCODED:
      element_type = types::DatetimeType();
      break;
    case FieldFormat::RANGE_TIMESTAMPS_ENCODED:
      element_type = types::TimestampType();
      break;
    default:
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "Unsupported field format for RANGE: " << field->DebugString();
  }
  GOOGLESQL_RETURN_IF_ERROR(MakeRangeType(element_type, result));
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeRowType(const Table* table,
                                      const std::string& table_name,
                                      const RowType** result) {
  *result = nullptr;
  GOOGLESQL_RET_CHECK(table != nullptr);

  *result = TakeOwnership(new RowType(this, table, table_name));
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeRowType(const Table* table,
                                      const std::string& table_name,
                                      const Type** result) {
  return MakeRowType(table, table_name,
                     reinterpret_cast<const RowType**>(result));
}

absl::Status TypeFactory::MakeRowType(
    const Table* table, const std::string& table_name, bool multi_row,
    std::vector<const Column*> bound_columns, const Table* bound_source_table,
    std::vector<const Column*> bound_source_columns, const RowType** result) {
  GOOGLESQL_RET_CHECK(table != nullptr);

  // When making a join RowType, we also construct and bind in a corresponding
  // non-join RowType as its element_type.  This is owned by the same
  // TypeFactory.
  const RowType* element_type =
      TakeOwnership(new RowType(this, table, table_name));

  *result = TakeOwnership(new RowType(
      this, table, table_name, multi_row, std::move(bound_columns),
      bound_source_table, std::move(bound_source_columns), element_type));
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeRowType(
    const Table* table, const std::string& table_name, bool multi_row,
    std::vector<const Column*> bound_columns, const Table* bound_source_table,
    std::vector<const Column*> bound_source_columns, const Type** result) {
  return MakeRowType(table, table_name, multi_row, std::move(bound_columns),
                     bound_source_table, std::move(bound_source_columns),
                     reinterpret_cast<const RowType**>(result));
}

absl::StatusOr<const Type*> TypeFactory::MakeMapTypeImpl(
    const Type* key_type, const Type* value_type) {
  if (this != s_type_factory() && StaticTypeSet()->contains(key_type) &&
      StaticTypeSet()->contains(value_type)) {
    return s_type_factory()->MakeMapTypeImpl(key_type, value_type);
  }

  AddDependency(key_type);
  AddDependency(value_type);

  const int depth_limit = nesting_depth_limit();
  if (std::max(key_type->nesting_depth(), value_type->nesting_depth()) + 1 >
      depth_limit) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Map type would exceed nesting depth limit of " << depth_limit;
  }

  // Cannot use TypeFactory::MakeTypeWithChildElementType here because we have a
  // pair of types.
  absl::MutexLock lock(&store_->mutex_);
  auto type_pair = std::make_pair(key_type, value_type);
  auto it = cached_map_types_.find(type_pair);
  if (it == cached_map_types_.end()) {
    auto [inserted_it, _] = cached_map_types_.insert(
        {type_pair,
         TakeOwnershipLocked(new MapType(this, key_type, value_type))});
    it = inserted_it;
  }
  return it->second;
}

namespace {

// Determines if a given input for `key` and `value` will create a valid map
// type, with error messages.
static absl::Status IsValidMapTypeKeyAndValue(
    const LanguageOptions& language_options, const Type* key_type,
    const Type* value_type) {
  if (!language_options.LanguageFeatureEnabled(FEATURE_MAP_TYPE)) {
    return absl::InvalidArgumentError("MAP datatype is not supported");
  }

  std::string no_grouping_type;
  if (!key_type->SupportsGrouping(language_options, &no_grouping_type)) {
    return absl::InvalidArgumentError(
        absl::StrCat("MAP key type ", no_grouping_type, " is not groupable"));
  }
  return absl::OkStatus();
}

}  // namespace

ABSL_DEPRECATED("Use MakeMapType(key, value, language_options) instead.")
absl::StatusOr<const Type*> TypeFactory::MakeMapType(const Type* key_type,
                                                     const Type* value_type) {
  return this->MakeMapTypeImpl(key_type, value_type);
}

absl::StatusOr<const Type*> TypeFactory::MakeMapType(
    const Type* key_type, const Type* value_type,
    const LanguageOptions& language_options) {
  GOOGLESQL_RETURN_IF_ERROR(
      IsValidMapTypeKeyAndValue(language_options, key_type, value_type));
  return MakeMapTypeImpl(key_type, value_type);
}

absl::Status TypeFactory::MakeGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> property_types,
    const GraphElementType** result) {
  return MakeGraphElementTypeFromVector(
      graph_reference, element_kind,
      {property_types.begin(), property_types.end()},
      /*is_dynamic=*/false, result);
}

absl::Status TypeFactory::MakeGraphElementTypeFromVector(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    std::vector<GraphElementType::PropertyType> property_types, bool is_dynamic,
    const GraphElementType** result) {
  *result = nullptr;
  if (graph_reference.empty()) {
    return absl::InvalidArgumentError("Graph reference cannot be empty");
  }
  absl::flat_hash_set<GraphElementType::PropertyType> property_type_set;
  absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash, googlesql_base::StringViewCaseEqual>
      property_type_names;
  const int depth_limit = nesting_depth_limit();
  int max_nesting_depth = 0;
  for (GraphElementType::PropertyType& property_type : property_types) {
    const int nesting_depth = property_type.value_type->nesting_depth();
    max_nesting_depth = std::max(max_nesting_depth, nesting_depth);
    if (ABSL_PREDICT_FALSE(nesting_depth + 1 > depth_limit)) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Graph element type would exceed nesting depth limit of "
             << depth_limit;
    }
    if (property_type.name.empty()) {
      return absl::InvalidArgumentError("Property type name cannot be empty");
    }
    const auto [itr, inserted] =
        property_type_set.insert(std::move(property_type));
    if (inserted && !property_type_names.emplace(itr->name).second) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Inconsistent property: " << itr->name;
    }
    AddDependency(itr->value_type);
  }

  absl::MutexLock lock(&store_->mutex_);
  const internal::GraphReference* cached_graph_reference =
      FindOrCreateCatalogName(graph_reference);
  *result = TakeOwnershipLocked(new GraphElementType(
      cached_graph_reference, element_kind, this, std::move(property_type_set),
      max_nesting_depth + 1, is_dynamic));
  return absl::OkStatus();
}

absl::Status TypeFactory::MakeDynamicGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> static_property_types,
    const GraphElementType** result) {
  return MakeGraphElementTypeFromVector(
      graph_reference, element_kind,
      {static_property_types.begin(), static_property_types.end()},
      /*is_dynamic=*/true, result);
}

absl::Status TypeFactory::MakeGraphPathType(const GraphElementType* node_type,
                                            const GraphElementType* edge_type,
                                            const GraphPathType** result) {
  if (node_type == nullptr) {
    return absl::InvalidArgumentError("Node type cannot be null");
  }
  if (edge_type == nullptr) {
    return absl::InvalidArgumentError("Edge type cannot be null");
  }
  if (node_type->element_kind() != GraphElementType::kNode) {
    return absl::InvalidArgumentError("Node type must be a node");
  }
  if (edge_type->element_kind() != GraphElementType::kEdge) {
    return absl::InvalidArgumentError("Edge type must be an edge");
  }
  GOOGLESQL_RET_CHECK(absl::c_equal(node_type->graph_reference(),
                          edge_type->graph_reference(), googlesql_base::CaseEqual))
      << "Node and edge types must have the same graph reference";
  *result = nullptr;
  const int depth_limit = nesting_depth_limit();
  int max_nesting_depth = 0;
  max_nesting_depth = std::max(max_nesting_depth, node_type->nesting_depth());
  AddDependency(node_type);
  max_nesting_depth = std::max(max_nesting_depth, edge_type->nesting_depth());
  AddDependency(edge_type);

  if (ABSL_PREDICT_FALSE(max_nesting_depth + 1 > depth_limit)) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Graph path type would exceed nesting depth limit of "
           << depth_limit;
  }

  absl::MutexLock lock(&store_->mutex_);
  *result = TakeOwnershipLocked(
      new GraphPathType(this, node_type, edge_type, max_nesting_depth + 1));
  return absl::OkStatus();
}

absl::StatusOr<const Type*> TypeFactory::MakeMeasureType(
    const Type* result_type) {
  const int depth_limit = nesting_depth_limit();
  if (result_type->nesting_depth() + 1 > depth_limit) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Measure type would exceed nesting depth limit of "
           << depth_limit;
  }

  if (this != s_type_factory() && StaticTypeSet()->contains(result_type)) {
    return s_type_factory()->MakeMeasureType(result_type);
  }

  AddDependency(result_type);

  // Not cached as every MeasureType is unique for now.
  absl::MutexLock l(&store_->mutex_);
  return TakeOwnershipLocked(new MeasureType(this, result_type));
}

absl::StatusOr<const ExtendedType*> TypeFactory::InternalizeExtendedType(
    std::unique_ptr<const ExtendedType> extended_type) {
  GOOGLESQL_RET_CHECK(extended_type);
  GOOGLESQL_RET_CHECK_EQ(extended_type->type_store_, store_);

  absl::MutexLock lock(&store_->mutex_);
  auto [it, inserted] = cached_extended_types_.emplace(extended_type.get());
  if (!inserted) {
    // Type is already present in the cache. Return existing type, so
    // `extended_type` will be freed.
    return (*it)->AsExtendedType();
  }

  // We've just added the type to `cached_extended_types_`, so add it to the
  // list of types for removal.
  return TakeOwnershipLocked(extended_type.release());
}

absl::Status TypeFactory::MakeUnwrappedTypeFromProto(
    const google::protobuf::Descriptor* message, bool use_obsolete_timestamp,
    const Type** result_type) {
  std::set<const google::protobuf::Descriptor*> ancestor_messages;
  return MakeUnwrappedTypeFromProtoImpl(
      message, nullptr /* existing_message_type */, use_obsolete_timestamp,
      result_type, &ancestor_messages);
}

absl::Status TypeFactory::UnwrapTypeIfAnnotatedProto(
    const Type* input_type, bool use_obsolete_timestamp,
    const Type** result_type) {
  std::set<const google::protobuf::Descriptor*> ancestor_messages;
  return UnwrapTypeIfAnnotatedProtoImpl(input_type, use_obsolete_timestamp,
                                        result_type, &ancestor_messages);
}

absl::Status TypeFactory::UnwrapTypeIfAnnotatedProtoImpl(
    const Type* input_type, bool use_obsolete_timestamp,
    const Type** result_type,
    std::set<const google::protobuf::Descriptor*>* ancestor_messages) {
  if (input_type->IsArray()) {
    // For Arrays, unwrap the element type inside the array.
    const ArrayType* array_type = input_type->AsArray();
    const Type* element_type = array_type->element_type();
    const Type* unwrapped_element_type;
    // If this is an array<proto>, unwrap the proto element if necessary.
    if (element_type->IsProto()) {
      GOOGLESQL_RETURN_IF_ERROR(MakeUnwrappedTypeFromProtoImpl(
          element_type->AsProto()->descriptor(), element_type,
          use_obsolete_timestamp, &unwrapped_element_type, ancestor_messages));
      GOOGLESQL_RETURN_IF_ERROR(MakeArrayType(unwrapped_element_type, &array_type));
    }
    *result_type = array_type;
    return absl::OkStatus();
  } else if (input_type->IsProto()) {
    return MakeUnwrappedTypeFromProtoImpl(input_type->AsProto()->descriptor(),
                                          input_type, use_obsolete_timestamp,
                                          result_type, ancestor_messages);
  } else {
    *result_type = input_type;
    return absl::OkStatus();
  }
}

absl::Status TypeFactory::MakeUnwrappedTypeFromProtoImpl(
    const google::protobuf::Descriptor* message, const Type* existing_message_type,
    bool use_obsolete_timestamp, const Type** result_type,
    std::set<const google::protobuf::Descriptor*>* ancestor_messages) {
  if (!ancestor_messages->insert(message).second) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Invalid proto " << message->full_name()
           << " has type annotations but is recursive";
  }
  // Always erase 'message' before returning so 'ancestor_messages' contains
  // only ancestors of the current message being unwrapped.
  auto cleanup = ::absl::MakeCleanup(
      [message, ancestor_messages] { ancestor_messages->erase(message); });
  absl::Status return_status;
  if (ProtoType::GetIsWrapperAnnotation(message)) {
    // If we have googlesql.is_wrapper, unwrap the proto and return the type
    // of the contained field.
    if (message->field_count() != 1) {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "Proto " << message->full_name()
             << " is invalid because it has googlesql.is_wrapper annotation"
                " but does not have exactly one field";
    }
    const google::protobuf::FieldDescriptor* proto_field = message->field(0);
    const Type* field_type;
    GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldType(proto_field, use_obsolete_timestamp,
                                      /*catalog_name_path=*/{}, &field_type));
    GOOGLESQL_RET_CHECK_EQ(field_type->IsArray(), proto_field->is_repeated());
    if (!proto_field->options().GetExtension(googlesql::is_raw_proto)) {
      return_status = UnwrapTypeIfAnnotatedProtoImpl(
          field_type, use_obsolete_timestamp, result_type, ancestor_messages);
    } else {
      *result_type = field_type;
    }
  } else if (ProtoType::GetIsStructAnnotation(message)) {
    // If we have googlesql.is_struct, convert this proto to a struct type.
    std::vector<StructType::StructField> struct_fields;
    for (int i = 0; i < message->field_count(); ++i) {
      const google::protobuf::FieldDescriptor* proto_field = message->field(i);
      const Type* field_type;
      GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldType(proto_field, use_obsolete_timestamp,
                                        /*catalog_name_path=*/{}, &field_type));
      if (!proto_field->options().GetExtension(googlesql::is_raw_proto)) {
        const Type* unwrapped_field_type;
        GOOGLESQL_RETURN_IF_ERROR(UnwrapTypeIfAnnotatedProtoImpl(
            field_type, use_obsolete_timestamp, &unwrapped_field_type,
            ancestor_messages));
        field_type = unwrapped_field_type;
      }

      absl::string_view name = proto_field->name();
      if (ProtoType::HasStructFieldName(proto_field)) {
        name = ProtoType::GetStructFieldName(proto_field);
      }

      struct_fields.emplace_back(std::string(name), field_type);
    }
    return_status = MakeStructType(struct_fields, result_type);
  } else if (existing_message_type != nullptr) {
    // Use the message_type we already have allocated.
    ABSL_DCHECK(existing_message_type->IsProto());
    ABSL_DCHECK_EQ(message->full_name(),
              existing_message_type->AsProto()->descriptor()->full_name());
    *result_type = existing_message_type;
    return_status = absl::OkStatus();
  } else {
    return_status = MakeProtoType(message, result_type);
  }
  return return_status;
}

absl::Status TypeFactory::GetProtoFieldTypeWithKind(
    const google::protobuf::FieldDescriptor* field_descr, TypeKind kind,
    absl::Span<const std::string> catalog_name_path, const Type** type) {
  if (Type::IsSimpleType(kind)) {
    *type = MakeSimpleType(kind);
  } else if (kind == TYPE_RANGE) {
    GOOGLESQL_RETURN_IF_ERROR(MakeRangeType(field_descr, type));
  } else if (kind == TYPE_ENUM) {
    const EnumType* enum_type;
    GOOGLESQL_RETURN_IF_ERROR(
        MakeEnumType(field_descr->enum_type(), &enum_type, catalog_name_path));
    *type = enum_type;
  } else if (kind == TYPE_PROTO) {
    GOOGLESQL_RETURN_IF_ERROR(
        MakeProtoType(field_descr->message_type(), type, catalog_name_path));
  } else {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "Unsupported type found: "
           << Type::TypeKindToString(kind, PRODUCT_INTERNAL);
  }
  if (field_descr->options().GetExtension(googlesql::is_measure)) {
    GOOGLESQL_ASSIGN_OR_RETURN(*type, MakeMeasureType(*type));
  }
  if (field_descr->is_repeated()) {
    const ArrayType* array_type;
    GOOGLESQL_RETURN_IF_ERROR(MakeArrayType(*type, &array_type));
    *type = array_type;
  }

  return absl::OkStatus();
}

absl::Status TypeFactory::GetProtoFieldType(
    bool ignore_annotations, const google::protobuf::FieldDescriptor* field_descr,
    absl::Span<const std::string> catalog_name_path, const Type** type) {
  TypeKind kind;
  GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKindBase(ignore_annotations,
                                                           field_descr, &kind));
  GOOGLESQL_RETURN_IF_ERROR(
      GetProtoFieldTypeWithKind(field_descr, kind, catalog_name_path, type));
  if (GOOGLESQL_DEBUG_MODE) {
    // For testing, make sure the TypeKinds we get from
    // FieldDescriptorToTypeKind match the Types returned by this method.
    TypeKind computed_type_kind;
    GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKind(
        ignore_annotations, field_descr, &computed_type_kind));
    GOOGLESQL_RET_CHECK_EQ((*type)->kind(), computed_type_kind)
        << (*type)->DebugString() << "\n"
        << field_descr->DebugString();
  }
  return absl::OkStatus();
}

absl::Status TypeFactory::GetProtoFieldType(
    const google::protobuf::FieldDescriptor* field_descr, bool use_obsolete_timestamp,
    absl::Span<const std::string> catalog_name_path, const Type** type) {
  TypeKind kind;
  GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKindBase(
      field_descr, use_obsolete_timestamp, &kind));

  GOOGLESQL_RETURN_IF_ERROR(
      GetProtoFieldTypeWithKind(field_descr, kind, catalog_name_path, type));
  if (GOOGLESQL_DEBUG_MODE) {
    // For testing, make sure the TypeKinds we get from
    // FieldDescriptorToTypeKind match the Types returned by this method.
    TypeKind computed_type_kind;
    GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKind(
        field_descr, use_obsolete_timestamp, &computed_type_kind));
    GOOGLESQL_RET_CHECK_EQ((*type)->kind(), computed_type_kind)
        << (*type)->DebugString() << "\n"
        << field_descr->DebugString();
  }

  return absl::OkStatus();
}

absl::StatusOr<const AnnotationMap*> TypeFactory::TakeOwnership(
    std::unique_ptr<AnnotationMap> annotation_map, bool normalize) {
  // TODO: look up in cache and return deduped AnnotationMap
  // pointer.
  GOOGLESQL_RET_CHECK(annotation_map != nullptr);
  if (normalize) {
    annotation_map->Normalize();
  }
  return TakeOwnershipInternal(annotation_map.release());
}

absl::Status TypeFactory::DeserializeAnnotationMap(
    const AnnotationMapProto& proto, const AnnotationMap** annotation_map) {
  *annotation_map = nullptr;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnnotationMap> deserialized_annotation_map,
                   AnnotationMap::Deserialize(proto));
  *annotation_map =
      TakeOwnershipInternal(deserialized_annotation_map.release());
  return absl::OkStatus();
}

const AnnotationMap* TypeFactory::TakeOwnershipInternal(
    const AnnotationMap* annotation_map) {
  absl::MutexLock lock(&store_->mutex_);
  store_->owned_annotation_maps_.push_back(annotation_map);
  estimated_memory_used_by_types_ +=
      annotation_map->GetEstimatedOwnedMemoryBytesSize();
  return annotation_map;
}

absl::Status TypeFactory::DeserializeFromProtoUsingExistingPool(
    const TypeProto& type_proto, const google::protobuf::DescriptorPool* pool,
    const Type** type) {
  return DeserializeFromProtoUsingExistingPools(type_proto, {pool}, type);
}

absl::Status TypeFactory::DeserializeFromProtoUsingExistingPools(
    const TypeProto& type_proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    const Type** type) {
  *type = nullptr;

  GOOGLESQL_ASSIGN_OR_RETURN(*type,
                   TypeDeserializer(this, pools).Deserialize(type_proto));

  return absl::OkStatus();
}

absl::Status TypeFactory::DeserializeFromSelfContainedProto(
    const TypeProto& type_proto, google::protobuf::DescriptorPool* pool,
    const Type** type) {
  if (type_proto.file_descriptor_set_size() > 1) {
    return MakeSqlError()
           << "DeserializeFromSelfContainedProto cannot be used to deserialize "
              "types that rely on multiple FileDescriptorSets. Use "
              "DeserializeFromSelfContainedProtoWithDistinctFiles instead";
  }
  return DeserializeFromSelfContainedProtoWithDistinctFiles(type_proto, {pool},
                                                            type);
}

absl::Status TypeFactory::DeserializeFromSelfContainedProtoWithDistinctFiles(
    const TypeProto& type_proto,
    const std::vector<google::protobuf::DescriptorPool*>& pools, const Type** type) {
  GOOGLESQL_RETURN_IF_ERROR(
      TypeDeserializer::DeserializeDescriptorPoolsFromSelfContainedProto(
          type_proto, pools));

  GOOGLESQL_ASSIGN_OR_RETURN(*type,
                   TypeDeserializer(this, pools).Deserialize(type_proto));

  return absl::OkStatus();
}

bool IsValidTypeKind(int kind) {
  return TypeKind_IsValid(kind) &&
         kind != __TypeKind__switch_must_have_a_default__;
}

namespace {

static const Type* s_int32_type() {
  static const Type* s_int32_type =
      new SimpleType(s_type_factory(), TYPE_INT32);
  return s_int32_type;
}

static const Type* s_int64_type() {
  static const Type* s_int64_type =
      new SimpleType(s_type_factory(), TYPE_INT64);
  return s_int64_type;
}

static const Type* s_uint32_type() {
  static const Type* s_uint32_type =
      new SimpleType(s_type_factory(), TYPE_UINT32);
  return s_uint32_type;
}

static const Type* s_uint64_type() {
  static const Type* s_uint64_type =
      new SimpleType(s_type_factory(), TYPE_UINT64);
  return s_uint64_type;
}

static const Type* s_bool_type() {
  static const Type* s_bool_type = new SimpleType(s_type_factory(), TYPE_BOOL);
  return s_bool_type;
}

static const Type* s_float_type() {
  static const Type* s_float_type =
      new SimpleType(s_type_factory(), TYPE_FLOAT);
  return s_float_type;
}

static const Type* s_double_type() {
  static const Type* s_double_type =
      new SimpleType(s_type_factory(), TYPE_DOUBLE);
  return s_double_type;
}

static const Type* s_string_type() {
  static const Type* s_string_type =
      new SimpleType(s_type_factory(), TYPE_STRING);
  return s_string_type;
}

static const Type* s_bytes_type() {
  static const Type* s_bytes_type =
      new SimpleType(s_type_factory(), TYPE_BYTES);
  return s_bytes_type;
}

static const Type* s_timestamp_type() {
  static const Type* s_timestamp_type =
      new SimpleType(s_type_factory(), TYPE_TIMESTAMP);
  return s_timestamp_type;
}

static const Type* s_date_type() {
  static const Type* s_date_type = new SimpleType(s_type_factory(), TYPE_DATE);
  return s_date_type;
}

static const Type* s_time_type() {
  static const Type* s_time_type = new SimpleType(s_type_factory(), TYPE_TIME);
  return s_time_type;
}

static const Type* s_datetime_type() {
  static const Type* s_datetime_type =
      new SimpleType(s_type_factory(), TYPE_DATETIME);
  return s_datetime_type;
}

static const Type* s_interval_type() {
  static const Type* s_interval_type =
      new SimpleType(s_type_factory(), TYPE_INTERVAL);
  return s_interval_type;
}

static const Type* s_geography_type() {
  static const Type* s_geography_type =
      new SimpleType(s_type_factory(), TYPE_GEOGRAPHY);
  return s_geography_type;
}

static const Type* s_numeric_type() {
  static const Type* s_numeric_type =
      new SimpleType(s_type_factory(), TYPE_NUMERIC);
  return s_numeric_type;
}

static const Type* s_bignumeric_type() {
  static const Type* s_bignumeric_type =
      new SimpleType(s_type_factory(), TYPE_BIGNUMERIC);
  return s_bignumeric_type;
}

static const Type* s_json_type() {
  static const Type* s_json_type = new SimpleType(s_type_factory(), TYPE_JSON);
  return s_json_type;
}

static const Type* s_tokenlist_type() {
  static const Type* s_tokenlist_type =
      new SimpleType(s_type_factory(), TYPE_TOKENLIST);
  return s_tokenlist_type;
}

static const EnumType* s_date_part_enum_type() {
  static const EnumType* s_date_part_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(s_type_factory()->MakeEnumType(
        functions::DateTimestampPart_descriptor(), &enum_type));
    return enum_type;
  }();
  return s_date_part_enum_type;
}

static const EnumType* s_normalize_mode_enum_type() {
  static const EnumType* s_normalize_mode_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(s_type_factory()->MakeEnumType(
        functions::NormalizeMode_descriptor(), &enum_type));
    return enum_type;
  }();
  return s_normalize_mode_enum_type;
}

static const EnumType* s_rounding_mode_enum_type() {
  static const EnumType* s_rounding_mode_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(), functions::RoundingMode_descriptor(), &enum_type,
        /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_rounding_mode_enum_type;
}

static const EnumType* s_unsupported_fields_enum_type() {
  static const EnumType* s_unsupported_fields_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(),
        functions::UnsupportedFieldsEnum::UnsupportedFields_descriptor(),
        &enum_type,
        /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_unsupported_fields_enum_type;
}

static const Type* s_uuid_type() {
  static const Type* s_uuid_type = new SimpleType(s_type_factory(), TYPE_UUID);
  return s_uuid_type;
}

static const EnumType* GetArrayFindModeEnumType() {
  static const EnumType* s_array_find_mode_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(), functions::ArrayFindEnums::ArrayFindMode_descriptor(),
        &enum_type, /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_array_find_mode_enum_type;
}

static const EnumType* GetDifferentialPrivacyReportFormatEnumType() {
  static const EnumType* s_differential_privacy_report_format_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(),
        functions::DifferentialPrivacyEnums::ReportFormat_descriptor(),
        &enum_type, /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_differential_privacy_report_format_enum_type;
}

static const EnumType* GetDifferentialPrivacyGroupSelectionStrategyEnumType() {
  static const EnumType*
      s_differential_privacy_group_selection_strategy_enum_type = [] {
        const EnumType* enum_type;
        GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
            s_type_factory(),
            functions::DifferentialPrivacyEnums::
                GroupSelectionStrategy_descriptor(),
            &enum_type, /*catalog_name_path=*/{}));
        return enum_type;
      }();
  return s_differential_privacy_group_selection_strategy_enum_type;
}

static const EnumType* GetRangeSessionizeModeEnumType() {
  static const EnumType* s_range_sessionize_option_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(),
        functions::RangeSessionizeEnums::RangeSessionizeMode_descriptor(),
        &enum_type, /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_range_sessionize_option_enum_type;
}

static const EnumType* GetBitwiseAggModeEnumType() {
  static const EnumType* s_bitwise_agg_mode_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(),
        functions::BitwiseAggEnums::BitwiseAggMode_descriptor(), &enum_type,
        /*catalog_name_path=*/{}));
    return enum_type;
  }();
  return s_bitwise_agg_mode_enum_type;
}

static const StructType* s_empty_struct_type() {
  static const StructType* s_empty_struct_type = [] {
    const StructType* type;
    GOOGLESQL_CHECK_OK(s_type_factory()->MakeStructType({}, &type));
    return type;
  }();
  return s_empty_struct_type;
}

static const ArrayType* MakeArrayType(const Type* element_type) {
  const ArrayType* array_type;
  GOOGLESQL_CHECK_OK(s_type_factory()->MakeArrayType(element_type, &array_type));
  return array_type;
}

static const ArrayType* s_int32_array_type() {
  static const ArrayType* s_int32_array_type =
      MakeArrayType(s_type_factory()->get_int32());
  return s_int32_array_type;
}

static const ArrayType* s_int64_array_type() {
  static const ArrayType* s_int64_array_type =
      MakeArrayType(s_type_factory()->get_int64());
  return s_int64_array_type;
}

static const ArrayType* s_uint32_array_type() {
  static const ArrayType* s_uint32_array_type =
      MakeArrayType(s_type_factory()->get_uint32());
  return s_uint32_array_type;
}

static const ArrayType* s_uint64_array_type() {
  static const ArrayType* s_uint64_array_type =
      MakeArrayType(s_type_factory()->get_uint64());
  return s_uint64_array_type;
}

static const ArrayType* s_bool_array_type() {
  static const ArrayType* s_bool_array_type =
      MakeArrayType(s_type_factory()->get_bool());
  return s_bool_array_type;
}

static const ArrayType* s_float_array_type() {
  static const ArrayType* s_float_array_type =
      MakeArrayType(s_type_factory()->get_float());
  return s_float_array_type;
}

static const ArrayType* s_double_array_type() {
  static const ArrayType* s_double_array_type =
      MakeArrayType(s_type_factory()->get_double());
  return s_double_array_type;
}

static const ArrayType* s_string_array_type() {
  static const ArrayType* s_string_array_type =
      MakeArrayType(s_type_factory()->get_string());
  return s_string_array_type;
}

static const ArrayType* s_bytes_array_type() {
  static const ArrayType* s_bytes_array_type =
      MakeArrayType(s_type_factory()->get_bytes());
  return s_bytes_array_type;
}

static const ArrayType* s_timestamp_array_type() {
  static const ArrayType* s_timestamp_array_type =
      MakeArrayType(s_type_factory()->get_timestamp());
  return s_timestamp_array_type;
}

static const ArrayType* s_date_array_type() {
  static const ArrayType* s_date_array_type =
      MakeArrayType(s_type_factory()->get_date());
  return s_date_array_type;
}

static const ArrayType* s_datetime_array_type() {
  static const ArrayType* s_datetime_array_type =
      MakeArrayType(s_type_factory()->get_datetime());
  return s_datetime_array_type;
}

static const ArrayType* s_time_array_type() {
  static const ArrayType* s_time_array_type =
      MakeArrayType(s_type_factory()->get_time());
  return s_time_array_type;
}

static const ArrayType* s_interval_array_type() {
  static const ArrayType* s_interval_array_type =
      MakeArrayType(s_type_factory()->get_interval());
  return s_interval_array_type;
}

static const ArrayType* s_geography_array_type() {
  static const ArrayType* s_geography_array_type =
      MakeArrayType(s_type_factory()->get_geography());
  return s_geography_array_type;
}

static const ArrayType* s_numeric_array_type() {
  static const ArrayType* s_numeric_array_type =
      MakeArrayType(s_type_factory()->get_numeric());
  return s_numeric_array_type;
}

static const ArrayType* s_bignumeric_array_type() {
  static const ArrayType* s_bignumeric_array_type =
      MakeArrayType(s_type_factory()->get_bignumeric());
  return s_bignumeric_array_type;
}

static const ArrayType* s_json_array_type() {
  static const ArrayType* s_json_array_type =
      MakeArrayType(s_type_factory()->get_json());
  return s_json_array_type;
}

static const ArrayType* s_tokenlist_array_type() {
  static const ArrayType* s_tokenlist_array_type =
      MakeArrayType(s_type_factory()->get_tokenlist());
  return s_tokenlist_array_type;
}

static const ArrayType* s_uuid_array_type() {
  static const ArrayType* s_uuid_array_type =
      MakeArrayType(s_type_factory()->get_uuid());
  return s_uuid_array_type;
}

static const EnumType* GetArrayZipModeEnumType() {
  static const EnumType* s_array_zip_mode_enum_type = [] {
    const EnumType* enum_type;
    GOOGLESQL_CHECK_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(  // Crash OK
        s_type_factory(), functions::ArrayZipEnums::ArrayZipMode_descriptor(),
        &enum_type, {}));
    return enum_type;
  }();
  return s_array_zip_mode_enum_type;
}

template <typename Enum>
struct EnumTraits;

template <>
struct EnumTraits<functions::DifferentialPrivacyEnums::
                      CountDistinctContributionBoundingStrategy> {
  static const google::protobuf::EnumDescriptor* descriptor() {
    return functions::DifferentialPrivacyEnums::
        CountDistinctContributionBoundingStrategy_descriptor();
  }
};

template <>
struct EnumTraits<functions::RankTypeEnums::RankType> {
  static const google::protobuf::EnumDescriptor* descriptor() {
    return functions::RankTypeEnums::RankType_descriptor();
  }
};

template <typename Enum>
absl::StatusOr<const EnumType*> GetEnumType() {
  // Need a pointer, because `StatusOr` is not trivially deconstructible.
  static const absl::StatusOr<const EnumType*>* s_enum_type =
      new absl::StatusOr<const EnumType*>(
          []() -> absl::StatusOr<const EnumType*> {
            const EnumType* enum_type;
            GOOGLESQL_RETURN_IF_ERROR(internal::TypeFactoryHelper::MakeOpaqueEnumType(
                s_type_factory(), EnumTraits<Enum>::descriptor(), &enum_type,
                /*catalog_name_path=*/{}));
            return enum_type;
          }());
  return *s_enum_type;
}

}  // namespace

namespace types {

const Type* Int32Type() { return s_int32_type(); }
const Type* Int64Type() { return s_int64_type(); }
const Type* Uint32Type() { return s_uint32_type(); }
const Type* Uint64Type() { return s_uint64_type(); }
const Type* BoolType() { return s_bool_type(); }
const Type* FloatType() { return s_float_type(); }
const Type* DoubleType() { return s_double_type(); }
const Type* StringType() { return s_string_type(); }
const Type* BytesType() { return s_bytes_type(); }
const Type* DateType() { return s_date_type(); }
const Type* TimestampType() { return s_timestamp_type(); }
const Type* TimeType() { return s_time_type(); }
const Type* DatetimeType() { return s_datetime_type(); }
const Type* IntervalType() { return s_interval_type(); }
const Type* GeographyType() { return s_geography_type(); }
const Type* NumericType() { return s_numeric_type(); }
const Type* BigNumericType() { return s_bignumeric_type(); }
const Type* JsonType() { return s_json_type(); }
const Type* TokenListType() { return s_tokenlist_type(); }
const StructType* EmptyStructType() { return s_empty_struct_type(); }
const EnumType* DatePartEnumType() { return s_date_part_enum_type(); }
const EnumType* NormalizeModeEnumType() { return s_normalize_mode_enum_type(); }
const EnumType* RoundingModeEnumType() { return s_rounding_mode_enum_type(); }
const EnumType* ArrayFindModeEnumType() { return GetArrayFindModeEnumType(); }
const EnumType* DifferentialPrivacyReportFormatEnumType() {
  return GetDifferentialPrivacyReportFormatEnumType();
}
const EnumType* DifferentialPrivacyGroupSelectionStrategyEnumType() {
  return GetDifferentialPrivacyGroupSelectionStrategyEnumType();
}

absl::StatusOr<const EnumType*>
DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType() {
  return GetEnumType<functions::DifferentialPrivacyEnums::
                         CountDistinctContributionBoundingStrategy>();
}
const EnumType* RangeSessionizeModeEnumType() {
  return GetRangeSessionizeModeEnumType();
}
const EnumType* UnsupportedFieldsEnumType() {
  return s_unsupported_fields_enum_type();
}
const Type* UuidType() { return s_uuid_type(); }

const ArrayType* Int32ArrayType() { return s_int32_array_type(); }
const ArrayType* Int64ArrayType() { return s_int64_array_type(); }
const ArrayType* Uint32ArrayType() { return s_uint32_array_type(); }
const ArrayType* Uint64ArrayType() { return s_uint64_array_type(); }
const ArrayType* BoolArrayType() { return s_bool_array_type(); }
const ArrayType* FloatArrayType() { return s_float_array_type(); }
const ArrayType* DoubleArrayType() { return s_double_array_type(); }
const ArrayType* StringArrayType() { return s_string_array_type(); }
const ArrayType* BytesArrayType() { return s_bytes_array_type(); }

const ArrayType* TimestampArrayType() { return s_timestamp_array_type(); }

const ArrayType* DateArrayType() { return s_date_array_type(); }

const ArrayType* DatetimeArrayType() { return s_datetime_array_type(); }

const ArrayType* TimeArrayType() { return s_time_array_type(); }

const ArrayType* IntervalArrayType() { return s_interval_array_type(); }

const ArrayType* GeographyArrayType() { return s_geography_array_type(); }

const ArrayType* NumericArrayType() { return s_numeric_array_type(); }

const ArrayType* BigNumericArrayType() { return s_bignumeric_array_type(); }

const ArrayType* JsonArrayType() { return s_json_array_type(); }

const ArrayType* TokenListArrayType() { return s_tokenlist_array_type(); }

const ArrayType* UuidArrayType() { return s_uuid_array_type(); }

const Type* TypeFromSimpleTypeKind(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_INT32:
      return Int32Type();
    case TYPE_INT64:
      return Int64Type();
    case TYPE_UINT32:
      return Uint32Type();
    case TYPE_UINT64:
      return Uint64Type();
    case TYPE_BOOL:
      return BoolType();
    case TYPE_FLOAT:
      return FloatType();
    case TYPE_DOUBLE:
      return DoubleType();
    case TYPE_STRING:
      return StringType();
    case TYPE_BYTES:
      return BytesType();
    case TYPE_TIMESTAMP:
      return TimestampType();
    case TYPE_DATE:
      return DateType();
    case TYPE_TIME:
      return TimeType();
    case TYPE_DATETIME:
      return DatetimeType();
    case TYPE_INTERVAL:
      return IntervalType();
    case TYPE_GEOGRAPHY:
      return GeographyType();
    case TYPE_NUMERIC:
      return NumericType();
    case TYPE_BIGNUMERIC:
      return BigNumericType();
    case TYPE_JSON:
      return JsonType();
    case TYPE_TOKENLIST:
      return TokenListType();
    case TYPE_UUID:
      return UuidType();
    default:
      GOOGLESQL_VLOG(1) << "Could not build static Type from type: "
              << Type::TypeKindToString(type_kind, PRODUCT_INTERNAL);
      return nullptr;
  }
}

const ArrayType* ArrayTypeFromSimpleTypeKind(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_INT32:
      return Int32ArrayType();
    case TYPE_INT64:
      return Int64ArrayType();
    case TYPE_UINT32:
      return Uint32ArrayType();
    case TYPE_UINT64:
      return Uint64ArrayType();
    case TYPE_BOOL:
      return BoolArrayType();
    case TYPE_FLOAT:
      return FloatArrayType();
    case TYPE_DOUBLE:
      return DoubleArrayType();
    case TYPE_STRING:
      return StringArrayType();
    case TYPE_BYTES:
      return BytesArrayType();
    case TYPE_TIMESTAMP:
      return TimestampArrayType();

    case TYPE_DATE:
      return DateArrayType();
    case TYPE_TIME:
      return TimeArrayType();
    case TYPE_DATETIME:
      return DatetimeArrayType();
    case TYPE_INTERVAL:
      return IntervalArrayType();
    case TYPE_GEOGRAPHY:
      return GeographyArrayType();
    case TYPE_NUMERIC:
      return NumericArrayType();
    case TYPE_BIGNUMERIC:
      return BigNumericArrayType();
    case TYPE_JSON:
      return JsonArrayType();
    case TYPE_TOKENLIST:
      return TokenListArrayType();
    case TYPE_UUID:
      return UuidArrayType();
    default:
      GOOGLESQL_VLOG(1) << "Could not build static ArrayType from type: "
              << Type::TypeKindToString(type_kind, PRODUCT_INTERNAL);
      return nullptr;
  }
}

const EnumType* ArrayZipModeEnumType() { return GetArrayZipModeEnumType(); }

const EnumType* BitwiseAggModeEnumType() { return GetBitwiseAggModeEnumType(); }

absl::StatusOr<const EnumType*> RankTypeEnumType() {
  return GetEnumType<functions::RankTypeEnums::RankType>();
}

absl::StatusOr<const EnumType*> GetOpaqueEnumTypeFromSqlName(
    absl::string_view enum_name) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      const EnumType* count_distinct_contribution_bounding_strategy_enum_type,
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType());  // NOLINT
  GOOGLESQL_ASSIGN_OR_RETURN(const EnumType* rank_type_enum_type,
                   types::RankTypeEnumType());

  static const auto* kStaticEnumMap =
      new absl::flat_hash_map<absl::string_view, const EnumType*,
                              googlesql_base::StringViewCaseHash,
                              googlesql_base::StringViewCaseEqual>{
          {"DIFFERENTIAL_PRIVACY_REPORT_FORMAT",
           types::DifferentialPrivacyReportFormatEnumType()},
          {"DIFFERENTIAL_PRIVACY_GROUP_SELECTION_STRATEGY",
           types::DifferentialPrivacyGroupSelectionStrategyEnumType()},
          {"DIFFERENTIAL_PRIVACY_COUNT_DISTINCT_CONTRIBUTION_BOUNDING_STRATEGY",
           count_distinct_contribution_bounding_strategy_enum_type},
          {"ROUNDING_MODE", types::RoundingModeEnumType()},
          {"ARRAY_FIND_MODE", types::ArrayFindModeEnumType()},
          {"RANGE_SESSIONIZE_MODE", types::RangeSessionizeModeEnumType()},
          {"ARRAY_ZIP_MODE", types::ArrayZipModeEnumType()},
          {"BITWISE_AGG_MODE", types::BitwiseAggModeEnumType()},
          {"UNSUPPORTED_FIELDS", types::UnsupportedFieldsEnumType()},
          {"RANK_TYPE", rank_type_enum_type},
      };
  return googlesql_base::FindPtrOrNull(*kStaticEnumMap, enum_name);
}

static const RangeType* MakeRangeType(const Type* element_type) {
  const RangeType* range_type;
  absl::Status status =
      s_type_factory()->MakeRangeType(element_type, &range_type);
  GOOGLESQL_DCHECK_OK(status);
  return range_type;
}

static const RangeType* s_date_range_type() {
  static const RangeType* s_date_range_type =
      MakeRangeType(s_type_factory()->get_date());
  return s_date_range_type;
}

static const RangeType* s_datetime_range_type() {
  static const RangeType* s_datetime_range_type =
      MakeRangeType(s_type_factory()->get_datetime());
  return s_datetime_range_type;
}

static const RangeType* s_timestamp_range_type() {
  static const RangeType* s_timestamp_range_type =
      MakeRangeType(s_type_factory()->get_timestamp());
  return s_timestamp_range_type;
}

const RangeType* DateRangeType() { return s_date_range_type(); }

const RangeType* DatetimeRangeType() { return s_datetime_range_type(); }

const RangeType* TimestampRangeType() { return s_timestamp_range_type(); }

const RangeType* RangeTypeFromSimpleTypeKind(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_TIMESTAMP:
      return TimestampRangeType();
    case TYPE_DATE:
      return DateRangeType();
    case TYPE_DATETIME:
      return DatetimeRangeType();
    default:
      GOOGLESQL_VLOG(1) << "Could not build static RangeType from type: "
              << Type::TypeKindToString(type_kind, PRODUCT_INTERNAL);
      return nullptr;
  }
}

}  // namespace types

void TypeFactory::AddDependency(const Type* /*absl_nonnull*/ other_type) {
  ABSL_DCHECK_NE(other_type, nullptr);
  const internal::TypeStore* other_store = other_type->type_store_;

  // Do not add a dependency if the other factory is the same as this factory or
  // is the static factory (since the static factory is never destroyed).
  if (other_store == store_ || other_store == s_type_factory()->store_) return;

  {
    absl::MutexLock l(&store_->mutex_);
    if (!googlesql_base::InsertIfNotPresent(&store_->depends_on_factories_, other_store)) {
      return;  // Already had it.
    }
    GOOGLESQL_VLOG(2) << "Added dependency from TypeFactory " << this << " to "
            << other_store << " which owns the type "
            << other_type->DebugString() << ":\n"
            ;

    // This detects trivial cycles between two TypeFactories.  It won't detect
    // longer cycles, so those won't give this error message, but the
    // destructor error will still fire because no destruction order is safe.
    if (store_->factories_depending_on_this_.contains(other_store)) {
      ABSL_LOG(ERROR) << "Created cyclical dependency between TypeFactories, "
                     "which is not legal because there can be no safe "
                     "destruction order";
    }
  }
  {
    absl::MutexLock l(&other_store->mutex_);
    if (googlesql_base::InsertIfNotPresent(&other_store->factories_depending_on_this_,
                                store_)) {
      if (other_store->keep_alive_while_referenced_from_value_) {
        other_store->Ref();
      }
    }
  }
}

}  // namespace googlesql
