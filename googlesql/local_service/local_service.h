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

#ifndef GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_H_
#define GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/local_service/local_service.pb.h"
#include "googlesql/local_service/state.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/parse_resume_location.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_table.pb.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace local_service {

class InternalPreparedExpressionState;
class InternalPreparedModifyState;
class InternalPreparedQueryState;
class PreparedExpressionPool;
class PreparedModifyPool;
class PreparedQueryPool;
class RegisteredCatalogPool;
class RegisteredCatalogState;
class RegisteredDescriptorPoolPool;
class RegisteredDescriptorPoolState;

// Implementation of GoogleSqlLocalService RPC service.
class GoogleSqlLocalServiceImpl {
 public:
  GoogleSqlLocalServiceImpl();
  GoogleSqlLocalServiceImpl(const GoogleSqlLocalServiceImpl&) = delete;
  GoogleSqlLocalServiceImpl& operator=(const GoogleSqlLocalServiceImpl&) =
      delete;
  ~GoogleSqlLocalServiceImpl();

  absl::Status Prepare(const PrepareRequest& request,
                       PrepareResponse* response);

  absl::Status Unprepare(int64_t id);

  absl::Status Evaluate(const EvaluateRequest& request,
                        EvaluateResponse* response);

  absl::Status PrepareQuery(const PrepareQueryRequest& request,
                            PrepareQueryResponse* response);

  absl::Status UnprepareQuery(int64_t id);

  absl::Status EvaluateQuery(const EvaluateQueryRequest& request,
                             EvaluateQueryResponse* response);

  absl::Status PrepareModify(const PrepareModifyRequest& request,
                             PrepareModifyResponse* response);

  absl::Status UnprepareModify(int64_t id);

  absl::Status EvaluateModify(const EvaluateModifyRequest& request,
                              EvaluateModifyResponse* response);

  absl::Status GetTableFromProto(const TableFromProtoRequest& request,
                                 SimpleTableProto* response);

  absl::Status GetBuiltinFunctions(
      const GoogleSQLBuiltinFunctionOptionsProto& proto,
      GetBuiltinFunctionsResponse* resp);

  absl::Status Analyze(const AnalyzeRequest& request,
                       AnalyzeResponse* response);

  absl::Status AnalyzeImpl(
      const AnalyzeRequest& request,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      // Analyzer requires a non-const catalog, however
      // is not mutated in practice.
      Catalog* catalog, AnalyzeResponse* response);

  absl::Status AnalyzeExpressionImpl(
      const AnalyzeRequest& request,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      // Analyzer requires a non-const catalog, however
      // is not mutated in practice.
      Catalog* catalog, AnalyzeResponse* response);

  absl::Status BuildSql(const BuildSqlRequest& request,
                        BuildSqlResponse* response);

  // Note, this also can handle scripts.
  absl::Status ExtractTableNamesFromStatement(
      const ExtractTableNamesFromStatementRequest& request,
      ExtractTableNamesFromStatementResponse* response);

  // Note, this does not handle scripts.
  absl::Status ExtractTableNamesFromNextStatement(
      const ExtractTableNamesFromNextStatementRequest& request,
      ExtractTableNamesFromNextStatementResponse* response);

  absl::Status SerializeResolvedOutput(
      const AnalyzerOutput* output,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      absl::string_view statement, AnalyzeResponse* response);

  absl::Status FormatSql(const FormatSqlRequest& request,
                         FormatSqlResponse* response);

  absl::Status LenientFormatSql(const FormatSqlRequest& request,
                                FormatSqlResponse* response);

  absl::Status RegisterCatalog(const RegisterCatalogRequest& request,
                               RegisterResponse* response);

  absl::Status UnregisterCatalog(int64_t id);

  absl::Status GetLanguageOptions(const LanguageOptionsRequest& request,
                                  LanguageOptionsProto* response);

  absl::Status GetAnalyzerOptions(const AnalyzerOptionsRequest& request,
                                  AnalyzerOptionsProto* response);

  absl::Status Parse(const ParseRequest& request, ParseResponse* response);

  absl::Status ParseStatementImpl(const ParseRequest& request,
                                  ParseResponse* response,
                                  ParserOptions& parser_options);

  absl::Status ParseScriptImpl(const ParseRequest& request,
                               ParseResponse* response,
                               ParserOptions& parser_options);

