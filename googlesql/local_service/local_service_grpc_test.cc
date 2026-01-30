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

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/local_service/local_service.grpc.pb.h"
#include "googlesql/local_service/local_service.h"
#include "googlesql/local_service/local_service.pb.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql::local_service {

namespace {

#define EXPECT_OK_GRPC(expression) EXPECT_TRUE(expression.ok())

class GoogleSqlLocalServiceGrpcImplTest : public testing::Test {
 protected:
  void SetUp() override {
    grpc::ServerBuilder builder;
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override { server_->Shutdown(); }

  GoogleSqlLocalServiceGrpcImpl service_;
  std::unique_ptr<grpc::Server> server_;
};

TEST_F(GoogleSqlLocalServiceGrpcImplTest, EvaluateStreamPrepared) {
  grpc::ChannelArguments channel_args;
  std::unique_ptr<GoogleSqlLocalService::Stub> stub(
      GoogleSqlLocalService::NewStub(
          server_->InProcessChannel(channel_args)));

  grpc::ClientContext context;
  std::unique_ptr<
      grpc::ClientReaderWriter<EvaluateRequestBatch, EvaluateResponseBatch>>
      stream = stub->EvaluateStream(&context);

  EvaluateRequestBatch batch_request;
  batch_request.add_request()->set_sql("0");
  batch_request.add_request()->set_sql("1");
  batch_request.add_request()->set_sql("2");
  ASSERT_TRUE(stream->Write(batch_request));

  EvaluateResponseBatch batch_response;
  ASSERT_TRUE(stream->Read(&batch_response));
  ASSERT_EQ(batch_response.response_size(), 3);
  ASSERT_EQ(batch_response.response(0).prepared().output_type().type_kind(),
            TYPE_INT64);
  EXPECT_EQ(batch_response.response(0).value().int64_value(), 0);
  ASSERT_EQ(batch_response.response(1).prepared().output_type().type_kind(),
            TYPE_INT64);
  EXPECT_EQ(batch_response.response(1).value().int64_value(), 1);
  ASSERT_EQ(batch_response.response(2).prepared().output_type().type_kind(),
            TYPE_INT64);
  EXPECT_EQ(batch_response.response(2).value().int64_value(), 2);

  EXPECT_TRUE(stream->WritesDone());
  EXPECT_FALSE(stream->Read(&batch_response));
  EXPECT_OK_GRPC(stream->Finish());
}

TEST_F(GoogleSqlLocalServiceGrpcImplTest, EvaluateStreamPreparedBigResponse) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH);
  std::unique_ptr<GoogleSqlLocalService::Stub> stub(
      GoogleSqlLocalService::NewStub(
          server_->InProcessChannel(channel_args)));

  grpc::ClientContext stream_context;
  std::unique_ptr<
      grpc::ClientReaderWriter<EvaluateRequestBatch, EvaluateResponseBatch>>
      stream = stub->EvaluateStream(&stream_context);

  EvaluateRequestBatch batch_request;
  const int request_count = 8;
  for (int i = 0; i < request_count; i++) {
    batch_request.add_request()->set_sql("REPEAT('a', 1024*1024)");
  }
  EXPECT_TRUE(stream->Write(batch_request));

  for (int i = 0; i < request_count;) {
    EvaluateResponseBatch batch_response;
    if (!stream->Read(&batch_response)) {
      EXPECT_EQ(i, request_count);
      break;
    }
    i += batch_response.response_size();
    EXPECT_LE(i, request_count);

    // If this fails, request_count needs to be increased.
    ASSERT_LT(batch_response.response_size(), request_count);
  }

  EXPECT_TRUE(stream->WritesDone());
  EXPECT_OK_GRPC(stream->Finish());
}

}  // namespace

}  // namespace googlesql::local_service
