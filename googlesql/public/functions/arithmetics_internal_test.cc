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


#include "googlesql/public/functions/arithmetics_internal.h"

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"

namespace {

using googlesql::functions::arithmetics_internal::Saturated;

template <typename T>
void TestMinMax() {
  EXPECT_EQ(std::numeric_limits<T>::min(), Saturated<T>::Min());
  EXPECT_EQ(std::numeric_limits<T>::max(), Saturated<T>::Max());
}

template <typename T>
void TestConstruction() {
  Saturated<T> t0;
  EXPECT_EQ(T(), t0.Value());
  EXPECT_FALSE(t0.DidOverflow());
  EXPECT_FALSE(t0.DidUnderflow());
  EXPECT_FALSE(t0.DidBecomeIndeterminate());

  T t_default = T();
  Saturated<T> t1(t_default);
  EXPECT_EQ(T(), t1.Value());
  EXPECT_FALSE(t1.DidOverflow());
  EXPECT_FALSE(t1.DidUnderflow());
  EXPECT_FALSE(t1.DidBecomeIndeterminate());

  Saturated<T> t2(0);
  EXPECT_EQ(0, t2.Value());
  EXPECT_FALSE(t2.DidOverflow());
  EXPECT_FALSE(t2.DidUnderflow());
  EXPECT_FALSE(t2.DidBecomeIndeterminate());

  Saturated<T> t3(std::numeric_limits<T>::min());
  EXPECT_EQ(t3.Min(), t3.Value());
  EXPECT_FALSE(t3.DidOverflow());
  EXPECT_FALSE(t3.DidUnderflow());
  EXPECT_FALSE(t3.DidBecomeIndeterminate());

  Saturated<T> t4(std::numeric_limits<T>::max());
  EXPECT_EQ(t4.Max(), t4.Value());
  EXPECT_FALSE(t4.DidOverflow());
  EXPECT_FALSE(t4.DidUnderflow());
  EXPECT_FALSE(t4.DidBecomeIndeterminate());
}

template <typename T>
void TestAddition() {
  Saturated<T> t(0);
  t.Add(t.Max() - 1);
  t.Add(1);
  EXPECT_EQ(t.Max(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Add(1);
  EXPECT_EQ(t.Max(), t.Value());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Add(t.Min());
  EXPECT_EQ(t.Min(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    t.Add(-1);
    EXPECT_EQ(t.Min(), t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

template <typename T>
void TestSubtraction() {
  Saturated<T> t(0);
  t.Sub(0);
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Add(t.Min());
  EXPECT_EQ(t.Min(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Sub(1);
  EXPECT_EQ(t.Min(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(t.Max());
  if (t.Min() < 0) {
    t.Sub(-1);
    EXPECT_EQ(t.Max(), t.Value());
    EXPECT_TRUE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

template <typename T>
void TestMultiplication() {
  Saturated<T> t;

  // Zero result.

  t = Saturated<T>(0);
  t.Mul(0);
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(1);
  t.Mul(0);
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Mul(1);
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  // Positive and negative results.
  // Assumes range of T is at least [0, 6].

  t = Saturated<T>(2);
  t.Mul(3);
  EXPECT_EQ(6, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    t = Saturated<T>(2);
    t.Mul(-3);
    EXPECT_EQ(-6, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-2);
    t.Mul(3);
    EXPECT_EQ(-6, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-2);
    t.Mul(-3);
    EXPECT_EQ(6, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  // Overflow.
  t = Saturated<T>(t.Max());
  t.Mul(2);
  EXPECT_EQ(t.Max(), t.Value());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    // Underflow
    t = Saturated<T>(t.Min());
    t.Mul(2);
    EXPECT_EQ(t.Min(), t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  if (t.Min() < 0) {
    // Asymmetric multiplication by -1.
    // Twos complement integers will execute the first test and not the second.
    if (t.Min() + t.Max() < 0) {
      t = Saturated<T>(t.Min());
      t.Mul(-1);
      EXPECT_EQ(t.Max(), t.Value());
      EXPECT_TRUE(t.DidOverflow());
      EXPECT_FALSE(t.DidUnderflow());
      EXPECT_FALSE(t.DidBecomeIndeterminate());
    }

    if (t.Min() + t.Max() > 0) {
      t = Saturated<T>(t.Max());
      t.Mul(-1);
      EXPECT_EQ(t.Min(), t.Value());
      EXPECT_FALSE(t.DidOverflow());
      EXPECT_TRUE(t.DidUnderflow());
      EXPECT_FALSE(t.DidBecomeIndeterminate());
    }
  }
}

template <typename T>
void TestDivisionRemainder() {
  Saturated<T> t;

  // Division / remainder by zero.

  if (t.Min() < 0) {
    t = Saturated<T>(-1);
    t.Div(0);
    EXPECT_EQ(t.Min(), t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(0);
    EXPECT_EQ(T(), t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_TRUE(t.DidBecomeIndeterminate());
  }

  t = Saturated<T>(0);
  t.Div(0);
  EXPECT_EQ(T(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Rem(0);
  EXPECT_EQ(T(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());

  if (t.Max() > 0) {
    t = Saturated<T>(1);
    t.Div(0);
    EXPECT_EQ(t.Max(), t.Value());
    EXPECT_TRUE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(0);
    EXPECT_EQ(T(), t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_TRUE(t.DidBecomeIndeterminate());
  }

  // Simple non-zero division.
  if (t.Min() < 0 && t.Max() > 0) {
    t = Saturated<T>(-1);
    t.Div(-1);
    EXPECT_EQ(1, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(-1);
    EXPECT_EQ(0, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    //

    t = Saturated<T>(-1);
    t.Div(1);
    EXPECT_EQ(-1, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(1);
    EXPECT_EQ(0, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    //

    t = Saturated<T>(1);
    t.Div(-1);
    EXPECT_EQ(-1, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(-1);
    EXPECT_EQ(0, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  if (t.Max() > 0) {
    t = Saturated<T>(1);
    t.Div(1);
    EXPECT_EQ(1, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(1);
    EXPECT_EQ(0, t.Value());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

template <typename T>
void TestStatus() {
  // These tests are chained to test status stickiness.
  Saturated<T> t(0);
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Add(t.Max());
  t.Add(1);
  EXPECT_EQ(t.Max(), t.Value());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.Sub(t.Max());
  EXPECT_EQ(t.Max(), t.Value());  // no change
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Max());
  EXPECT_EQ(0, t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Add(t.Min());
  EXPECT_EQ(t.Min(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Sub(1);
  EXPECT_EQ(t.Min(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.Sub(t.Min());
  EXPECT_EQ(t.Min(), t.Value());  // no change
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Min());
  t.Div(0);
  EXPECT_EQ(T(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  EXPECT_EQ(T(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());
}

template <typename T>
void TestChaining() {
  EXPECT_EQ(10, Saturated<T>(1).Add(9).Value());
  EXPECT_EQ(70, Saturated<T>(1).Add(9).Mul(7).Value());
  EXPECT_EQ(35, Saturated<T>(1).Add(9).Mul(7).Div(2).Value());
  EXPECT_EQ(34, Saturated<T>(1).Add(9).Mul(7).Div(2).Sub(1).Value());
  EXPECT_EQ(4, Saturated<T>(1).Add(9).Mul(7).Div(2).Sub(1).Rem(10).Value());

  Saturated<T> t(1);
  // Overflow sticks after Add(t.Max()).
  t.Add(t.Max()).Sub(1);
  EXPECT_EQ(t.Max(), t.Value());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Value());
  // Indeterminate sticks after Div(0).
  t.Add(3).Mul(10).Rem(6).Div(0).Add(3);
  EXPECT_EQ(T(), t.Value());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());
}

template <typename T>
void TestBasicOperations() {
  TestMinMax<T>();
  TestConstruction<T>();
  TestAddition<T>();
  TestSubtraction<T>();
  TestMultiplication<T>();
  TestDivisionRemainder<T>();
  TestStatus<T>();
  TestChaining<T>();
}

TEST(Saturated, BasicOperations) {
  TestBasicOperations<uint8_t>();
  TestBasicOperations<uint16_t>();
  TestBasicOperations<uint32_t>();
  TestBasicOperations<uint64_t>();
  TestBasicOperations<int8_t>();
  TestBasicOperations<int16_t>();
  TestBasicOperations<int32_t>();
  TestBasicOperations<int64_t>();
}

// Test ALL the values!
// OP(T,T) must fit in a TBIG.

template <typename T, typename TBIG>
void MyValidate(Saturated<T> v1, TBIG tb_expect, TBIG tb_min, TBIG tb_max) {
  if (tb_expect < tb_min) {
    EXPECT_EQ(v1.Min(), v1.Value());
    EXPECT_TRUE(v1.DidUnderflow());
    EXPECT_FALSE(v1.DidOverflow());
    EXPECT_FALSE(v1.DidBecomeIndeterminate());
  } else if (tb_expect > tb_max) {
    EXPECT_EQ(v1.Max(), v1.Value());
    EXPECT_FALSE(v1.DidUnderflow());
    EXPECT_TRUE(v1.DidOverflow());
    EXPECT_FALSE(v1.DidBecomeIndeterminate());
  } else {
    EXPECT_EQ(tb_expect, v1.Value());
    EXPECT_FALSE(v1.DidUnderflow());
    EXPECT_FALSE(v1.DidOverflow());
    EXPECT_FALSE(v1.DidBecomeIndeterminate());
  }
}

template <typename T, typename TBIG>
void TestTotal() {
  const TBIG tb_min = std::numeric_limits<T>::min();
  const TBIG tb_max = std::numeric_limits<T>::max();
  for (TBIG tb1 = tb_min; tb1 <= tb_max; ++tb1) {
    for (TBIG tb2 = tb_min; tb2 <= tb_max; ++tb2) {
      T t1 = static_cast<T>(tb1);
      T t2 = static_cast<T>(tb2);
      Saturated<T> v1;

      // Addition.
      v1 = Saturated<T>(t1);
      v1.Add(t2);
      MyValidate(v1, tb1 + tb2, tb_min, tb_max);

      // Subtraction.
      v1 = Saturated<T>(t1);
      v1.Sub(t2);
      MyValidate(v1, tb1 - tb2, tb_min, tb_max);

      // Multiplication.
      v1 = Saturated<T>(t1);
      v1.Mul(t2);
      MyValidate(v1, tb1 * tb2, tb_min, tb_max);

      // Division.
      v1 = Saturated<T>(t1);
      v1.Div(t2);
      if (tb2 == 0) {
        EXPECT_EQ(v1.Value(), (tb1 < 0) ? tb_min : (tb1 > 0) ? tb_max : T());
        EXPECT_EQ(tb1 < 0, v1.DidUnderflow());
        EXPECT_EQ(tb1 > 0, v1.DidOverflow());
        EXPECT_EQ(tb1 == 0, v1.DidBecomeIndeterminate());
      } else {
        MyValidate(v1, tb1 / tb2, tb_min, tb_max);
      }
      // For Remainder.
      const bool division_failed =
          v1.DidUnderflow() || v1.DidOverflow() || v1.DidBecomeIndeterminate();

      // Remainder.
      v1 = Saturated<T>(t1);
      v1.Rem(t2);
      if (division_failed || tb2 == 0) {
        EXPECT_EQ(v1.Value(), T());
        EXPECT_FALSE(v1.DidUnderflow());
        EXPECT_FALSE(v1.DidOverflow());
        EXPECT_TRUE(v1.DidBecomeIndeterminate());
      } else {
        MyValidate(v1, tb1 % tb2, tb_min, tb_max);
      }
    }
  }
}

TEST(Saturated, Total) {
  TestTotal<uint8_t, int32_t>();
  TestTotal<int8_t, int32_t>();
}

}  // namespace
