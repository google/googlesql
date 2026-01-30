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

#ifndef GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_GRPC_H_
#define GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_GRPC_H_

#include "googlesql/local_service/local_service.grpc.pb.h"
#include "googlesql/local_service/local_service.h"
#include "googlesql/local_service/local_service.pb.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/parse_resume_location.pb.h"
#include "googlesql/public/simple_table.pb.h"

namespace googlesql {
namespace local_service {

// Implementation of GoogleSqlLocalService Grpc RPC service.
class GoogleSqlLocalServiceGrpcImpl
    : public GoogleSqlLocalService::Service {
 public:
  grpc::Status Prepare(grpc::ServerContext* context, const PrepareRequest* req,
                       PrepareResponse* resp) override;

  grpc::Status Unprepare(grpc::ServerContext* context,
                         const UnprepareRequest* req,
                         google::protobuf::Empty* unused) override;

  grpc::Status Evaluate(grpc::ServerContext* context,
                        const EvaluateRequest* req,
                        EvaluateResponse* resp) override;

  grpc::Status EvaluateStream(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<EvaluateResponseBatch, EvaluateRequestBatch>*
          stream) override;

  grpc::Status PrepareQuery(grpc::ServerContext* context,
                            const PrepareQueryRequest* req,
                            PrepareQueryResponse* resp) override;

  grpc::Status UnprepareQuery(grpc::ServerContext* context,
                              const UnprepareQueryRequest* req,
                              google::protobuf::Empty* unused) override;

  grpc::Status EvaluateQuery(grpc::ServerContext* context,
                             const EvaluateQueryRequest* req,
                             EvaluateQueryResponse* resp) override;

  grpc::Status EvaluateQueryStream(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<EvaluateQueryBatchResponse,
                               EvaluateQueryBatchRequest>* stream) override;

  grpc::Status PrepareModify(grpc::ServerContext* context,
                             const PrepareModifyRequest* req,
                             PrepareModifyResponse* resp) override;

  grpc::Status UnprepareModify(grpc::ServerContext* context,
                               const UnprepareModifyRequest* req,
                               google::protobuf::Empty* unused) override;

  grpc::Status EvaluateModify(grpc::ServerContext* context,
                              const EvaluateModifyRequest* req,
                              EvaluateModifyResponse* resp) override;

  grpc::Status EvaluateModifyStream(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<EvaluateModifyBatchResponse,
                               EvaluateModifyBatchRequest>* stream) override;

  grpc::Status GetTableFromProto(grpc::ServerContext* context,
                                 const TableFromProtoRequest* req,
                                 SimpleTableProto* resp) override;

  grpc::Status Analyze(grpc::ServerContext* context, const AnalyzeRequest* req,
                       AnalyzeResponse* resp) override;

  grpc::Status BuildSql(grpc::ServerContext* context,
                        const BuildSqlRequest* req,
                        BuildSqlResponse* resp) override;

  grpc::Status ExtractTableNamesFromStatement(
      grpc::ServerContext* context,
      const ExtractTableNamesFromStatementRequest* req,
      ExtractTableNamesFromStatementResponse* resp) override;

  grpc::Status ExtractTableNamesFromNextStatement(
      grpc::ServerContext* context,
      const ExtractTableNamesFromNextStatementRequest* req,
      ExtractTableNamesFromNextStatementResponse* resp) override;

  grpc::Status FormatSql(grpc::ServerContext* context,
                         const FormatSqlRequest* req,
                         FormatSqlResponse* resp) override;

  grpc::Status LenientFormatSql(grpc::ServerContext* context,
                                const FormatSqlRequest* req,
                                FormatSqlResponse* resp) override;

  grpc::Status RegisterCatalog(grpc::ServerContext* context,
                               const RegisterCatalogRequest* req,
                               RegisterResponse* resp) override;

  grpc::Status UnregisterCatalog(grpc::ServerContext* context,
                                 const UnregisterRequest* req,
                                 google::protobuf::Empty* unused) override;

  grpc::Status GetBuiltinFunctions(
      grpc::ServerContext* context,
      const GoogleSQLBuiltinFunctionOptionsProto* options,
      GetBuiltinFunctionsResponse* resp) override;

  grpc::Status GetLanguageOptions(grpc::ServerContext* context,
                                  const LanguageOptionsRequest* req,
                                  LanguageOptionsProto* resp) override;

  grpc::Status GetAnalyzerOptions(grpc::ServerContext* context,
                                  const AnalyzerOptionsRequest* req,
                                  AnalyzerOptionsProto* resp) override;

  grpc::Status Parse(grpc::ServerContext* context,
                     const ParseRequest* req,
                     ParseResponse* resp) override;

 private:
  GoogleSqlLocalServiceImpl service_;
};

}  // namespace local_service
}  // namespace googlesql

#endif  // GOOGLESQL_LOCAL_SERVICE_LOCAL_SERVICE_GRPC_H_
