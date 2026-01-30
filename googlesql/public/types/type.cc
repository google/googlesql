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

#include "googlesql/public/types/type.h"

#include <stddef.h>
#include <stdlib.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/base/optimization.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace internal {

TypeStore::TypeStore(bool keep_alive_while_referenced_from_value)
    : keep_alive_while_referenced_from_value_(
          keep_alive_while_referenced_from_value) {}

TypeStore::~TypeStore() {
  // Need to delete these in a loop because the destructor is only visible
  // via friend declaration on Type.
  for (const Type* type : owned_types_) {
    delete type;
  }
  for (const AnnotationMap* annotation_map : owned_annotation_maps_) {
    delete annotation_map;
  }
  if (!factories_depending_on_this_.empty()) {
    ABSL_LOG(ERROR) << "Destructing TypeFactory " << this
                << " is unsafe because TypeFactory "
                << *factories_depending_on_this_.begin()
                << " depends on it staying alive.\n"
                << "Using --vmodule=type=2 may aid debugging.\n"
                ;
    // Avoid crashing on the TypeFactory dependency reference itself.
    for (const TypeStore* other : factories_depending_on_this_) {
      absl::MutexLock l(&other->mutex_);
      other->depends_on_factories_.erase(this);
    }
  }

  for (const TypeStore* other : depends_on_factories_) {
    bool need_to_unref = false;
    {
      absl::MutexLock l(&other->mutex_);
      if (other->factories_depending_on_this_.erase(this) != 0) {
        need_to_unref = other->keep_alive_while_referenced_from_value_;
      }
    }
    if (need_to_unref) {
      other->Unref();
    }
  }
}

void TypeStore::Ref() const {
  ref_count_.fetch_add(1, std::memory_order_relaxed);
}

void TypeStore::Unref() const {
  if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete this;
  }
}

void TypeStoreHelper::RefFromValue(const TypeStore* store) {
  ABSL_DCHECK(store);

  // We still do TypeStore reference counting in debug mode regardless of
  // whether keep_alive_while_referenced_from_value_ is true or not: this is
  // done to check that no values that reference types from this TypeStore are
  // alive when TypeFactore gets released. In release mode (NDEBUG), we do
  // refcounting only if keep_alive_while_referenced_from_value_ is true.
#ifdef NDEBUG
  if (!store->keep_alive_while_referenced_from_value_) return;
#endif

  store->Ref();
}

void TypeStoreHelper::UnrefFromValue(const TypeStore* store) {
  ABSL_DCHECK(store);

#ifdef NDEBUG
  if (!store->keep_alive_while_referenced_from_value_) return;
#endif

  store->Unref();
}

const TypeStore* TypeStoreHelper::GetTypeStore(const TypeFactoryBase* factory) {
  ABSL_DCHECK(factory);
  return factory->store_;
}

int64_t TypeStoreHelper::Test_GetRefCount(const TypeStore* store) {
  ABSL_DCHECK(store);
  return store->ref_count_.load(std::memory_order_seq_cst);
}

}  // namespace internal

namespace {

struct TypeKindInfo {
  // Nonexisting type kinds have NULL name pointer.
  const char* name = nullptr;

  // This is used to determine the cost of coercing from one type to another,
  // computed as the difference in the two type values.  Note that this is
  // different from <specificity> in that FLOAT and DOUBLE are reversed
  // in cost ordering, reflecting that we prefer to coerce INTs to DOUBLE rather
  // than FLOAT.  Additionally, we prefer coercing from STRING/INT to TIMESTAMP
  // rather than any of the legacy timestamp types (TIMESTAMP_MICROS, etc.).
  // Date/time related types can only be coerced from STRING, we prefer coercing
  // from STRING to the closest of these date/time types before coercing to
  // date/time types that are earlier in the list. When
  // FEATURE_IMPLICIT_COERCION_STRING_LITERAL_TO_BYTES is enabled, STRING
  // literals can also coerce to BYTES, at a lower precedence than to other
  // related types.
  int cost = 0;

