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

#include "googlesql/local_service/local_service.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/proto_helper.h"
#include "googlesql/local_service/local_service.pb.h"
#include "googlesql/local_service/state.h"
#include "googlesql/parser/parse_tree_serializer.h"
#include "googlesql/proto/simple_catalog.pb.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/formatter_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/lenient_formatter.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_table.pb.h"
#include "googlesql/public/sql_formatter.h"
#include "googlesql/public/table_from_proto.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace local_service {

using google::protobuf::RepeatedPtrField;

namespace {

template <typename ParamT>
absl::Status RepeatedParametersToMap(const RepeatedPtrField<ParamT>& params,
                                     const QueryParametersMap& types,
                                     ParameterValueMap* map) {
  for (const auto& param : params) {
    std::string name = absl::AsciiStrToLower(param.name());
    const Type* type = googlesql_base::FindPtrOrNull(types, name);
    GOOGLESQL_RET_CHECK(type != nullptr) << "Type not found for '" << name << "'";
    auto result = Value::Deserialize(param.value(), type);
    GOOGLESQL_RETURN_IF_ERROR(result.status());
    (*map)[name] = result.value();
  }

  return absl::OkStatus();
}

// Populate the existing pools into the map with existing indices, to make sure
// the serialized type will use the same indices.
void PopulateExistingPoolsToFileDescriptorSetMap(
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    FileDescriptorSetMap* file_descriptor_set_map) {
  file_descriptor_set_map->clear();

  for (int i = 0; i < pools.size(); ++i) {
    std::unique_ptr<Type::FileDescriptorEntry>& entry =
        (*file_descriptor_set_map)[pools[i]];
    ABSL_CHECK_EQ(entry.get(), nullptr);
    entry = std::make_unique<Type::FileDescriptorEntry>();
    entry->descriptor_set_index = i;
  }

  ABSL_CHECK_EQ(pools.size(), file_descriptor_set_map->size());
}

absl::Status SerializeTypeUsingExistingPools(
    const Type* type, const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeProto* type_proto) {
  FileDescriptorSetMap file_descriptor_set_map;
  PopulateExistingPoolsToFileDescriptorSetMap(pools, &file_descriptor_set_map);

  GOOGLESQL_RETURN_IF_ERROR(type->SerializeToProtoAndDistinctFileDescriptors(
      type_proto, &file_descriptor_set_map));

  GOOGLESQL_RET_CHECK_EQ(pools.size(), file_descriptor_set_map.size())
      << type->DebugString(true)
      << " uses unknown DescriptorPool, this shouldn't happen.";
  return absl::OkStatus();
}

absl::Status SerializeColumnUsingExistingPools(
    PreparedQueryBase::NameAndType column,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    SimpleColumnProto* column_proto) {
  FileDescriptorSetMap file_descriptor_set_map;
  PopulateExistingPoolsToFileDescriptorSetMap(pools, &file_descriptor_set_map);

  column_proto->Clear();
  column_proto->set_name(column.first);
  GOOGLESQL_RETURN_IF_ERROR(column.second->SerializeToProtoAndDistinctFileDescriptors(
      column_proto->mutable_type(), &file_descriptor_set_map));
  column_proto->set_is_pseudo_column(false);
  column_proto->set_is_writable_column(true);

  GOOGLESQL_RET_CHECK_EQ(pools.size(), file_descriptor_set_map.size())
      << column.first << " uses unknown DescriptorPool, this shouldn't happen.";
  return absl::OkStatus();
}

absl::StatusOr<EvaluateModifyResponse::Row::Operation> SerializeModifyOperation(
    const EvaluatorTableModifyIterator::Operation operation) {
  switch (operation) {
    case EvaluatorTableModifyIterator::Operation::kDelete:
      return EvaluateModifyResponse::Row::DELETE;
    case EvaluatorTableModifyIterator::Operation::kInsert:
      return EvaluateModifyResponse::Row::INSERT;
    case EvaluatorTableModifyIterator::Operation::kUpdate:
      return EvaluateModifyResponse::Row::UPDATE;
    default:
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "Unknown Modify operation";
  }
}

}  // namespace

class RegisteredDescriptorPoolState : public GenericState {
 public:
  RegisteredDescriptorPoolState() = delete;
  RegisteredDescriptorPoolState(const RegisteredDescriptorPoolState&) = delete;

  RegisteredDescriptorPoolState& operator=(
      const RegisteredDescriptorPoolState&) = delete;

  static absl::StatusOr<std::unique_ptr<RegisteredDescriptorPoolState>> Create(
      const google::protobuf::FileDescriptorSet& fdset) {
    auto pool = std::make_unique<google::protobuf::DescriptorPool>();
    GOOGLESQL_RETURN_IF_ERROR(AddFileDescriptorSetToPool(&fdset, pool.get()));

    return absl::WrapUnique(new RegisteredDescriptorPoolState(std::move(pool)));
  }

  const google::protobuf::DescriptorPool* pool() {
    if (is_builtin_) {
      return google::protobuf::DescriptorPool::generated_pool();
    } else {
      return pool_.get();
    }
  }

 private:
  friend class RegisteredDescriptorPoolPool;
  class builtin_descriptor_pool_t {};
  explicit RegisteredDescriptorPoolState(builtin_descriptor_pool_t)
      : is_builtin_(true) {}
  explicit RegisteredDescriptorPoolState(
      std::unique_ptr<const google::protobuf::DescriptorPool> pool)
      : pool_(std::move(pool)), is_builtin_(false) {}
  const std::unique_ptr<const google::protobuf::DescriptorPool> pool_ = nullptr;
  const bool is_builtin_ = false;
};

class RegisteredDescriptorPoolPool
    : public SharedStatePool<RegisteredDescriptorPoolState> {
 public:
  RegisteredDescriptorPoolPool() {
    int64_t id = Register(new RegisteredDescriptorPoolState(
        RegisteredDescriptorPoolState::builtin_descriptor_pool_t()));
    ABSL_CHECK_NE(id, -1);
    builtin_pool_ = Get(id);
  }

  std::shared_ptr<RegisteredDescriptorPoolState>
  GetBuiltinDescriptorPoolState() {
    return builtin_pool_;
  }

 private:
  std::shared_ptr<RegisteredDescriptorPoolState> builtin_pool_;
};

class InternalPreparedExpressionState : public GenericState {
 public:
  InternalPreparedExpressionState() = delete;
  InternalPreparedExpressionState(const InternalPreparedExpressionState&) =
      delete;
  InternalPreparedExpressionState& operator=(
      const InternalPreparedExpressionState&) = delete;

  static absl::StatusOr<std::unique_ptr<InternalPreparedExpressionState>>
  CreateAndPrepareExpression(
      absl::string_view sql, const AnalyzerOptionsProto& options_proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      SimpleCatalog* catalog,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids = {},
      std::optional<int64_t> owned_catalog_id = std::nullopt) {
    auto type_factory = std::make_unique<TypeFactory>();
    auto options = std::make_unique<AnalyzerOptions>();

    GOOGLESQL_RETURN_IF_ERROR(AnalyzerOptions::Deserialize(
        options_proto, pools, type_factory.get(), options.get()));
    EvaluatorOptions evaluator_options;
    evaluator_options.type_factory = type_factory.get();
    evaluator_options.default_time_zone = options->default_time_zone();
    auto exp = std::make_unique<PreparedExpression>(sql, evaluator_options);
    GOOGLESQL_RETURN_IF_ERROR(exp->Prepare(*options, catalog));
    return absl::WrapUnique(new InternalPreparedExpressionState(
        std::move(type_factory), std::move(options), std::move(exp),
        std::move(owned_descriptor_pool_ids), owned_catalog_id));
  }

  const PreparedExpression* GetExpression() const { return expression_.get(); }

  const AnalyzerOptions& GetAnalyzerOptions() const { return *options_; }

  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids() const {
    return owned_descriptor_pool_ids_;
  }

