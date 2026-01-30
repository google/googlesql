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

#ifndef GOOGLESQL_PUBLIC_ANON_FUNCTION_H_
#define GOOGLESQL_PUBLIC_ANON_FUNCTION_H_

#include <string>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class AnonFunction : public Function {
 public:
  // Construct a function for use in SELECT WITH ANONYMIZATION style queries,
  // for more details see the spec (broken link)
  //
  // partial_aggregate_name is the name of the function to be used for the
  // partial per-user aggregation, for example 'SUM' for 'ANON_SUM()', or
  // '$count_star' for 'ANON_COUNT(*)'. partial_aggregate_name is not qualified,
  // and must exist in the top level namespace. If partial_aggregate_name is not
  // valid, an internal error is returned by RewriteForAnonymization.
  //
  // These functions always have the mode set to AGGREGATE, and must accept
  // three arguments with the second two being the CLAMPED BETWEEN clause. The
  // appropriate error message callbacks for rendering the function call with
  // CLAMPED BETWEEN syntax are automatically added.
  AnonFunction(absl::string_view name, absl::string_view group,
               const std::vector<FunctionSignature>& function_signatures,
               const FunctionOptions& function_options,
               const std::string& partial_aggregate_name);

  const std::string& GetPartialAggregateName() const;

 private:
  const std::string partial_aggregate_name_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANON_FUNCTION_H_