  // Type specificity is used for Supertype analysis.  To evaluate the
  // Supertype of multiple types, we get the sets of supertypes associated
  // with each type, and the common supertype is the most specific type
  // in their intersection.
  int specificity = 0;

  // Result for Type::IsSimpleType.
  bool simple = false;
};

using TypeKindInfoList = std::array<TypeKindInfo, TypeKind_ARRAYSIZE>;
constexpr TypeKindInfoList MakeTypeKindInfoList() {
  TypeKindInfoList kinds = {};

  // None of the built-in type names/aliases should start with "[a-zA-Z]_",
  // which is reserved for user-defined objects.
  kinds[TYPE_UNKNOWN] = {
      .name = "UNKNOWN",
      .cost = 0,
      .specificity = 0,
      .simple = false,
  };
  kinds[TYPE_INT32] = {
      .name = "INT32",
      .cost = 12,
      .specificity = 12,
      .simple = true,
  };
  kinds[TYPE_INT64] = {
      .name = "INT64",
      .cost = 14,
      .specificity = 14,
      .simple = true,
  };
  kinds[TYPE_UINT32] = {
      .name = "UINT32",
      .cost = 11,
      .specificity = 11,
      .simple = true,
  };
  kinds[TYPE_UINT64] = {
      .name = "UINT64",
      .cost = 13,
      .specificity = 13,
      .simple = true,
  };
  kinds[TYPE_BOOL] = {
      .name = "BOOL",
      .cost = 10,
      .specificity = 10,
      .simple = true,
  };
  kinds[TYPE_FLOAT] = {
      .name = "FLOAT",
      .cost = 18,
      .specificity = 17,
      .simple = true,
  };
  kinds[TYPE_DOUBLE] = {
      .name = "DOUBLE",
      .cost = 17,
      .specificity = 18,
      .simple = true,
  };
  kinds[TYPE_STRING] = {
      .name = "STRING",
      .cost = 19,
      .specificity = 19,
      .simple = true,
  };
  kinds[TYPE_BYTES] = {
      .name = "BYTES",
      .cost = 38,
      .specificity = 20,
      .simple = true,
  };
  kinds[TYPE_DATE] = {
      .name = "DATE",
      .cost = 9,
      .specificity = 7,
      .simple = true,
  };

  kinds[TYPE_ENUM] = {
      .name = "ENUM",
      .cost = 1,
      .specificity = 1,
      .simple = false,
  };
  kinds[TYPE_ARRAY] = {
      .name = "ARRAY",
      .cost = 23,
      .specificity = 23,
      .simple = false,
  };
  kinds[TYPE_STRUCT] = {
      .name = "STRUCT",
      .cost = 22,
      .specificity = 22,
      .simple = false,
  };
  kinds[TYPE_PROTO] = {
      .name = "PROTO",
      .cost = 21,
      .specificity = 21,
      .simple = false,
  };
  kinds[TYPE_TIMESTAMP] = {
      .name = "TIMESTAMP",
      .cost = 8,
      .specificity = 2,
      .simple = true,
  };
  kinds[TYPE_TIME] = {
      .name = "TIME",
      .cost = 2,
      .specificity = 8,
      .simple = true,
  };
  kinds[TYPE_DATETIME] = {
      .name = "DATETIME",
      .cost = 3,
      .specificity = 9,
      .simple = true,
  };
  kinds[TYPE_GEOGRAPHY] = {
      .name = "GEOGRAPHY",
      .cost = 24,
      .specificity = 24,
      .simple = true,
  };
  kinds[TYPE_NUMERIC] = {
      .name = "NUMERIC",
      .cost = 15,
      .specificity = 15,
      .simple = true,
  };
  kinds[TYPE_BIGNUMERIC] = {
      .name = "BIGNUMERIC",
      .cost = 16,
      .specificity = 16,
      .simple = true,
  };
  kinds[TYPE_EXTENDED] = {
      .name = "EXTENDED",
      .cost = 25,
      .specificity = 25,
      .simple = false,
  };
  kinds[TYPE_JSON] = {
      .name = "JSON",
      .cost = 26,
      .specificity = 26,
      .simple = true,
  };
  kinds[TYPE_INTERVAL] = {
      .name = "INTERVAL",
      .cost = 27,
      .specificity = 27,
      .simple = true,
  };
  kinds[TYPE_TOKENLIST] = {
      .name = "TOKENLIST",
      .cost = 28,
      .specificity = 28,
      .simple = true,
  };
  kinds[TYPE_RANGE] = {
      .name = "RANGE",
      .cost = 29,
      .specificity = 29,
      .simple = false,
  };
  kinds[TYPE_GRAPH_ELEMENT] = {
      .name = "GRAPH_ELEMENT",
      .cost = 30,
      .specificity = 30,
      .simple = false,
  };
  kinds[TYPE_GRAPH_PATH] = {
      .name = "GRAPH_PATH",
      .cost = 33,
      .specificity = 33,
      .simple = false,
  };
  kinds[TYPE_MAP] = {
      .name = "MAP",
      .cost = 31,
      .specificity = 31,
      .simple = false,
  };
  kinds[TYPE_UUID] = {
      .name = "UUID",
      .cost = 32,
      .specificity = 32,
      .simple = true,
  };
  kinds[TYPE_MEASURE] = {
      .name = "MEASURE",
      .cost = 34,
      .specificity = 34,
      .simple = false,
  };
  kinds[TYPE_ROW] = {
      .name = "ROW",
      .cost = 36,
      .specificity = 36,
      .simple = false,
  };

  return kinds;
}

// Global static to avoid overhead of thread-safe local statics initialization.
constexpr TypeKindInfoList kTypeKindInfoList = MakeTypeKindInfoList();

// Returns NULL if TypeKindInfo is not defined for the given `kind`.
const TypeKindInfo* FindTypeKindInfo(TypeKind kind) {
  if (ABSL_PREDICT_FALSE(kind >= kTypeKindInfoList.size())) {
    return nullptr;
  }
  if (ABSL_PREDICT_FALSE(kTypeKindInfoList[kind].name == nullptr)) {
    return nullptr;
  }
  return &kTypeKindInfoList[kind];
}

}  // namespace