  std::optional<int64_t> owned_catalog_id() const { return owned_catalog_id_; }

 private:
  InternalPreparedExpressionState(
      std::unique_ptr<const TypeFactory> factory,
      std::unique_ptr<const AnalyzerOptions> options,
      std::unique_ptr<const PreparedExpression> expression,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
      std::optional<int64_t> owned_catalog_id)
      : factory_(std::move(factory)),
        options_(std::move(options)),
        expression_(std::move(expression)),
        owned_descriptor_pool_ids_(std::move(owned_descriptor_pool_ids)),
        owned_catalog_id_(owned_catalog_id) {}

  const std::unique_ptr<const TypeFactory> factory_;
  const std::unique_ptr<const AnalyzerOptions> options_;
  const std::unique_ptr<const PreparedExpression> expression_;
  // Descriptor pools that are owned by this PreparedExpression, and should
  // be deleted when this object is deleted.
  const absl::flat_hash_set<int64_t> owned_descriptor_pool_ids_;
  const std::optional<int64_t> owned_catalog_id_;
};

class PreparedExpressionPool
    : public SharedStatePool<InternalPreparedExpressionState> {};

class InternalPreparedQueryState : public GenericState {
 public:
  InternalPreparedQueryState() = delete;
  InternalPreparedQueryState(const InternalPreparedQueryState&) = delete;
  InternalPreparedQueryState& operator=(const InternalPreparedQueryState&) =
      delete;

  static absl::StatusOr<std::unique_ptr<InternalPreparedQueryState>>
  CreateAndPrepareQuery(
      absl::string_view sql, const AnalyzerOptionsProto& options_proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      SimpleCatalog* catalog,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids = {},
      std::optional<int64_t> owned_catalog_id = std::nullopt) {
    auto type_factory = std::make_unique<TypeFactory>();
    auto options = std::make_unique<AnalyzerOptions>();

    GOOGLESQL_RETURN_IF_ERROR(AnalyzerOptions::Deserialize(
        options_proto, pools, type_factory.get(), options.get()));
    EvaluatorOptions evaluator_options;
    evaluator_options.type_factory = type_factory.get();
    evaluator_options.default_time_zone = options->default_time_zone();
    auto query = std::make_unique<PreparedQuery>(sql, evaluator_options);
    GOOGLESQL_RETURN_IF_ERROR(query->Prepare(*options, catalog));
    return absl::WrapUnique(new InternalPreparedQueryState(
        std::move(type_factory), std::move(options), std::move(query),
        std::move(owned_descriptor_pool_ids), owned_catalog_id));
  }

  const PreparedQuery* GetQuery() const { return query_.get(); }

  const AnalyzerOptions& GetAnalyzerOptions() const { return *options_; }

  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids() const {
    return owned_descriptor_pool_ids_;
  }

  std::optional<int64_t> owned_catalog_id() const { return owned_catalog_id_; }

 private:
  InternalPreparedQueryState(
      std::unique_ptr<const TypeFactory> factory,
      std::unique_ptr<const AnalyzerOptions> options,
      std::unique_ptr<const PreparedQuery> query,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
      std::optional<int64_t> owned_catalog_id)
      : factory_(std::move(factory)),
        options_(std::move(options)),
        query_(std::move(query)),
        owned_descriptor_pool_ids_(std::move(owned_descriptor_pool_ids)),
        owned_catalog_id_(owned_catalog_id) {}

  const std::unique_ptr<const TypeFactory> factory_;
  const std::unique_ptr<const AnalyzerOptions> options_;
  const std::unique_ptr<const PreparedQuery> query_;
  // Descriptor pools that are owned by this PreparedQuery, and should
  // be deleted when this object is deleted.
  const absl::flat_hash_set<int64_t> owned_descriptor_pool_ids_;
  const std::optional<int64_t> owned_catalog_id_;
};

class PreparedQueryPool : public SharedStatePool<InternalPreparedQueryState> {};

class InternalPreparedModifyState : public GenericState {
 public:
  InternalPreparedModifyState() = delete;
  InternalPreparedModifyState(const InternalPreparedModifyState&) = delete;
  InternalPreparedModifyState& operator=(const InternalPreparedModifyState&) =
      delete;

  static absl::StatusOr<std::unique_ptr<InternalPreparedModifyState>>
  CreateAndPrepareModify(
      absl::string_view sql, const AnalyzerOptionsProto& options_proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      SimpleCatalog* catalog,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids = {},
      std::optional<int64_t> owned_catalog_id = std::nullopt) {
    auto type_factory = std::make_unique<TypeFactory>();
    auto options = std::make_unique<AnalyzerOptions>();

    GOOGLESQL_RETURN_IF_ERROR(AnalyzerOptions::Deserialize(
        options_proto, pools, type_factory.get(), options.get()));
    EvaluatorOptions evaluator_options;
    evaluator_options.type_factory = type_factory.get();
    evaluator_options.default_time_zone = options->default_time_zone();
    auto modify = std::make_unique<PreparedModify>(sql, evaluator_options);
    GOOGLESQL_RETURN_IF_ERROR(modify->Prepare(*options, catalog));
    return absl::WrapUnique(new InternalPreparedModifyState(
        std::move(type_factory), std::move(options), std::move(modify),
        std::move(owned_descriptor_pool_ids), owned_catalog_id));
  }

  PreparedModify* GetModify() { return modify_.get(); }

  const AnalyzerOptions& GetAnalyzerOptions() const { return *options_; }

  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids() const {
    return owned_descriptor_pool_ids_;
  }

  std::optional<int64_t> owned_catalog_id() const { return owned_catalog_id_; }

 private:
  InternalPreparedModifyState(
      std::unique_ptr<const TypeFactory> factory,
      std::unique_ptr<const AnalyzerOptions> options,
      std::unique_ptr<PreparedModify> modify,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
      std::optional<int64_t> owned_catalog_id)
      : factory_(std::move(factory)),
        options_(std::move(options)),
        modify_(std::move(modify)),
        owned_descriptor_pool_ids_(std::move(owned_descriptor_pool_ids)),
        owned_catalog_id_(owned_catalog_id) {}

  const std::unique_ptr<const TypeFactory> factory_;
  const std::unique_ptr<const AnalyzerOptions> options_;
  const std::unique_ptr<PreparedModify> modify_;
  // Descriptor pools that are owned by this PreparedModify, and should
  // be deleted when this object is deleted.
  const absl::flat_hash_set<int64_t> owned_descriptor_pool_ids_;
  const std::optional<int64_t> owned_catalog_id_;
};

class PreparedModifyPool : public SharedStatePool<InternalPreparedModifyState> {
};

class RegisteredCatalogState : public GenericState {
 public:
  RegisteredCatalogState() = delete;
  RegisteredCatalogState(const RegisteredCatalogState&) = delete;
  RegisteredCatalogState& operator=(const RegisteredCatalogState&) = delete;

  static absl::StatusOr<std::unique_ptr<RegisteredCatalogState>> Create(
      const SimpleCatalogProto& proto,
      const google::protobuf::Map<std::string, TableContent>& tables_contents,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids = {}) {
    std::unique_ptr<SimpleCatalog> catalog;

    if (tables_contents.empty()) {
      // If there are is no table content to be set then there is no need
      // to serialize the Tables independently and we can deserialize
      // the Catalog proto as it is
      GOOGLESQL_RETURN_IF_ERROR(SimpleCatalog::Deserialize(proto, pools, &catalog));
    } else {
      // Make a copy of the original immutable Catalog proto, which will be
      // mutable and will allow us to manipulate the tables' contents
      SimpleCatalogProto proto_copy = proto;
      proto_copy.clear_table();

      // Deserialize the Catalog proto with the tables
      GOOGLESQL_RETURN_IF_ERROR(SimpleCatalog::Deserialize(proto_copy, pools, &catalog));

      // Deserialize each individual Table proto, set its content if provided
      // and then add the Table to the Catalog
      const TypeDeserializer type_deserializer =
          TypeDeserializer(catalog->type_factory(), pools);
      for (const SimpleTableProto& table_proto : proto.table()) {
        const std::string& name = table_proto.has_name_in_catalog()
                                      ? table_proto.name_in_catalog()
                                      : table_proto.name();
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleTable> table,
                         DeserializeTable(name, table_proto, tables_contents,
                                          type_deserializer));

        if (!catalog->AddOwnedTableIfNotPresent(name, std::move(table))) {
          return ::googlesql_base::InvalidArgumentErrorBuilder()
                 << "Duplicate table '" << name << "' in serialized catalog";
        }
      }
    }

