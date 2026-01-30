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

#ifndef GOOGLESQL_PUBLIC_MODULE_DETAILS_H_
#define GOOGLESQL_PUBLIC_MODULE_DETAILS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/resolution_scope.h"
#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// ModuleDetails contains information about a GoogleSQL module. It is meant to
// be passed to objects that are owned by a module.
class ModuleDetails {
 public:
  static ModuleDetails CreateEmpty();

  // Creates a ModuleDetails given the list of <resolved_options>. For all
  // module options, their names must be distinct (case insensitively), and
  // their value must be a constant expression that can be evaluated as a
  // googlesql::Value, which can be either:
  //   * A ResolvedLiteral
  //   * A constant expression of ResolvedLiterals
  // In both cases, the option value is accessible as a googlesql::Value.
  // The constant expression will be evaluated by `constant_evaluator`.
  // If `constant_evaluator` is nullptr, only ResolvedLiterals will be
  // allowed.
  static absl::StatusOr<ModuleDetails> Create(
      absl::string_view module_fullname,
      absl::Span<const std::unique_ptr<const ResolvedOption>> resolved_options,
      ConstantEvaluator* constant_evaluator = nullptr,
      ModuleOptions module_options = ModuleOptions(),
      absl::Span<const std::string> module_name_from_import = {});

  absl::string_view module_fullname() const { return module_fullname_; }

  const std::optional<PerModuleOptions>& udf_server_options() const {
    return udf_server_options_;
  }

  ResolutionScope default_resolution_scope() const {
    return default_resolution_scope_;
  }

  absl::Span<const std::string> module_name_from_import() const {
    return module_name_from_import_;
  }

 private:
  ModuleDetails(absl::string_view module_fullname,
                std::optional<PerModuleOptions> udf_server_options,
                ResolutionScope default_resolution_scope,
                absl::Span<const std::string> module_name_from_import = {})
      : module_fullname_(module_fullname),
        udf_server_options_(std::move(udf_server_options)),
        default_resolution_scope_(default_resolution_scope),
        module_name_from_import_(module_name_from_import.begin(),
                                 module_name_from_import.end()) {}

  static absl::Status CreateModuleOptionError(absl::string_view option_name,
                                              absl::string_view expected_type,
                                              absl::string_view actual_type);

  // The full name of the module.
  std::string module_fullname_;

  // The UDF server options. This field is non-empty if this module is UDF
  // server module, where the option stub_module_type has value of
  // "udf_server_catalog".
  std::optional<PerModuleOptions> udf_server_options_;

  // The default resolution scope for objects in this module which support
  // changing the resolution scope.
  ResolutionScope default_resolution_scope_;

  // The module name path of corresponding the IMPORT MODULE statement. Empty
  // by default.
  std::vector<std::string> module_name_from_import_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MODULE_DETAILS_H_
