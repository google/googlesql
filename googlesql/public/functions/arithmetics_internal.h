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

// Saturated arithmetic operations.
// A Saturated<T> behaves like a T with clipping at the range boundaries.
//
// REQUIRES:
//   T() exists and is valid (does not have to be 0)
//   T has a copy constructor and an assignment operator
//   T provides these binary operators:
//     + - / * %
//     == != < > <= >=
//   std::numeric_limits<T>::min() exists
//   std::numeric_limits<T>::max() exists
//   0 is a valid value of T
//   T has no value t such that -1 < t < 0
//   T has no value t such that 0 < t < 1
//     -1 and 1 themselves may or may not be valid values of T
//     the point is: there is no t such that -1 < t < 0 or 0 < t < 1
//
// Motivation:
// The requirements about "no value t such that -1 < t < 0"
// and "no value t such that 0 < t < 1" enable multiplication and
// division to have simple range-checking code.  For example,
// consider the expression t1 / t2.  If t2 is greater than 0
// then this expression cannot overflow or underflow
// because of the requirement that 0 < t2 < 1 is not true.
//
// C++ integral types satisfy all these requirements.
// User-defined types may satisfy these requirements too.
//
// PROVIDES:
//   Saturated<T> has a Min() and a Max().
//   Results less than Min() clip to Min() and DidUnderflow() becomes true.
//   Results greater than Max() clip to Max() and DidOverflow() becomes true.
//   A result with an indeterminate value (e.g., 0/0) produces a result of T()
//   and DidBecomeIndeterminate() becomes true.
//
// Saturated<T>::Min() has the value std::numeric_limits<T>::min().
// Saturated<T>::Max() has the value std::numeric_limits<T>::max().

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_INTERNAL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_INTERNAL_H_

#include <assert.h>
#include <stdlib.h>

#include <algorithm>
#include <limits>

#include "googlesql/base/logging.h"

namespace googlesql {
namespace functions {

// This is not part of the public api (this is enforced by build visibility).
namespace arithmetics_internal {

template <class T>
class Saturated {
 public:
  typedef T value_type;
  Saturated();  // Initializes value to T()
  explicit Saturated(T t);

  // Arithmetic operations.
  //
  // We check for overflow, underflow and indeterminate status.  If none of
  // those happen then we pass the operation down to the underlying type.
  // For normal results (no status bits set), t1.OP(v2) has the value as
  // t1.Value() OP v2.  For example, Saturated<int>(5).Div(-3) will have the
  // same value as 5 / -3.  In C++ 2003, this value is implementation-defined.
  // In C++ 2011, this value is -1.
  //
  // In the paragraph below, OP(a, b) is short for:
  //   Saturated<T> t1(a);
  //   t1.OP(b);
  //
  // These operations may set status bits as follows.
  // ADD may overflow or underflow.
  // SUB may overflow or underflow.
  // MUL may overflow or underflow.
  //   MUL(MAX, -1) may underflow
  //   MUL(MIN, -1) may overflow
  // DIV may overflow, underflow, or become indeterminate.
  //   DIV(0, 0) has value T() and becomes indeterminate.
  //   DIV(+N, 0) has value MAX and sets overflow.
  //   DIV(-N, 0) has value MIN and sets underflow.
  //   DIV(MAX, -1) may underflow
  //   DIV(MIN, -1) may overflow
  // REM may become indeterminate
  //   If DIV(X, Y) becomes indeterminate, underflows, or overflows,
  //     then REM(X, Y) has value T() and becomes indeterminate.
  //
  // One more case:
  //   if MAX + MIN is not -1, 0, or 1
  //     then DIV(A, B) or REM(A, B) may abort the program
  //   this is an implementation limit in my overflow checker
  //
  // When an operation overflows, underflows, or becomes indeterminate, and
  // the Saturated<T> assumes its clipped or default value, all subsequent
  // operations will be disregarded and the value will remain unchanged until
  // the status is reset.
  //
  // All operations return *this to allow chaining.
  Saturated<T>& Add(T t2);
  Saturated<T>& Sub(T t2);
  Saturated<T>& Mul(T t2);
  Saturated<T>& Div(T t2);
  Saturated<T>& Rem(T t2);

