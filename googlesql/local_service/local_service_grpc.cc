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

#include "googlesql/local_service/local_service_grpc.h"

#include "googlesql/base/status.h"

namespace googlesql {
namespace local_service {

namespace {

grpc::Status ToGrpcStatus(absl::Status status) {
  if (status.ok()) {
    return grpc::Status();
  }
  grpc::StatusCode grpc_code;
  switch (status.code()) {
    case absl::StatusCode::kCancelled:
      grpc_code = grpc::CANCELLED;
      break;
    case absl::StatusCode::kInvalidArgument:
      grpc_code = grpc::INVALID_ARGUMENT;
      break;
    case absl::StatusCode::kDeadlineExceeded:
      grpc_code = grpc::DEADLINE_EXCEEDED;
      break;
    case absl::StatusCode::kNotFound:
      grpc_code = grpc::NOT_FOUND;
      break;
    case absl::StatusCode::kAlreadyExists:
      grpc_code = grpc::ALREADY_EXISTS;
      break;
    case absl::StatusCode::kPermissionDenied:
      grpc_code = grpc::PERMISSION_DENIED;
      break;
    case absl::StatusCode::kResourceExhausted:
      grpc_code = grpc::RESOURCE_EXHAUSTED;
      break;
    case absl::StatusCode::kFailedPrecondition:
      grpc_code = grpc::FAILED_PRECONDITION;
      break;
    case absl::StatusCode::kAborted:
      grpc_code = grpc::ABORTED;
      break;
    case absl::StatusCode::kOutOfRange:
      grpc_code = grpc::OUT_OF_RANGE;
      break;
    case absl::StatusCode::kUnimplemented:
      grpc_code = grpc::UNIMPLEMENTED;
      break;
    case absl::StatusCode::kInternal:
      grpc_code = grpc::INTERNAL;
      break;
    case absl::StatusCode::kUnavailable:
      grpc_code = grpc::UNAVAILABLE;
      break;
    case absl::StatusCode::kDataLoss:
      grpc_code = grpc::DATA_LOSS;
      break;
    case absl::StatusCode::kUnauthenticated:
      grpc_code = grpc::UNAUTHENTICATED;
      break;
    default:
      grpc_code = grpc::UNKNOWN;
  }
  return grpc::Status(grpc_code, std::string(status.message()), "");
}

}  // namespace

grpc::Status GoogleSqlLocalServiceGrpcImpl::Prepare(
    grpc::ServerContext* context, const PrepareRequest* req,
    PrepareResponse* resp) {
  return ToGrpcStatus(service_.Prepare(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::Unprepare(
    grpc::ServerContext* context, const UnprepareRequest* req,
    google::protobuf::Empty* unused) {
  return ToGrpcStatus(service_.Unprepare(req->prepared_expression_id()));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::Evaluate(
    grpc::ServerContext* context, const EvaluateRequest* req,
    EvaluateResponse* resp) {
  return ToGrpcStatus(service_.Evaluate(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::EvaluateStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<EvaluateResponseBatch, EvaluateRequestBatch>*
        stream) {
  EvaluateRequestBatch reqb;
  while (stream->Read(&reqb)) {
    EvaluateResponseBatch respb;
    for (const auto& req : reqb.request()) {
      EvaluateResponse* resp = respb.add_response();
      auto status = service_.Evaluate(req, resp);
      if (!status.ok()) {
        return ToGrpcStatus(status);
      }
      if (respb.response_size() > 1 &&
          respb.ByteSizeLong() > GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH) {
        // The response pushed us over the max message size. Remove it from the
        // batch, send prior responses, then add it back.
        auto* last = respb.mutable_response()->ReleaseLast();
        stream->Write(respb, grpc::WriteOptions().set_corked());
        respb.Clear();
        respb.mutable_response()->AddAllocated(last);
      }
    }
    stream->Write(respb);
  }
  return grpc::Status();
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::PrepareQuery(
    grpc::ServerContext* context, const PrepareQueryRequest* req,
    PrepareQueryResponse* resp) {
  return ToGrpcStatus(service_.PrepareQuery(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::UnprepareQuery(
    grpc::ServerContext* context, const UnprepareQueryRequest* req,
    google::protobuf::Empty* unused) {
  return ToGrpcStatus(service_.UnprepareQuery(req->prepared_query_id()));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::EvaluateQuery(
    grpc::ServerContext* context, const EvaluateQueryRequest* req,
    EvaluateQueryResponse* resp) {
  return ToGrpcStatus(service_.EvaluateQuery(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::EvaluateQueryStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<EvaluateQueryBatchResponse,
                             EvaluateQueryBatchRequest>* stream) {
  EvaluateQueryBatchRequest reqb;
  while (stream->Read(&reqb)) {
    EvaluateQueryBatchResponse respb;
    for (const auto& req : reqb.request()) {
      EvaluateQueryResponse* resp = respb.add_response();
      auto status = service_.EvaluateQuery(req, resp);
      if (!status.ok()) {
        return ToGrpcStatus(status);
      }
      if (respb.response_size() > 1 &&
          respb.ByteSizeLong() > GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH) {
        // The response pushed us over the max message size. Remove it from the
        // batch, send prior responses, then add it back.
        auto* last = respb.mutable_response()->ReleaseLast();
        stream->Write(respb, grpc::WriteOptions().set_corked());
        respb.Clear();
        respb.mutable_response()->AddAllocated(last);
      }
    }
    stream->Write(respb);
  }
  return grpc::Status();
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::PrepareModify(
    grpc::ServerContext* context, const PrepareModifyRequest* req,
    PrepareModifyResponse* resp) {
  return ToGrpcStatus(service_.PrepareModify(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::UnprepareModify(
    grpc::ServerContext* context, const UnprepareModifyRequest* req,
    google::protobuf::Empty* unused) {
  return ToGrpcStatus(service_.UnprepareModify(req->prepared_modify_id()));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::EvaluateModify(
    grpc::ServerContext* context, const EvaluateModifyRequest* req,
    EvaluateModifyResponse* resp) {
  return ToGrpcStatus(service_.EvaluateModify(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::EvaluateModifyStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<EvaluateModifyBatchResponse,
                             EvaluateModifyBatchRequest>* stream) {
  EvaluateModifyBatchRequest reqb;
  while (stream->Read(&reqb)) {
    EvaluateModifyBatchResponse respb;
    for (const auto& req : reqb.request()) {
      EvaluateModifyResponse* resp = respb.add_response();
      auto status = service_.EvaluateModify(req, resp);
      if (!status.ok()) {
        return ToGrpcStatus(status);
      }
      if (respb.response_size() > 1 &&
          respb.ByteSizeLong() > GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH) {
        // The response pushed us over the max message size. Remove it from the
        // batch, send prior responses, then add it back.
        auto* last = respb.mutable_response()->ReleaseLast();
        stream->Write(respb, grpc::WriteOptions().set_corked());
        respb.Clear();
        respb.mutable_response()->AddAllocated(last);
      }
    }
    stream->Write(respb);
  }
  return grpc::Status();
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::GetTableFromProto(
    grpc::ServerContext* context, const TableFromProtoRequest* req,
    SimpleTableProto* resp) {
  return ToGrpcStatus(service_.GetTableFromProto(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::Analyze(
    grpc::ServerContext* context, const AnalyzeRequest* req,
    AnalyzeResponse* resp) {
  return ToGrpcStatus(service_.Analyze(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::BuildSql(
    grpc::ServerContext* context, const BuildSqlRequest* req,
    BuildSqlResponse* resp) {
  return ToGrpcStatus(service_.BuildSql(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::ExtractTableNamesFromStatement(
    grpc::ServerContext* context,
    const ExtractTableNamesFromStatementRequest* req,
    ExtractTableNamesFromStatementResponse* resp) {
  return ToGrpcStatus(service_.ExtractTableNamesFromStatement(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::ExtractTableNamesFromNextStatement(
    grpc::ServerContext* context,
    const ExtractTableNamesFromNextStatementRequest* req,
    ExtractTableNamesFromNextStatementResponse* resp) {
  return ToGrpcStatus(service_.ExtractTableNamesFromNextStatement(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::FormatSql(
    grpc::ServerContext* context, const FormatSqlRequest* req,
    FormatSqlResponse* resp) {
  return ToGrpcStatus(service_.FormatSql(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::LenientFormatSql(
    grpc::ServerContext* context, const FormatSqlRequest* req,
    FormatSqlResponse* resp) {
  return ToGrpcStatus(service_.LenientFormatSql(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::RegisterCatalog(
    grpc::ServerContext* context, const RegisterCatalogRequest* req,
    RegisterResponse* resp) {
  return ToGrpcStatus(service_.RegisterCatalog(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::UnregisterCatalog(
    grpc::ServerContext* context, const UnregisterRequest* req,
    google::protobuf::Empty* unused) {
  return ToGrpcStatus(service_.UnregisterCatalog(req->registered_id()));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::GetBuiltinFunctions(
    grpc::ServerContext* context,
    const GoogleSQLBuiltinFunctionOptionsProto* options,
    GetBuiltinFunctionsResponse* resp) {
  return ToGrpcStatus(service_.GetBuiltinFunctions(*options, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::GetLanguageOptions(
    grpc::ServerContext* context, const LanguageOptionsRequest* req,
    LanguageOptionsProto* resp) {
  return ToGrpcStatus(service_.GetLanguageOptions(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::GetAnalyzerOptions(
    grpc::ServerContext* context, const AnalyzerOptionsRequest* req,
    AnalyzerOptionsProto* resp) {
  return ToGrpcStatus(service_.GetAnalyzerOptions(*req, resp));
}

grpc::Status GoogleSqlLocalServiceGrpcImpl::Parse(
    grpc::ServerContext* context, const ParseRequest* req,
    ParseResponse* resp) {
  return ToGrpcStatus(service_.Parse(*req, resp));
}

}  // namespace local_service
}  // namespace googlesql