 private:
  // Fetches the descriptor pools for the given descriptor_pool_list.
  // descriptor_pools is a view into pool_states_out, and is returned as a
  // convenience for calls into the google Deserialize calls..
  // This will _not_ register the returned states, although it will retrieve
  // states based on registered_id as necessary.
  absl::Status GetDescriptorPools(
      const DescriptorPoolListProto& descriptor_pool_list,
      std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>&
          pool_states_out,
      std::vector<const google::protobuf::DescriptorPool*>& descriptor_pools);

  // Registers each entry in <descriptor_pool_states> if not already registered
  // and returns a list of the newly registered objects in
  // <owned_descriptor_pool_ids>.
  // Also sets <descriptor_pool_id_list> to the appropriate ids as a
  // convenience.
  absl::Status RegisterNewDescriptorPools(
      std::vector<std::shared_ptr<RegisteredDescriptorPoolState>>&
          descriptor_pool_states,
      absl::flat_hash_set<int64_t>& registered_descriptor_pool_ids,
      DescriptorPoolIdList& descriptor_pool_id_list);

  template <typename RequestProto>
  absl::Status GetCatalogState(
      const RequestProto& request,
      const google::protobuf::Map<std::string, TableContent>& tables_contents,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      std::shared_ptr<RegisteredCatalogState>& state);

  std::unique_ptr<RegisteredDescriptorPoolPool> registered_descriptor_pools_;
  std::unique_ptr<RegisteredCatalogPool> registered_catalogs_;
  std::unique_ptr<PreparedExpressionPool> prepared_expressions_;
  std::unique_ptr<PreparedQueryPool> prepared_queries_;
  std::unique_ptr<PreparedModifyPool> prepared_modifies_;

  template <typename InternalStateT>
  absl::Status CreateAndPrepare(
      absl::string_view sql, const AnalyzerOptionsProto& options,
      std::shared_ptr<RegisteredCatalogState> catalog_state,
      std::vector<const google::protobuf::DescriptorPool*> pools,
      absl::flat_hash_set<int64_t> owned_descriptor_pool_ids,
      std::optional<int64_t> owned_catalog_id,
      std::shared_ptr<InternalStateT>& internal_state);

  template <typename ResponseStateT, typename InternalStateT>
  absl::Status RegisterPrepared(
      bool should_register_prepared,
      std::shared_ptr<InternalStateT> internal_state,
      SharedStatePool<InternalStateT>& prepared_statements_pool,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      ResponseStateT* response_state);

  template <typename RequestT, typename ResponseT, typename InternalStateT>
  absl::Status PrepareImpl(
      const RequestT& request,
      const ::google::protobuf::Map<std::string, TableContent>& tables_contents,
      SharedStatePool<InternalStateT>& prepared_statements_pool,
      ResponseT* response);

  template <typename InternalStateT>
  absl::Status UnprepareImpl(
      SharedStatePool<InternalStateT>& prepared_statements_pool, int64_t id,
      absl::string_view statement_type);

  template <typename RequestT, typename ResponseT, typename InternalStateT>
  absl::Status EvaluateImpl(
      const RequestT& request,
      const google::protobuf::Map<std::string, TableContent>& tables_contents,
      std::optional<int64_t>& prepared_statement_id_opt,
      SharedStatePool<InternalStateT>& prepared_statements_pool,
      absl::string_view statement_type, ResponseT* response);

  template <typename RequestT, typename ResponseT, typename InternalStateT>
  absl::Status EvaluatePrepared(const RequestT& request,
                                ResponseT* internal_state,
                                InternalStateT* response);

  absl::Status EvaluatePreparedExpression(
      const EvaluateRequest& request,
      InternalPreparedExpressionState* internal_state,
      EvaluateResponse* response);

  void CleanupDescriptorPools(
      absl::flat_hash_set<int64_t>* descriptor_pool_ids);

  void CleanupCatalog(std::optional<int64_t>* catalog_id);

  // For testing.
  size_t NumRegisteredDescriptorPools() const;
  size_t NumRegisteredCatalogs() const;
  size_t NumSavedPreparedExpression() const;
  size_t NumSavedPreparedQueries() const;
  size_t NumSavedPreparedModifies() const;

  friend class GoogleSqlLocalServiceImplTest;
};

}  // namespace local_service
}  // namespace googlesql

#endif  // GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_H_
