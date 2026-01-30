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

#ifndef GOOGLESQL_PUBLIC_EVALUATOR_LITE_H_
#define GOOGLESQL_PUBLIC_EVALUATOR_LITE_H_

#include "googlesql/public/evaluator_base.h"

// GoogleSQL in-memory expression or query evaluation using the reference
// implementation. Please see evaluator_base.h for full documentation and usage
// examples.
//
// evaluator_base.h  <- abstract base class and documentation
// evaluator.h       <- entry point for the full evaluator
// evaluator_lite.h  <- entry point for the "lite" evaluator
//
// This "lite" version of the evaluator is the same as the "full"
// implementation, but excludes some builtin function implementations. It's
// intended for use in environments where executable size is a major concern.
// Unless you have a good reason not to, you should probably use the full
// evaluator.
//
// Refer to ../reference_impl/functions/register_all.h for the list of functions
// that are not included with this implementation. The other modules in
// ../reference_impl/functions/ can be enabled individually by calling their
// exposed Register*() functions once before constructing any
// PreparedExpressionLite/PreparedQueryLite instances.
//
// Note that the optional builtin functions are *not* excluded from analysis,
// only from the evaluator library. This means that expressions using the
// excluded functions will still analyze as being valid, but will fail during
// execution.

namespace googlesql {

// See evaluator_base.h for the full interface and usage instructions.
class PreparedExpressionLite : public PreparedExpressionBase {
 public:
  using PreparedExpressionBase::PreparedExpressionBase;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_EVALUATOR_LITE_H_