    return absl::WrapUnique(new RegisteredCatalogState(
        std::move(catalog), std::move(owned_descriptor_pool_ids)));
  }

  // Ideally, this would be const, however, the googlesql analyzer API
  // requires this be mutable (even though it does ever mutate anything).
  SimpleCatalog* GetCatalog() { return catalog_.get(); }

  const absl::flat_hash_set<int64_t>& owned_descriptor_pool_ids() const {
    return owned_descriptor_pool_ids_;
  }

 private:
  RegisteredCatalogState(std::unique_ptr<SimpleCatalog> catalog,
                         absl::flat_hash_set<int64_t> owned_descriptor_pool_ids)
      : catalog_(std::move(catalog)),
        owned_descriptor_pool_ids_(std::move(owned_descriptor_pool_ids)) {}

  static absl::StatusOr<std::unique_ptr<SimpleTable>> DeserializeTable(
      absl::string_view name, const SimpleTableProto& proto,
      const google::protobuf::Map<std::string, TableContent>& tables_contents,
      const TypeDeserializer& type_deserializer) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleTable> table,
                     SimpleTable::Deserialize(proto, type_deserializer));

    const TableContent* table_content = googlesql_base::FindOrNull(tables_contents, name);
    if (table_content == nullptr || !table_content->has_table_data()) {
      return table;
    }

    const TableData& table_data = table_content->table_data();

    std::vector<std::vector<Value>> content;
    for (const auto& row : table_data.row()) {
      std::vector<Value> googlesql_row;
      for (int i = 0; i < row.cell_size(); i++) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto googlesql_value,
            Value::Deserialize(row.cell(i), table->GetColumn(i)->GetType()));
        googlesql_row.push_back(googlesql_value);
      }
      content.push_back(std::move(googlesql_row));
    }
    table->SetContents(std::move(content));

    return table;
  }

  const std::unique_ptr<SimpleCatalog> catalog_;
  const absl::flat_hash_set<int64_t> owned_descriptor_pool_ids_;
};

class RegisteredCatalogPool : public SharedStatePool<RegisteredCatalogState> {};

GoogleSqlLocalServiceImpl::GoogleSqlLocalServiceImpl()
    : registered_descriptor_pools_(new RegisteredDescriptorPoolPool()),
      registered_catalogs_(new RegisteredCatalogPool()),
      prepared_expressions_(new PreparedExpressionPool()),
      prepared_queries_(new PreparedQueryPool()),
      prepared_modifies_(new PreparedModifyPool()) {}

GoogleSqlLocalServiceImpl::~GoogleSqlLocalServiceImpl() = default;

void GoogleSqlLocalServiceImpl::CleanupCatalog(
    std::optional<int64_t>* catalog_id) {
  if (catalog_id->has_value()) {
    registered_catalogs_->Delete(**catalog_id);
  }
}

void GoogleSqlLocalServiceImpl::CleanupDescriptorPools(
    absl::flat_hash_set<int64_t>* descriptor_pool_ids) {
  for (int64_t pool_id : *descriptor_pool_ids) {
    registered_descriptor_pools_->Delete(pool_id);
  }
}

absl::Status GoogleSqlLocalServiceImpl::RegisterNewDescriptorPools(
    std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>&
        descriptor_pool_states,
    absl::flat_hash_set<int64_t>& registered_descriptor_pool_ids,
    DescriptorPoolIdList& descriptor_pool_id_list) {
  registered_descriptor_pool_ids.clear();
  descriptor_pool_id_list.Clear();
  for (std::shared_ptr<RegisteredDescriptorPoolState>& pool_state :
       descriptor_pool_states) {
    if (!pool_state->IsRegistered()) {
      // Not registered, so we registered it, and own it.
      int64_t pool_id = registered_descriptor_pools_->Register(pool_state);
      GOOGLESQL_RET_CHECK_NE(-1, pool_id)
          << "Failed to register descriptor pool, this shouldn't happen";
      registered_descriptor_pool_ids.insert(pool_id);
    }
    descriptor_pool_id_list.add_registered_ids(pool_state->GetId());
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::Prepare(const PrepareRequest& request,
                                                PrepareResponse* response) {
  return PrepareImpl(request, {}, *prepared_expressions_, response);
}

template <>
absl::Status GoogleSqlLocalServiceImpl::CreateAndPrepare(
    absl::string_view sql, const AnalyzerOptionsProto& options,
    std::shared_ptr<RegisteredCatalogState> catalog_state,
    std::vector<const google::protobuf::DescriptorPool*> pools,
    absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
    std::optional<int64_t> owned_catalog_id,
    std::shared_ptr<InternalPreparedExpressionState>& internal_state) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      internal_state,
      InternalPreparedExpressionState::CreateAndPrepareExpression(
          sql, options, pools,
          catalog_state != nullptr ? catalog_state->GetCatalog() : nullptr,
          owned_descriptor_pool_ids, owned_catalog_id));
  return absl::OkStatus();
}

template <>
absl::Status GoogleSqlLocalServiceImpl::RegisterPrepared(
    const bool should_register_prepared,
    std::shared_ptr<InternalPreparedExpressionState> internal_state,
    SharedStatePool<InternalPreparedExpressionState>& prepared_statements_pool,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    PreparedState* response_state) {
  const PreparedExpression* exp = internal_state->GetExpression();

  GOOGLESQL_RETURN_IF_ERROR(SerializeTypeUsingExistingPools(
      exp->output_type(), pools, response_state->mutable_output_type()));

  GOOGLESQL_ASSIGN_OR_RETURN(auto columns, exp->GetReferencedColumns());
  for (const std::string& column_name : columns) {
    response_state->add_referenced_columns(column_name);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameters, exp->GetReferencedParameters());
  for (const std::string& parameter_name : parameters) {
    response_state->add_referenced_parameters(parameter_name);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameter_count, exp->GetPositionalParameterCount());
  response_state->set_positional_parameter_count(parameter_count);

  if (should_register_prepared) {
    int64_t id = prepared_statements_pool.Register(internal_state);
    GOOGLESQL_RET_CHECK_NE(-1, id)
        << "Failed to register prepared state, this shouldn't happen.";
    response_state->set_prepared_expression_id(id);
  }

  if (response_state->descriptor_pool_id_list().registered_ids_size() == 0) {
    response_state->clear_descriptor_pool_id_list();
  }

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::PrepareQuery(
    const PrepareQueryRequest& request, PrepareQueryResponse* response) {
  return PrepareImpl(request, request.table_content(), *prepared_queries_,
                     response);
}

template <>
absl::Status GoogleSqlLocalServiceImpl::CreateAndPrepare(
    absl::string_view sql, const AnalyzerOptionsProto& options,
    std::shared_ptr<RegisteredCatalogState> catalog_state,
    std::vector<const google::protobuf::DescriptorPool*> pools,
    absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
    std::optional<int64_t> owned_catalog_id,
    std::shared_ptr<InternalPreparedQueryState>& internal_state) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      internal_state,
      InternalPreparedQueryState::CreateAndPrepareQuery(
          sql, options, pools,
          catalog_state != nullptr ? catalog_state->GetCatalog() : nullptr,
          owned_descriptor_pool_ids, owned_catalog_id));
  return absl::OkStatus();
}