Type::Type(const TypeFactoryBase* factory, TypeKind kind)
    : type_store_(internal::TypeStoreHelper::GetTypeStore(factory)),
      kind_(kind) {}

// static
bool Type::IsSimpleType(TypeKind kind) {
  const TypeKindInfo* info = FindTypeKindInfo(kind);
  if (ABSL_PREDICT_TRUE(info != nullptr)) {
    return info->simple;
  }
  return false;
}

TypeKind Type::ResolveBuiltinTypeNameToKindIfSimple(absl::string_view type_name,
                                                    ProductMode mode) {
  return SimpleType::GetTypeKindIfSimple(type_name, mode);
}

TypeKind Type::ResolveBuiltinTypeNameToKindIfSimple(
    absl::string_view type_name, const LanguageOptions& language_options) {
  return SimpleType::GetTypeKindIfSimple(
      type_name, language_options.product_mode(),
      &language_options.GetEnabledLanguageFeatures());
}

std::string Type::TypeKindToString(TypeKind kind, ProductMode mode,
                                   bool use_external_float32_unused) {
  // Note that for types not externally supported we still want to produce
  // the internal names for them.  This is because during development
  // we want error messages to indicate what the unsupported type actually
  // is as an aid in debugging.  When used in production in external mode,
  // those internal names should never actually be reachable.
  const TypeKindInfo* info = FindTypeKindInfo(kind);
  if (ABSL_PREDICT_TRUE(info != nullptr)) {
    if (mode == PRODUCT_EXTERNAL && kind == TYPE_DOUBLE) {
      return "FLOAT64";
    } else if (mode == PRODUCT_EXTERNAL && kind == TYPE_FLOAT) {
      return "FLOAT32";
    }
    return info->name;
  }
  return absl::StrCat("INVALID_TYPE_KIND(", kind, ")");
}

