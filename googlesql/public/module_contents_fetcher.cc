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

#include "googlesql/public/module_contents_fetcher.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_helpers.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "googlesql/base/status_builder.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

absl::Status ModuleContentsFetcher::FetchProtoFileDescriptor(
    const std::string& proto_file_name,
    const google::protobuf::FileDescriptor** proto_file_descriptor) {
  return googlesql_base::InvalidArgumentErrorBuilder()
         << "The IMPORT PROTO statement is not supported inside of modules";
}

// Scans <module_contents> specifically for IMPORT statements.
// Non-IMPORT statements are skipped so dependencies can be gathered before
// macro expansion occurs.
//
// When finding a valid IMPORT MODULE statement, the imported module name path
// is appended to <module_name_paths>. Adds an error status to <errors> if an
// IMPORT statement fails to parse, contains macro invocations, or if statement
// skipping fails (e.g. due to a lexical error).
//
// For example, when parsing a module and finding the statement
// 'IMPORT MODULE a.b.c AS d', the vector <a, b, c> is appended to
// <module_name_paths>.
static void AppendImportedModuleNamePaths(
    const std::string& filename, const std::string& module_contents,
    std::set<std::vector<std::string>>* module_name_paths,
    std::set<std::string>* proto_names, std::vector<absl::Status>* errors) {
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromString(filename, module_contents);
  // Note that we don't allow the ParserOptions to be passed in by the caller.
  // The ParserOptions only specify memory pools/arenas, and for this use
  // case, locally owned pools/arenas are fine so we keep the APIs simple.
  // TODO: Remove language_options when not required for parsing.
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();
  language_options.EnableLanguageFeature(FEATURE_PIPES);
  ParserOptions parser_options(language_options);
  bool is_end_of_input = false;
  while (!is_end_of_input) {
    std::unique_ptr<ParserOutput> parser_output;
    bool unused_next_statement_is_ctas = false;
    ASTNodeKind statement_kind =
        ParseNextStatementKind(parse_resume_location, language_options,
                               &unused_next_statement_is_ctas);
    // We only need to collect top-level IMPORT statements here, so skip other
    // statements. Note that for procedural blocks (such as CREATE PROCEDURE ...
    // BEGIN ... END;), SkipNextStatement stops at each semicolon inside the
    // BEGIN ... END; block rather than at the end of the entire procedure. This
    // is safe here because IMPORT statements are only supported at the module
    // top level (not inside procedure bodies), so each inner statement and the
    // closing END; will also have `statement_kind != AST_IMPORT_STATEMENT` and
    // be skipped in subsequent loop iterations.
    if (statement_kind != AST_IMPORT_STATEMENT) {
      const absl::Status skip_status =
          SkipNextStatement(&parse_resume_location, &is_end_of_input);
      if (!skip_status.ok()) {
        errors->push_back(ConvertInternalErrorLocationAndAdjustErrorString(
            parser_options.error_message_options(), module_contents,
            skip_status));
        break;
      }
      if (is_end_of_input) {
        break;
      }
      continue;
    }

    ParserRuntimeInfo runtime_info;
    const ParseResumeLocation this_parse_resume_location =
        parse_resume_location;
    const absl::Status parse_status =
        ParseNextStatement(&parse_resume_location, parser_options,
                           &parser_output, &is_end_of_input, &runtime_info);
    if (runtime_info.has_macro_invocation()) {
      errors->push_back(MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(
                            this_parse_resume_location.filename(),
                            this_parse_resume_location.byte_position()))
                        << "IMPORT statements cannot be produced via macro "
                           "expansion or contain macro invocations");
      break;
    }
    if (!parse_status.ok()) {
      errors->push_back(parse_status);
      break;
    }
    const ASTImportStatement* ast_import_statement =
        parser_output->statement()->GetAs<ASTImportStatement>();
    switch (ast_import_statement->import_kind()) {
      case (ASTImportStatement::MODULE): {
        const ASTPathExpression* module_name = ast_import_statement->name();
        if (module_name == nullptr) {
          // This happens if someone does 'IMPORT MODULE "foo"'.  For IMPORT
          // MODULE, quoting the name is not valid, but it parses so we need
          // to handle it here.  We ignore this IMPORT statement here since
          // all we're trying to do is gather up all the imported module
          // contents and protos.
          break;
        }
        const std::vector<std::string> identifier_vector =
            (module_name == nullptr ? std::vector<std::string>({"NULL"})
                                    : module_name->ToIdentifierVector());
        module_name_paths->insert(module_name->ToIdentifierVector());
        break;
      }
      case (ASTImportStatement::PROTO):
        if (proto_names == nullptr) {
          break;
        }
        const ASTStringLiteral* file_path =
            ast_import_statement->string_value();
        if (file_path == nullptr) {
          // This happens if someone does 'IMPORT PROTO foo.bar'. IMPORT PROTO
          // requires a quoted file path. However, the grammar does not so we
          // need to handle it here.
          absl::Status error =
              MakeSqlErrorAtPoint(ast_import_statement->location().start())
              << "Invalid IMPORT PROTO statement. IMPORT PROTO must be "
                 "followed by a file path (with quotes), e.g. IMPORT PROTO "
                 "'path/to/file.proto'";
          errors->push_back(error);
          break;
        }
        proto_names->insert(file_path->string_value());
        break;
    }
  }
}

