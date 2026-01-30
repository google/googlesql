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

#include "googlesql/parser/deidentify.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/unparser.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace {

// Returns the images of components in this literal concatenation, separated by
// a single space.
static std::string ComposeImageForLiteralConcatenation(
    const ASTStringLiteral* string_literal) {
  std::string composed_image;
  for (const auto& component : string_literal->components()) {
    absl::StrAppend(&composed_image, component->image(), " ");
  }
  composed_image.pop_back();  // remove trailing space
  return composed_image;
}

// Deidentify SQL but identifiers and literals.
//
// Unfortunately, we cannot replace all literals just with ? parameters as that
// does not reparse properly; for example, literals in hints cannot be
// parameterised.
//
// The compromise
// - numbers are replaced with 0
// - strings with <redacted>
// - JSON with '{"<redacted>": null}'
// - and names of standard builtin, well known functions etc are not replaced
class DeidentifyingUnparser : public Unparser {
 public:
  DeidentifyingUnparser(Catalog& catalog,
                        const LanguageOptions& language_options,
                        std::set<ASTNodeKind> deidentified_ast_node_kinds,
                        std::set<ASTNodeKind> remapped_ast_node_kinds)
      : googlesql::parser::Unparser(&unparsed_output_),
        catalog_(catalog),
        language_options_(language_options),
        deidentified_ast_node_kinds_(deidentified_ast_node_kinds),
        remapped_ast_node_kinds_(remapped_ast_node_kinds) {}

  absl::string_view GetUnparsedOutput() const { return unparsed_output_; }
  absl::flat_hash_map<std::string, std::string> GetRemappedIdentifiers() const {
    return remapped_identifiers_;
  }
  void ResetOutput() { unparsed_output_.clear(); }

