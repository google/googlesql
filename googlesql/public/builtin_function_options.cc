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

#include "googlesql/public/builtin_function_options.h"

#include "googlesql/proto/options.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/language_options.h"
#include "absl/container/flat_hash_set.h"

namespace googlesql {

size_t FunctionSignatureIdHasher::operator()(FunctionSignatureId id) const {
  return static_cast<size_t>(id);
}

// static
BuiltinFunctionOptions BuiltinFunctionOptions::AllReleasedFunctions() {
  LanguageOptions language_options;
  language_options.set_product_mode(PRODUCT_INTERNAL);
  language_options.EnableMaximumLanguageFeatures();
  return BuiltinFunctionOptions(language_options);
}

BuiltinFunctionOptions::BuiltinFunctionOptions() {
  language_options.EnableMaximumLanguageFeatures();
}

BuiltinFunctionOptions::BuiltinFunctionOptions(const LanguageOptions& options)
    : language_options(options) {}

BuiltinFunctionOptions::BuiltinFunctionOptions(
    const GoogleSQLBuiltinFunctionOptionsProto& proto)
    : language_options(proto.language_options()) {
  for (int i = 0; i < proto.include_function_ids_size(); ++i) {
    include_function_ids.insert(proto.include_function_ids(i));
  }
  for (int i = 0; i < proto.exclude_function_ids_size(); ++i) {
    exclude_function_ids.insert(proto.exclude_function_ids(i));
  }
  for (int i = 0; i < proto.enabled_rewrites_map_entry_size(); ++i) {
    rewrite_enabled.insert({proto.enabled_rewrites_map_entry(i).key(),
                            proto.enabled_rewrites_map_entry(i).value()});
  }
  // TODO: b/332322078 - Implement serialization/deserialization logic for
  // `proto_arg_map`.
}

}  // namespace googlesql