static void FetchAndAppendTransitiveProtoDependencies(
    std::set<std::string> proto_names,
    ModuleContentsFetcher* module_contents_fetcher,
    ProtoContentsInfoMap* proto_contents_info_map,
    std::vector<absl::Status>* errors) {
  while (!proto_names.empty()) {
    const std::string& proto_name = *proto_names.begin();
    if (proto_contents_info_map->contains(proto_name)) {
      proto_names.erase(proto_name);
      continue;
    }

    const google::protobuf::FileDescriptor* file_descriptor = nullptr;
    const absl::Status fetch_status =
        module_contents_fetcher->FetchProtoFileDescriptor(proto_name,
                                                          &file_descriptor);
    if (!fetch_status.ok()) {
      errors->push_back(fetch_status);
      proto_names.erase(proto_name);
      continue;
    }
    ProtoContentsInfo proto_contents_info;
    proto_contents_info.filename = proto_name;
    file_descriptor->CopyTo(&proto_contents_info.file_descriptor_proto);
    // Add transitively imported proto names to the name set for further
    // analysis.
    for (int idx = 0;
         idx < proto_contents_info.file_descriptor_proto.dependency_size();
         ++idx) {
      const std::string& dependency_name =
          proto_contents_info.file_descriptor_proto.dependency(idx);
      googlesql_base::InsertIfNotPresent(&proto_names, dependency_name);
    }
    // Check validated.  If <proto_name> is already present in the map, then
    // we continued the loop above and do not get to here.
    ABSL_CHECK(googlesql_base::InsertIfNotPresent(proto_contents_info_map, proto_name,
                                  proto_contents_info));
    proto_names.erase(proto_name);
  }
}

static void FetchAndAppendAllModuleAndProtoContents(
    const std::vector<std::string>& module_name_path,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    ProtoContentsInfoMap* proto_contents_info_map,
    std::vector<absl::Status>* errors) {
  std::set<std::vector<std::string>> module_name_paths;
  std::set<std::string> proto_names;
  module_name_paths.insert(module_name_path);
  while (!module_name_paths.empty()) {
    const std::vector<std::string>& module_name_path =
        *module_name_paths.begin();
    if (googlesql_base::ContainsKey(*module_contents_info_map, module_name_path)) {
      module_name_paths.erase(module_name_path);
      continue;
    }
    ModuleContentsInfo module_contents_info;
    const absl::Status fetch_status =
        module_contents_fetcher->FetchModuleContents(module_name_path,
                                                     &module_contents_info);
    if (!fetch_status.ok()) {
      errors->push_back(fetch_status);
      module_name_paths.erase(module_name_path);
      continue;
    }

    // Check validated.  If <module_name_path> is already present, then we
    // continued the loop above and do not get to here.
    ABSL_CHECK(googlesql_base::InsertIfNotPresent(module_contents_info_map, module_name_path,
                                  module_contents_info));

    // Parse the <module_contents_info>, looking for IMPORT MODULE statements.
    // Add them to the <module_name_paths> set for further analysis.
    AppendImportedModuleNamePaths(module_contents_info.filename,
                                  module_contents_info.contents,
                                  &module_name_paths, &proto_names, errors);
    module_name_paths.erase(module_name_path);
  }
  if (proto_contents_info_map == nullptr) {
    // If we do not have a proto contents map to populate, then we do not
    // need to fetch or return any proto information so we are done.
    return;
  }
  // We may have some imported proto names, and now we need to populate the
  // ProtoContentsInfoMap with these protos and all transitively imported
  // protos.
  FetchAndAppendTransitiveProtoDependencies(std::move(proto_names),
                                            module_contents_fetcher,
                                            proto_contents_info_map, errors);
}

absl::Status FetchAllModuleContents(
    const std::vector<std::string>& module_name_path,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    std::vector<absl::Status>* errors) {
  errors->clear();
  module_contents_info_map->clear();
  FetchAndAppendAllModuleAndProtoContents(
      module_name_path, module_contents_fetcher, module_contents_info_map,
      /*proto_contents_info_map=*/nullptr, errors);
  if (!errors->empty()) {
    return (*errors)[0];
  }
  return absl::OkStatus();
}

absl::Status FetchAllModuleContents(
    absl::Span<const std::vector<std::string>> module_name_paths,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    std::vector<absl::Status>* errors) {
  errors->clear();
  module_contents_info_map->clear();
  for (const std::vector<std::string>& module_name_path : module_name_paths) {
    FetchAndAppendAllModuleAndProtoContents(
        module_name_path, module_contents_fetcher, module_contents_info_map,
        /*proto_contents_info_map=*/nullptr, errors);
  }
  if (!errors->empty()) {
    return (*errors)[0];
  }
  return absl::OkStatus();
}

absl::Status FetchAllModuleAndProtoContents(
    absl::Span<const std::vector<std::string>> module_name_paths,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    ProtoContentsInfoMap* proto_contents_info_map,
    std::vector<absl::Status>* errors) {
  errors->clear();
  module_contents_info_map->clear();
  proto_contents_info_map->clear();
  for (const std::vector<std::string>& module_name_path : module_name_paths) {
    FetchAndAppendAllModuleAndProtoContents(
        module_name_path, module_contents_fetcher, module_contents_info_map,
        proto_contents_info_map, errors);
  }
  if (!errors->empty()) {
    // If we found one or more errors then return the first one.
    return (*errors)[0];
  }
  return absl::OkStatus();
}

absl::Status FetchTransitiveProtoDependencies(
    absl::Span<const std::string> initial_proto_names,
    ModuleContentsFetcher* module_contents_fetcher,
    ProtoContentsInfoMap* proto_contents_info_map,
    std::vector<absl::Status>* errors) {
  errors->clear();
  proto_contents_info_map->clear();
  std::set<std::string> proto_names(initial_proto_names.begin(),
                                    initial_proto_names.end());
  FetchAndAppendTransitiveProtoDependencies(std::move(proto_names),
                                            module_contents_fetcher,
                                            proto_contents_info_map, errors);
  if (!errors->empty()) {
    return (*errors)[0];
  }
  return absl::OkStatus();
}

}  // namespace googlesql