template <>
absl::Status GoogleSqlLocalServiceImpl::RegisterPrepared(
    const bool should_register_prepared,
    std::shared_ptr<InternalPreparedQueryState> internal_state,
    SharedStatePool<InternalPreparedQueryState>& prepared_statements_pool,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    PreparedQueryState* response_state) {
  const PreparedQuery* query = internal_state->GetQuery();

  for (const PreparedQueryBase::NameAndType& column : query->GetColumns()) {
    GOOGLESQL_RETURN_IF_ERROR(SerializeColumnUsingExistingPools(
        column, pools, response_state->add_columns()));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameters, query->GetReferencedParameters());
  for (const std::string& parameter_name : parameters) {
    response_state->add_referenced_parameters(parameter_name);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameter_count, query->GetPositionalParameterCount());
  response_state->set_positional_parameter_count(parameter_count);

  if (should_register_prepared) {
    int64_t id = prepared_statements_pool.Register(internal_state);
    GOOGLESQL_RET_CHECK_NE(-1, id)
        << "Failed to register prepared state, this shouldn't happen.";
    response_state->set_prepared_query_id(id);
  }

  if (response_state->descriptor_pool_id_list().registered_ids_size() == 0) {
    response_state->clear_descriptor_pool_id_list();
  }

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::PrepareModify(
    const PrepareModifyRequest& request, PrepareModifyResponse* response) {
  return PrepareImpl(request, request.table_content(), *prepared_modifies_,
                     response);
}

template <>
absl::Status GoogleSqlLocalServiceImpl::CreateAndPrepare(
    absl::string_view sql, const AnalyzerOptionsProto& options,
    std::shared_ptr<RegisteredCatalogState> catalog_state,
    std::vector<const google::protobuf::DescriptorPool*> pools,
    absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
    std::optional<int64_t> owned_catalog_id,
    std::shared_ptr<InternalPreparedModifyState>& internal_state) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      internal_state,
      InternalPreparedModifyState::CreateAndPrepareModify(
          sql, options, pools,
          catalog_state != nullptr ? catalog_state->GetCatalog() : nullptr,
          owned_descriptor_pool_ids, owned_catalog_id));
  return absl::OkStatus();
}

template <>
absl::Status GoogleSqlLocalServiceImpl::RegisterPrepared(
    const bool should_register_prepared,
    std::shared_ptr<InternalPreparedModifyState> internal_state,
    SharedStatePool<InternalPreparedModifyState>& prepared_statements_pool,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    PreparedModifyState* response_state) {
  const PreparedModify* modify = internal_state->GetModify();

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameters, modify->GetReferencedParameters());
  for (const std::string& parameter_name : parameters) {
    response_state->add_referenced_parameters(parameter_name);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto parameter_count, modify->GetPositionalParameterCount());
  response_state->set_positional_parameter_count(parameter_count);

  if (should_register_prepared) {
    int64_t id = prepared_statements_pool.Register(internal_state);
    GOOGLESQL_RET_CHECK_NE(-1, id)
        << "Failed to register prepared state, this shouldn't happen.";
    response_state->set_prepared_modify_id(id);
  }

  if (response_state->descriptor_pool_id_list().registered_ids_size() == 0) {
    response_state->clear_descriptor_pool_id_list();
  }

  return absl::OkStatus();
}

template <typename RequestT, typename ResponseT, typename InternalStateT>
absl::Status GoogleSqlLocalServiceImpl::PrepareImpl(
    const RequestT& request,
    const google::protobuf::Map<std::string, TableContent>& tables_contents,
    SharedStatePool<InternalStateT>& prepared_statements_pool,
    ResponseT* response) {
  std::shared_ptr<RegisteredCatalogState> catalog_state;
  std::vector<const google::protobuf::DescriptorPool*> pools;

  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;
  GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                     descriptor_pool_states, pools));

  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids;
  // On error, make sure we don't leak any registered descriptor pools.
  auto descriptor_pool_cleanup = absl::MakeCleanup(
      absl::bind_front(&GoogleSqlLocalServiceImpl::CleanupDescriptorPools, this,
                       &owned_descriptor_pool_ids));
  GOOGLESQL_RETURN_IF_ERROR(RegisterNewDescriptorPools(
      descriptor_pool_states, owned_descriptor_pool_ids,
      *(response->mutable_prepared()->mutable_descriptor_pool_id_list())));

  GOOGLESQL_RETURN_IF_ERROR(
      GetCatalogState(request, tables_contents, pools, catalog_state));

  std::optional<int64_t> owned_catalog_id;
  auto catalog_cleanup = absl::MakeCleanup(absl::bind_front(
      &GoogleSqlLocalServiceImpl::CleanupCatalog, this, &owned_catalog_id));

  if (catalog_state != nullptr && !catalog_state->IsRegistered()) {
    owned_catalog_id = registered_catalogs_->Register(catalog_state);
    GOOGLESQL_RET_CHECK_NE(-1, owned_catalog_id.value())
        << "Failed to register catalog, this shouldn't happen";
  }

  std::shared_ptr<InternalStateT> internal_state;
  GOOGLESQL_RETURN_IF_ERROR(CreateAndPrepare(
      request.sql(), request.options(), catalog_state, pools,
      owned_descriptor_pool_ids, owned_catalog_id, internal_state));

  GOOGLESQL_RETURN_IF_ERROR(RegisterPrepared(
      /*should_register_prepared=*/true, internal_state,
      prepared_statements_pool, pools, response->mutable_prepared()));

  // No errors, caller is now responsible for the prepared expression and
  // therefore any owned descriptor pools.
  std::move(catalog_cleanup).Cancel();
  std::move(descriptor_pool_cleanup).Cancel();
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::Unprepare(int64_t id) {
  return UnprepareImpl(*prepared_expressions_, id, "expression");
}

absl::Status GoogleSqlLocalServiceImpl::UnprepareQuery(int64_t id) {
  return UnprepareImpl(*prepared_queries_, id, "query");
}

absl::Status GoogleSqlLocalServiceImpl::UnprepareModify(int64_t id) {
  return UnprepareImpl(*prepared_modifies_, id, "modify");
}

template <typename InternalStateT>
absl::Status GoogleSqlLocalServiceImpl::UnprepareImpl(
    SharedStatePool<InternalStateT>& prepared_statements_pool, int64_t id,
    absl::string_view statement_type) {
  std::shared_ptr<InternalStateT> state = prepared_statements_pool.Get(id);
  if (state == nullptr) {
    return MakeSqlError() << "Unknown prepared " << statement_type
                          << " ID: " << id;
  }

  // This will only capture the 'last' error we encounter, but since any error
  // would indicate some sort of horrible internal state error, that's
  // probably okay.
  absl::Status status;
  for (int64_t pool_id : state->owned_descriptor_pool_ids()) {
    if (!registered_descriptor_pools_->Delete(pool_id)) {
      status = MakeSqlError() << "Unknown descriptor pool ID: " << pool_id;
    }
  }
  if (state->owned_catalog_id().has_value()) {
    int64_t owned_catalog_id = state->owned_catalog_id().value();
    if (!registered_catalogs_->Delete(owned_catalog_id)) {
      status = MakeSqlError() << "Unknown catalog ID: " << owned_catalog_id;
    }
  }

  if (!prepared_statements_pool.Delete(id)) {
    status = MakeSqlError()
             << "Unknown prepared " << statement_type << " ID: " << id;
  }
  return status;
}

