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

#include "googlesql/common/multiprecision_int.h"

#include <cstdint>
#include <type_traits>

namespace googlesql {
namespace {
template <uint64_t power>
uint64_t DividePower10(VarUintRef<64>& var_int, uint64_t& remainder) {
  remainder = var_int.DivMod(std::integral_constant<uint64_t, power>());
  return power;
}

template <>
uint64_t DividePower10<1>(VarUintRef<64>& var_int, uint64_t& remainder) {
  return 1;
}
}  // namespace

template <>
const VarUintRef<64>::DivModFn VarUintRef<64>::kVarUintDivModPow10[] = {
    &DividePower10<1>,
    &DividePower10<10>,
    &DividePower10<100>,
    &DividePower10<1000>,
    &DividePower10<10000>,
    &DividePower10<100000>,
    &DividePower10<1000000>,
    &DividePower10<10000000>,
    &DividePower10<100000000>,
    &DividePower10<1000000000>,
    &DividePower10<10000000000>,
    &DividePower10<100000000000>,
    &DividePower10<1000000000000>,
    &DividePower10<10000000000000>,
    &DividePower10<100000000000000>,
    &DividePower10<1000000000000000>,
    &DividePower10<10000000000000000>,
    &DividePower10<100000000000000000>,
    &DividePower10<1000000000000000000>,
    &DividePower10<10000000000000000000ULL>};
}  // namespace googlesql