std::string Type::TypeKindListToString(const std::vector<TypeKind>& kinds,
                                       ProductMode mode,
                                       bool use_external_float32) {
  std::vector<std::string> kind_strings;
  kind_strings.reserve(kinds.size());
  for (const TypeKind& kind : kinds) {
    kind_strings.push_back(TypeKindToString(kind, mode, use_external_float32));
  }
  return absl::StrJoin(kind_strings, ", ");
}

std::string Type::TypeListToString(TypeListView types, ProductMode mode,
                                   bool use_external_float32) {
  std::vector<std::string> type_strings;
  type_strings.reserve(types.size());
  for (const Type* type : types) {
    type_strings.push_back(type->ShortTypeName(mode, use_external_float32));
  }
  return absl::StrJoin(type_strings, ", ");
}

Type::FormatValueContentOptions
Type::FormatValueContentOptions::IncreaseIndent() {
  FormatValueContentOptions ret = *this;
  ret.indent += kIndentStep;
  // `force_type` only applies at the topmost level
  ret.force_type_at_top_level = false;
  return ret;
}

int Type::KindSpecificity(TypeKind kind) {
  const TypeKindInfo* info = FindTypeKindInfo(kind);
  if (ABSL_PREDICT_TRUE(info != nullptr)) {
    return info->specificity;
  }

  ABSL_LOG(FATAL) << "Out of range: " << kind;
}

static int KindCost(TypeKind kind) {
  const TypeKindInfo* info = FindTypeKindInfo(kind);
  if (ABSL_PREDICT_TRUE(info != nullptr)) {
    return info->cost;
  }

  ABSL_LOG(FATAL) << "Out of range: " << kind;
}

int Type::GetTypeCoercionCost(TypeKind kind1, TypeKind kind2) {
  return abs(KindCost(kind1) - KindCost(kind2));
}

bool Type::KindSpecificityLess(TypeKind kind1, TypeKind kind2) {
  ABSL_DCHECK_NE(kind1, TypeKind::TYPE_EXTENDED);
  ABSL_DCHECK_NE(kind2, TypeKind::TYPE_EXTENDED);

  return KindSpecificity(kind1) < KindSpecificity(kind2);
}

bool Type::TypeSpecificityLess(const Type* t1, const Type* t2) {
  return KindSpecificityLess(t1->kind(), t2->kind());
}

absl::Status Type::SerializeToProtoAndFileDescriptors(
    TypeProto* type_proto,
    google::protobuf::FileDescriptorSet* file_descriptor_set,
    std::set<const google::protobuf::FileDescriptor*>* file_descriptors) const {
  type_proto->Clear();

  FileDescriptorSetMap file_descriptor_set_map;
  if (file_descriptors != nullptr && !file_descriptors->empty()) {
    const google::protobuf::DescriptorPool* pool = (*file_descriptors->begin())->pool();
    std::unique_ptr<FileDescriptorEntry> file_descriptor_entry =
        std::make_unique<FileDescriptorEntry>();
    file_descriptor_entry->descriptor_set_index = 0;
    if (file_descriptor_set != nullptr) {
      file_descriptor_entry->file_descriptor_set.Swap(file_descriptor_set);
    }
    file_descriptor_entry->file_descriptors.swap(*file_descriptors);
    file_descriptor_set_map.emplace(pool, std::move(file_descriptor_entry));
  }
  // Optimization: skip building the descriptor sets if they would not
  // affect the output at all.
  BuildFileDescriptorSetMapOptions options;
  options.build_file_descriptor_sets =
      file_descriptor_set != nullptr || file_descriptors != nullptr;

  // No limit on FileDescriptorSet size.
  GOOGLESQL_RETURN_IF_ERROR(SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto, &file_descriptor_set_map));
  if (file_descriptor_set_map.size() > 1) {
    return MakeSqlError()
           << "Unable to serialize descriptors spanning multiple "
              "DescriptorPools into a single FileDescriptorSet. "
              "Use SerializeToProtoAndDistinctFileDescriptors "
              "instead.";
  } else if (!file_descriptor_set_map.empty()) {
    const std::unique_ptr<FileDescriptorEntry>& file_descriptor_entry =
        file_descriptor_set_map.begin()->second;
    if (file_descriptor_set != nullptr) {
      file_descriptor_set->Swap(&file_descriptor_entry->file_descriptor_set);
    }
    if (file_descriptors != nullptr) {
      file_descriptors->swap(file_descriptor_entry->file_descriptors);
    }
  }
  return absl::OkStatus();
}