 private:
  void visitASTIntLiteral(const ASTIntLiteral* node, void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("0");
      return;
    }
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(node->image());
      return;
    }
    absl::string_view literal = node->image();
    if (ShouldRemapLiteral(literal)) {
      formatter_.Format(RemapLiteral(TypeKind::TYPE_INT32, literal));
    } else {
      Unparser::visitASTIntLiteral(node, data);
    }
  }
  void visitASTNumericLiteral(const ASTNumericLiteral* node,
                              void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("NUMERIC \"0\"");
      return;
    }

    std::string composed_image =
        ComposeImageForLiteralConcatenation(node->string_literal());

    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(absl::StrCat("NUMERIC ", composed_image));
      return;
    }

    if (ShouldRemapLiteral(composed_image)) {
      formatter_.Format(RemapLiteral(TypeKind::TYPE_NUMERIC, composed_image));
    } else {
      Unparser::visitASTNumericLiteral(node, data);
    }
  }
  void visitASTBigNumericLiteral(const ASTBigNumericLiteral* node,
                                 void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("BIGNUMERIC \"0\"");
      return;
    }

    std::string composed_image =
        ComposeImageForLiteralConcatenation(node->string_literal());

    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(absl::StrCat("BIGNUMERIC ", composed_image));
      return;
    }
    if (ShouldRemapLiteral(composed_image)) {
      formatter_.Format(
          RemapLiteral(TypeKind::TYPE_BIGNUMERIC, composed_image));
    } else {
      Unparser::visitASTBigNumericLiteral(node, data);
    }
  }
  void visitASTJSONLiteral(const ASTJSONLiteral* node, void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("JSON '{\"<redacted>\": null}'");
      return;
    }

    std::string composed_image =
        ComposeImageForLiteralConcatenation(node->string_literal());

    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(absl::StrCat("JSON ", composed_image));
      return;
    }

    if (ShouldRemapLiteral(composed_image)) {
      formatter_.Format(RemapLiteral(TypeKind::TYPE_JSON, composed_image));
    } else {
      Unparser::visitASTJSONLiteral(node, data);
    }
  }
  void visitASTFloatLiteral(const ASTFloatLiteral* node, void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("0.0");
      return;
    }
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(node->image());
      return;
    }
    absl::string_view literal = node->image();
    if (ShouldRemapLiteral(literal)) {
      formatter_.Format(RemapLiteral(TypeKind::TYPE_FLOAT, literal));
    } else {
      Unparser::visitASTFloatLiteral(node, data);
    }
  }

  void visitASTStringLiteral(const ASTStringLiteral* node,
                             void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("\"<redacted>\"");
      return;
    }

    std::string composed_image = ComposeImageForLiteralConcatenation(node);
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(composed_image);
      return;
    }

    if (ShouldRemapLiteral(composed_image)) {
      formatter_.Format(RemapLiteral(TypeKind::TYPE_STRING, composed_image));
    } else {
      Unparser::visitASTStringLiteral(node, data);
    }
  }
  void visitASTBytesLiteral(const ASTBytesLiteral* node, void* data) override {
    formatter_.Format("?");
  }
  void visitASTDateOrTimeLiteral(const ASTDateOrTimeLiteral* node,
                                 void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      formatter_.Format("?");
      return;
    }
    std::string composed_image =
        ComposeImageForLiteralConcatenation(node->string_literal());
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(absl::StrCat(
          Type::TypeKindToString(node->type_kind(), PRODUCT_INTERNAL), " ",
          composed_image));
      return;
    }
    if (ShouldRemapLiteral(composed_image)) {
      formatter_.Format(RemapLiteral(node->type_kind(), composed_image));
    } else {
      Unparser::visitASTDateOrTimeLiteral(node, data);
    }
  }
  void visitASTRangeLiteral(const ASTRangeLiteral* node, void* data) override {
    formatter_.Format("?");
  }

  void visitASTIdentifier(const ASTIdentifier* node, void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      return;
    }
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(node->GetAsStringView());
      return;
    }
    if (ShouldRemapIdentifier(node->GetAsStringView())) {
      formatter_.Format(RemapIdentifier(node->GetAsStringView()));
    } else {
      Unparser::visitASTIdentifier(node, data);
    }
  }

  void visitASTAlias(const ASTAlias* node, void* data) override {
    if (deidentified_ast_node_kinds_.find(node->node_kind()) !=
        deidentified_ast_node_kinds_.end()) {
      return;
    }
    if (remapped_ast_node_kinds_.find(node->node_kind()) ==
        remapped_ast_node_kinds_.end()) {
      formatter_.Format(node->identifier()->GetAsStringView());
      return;
    }
    absl::string_view identifier = node->identifier()->GetAsStringView();
    if (ShouldRemapIdentifier(identifier)) {
      print(absl::StrCat("AS ", RemapIdentifier(identifier)));
    } else {
      Unparser::visitASTAlias(node, data);
    }
  }

  bool ShouldRemapIdentifier(absl::string_view identifier) {
    if (googlesql::IsKeyword(identifier)) {
      return false;
    }
    if (language_options_.GenericEntityTypeSupported(identifier)) {
      return false;
    }
    if (language_options_.GenericSubEntityTypeSupported(identifier)) {
      return false;
    }

    std::vector<std::string> path;
    path.push_back(std::string(identifier));

    if (const Constant* constant = nullptr;
        catalog_.FindConstant(path, &constant).ok()) {
      return false;
    }
    if (const Type* type = nullptr; catalog_.FindType(path, &type).ok()) {
      return false;
    }
    if (const Function* function = nullptr;
        catalog_.FindFunction(path, &function).ok()) {
      return false;
    }
    if (const TableValuedFunction* function = nullptr;
        catalog_.FindTableValuedFunction(path, &function).ok()) {
      return false;
    }
    if (const Table* table = nullptr; catalog_.FindTable(path, &table).ok()) {
      return false;
    }
    return true;
  }

  // TODO: This function should always return true, i.e., should be removed.
  bool ShouldRemapLiteral(absl::string_view literal_value) {
    if (googlesql::IsKeyword(literal_value)) {
      return false;
    }
    if (language_options_.GenericEntityTypeSupported(literal_value)) {
      return false;
    }
    if (language_options_.GenericSubEntityTypeSupported(literal_value)) {
      return false;
    }

    return true;
  }

  std::string RemapIdentifier(absl::string_view identifier) {
    if (auto i = remapped_identifiers_.find(identifier);
        i != remapped_identifiers_.end()) {
      return i->second;
    }

    std::string remapped_name = "";
    size_t index = remapped_identifiers_.size();

    while (index > 25) {
      remapped_name = 'A' + (index % 26);
      index /= 26;
      index -= 1;
    }
    remapped_name += 'A' + index;
    std::reverse(remapped_name.begin(), remapped_name.end());

    while (googlesql::IsKeyword(remapped_name)) {
      remapped_name += "_";
    }

    remapped_identifiers_[identifier] = remapped_name;
    return remapped_name;
  }

  std::string GetLiteralPrefix(TypeKind kind) {
    if (kind == TypeKind::TYPE_NUMERIC) {
      return "NUMERIC ";
    } else if (kind == TypeKind::TYPE_BIGNUMERIC) {
      return "BIGNUMERIC ";
    } else if (kind == TypeKind::TYPE_JSON) {
      return "JSON ";
    } else if (kind == TypeKind::TYPE_DATE) {
      return "DATE ";
    } else if (kind == TypeKind::TYPE_TIME) {
      return "TIME ";
    } else if (kind == TypeKind::TYPE_DATETIME) {
      return "DATETIME ";
    }
    return "";
  }

  std::string RemapLiteral(TypeKind kind, absl::string_view literal) {
    std::string literal_prefix = GetLiteralPrefix(kind);
    if (auto i = remapped_identifiers_.find(literal);
        i != remapped_identifiers_.end()) {
      return i->second;
    }

    std::string remapped_name = "";
    size_t index = remapped_identifiers_.size();

    while (index > 0) {
      char current_digit = 'A' + (index % 26);
      remapped_name += current_digit;
      index /= 26;
    }
    std::reverse(remapped_name.begin(), remapped_name.end());

    while (googlesql::IsKeyword(remapped_name)) {
      remapped_name += "_";
    }

    remapped_identifiers_[literal_prefix.append(std::string(literal))] =
        remapped_name;
    return remapped_name;
  }

  Catalog& catalog_;
  const googlesql::LanguageOptions& language_options_;
  std::string unparsed_output_;
  absl::flat_hash_map<std::string, std::string> remapped_identifiers_;
  std::set<ASTNodeKind> deidentified_ast_node_kinds_;
  std::set<ASTNodeKind> remapped_ast_node_kinds_;
};
}  // namespace

