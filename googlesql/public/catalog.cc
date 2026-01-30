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

#include "googlesql/public/catalog.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog_helper.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/strings.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/io/tokenizer.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// TODO We may want to change the interfaces to just return a bool.
// The resolver never uses the error message that gets returned from here.
absl::Status Catalog::FindTable(const absl::Span<const std::string>& path,
                                const Table** table,
                                const FindOptions& options) {
  *table = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Table");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return TableNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindTable(path_suffix, table, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetTable(name, table, options));
    if (*table == nullptr) {
      return TableNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

namespace {

absl::Status TableNotFoundErrorWithPathPrefix(
    absl::string_view root_name, absl::Span<const std::string> table_path) {
  const std::string& name = table_path.back();
  const int length = static_cast<int>(table_path.size());
  if (length > 1) {
    return ::googlesql_base::NotFoundErrorBuilder()
           << "Table not found: " << ToIdentifierLiteral(name)
           << " not found in catalog " << ToIdentifierLiteral(root_name) << "."
           << IdentifierPathToString(table_path.first(length - 1));
  }
  return ::googlesql_base::NotFoundErrorBuilder()
         << "Table not found: " << ToIdentifierLiteral(name)
         << " not found in catalog " << root_name;
}

}  // namespace

absl::StatusOr<Column::ExpressionAttributes>
Column::ExpressionAttributes::Create(
    ExpressionKind expression_kind, const std::string& expression_string,
    const ResolvedExpr* resolved_expression,
    std::optional<std::vector<int>> row_identity_columns) {
  if (row_identity_columns.has_value() &&
      expression_kind != ExpressionKind::MEASURE_EXPRESSION) {
    return absl::InvalidArgumentError(
        "Row identity columns can only be provided for measure "
        "expressions");
  }
  return ExpressionAttributes(expression_kind, expression_string,
                              resolved_expression,
                              std::move(row_identity_columns));
}

absl::Status Catalog::FindTableWithPathPrefixImpl(
    const absl::Span<const std::string> path, const std::string& root_name,
    const FindOptions& options, int current_index, int* result_index,
    const Table** table) {
  if (current_index == path.size() - 1) {
    // We have already validated in FindTable that the last element in path is
    // not a table. So the base case here returns error directly.
    return TableNotFoundErrorWithPathPrefix(root_name, path);
  }

  // In each recursion layer, we first try to find sub-catalog using the current
  // identifier. If the sub-catalog exists, we traverse to the next layer.
  // Otherwise, we try to find table under current catalog using the current
  // identifier. If the table also doesn't exist, it's a NOT_FOUND error.
  Catalog* next_catalog = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetCatalog(path[current_index], &next_catalog, options));
  if (next_catalog != nullptr) {
    return next_catalog->FindTableWithPathPrefixImpl(
        path, root_name, options, current_index + 1, result_index, table);
  }

  // Note that, there is complexity behind FindTable that GetTable is not paired
  // with here, for example, potential multi-catalog lookups, different engines
  // might have different implementation for the look up process too.
  // We also need to diregard the original error messages and overwrite a new
  // one when table is not found.
  absl::Status status = FindTable({path[current_index]}, table, options);
  if (absl::IsNotFound(status)) {
    return TableNotFoundErrorWithPathPrefix(root_name,
                                            path.first(current_index + 1));
  }
  if (*table != nullptr) {
    GOOGLESQL_RET_CHECK(status.ok());
    *result_index = current_index;
  }
  return status;
}

absl::Status Catalog::FindTableWithPathPrefix(
    const absl::Span<const std::string> path, const FindOptions& options,
    int* num_names_consumed, const Table** table) {
  // First of all, try to find table with the full identifier path. In case
  // there are engine code that overrides FindTable with a different
  // implementation.
  *num_names_consumed = 0;
  *table = nullptr;
  absl::Status find_full_path_status = FindTable(path, table, options);
  if (!absl::IsNotFound(find_full_path_status)) {
    // Errors other than NotFound should be propagated without further ado.
    GOOGLESQL_RETURN_IF_ERROR(find_full_path_status);
    GOOGLESQL_RET_CHECK(*table != nullptr)
        << "FindTable with full identifier path " << absl::StrJoin(path, ".")
        << " returns an OK status but leaves `table` null.";
    *num_names_consumed = static_cast<int>(path.size());
    return find_full_path_status;
  }

  int result_index = -1;
  *num_names_consumed = 0;
  *table = nullptr;
  // Start recursion to find table from the first index of the path expression.
  // Any error encountered would mean a failed table name path prefix lookup.
  GOOGLESQL_RETURN_IF_ERROR(FindTableWithPathPrefixImpl(path, FullName(), options, 0,
                                              &result_index, table));
  GOOGLESQL_RET_CHECK(*table != nullptr);
  *num_names_consumed = result_index + 1;
  return absl::OkStatus();
}

absl::Status Catalog::FindModel(const absl::Span<const std::string>& path,
                                const Model** model,
                                const FindOptions& options) {
  *model = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Model");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return ModelNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindModel(path_suffix, model, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetModel(name, model, options));
    if (*model == nullptr) {
      return ModelNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindConnection(const absl::Span<const std::string>& path,
                                     const Connection** connection,
                                     const FindOptions& options) {
  *connection = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Connection");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return ConnectionNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindConnection(path_suffix, connection, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetConnection(name, connection, options));
    if (*connection == nullptr) {
      return ConnectionNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindSequence(const absl::Span<const std::string>& path,
                                   const Sequence** sequence,
                                   const FindOptions& options) {
  *sequence = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Sequence");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return SequenceNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindSequence(path_suffix, sequence, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetSequence(name, sequence, options));
    if (*sequence == nullptr) {
      return SequenceNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindFunction(const absl::Span<const std::string>& path,
                                   const Function** function,
                                   const FindOptions& options) {
  *function = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Function");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return FunctionNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindFunction(path_suffix, function, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetFunction(name, function, options));
    if (*function == nullptr) {
      return FunctionNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  *function = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("TableValuedFunction");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return TableValuedFunctionNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindTableValuedFunction(path_suffix, function, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetTableValuedFunction(name, function, options));
    if (*function == nullptr) {
      return TableValuedFunctionNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindProcedure(const absl::Span<const std::string>& path,
                                    const Procedure** procedure,
                                    const FindOptions& options) {
  *procedure = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Procedure");
  }
  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return ProcedureNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindProcedure(path_suffix, procedure, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetProcedure(name, procedure, options));
    if (*procedure == nullptr) {
      return ProcedureNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindType(const absl::Span<const std::string>& path,
                               const Type** type, const FindOptions& options) {
  *type = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("Type");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      // FindType-specific behavior: If we have a path, and it looks like it
      // could be a valid unquoted multi-part proto name, try looking up that
      // proto name as a type.
      const std::string proto_name = ConvertPathToProtoName(path);
      if (!proto_name.empty()) {
        GOOGLESQL_RETURN_IF_ERROR(GetType(proto_name, type, options));
        if (*type != nullptr) {
          return absl::OkStatus();
        } else {
          return ::googlesql_base::NotFoundErrorBuilder()
                 << "Type not found: " << ToIdentifierLiteral(proto_name)
                 << " is not a type and " << ToIdentifierLiteral(name)
                 << " is not a nested catalog in catalog " << FullName();
        }
      }
      return TypeNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix =
        path.subspan(1, path.size() - 1);
    return catalog->FindType(path_suffix, type, options);
  } else {
    GOOGLESQL_RETURN_IF_ERROR(GetType(name, type, options));
    if (*type == nullptr) {
      return TypeNotFoundError(path);
    }
    return absl::OkStatus();
  }
}

absl::Status Catalog::FindConstant(const absl::Span<const std::string> path,
                                   const Constant** constant,
                                   const FindOptions& options) {
  int num_names_consumed = 0;
  GOOGLESQL_RETURN_IF_ERROR(
      FindConstantWithPathPrefix(path, &num_names_consumed, constant, options));
  if (num_names_consumed < path.size()) {
    return ConstantNotFoundError(path);
  }
  return absl::OkStatus();
}

absl::Status Catalog::FindConversion(const Type* from_type, const Type* to_type,
                                     const FindConversionOptions& options,
                                     Conversion* conversion) {
  return ConversionNotFoundError(from_type, to_type, options);
}

absl::Status Catalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  // Return an error if <path> is empty.
  *constant = nullptr;
  *num_names_consumed = 0;
  if (path.empty()) {
    return EmptyNamePathInternalError("Constant");
  }

  // Find the longest prefix of <path> that can be resolved to either a constant
  // or a catalog.
  absl::Status find_constant_with_path_prefix_status =
      FindConstantWithPathPrefixImpl(path, num_names_consumed, constant,
                                     options);

  // If no prefix of <path> resolves to a constant or a catalog, return
  // NOT_FOUND for the whole <path> in this catalog. Otherwise return the
  // resolved constant or whatever other error occurred.
  return find_constant_with_path_prefix_status.code() ==
                 absl::StatusCode::kNotFound
             ? ConstantNotFoundError(path)
             : find_constant_with_path_prefix_status;
}

absl::Status Catalog::FindConstantWithPathPrefixImpl(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  // Get the first step in <path>.
  GOOGLESQL_RET_CHECK(!path.empty());
  const std::string& name = path.front();

  // If there are multiple steps in <path>, try to resolve the first step
  // against a sub-catalog. If that succeeds, move on to the next step.
  if (path.size() > 1) {
    Catalog* next_resolved_catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &next_resolved_catalog, options));
    if (next_resolved_catalog != nullptr) {
      // Continue resolving <path> assuming that <name> is a catalog reference.
      // If this fails, fall through to backtracking on <name> which could also
      // be a struct-typed constant (see below).
      const absl::Span<const std::string> path_suffix =
          path.subspan(1, path.size() - 1);
      const absl::Status find_constant_with_path_prefix_status =
          next_resolved_catalog->FindConstantWithPathPrefix(
              path_suffix, num_names_consumed, constant, options);
      if (find_constant_with_path_prefix_status.code() !=
          absl::StatusCode::kNotFound) {
        *num_names_consumed += 1;
        return find_constant_with_path_prefix_status;
      }
    }
  }

  // At any step in <path>, once <name> does not resolve to a catalog, it must
  // reference a constant.
  GOOGLESQL_RETURN_IF_ERROR(GetConstant(name, constant, options));
  if (*constant != nullptr) {
    *num_names_consumed += 1;
    return absl::OkStatus();
  }

  // If no constant is found, reset <num_names_consumed> and return an error.
  *num_names_consumed = 0;
  return ConstantNotFoundError(path);
}

absl::Status Catalog::FindPropertyGraph(
    const absl::Span<const std::string> path,
    const PropertyGraph*& property_graph, const FindOptions& options) {
  property_graph = nullptr;
  if (path.empty()) {
    return EmptyNamePathInternalError("PropertyGraph");
  }

  const std::string& name = path.front();
  if (path.size() > 1) {
    Catalog* catalog = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetCatalog(name, &catalog, options));
    if (catalog == nullptr) {
      return PropertyGraphNotFoundError(path);
    }
    const absl::Span<const std::string> path_suffix = path.subspan(1);
    return catalog->FindPropertyGraph(path_suffix, property_graph, options);
  }
  GOOGLESQL_RETURN_IF_ERROR(GetPropertyGraph(name, property_graph, options));
  if (property_graph == nullptr) {
    return PropertyGraphNotFoundError(path);
  }
  return absl::OkStatus();
}

absl::Status Catalog::FindObject(absl::Span<const std::string> path,
                                 const Table** object,
                                 const FindOptions& options) {
  return FindTable(path, object, options);
}
absl::Status Catalog::FindObject(absl::Span<const std::string> path,
                                 const Type** object,
                                 const FindOptions& options) {
  return FindType(path, object, options);
}

absl::Status Catalog::FindObject(absl::Span<const std::string> path,
                                 const PropertyGraph** object,
                                 const FindOptions& options) {
  return FindPropertyGraph(path, *object, options);
}

absl::StatusOr<TypeListView> Catalog::GetExtendedTypeSuperTypes(
    const Type* type) {
  GOOGLESQL_RET_CHECK_NE(type, nullptr);
  GOOGLESQL_RET_CHECK(type->IsExtendedType());

  // By default, Catalogs do not support extended types so we return NOT_FOUND
  // here.  Catalogs that support extended types must override this method in
  // order to find the <type> and return the relevant supertypes.
  return ::googlesql_base::NotFoundErrorBuilder()
         << "Type " << type->DebugString() << " not found in catalog "
         << FullName();
}

std::string Catalog::ConvertPathToProtoName(
    absl::Span<const std::string> path) {
  // Return "" if path contains an invalid proto identifier name.
  for (const std::string& identifier : path) {
    if (!google::protobuf::io::Tokenizer::IsIdentifier(identifier)) {
      return "";
    }
  }
  // This also returns "" for empty paths.
  return absl::StrJoin(path, ".");
}

std::string Catalog::SuggestTable(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

std::string Catalog::SuggestModel(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

std::string Catalog::SuggestFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

std::string Catalog::SuggestTableValuedFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

std::string Catalog::SuggestConstant(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

std::string Catalog::SuggestEnumValue(const EnumType* type,
                                      absl::string_view mistyped_value) {
  return ::googlesql::SuggestEnumValue(type, mistyped_value);
}

std::string Catalog::SuggestPropertyGraph(
    absl::Span<const std::string> mistyped_path) {
  return "";
}

std::string Catalog::SuggestSequence(
    const absl::Span<const std::string>& mistyped_path) {
  return "";
}

absl::Status Catalog::GetTable(const std::string& name, const Table** table,
                               const FindOptions& options) {
  *table = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetModel(const std::string& name, const Model** model,
                               const FindOptions& options) {
  *model = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetConnection(const std::string& name,
                                    const Connection** connection,
                                    const FindOptions& options) {
  *connection = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetSequence(const std::string& name,
                                  const Sequence** sequence,
                                  const FindOptions& options) {
  *sequence = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetFunction(const std::string& name,
                                  const Function** function,
                                  const FindOptions& options) {
  *function = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetTableValuedFunction(
    const std::string& name, const TableValuedFunction** function,
    const FindOptions& options) {
  *function = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetProcedure(const std::string& name,
                                   const Procedure** procedure,
                                   const FindOptions& options) {
  *procedure = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetType(const std::string& name, const Type** type,
                              const FindOptions& options) {
  *type = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetCatalog(const std::string& name, Catalog** catalog,
                                 const FindOptions& options) {
  *catalog = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetConstant(const std::string& name,
                                  const Constant** constant,
                                  const FindOptions& options) {
  *constant = nullptr;
  return absl::OkStatus();
}

absl::Status Catalog::GetPropertyGraph(absl::string_view name,
                                       const PropertyGraph*& property_graph,
                                       const FindOptions& options) {
  property_graph = nullptr;
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "GetPropertyGraph is not implemented yet.");
}

absl::Status Catalog::GenericNotFoundError(
    absl::string_view object_type, absl::Span<const std::string> path) const {
  const std::string& name = path.front();
  if (path.size() > 1) {
    return ::googlesql_base::NotFoundErrorBuilder()
           << object_type << " not found: catalog " << ToIdentifierLiteral(name)
           << " not found in catalog " << FullName();
  }
  return ::googlesql_base::NotFoundErrorBuilder()
         << object_type << " not found: " << ToIdentifierLiteral(name)
         << " not found in catalog " << FullName();
}

absl::Status Catalog::TableNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Table", path);
}

absl::Status Catalog::ModelNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Model", path);
}

absl::Status Catalog::ConnectionNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Connection", path);
}

absl::Status Catalog::SequenceNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Sequence", path);
}

absl::Status Catalog::FunctionNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Function", path);
}

absl::Status Catalog::TableValuedFunctionNotFoundError(
    const absl::Span<const std::string> path) const {
  return GenericNotFoundError("Table function", path);
}

absl::Status Catalog::ProcedureNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Procedure", path);
}

absl::Status Catalog::TypeNotFoundError(
    absl::Span<const std::string> path) const {
  return GenericNotFoundError("Type", path);
}

absl::Status Catalog::ConstantNotFoundError(
    const absl::Span<const std::string> path) const {
  return GenericNotFoundError("Constant", path);
}

absl::Status Catalog::ConversionNotFoundError(
    const Type* from, const Type* to,
    const FindConversionOptions& options) const {
  return googlesql_base::NotFoundErrorBuilder()
         << (options.is_explicit() ? "Cast" : "Coercion") << " from type "
         << from->TypeName(options.product_mode()) << " to type "
         << to->TypeName(options.product_mode()) << " not found in catalog "
         << FullName();
}

absl::Status Catalog::PropertyGraphNotFoundError(
    const absl::Span<const std::string> path) const {
  return GenericNotFoundError("PropertyGraph", path);
}

absl::Status Catalog::EmptyNamePathInternalError(
    absl::string_view object_type) const {
  return ::googlesql_base::InternalErrorBuilder()
         << "Invalid empty " << object_type << " name path";
}

// static
absl::StatusOr<AnonymizationUserIdInfo> AnonymizationUserIdInfo::Create(
    const Column* column) {
  GOOGLESQL_RET_CHECK_NE(column, nullptr);
  GOOGLESQL_RET_CHECK(!column->Name().empty());
  return AnonymizationUserIdInfo(column);
}

// static
absl::StatusOr<AnonymizationUserIdInfo> AnonymizationUserIdInfo::Create(
    absl::Span<const std::string> column_name_path) {
  GOOGLESQL_RET_CHECK(!column_name_path.empty());
  return AnonymizationUserIdInfo(column_name_path);
}

AnonymizationUserIdInfo::AnonymizationUserIdInfo(const Column* column)
    : column_(column), column_name_path_(1, column->Name()) {
}

AnonymizationUserIdInfo::AnonymizationUserIdInfo(
    absl::Span<const std::string> column_name_path)
    : column_name_path_(column_name_path.begin(), column_name_path.end()) {}

absl::Status ValidateDefaultThreshold(int default_threshold) {
  if (default_threshold < 0) {
    return absl::InvalidArgumentError(
        "Default threshold value cannot be negative.");
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<std::unique_ptr<AnonymizationInfo>> AnonymizationInfo::Create(
    const Table* table, absl::Span<const std::string> userid_column_name_path,
    int default_threshold) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateDefaultThreshold(default_threshold));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnonymizationInfo> anonymization_info,
                   AnonymizationInfo::Create(table, userid_column_name_path));
  anonymization_info->SetDefaultThreshold(default_threshold);
  return anonymization_info;
}

// static
absl::StatusOr<std::unique_ptr<AnonymizationInfo>> AnonymizationInfo::Create(
    const Table* table, absl::Span<const std::string> userid_column_name_path) {
  GOOGLESQL_RET_CHECK_NE(table, nullptr);
  absl::Status not_found_error =
      googlesql_base::InvalidArgumentErrorBuilder()
      << "The anonymization userid column name "
      << IdentifierPathToString(userid_column_name_path)
      << " was not found in table " << table->Name();
  if (userid_column_name_path.empty()) {
    return not_found_error;
  }

  if (table->IsValueTable()) {
    // Value tables can have pseudo columns that are not part of the Value,
    // and the userid column can be one of these pseudo columns.  We first
    // look for a non-pseudo column from the struct/proto.  As per contract,
    // value tables require that their first column (column 0) be the value
    // table value column.
    GOOGLESQL_RET_CHECK_GE(table->NumColumns(), 1);
    const Column* value_table_column = table->GetColumn(0);
    GOOGLESQL_RET_CHECK_NE(value_table_column, nullptr);
    GOOGLESQL_RET_CHECK(value_table_column->GetType()->IsStruct() ||
              value_table_column->GetType()->IsProto());
    if (value_table_column->GetType()->IsStruct()) {
      bool is_ambiguous = false;
      bool user_id_column_found = true;
      const StructType* struct_type = value_table_column->GetType()->AsStruct();
      const StructField* struct_field = nullptr;
      for (const std::string& userid_column_field : userid_column_name_path) {
        if (struct_type == nullptr) {
          user_id_column_found = false;
          break;
        }
        struct_field =
            struct_type->FindField(userid_column_field, &is_ambiguous);
        if (struct_field == nullptr) {
          user_id_column_found = false;
          break;
        }
        if (is_ambiguous) {
          return googlesql_base::InvalidArgumentErrorBuilder()
                 << "The anonymization userid column name "
                 << IdentifierPathToString(userid_column_name_path)
                 << " is ambiguous in table " << table->Name();
        }
        struct_type = struct_field->type->AsStruct();
      }

      if (user_id_column_found) {
        // We found the field from the struct, so return an AnonymizationInfo
        // with this name (if it is not ambiguous).
        return Create(userid_column_name_path);
      }
    } else {
      bool user_id_column_found = true;
      const google::protobuf::Descriptor* descriptor =
          value_table_column->GetType()->AsProto()->descriptor();
      const google::protobuf::FieldDescriptor* field_descriptor = nullptr;
      for (const std::string& userid_column_field : userid_column_name_path) {
        if (descriptor == nullptr) {
          user_id_column_found = false;
          break;
        }
        field_descriptor = ProtoType::FindFieldByNameIgnoreCase(
            descriptor, userid_column_field);
        if (field_descriptor == nullptr) {
          user_id_column_found = false;
          break;
        }
        descriptor = field_descriptor->message_type();
      }

      if (user_id_column_found) {
        // We found the field from the proto, so return an AnonymizationInfo
        // with this name.
        return Create(userid_column_name_path);
      }
    }
  }

  // TODO: support subfield user column id for non-value tables.
  if (userid_column_name_path.size() > 1) {
    return not_found_error;
  }

  // Either the table is not a value table, or we did not find the specified
  // userid column name from the value table's struct/proto.  Try to find the
  // userid column by name instead (this handles both non-value table columns,
  // and value table pseudocolumns).
  const Column* column =
      table->FindColumnByName(userid_column_name_path.back());
  if (column != nullptr) {
    std::unique_ptr<AnonymizationInfo> anonymization_info;
    GOOGLESQL_ASSIGN_OR_RETURN(
        AnonymizationUserIdInfo userid_info,
        AnonymizationUserIdInfo::Create(column));
    anonymization_info.reset(new AnonymizationInfo(std::move(userid_info)));
    return anonymization_info;
  }

  return not_found_error;
}
// static
absl::StatusOr<std::unique_ptr<AnonymizationInfo>> AnonymizationInfo::Create(
    absl::Span<const std::string> userid_column_name_path) {
  std::unique_ptr<AnonymizationInfo> anonymization_info;
  GOOGLESQL_ASSIGN_OR_RETURN(AnonymizationUserIdInfo userid_info,
                   AnonymizationUserIdInfo::Create(userid_column_name_path));
  anonymization_info.reset(new AnonymizationInfo(std::move(userid_info)));
  return anonymization_info;
}

absl::Span<const std::string> AnonymizationInfo::UserIdColumnNamePath() const {
  return userid_info_.get_column_name_path();
}

std::string Table::GetTableTypeName(ProductMode mode) const {
  std::string ret = "TABLE<";
  for (int i = 0; i < NumColumns(); ++i) {
    const Column* column = GetColumn(i);
    // Skip pseudo-columns such as _PARTITION_DATE
    if (column->IsPseudoColumn()) {
      continue;
    }
    if (i != 0) absl::StrAppend(&ret, ", ");
    if (!column->Name().empty()) {
      absl::StrAppend(&ret, ToIdentifierLiteral(column->Name()), " ");
    }
    absl::StrAppend(&ret, column->GetType()->TypeName(mode));
  }
  absl::StrAppend(&ret, ">");
  return ret;
}

// Note: This has a different name rather than being an overload of
// CreateEvaluatorTableIterator to avoid ambiguities for calls with {} or {0}.
absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
Table::CreateEvaluatorTableIteratorFromColumns(
    absl::Span<const Column* const> column_list) const {
  // Build a map from Column to its idx.
  absl::flat_hash_map<const Column*, int> column_to_idx_map;
  for (int idx = 0; idx < NumColumns(); ++idx) {
    column_to_idx_map[GetColumn(idx)] = idx;
  }

  // Convert the list of Columns to the list of idxs.
  std::vector<int> column_idx_list;
  column_idx_list.reserve(column_list.size());
  for (const Column* column : column_list) {
    const int* idx = googlesql_base::FindOrNull(column_to_idx_map, column);
    GOOGLESQL_RET_CHECK(idx != nullptr) << "Column " << column->FullName()
                              << " not found in table" << FullName();
    column_idx_list.push_back(*idx);
  }

  return CreateEvaluatorTableIterator(column_idx_list);
}

absl::StatusOr<std::vector<const Column*>> Table::ListLazyColumns(
    LazyColumnsTableScanContext* context,
    const Catalog::FindOptions& options) const {
  // This base class implementation works for tables with ColumnListMode
  // DEFAULT, where we don't need lazy lookups.
  GOOGLESQL_RET_CHECK(HasColumnList())
      << "ListLazyColumns not implemented for table " << FullName();

  std::vector<const Column*> columns;
  columns.reserve(NumColumns());
  for (int i = 0; i < NumColumns(); ++i) {
    columns.push_back(GetColumn(i));
  }
  return columns;
}

absl::StatusOr<Table::FindLazyColumnsResult> Table::FindLazyColumns(
    const absl::Span<const std::string>& column_names,
    LazyColumnsTableScanContext* context,
    const Catalog::FindOptions& options) const {
  // This base class implementation works for tables with ColumnListMode
  // DEFAULT, where we don't need lazy lookups.
  GOOGLESQL_RET_CHECK(HasColumnList())
      << "FindLazyColumns not implemented for table " << FullName();

  Table::FindLazyColumnsResult result;
  result.reserve(column_names.size());
  for (const std::string& column_name : column_names) {
    result.push_back(FindColumnByName(column_name));
  }
  return result;
}

absl::StatusOr<const Column*> Table::FindLazyColumn(
    absl::string_view column_name, LazyColumnsTableScanContext* context,
    const Catalog::FindOptions& options) const {
  GOOGLESQL_ASSIGN_OR_RETURN(
      FindLazyColumnsResult result,
      FindLazyColumns({std::string(column_name)}, context, options));
  GOOGLESQL_RET_CHECK_EQ(result.size(), 1);
  return result[0];
}

}  // namespace googlesql