absl::Status Type::SerializeToProtoAndDistinctFileDescriptors(
    TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  // No limit on FileDescriptorSet size.
  return SerializeToProtoAndDistinctFileDescriptors(
      type_proto,
      /*file_descriptor_sets_max_size_bytes=*/std::optional<int64_t>(),
      file_descriptor_set_map);
}

absl::Status Type::SerializeToProtoAndDistinctFileDescriptors(
    TypeProto* type_proto,
    std::optional<int64_t> file_descriptor_sets_max_size_bytes,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  GOOGLESQL_RET_CHECK(file_descriptor_set_map != nullptr);
  type_proto->Clear();
  BuildFileDescriptorSetMapOptions options;
  options.file_descriptor_sets_max_size_bytes =
      file_descriptor_sets_max_size_bytes;
  return SerializeToProtoAndDistinctFileDescriptorsImpl(
      options, type_proto, file_descriptor_set_map);
}

absl::Status Type::SerializeToSelfContainedProto(
    TypeProto* type_proto) const {
  type_proto->Clear();
  FileDescriptorSetMap file_descriptor_set_map;
  // No limit on FileDescriptorSet size.  TODO: Allow a limit to be
  // provided here as well.  Maybe this should just call the non-Impl version.
  GOOGLESQL_RETURN_IF_ERROR(SerializeToProtoAndDistinctFileDescriptorsImpl(
      {}, type_proto, &file_descriptor_set_map));
  // Determine the order of the FileDescriptorSets in the TypeProto.
  std::vector<google::protobuf::FileDescriptorSet*> file_descriptor_sets;
  file_descriptor_sets.resize(file_descriptor_set_map.size());
  for (const auto& pool_and_file_set : file_descriptor_set_map) {
    file_descriptor_sets[pool_and_file_set.second->descriptor_set_index] =
        &pool_and_file_set.second->file_descriptor_set;
  }
  // Now add all of them in order.
  for (google::protobuf::FileDescriptorSet* file_descriptor_set : file_descriptor_sets) {
    GOOGLESQL_RET_CHECK(file_descriptor_set != nullptr);
    type_proto->add_file_descriptor_set()->Swap(file_descriptor_set);
  }
  return absl::OkStatus();
}

std::string Type::ShortTypeName(ProductMode mode) const {
  return TypeName(mode);
}

std::string Type::ShortTypeName(ProductMode mode,
                                bool use_external_float32) const {
  return TypeName(mode, use_external_float32);
}

std::string Type::DebugString(bool details) const {
  std::string debug_string;

  TypeOrStringVector stack;
  stack.push_back(this);
  while (!stack.empty()) {
    const auto stack_entry = stack.back();
    stack.pop_back();
    if (std::holds_alternative<std::string>(stack_entry)) {
      absl::StrAppend(&debug_string, std::get<std::string>(stack_entry));
      continue;
    }
    std::get<const Type*>(stack_entry)
        ->DebugStringImpl(details, &stack, &debug_string);
  }
  return debug_string;
}

bool Type::SupportsGrouping(const LanguageOptions& language_options,
                            std::string* type_description) const {
  const Type* no_grouping_type;
  const bool supports_grouping =
      this->SupportsGroupingImpl(language_options, &no_grouping_type);
  if (!supports_grouping && type_description != nullptr) {
    if (no_grouping_type == this) {
      *type_description =
          TypeKindToString(this->kind(), language_options.product_mode());
    } else {
      *type_description = absl::StrCat(
          TypeKindToString(this->kind(), language_options.product_mode()),
          " containing ",
          TypeKindToString(no_grouping_type->kind(),
                           language_options.product_mode()));
    }
  }
  return supports_grouping;
}