  // Convenient max/min functions.
  static T Max() { return std::numeric_limits<T>::max(); }
  static T Min() { return std::numeric_limits<T>::min(); }

  // Value.
  T Value() const { return t_; }

  // Status bits.
  // These bits are sticky.
  // When one is set, it stays set until ResetStatus.
  bool DidOverflow() const { return did_overflow_; }
  bool DidUnderflow() const { return did_underflow_; }
  bool DidBecomeIndeterminate() const { return did_become_indeterminate_; }
  bool IsValid() const {
    return !did_overflow_ && !did_underflow_ && !did_become_indeterminate_;
  }
  void ResetStatus() {
    did_overflow_ = false;
    did_underflow_ = false;
    did_become_indeterminate_ = false;
  }

 private:
  T t_;
  bool did_overflow_ : 1;
  bool did_underflow_ : 1;
  bool did_become_indeterminate_ : 1;
};

// Implementation.
// This code conforms to both C++ standards: 1998+TC1+TC2+TC3 and 2011.
//
// When you think about the range of T (for underflow and underflow)
// it helps to consider the negative and positive cases separately.
// Min() and Max() are usually not symmetrical.

template <class T>
Saturated<T>::Saturated()
    : t_(T()),
      did_overflow_(false),
      did_underflow_(false),
      did_become_indeterminate_(false) {
  ABSL_CHECK_LE(Min(), t_);
  ABSL_CHECK_LE(t_, Max());
}

template <class T>
Saturated<T>::Saturated(T t)
    : t_(t),
      did_overflow_(false),
      did_underflow_(false),
      did_become_indeterminate_(false) {
  ABSL_CHECK_LE(Min(), t_);
  ABSL_CHECK_LE(t_, Max());
}

template <class T>
inline Saturated<T>& Saturated<T>::Add(T t2) {
  if (!IsValid()) {
    return *this;
  }

  // Overflow-safe version of "Max() < t_ + t2"
  if (t2 > 0 && Max() - t2 < t_) {
    t_ = Max();
    did_overflow_ = true;
    // Underflow-safe version of "Min() > t_ + t2"
  } else if (t2 < 0 && Min() - t2 > t_) {
    t_ = Min();
    did_underflow_ = true;
  } else {
    t_ = t_ + t2;
  }
  return *this;
}

template <class T>
inline Saturated<T>& Saturated<T>::Sub(T t2) {
  if (!IsValid()) {
    return *this;
  }

  // Overflow-safe version of "Max() < t_ - t2"
  if (t2 < 0 && Max() + t2 < t_) {
    t_ = Max();
    did_overflow_ = true;
    // Underflow-safe version of "Min() > t_ - t2"
  } else if (t2 > 0 && Min() + t2 > t_) {
    t_ = Min();
    did_underflow_ = true;
  } else {
    t_ = t_ - t2;
  }
  return *this;
}

template <class T>
inline Saturated<T>& Saturated<T>::Mul(T t2) {
  if (!IsValid()) {
    return *this;
  }
  if (t_ == 0 || t2 == 0) {
    t_ = t_ * t2;
    return *this;
  }

  if (t_ < 0 && t2 < 0) {
    ABSL_CHECK_LE(t_, -1);
    ABSL_CHECK_LE(t2, -1);
    // Overflow-safe version of "Max() < 0 - t_"
    // Overflow-safe version of "Max() < 0 - t2"
    if (Max() + t_ < 0 || Max() + t2 < 0) {
      // One of the operands will not fit in a positive T.
      // Their product will not fit either.
      // This is why we require that T has no fractions.
      t_ = Max();
      did_overflow_ = true;
      return *this;
    } else {
      t_ = 0 - t_;
      t2 = 0 - t2;
      // fall through
    }
  }

  if (t_ > 0 && t2 > 0) {
    ABSL_CHECK_GE(t_, 1);
    ABSL_CHECK_GE(t2, 1);
    // Overflow-safe version of "Max() < t_ * t2"
    if (Max() / t2 < t_) {
      t_ = Max();
      did_overflow_ = true;
      return *this;
    } else {
      t_ = t_ * t2;
      return *this;
    }
  }

  if (t_ > 0 && t2 < 0) {
    ABSL_CHECK_GE(t_, 1);
    ABSL_CHECK_LE(t2, -1);
    using std::swap;
    swap(t_, t2);
    // fall through
  }

  if (t_ < 0 && t2 > 0) {
    ABSL_CHECK_LE(t_, -1);
    ABSL_CHECK_GE(t2, 1);
    // Similar to above, we want to compute Min() / t2.
    // Sadly, c++03 leaves division implementation-defined.
    T q = Min() / t2;
    T r = Min() % t2;
    if (r > 0) {
      ABSL_CHECK_LT(r, t2);  // or % is broken
      ABSL_CHECK_LT(q, 0);
      q = q + 1;  // q < 0, ergo q+1 is in range
    }
    if (q > t_) {
      t_ = Min();
      did_underflow_ = true;
      return *this;
    } else {
      t_ = t_ * t2;
      return *this;
    }
  }

  // programming error.
  abort();
}

template <class T>
inline Saturated<T>& Saturated<T>::Div(T t2) {
  // Negative divisors will be hard because of MIN/MAX asymmetry.
  // Abort here if your C++ implementation is too weird or your type T
  // is too asymmetric.  An unconditional abort is better than a
  // data-dependent abort because it is more likely to show up in testing.
  // This code accepts unsigned types and twos complement signed types.
  if (Min() < 0) {
    if (Max() + Min() == -1) {
      // okay
    } else {
      abort();
    }
  }

  if (!IsValid()) {
    return *this;
  }
  if (t2 == 0) {
    // zero divisor
    if (t_ < 0) {
      t_ = Min();
      did_underflow_ = true;
    } else if (t_ == 0) {
      t_ = T();
      did_become_indeterminate_ = true;
    } else if (t_ > 0) {
      t_ = Max();
      did_overflow_ = true;
    } else {
      // probably a bug in T
      abort();
    }
  } else if (t2 > 0) {
    // positive divisor
    // quotient sign is same as dividend sign
    // quotient magnitude is same or smaller as dividend magnitude
    ABSL_CHECK_GE(t2, 1);
    t_ = t_ / t2;
  } else if (t2 < 0) {
    // negative divisor
    // quotient sign is opposite to dividend sign
    // quotient magnitude is same or smaller as dividend magnitude
    //
    // http://en.wikipedia.org/wiki/Signed_number_representations
    // Some representations are asymmetric
    // We accommodate twos complement.
    // DIV(MIN, -1) or DIV(MAX, -1) are the worst cases
    ABSL_CHECK_LE(t2, -1);
    if (Max() + Min() == -1) {
      // DIV(MIN, -1) is only failure case
      // representations: twos complement
      if (t_ == Min() && t2 == -1) {
        t_ = Max();
        did_overflow_ = true;
      } else {
        t_ = t_ / t2;
      }
    } else {
      // programming error: should have aborted up front!
      abort();
    }
  } else {
    // probably a bug in T
    abort();
  }
  return *this;
}

template <class T>
inline Saturated<T>& Saturated<T>::Rem(T t2) {
  if (!IsValid()) {
    return *this;
  }
  Saturated<T> t3(t_);
  t3.Div(t2);
  if (!t3.IsValid()) {
    t_ = T();
    did_become_indeterminate_ = true;
  } else {
    T t_new = t_ % t2;
    ABSL_CHECK_EQ(t3.Value() * t2 + t_new, t_);
    t_ = t_new;
  }
  return *this;
}

}  // namespace arithmetics_internal
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_ARITHMETICS_INTERNAL_H_