// NOTE: This Evaluate() API which is being used to evaluate
// prepared expressions is the only Evaluate API that, when it runs,
// it registers the prepared expression and its owned descriptor pools.
// The other 2 Evaluate APIs (EvaluateQuery and EvaluateModify) do not register
// any entities on the service side and this is the original intended behavior
// for all 3 Evaluate APIs.
// Once it's been decided to correct the behavior of this API, the code within
// this method can be replaced with a call to the EvaluateImpl() API and
// the EvaluatePreparedExpression() method should be renamed to EvaluatePrepared
absl::Status GoogleSqlLocalServiceImpl::Evaluate(const EvaluateRequest& request,
                                                 EvaluateResponse* response) {
  bool prepared = request.has_prepared_expression_id();
  std::shared_ptr<InternalPreparedExpressionState> state;
  std::vector<const google::protobuf::DescriptorPool*> pools;
  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;
  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids;
  // On error, make sure we don't leak any registered descriptor pools.
  auto descriptor_pool_cleanup = absl::MakeCleanup(
      absl::bind_front(&GoogleSqlLocalServiceImpl::CleanupDescriptorPools, this,
                       &owned_descriptor_pool_ids));
  if (prepared) {
    // Descriptor pools should only be transmitted during prepare (or the
    // the first call to evaluate, which is implicitly a Prepare).
    GOOGLESQL_RET_CHECK_EQ(request.descriptor_pool_list().definitions_size(), 0);
    int64_t id = request.prepared_expression_id();
    state = prepared_expressions_->Get(id);
    if (state == nullptr) {
      return MakeSqlError() << "Prepared expression " << id << " unknown.";
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                       descriptor_pool_states, pools));
    GOOGLESQL_RETURN_IF_ERROR(RegisterNewDescriptorPools(
        descriptor_pool_states, owned_descriptor_pool_ids,
        *(response->mutable_prepared()->mutable_descriptor_pool_id_list())));
    GOOGLESQL_ASSIGN_OR_RETURN(
        state, InternalPreparedExpressionState::CreateAndPrepareExpression(
                   request.sql(), request.options(), pools,
                   /*catalog=*/nullptr, owned_descriptor_pool_ids,
                   /*owned_catalog_id=*/std::nullopt));
  }

  GOOGLESQL_RETURN_IF_ERROR(EvaluatePreparedExpression(request, state.get(), response));

  if (!prepared) {
    GOOGLESQL_RETURN_IF_ERROR(RegisterPrepared(
        /*should_register_prepared=*/true, state, *prepared_expressions_, pools,
        response->mutable_prepared()));
  }

  // No errors, caller is now responsible for the prepared expression and
  // therefore any owned descriptor pools.
  std::move(descriptor_pool_cleanup).Cancel();
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::EvaluateQuery(
    const EvaluateQueryRequest& request, EvaluateQueryResponse* response) {
  std::optional<int64_t> prepared_query_id_opt =
      request.has_prepared_query_id()
          ? std::optional<int64_t>(request.prepared_query_id())
          : std::nullopt;
  return EvaluateImpl(request, request.table_content(), prepared_query_id_opt,
                      *prepared_queries_, "query", response);
}

absl::Status GoogleSqlLocalServiceImpl::EvaluateModify(
    const EvaluateModifyRequest& request, EvaluateModifyResponse* response) {
  std::optional<int64_t> prepared_modify_id_opt =
      request.has_prepared_modify_id()
          ? std::optional<int64_t>(request.prepared_modify_id())
          : std::nullopt;
  return EvaluateImpl(request, request.table_content(), prepared_modify_id_opt,
                      *prepared_modifies_, "modify", response);
}

template <typename RequestT, typename ResponseT, typename InternalStateT>
absl::Status GoogleSqlLocalServiceImpl::EvaluateImpl(
    const RequestT& request,
    const google::protobuf::Map<std::string, TableContent>& tables_contents,
    std::optional<int64_t>& prepared_statement_id_opt,
    SharedStatePool<InternalStateT>& prepared_statements_pool,
    absl::string_view statement_type, ResponseT* response) {
  std::shared_ptr<InternalStateT> internal_state;

  std::vector<const google::protobuf::DescriptorPool*> pools;
  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;
  std::shared_ptr<RegisteredCatalogState> catalog_state;

  bool prepared = prepared_statement_id_opt.has_value();
  if (prepared) {
    // At the moment there is no support for updating the tables' contents
    // once the statement has already been prepared
    if (!tables_contents.empty()) {
      return MakeSqlError()
             << "Modifying the content of a catalog of a prepared "
             << statement_type << " is not supported";
    }
    GOOGLESQL_RET_CHECK(tables_contents.empty())
        << "Modifying the content of a catalog of a prepared " << statement_type
        << " is not supported";
    // Descriptor pools should only be transmitted during prepare (or the
    // the first call to evaluate, which is implicitly a Prepare).
    GOOGLESQL_RET_CHECK_EQ(request.descriptor_pool_list().definitions_size(), 0);
    int64_t id = prepared_statement_id_opt.value();
    internal_state = prepared_statements_pool.Get(id);
    if (internal_state == nullptr) {
      return MakeSqlError()
             << "Prepared " << statement_type << " " << id << " unknown.";
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                       descriptor_pool_states, pools));

    GOOGLESQL_RETURN_IF_ERROR(
        GetCatalogState(request, tables_contents, pools, catalog_state));

    GOOGLESQL_RETURN_IF_ERROR(
        CreateAndPrepare(request.sql(), request.options(), catalog_state, pools,
                         /*owned_descriptor_pool_ids=*/{},
                         /*owned_catalog_id=*/std::nullopt, internal_state));

    GOOGLESQL_RETURN_IF_ERROR(RegisterPrepared(
        /*should_register_prepared=*/false, internal_state,
        prepared_statements_pool, pools, response->mutable_prepared()));
  }

  // When evaluating a prepared query or a prepared modify, the operation could
  // return the Status as Unimplemented in case one of the tables being touched
  // does not have its content set.
  // Here we converting the Unimplemented error into Unimplemented
  // which is more appropriate
  absl::Status evaluate_status =
      EvaluatePrepared(request, internal_state.get(), response);

  if (evaluate_status.code() == absl::StatusCode::kUnimplemented) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "One or more tables being queried do(es) not have"
           << " its/their content set. "
           << "[" << evaluate_status.message() << "]";
  }

  return evaluate_status;
}

absl::Status GoogleSqlLocalServiceImpl::EvaluatePreparedExpression(
    const EvaluateRequest& request,
    InternalPreparedExpressionState* internal_state,
    EvaluateResponse* response) {
  const AnalyzerOptions& analyzer_options =
      internal_state->GetAnalyzerOptions();

  ParameterValueMap columns, params;
  GOOGLESQL_RETURN_IF_ERROR(RepeatedParametersToMap(
      request.columns(), analyzer_options.expression_columns(), &columns));
  GOOGLESQL_RETURN_IF_ERROR(RepeatedParametersToMap(
      request.params(), analyzer_options.query_parameters(), &params));

  auto result =
      internal_state->GetExpression()->ExecuteAfterPrepare(columns, params);
  GOOGLESQL_RETURN_IF_ERROR(result.status());

  const Value& value = result.value();
  GOOGLESQL_RETURN_IF_ERROR(value.Serialize(response->mutable_value()));

  return absl::OkStatus();
}

template <>
absl::Status GoogleSqlLocalServiceImpl::EvaluatePrepared(
    const EvaluateQueryRequest& request,
    InternalPreparedQueryState* internal_state,
    EvaluateQueryResponse* response) {
  const AnalyzerOptions& analyzer_options =
      internal_state->GetAnalyzerOptions();

  ParameterValueMap params;
  GOOGLESQL_RETURN_IF_ERROR(RepeatedParametersToMap(
      request.params(), analyzer_options.query_parameters(), &params));

  PreparedQuery::QueryOptions options;
  options.parameters = std::move(params);

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableIterator> results_iterator,
                   internal_state->GetQuery()->ExecuteAfterPrepare(options));

  TableData* table_data = response->mutable_content()->mutable_table_data();
  while (results_iterator->NextRow()) {
    TableData::Row* row = table_data->add_row();
    for (int i = 0; i < results_iterator->NumColumns(); i++) {
      ValueProto* value = row->add_cell();
      GOOGLESQL_RETURN_IF_ERROR(results_iterator->GetValue(i).Serialize(value));
    }
  }

  return results_iterator->Status();
}