bool Type::SupportsGroupingImpl(const LanguageOptions& language_options,
                                const Type** no_grouping_type) const {
  if (no_grouping_type != nullptr) {
    *no_grouping_type = this;
  }
  return false;
}

bool Type::SupportsPartitioning(const LanguageOptions& language_options,
                                std::string* type_description) const {
  const Type* no_partitioning_type;
  const bool supports_partitioning =
      this->SupportsPartitioningImpl(language_options, &no_partitioning_type);

  if (!supports_partitioning && type_description != nullptr) {
    if (no_partitioning_type == this) {
      *type_description =
          TypeKindToString(this->kind(), language_options.product_mode());
    } else {
      *type_description = absl::StrCat(
          TypeKindToString(this->kind(), language_options.product_mode()),
          " containing ",
          TypeKindToString(no_partitioning_type->kind(),
                           language_options.product_mode()));
    }
  }
  return supports_partitioning;
}

bool Type::SupportsPartitioningImpl(const LanguageOptions& language_options,
                                    const Type** no_partitioning_type) const {
  bool supports_partitioning =
      !this->IsGeography() && !this->IsFloatingPoint() && !this->IsTokenList();
  if (this->IsJson()) {
    supports_partitioning =
        language_options.LanguageFeatureEnabled(FEATURE_JSON_TYPE_COMPARISON);
  }

  if (no_partitioning_type != nullptr) {
    *no_partitioning_type = supports_partitioning ? nullptr : this;
  }
  return supports_partitioning;
}

bool Type::SupportsOrdering(const LanguageOptions& language_options,
                            std::string* type_description) const {
  bool supports_ordering = !IsGeography() && !IsTokenList();

  if (this->IsJson()) {
    supports_ordering =
        language_options.LanguageFeatureEnabled(FEATURE_JSON_TYPE_COMPARISON);
  }
  if (supports_ordering) return true;
  // For unsupported types, return the type name.
  if (type_description != nullptr) {
    *type_description = TypeKindToString(this->kind(),
                                         language_options.product_mode());
  }
  return false;
}

bool Type::SupportsOrdering() const {
  return SupportsOrdering(LanguageOptions(), /*type_description=*/nullptr);
}

// Array type equality support is controlled by the language option
// FEATURE_ARRAY_EQUALITY. To test if 'type' supports equality,
// checks the type recursively as array types can be nested under
// struct types or vice versa.
bool Type::SupportsEquality(
    const LanguageOptions& language_options) const {
  if (this->IsArray()) {
    if (language_options.LanguageFeatureEnabled(FEATURE_ARRAY_EQUALITY)) {
      return this->AsArray()->element_type()->SupportsEquality(
          language_options);
    } else {
      return false;
    }
  } else if (this->IsStruct()) {
    for (const StructField& field : this->AsStruct()->fields()) {
      if (!field.type->SupportsEquality(language_options)) {
        return false;
      }
    }
    return true;
  } else if (this->IsJson()) {
    return language_options.LanguageFeatureEnabled(
        FEATURE_JSON_TYPE_COMPARISON);
  }
  return this->SupportsEquality();
}