static const auto all_literals = new std::set<ASTNodeKind>{
    ASTNodeKind::AST_INT_LITERAL,         ASTNodeKind::AST_FLOAT_LITERAL,
    ASTNodeKind::AST_STRING_LITERAL,      ASTNodeKind::AST_NUMERIC_LITERAL,
    ASTNodeKind::AST_BIGNUMERIC_LITERAL,  ASTNodeKind::AST_JSON_LITERAL,
    ASTNodeKind::AST_DATE_OR_TIME_LITERAL};

absl::StatusOr<std::string> DeidentifySQLIdentifiersAndLiterals(
    absl::string_view input, const ParserOptions& parser_options) {
  std::unique_ptr<googlesql::ParserOutput> parser_output;
  googlesql::ParseResumeLocation parse_resume =
      googlesql::ParseResumeLocation::FromStringView(input);

  bool at_end_of_input = false;
  std::string all_deidentified;
  SimpleCatalog catalog("allowed identifiers for deidentification");
  GOOGLESQL_RETURN_IF_ERROR(catalog.AddBuiltinFunctionsAndTypes(
      googlesql::BuiltinFunctionOptions::AllReleasedFunctions()));
  DeidentifyingUnparser unparser(
      catalog, parser_options.language_options(), *all_literals,
      {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS});

  do {
    GOOGLESQL_RETURN_IF_ERROR(googlesql::ParseNextStatement(
        &parse_resume, parser_options, &parser_output, &at_end_of_input));
    parser_output->node()->Accept(&unparser, nullptr);
    unparser.FlushLine();

    all_deidentified += unparser.GetUnparsedOutput();
    unparser.ResetOutput();
  } while (!at_end_of_input);
  return all_deidentified;
}

absl::StatusOr<DeidentificationResult> DeidentifySQLWithMapping(
    absl::string_view input, std::set<ASTNodeKind> deidentified_kinds,
    std::set<ASTNodeKind> remapped_kinds, const ParserOptions& parser_options) {
  std::unique_ptr<googlesql::ParserOutput> parser_output;
  googlesql::ParseResumeLocation parse_resume =
      googlesql::ParseResumeLocation::FromStringView(input);
  DeidentificationResult remapped_identifiers = {};

  std::string all_deidentified;
  SimpleCatalog catalog("allowed identifiers for deidentification");
  GOOGLESQL_RETURN_IF_ERROR(catalog.AddBuiltinFunctionsAndTypes(
      googlesql::BuiltinFunctionOptions::AllReleasedFunctions()));
  DeidentifyingUnparser unparser(catalog, parser_options.language_options(),
                                 deidentified_kinds, remapped_kinds);

  for (bool at_end_of_input = false; !at_end_of_input;) {
    GOOGLESQL_RETURN_IF_ERROR(googlesql::ParseNextStatement(
        &parse_resume, parser_options, &parser_output, &at_end_of_input));
    parser_output->node()->Accept(&unparser, nullptr);
    unparser.FlushLine();

    absl::StrAppend(&all_deidentified, unparser.GetUnparsedOutput());
    remapped_identifiers.remappings.merge(unparser.GetRemappedIdentifiers());
    unparser.ResetOutput();
  }

  remapped_identifiers.deidentified_sql = all_deidentified;
  return remapped_identifiers;
}

}  // namespace parser
}  // namespace googlesql