template <>
absl::Status GoogleSqlLocalServiceImpl::EvaluatePrepared(
    const EvaluateModifyRequest& request,
    InternalPreparedModifyState* internal_state,
    EvaluateModifyResponse* response) {
  const AnalyzerOptions& analyzer_options =
      internal_state->GetAnalyzerOptions();

  ParameterValueMap params;
  GOOGLESQL_RETURN_IF_ERROR(RepeatedParametersToMap(
      request.params(), analyzer_options.query_parameters(), &params));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<EvaluatorTableModifyIterator> results_iterator,
      internal_state->GetModify()->ExecuteAfterPrepare(params));

  const Table* table = results_iterator->table();
  int num_columns = table->NumColumns();
  uint64_t num_primary_keys =
      table->PrimaryKey().has_value() ? table->PrimaryKey().value().size() : 0;

  response->set_table_name(table->Name());

  while (results_iterator->NextRow()) {
    EvaluateModifyResponse::Row* row = response->add_content();

    GOOGLESQL_ASSIGN_OR_RETURN(auto operation, SerializeModifyOperation(
                                         results_iterator->GetOperation()));
    row->set_operation(operation);

    // If the SQL operation was a DELETE, then the values returned
    // by the iterator are Value::Invalid() which is not serializable and
    // does not have a ValueProto equivalent. So in this case,
    // we are not returning any values. For the rest, INSERT and UPDATE,
    // we return the new or updated values.
    if (results_iterator->GetOperation() !=
        EvaluatorTableModifyIterator::Operation::kDelete) {
      for (int i = 0; i < num_columns; i++) {
        ValueProto* value = row->add_cell();
        GOOGLESQL_RETURN_IF_ERROR(results_iterator->GetColumnValue(i).Serialize(value));
      }
    }

    for (int i = 0; i < num_primary_keys; i++) {
      ValueProto* value = row->add_old_primary_key();
      GOOGLESQL_RETURN_IF_ERROR(
          results_iterator->GetOriginalKeyValue(i).Serialize(value));
    }
  }

  return results_iterator->Status();
}