bool Type::SupportsReturning(const LanguageOptions& language_options,
                             std::string* type_description) const {
  switch (this->kind()) {
    case TYPE_ARRAY:
      return this->AsArray()->element_type()->SupportsReturning(
          language_options, type_description);
    case TYPE_STRUCT:
      for (const StructField& field : this->AsStruct()->fields()) {
        if (!field.type->SupportsReturning(language_options,
                                           type_description)) {
          return false;
        }
      }
      return true;
    case TYPE_MAP:
      return GetMapKeyType(this)->SupportsReturning(language_options,
                                                    type_description) &&
             GetMapValueType(this)->SupportsReturning(language_options,
                                                      type_description);
    case TYPE_GRAPH_ELEMENT:
    case TYPE_GRAPH_PATH:
      if (type_description != nullptr) {
        *type_description = TypeKindToString(
            this->kind(), language_options.product_mode(),
            !language_options.LanguageFeatureEnabled(FEATURE_DISABLE_FLOAT32));
      }
      return false;
    case TYPE_MEASURE:
      if (type_description != nullptr) {
        *type_description = "MEASURE";
      }
      return false;
    case TYPE_ROW:
      if (type_description != nullptr) {
        *type_description = "ROW";
      }
      return false;
    default:
      return true;
  }
}

void Type::CopyValueContent(const ValueContent& from, ValueContent* to) const {
  *to = from;
}

absl::HashState Type::Hash(absl::HashState state) const {
  // Hash a type's parameter.
  state = HashTypeParameter(std::move(state));
  // Hash a type's kind.
  return absl::HashState::combine(std::move(state), kind());
}

bool Type::HasFloatingPointFields() const {
  // This should only be called for ProtoType, and is implemented there.
  ABSL_LOG(ERROR)
      << "HasFloatingPointFields() should only be called for proto types";
  return false;
}

bool Type::IsArrayLike() const {
  return IsArray() || (IsRow() && AsRow()->IsJoin());
}

absl::StatusOr<const Type*> Type::GetElementType() const {
  if (IsArray()) {
    return AsArray()->element_type();
  } else if (IsRow() && AsRow()->IsJoin()) {
    return AsRow()->element_type();
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "GetElementType called on " << DebugString();
  }
}

absl::Status Type::TypeMismatchError(const ValueProto& value_proto) const {
  return absl::Status(
      absl::StatusCode::kInternal,
      absl::StrCat(
          "Type mismatch: provided type ", DebugString(), " but proto <",
          value_proto.ShortDebugString(),
          "> doesn't have field of that type and is not null"));
}

bool TypeEquals::operator()(const Type* const type1,
                            const Type* const type2) const {
  if (type1 == type2) {
    // Note that two nullptr Type pointers will compare to TRUE.
    return true;
  }
  if (type1 == nullptr || type2 == nullptr) {
    // If one is nullptr and the other not nullptr, then they cannot be equal.
    return false;
  }
  return type1->Equals(type2);
}

bool TypeEquivalent::operator()(const Type* const type1,
                                const Type* const type2) const {
  if (type1 == type2) {
    // Note that two nullptr Type pointers will compare to TRUE.
    return true;
  }
  if (type1 == nullptr || type2 == nullptr) {
    // If one is nullptr and the other not nullptr, then they cannot be equal.
    return false;
  }
  return type1->Equivalent(type2);
}

size_t TypeHash::operator()(const Type* const type) const {
  if (type == nullptr) {
    return 17 * 23;  // Some random number to represent nullptr.
  }

  return absl::Hash<Type>()(*type);
}

absl::StatusOr<TypeParameters> Type::ValidateAndResolveTypeParameters(
    const std::vector<TypeParameterValue>& type_parameter_values,
    ProductMode mode) const {
  return MakeSqlError() << "Type " << ShortTypeName(mode)
                        << " does not support type parameters";
}

// TODO: Update this to use type visitor so that we can support all
// types with one implementation.
absl::Status Type::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  GOOGLESQL_RET_CHECK(type_parameters.IsEmpty())
      << "Type " << ShortTypeName(mode) << " does not support type parameters";
  return absl::OkStatus();
}

std::string Type::AddCapitalizedTypePrefix(absl::string_view input,
                                           bool is_null) const {
  if (kind() == TYPE_PROTO && !is_null) {
    // Proto types wrap their values using curly brackets, so don't need
    // to add additional parentheses.
    return absl::StrCat(CapitalizedName(), input);
  }

  return absl::StrCat(CapitalizedName(), "(", input, ")");
}

}  // namespace googlesql
