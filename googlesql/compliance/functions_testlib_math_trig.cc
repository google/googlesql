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

#include <math.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/statusor.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsTrigonometric() {
  double epsilon = std::numeric_limits<double>::epsilon();
  return {
      {"cos", {NullDouble()}, NullDouble()},
      {"cos", {0.0}, 1.0},
      {"cos", {M_PI}, -1.0},
      {"cos", {-M_PI}, -1.0},
      {"cos", {M_PI_2}, 0.0, kApproximate},
      {"cos", {-M_PI_2}, 0.0, kApproximate},
      // cos(pi / 2 + x) is asymptotically close to -x near x=0.
      // Due to proximity to zero, the ULP error is significant.
      {"cos", {M_PI_2 - 1.0e-10}, 1.0e-10, FloatMargin::UlpMargin(33)},
      {"cos", {-M_PI_2 + 1.0e-10}, 1.0e-10, FloatMargin::UlpMargin(33)},

      {"cos", {double_pos_inf}, double_nan},
      {"cos", {double_neg_inf}, double_nan},
      {"cos", {double_nan}, double_nan},

      {"acos", {NullDouble()}, NullDouble()},
      {"acos", {0.0}, M_PI_2},
      {"acos", {1.0}, 0.0},
      {"acos", {-1.0}, M_PI},
      // acos is only defined in [-1.0, 1.0]
      {"acos", {1.0 + epsilon}, NullDouble(), OUT_OF_RANGE},
      {"acos", {-1.0 - epsilon}, NullDouble(), OUT_OF_RANGE},
      {"acos", {1.0e-10}, M_PI_2 - 1.0e-10},

      {"acos", {double_pos_inf}, double_nan},
      {"acos", {double_neg_inf}, double_nan},
      {"acos", {double_nan}, double_nan},

      // cosh is defined as (exp(x)+exp(-x)) / 2
      {"cosh", {NullDouble()}, NullDouble()},
      {"cosh", {0.0}, 1.0},
      {"cosh", {1.0e-10}, 1.0},
      {"cosh", {1.0}, (M_E + 1 / M_E) / 2},
      {"cosh", {-1.0}, (M_E + 1 / M_E) / 2},
      {"cosh", {710.0}, 1.1169973830808557e+308, kApproximate},
      // Overflow.
      {"cosh", {711.0}, NullDouble(), OUT_OF_RANGE},

      {"cosh", {double_pos_inf}, double_pos_inf},
      {"cosh", {double_neg_inf}, double_pos_inf},
      {"cosh", {double_nan}, double_nan},

      // acosh(x) = ln(x + sqrt(x^2 - 1))
      {"acosh", {NullDouble()}, NullDouble()},
      {"acosh", {1.0}, 0.0},
      // acosh only defined for x >= 1
      {"acosh", {1 - epsilon}, NullDouble(), OUT_OF_RANGE},
      {"acosh", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"acosh", {(M_E + 1 / M_E) / 2}, 1.0},
      {"acosh", {1.1169973830808557e+308}, 710.0, kApproximate},

      {"acosh", {double_pos_inf}, double_pos_inf},
      {"acosh", {double_neg_inf}, double_nan},
      {"acosh", {double_nan}, double_nan},

      {"sin", {NullDouble()}, NullDouble()},
      {"sin", {0.0}, 0.0},
      {"sin", {M_PI}, 0.0, kApproximate},
      {"sin", {-M_PI}, 0.0, kApproximate},
      {"sin", {M_PI_2}, 1.0},
      {"sin", {-M_PI_2}, -1.0},
      // sin(x) is asymptotically close to x near x=0
      {"sin", {1.0e-10}, 1.0e-10, kApproximate},
      {"sin", {-1.0e-10}, -1.0e-10, kApproximate},

      {"sin", {double_pos_inf}, double_nan},
      {"sin", {double_neg_inf}, double_nan},
      {"sin", {double_nan}, double_nan},

      {"asin", {NullDouble()}, NullDouble()},
      {"asin", {0.0}, 0.0},
      {"asin", {1.0}, M_PI_2},
      {"asin", {-1.0}, -M_PI_2},
      // asin is only defined in [-1.0, 1.0]
      {"asin", {1.0 + epsilon}, NullDouble(), OUT_OF_RANGE},
      {"asin", {-1.0 - epsilon}, NullDouble(), OUT_OF_RANGE},
      // asin(x) is asymptotically close to x near x=0
      {"asin", {1.0e-10}, 1.0e-10, kApproximate},
      {"asin", {-1.0e-10}, -1.0e-10, kApproximate},

      {"asin", {double_pos_inf}, double_nan},
      {"asin", {double_neg_inf}, double_nan},
      {"asin", {double_nan}, double_nan},

      // sinh is defined as (exp(x)-exp(-x)) / 2
      {"sinh", {NullDouble()}, NullDouble()},
      {"sinh", {0.0}, 0.0},
      {"sinh", {1.0e-10}, 1.0e-10, kApproximate},
      {"sinh", {-1.0e-10}, -1.0e-10, kApproximate},
      {"sinh", {1.0}, (M_E - 1 / M_E) / 2},
      {"sinh", {-1.0}, (-M_E + 1 / M_E) / 2},
      {"sinh", {710.0}, 1.1169973830808557e+308, kApproximate},
      // Overflow.
      {"sinh", {711.0}, NullDouble(), OUT_OF_RANGE},
      {"sinh", {-710.0}, -1.1169973830808557e+308, kApproximate},
      // Overflow.
      {"sinh", {-711.0}, NullDouble(), OUT_OF_RANGE},

      {"sinh", {double_pos_inf}, double_pos_inf},
      {"sinh", {double_neg_inf}, double_neg_inf},
      {"sinh", {double_nan}, double_nan},

      // asinh(x) = ln(x + sqrt(x^2 + 1))
      {"asinh", {NullDouble()}, NullDouble()},
      {"asinh", {0.0}, 0.0},
      {"asinh", {1.0e-10}, 1.0e-10, kApproximate},
      {"asinh", {-1.0e-10}, -1.0e-10, kApproximate},
      {"asinh", {(M_E - 1 / M_E) / 2}, 1.0},
      {"asinh", {(-M_E + 1 / M_E) / 2}, -1.0},
      {"asinh", {doublemax}, 710.47586007394386, kApproximate},
      {"asinh", {doublemin}, -710.47586007394386, kApproximate},

      {"asinh", {double_pos_inf}, double_pos_inf},
      {"asinh", {double_neg_inf}, double_neg_inf},
      {"asinh", {double_nan}, double_nan},

      {"tan", {NullDouble()}, NullDouble()},
      {"tan", {0.0}, 0.0},
      {"tan", {M_PI}, 0.0, kApproximate},
      {"tan", {-M_PI}, 0.0, kApproximate},
      {"tan", {M_PI_4}, 1.0, kApproximate},
      {"tan", {-M_PI_4}, -1.0, kApproximate},
      {"tan", {M_PI_2 + M_PI_4}, -1.0, kApproximate},
      {"tan", {-M_PI_2 - M_PI_4}, 1.0, kApproximate},
      // tan(x) is asymptotically close to x near x=0
      {"tan", {1.0e-10}, 1.0e-10, kApproximate},
      {"tan", {-1.0e-10}, -1.0e-10, kApproximate},

      {"tan", {double_pos_inf}, double_nan},
      {"tan", {double_neg_inf}, double_nan},
      {"tan", {double_nan}, double_nan},

      {"atan", {NullDouble()}, NullDouble()},
      {"atan", {0.0}, 0.0},
      {"atan", {1.0}, M_PI_4},
      {"atan", {-1.0}, -M_PI_4},
      // atan(x) is asymptotically close to x near x=0
      {"atan", {1.0e-10}, 1.0e-10, kApproximate},
      {"atan", {-1.0e-10}, -1.0e-10, kApproximate},

      {"atan", {double_pos_inf}, M_PI_2},
      {"atan", {double_neg_inf}, -M_PI_2},
      {"atan", {double_nan}, double_nan},

      // tanh is defined as (exp(x)-exp(-x)) / (exp(x)+exp(-x))
      {"tanh", {NullDouble()}, NullDouble()},
      {"tanh", {0.0}, 0.0},
      {"tanh", {1.0}, (M_E - 1 / M_E) / (M_E + 1 / M_E)},
      {"tanh", {-1.0}, -(M_E - 1 / M_E) / (M_E + 1 / M_E)},
      // tanh(x) is asymptotically close to x near 0.
      {"tanh", {1.0e-10}, 1.0e-10, kApproximate},
      {"tanh", {-1.0e-10}, -1.0e-10, kApproximate},

      {"tanh", {double_pos_inf}, 1.0},
      {"tanh", {double_neg_inf}, -1.0},
      {"tanh", {double_nan}, double_nan},

      // atanh = 1/2 * ln((1+x)/(1-x))
      {"atanh", {NullDouble()}, NullDouble()},
      {"atanh", {0.0}, 0.0},
      {"atanh", {(M_E - 1 / M_E) / (M_E + 1 / M_E)}, 1.0, kApproximate},
      {"atanh", {-(M_E - 1 / M_E) / (M_E + 1 / M_E)}, -1.0, kApproximate},
      // atanh(x) is asymptotically close to x near 0.
      {"atanh", {1.0e-10}, 1.0e-10, kApproximate},
      {"atanh", {-1.0e-10}, -1.0e-10, kApproximate},
      // atanh is only defined in (-1.0, 1.0)
      {"atanh", {1.0}, NullDouble(), OUT_OF_RANGE},
      {"atanh", {-1.0}, NullDouble(), OUT_OF_RANGE},

      {"atanh", {double_pos_inf}, double_nan},
      {"atanh", {double_neg_inf}, double_nan},
      {"atanh", {double_nan}, double_nan},

      // atan2(y, x) = atan(y/x) with return value in range [-pi, pi]
      // Signs of x and y are used to determine the quadrant of the result.
      {"atan2", {NullDouble(), NullDouble()}, NullDouble()},
      {"atan2", {0.0, 0.0}, 0.0},
      {"atan2", {0.0, 1.0}, 0.0},
      {"atan2", {1.0, 1.0}, M_PI_4},
      {"atan2", {1.0, 0.0}, M_PI_2},
      {"atan2", {1.0, -1.0}, M_PI_2 + M_PI_4},
      {"atan2", {doubleminpositive, -1.0}, M_PI},
      {"atan2", {-1.0, 1.0}, -M_PI_4},
      {"atan2", {-1.0, 0.0}, -M_PI_2},
      {"atan2", {-1.0, -1.0}, -M_PI_2 - M_PI_4},
      {"atan2", {-doubleminpositive, -1.0}, -M_PI},

      {"atan2", {1.0, double_neg_inf}, M_PI},
      {"atan2", {1.0, double_pos_inf}, 0.0},
      {"atan2", {double_pos_inf, 1.0}, M_PI_2},
      {"atan2", {double_neg_inf, 1.0}, -M_PI_2},
      {"atan2", {double_pos_inf, double_pos_inf}, M_PI_4},
      {"atan2", {double_pos_inf, double_neg_inf}, M_PI_2 + M_PI_4},
      {"atan2", {double_neg_inf, double_pos_inf}, -M_PI_4},
      {"atan2", {double_neg_inf, double_neg_inf}, -M_PI_2 - M_PI_4},
      {"atan2", {double_nan, 0.0}, double_nan},
      {"atan2", {0.0, double_nan}, double_nan},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsInverseTrigonometric() {
  return {
      // CSC(x) = 1 / SIN(x)
      // Exceptional cases
      {"csc", {NullDouble()}, NullDouble()},
      {"csc", {double_pos_inf}, double_nan},
      {"csc", {double_neg_inf}, double_nan},
      {"csc", {double_nan}, double_nan},

      // Special values
      {"csc", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"csc", {M_PI_2}, 1.0},
      {"csc", {-M_PI_2}, -1.0},
      // sin(x) is asymptotically close to x near x=0
      // csc(x) is asymptotically close to 1/x near x=0
      {"csc", {1.0e-10}, 1.0e+10, kApproximate},
      {"csc", {-1.0e-10}, -1.0e+10, kApproximate},

      // SEC(x) = 1 / COS(x)
      // Exceptional cases
      {"sec", {NullDouble()}, NullDouble()},
      {"sec", {double_pos_inf}, double_nan},
      {"sec", {double_neg_inf}, double_nan},
      {"sec", {double_nan}, double_nan},

      // Special values
      {"sec", {0.0}, 1.0, kApproximate},
      {"sec", {M_PI}, -1.0, kApproximate},
      {"sec", {-M_PI}, -1.0, kApproximate},

      // cot(x) = 1 / tan(x)
      // Exceptional cases
      {"cot", {NullDouble()}, NullDouble()},
      {"cot", {double_pos_inf}, double_nan},
      {"cot", {double_neg_inf}, double_nan},
      {"cot", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"cot", {double_nan}, double_nan},

      // Special values
      {"cot", {M_PI_4}, 1.0, kApproximate},
      {"cot", {-M_PI_4}, -1.0, kApproximate},
      {"cot", {M_PI_2}, 0.0, kApproximate},
      {"cot", {-M_PI_2}, 0.0, kApproximate},
      {"cot", {M_PI_2 + M_PI_4}, -1.0, kApproximate},
      {"cot", {-M_PI_2 - M_PI_4}, 1.0, kApproximate},

      // sinh(x) = (exp(x)-exp(-x)) / 2
      // csch(x) = 1 / sinh(x)
      // Exceptional cases
      {"csch", {0.0}, NullDouble(), OUT_OF_RANGE},
      {"csch", {double_pos_inf}, 0.0},
      {"csch", {double_neg_inf}, 0.0},
      {"csch", {double_nan}, double_nan},
      {"csch", {NullDouble()}, NullDouble()},

      {"csch", {1.0e-10}, 1.0e10, kApproximate},
      {"csch", {-1.0e-10}, -1.0e10, kApproximate},

      {"csch", {1.0}, 2 / (M_E - 1 / M_E)},
      {"csch", {-1.0}, 2 / (-M_E + 1 / M_E)},

      {"csch", {710.0}, 0.0, kApproximate},
      {"csch", {711.0}, 0.0, kApproximate},
      {"csch", {-710.0}, 0.0, kApproximate},
      {"csch", {-711.0}, 0.0, kApproximate},

      // cosh(x) = (exp(x)+exp(-x)) / 2
      // sech(x) = 1 / cosh(x)
      // Exceptional cases
      {"sech", {NullDouble()}, NullDouble()},
      {"sech", {double_pos_inf}, 0.0},
      {"sech", {double_neg_inf}, 0.0},
      {"sech", {double_nan}, double_nan},

      {"sech", {0.0}, 1.0},
      {"sech", {1.0e-10}, 1.0},
      {"sech", {1.0}, 2 / (M_E + 1 / M_E)},
      {"sech", {-1.0}, 2 / (M_E + 1 / M_E)},
      {"sech", {710.0}, 0.0, kApproximate},
      {"sech", {711.0}, 0.0, kApproximate},

      // tanh is defined as (exp(x)-exp(-x)) / (exp(x)+exp(-x))
      // coth(x) = 1 / tanh(x)
      {"coth", {NullDouble()}, NullDouble()},
      {"coth", {double_pos_inf}, 1.0},
      {"coth", {double_neg_inf}, -1.0},
      {"coth", {double_nan}, double_nan},
      {"coth", {0.0}, NullDouble(), OUT_OF_RANGE},

      {"coth", {1.0}, (M_E + 1 / M_E) / (M_E - 1 / M_E), kApproximate},
      {"coth", {-1.0}, -(M_E + 1 / M_E) / (M_E - 1 / M_E), kApproximate},

      // tanh(x) is asymptotically close to x near 0.
      // so coth(x) is asymptotically close to 1/x near 0.
      {"coth", {1.0e-10}, 1.0e10, kApproximate},
      {"coth", {-1.0e-10}, -1.0e10, kApproximate},
  };
}

}  // namespace googlesql
