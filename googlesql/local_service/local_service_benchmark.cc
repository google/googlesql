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

#include <cstdint>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/local_service/local_service.h"
#include "googlesql/local_service/local_service.pb.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/status/status.h"
#include "googlesql/base/status.h"

namespace googlesql {

using ::absl::base_internal::NumCPUs;

namespace local_service {

static void BM_EvaluatePrepared(::benchmark::State& state) {
  static GoogleSqlLocalServiceImpl* service = new GoogleSqlLocalServiceImpl();
  // use static initialization to share the prepared expression across threads
  static int64_t prepared_expression_id = []() -> int64_t {
    EvaluateRequest evaluate_request;
    evaluate_request.set_sql("1");

    EvaluateResponse evaluate_response;
    GOOGLESQL_CHECK_OK(service->Evaluate(evaluate_request, &evaluate_response));
    return evaluate_response.prepared().prepared_expression_id();
  }();

  EvaluateRequest evaluate_request;
  evaluate_request.set_prepared_expression_id(prepared_expression_id);

  EvaluateResponse evaluate_response;
  for (auto s : state) {
    GOOGLESQL_ASSERT_OK(service->Evaluate(evaluate_request, &evaluate_response));
  }
}
BENCHMARK(BM_EvaluatePrepared)->ThreadRange(1, NumCPUs());

}  // namespace local_service
}  // namespace googlesql