absl::Status GoogleSqlLocalServiceImpl::GetTableFromProto(
    const TableFromProtoRequest& request, SimpleTableProto* response) {
  TypeFactory factory;
  google::protobuf::DescriptorPool pool;
  GOOGLESQL_RETURN_IF_ERROR(
      AddFileDescriptorSetToPool(&request.file_descriptor_set(), &pool));
  const google::protobuf::Descriptor* proto_descr =
      pool.FindMessageTypeByName(request.proto().proto_name());
  if (proto_descr == nullptr) {
    return ::googlesql_base::UnknownErrorBuilder()
           << "Proto type name not found: " << request.proto().proto_name();
  }
  if (proto_descr->file()->name() != request.proto().proto_file_name()) {
    return ::googlesql_base::UnknownErrorBuilder()
           << "Proto " << request.proto().proto_name() << " found in "
           << proto_descr->file()->name() << ", not "
           << request.proto().proto_file_name() << " as specified.";
  }
  TableFromProto table(proto_descr->name());
  GOOGLESQL_RETURN_IF_ERROR(table.Init(proto_descr, &factory));
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_RETURN_IF_ERROR(table.Serialize(&file_descriptor_set_map, response));
  if (!file_descriptor_set_map.empty()) {
    GOOGLESQL_RET_CHECK_EQ(1, file_descriptor_set_map.size())
        << "Table from proto " << proto_descr->full_name()
        << " uses unknown DescriptorPool, this shouldn't happen.";
    GOOGLESQL_RET_CHECK_EQ(0, file_descriptor_set_map.at(&pool)->descriptor_set_index)
        << "Table from proto " << proto_descr->full_name()
        << " uses unknown DescriptorPool, this shouldn't happen.";
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::GetDescriptorPools(
    const DescriptorPoolListProto& descriptor_pool_list,
    std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>&
        descriptor_pool_states,
    std::vector<const google::protobuf::DescriptorPool*>& descriptor_pools) {
  using Definition = DescriptorPoolListProto::Definition;
  descriptor_pool_states.clear();
  descriptor_pools.clear();
  for (const Definition& definition : descriptor_pool_list.definitions()) {
    std::shared_ptr<RegisteredDescriptorPoolState> state;
    switch (definition.definition_case()) {
      case Definition::kFileDescriptorSet: {
        GOOGLESQL_ASSIGN_OR_RETURN(state, RegisteredDescriptorPoolState::Create(
                                    definition.file_descriptor_set()));
        break;
      }
      case Definition::kRegisteredId: {
        state = registered_descriptor_pools_->Get(definition.registered_id());
        if (state == nullptr) {
          return absl::Status(
              absl::StatusCode::kInvalidArgument,
              absl::StrCat("Invalid DescriptorPoolList::Definition: unknown "
                           "registered_id",
                           definition));
        }
        break;
      }
      case Definition::kBuiltin: {
        state = registered_descriptor_pools_->GetBuiltinDescriptorPoolState();
        break;
      }
      default:
        return absl::Status(
            absl::StatusCode::kInvalidArgument,
            absl::StrCat(
                "Invalid DescriptorPoolList::Definition contains unknown "
                "definition type",
                definition));
    }
    descriptor_pool_states.push_back(state);
    GOOGLESQL_RET_CHECK_NE(state->pool(), nullptr);
    descriptor_pools.push_back(state->pool());
  }

  return absl::OkStatus();
}

template <typename RequestProto>
absl::Status GoogleSqlLocalServiceImpl::GetCatalogState(
    const RequestProto& request,
    const google::protobuf::Map<std::string, TableContent>& tables_contents,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    std::shared_ptr<RegisteredCatalogState>& state) {
  if (request.has_registered_catalog_id()) {
    // At the moment there is no support for updating the tables' contents
    // of a registered catalog
    if (!tables_contents.empty()) {
      return MakeSqlError() << "Modifying the content of a registered catalog "
                               "is not supported";
    }
    int64_t id = request.registered_catalog_id();
    state = registered_catalogs_->Get(id);
    if (state == nullptr) {
      return MakeSqlError() << "Registered catalog " << id << " unknown.";
    }
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(state,
                     RegisteredCatalogState::Create(request.simple_catalog(),
                                                    tables_contents, pools));
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::Analyze(const AnalyzeRequest& request,
                                                AnalyzeResponse* response) {
  std::shared_ptr<RegisteredCatalogState> catalog_state;
  std::vector<const google::protobuf::DescriptorPool*> pools;
  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;

  GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                     descriptor_pool_states, pools));
  GOOGLESQL_RETURN_IF_ERROR(GetCatalogState(request, {}, pools, catalog_state));
  if (request.has_sql_expression()) {
    return AnalyzeExpressionImpl(request, pools, catalog_state->GetCatalog(),
                                 response);
  } else {
    return AnalyzeImpl(request, pools, catalog_state->GetCatalog(), response);
  }
}

absl::Status GoogleSqlLocalServiceImpl::AnalyzeImpl(
    const AnalyzeRequest& request,
    const std::vector<const google::protobuf::DescriptorPool*>& pools, Catalog* catalog,
    AnalyzeResponse* response) {
  AnalyzerOptions options;
  TypeFactory factory;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzerOptions::Deserialize(request.options(), pools,
                                               &factory, &options));

  if (!(request.has_sql_statement() || request.has_parse_resume_location())) {
    return ::googlesql_base::UnknownErrorBuilder()
           << "Unrecognized AnalyzeRequest target " << request.target_case();
  }
  std::unique_ptr<const AnalyzerOutput> output;

  if (request.has_sql_statement()) {
    absl::string_view sql = request.sql_statement();

    GOOGLESQL_RETURN_IF_ERROR(
        googlesql::AnalyzeStatement(sql, options, catalog, &factory, &output));

    GOOGLESQL_RETURN_IF_ERROR(
        SerializeResolvedOutput(output.get(), pools, sql, response));
  } else if (request.has_parse_resume_location()) {
    bool at_end_of_input;
    ParseResumeLocation location =
        ParseResumeLocation::FromProto(request.parse_resume_location());
    GOOGLESQL_RETURN_IF_ERROR(googlesql::AnalyzeNextStatement(
        &location, options, catalog, &factory, &output, &at_end_of_input));

    GOOGLESQL_RETURN_IF_ERROR(SerializeResolvedOutput(output.get(), pools,
                                            location.input(), response));
    response->set_resume_byte_position(location.byte_position());
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::AnalyzeExpressionImpl(
    const AnalyzeRequest& request,
    const std::vector<const google::protobuf::DescriptorPool*>& pools, Catalog* catalog,
    AnalyzeResponse* response) {
  AnalyzerOptions options;
  TypeFactory factory;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzerOptions::Deserialize(request.options(), pools,
                                               &factory, &options));

  if (request.has_sql_expression()) {
    std::unique_ptr<const AnalyzerOutput> output;
    TypeFactory factory;

    absl::string_view sql = request.sql_expression();

    GOOGLESQL_RETURN_IF_ERROR(
        googlesql::AnalyzeExpression(sql, options, catalog, &factory, &output));

    GOOGLESQL_RETURN_IF_ERROR(
        SerializeResolvedOutput(output.get(), pools, sql, response));
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::BuildSql(const BuildSqlRequest& request,
                                                 BuildSqlResponse* response) {
  std::shared_ptr<RegisteredCatalogState> catalog_state;
  std::vector<const google::protobuf::DescriptorPool*> pools;

  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;

  GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                     descriptor_pool_states, pools));
  GOOGLESQL_RETURN_IF_ERROR(GetCatalogState(request, {}, pools, catalog_state));
  IdStringPool string_pool;
  ResolvedNode::RestoreParams restore_params(
      pools, catalog_state->GetCatalog(),
      catalog_state->GetCatalog()->type_factory(), &string_pool);

  std::unique_ptr<ResolvedNode> ast;
  if (request.has_resolved_statement()) {
    ast = std::move(ResolvedStatement::RestoreFrom(request.resolved_statement(),
                                                   restore_params)
                        .value());
  } else if (request.has_resolved_expression()) {
    ast = std::move(
        ResolvedExpr::RestoreFrom(request.resolved_expression(), restore_params)
            .value());
  } else {
    return absl::OkStatus();
  }

  googlesql::SQLBuilder::SQLBuilderOptions builder_options;
  builder_options.catalog = catalog_state->GetCatalog();
  googlesql::SQLBuilder sql_builder(builder_options);

  GOOGLESQL_CHECK_OK(ast->Accept(&sql_builder));
  response->set_sql(sql_builder.sql());
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::ExtractTableNamesFromStatement(
    const ExtractTableNamesFromStatementRequest& request,
    ExtractTableNamesFromStatementResponse* response) {
  LanguageOptions language_options = request.has_options()
                                         ? LanguageOptions(request.options())
                                         : LanguageOptions();

  googlesql::TableNamesSet table_names;
  if (request.allow_script()) {
    GOOGLESQL_RETURN_IF_ERROR(googlesql::ExtractTableNamesFromScript(
        request.sql_statement(), googlesql::AnalyzerOptions(language_options),
        &table_names));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(googlesql::ExtractTableNamesFromStatement(
        request.sql_statement(), googlesql::AnalyzerOptions(language_options),
        &table_names));
  }
  for (const std::vector<std::string>& table_name : table_names) {
    ExtractTableNamesFromStatementResponse_TableName* table_name_field =
        response->add_table_name();
    for (const std::string& name_segment : table_name) {
      table_name_field->add_table_name_segment(name_segment);
    }
  }
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::ExtractTableNamesFromNextStatement(
    const ExtractTableNamesFromNextStatementRequest& request,
    ExtractTableNamesFromNextStatementResponse* response) {
  ParseResumeLocation location =
      ParseResumeLocation::FromProto(request.parse_resume_location());

  LanguageOptions language_options = request.has_options()
                                         ? LanguageOptions(request.options())
                                         : LanguageOptions();

  bool at_end_of_input;
  googlesql::TableNamesSet table_names;
  GOOGLESQL_RETURN_IF_ERROR(googlesql::ExtractTableNamesFromNextStatement(
      &location, googlesql::AnalyzerOptions(language_options), &table_names,
      &at_end_of_input));

  for (const std::vector<std::string>& table_name : table_names) {
    ExtractTableNamesFromNextStatementResponse_TableName* table_name_field =
        response->add_table_name();
    for (const std::string& name_segment : table_name) {
      table_name_field->add_table_name_segment(name_segment);
    }
  }

  response->set_resume_byte_position(location.byte_position());

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::SerializeResolvedOutput(
    const AnalyzerOutput* output,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    absl::string_view statement, AnalyzeResponse* response) {
  FileDescriptorSetMap file_descriptor_set_map;
  PopulateExistingPoolsToFileDescriptorSetMap(pools, &file_descriptor_set_map);

  if (output->resolved_statement() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(output->resolved_statement()->SaveTo(
        &file_descriptor_set_map, response->mutable_resolved_statement()));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(output->resolved_expr()->SaveTo(
        &file_descriptor_set_map, response->mutable_resolved_expression()));
  }

  // If the file_descriptor_set_map contains more descriptor pools than those
  // passed in the request, the additional one must be the generated descriptor
  // pool. The reason is that some built-in functions use the DatetimePart
  // enum whose descriptor comes from the generated pool.
  // TODO: Describe the descriptor pool passing contract in detail
  // with a doc, and put a link here.
  if (file_descriptor_set_map.size() != pools.size()) {
    GOOGLESQL_RET_CHECK_EQ(file_descriptor_set_map.size(), pools.size() + 1)
        << "Analyzer result of " << statement
        << " uses unknown DescriptorPool, this shouldn't happen.";
    const auto& entry =
        file_descriptor_set_map.at(google::protobuf::DescriptorPool::generated_pool());
    GOOGLESQL_RET_CHECK_NE(entry.get(), nullptr)
        << "Analyzer result of " << statement
        << " uses unknown DescriptorPool, this shouldn't happen.";
    GOOGLESQL_RET_CHECK_EQ(entry->descriptor_set_index, pools.size())
        << "Analyzer result of " << statement
        << " uses unknown DescriptorPool, this shouldn't happen.";
  }

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::FormatSql(
    const FormatSqlRequest& request, FormatSqlResponse* response) {
  return ::googlesql::FormatSql(request.sql(), response->mutable_sql());
}

absl::Status GoogleSqlLocalServiceImpl::LenientFormatSql(
    const FormatSqlRequest& request, FormatSqlResponse* response) {
  ::googlesql::FormatterOptions options(request.options());
  std::vector<FormatterRange> ranges(request.byte_ranges_size());
  std::transform(request.byte_ranges().begin(), request.byte_ranges().end(),
                 ranges.begin(), [](FormatterRangeProto f) {
                   return FormatterRange({.start = f.start(), .end = f.end()});
                 });
  absl::StatusOr<std::string> result =
      ::googlesql::LenientFormatSqlByteRanges(request.sql(), ranges, options);
  if (result.ok()) {
    response->set_sql(result.value());
  }
  return result.status();
}

absl::Status GoogleSqlLocalServiceImpl::RegisterCatalog(
    const RegisterCatalogRequest& request, RegisterResponse* response) {
  std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>
      descriptor_pool_states;
  std::vector<const google::protobuf::DescriptorPool*> pools;

  GOOGLESQL_RETURN_IF_ERROR(GetDescriptorPools(request.descriptor_pool_list(),
                                     descriptor_pool_states, pools));

  absl::flat_hash_set<int64_t> owned_descriptor_pool_ids;
  // On error, make sure we don't leak any registered descriptor pools.
  auto descriptor_pool_cleanup = absl::MakeCleanup(
      absl::bind_front(&GoogleSqlLocalServiceImpl::CleanupDescriptorPools, this,
                       &owned_descriptor_pool_ids));
  for (std::shared_ptr<RegisteredDescriptorPoolState>& pool_state :
       descriptor_pool_states) {
    if (!pool_state->IsRegistered()) {
      // Not registered, so we registered it, and own it.
      int64_t pool_id = registered_descriptor_pools_->Register(pool_state);
      GOOGLESQL_RET_CHECK_NE(-1, pool_id)
          << "Failed to register descriptor pool, this shouldn't happen";
      owned_descriptor_pool_ids.insert(pool_id);
    }
    response->mutable_descriptor_pool_id_list()->add_registered_ids(
        pool_state->GetId());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RegisteredCatalogState> state,
                   RegisteredCatalogState::Create(
                       request.simple_catalog(), request.table_content(), pools,
                       owned_descriptor_pool_ids));
  int64_t id = registered_catalogs_->Register(std::move(state));
  GOOGLESQL_RET_CHECK_NE(-1, id) << "Failed to register catalog, this shouldn't happen.";

  response->set_registered_id(id);
  // No errors, caller is now responsible for the prepared expression and
  // therefore any owned descriptor pools.
  std::move(descriptor_pool_cleanup).Cancel();

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::UnregisterCatalog(int64_t id) {
  std::shared_ptr<RegisteredCatalogState> state = registered_catalogs_->Get(id);
  if (state == nullptr) {
    return MakeSqlError() << "Unknown catalog ID: " << id;
  }

  absl::Status status;
  for (int64_t pool_id : state->owned_descriptor_pool_ids()) {
    if (!registered_descriptor_pools_->Delete(pool_id)) {
      status = MakeSqlError() << "Unknown descriptor pool ID: " << pool_id;
    }
  }
  if (!registered_catalogs_->Delete(id)) {
    status = MakeSqlError() << "Failed to fully delete catalog ID: " << id;
  }
  return status;
}

absl::Status GoogleSqlLocalServiceImpl::GetBuiltinFunctions(
    const GoogleSQLBuiltinFunctionOptionsProto& proto,
    GetBuiltinFunctionsResponse* resp) {
  TypeFactory factory;
  absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>>
      table_valued_functions;
  BuiltinFunctionOptions options(proto);
  absl::flat_hash_map<std::string, const Type*> types;

  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionsAndTypes(options, factory, functions,
                                              types, table_valued_functions));

  FileDescriptorSetMap file_descriptor_set_map;
  for (const auto& function : functions) {
    GOOGLESQL_RETURN_IF_ERROR(function.second->Serialize(&file_descriptor_set_map,
                                               resp->add_function()));
  }

  for (const auto& table_valued_function : table_valued_functions) {
    GOOGLESQL_RETURN_IF_ERROR(table_valued_function.second->Serialize(
        &file_descriptor_set_map, resp->add_table_valued_function()));
  }

  auto& response_types = *resp->mutable_types();
  for (const auto& [name, type] : types) {
    TypeProto& type_proto = response_types[name];
    GOOGLESQL_RETURN_IF_ERROR(type->SerializeToProtoAndDistinctFileDescriptors(
        &type_proto, &file_descriptor_set_map));
  }

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::GetLanguageOptions(
    const LanguageOptionsRequest& request, LanguageOptionsProto* response) {
  googlesql::LanguageOptions options;
  if (request.has_maximum_features() && request.maximum_features()) {
    options.EnableMaximumLanguageFeatures();
  }
  if (request.has_language_version()) {
    options.SetLanguageVersion(request.language_version());
  }
  options.Serialize(response);
  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::GetAnalyzerOptions(
    const AnalyzerOptionsRequest& request, AnalyzerOptionsProto* response) {
  googlesql::AnalyzerOptions options;
  FileDescriptorSetMap unused_map;
  return options.Serialize(&unused_map, response);
}

absl::Status GoogleSqlLocalServiceImpl::Parse(const ParseRequest& request,
                                              ParseResponse* response) {
  auto language_options =
      request.has_options()
          ? std::make_unique<LanguageOptions>(request.options())
          : std::make_unique<LanguageOptions>();

  ParserOptions parser_options = ParserOptions(
      /*id_string_pool=*/nullptr, /*arena=*/nullptr,
      language_options ? *language_options : LanguageOptions());

  if (request.allow_script()) {
    return ParseScriptImpl(request, response, parser_options);
  } else {
    return ParseStatementImpl(request, response, parser_options);
  }
}

absl::Status GoogleSqlLocalServiceImpl::ParseStatementImpl(
    const ParseRequest& request, ParseResponse* response,
    ParserOptions& parser_options) {
  std::unique_ptr<ParserOutput> parser_output;

  if (request.has_sql_statement()) {
    absl::string_view sql = request.sql_statement();

    GOOGLESQL_RETURN_IF_ERROR(ParseStatement(sql, parser_options, &parser_output));

    return ParseTreeSerializer::Serialize(parser_output->statement(),
                                          response->mutable_parsed_statement());
  } else if (request.has_parse_resume_location()) {
    bool at_end_of_input;
    ParseResumeLocation location =
        ParseResumeLocation::FromProto(request.parse_resume_location());
    GOOGLESQL_RETURN_IF_ERROR(ParseNextStatement(&location, parser_options,
                                       &parser_output, &at_end_of_input));

    response->set_resume_byte_position(location.byte_position());
    return ParseTreeSerializer::Serialize(parser_output->statement(),
                                          response->mutable_parsed_statement());
  } else {
    return ::googlesql_base::UnknownErrorBuilder() << "ParseRequest target not set";
  }

  return absl::OkStatus();
}

absl::Status GoogleSqlLocalServiceImpl::ParseScriptImpl(
    const ParseRequest& request, ParseResponse* response,
    ParserOptions& parser_options) {
  std::unique_ptr<ParserOutput> parser_output;

  if (request.has_sql_statement()) {
    absl::string_view sql = request.sql_statement();

    GOOGLESQL_RETURN_IF_ERROR(
        ParseScript(sql, parser_options,
                    {.mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
                     .attach_error_location_payload =
                         (ErrorMessageMode::ERROR_MESSAGE_ONE_LINE ==
                          ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD),
                     .stability = GetDefaultErrorMessageStability()},
                    &parser_output));

    return ParseTreeSerializer::Serialize(parser_output->script(),
                                          response->mutable_parsed_script());
  } else if (request.has_parse_resume_location()) {
    bool at_end_of_input;
    ParseResumeLocation location =
        ParseResumeLocation::FromProto(request.parse_resume_location());
    GOOGLESQL_RETURN_IF_ERROR(ParseNextScriptStatement(&location, parser_options,
                                             &parser_output, &at_end_of_input));

    response->set_resume_byte_position(location.byte_position());
    return ParseTreeSerializer::Serialize(parser_output->statement(),
                                          response->mutable_parsed_statement());
  } else {
    return ::googlesql_base::UnknownErrorBuilder() << "ParseRequest target not set";
  }

  return absl::OkStatus();
}

size_t GoogleSqlLocalServiceImpl::NumRegisteredDescriptorPools() const {
  return registered_descriptor_pools_->NumSavedStates();
}

size_t GoogleSqlLocalServiceImpl::NumRegisteredCatalogs() const {
  return registered_catalogs_->NumSavedStates();
}

size_t GoogleSqlLocalServiceImpl::NumSavedPreparedExpression() const {
  return prepared_expressions_->NumSavedStates();
}

size_t GoogleSqlLocalServiceImpl::NumSavedPreparedQueries() const {
  return prepared_queries_->NumSavedStates();
}

size_t GoogleSqlLocalServiceImpl::NumSavedPreparedModifies() const {
  return prepared_modifies_->NumSavedStates();
}

}  // namespace local_service
}  // namespace googlesql
