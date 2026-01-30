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

#include "googlesql/testdata/sample_catalog_impl.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_property_graph.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/simple_evaluator_table_iterator.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/anon_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/procedure.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/testdata/ambiguous_has.pb.h"
#include "googlesql/testdata/referenced_schema.pb.h"
#include "googlesql/testdata/sample_annotation.h"
#include "googlesql/testdata/test_proto3.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

// The _UNLESS_DBG macros are a hack to make analyzer tests (and anything else
// using SampleCatalog) work to some extent in dbg builds.
//
// In dbg builds, stack usage is excessively high, and tests have small stacks
// by default, so out-of-stack errors are more common.  Some of the UDFs and
// other definitions in SampleCatalog trigger them.  Without these macros, that
// makes SampleCatalog construction ABSL_CHECK-fail, making all tests crash on
// startup.  That makes it impossible to get stack traces with line numbers or
// use a debugger.
//
// Where these _UNLESS_DBG macros are used, when we get an out-of-stack error
// (in debug builds only), the error is sent to ABSL_LOG(ERROR) but SampleCatalog
// construction continues.  We don't crash, we just omit the catalog definitions
// that don't work.  That means most the tests can run, and mostly pass, and
// debugging is possible.  Some tests will fail with a not-found error for the
// entries that got skipped, and some fail with unrelated out-of-stack errors.
//
// This is only needed in dbg builds but this gets applied for fastbuild too
// because the #ifdef symbols don't provide a way to distinguish those.
// opt builds will use the normal error handling and continue to ABSL_CHECK-fail.
//
// The ignore-errors behavior happens only for ResolvedExhausted errors so this
// will still ABSL_CHECK-fail if the definitions are actually bad, so it's still
// easy to notice the bad definitions.
#ifdef NDEBUG
#define RETURN_IF_ERROR_UNLESS_DBG(expr) GOOGLESQL_RETURN_IF_ERROR((expr))
#define CHECK_OK_UNLESS_DBG(expr) GOOGLESQL_CHECK_OK((expr))

#else

#define CHECK_OK_UNLESS_DBG(expr)                                          \
  do {                                                                     \
    absl::Status status = (expr);                                          \
    if (!status.ok()) {                                                    \
      if (absl::IsResourceExhausted(status)) {                             \
        ABSL_LOG(ERROR) << "Failed while loading SampleCatalog but continuing " \
                      "while running in a DEBUG build: "                   \
                   << status;                                              \
        return;                                                            \
      } else {                                                             \
        GOOGLESQL_CHECK_OK(status);                                                  \
      }                                                                    \
    }                                                                      \
  } while (false)

#define RETURN_IF_ERROR_UNLESS_DBG(expr)                                   \
  do {                                                                     \
    absl::Status status = (expr);                                          \
    if (!status.ok()) {                                                    \
      if (absl::IsResourceExhausted(status)) {                             \
        ABSL_LOG(ERROR) << "Failed while loading SampleCatalog but continuing " \
                      "while running in a DEBUG build: "                   \
                   << status;                                              \
      } else {                                                             \
        return status;                                                     \
      }                                                                    \
    }                                                                      \
  } while (false)
#endif

namespace googlesql {
namespace {

// A fluent builder for building FunctionArgumentType instances
class ArgBuilder {
 public:
  explicit ArgBuilder() = default;
  ArgBuilder&& Type(const Type* type) && {
    kind_ = ARG_TYPE_FIXED;
    type_ = type;
    return std::move(*this);
  }
  // Convenience wrappers for some common Type choices.
  // This should _not_ be exhaustive, prefer Type(...) over adding multitudes
  // of these wrappers
  ArgBuilder&& String() && {
    return std::move(*this).Type(types::StringType());
  }
  ArgBuilder&& Int64() && { return std::move(*this).Type(types::Int64Type()); }
  ArgBuilder&& Bool() && { return std::move(*this).Type(types::BoolType()); }
  ArgBuilder&& T1() && {
    kind_ = ARG_TYPE_ANY_1;
    return std::move(*this);
  }
  ArgBuilder&& T2() && {
    kind_ = ARG_TYPE_ANY_2;
    return std::move(*this);
  }
  ArgBuilder&& Any() && {
    kind_ = ARG_TYPE_ARBITRARY;
    return std::move(*this);
  }
  ArgBuilder&& Repeated() && {
    options_.set_cardinality(FunctionEnums::REPEATED);
    return std::move(*this);
  }
  ArgBuilder&& Optional() && {
    options_.set_cardinality(FunctionEnums::OPTIONAL);
    return std::move(*this);
  }
  // Implies optional.
  ArgBuilder&& Default(Value value) && {
    options_.set_default(value);
    options_.set_cardinality(FunctionEnums::OPTIONAL);
    return std::move(*this);
  }
  ArgBuilder&& Name(absl::string_view name) && {
    options_.set_argument_name(name, googlesql::kPositionalOrNamed);
    return std::move(*this);
  }
  ArgBuilder&& NameOnly(absl::string_view name) && {
    options_.set_argument_name(name, googlesql::kNamedOnly);
    return std::move(*this);
  }

  FunctionArgumentType Build() && {
    ABSL_CHECK(kind_.has_value());
    if (type_ != nullptr) {
      ABSL_CHECK(kind_ = ARG_TYPE_FIXED);
      return FunctionArgumentType(type_, options_);
    } else {
      return FunctionArgumentType(*kind_, options_);
    }
  }

 private:
  FunctionArgumentTypeOptions options_;
  std::optional<SignatureArgumentKind> kind_;
  const ::googlesql::Type* type_ = nullptr;
};

// A fluent class for constructing FuntionSignature objects.
class SignatureBuilder {
 public:
  explicit SignatureBuilder(
      googlesql_base::SourceLocation loc = googlesql_base::SourceLocation::current())
      : loc_(loc) {}

  SignatureBuilder&& AddArg(FunctionArgumentType t) && {
    args_.push_back(std::move(t));
    return std::move(*this);
  }
  SignatureBuilder&& AddArg(ArgBuilder&& t) && {
    return std::move(*this).AddArg(std::move(t).Build());
  }

  // By default, result type is string (many tests don't care about the return
  // type).
  SignatureBuilder&& Returns(FunctionArgumentType t) && {
    ret_type_ = std::move(t);
    return std::move(*this);
  }
  SignatureBuilder&& Returns(ArgBuilder&& t) && {
    return std::move(*this).Returns(std::move(t).Build());
  }

  // Sets context_id to line number, which can speed up debugging.
  FunctionSignature Build() && {
    return FunctionSignature(ret_type_, std::move(args_),
                             /*context_id=*/loc_.line());
  }

 private:
  FunctionArgumentTypeList args_;
  // Default to return string.
  FunctionArgumentType ret_type_ = {types::StringType()};
  googlesql_base::SourceLocation loc_;
};

}  // namespace

SampleCatalogImpl::SampleCatalogImpl(TypeFactory* type_factory) {
  if (type_factory == nullptr) {
    internal_type_factory_ = std::make_unique<TypeFactory>();
    types_ = internal_type_factory_.get();
  } else {
    types_ = type_factory;
  }
  catalog_ = std::make_unique<SimpleCatalog>("sample_catalog", types_);
}

SampleCatalogImpl::~SampleCatalogImpl() = default;

SimpleTable* SampleCatalogImpl::GetTableOrDie(absl::string_view name) {
  return googlesql_base::FindOrDie(tables_, name);
}

absl::StatusOr<SimpleTable*> SampleCatalogImpl::GetTable(
    absl::string_view name) {
  SimpleTable** table = googlesql_base::FindOrNull(tables_, name);
  if (table != nullptr) {
    return *table;
  } else {
    return googlesql_base::NotFoundErrorBuilder()
           << "SampleCatalog: Table " << name << " not found";
  }
}

const ProtoType* SampleCatalogImpl::GetProtoType(
    const google::protobuf::Descriptor* descriptor) {
  const Type* type;
  GOOGLESQL_CHECK_OK(catalog_->FindType({std::string(descriptor->full_name())}, &type));
  ABSL_CHECK(type != nullptr);
  ABSL_CHECK(type->IsProto());
  return type->AsProto();
}

const EnumType* SampleCatalogImpl::GetEnumType(
    const google::protobuf::EnumDescriptor* descriptor) {
  const Type* type;
  GOOGLESQL_CHECK_OK(catalog_->FindType({std::string(descriptor->full_name())}, &type));
  ABSL_CHECK(type != nullptr);
  ABSL_CHECK(type->IsEnum());
  return type->AsEnum();
}

static absl::StatusOr<const Type*> ComputeResultTypeCallbackForNullOfType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 1);
  GOOGLESQL_RET_CHECK_EQ(signature.NumConcreteArguments(), arguments.size());
  const LanguageOptions& language_options = analyzer_options.language();
  if (!arguments[0].is_literal() || arguments[0].is_literal_null()) {
    return MakeSqlError()
           << "Argument to NULL_OF_TYPE must be a literal string";
  }
  GOOGLESQL_RET_CHECK(arguments[0].type()->IsString());
  const Value& value = *arguments[0].literal_value();
  GOOGLESQL_RET_CHECK(!value.is_null());
  const absl::string_view type_name = value.string_value();
  const TypeKind type_kind =
      Type::ResolveBuiltinTypeNameToKindIfSimple(type_name, language_options);
  if (type_kind == TYPE_UNKNOWN) {
    return MakeSqlError() << "Type not implemented for NULL_OF_TYPE: "
                          << ToStringLiteral(absl::AsciiStrToUpper(type_name));
  }
  // We could parse complex type names here too by calling the type analyzer.
  return type_factory->MakeSimpleType(type_kind);
}

static absl::StatusOr<const Type*>
ComputeResultTypeCallbackForIntentionallyRetCheck(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  // This callback intentionally RET_CHECKs any time it's invoked.
  GOOGLESQL_RET_CHECK_FAIL() << "Intentionally RET_CHECKing for test";
}

// A ComputeResultTypeCallback that looks for the 'type_name' argument from the
// input list and uses its value to generate the result type.
static absl::StatusOr<const Type*> ComputeResultTypeFromStringArgumentValue(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_EQ(signature.NumConcreteArguments(), arguments.size());
  const LanguageOptions& language_options = analyzer_options.language();
  std::string type_name;
  for (int i = 0; i < arguments.size(); ++i) {
    if (!signature.ConcreteArgument(i).has_argument_name() ||
        signature.ConcreteArgument(i).argument_name() != "type_name") {
      continue;
    }

    const InputArgumentType& arg = arguments[i];
    GOOGLESQL_RET_CHECK(arg.type()->IsString());
    GOOGLESQL_RET_CHECK(arg.is_literal());
    if (arg.is_literal_null()) {
      return MakeSqlError() << "Argument 'type_name' cannot be NULL";
    }

    const Value& value = *arg.literal_value();
    GOOGLESQL_RET_CHECK(!value.is_null());
    type_name = value.string_value();
    const TypeKind type_kind =
        Type::ResolveBuiltinTypeNameToKindIfSimple(type_name, language_options);
    if (type_kind != TYPE_UNKNOWN) {
      return type_factory->MakeSimpleType(type_kind);
    }
    // Try to find the type in catalog.
    const Type* type = nullptr;
    std::vector<std::string> path;
    GOOGLESQL_RETURN_IF_ERROR(ParseIdentifierPath(type_name, LanguageOptions(), &path));
    if (catalog->FindType(path, &type).ok()) {
      return type;
    }
    break;
  }
  if (!type_name.empty()) {
    return MakeSqlError() << "Invalid type name provided: " << type_name;
  }
  // Use INT64 as return type if not overridden.
  return type_factory->get_int64();
}

static absl::StatusOr<const Type*> ComputeResultTypeCallbackToStruct(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  const StructType* struct_type;
  std::vector<StructType::StructField> struct_fields;
  struct_fields.reserve(arguments.size());
  for (int i = 0; i < arguments.size(); ++i) {
    struct_fields.push_back({absl::StrCat("field", i), arguments[i].type()});
  }
  GOOGLESQL_RET_CHECK_OK(
      type_factory->MakeStructType(std::move(struct_fields), &struct_type));
  return struct_type;
}

// Similar to `ComputeResultTypeCallbackToStruct`, but use the argument aliases
// in `arguments`, if specified, as the struct field names.
static absl::StatusOr<const Type*>
ComputeResultTypeCallbackToStructUseArgumentAliases(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  const StructType* struct_type;
  std::vector<StructType::StructField> struct_fields;
  struct_fields.reserve(arguments.size());
  for (int i = 0; i < arguments.size(); ++i) {
    std::string field_name;
    if (arguments[i].argument_alias().has_value()) {
      field_name = arguments[i].argument_alias()->ToString();
    } else {
      field_name = absl::StrCat("no_alias_", i);
    }
    struct_fields.push_back({field_name, arguments[i].type()});
  }
  GOOGLESQL_RET_CHECK_OK(type_factory->MakeStructType(struct_fields, &struct_type));
  return struct_type;
}

// Returns the annotation maps of the input arguments of the `function_call`.
static std::vector<const AnnotationMap*> GetArgumentAnnotations(
    const ResolvedFunctionCallBase& function_call) {
  std::vector<const AnnotationMap*> argument_annotations;
  if (!function_call.argument_list().empty()) {
    for (const std::unique_ptr<const ResolvedExpr>& argument :
         function_call.argument_list()) {
      argument_annotations.push_back(argument->type_annotation_map());
    }
  } else {
    // The function uses `generic_argument_list` to store its arguments.
    for (const std::unique_ptr<const ResolvedFunctionArgument>& argument :
         function_call.generic_argument_list()) {
      if (argument->expr() == nullptr) {
        // Only arguments of type `expr` can have collations.
        argument_annotations.push_back(nullptr);
      } else {
        argument_annotations.push_back(argument->expr()->type_annotation_map());
      }
    }
  }
  return argument_annotations;
}

// f(collation_1, ..., collation_n) -> STRUCT<collation_1, ..., collation_n>.
// If the return value is nullptr it means no collations.
static absl::StatusOr<const AnnotationMap*>
ComputeResultAnnotationsCallbackToStruct(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  const Type* result_type = function_call.type();
  std::vector<const AnnotationMap*> arguments =
      GetArgumentAnnotations(function_call);

  // Can only handle struct type, where each argument becomes a field of the
  // struct.
  GOOGLESQL_RET_CHECK(result_type->IsStruct());
  GOOGLESQL_RET_CHECK_EQ(result_type->AsStruct()->num_fields(), arguments.size());

  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(result_type);
  GOOGLESQL_RET_CHECK(annotation_map->IsStructMap());
  StructAnnotationMap* struct_annotation_map = annotation_map->AsStructMap();
  GOOGLESQL_RET_CHECK_EQ(struct_annotation_map->num_fields(), arguments.size());

  // We only check collation because currently the only supported annotation for
  // functions is collation.
  const int collation_annotation_id = CollationAnnotation::GetId();

  // The annotation of each input argument is added to the corresponding
  // struct field.
  for (int i = 0; i < arguments.size(); ++i) {
    if (arguments[i] == nullptr ||
        arguments[i]->GetAnnotation(collation_annotation_id) == nullptr) {
      continue;
    }
    struct_annotation_map->mutable_field(i)->SetAnnotation(
        collation_annotation_id,
        *arguments[i]->GetAnnotation(collation_annotation_id));
  }
  if (annotation_map->Empty()) {
    // Use nullptr rather than an empty annotation map when there are no
    // annotations.
    return nullptr;
  }
  return type_factory.TakeOwnership(std::move(annotation_map));
}

// f(ARRAY<collation_1>, ..., ARRAY<collation_n>) -> ARRAY<STRUCT<collation_1,
// ..., collation_n>>.
// If the return value is nullptr it means no collations.
static absl::StatusOr<const AnnotationMap*>
ComputeResultAnnotationsCallbackArraysToArrayOfStruct(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  const Type* result_type = function_call.type();
  std::vector<const AnnotationMap*> arguments =
      GetArgumentAnnotations(function_call);

  // The return type must be ARRAY<STRUCT>.
  GOOGLESQL_RET_CHECK(result_type->IsArray());
  GOOGLESQL_RET_CHECK(result_type->AsArray()->element_type()->IsStruct());
  GOOGLESQL_RET_CHECK_EQ(result_type->AsArray()->element_type()->AsStruct()->num_fields(),
               arguments.size());

  // We only check collation because currently the only supported annotation for
  // functions is collation.
  const int collation_annotation_id = CollationAnnotation::GetId();

  // Validations on the annotations of the input arguments.
  for (const AnnotationMap* argument : arguments) {
    if (argument == nullptr) {
      continue;
    }
    GOOGLESQL_RET_CHECK(argument->IsArrayMap());
    // Only the array elements may have collations, not the array themselves.
    GOOGLESQL_RET_CHECK_EQ(argument->AsArrayMap()->GetAnnotation(collation_annotation_id),
                 nullptr);
  }

  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(result_type);
  GOOGLESQL_RET_CHECK(annotation_map->IsArrayMap());
  GOOGLESQL_RET_CHECK(annotation_map->AsArrayMap()->element()->IsStructMap());

  ArrayAnnotationMap* array_annotation_map = annotation_map->AsArrayMap();
  StructAnnotationMap* element_struct_annotation_map =
      array_annotation_map->mutable_element()->AsStructMap();
  GOOGLESQL_RET_CHECK_EQ(element_struct_annotation_map->num_fields(), arguments.size());

  // Propagate the array element collations to the elements of the result array.
  for (int i = 0; i < arguments.size(); ++i) {
    if (arguments[i] == nullptr) {
      continue;
    }
    const AnnotationMap* argument_element_annotation_map =
        arguments[i]->AsArrayMap()->element();
    if (argument_element_annotation_map == nullptr ||
        argument_element_annotation_map->GetAnnotation(
            collation_annotation_id) == nullptr) {
      continue;
    }
    element_struct_annotation_map->mutable_field(i)->SetAnnotation(
        collation_annotation_id,
        *argument_element_annotation_map->GetAnnotation(
            collation_annotation_id));
  }
  if (annotation_map->Empty()) {
    // Use nullptr rather than an empty annotation map when there are no
    // annotations.
    return nullptr;
  }
  return type_factory.TakeOwnership(std::move(annotation_map));
}

// f(annotation_1, ..., annotation_n) -> annotation_n.
static absl::StatusOr<const AnnotationMap*>
ComputeResultAnnotationsCallbackUseTheFinalAnnotation(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  const Type* result_type = function_call.type();
  GOOGLESQL_RET_CHECK_EQ(result_type, type_factory.get_string());

  std::vector<const AnnotationMap*> arguments =
      GetArgumentAnnotations(function_call);

  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(result_type);
  // We only check collation because currently the only supported annotation for
  // functions is collation.
  const int collation_annotation_id = CollationAnnotation::GetId();
  for (int i = 0; i < arguments.size(); ++i) {
    if (arguments[i] == nullptr) {
      annotation_map->UnsetAnnotation(collation_annotation_id);
    } else {
      annotation_map->SetAnnotation(
          collation_annotation_id,
          *arguments[i]->GetAnnotation(collation_annotation_id));
    }
  }
  if (annotation_map->Empty()) {
    return nullptr;
  }
  return type_factory.TakeOwnership(std::move(annotation_map));
}

// f(collation_1, ..., collation_n) -> SQL error.
// This is to verify that the thrown error is used as why a function call is
// invalid.
static absl::StatusOr<const AnnotationMap*>
ComputeResultAnnotationsCallbackSqlError(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  for (const AnnotationMap* argument : GetArgumentAnnotations(function_call)) {
    if (argument != nullptr && !argument->Empty()) {
      return MakeSqlError() << "Arguments should not have annotations";
    }
  }
  return nullptr;
}

GoogleSQLBuiltinFunctionOptions SampleCatalogImpl::LoadDefaultSuppliedTypes(
    const GoogleSQLBuiltinFunctionOptions& options) {
  GoogleSQLBuiltinFunctionOptions options_copy = options;
  const ProtoType* approx_distance_function_options_proto_type;
  GOOGLESQL_CHECK_OK(types_->MakeProtoType(
      googlesql_test::TestApproxDistanceFunctionOptionsProto::GetDescriptor(),
      &approx_distance_function_options_proto_type));
  absl::flat_hash_set<FunctionSignatureId> approx_distance_function_ids = {
      FN_APPROX_COSINE_DISTANCE_FLOAT_WITH_PROTO_OPTIONS,
      FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS,
      FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT_WITH_PROTO_OPTIONS,
      FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS,
      FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS,
      FN_APPROX_DOT_PRODUCT_FLOAT_WITH_PROTO_OPTIONS,
      FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS};
  // Note that only argument index `2` is specified because only the third
  // argument in approximate distance functions supports supplied argument
  // types.
  for (const auto& id : approx_distance_function_ids) {
    std::pair<FunctionSignatureId, int> id_idx_pair = {id, 2};
    options_copy.argument_types[id_idx_pair] =
        approx_distance_function_options_proto_type;
  }
  options_copy
      .argument_types[{FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS, 4}] =
      approx_distance_function_options_proto_type;
  return options_copy;
}

void SampleCatalogImpl::LoadCatalogBuiltins(
    const GoogleSQLBuiltinFunctionOptions& builtin_function_options) {
  // Populate the sample catalog with the GoogleSQL functions using the
  // specified GoogleSQLBuiltinFunctionOptions.
  GOOGLESQL_CHECK_OK(catalog_->AddBuiltinFunctionsAndTypes(
      LoadDefaultSuppliedTypes(builtin_function_options)));
}

absl::Status SampleCatalogImpl::LoadCatalogImpl(
    const GoogleSQLBuiltinFunctionOptions& builtin_function_options) {
  const LanguageOptions& language_options =
      builtin_function_options.language_options;

  LoadCatalogBuiltins(builtin_function_options);

  // Make all proto Descriptors linked into this binary available.
  catalog_->SetDescriptorPool(google::protobuf::DescriptorPool::generated_pool());

  // Create a Catalog called alt_descriptor_pool which has a duplicate copy
  // of all protos in the main catalog, but in a different DescriptorPool.
  alt_descriptor_database_ = std::make_unique<google::protobuf::DescriptorPoolDatabase>(
      *google::protobuf::DescriptorPool::generated_pool());
  alt_descriptor_pool_ =
      std::make_unique<google::protobuf::DescriptorPool>(alt_descriptor_database_.get());

  SimpleCatalog* alt_descriptor_pool_catalog =
      catalog_->MakeOwnedSimpleCatalog("alt_descriptor_pool");
  alt_descriptor_pool_catalog->SetDescriptorPool(alt_descriptor_pool_.get());

  // Create a Catalog called ambiguous_has_descriptor_pool which has the
  // (modified) AmbiguousHasPB proto, obtained by changing the
  // "confusing_name_to_be_rewritten" field's name changed to "confusing_name".
  // This makes it such that the modified AmbiguousHasPB has a field called
  // "has_confusing_name" and "confusing_name". Such proto descriptors can occur
  // in practice, but not when C++ code is generated, hence this hack.
  google::protobuf::FileDescriptorProto modified_descriptor_proto;
  googlesql_test::AmbiguousHasPB::descriptor()->file()->CopyTo(
      &modified_descriptor_proto);
  bool found_message = false;
  for (google::protobuf::DescriptorProto& message_descriptor_proto :
       *modified_descriptor_proto.mutable_message_type()) {
    if (message_descriptor_proto.name() == "AmbiguousHasPB") {
      found_message = true;
      bool found_field = false;
      for (google::protobuf::FieldDescriptorProto& field_descriptor_proto :
           *message_descriptor_proto.mutable_field()) {
        if (field_descriptor_proto.name() == "confusing_name_to_be_rewritten") {
          found_field = true;
          field_descriptor_proto.set_name("confusing_name");
          break;
        }
      }
      ABSL_CHECK(found_field) << message_descriptor_proto;
    }
  }
  ABSL_CHECK(found_message) << modified_descriptor_proto;
  ambiguous_has_descriptor_pool_ = std::make_unique<google::protobuf::DescriptorPool>();
  ambiguous_has_descriptor_pool_->BuildFile(modified_descriptor_proto);

  auto ambiguous_has_descriptor_pool_catalog =
      std::make_unique<SimpleCatalog>("ambiguous_has_descriptor_pool");
  ambiguous_has_descriptor_pool_catalog->SetDescriptorPool(
      ambiguous_has_descriptor_pool_.get());
  catalog_->AddOwnedCatalog(ambiguous_has_descriptor_pool_catalog.release());

  // Add various kinds of objects to the catalog(s).
  // In debug builds, these skip over errors caused by out-of-stack failures.
  RETURN_IF_ERROR_UNLESS_DBG(LoadTypes());
  RETURN_IF_ERROR_UNLESS_DBG(LoadTables());
  LoadConnections();
  LoadSequences();
  LoadProtoTables();
  LoadViews(language_options);
  LoadNestedCatalogs();
  LoadFunctions();
  LoadFunctions2();
  LoadAllRegisteredCatalogChanges();
  RETURN_IF_ERROR_UNLESS_DBG(LoadExtendedSubscriptFunctions(language_options));
  LoadFunctionsWithDefaultArguments();
  LoadFunctionsWithStructArgs();
  LoadTemplatedSQLUDFs();
  LoadTableValuedFunctions1();
  LoadTableValuedFunctions2();
  LoadTableValuedFunctionsWithEvaluators();
  LoadTVFWithExtraColumns();
  LoadDescriptorTableValuedFunctions();
  LoadConnectionTableValuedFunctions();
  LoadTableValuedFunctionsWithDeprecationWarnings();
  LoadTableValuedFunctionsWithMultipleSignatures();
  RETURN_IF_ERROR_UNLESS_DBG(LoadTvfsWithTableSchema());
  LoadTemplatedSQLTableValuedFunctions();
  LoadTableValuedFunctionsWithAnonymizationUid();
  LoadTableValuedFunctionsWithOptionalRelations();
  LoadProcedures();
  LoadConstants();
  LoadWellKnownLambdaArgFunctions();
  LoadContrivedLambdaArgFunctions();
  LoadSqlFunctions(language_options);
  RETURN_IF_ERROR_UNLESS_DBG(LoadMeasureTables());
  LoadNonTemplatedSqlTableValuedFunctions(language_options);
  RETURN_IF_ERROR_UNLESS_DBG(LoadAmlBasedPropertyGraphs());
  LoadMultiSrcDstEdgePropertyGraphs();
  LoadCompositeKeyPropertyGraphs();
  LoadPropertyGraphWithDynamicLabelAndProperties();
  LoadPropertyGraphWithDynamicMultiLabelsAndProperties();
  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadTypes() {
  enum_TestEnum_ = GetEnumType(googlesql_test::TestEnum_descriptor());
  enum_AnotherTestEnum_ =
      GetEnumType(googlesql_test::AnotherTestEnum_descriptor());
  enum_TestEnumWithAnnotations_ =
      GetEnumType(googlesql_test::TestEnumWithAnnotations_descriptor());
  proto_KitchenSinkPB_ =
      GetProtoType(googlesql_test::KitchenSinkPB::descriptor());
  proto_MessageWithKitchenSinkPB_ =
      GetProtoType(googlesql_test::MessageWithKitchenSinkPB::descriptor());
  proto_CivilTimeTypesSinkPB_ =
      GetProtoType(googlesql_test::CivilTimeTypesSinkPB::descriptor());
  proto_TestExtraPB_ = GetProtoType(googlesql_test::TestExtraPB::descriptor());
  proto_abPB_ = GetProtoType(googlesql_test::TestAbPB::descriptor());
  proto_bcPB_ = GetProtoType(googlesql_test::TestBcPB::descriptor());
  proto_EmptyMessage_ =
      GetProtoType(googlesql_test::EmptyMessage::descriptor());
  proto3_KitchenSinkPB_ =
      GetProtoType(googlesql_test::Proto3KitchenSink::descriptor());
  proto3_MessageWithInvalidMap_ =
      GetProtoType(googlesql_test::MessageWithInvalidMap::descriptor());
  proto_field_formats_proto_ =
      GetProtoType(googlesql_test::FieldFormatsProto::descriptor());
  proto_MessageWithMapField_ =
      GetProtoType(googlesql_test::MessageWithMapField::descriptor());
  proto_approx_distance_function_options_ = GetProtoType(
      googlesql_test::TestApproxDistanceFunctionOptionsProto::descriptor());

  // We want to pull AmbiguousHasPB from the descriptor pool where it was
  // modified, not the generated pool.
  const google::protobuf::Descriptor* ambiguous_has_descriptor =
      ambiguous_has_descriptor_pool_->FindMessageTypeByName(
          "googlesql_test.AmbiguousHasPB");
  ABSL_CHECK(ambiguous_has_descriptor);
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeProtoType(ambiguous_has_descriptor, &proto_ambiguous_has_));

  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int32()}, {"b", types_->get_string()}},
      &struct_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"c", types_->get_int32()}, {"d", struct_type_}}, &nested_struct_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"e", types_->get_int32()}, {"f", nested_struct_type_}},
      &doubly_nested_struct_type_));

  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_int32(), &int32array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_int64(), &int64array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_uint32(), &uint32array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_uint64(), &uint64array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_bytes(), &bytes_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_bool(), &bool_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_float(), &float_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_double(), &double_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_date(), &date_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_string(), &string_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_timestamp(), &timestamp_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(proto_TestExtraPB_, &proto_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(struct_type_, &struct_array_type_));
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(types_->get_json(), &json_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_numeric(), &numeric_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_bignumeric(), &bignumeric_array_type_));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeArrayType(types_->get_interval(), &interval_array_type_));

  GOOGLESQL_ASSIGN_OR_RETURN(int32map_type_, types_->MakeMapType(types_->get_int32(),
                                                       types_->get_int32()));
  GOOGLESQL_ASSIGN_OR_RETURN(int64map_type_, types_->MakeMapType(types_->get_int64(),
                                                       types_->get_int64()));
  GOOGLESQL_ASSIGN_OR_RETURN(bytesmap_type_, types_->MakeMapType(types_->get_bytes(),
                                                       types_->get_bytes()));

  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"x", types_->get_int64()},
                                       {"y", struct_type_},
                                       {"z", struct_array_type_}},
                                      &struct_with_array_field_type_));

  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"x", types_->get_int64()}},
                                      &struct_with_one_field_type_));

  const StructType* struct_with_just_kitchen_sink_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"kitchen_sink", proto_KitchenSinkPB_}},
                                      &struct_with_just_kitchen_sink_type));
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeStructType({{"kitchen_sink", proto_KitchenSinkPB_},
                              {"s", struct_with_just_kitchen_sink_type}},
                             &struct_with_kitchen_sink_type_));
  const ArrayType* array_of_struct_with_kitchen_sink_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(struct_with_just_kitchen_sink_type,
                                     &array_of_struct_with_kitchen_sink_type));
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int64()},
       {"b", array_of_struct_with_kitchen_sink_type}},
      &struct_of_array_of_struct_with_kitchen_sink_type_));

  // Add a named struct type for testing name collisions.
  const StructType* name_conflict_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"Key", types_->get_int64()}, {"Value", types_->get_string()}},
      &name_conflict_type));
  catalog_->AddType("NameConflictType", name_conflict_type);

  // Add a simple type for testing alias type from engine catalog
  catalog_->AddType("INT64AliasType", types_->get_int64());

  return absl::OkStatus();
}

namespace {

// Implementation of EvaluatorTableIterator which ignores SetReadTime(), but
// delegates all other methods to an underlying iterator passed to the
// constructor.
class IgnoreReadTimeIterator : public EvaluatorTableIterator {
 public:
  explicit IgnoreReadTimeIterator(
      std::unique_ptr<EvaluatorTableIterator> iterator)
      : iterator_(std::move(iterator)) {}

  int NumColumns() const override { return iterator_->NumColumns(); }
  std::string GetColumnName(int i) const override {
    return iterator_->GetColumnName(i);
  }
  const Type* GetColumnType(int i) const override {
    return iterator_->GetColumnType(i);
  }
  absl::Status SetColumnFilterMap(
      absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map)
      override {
    return iterator_->SetColumnFilterMap(std::move(filter_map));
  }
  absl::Status SetReadTime(absl::Time read_time) override {
    return absl::OkStatus();
  }
  bool NextRow() override { return iterator_->NextRow(); }
  const Value& GetValue(int i) const override { return iterator_->GetValue(i); }
  absl::Status Status() const override { return iterator_->Status(); }
  absl::Status Cancel() override { return iterator_->Cancel(); }
  void SetDeadline(absl::Time deadline) override {
    iterator_->SetDeadline(deadline);
  }

 private:
  std::unique_ptr<EvaluatorTableIterator> iterator_;
};

// Minimal table implementation to support testing of FOR SYSTEM TIME AS OF.
// This is just a modified version of SimpleTable which ignores the read time.
class SimpleTableWithReadTimeIgnored : public SimpleTable {
 public:
  SimpleTableWithReadTimeIgnored(absl::string_view name,
                                 absl::Span<const NameAndType> columns,
                                 const int64_t id = 0)
      : SimpleTable(name, columns, id) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluatorTableIterator(
      absl::Span<const int> column_idxs) const override {
    std::unique_ptr<EvaluatorTableIterator> iterator;
    GOOGLESQL_ASSIGN_OR_RETURN(iterator,
                     SimpleTable::CreateEvaluatorTableIterator(column_idxs));
    return std::make_unique<IgnoreReadTimeIterator>(std::move(iterator));
  }
};

// LazySimpleTable is a specialization of SimpleTable that uses
// lazy column lookup modes.
// In this implementation, the columns are actually preloaded in the
// SimpleTable containers, but the default (non-lazy) methods don't return
// them.  This is mainly suitable for testing.
class LazySimpleTable : public SimpleTable {
 public:
  LazySimpleTable(absl::string_view name, ColumnListMode column_list_mode,
                  absl::Span<const NameAndType> columns,
                  int64_t serialization_id = 0)
      : SimpleTable(name, columns, serialization_id),
        column_list_mode_(column_list_mode) {}

  ColumnListMode GetColumnListMode() const override {
    return column_list_mode_;
  }

  int NumColumns() const override { return 0; }
  const Column* GetColumn(int i) const override { return nullptr; }
  const Column* FindColumnByName(const std::string& name) const override {
    return nullptr;
  }

  absl::StatusOr<std::vector<const Column*>> ListLazyColumns(
      LazyColumnsTableScanContext* context,
      const Catalog::FindOptions& options) const override {
    if (!SupportsListLazyColumns()) {
      // Return the default error from the superclass.
      return Table::ListLazyColumns(context, options);
    }
    std::vector<const Column*> result;
    result.reserve(SimpleTable::NumColumns());
    for (int i = 0; i < SimpleTable::NumColumns(); ++i) {
      result.push_back(SimpleTable::GetColumn(i));
    }
    return result;
  }

  absl::StatusOr<FindLazyColumnsResult> FindLazyColumns(
      const absl::Span<const std::string>& column_names,
      LazyColumnsTableScanContext* context,
      const Catalog::FindOptions& options) const override {
    Table::FindLazyColumnsResult result;
    for (const std::string& column_name : column_names) {
      result.push_back(SimpleTable::FindColumnByName(column_name));
    }
    return result;
  }

  // Set contents, loading into `column_contents_`.
  // Derived from SimpleTable SetContents and iterators, adapted to use
  // a map keyed by Column* to store column values vectors.
  void SetContents(absl::Span<const std::vector<Value>> rows) {
    column_contents_.clear();

    for (int i = 0; i < SimpleTable::NumColumns(); ++i) {
      const Column* column = SimpleTable::GetColumn(i);
      auto column_values = std::make_shared<std::vector<Value>>();
      column_values->reserve(rows.size());
      for (int j = 0; j < rows.size(); ++j) {
        column_values->push_back(rows[j][i]);
      }
      column_contents_[column] = column_values;
    }
    num_rows_ = rows.size();
  }

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluatorTableIterator(
      absl::Span<const int> column_idxs) const override {
    GOOGLESQL_RET_CHECK_FAIL()
        << "LazySimpleTable does not support CreateEvaluatorTableIterator";
  }

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluatorTableIteratorFromColumns(
      absl::Span<const Column* const> columns) const override {
    std::vector<std::shared_ptr<const std::vector<Value>>> column_values;
    column_values.reserve(columns.size());
    for (const Column* column : columns) {
      const auto* values = googlesql_base::FindOrNull(column_contents_, column);
      GOOGLESQL_RET_CHECK(values != nullptr) << "Column " << column->FullName()
                                   << " not found in table " << FullName();
      column_values.push_back(*values);
    }
    return std::unique_ptr<EvaluatorTableIterator>(
        new SimpleEvaluatorTableIterator(
            columns, column_values, num_rows_,
            /*end_status=*/absl::OkStatus(), /*filter_column_idxs=*/{},
            /*cancel_cb=*/[]() {},
            /*set_deadline_cb=*/[](absl::Time t) {}, googlesql_base::Clock::RealClock()));
  }

 private:
  ColumnListMode column_list_mode_ = ColumnListMode::DEFAULT;

  // Store contents set in SetContents.
  // This is copied from the SimpleTable implementation but modified to use
  // a map keyed by Column* since we can't reference columns by index.
  absl::flat_hash_map<const Column*, std::shared_ptr<const std::vector<Value>>>
      column_contents_;
  int64_t num_rows_ = 0;
};

// A TVF class whose resolved output schema is described by a const Table* or
// a table looked up from the catalog.
class TvfWithTableSchema : public TableValuedFunction {
 public:
  // Creates a TVF with output schema described by the given `result_table`.
  TvfWithTableSchema(const std::vector<std::string>& function_name_path,
                     const std::vector<FunctionSignature>& signatures,
                     const Table* result_table,
                     const TableValuedFunctionOptions& tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"", signatures,
                            tvf_options),
        result_table_(result_table) {}

  // Creates a TVF whose output schema is determined by looking up
  // a table named via the table name argument to the TVF.
  TvfWithTableSchema(const std::vector<std::string>& function_name_path,
                     const std::vector<FunctionSignature>& signatures,
                     const TableValuedFunctionOptions& tvf_options = {})
      : TableValuedFunction(function_name_path, /*group=*/"", signatures,
                            tvf_options) {}

  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* tvf_signature) const override {
    const Table* output_schema_table = result_table_;
    if (output_schema_table == nullptr) {
      // Dynamic lookup.
      GOOGLESQL_RET_CHECK_EQ(actual_arguments.size(), 1);
      const TVFInputArgumentType& table_name_argument = actual_arguments[0];
      GOOGLESQL_RET_CHECK(table_name_argument.is_scalar());
      GOOGLESQL_RET_CHECK(table_name_argument.scalar_expr()->Is<ResolvedLiteral>())
          << table_name_argument.scalar_expr()->DebugString();
      const std::string table_name = table_name_argument.scalar_expr()
                                         ->GetAs<ResolvedLiteral>()
                                         ->value()
                                         .string_value();
      GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({table_name}, &output_schema_table));
      if (output_schema_table == nullptr) {
        return absl::NotFoundError(
            absl::StrCat("Table not found: ", table_name));
      }
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        *tvf_signature,
        TVFSignature::Create(actual_arguments, output_schema_table, {}));
    return absl::OkStatus();
  }

 private:
  // If set, the output schema is described by this table.
  const Table* result_table_ = nullptr;
};

}  // namespace

absl::Status SampleCatalogImpl::AddGeneratedColumnToTable(
    std::string column_name, std::vector<std::string> expression_columns,
    std::string generated_expr, SimpleTable* table) {
  AnalyzerOptions options;
  options.mutable_language()->SetSupportsAllStatementKinds();
  std::unique_ptr<const AnalyzerOutput> output;
  for (const std::string& expression_column : expression_columns) {
    GOOGLESQL_RET_CHECK_OK(
        options.AddExpressionColumn(expression_column, types::Int64Type()));
  }
  GOOGLESQL_RET_CHECK_OK(AnalyzeExpression(generated_expr, options, catalog_.get(),
                                 catalog_->type_factory(), &output));
  GOOGLESQL_ASSIGN_OR_RETURN(
      SimpleColumn::ExpressionAttributes expr_attributes,
      SimpleColumn::ExpressionAttributes::Create(
          SimpleColumn::ExpressionAttributes::ExpressionKind::GENERATED,
          generated_expr, output->resolved_expr()));
  GOOGLESQL_RET_CHECK_OK(table->AddColumn(
      new SimpleColumn(table->Name(), column_name, types::Int64Type(),
                       {.column_expression = expr_attributes}),
      /*is_owned=*/true));
  sql_object_artifacts_.push_back(std::move(output));
  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadTables() {
  SimpleTable* value_table = new SimpleTable(
      "Value", {{"Value", types_->get_int64()},
                // to test while() loop in SQLBuilder::GetScanAlias
                {"Value_1", types_->get_int64()}});
  AddOwnedTable(value_table);

  const std::vector<SimpleTable::NameAndType> key_value_columns = {
      {"Key", types_->get_int64()}, {"Value", types_->get_string()}};
  const std::vector<std::vector<Value>> key_value_contents = {
      {Value::Int64(1), Value::String("a")},
      {Value::Int64(2), Value::String("b")}};

  SimpleTable* key_value_table = new SimpleTable("KeyValue", key_value_columns);
  key_value_table->SetContents(key_value_contents);

  AddOwnedTable(key_value_table);
  key_value_table_ = key_value_table;

  // Copy of KeyValue with ColumnListMode::LAZY.
  LazySimpleTable* key_value_lazy_table = new LazySimpleTable(
      "KeyValueLazy", Table::ColumnListMode::LAZY, key_value_columns);
  key_value_lazy_table->SetContents(key_value_contents);
  AddOwnedTable(key_value_lazy_table);

  // Copy of KeyValue with ColumnListMode::FIND_ONLY.
  LazySimpleTable* key_value_find_only_table = new LazySimpleTable(
      "KeyValueFindOnly", Table::ColumnListMode::FIND_ONLY, key_value_columns);
  key_value_find_only_table->SetContents(key_value_contents);
  AddOwnedTable(key_value_find_only_table);

  SimpleTable* multiple_columns_table =
      new SimpleTable("MultipleColumns", {{"int_a", types_->get_int64()},
                                          {"string_a", types_->get_string()},
                                          {"int_b", types_->get_int64()},
                                          {"string_b", types_->get_string()},
                                          {"int_c", types_->get_int64()},
                                          {"int_d", types_->get_int64()}});
  AddOwnedTable(multiple_columns_table);

  SimpleTable* table_with_ts_column = new SimpleTable(
      "TableWithTimestampColumn", {
                                      {"ts_col", types_->get_timestamp()},
                                  });
  AddOwnedTable(table_with_ts_column);

  SimpleTable* table_with_mixed_case_column = new SimpleTable(
      "TableWithMixedCaseColumn", {
                                      {"ts_col", types_->get_timestamp()},
                                      {"MiXeD_cAsE", types_->get_string()},
                                  });
  AddOwnedTable(table_with_mixed_case_column);

  SimpleTable* key_value_with_primary_key_table = new SimpleTable(
      "KeyValueWithPrimaryKey",
      {{"Key", types_->get_int64()}, {"Value", types_->get_string()}});
  key_value_with_primary_key_table->SetContents(
      {{Value::Int64(1), Value::String("a")},
       {Value::Int64(2), Value::String("b")}});
  GOOGLESQL_RETURN_IF_ERROR(key_value_with_primary_key_table->SetPrimaryKey({0}));
  AddOwnedTable(key_value_with_primary_key_table);

  SimpleTable* update_to_default_table = new SimpleTable(
      "UpdateToDefaultTable", {{"writable", types_->get_int64()}});
  GOOGLESQL_RET_CHECK_OK(update_to_default_table->AddColumn(
      new SimpleColumn(update_to_default_table->Name(), "readonly",
                       types_->get_int64(), {.is_writable_column = false}),
      /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(update_to_default_table->AddColumn(
      new SimpleColumn(update_to_default_table->Name(),
                       "readonly_settable_to_default", types_->get_int64(),
                       {.is_writable_column = false,
                        .can_update_unwritable_to_default = true}),
      /*is_owned=*/true));
  AddOwnedTable(update_to_default_table);

  SimpleTable* ab_table = new SimpleTable(
      "abTable", {{"a", types_->get_int64()}, {"b", types_->get_string()}});
  AddOwnedTable(ab_table);
  SimpleTable* bc_table = new SimpleTable(
      "bcTable", {{"b", types_->get_int64()}, {"c", types_->get_string()}});
  AddOwnedTable(bc_table);

  SimpleTable* key_value_table_read_time_ignored =
      new SimpleTableWithReadTimeIgnored(
          "KeyValueReadTimeIgnored",
          {{"Key", types_->get_int64()}, {"Value", types_->get_string()}});
  AddOwnedTable(key_value_table_read_time_ignored);

  SimpleTable* another_key_value = new SimpleTable(
      "AnotherKeyValue",
      {{"Key", types_->get_int64()}, {"value", types_->get_string()}});
  AddOwnedTable(another_key_value);

  const SimpleModel* one_double_model =
      new SimpleModel("OneDoubleModel", {{"a", types_->get_double()}},
                      {{"label", types_->get_double()}}, /*id=*/0);
  const SimpleModel* one_double_one_string_model = new SimpleModel(
      "OneDoubleOneStringModel",
      {{"a", types_->get_double()}, {"b", types_->get_string()}},
      {{"label", types_->get_double()}}, /*id=*/1);
  const SimpleModel* one_double_two_output_model = new SimpleModel(
      "OneDoubleTwoOutputModel", {{"a", types_->get_double()}},
      {{"label1", types_->get_double()}, {"label2", types_->get_double()}},
      /*id=*/2);
  catalog_->AddOwnedModel(one_double_model);
  catalog_->AddOwnedModel(one_double_one_string_model);
  catalog_->AddOwnedModel(one_double_two_output_model);

  SimpleTable* key_value2_table = new SimpleTable(
      "KeyValue2",
      {{"Key", types_->get_int64()}, {"Value2", types_->get_string()}});
  AddOwnedTable(key_value2_table);

  SimpleTable* space_value_table = new SimpleTable(
      " Value",
      {{" Key", types_->get_int64()}, {" Value", types_->get_string()}});
  AddOwnedTable(space_value_table);

  SimpleTable* table_with_default_column = new SimpleTable(
      "TableWithDefaultColumn",
      {{"id", types_->get_int64()}, {"a", types_->get_int64()}});

  const std::string default_expr = "10";
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_RET_CHECK_OK(AnalyzeExpression(default_expr, analyzer_options, catalog_.get(),
                                 catalog_->type_factory(), &output));

  GOOGLESQL_ASSIGN_OR_RETURN(
      SimpleColumn::ExpressionAttributes expr_attributes,
      SimpleColumn::ExpressionAttributes::Create(
          SimpleColumn::ExpressionAttributes::ExpressionKind::DEFAULT,
          default_expr, output->resolved_expr()));
  GOOGLESQL_RET_CHECK_OK(table_with_default_column->AddColumn(
      new SimpleColumn(table_with_default_column->Name(), "default_col",
                       types_->get_int64(),
                       {.column_expression = expr_attributes}),
      /*is_owned=*/true));

  sql_object_artifacts_.emplace_back(std::move(output));
  AddOwnedTable(table_with_default_column);

  SimpleTable* table_with_generated_column = new SimpleTable(
      "TableWithGeneratedColumn", std::vector<SimpleTable::NameAndType>{});
  // Adding column A AS (B+C) to table.
  GOOGLESQL_RET_CHECK_OK(AddGeneratedColumnToTable("A", {"B", "C"}, "B+C",
                                         table_with_generated_column));
  // Adding column B AS (C+1) to table.
  GOOGLESQL_RET_CHECK_OK(AddGeneratedColumnToTable("B", {"C"}, "C+1",
                                         table_with_generated_column));
  // Adding column C int64 to table.
  GOOGLESQL_RET_CHECK_OK(table_with_generated_column->AddColumn(
      new SimpleColumn(table_with_generated_column->Name(), "C",
                       types::Int64Type()),
      /*is_owned=*/true));
  // Adding column D AS (A+B+C) to table.
  GOOGLESQL_RET_CHECK_OK(AddGeneratedColumnToTable("D", {"A", "B", "C"}, "A+B+C",
                                         table_with_generated_column));
  AddOwnedTable(table_with_generated_column);

  // Create two tables with the following schema.
  // GeoStructTable1: geo STRUCT<a GEOGRAPHY>
  // GeoStructTable2: geo STRUCT<b GEOGRAPHY>
  const StructType* struct_with_geo_type1;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"a", types_->get_geography()}},
                                      &struct_with_geo_type1));
  const StructType* struct_with_geo_type2;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"b", types_->get_geography()}},
                                      &struct_with_geo_type2));

  SimpleTable* geo_struct_table1 =
      new SimpleTable("GeoStructTable1", {{"geo", struct_with_geo_type1}});
  AddOwnedTable(geo_struct_table1);

  SimpleTable* geo_struct_table2 =
      new SimpleTable("GeoStructTable2", {{"geo", struct_with_geo_type2}});
  AddOwnedTable(geo_struct_table2);

  // Create a table with a struct containing JSON.
  const StructType* struct_with_json_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"json", types_->get_json()}, {"json_2", types_->get_json()}},
      &struct_with_json_type));
  SimpleTable* struct_with_json_table = new SimpleTable(
      "StructWithJsonTable", {{"json_struct", struct_with_json_type}});
  AddOwnedTable(struct_with_json_table);

  const StructType* struct_with_json_and_int_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"json", types_->get_json()}, {"int", types_->get_int64()}},
      &struct_with_json_and_int_type));
  const ArrayType* array_of_struct_with_json_and_int_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(struct_with_json_and_int_type,
                                     &array_of_struct_with_json_and_int_type));
  SimpleTable* table_with_array_of_struct_with_json_and_int = new SimpleTable(
      "ArrayOfStructOfJsonAndIntTable",
      {{"json_and_int_array", array_of_struct_with_json_and_int_type}});
  AddOwnedTable(table_with_array_of_struct_with_json_and_int);

  auto collatedTable = new SimpleTable("CollatedTable");
  const AnnotationMap* annotation_map_string_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types_->get_string());
    annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_string_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_string_binary;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types_->get_string());
    annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String("binary"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_string_binary,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_struct_with_string_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_type_);
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_struct_with_string_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_array_with_string_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(string_array_type_);
    annotation_map->AsArrayMap()
        ->mutable_element()
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_array_with_string_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  auto string_ci = new SimpleColumn(
      collatedTable->Name(), "string_ci",
      AnnotatedType(types_->get_string(), annotation_map_string_ci));

  auto string_cs = new SimpleColumn(
      collatedTable->Name(), "string_binary",
      AnnotatedType(types_->get_string(), annotation_map_string_binary));

  auto struct_ci = new SimpleColumn(
      collatedTable->Name(), "struct_with_string_ci",
      AnnotatedType(struct_type_, annotation_map_struct_with_string_ci));

  auto array_ci = new SimpleColumn(
      collatedTable->Name(), "array_with_string_ci",
      AnnotatedType(string_array_type_, annotation_map_array_with_string_ci));

  GOOGLESQL_RET_CHECK_OK(collatedTable->AddColumn(string_ci, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(collatedTable->AddColumn(string_cs, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(collatedTable->AddColumn(struct_ci, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(collatedTable->AddColumn(array_ci, /*is_owned=*/true));
  AddOwnedTable(collatedTable);

  auto complex_collated_table = new SimpleTable("ComplexCollatedTable");

  const StructType* struct_with_bool_string_type;
  // We let the first field have bool (rather than int32) type to avoid
  // implicitly coercing ARRAY<STRUCT<a INT64, b STRING>> to
  // ARRAY<STRUCT<a INT32, b STRING>> in the testing, which is currently not
  // supported.
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_bool()}, {"b", types_->get_string()}},
      &struct_with_bool_string_type));
  const ArrayType* array_of_struct_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeArrayType(struct_with_bool_string_type,
                                     &array_of_struct_type));
  const StructType* struct_with_array_of_struct_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int32()}, {"b", array_of_struct_type}},
      &struct_with_array_of_struct_type));

  const StructType* struct_of_multiple_string_type;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_string()}, {"b", types_->get_string()}},
      &struct_of_multiple_string_type));

  const StructType* struct_of_double_and_string_and_array_type;
  GOOGLESQL_RET_CHECK_OK(
      types_->MakeStructType({{"a", types_->get_double()},
                              {"b", types_->get_string()},
                              {"c", string_array_type_}},
                             &struct_of_double_and_string_and_array_type));

  const AnnotationMap* annotation_map_array_of_struct_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(array_of_struct_type);
    annotation_map->AsArrayMap()
        ->mutable_element()
        ->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_array_of_struct_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_struct_with_array_of_struct_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_with_array_of_struct_type);
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->AsArrayMap()
        ->mutable_element()
        ->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_struct_with_array_of_struct_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_struct_of_struct_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(nested_struct_type_);
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_struct_of_struct_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* annotation_map_struct_with_string_ci_binary;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_of_multiple_string_type);
    annotation_map->AsStructMap()
        ->mutable_field(0)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("binary"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_struct_with_string_ci_binary,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap*
      annotation_map_struct_of_double_and_string_ci_and_array_ci;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_of_double_and_string_and_array_type);
    // Set collation for the second field of STRING type.
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    // Set collation for the third field of ARRAY<STRING> type.
    annotation_map->AsStructMap()
        ->mutable_field(2)
        ->AsArrayMap()
        ->mutable_element()
        ->SetAnnotation<CollationAnnotation>(SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_struct_of_double_and_string_ci_and_array_ci,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  auto string_no_collation = new SimpleColumn(
      complex_collated_table->Name(), "string_no_collation",
      AnnotatedType(types_->get_string(), /*annotation_map=*/nullptr));

  auto array_of_struct_ci = new SimpleColumn(
      complex_collated_table->Name(), "array_of_struct_ci",
      AnnotatedType(array_of_struct_type, annotation_map_array_of_struct_ci));

  auto struct_with_array_of_struct_ci = new SimpleColumn(
      complex_collated_table->Name(), "struct_with_array_of_struct_ci",
      AnnotatedType(struct_with_array_of_struct_type,
                    annotation_map_struct_with_array_of_struct_ci));

  auto struct_of_struct_ci = new SimpleColumn(
      complex_collated_table->Name(), "struct_of_struct_ci",
      AnnotatedType(nested_struct_type_, annotation_map_struct_of_struct_ci));

  auto struct_with_string_ci_binary = new SimpleColumn(
      complex_collated_table->Name(), "struct_with_string_ci_binary",
      AnnotatedType(struct_of_multiple_string_type,
                    annotation_map_struct_with_string_ci_binary));

  auto struct_of_double_and_string_ci_and_array_ci = new SimpleColumn(
      complex_collated_table->Name(),
      "struct_of_double_and_string_ci_and_array_ci",
      AnnotatedType(
          struct_of_double_and_string_and_array_type,
          annotation_map_struct_of_double_and_string_ci_and_array_ci));

  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(string_no_collation,
                                                 /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(array_of_struct_ci,
                                                 /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(struct_with_array_of_struct_ci,
                                                 /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(struct_of_struct_ci,
                                                 /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(struct_with_string_ci_binary,
                                                 /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(complex_collated_table->AddColumn(
      struct_of_double_and_string_ci_and_array_ci,
      /*is_owned=*/true));
  AddOwnedTable(complex_collated_table);

  auto collated_table_with_proto = new SimpleTable("CollatedTableWithProto");
  const AnnotationMap* annotation_map_proto;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(proto_KitchenSinkPB_);
    annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String("und:ci"));
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map_proto,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  auto proto_with_collation = new SimpleColumn(
      collated_table_with_proto->Name(), "proto_with_collation",
      AnnotatedType(proto_KitchenSinkPB_, annotation_map_proto));

  GOOGLESQL_RET_CHECK_OK(collated_table_with_proto->AddColumn(proto_with_collation,
                                                    /*is_owned=*/true));
  AddOwnedTable(collated_table_with_proto);

  auto generic_annotation_test_table = new SimpleTable("AnnotatedTable");

  const AnnotationMap* generic_test_annotation_map_for_string_field;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types_->get_string());
    annotation_map->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(0));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_test_annotation_map_for_string_field,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* generic_test_annotation_map_for_int_field;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types_->get_int64());
    annotation_map->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(1));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_test_annotation_map_for_int_field,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* generic_annotation_map_for_struct_field;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_type_);
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(2));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_annotation_map_for_struct_field,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* generic_test_annotation_map_for_string_array_field;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(string_array_type_);
    annotation_map->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(3));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_test_annotation_map_for_string_array_field,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* generic_annotation_map_for_nested_struct_field;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(nested_struct_type_);
    annotation_map->AsStructMap()
        ->mutable_field(0)
        ->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(4));
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->AsStructMap()
        ->mutable_field(1)
        ->SetAnnotation<SampleAnnotation>(SimpleValue::Int64(5));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_annotation_map_for_nested_struct_field,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  const AnnotationMap* generic_annotation_map_for_map;
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(bytesmap_type_);
    StructAnnotationMap* composite_map = annotation_map->AsStructMap();

    composite_map->mutable_field(0)->SetAnnotation<SampleAnnotation>(
        SimpleValue::Int64(4));
    composite_map->mutable_field(1)->SetAnnotation<SampleAnnotation>(
        SimpleValue::Int64(5));
    GOOGLESQL_ASSIGN_OR_RETURN(generic_annotation_map_for_map,
                     types_->TakeOwnership(std::move(annotation_map)));
  }

  auto generic_annotation_test_string_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "string",
      AnnotatedType(types_->get_string(),
                    generic_test_annotation_map_for_string_field));
  auto generic_annotation_test_int_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "int_a",
      AnnotatedType(types_->get_int64(),
                    generic_test_annotation_map_for_int_field));
  auto generic_annotation_test_int_column_unannotated = new SimpleColumn(
      generic_annotation_test_table->Name(), "int_b", types_->get_int64());
  auto generic_annotation_test_struct_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "struct_a",
      AnnotatedType(struct_type_, generic_annotation_map_for_struct_field));
  auto generic_annotation_test_string_array_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "string_array",
      AnnotatedType(string_array_type_,
                    generic_test_annotation_map_for_string_array_field));
  auto generic_annotation_test_nested_struct_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "struct_b",
      AnnotatedType(nested_struct_type_,
                    generic_annotation_map_for_nested_struct_field));
  auto generic_annotation_test_map_column = new SimpleColumn(
      generic_annotation_test_table->Name(), "map",
      AnnotatedType(bytesmap_type_, generic_annotation_map_for_map));

  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_string_column, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_int_column, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_int_column_unannotated, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_struct_column, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_string_array_column, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_nested_struct_column, /*is_owned=*/true));
  GOOGLESQL_RET_CHECK_OK(generic_annotation_test_table->AddColumn(
      generic_annotation_test_map_column, /*is_owned=*/true));
  AddOwnedTable(generic_annotation_test_table);

  AddOwnedTable(new SimpleTable(
      "SimpleTypes",
      {{"int32", types_->get_int32()},
       {"int64", types_->get_int64()},
       {"uint32", types_->get_uint32()},
       {"uint64", types_->get_uint64()},
       {"string", types_->get_string()},
       {"bytes", types_->get_bytes()},
       {"bool", types_->get_bool()},
       {"float", types_->get_float()},
       {"double", types_->get_double()},
       {"date", types_->get_date()},
       // These types were removed, but we keep the fields in the sample
       // table in order not to disturb analyzer test results too much (all
       // the variable ids would change if we were to remove them).
       // TODO: Remove them when all other changes settled down.
       {"timestamp_seconds", types_->get_timestamp()},
       {"timestamp_millis", types_->get_timestamp()},
       {"timestamp_micros", types_->get_timestamp()},
       {"timestamp_nanos", types_->get_timestamp()},
       // Real types resume here.
       {"timestamp", types_->get_timestamp()},
       {"numeric", types_->get_numeric()},
       {"bignumeric", types_->get_bignumeric()},
       {"json", types_->get_json()},
       {"uuid", types_->get_uuid()}}));

  {
    auto table_with_single_int64_array_col = std::make_unique<SimpleTable>(
        "TableWithSingleInt64ArrayCol",
        std::vector<SimpleTable::NameAndType>{{"array_col", int64array_type_}});
    AddOwnedTable(table_with_single_int64_array_col.release());
  }

  {
    auto simple_table_with_uid =
        std::make_unique<SimpleTable>("SimpleTypesWithAnonymizationUid",
                                      std::vector<SimpleTable::NameAndType>{
                                          {"int32", types_->get_int32()},
                                          {"int64", types_->get_int64()},
                                          {"uint32", types_->get_uint32()},
                                          {"uint64", types_->get_uint64()},
                                          {"string", types_->get_string()},
                                          {"bytes", types_->get_bytes()},
                                          {"bool", types_->get_bool()},
                                          {"float", types_->get_float()},
                                          {"double", types_->get_double()},
                                          {"date", types_->get_date()},
                                          {"uid", types_->get_int64()},
                                          {"numeric", types_->get_numeric()}});
    GOOGLESQL_RET_CHECK_OK(simple_table_with_uid->SetAnonymizationInfo("uid"));
    AddOwnedTable(simple_table_with_uid.release());
  }

  {
    auto array_table_with_uid = std::make_unique<SimpleTable>(
        "ArrayWithAnonymizationUid", std::vector<SimpleTable::NameAndType>{
                                         {"int64_array", int64array_type_},
                                         {"double_array", double_array_type_},
                                         {"uid", types_->get_int64()}});
    GOOGLESQL_RET_CHECK_OK(array_table_with_uid->SetAnonymizationInfo("uid"));
    AddOwnedTable(array_table_with_uid.release());
  }

  {
    auto table_with_string_uid = std::make_unique<SimpleTable>(
        "T1StringAnonymizationUid",
        std::vector<SimpleTable::NameAndType>{{"uid", types_->get_string()},
                                              {"c2", types_->get_string()}});
    GOOGLESQL_RET_CHECK_OK(table_with_string_uid->SetAnonymizationInfo("uid"));
    AddOwnedTable(table_with_string_uid.release());
  }

  {
    auto table_with_string_uid = std::make_unique<SimpleTable>(
        "T2StringAnonymizationUid",
        std::vector<SimpleTable::NameAndType>{{"c1", types_->get_string()},
                                              {"uid", types_->get_string()}});
    GOOGLESQL_RET_CHECK_OK(table_with_string_uid->SetAnonymizationInfo("uid"));
    AddOwnedTable(table_with_string_uid.release());
  }

  {
    auto table_with_proto_uid = std::make_unique<SimpleTable>(
        "ProtoAnonymizationUid",
        std::vector<SimpleTable::NameAndType>{{"uid", proto_KitchenSinkPB_}});
    GOOGLESQL_RET_CHECK_OK(table_with_proto_uid->SetAnonymizationInfo("uid"));
    AddOwnedTable(table_with_proto_uid.release());
  }

  {
    auto value_table_with_uid = std::make_unique<SimpleTable>(
        "KitchenSinkWithUidValueTable", proto_KitchenSinkPB_);
    GOOGLESQL_RET_CHECK_OK(value_table_with_uid->SetAnonymizationInfo("string_val"));
    AddOwnedTable(value_table_with_uid.release());
  }

  {
    auto value_table_with_uid = std::make_unique<SimpleTable>(
        "TestStructWithUidValueTable", struct_type_);
    GOOGLESQL_RET_CHECK_OK(value_table_with_uid->SetAnonymizationInfo("a"));
    AddOwnedTable(value_table_with_uid.release());
  }

  {
    auto value_table_with_doubly_nested_uid = std::make_unique<SimpleTable>(
        "TestWithDoublyNestedStructUidValueTable", doubly_nested_struct_type_);
    GOOGLESQL_RET_CHECK_OK(value_table_with_doubly_nested_uid->SetAnonymizationInfo(
        {"f", "d", "a"}));
    AddOwnedTable(value_table_with_doubly_nested_uid.release());
  }

  {
    auto value_table_with_no_uid = std::make_unique<SimpleTable>(
        "TestStructWithNoUidValueTable", struct_type_);
    AddOwnedTable(value_table_with_no_uid.release());
  }

  {
    auto value_table_with_doubly_nested_no_uid = std::make_unique<SimpleTable>(
        "TestWithDoublyNestedStructNoUidValueTable",
        doubly_nested_struct_type_);
    AddOwnedTable(value_table_with_doubly_nested_no_uid.release());
  }

  {
    auto value_table_with_proto_uid = std::make_unique<SimpleTable>(
        "TestWithProtoUidValueTable", proto_MessageWithKitchenSinkPB_);
    GOOGLESQL_RET_CHECK_OK(value_table_with_proto_uid->SetAnonymizationInfo(
        {"kitchen_sink", "nested_value", "nested_int64"}));
    AddOwnedTable(value_table_with_proto_uid.release());
  }

  {
    auto value_table_with_proto_uid_of_wrong_type =
        std::make_unique<SimpleTable>("TestWithWrongTypeProtoUidValueTable",
                                      proto_MessageWithKitchenSinkPB_);
    GOOGLESQL_RET_CHECK_OK(value_table_with_proto_uid_of_wrong_type->SetAnonymizationInfo(
        std::vector<std::string>({"kitchen_sink", "nested_value"})));
    AddOwnedTable(value_table_with_proto_uid_of_wrong_type.release());
  }

  AddOwnedTable(
      new SimpleTable("GeographyTable", {{"key", types_->get_int64()},
                                         {"text", types_->get_string()},
                                         {"geo1", types_->get_geography()},
                                         {"geo2", types_->get_geography()}}));

  AddOwnedTable(new SimpleTable("NumericTypeTable",
                                {{"numeric_col", types_->get_numeric()}}));

  AddOwnedTable(new SimpleTable(
      "BigNumericTypeTable", {{"bignumeric_col", types_->get_bignumeric()}}));

  AddOwnedTable(
      new SimpleTable("JSONTable", {{"json_col", types_->get_json()}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_string_to_int64,
      types_->MakeMapType(types_->get_string(), types_->get_int64()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_string_to_nested_map_string_int64,
      types_->MakeMapType(types_->get_string(), map_type_string_to_int64));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_int64_to_string,
      types_->MakeMapType(types_->get_int64(), types_->get_string()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_int64_to_nested_map_int64_string,
      types_->MakeMapType(types_->get_int64(), map_type_int64_to_string));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_int64_to_nested_map_string_int64,
      types_->MakeMapType(types_->get_int64(), map_type_string_to_int64));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_bytes_to_bytes,
      types_->MakeMapType(types_->get_bytes(), types_->get_bytes()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* map_type_uint32_to_timestamp,
      types_->MakeMapType(types_->get_uint32(), types_->get_timestamp()));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* map_type_testenum_to_kitchensink,
                   types_->MakeMapType(enum_TestEnum_, proto_KitchenSinkPB_));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* map_type_string_to_struct,
                   types_->MakeMapType(types_->get_string(), struct_type_));
  auto* map_table = new SimpleTable(
      "MapTable",
      {
          {"map_col_string_to_int64", map_type_string_to_int64},
          {"map_col_string_to_nested_map_string_int64",
           map_type_string_to_nested_map_string_int64},
          {"map_col_int64_to_string", map_type_int64_to_string},
          {"map_col_int64_to_nested_map_int64_string",
           map_type_int64_to_nested_map_int64_string},
          {"map_col_int64_to_nested_map_string_int64",
           map_type_int64_to_nested_map_string_int64},
          {"map_col_bytes_to_bytes", map_type_bytes_to_bytes},
          {"map_col_uint32_to_timestamp", map_type_uint32_to_timestamp},
          {"map_col_testenum_to_kitchensink", map_type_testenum_to_kitchensink},
          {"map_col_string_to_struct", map_type_string_to_struct},
      });
  GOOGLESQL_RETURN_IF_ERROR(map_table->AddColumn(
      new SimpleColumn(
          map_table->Name(), "string_with_collation",
          AnnotatedType(types_->get_string(), annotation_map_string_ci)),
      /*is_owned=*/true));
  AddOwnedTable(map_table);

  SimpleTable* two_integers = new SimpleTable(
      "TwoIntegers",
      {{"key", types_->get_int64()}, {"value", types_->get_int64()}});
  GOOGLESQL_RET_CHECK_OK(two_integers->SetPrimaryKey({0}));
  AddOwnedTable(two_integers);

  SimpleTable* four_integers =
      new SimpleTable("FourIntegers", {{"key1", types_->get_int64()},
                                       {"value1", types_->get_int64()},
                                       {"key2", types_->get_int64()},
                                       {"value2", types_->get_int64()}});
  GOOGLESQL_RET_CHECK_OK(four_integers->SetPrimaryKey({0, 2}));
  AddOwnedTable(four_integers);

  // Tables with no columns are legal.
  AddOwnedTable(new SimpleTable("NoColumns"));

  // Add tables for testing name collisions.
  AddOwnedTable(
      new SimpleTable("NameConflictTable", {{"key", types_->get_int32()}}));
  AddOwnedTable(new SimpleTable(
      "name_conflict_table", {{"a", types_->get_string()},
                              {"name_conflict_field", types_->get_string()}}));

  // Add tables for testing create model with aliased queries.
  AddOwnedTable(
      new SimpleTable("user_training_data", {{"data", types_->get_int32()}}));
  AddOwnedTable(new SimpleTable("user_custom_holiday",
                                {{"region", types_->get_string()},
                                 {"holiday_name", types_->get_string()},
                                 {"primary_date", types_->get_date()}}));

  // Create two tables with common prefix.
  // One table "Int32Array" exists under the catalog "ArrayTableOrCatalog", so
  // its full path is "ArrayTableOrCatalog.Int32Array".
  // Another table "ArrayTableOrCatalog" exists under the root catalog, so its
  // full path is "ArrayTableOrCatalog". "ArrayTableOrCatalog.Int32Array" refers
  // to an array column.
  SimpleCatalog* array_catalog =
      catalog_->MakeOwnedSimpleCatalog("ArrayTableOrCatalog");
  SimpleTable* array_table_1 =
      new SimpleTable("Int32Array", {{"Int32Array", int32array_type_}});
  GOOGLESQL_RET_CHECK_OK(array_table_1->set_full_name("ArrayTableOrCatalog.Int32Array"));
  SimpleTable* array_table_2 = new SimpleTable(
      "ArrayTableOrCatalog", {{"Int32Array", int32array_type_}});

  array_catalog->AddOwnedTable("Int32Array", array_table_1);
  AddOwnedTable(array_table_2);

  // Add table for testing case insensitive lookup of column names.
  const StructType* struct_with_unicode_column_table;
  GOOGLESQL_RET_CHECK_OK(types_->MakeStructType({{"1𐌰:aô", types_->get_string()}},
                                      &struct_with_unicode_column_table));
  AddOwnedTable(new SimpleTable("unicode_column_table",
                                {{"å学", types_->get_int64()},
                                 {"ô", types_->get_string()},
                                 {"a", struct_with_unicode_column_table}}));

  // Tables for vector_search.
  SimpleTable* base_table =
      new SimpleTable("base_table", {{"id", types_->get_int64()},
                                     {"embedding", double_array_type_},
                                     {"string_col", types_->get_string()}});
  AddOwnedTable(base_table);

  SimpleTable* query_table = new SimpleTable(
      "query_table", {{"id", types_->get_int64()},
                      {"query_embedding", double_array_type_},
                      {"embedding", double_array_type_},
                      {"float_embedding", float_array_type_},
                      {"string_query_col", types_->get_string()}});
  AddOwnedTable(query_table);

  SimpleTable* base_table_wrong_type = new SimpleTable(
      "base_table_wrong_type",
      {{"id", types_->get_int64()}, {"embedding", string_array_type_}});
  AddOwnedTable(base_table_wrong_type);

  return absl::OkStatus();
}  // NOLINT(readability/fn_size)

// If `is_value_table` is true, validate that the first column in
// `columns_not_owned` is a non-pseudo column representing the row type of the
// value table and that all other columns and measures are pseudo columns.
static absl::Status ValidateValueTableness(
    absl::Span<const Column* const> columns_not_owned,
    absl::Span<const MeasureColumnDef> measures, bool is_value_table) {
  // If not a value table, no validation is needed.
  if (!is_value_table) {
    return absl::OkStatus();
  }
  // Count the number of non-pseudo columns in the table.
  int non_pseudo_column_count = 0;
  std::for_each(columns_not_owned.begin(), columns_not_owned.end(),
                [&non_pseudo_column_count](const Column* column) {
                  if (!column->IsPseudoColumn()) {
                    ++non_pseudo_column_count;
                  }
                });
  std::for_each(measures.begin(), measures.end(),
                [&non_pseudo_column_count](const MeasureColumnDef& measure) {
                  if (!measure.is_pseudo_column) {
                    ++non_pseudo_column_count;
                  }
                });
  GOOGLESQL_RET_CHECK_EQ(non_pseudo_column_count, 1);
  // Make sure the first column is the non-pseudo column.
  GOOGLESQL_RET_CHECK(!columns_not_owned.empty());
  GOOGLESQL_RET_CHECK(!columns_not_owned[0]->IsPseudoColumn());
  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::AddTableWithMeasures(
    AnalyzerOptions& analyzer_options, absl::string_view table_name,
    std::vector<const Column*> columns_not_owned,
    std::optional<absl::btree_set<int>> row_identity_column_indices,
    std::vector<MeasureColumnDef> measures, bool is_value_table) {
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateValueTableness(columns_not_owned, measures, is_value_table));
  auto table = std::make_unique<SimpleTable>(table_name, columns_not_owned,
                                             /*take_ownership=*/true);
  table->set_is_value_table(is_value_table);
  if (row_identity_column_indices.has_value()) {
    for (int row_identity_column_index : row_identity_column_indices.value()) {
      GOOGLESQL_RET_CHECK_GE(row_identity_column_index, 0);
      GOOGLESQL_RET_CHECK_LT(row_identity_column_index, columns_not_owned.size());
    }
    GOOGLESQL_RETURN_IF_ERROR(table->SetRowIdentityColumns(
        std::vector<int>(row_identity_column_indices.value().begin(),
                         row_identity_column_indices.value().end())));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs,
      AddMeasureColumnsToTable(*table, measures, *types_, *catalog_,
                               analyzer_options));
  sql_object_artifacts_.insert(
      sql_object_artifacts_.end(),
      std::make_move_iterator(analyzer_outputs.begin()),
      std::make_move_iterator(analyzer_outputs.end()));
  AddOwnedTable(table.release());
  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadMeasureTables() {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_MULTILEVEL_AGGREGATION);
  analyzer_options.mutable_language()->EnableLanguageFeature(FEATURE_JSON_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_WITH_EXPRESSION);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_FIRST_AND_LAST_N);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_INLINE_LAMBDA_ARGUMENT);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_BITWISE_AGGREGATE_BYTES_SIGNATURES);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_NAMED_ARGUMENTS);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_MULTILEVEL_AGGREGATION_ON_UDAS);

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_SingleKey",
      {new SimpleColumn("MeasureTable_SingleKey", "key", types_->get_int64()),
       new SimpleColumn("MeasureTable_SingleKey", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_SingleKey", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_SingleKey", "price",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)"},
       {"measure_count_star_per_key", "COUNT(* GROUP BY key)"},
       {"measure_count_distinct_key", "COUNT(DISTINCT key)"},
       {"measure_count_key_per_key", "COUNT(1 GROUP BY key)"},
       {"measure_sum_quantity", "SUM(quantity)"},
       {"measure_sum_price", "SUM(price)"},
       {"measure_sum_price_times_quantity", "SUM(price * quantity)"},
       {"measure_ratio_price_to_quantity", "SUM(price) / SUM(quantity)"},
       {"measure_ratio_price_to_quantity_per_key",
        "SUM(ANY_VALUE(price) GROUP BY key) / "
        "SUM(ANY_VALUE(quantity) GROUP BY key)"},
       {"measure_complex_ratio_metric",
        "SUM(AVG(price) + MIN(price) GROUP BY key) / "
        "SUM(AVG(price) + MAX(price) GROUP BY key)"}},
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_TwoKeys",
      {new SimpleColumn("MeasureTable_TwoKeys", "KEY1", types_->get_int64()),
       new SimpleColumn("MeasureTable_TwoKeys", "key2", types_->get_int64()),
       new SimpleColumn("MeasureTable_TwoKeys", "Country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_TwoKeys", "Quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_TwoKeys", "Price", types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0, 1},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)"},
       {"measure_count_star_per_key", "COUNT(* GROUP BY key1,key2)"},
       {"measure_count_key_per_key", "COUNT(1 GROUP BY key1,key2)"},
       {"measure_sum_quantity", "SUM(quantity)"},
       {"measure_sum_price", "SUM(price)"},
       {"measure_sum_price_times_quantity", "SUM(price * quantity)"},
       {"measure_ratio_price_to_quantity", "SUM(PRICE) / SUM(QUANTITY)"},
       {"measure_ratio_price_to_quantity_per_key",
        "SUM(ANY_VALUE(price) GROUP BY key1,key2) / "
        "SUM(ANY_VALUE(quantity) GROUP BY key1,key2)"}},
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_DifferentNames",
      {new SimpleColumn("MeasureTable_DifferentNames", "key_different_name",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_DifferentNames", "country_different_name",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_DifferentNames",
                        "quantity_different_name", types_->get_int64()),
       new SimpleColumn("MeasureTable_DifferentNames", "price_different_name",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_count_star_different_name", "COUNT(*)"}},
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(
      AddTableWithMeasures(analyzer_options, "MeasureTable_NoRowIdentity",
                           {new SimpleColumn("MeasureTable_NoRowIdentity",
                                             "key", types_->get_int64())},
                           /*row_identity_column_indices=*/std::nullopt,
                           /*measures=*/{{"measure_count_star", "COUNT(*)"}},
                           /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_NonGroupableRowIdentity",
      {new SimpleColumn("MeasureTable_NonGroupableRowIdentity", "json_col",
                        types_->get_json())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)"}}, /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithPseudoColumns",
      {new SimpleColumn("MeasureTable_WithPseudoColumns", "key",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithPseudoColumns", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithPseudoColumns", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithPseudoColumns", "price",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {
          {"measure_count_star", "COUNT(*)", /*is_pseudo_column=*/true},
          {"measure_count_star_per_key", "COUNT(* GROUP BY key)",
           /*is_pseudo_column=*/true},
      },
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithSubqueryMeasureExprs",
      {new SimpleColumn("MeasureTable_WithSubqueryMeasureExprs", "key",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithSubqueryMeasureExprs", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithSubqueryMeasureExprs", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithSubqueryMeasureExprs", "price",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_with_top_level_subquery",
        "SUM(price) + (SELECT SUM(1) FROM UNNEST([1]))"},
       {"measure_with_simple_subquery_in_aggregate_function",
        "SUM((SELECT price))"},
       {"measure_with_aggregate_subquery_in_aggregate_function",
        "SUM((SELECT SUM(price) FROM UNNEST([1])))"},
       {"measure_with_multiple_subqueries",
        "SUM((SELECT SUM(price) FROM UNNEST([1]))) + (SELECT SUM(1) FROM "
        "UNNEST([1]))"},
       {"measure_with_deeply_nested_subquery",
        "SUM((SELECT x FROM (SELECT (SELECT SUM(price) FROM UNNEST([1])) AS "
        "x)))"},
       {"measure_literal_one", "1", /*is_pseudo_column=*/true},
       {"measure_one_plus_one", "1 + 1", /*is_pseudo_column=*/true},
       {"measure_scalar_subquery",
        "(SELECT SUM(x) FROM UNNEST([1, 2, 3]) AS x)",
        /*is_pseudo_column=*/true}},
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithUdfs",
      {new SimpleColumn("MeasureTable_WithUdfs", "key", types_->get_int64()),
       new SimpleColumn("MeasureTable_WithUdfs", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithUdfs", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithUdfs", "price", types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {
          {"measure_with_top_level_udf", "SUM(price) + NullaryPi()"},
          {"measure_with_udf_in_argument", "SUM(UnaryIncrement(price))"},
          {"measure_with_udf_and_multilevel_aggregation",
           "SUM(ANY_VALUE(UnaryIncrement(price)) group by "
           "UnaryIncrement(key))"},
          {"measure_with_udf_containing_multilevel_aggregation",
           "SUM(COUNT(UnaryIncrement(price)) + NullaryWithMultiLevelAgg() "
           "group by key)"},
      },
      /*is_value_table=*/false));

  // The column `arg` has a name conflict with the function argument of
  // template UDF `TimesTwo(arg)`
  // We deliberately use column name arg to ensure that the templated
  // UDF TimesTwo(arg) resolves arg against the function argument name rather
  // than the name of the column on the table.
  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithTemplatedUdfs",
      {new SimpleColumn("MeasureTable_WithTemplatedUdfs", "key",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithTemplatedUdfs", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithTemplatedUdfs", "arg",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithTemplatedUdfs", "price",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {
          {"measure_with_top_level_udf",
           "SUM(price) + SUM_DOUBLE_ARRAY([1, 2])"},
          {"measure_with_udf_in_argument", "SUM(TimesTwo(price))"},
          {"measure_with_udf_and_multilevel_aggregation",
           "SUM(ANY_VALUE(TimesTwo(price)) group by "
           "TimesTwo(key))"},
          {"measure_with_udf_containing_multilevel_aggregation",
           "SUM(COUNT(TimesTwo(price)) + NullaryWithMultiLevelAgg() "
           "group by key)"},
          {"measure_with_name_conflict_on_arg",
           "SUM(ANY_VALUE(TimesTwo(price + 1)) + COUNT(TimesTwo(price + 2)) "
           "group by key)"},
      },
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "StructValueTable_WithMeasures",
      {new SimpleColumn("StructValueTable_WithMeasures", "value",
                        doubly_nested_struct_type_),
       new SimpleColumn("StructValueTable_WithMeasures", "key",
                        types_->get_int64(), {.is_pseudo_column = true})},
      /*row_identity_column_indices=*/absl::btree_set<int>{1},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)", /*is_pseudo_column=*/true},
       {"measure_sum_e", "SUM(e)", /*is_pseudo_column=*/true},
       {"measure_ratio_sum_a_to_sum_c", "SUM(f.d.a) / SUM(f.c)",
        /*is_pseudo_column=*/true},
       {"measure_sum_key", "SUM(key)", /*is_pseudo_column=*/true}},
      /*is_value_table=*/true));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "KitchenSinkValueTable_WithMeasures",
      {new SimpleColumn("KitchenSinkValueTable_WithMeasures", "value",
                        proto_KitchenSinkPB_),
       new SimpleColumn("KitchenSinkValueTable_WithMeasures", "key",
                        types_->get_int64(), {.is_pseudo_column = true})},
      /*row_identity_column_indices=*/absl::btree_set<int>{1},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)", /*is_pseudo_column=*/true},
       {"measure_sum_int64", "SUM(int64_val)", /*is_pseudo_column=*/true},
       {"measure_ratio_sum_int64_to_sum_int32",
        "SUM(int64_val) / SUM(int32_val)",
        /*is_pseudo_column=*/true},
       {"measure_sum_key", "SUM(key)", /*is_pseudo_column=*/true}},
      /*is_value_table=*/true));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "Int64ValueTable_WithMeasures",
      {new SimpleColumn("Int64ValueTable_WithMeasures", "value",
                        types_->get_int64()),
       new SimpleColumn("Int64ValueTable_WithMeasures", "key",
                        types_->get_int64(), {.is_pseudo_column = true})},
      /*row_identity_column_indices=*/absl::btree_set<int>{1},
      /*measures=*/
      {{"measure_count_star", "COUNT(*)", /*is_pseudo_column=*/true},
       {"measure_sum_key", "SUM(key)", /*is_pseudo_column=*/true},
       {"measure_literal_one", "1", /*is_pseudo_column=*/true},
       {"measure_one_plus_one", "1 + 1", /*is_pseudo_column=*/true},
       {"measure_scalar_subquery",
        "(SELECT SUM(x) FROM UNNEST([1, 2, 3]) AS x)",
        /*is_pseudo_column=*/true}},
      /*is_value_table=*/true));

  // Table containing measure expressions that would ordinarily trigger
  // rewrites. We disable these rewrites during measure expression analysis.
  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithRewritableExprs",
      {new SimpleColumn("MeasureTable_WithRewritableExprs", "key",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithRewritableExprs", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithRewritableExprs", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithRewritableExprs", "price",
                        types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_with_with_expr", "WITH(a AS SUM(price), a + 1)",
        /*is_pseudo_column=*/true},
       {"measure_with_array_remove_last_n",
        "ARRAY_REMOVE_LAST_N([1,2], SUM(price))", /*is_pseudo_column=*/true}},
      /*is_value_table=*/false));

  // Table containing complex measure expressions that had triggered RQG
  // failures in the past.
  const Type* array_int64_type = nullptr;
  GOOGLESQL_CHECK_OK(types_->MakeArrayType(types_->get_int64(), &array_int64_type));
  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_ComplexExprs",
      {new SimpleColumn("MeasureTable_ComplexExprs", "key",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_ComplexExprs", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_ComplexExprs", "array_val",
                        array_int64_type)},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_with_in_subquery",
        "COUNTIF(((key) IN (SELECT scan_alias.a_2 AS renamed FROM (SELECT "
        "key "
        "AS a_1, key AS a_2) AS scan_alias)))"},
       {"measure_with_lambda_containing_with_expr",
        "SUM(ARRAY_FILTER([1, 2, 3], e -> WITH(a1 AS array_val, a2 AS key, "
        "true))[OFFSET(0)])"},
       {"measure_sum_one", "SUM(1)"},
       {"measure_string_agg", "STRING_AGG(country, ',')"},
       {"measure_bit_and", "BIT_AND(CAST(country AS BYTES), mode => 'PAD')"},
       {"measure_aggregation_in_in_expr", "SUM(key) IN ((SELECT 1))"}},
      /*is_value_table=*/false));

  GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
      analyzer_options, "MeasureTable_WithUdas",
      {new SimpleColumn("MeasureTable_WithUdas", "key", types_->get_int64()),
       new SimpleColumn("MeasureTable_WithUdas", "country",
                        types_->get_string()),
       new SimpleColumn("MeasureTable_WithUdas", "quantity",
                        types_->get_int64()),
       new SimpleColumn("MeasureTable_WithUdas", "price", types_->get_int64())},
      /*row_identity_column_indices=*/absl::btree_set<int>{0},
      /*measures=*/
      {{"measure_uda_sum_price", "SumOfAggregateArgs(price)"},
       {"measure_uda_sum_price_per_key",
        "SumOfAggregateArgs(ANY_VALUE(price) GROUP BY key)"},
       {"measure_uda_sum_max_price_per_country",
        "SumOfAggregateArgs(MaxOfAggregateArgs(price) GROUP BY country)"}},
      /*is_value_table=*/false));

  // A Measure table the represents a "join" of two fact tables.
  //
  // `price` is uniquely identified by `item_id`.
  // `quantity` is uniquely identified by `store_id`.
  {
    const std::string table_name = "MeasureTable_SalesFacts";
    GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
        analyzer_options, table_name,
        {new SimpleColumn(table_name, "item_id", types_->get_int64()),
         new SimpleColumn(table_name, "store_id", types_->get_int64()),
         new SimpleColumn(table_name, "price", types_->get_int64()),
         new SimpleColumn(table_name, "quantity", types_->get_int64())},
        /*row_identity_column_indices=*/absl::btree_set<int>{0, 1},
        /*measures=*/
        {{
             .name = "measure_total_price",
             .expression = "SUM(price)",
             .is_pseudo_column = false,
             .row_identity_column_indices = {{0}},
         },
         {
             .name = "measure_total_quantity",
             .expression = "SUM(quantity)",
             .is_pseudo_column = false,
             .row_identity_column_indices = {{1}},
         },
         {
             .name = "measure_price_to_quantity_ratio",
             .expression = "SUM(price)/SUM(quantity)",
             .is_pseudo_column = false,
             // Defaults to the table's row identity columns.
             .row_identity_column_indices = std::nullopt,
         }},
        /*is_value_table=*/false));
  }

  {
    // The table does not have row identity columns specified, but each measure
    // column does, so it is still valid.
    const std::string table_name = "MeasureTable_NoTableKey";
    GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
        analyzer_options, table_name,
        {new SimpleColumn(table_name, "item_id", types_->get_int64()),
         new SimpleColumn(table_name, "store_id", types_->get_int64()),
         new SimpleColumn(table_name, "price", types_->get_int64()),
         new SimpleColumn(table_name, "quantity", types_->get_int64())},
        /*row_identity_column_indices=*/std::nullopt,
        /*measures=*/
        {{
             .name = "measure_total_price",
             .expression = "SUM(price)",
             .is_pseudo_column = false,
             .row_identity_column_indices = std::vector<int>{0},
         },
         {
             .name = "measure_total_quantity",
             .expression = "SUM(quantity)",
             .is_pseudo_column = false,
             .row_identity_column_indices = std::vector<int>{1},
         },
         {
             .name = "measure_price_to_quantity_ratio",
             .expression = "SUM(price)/SUM(quantity)",
             .is_pseudo_column = false,
             .row_identity_column_indices = std::vector<int>{0, 1},
         }},
        /*is_value_table=*/false));
  }

  // Invalid measure TVF: The underlying table has a measure column whose row
  // identity column is empty.
  {
    const std::string table_name = "MeasureTable_ColumnEmptyRowIdentities";
    GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
        analyzer_options, table_name,
        {new SimpleColumn(table_name, "key", types_->get_int64())},
        /*row_identity_column_indices=*/absl::btree_set<int>{0},
        /*measures=*/
        {{
            .name = "measure_count",
            .expression = "COUNT(*)",
            .is_pseudo_column = false,
            .row_identity_column_indices = std::vector<int>{},
        }},
        /*is_value_table=*/false));
  }

  // Invalid measure TVF: The underlying table has a measure column with
  // out-of-bound column-level row identity column indices.
  {
    const std::string table_name = "MeasureTable_ColumnInvalidRowIdentityIndex";
    GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
        analyzer_options, table_name,
        {new SimpleColumn(table_name, "key", types_->get_int64())},
        /*row_identity_column_indices=*/absl::btree_set<int>{0},
        /*measures=*/
        {{
            .name = "measure_count",
            .expression = "COUNT(*)",
            .is_pseudo_column = false,
            .row_identity_column_indices = std::vector<int>{100},
        }},
        /*is_value_table=*/false));
  }

  // Invalid measure TVF: The underlying table has a measure column with
  // non-groupable column-level row identity columns.
  {
    const std::string table_name = "MeasureTable_ColumnNonGroupableRowIdentity";
    GOOGLESQL_RETURN_IF_ERROR(AddTableWithMeasures(
        analyzer_options, table_name,
        {new SimpleColumn(table_name, "json_col", types_->get_json())},
        /*row_identity_column_indices=*/absl::btree_set<int>{0},
        /*measures=*/
        {{
            .name = "measure_count",
            .expression = "COUNT(*)",
            .is_pseudo_column = false,
            .row_identity_column_indices = std::vector<int>{0},
        }},
        /*is_value_table=*/false));
  }

  return absl::OkStatus();
}

void SampleCatalogImpl::LoadProtoTables() {
  // Add a named struct type.
  const StructType* struct_TestStruct;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"Key", types_->get_int64()}, {"Value", types_->get_string()}},
      &struct_TestStruct));
  catalog_->AddType("TestStruct", struct_TestStruct);

  const StructType* struct_AnotherTestStruct;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"K", types_->get_int32()}, {"v", types_->get_bytes()}},
      &struct_AnotherTestStruct));
  catalog_->AddType("AnotherTestStruct", struct_TestStruct);

  AddOwnedTable(
      new SimpleTable("TestTable", {{"key", types_->get_int32()},
                                    {"TestEnum", enum_TestEnum_},
                                    {"KitchenSink", proto_KitchenSinkPB_}}));

  // We want to be sure that AmbiguousHasTestTable is not the first table
  // serialized by SimpleCatalog::Serialize. See b/125914476 for more detail.
  AddOwnedTable(new SimpleTable(
      "ZZZ_AmbiguousHasTestTable",
      {{"key", types_->get_int32()}, {"AmbiguousHas", proto_ambiguous_has_}}));

  AddOwnedTable(
      new SimpleTable("CivilTimeTestTable",
                      {{"key", types_->get_int32()},
                       {"CivilTimeTypesSink", proto_CivilTimeTypesSinkPB_}}));

  AddOwnedTable(
      new SimpleTable("FieldFormatsTable",
                      {{"key", types_->get_int32()},
                       {"FieldFormatsProto", proto_field_formats_proto_}}));

  AddOwnedTable(
      new SimpleTable("ApproxDistanceFunctionOptionsProtoTable",
                      {{"key", types_->get_int32()},
                       {"options", proto_approx_distance_function_options_}}));

  // EnumTable has two pseudo-columns Filename and RowId.
  AddOwnedTable(new SimpleTable(
      "EnumTable",
      {new SimpleColumn("EnumTable", "key", types_->get_int32()),
       new SimpleColumn("EnumTable", "TestEnum", enum_TestEnum_),
       new SimpleColumn("EnumTable", "AnotherTestEnum", enum_AnotherTestEnum_),
       new SimpleColumn("EnumTable", "Filename", types_->get_string(),
                        {.is_pseudo_column = true}),
       new SimpleColumn("EnumTable", "RowId", types_->get_bytes(),
                        {.is_pseudo_column = true})},
      true /* take_ownership */));

  AddOwnedTable(new SimpleTable(
      "MapFieldTable", {{"key", types_->get_int32()},
                        {"MessageWithMapField", proto_MessageWithMapField_}}));

  AddOwnedTable(new SimpleTable(
      "Proto3Table", {{"key", types_->get_int32()},
                      {"Proto3KitchenSink", proto3_KitchenSinkPB_}}));

  AddOwnedTable(new SimpleTable(
      "Proto3InvalidMapTable",
      {{"key", types_->get_int32()},
       {"MessageWithInvalidMap", proto3_MessageWithInvalidMap_}}));

  // This table only has pseudo-columns.
  AddOwnedTable(new SimpleTable(
      "AllPseudoColumns",
      {
          new SimpleColumn("AllPseudoColumns", "Key", types_->get_int32(),
                           {.is_pseudo_column = true}),
          new SimpleColumn("AllPseudoColumns", "Value", types_->get_string(),
                           {.is_pseudo_column = true}),
      },
      true /* take_ownership */));

  // Another table with only pseudo-columns, this time with a repeated field. We
  // don't extend AllPseudoColumns to avoid breaking pre-existing tests.
  AddOwnedTable(new SimpleTable(
      "AllPseudoColumnsWithRepeated",
      {
          new SimpleColumn("AllPseudoColumns", "Key", types_->get_int32(),
                           {.is_pseudo_column = true}),
          new SimpleColumn("AllPseudoColumns", "Value", types_->get_string(),
                           {.is_pseudo_column = true}),
          new SimpleColumn("AllPseudoColumns", "RepeatedValue",
                           string_array_type_, {.is_pseudo_column = true}),
      },
      true /* take_ownership */));

  {
    // This table has an anonymous pseudo-column, which should be inaccessible.
    auto table = new SimpleTable("AnonymousPseudoColumn");
    AddOwnedTable(table);
    GOOGLESQL_CHECK_OK(table->set_allow_anonymous_column_name(true));
    GOOGLESQL_CHECK_OK(table->AddColumn(
        new SimpleColumn("AnonymousPseudoColumn", "key", types_->get_int32()),
        true /* take_ownership */));
    GOOGLESQL_CHECK_OK(table->AddColumn(
        new SimpleColumn("AnonymousPseudoColumn", "", types_->get_string(),
                         {.is_pseudo_column = true}),
        true /* take_ownership */));
  }

  {
    // This table has two duplicate columns.
    auto table = new SimpleTable("DuplicateColumns");
    GOOGLESQL_CHECK_OK(table->set_allow_duplicate_column_names(true));
    AddOwnedTable(table);
    GOOGLESQL_CHECK_OK(
        table->AddColumn(new SimpleColumn("DuplicateColumns", "DuplicateColumn",
                                          types_->get_int32()),
                         /*is_owned=*/true));
    GOOGLESQL_CHECK_OK(
        table->AddColumn(new SimpleColumn("DuplicateColumns", "DuplicateColumn",
                                          types_->get_string()),
                         /*is_owned=*/true));
  }

  AddOwnedTable(new SimpleTable(
      "AllNonKeysNonWritable",
      {
          new SimpleColumn("AllNonKeysNonWritable", "Key", types_->get_int32(),
                           {.is_writable_column = true}),
          new SimpleColumn("AllNonKeysNonWritable", "Value",
                           types_->get_string(), {.is_writable_column = false}),
          new SimpleColumn("AllNonKeysNonWritable", "RepeatedValue",
                           int32array_type_, {.is_writable_column = false}),
          new SimpleColumn("AllNonKeysNonWritable", "ProtoValue",
                           proto_TestExtraPB_, {.is_writable_column = false}),
          new SimpleColumn("AllNonKeysNonWritable", "StructValue", struct_type_,
                           {.is_writable_column = false}),
      },
      true /* take_ownership */));

  SimpleTable* complex_types =
      new SimpleTable("ComplexTypes", {{"key", types_->get_int32()},
                                       {"TestEnum", enum_TestEnum_},
                                       {"KitchenSink", proto_KitchenSinkPB_},
                                       {"Int32Array", int32array_type_},
                                       {"TestStruct", nested_struct_type_},
                                       {"TestProto", proto_TestExtraPB_}});
  GOOGLESQL_CHECK_OK(complex_types->SetPrimaryKey({0}));
  AddOwnedTable(complex_types);

  AddOwnedTable(new SimpleTable(
      "MoreComplexTypes",
      {{"key", types_->get_int32()},
       {"ArrayOfStruct", struct_array_type_},
       {"StructOfArrayOfStruct", struct_with_array_field_type_}}));

  AddOwnedTable(new SimpleTable(
      "StructWithKitchenSinkTable",
      {{"kitchen_sink", proto_KitchenSinkPB_},
       {"s", struct_with_kitchen_sink_type_},
       {"t", struct_of_array_of_struct_with_kitchen_sink_type_}}));

  AddOwnedTable(
      new SimpleTable("DoublyNestedStructTable",
                      {{"key", types_->get_int32()},
                       {"doubly_nested_struct", doubly_nested_struct_type_}}));

  AddOwnedTable(new SimpleTable("KitchenSinkValueTable", proto_KitchenSinkPB_));

  AddOwnedTable(new SimpleTable("MessageWithKitchenSinkValueTable",
                                proto_MessageWithKitchenSinkPB_));

  AddOwnedTable(new SimpleTable("EmptyMessageValueTable", proto_EmptyMessage_));

  catalog_->AddOwnedTable(
      new SimpleTable("TestExtraPBValueTable", proto_TestExtraPB_));

  catalog_->AddOwnedTable(new SimpleTable("TestAbPBValueTable", proto_abPB_));

  catalog_->AddOwnedTable(new SimpleTable("TestBcPBValueTable", proto_bcPB_));

  catalog_->AddOwnedTable(new SimpleTable("TestBcPBValueProtoTable",
                                          {{"value", proto_bcPB_},
                                           {"Filename", types_->get_string()},
                                           {"RowId", types_->get_int64()}}));

  // TestExtraValueTable also has pseudo-columns Filename and RowID.
  SimpleTable* extra_value_table;
  AddOwnedTable((
      extra_value_table = new SimpleTable(
          "TestExtraValueTable",
          {new SimpleColumn("TestExtraValueTable", "value", proto_TestExtraPB_),
           new SimpleColumn("TestExtraValueTable", "Filename",
                            types_->get_string(), {.is_pseudo_column = true}),
           new SimpleColumn("TestExtraValueTable", "RowId", types_->get_bytes(),
                            {.is_pseudo_column = true})},
          true /* take_ownership */)));
  extra_value_table->set_is_value_table(true);

  // AmbiguousFieldValueTable has a pseudo-column int32_val1 that is
  // also a field name.
  SimpleTable* ambiguous_field_value_table;
  AddOwnedTable((ambiguous_field_value_table = new SimpleTable(
                     "AmbiguousFieldValueTable",
                     {
                         new SimpleColumn("TestExtraValueTable", "value",
                                          proto_TestExtraPB_),
                         new SimpleColumn("TestExtraValueTable", "int32_val1",
                                          types_->get_string(),
                                          {.is_pseudo_column = true}),
                     },
                     true /* take_ownership */)));
  ambiguous_field_value_table->set_is_value_table(true);

  SimpleTable* int64_value_table;
  AddOwnedTable(int64_value_table = new SimpleTable(
                    "Int64ValueTable",
                    {new SimpleColumn("Int64ValueTable", "IntValue",
                                      types_->get_int64())},
                    /*take_ownership=*/true));
  int64_value_table->set_is_value_table(true);
  GOOGLESQL_CHECK_OK(int64_value_table->SetPrimaryKey({0}));

  AddOwnedTable(new SimpleTable(
      "ArrayTypes",
      {{"Int32Array", int32array_type_},
       {"Int64Array", int64array_type_},
       {"UInt32Array", uint32array_type_},
       {"UInt64Array", uint64array_type_},
       {"StringArray", string_array_type_},
       {"BytesArray", bytes_array_type_},
       {"BoolArray", bool_array_type_},
       {"FloatArray", float_array_type_},
       {"DoubleArray", double_array_type_},
       {"DateArray", date_array_type_},
       // These corresponding legacy types were removed, but we keep the fields
       // in the sample table in order not to disturb analyzer test results too
       // much (all the variable ids would change if we were to remove them).
       // TODO: Eventually remove these.
       {"TimestampSecondsArray", timestamp_array_type_},
       {"TimestampMillisArray", timestamp_array_type_},
       {"TimestampMicrosArray", timestamp_array_type_},
       // Real types resume here.
       {"TimestampArray", timestamp_array_type_},
       {"ProtoArray", proto_array_type_},
       {"StructArray", struct_array_type_},
       {"JsonArray", json_array_type_},
       {"NumericArray", numeric_array_type_},
       {"BigNumericArray", bignumeric_array_type_},
       {"IntervalArray", interval_array_type_}}));

  const EnumType* enum_TestEnum =
      GetEnumType(googlesql_test::TestEnum_descriptor());
  AddOwnedTable(new SimpleTable("SimpleTypesWithStruct",
                                {{"key", types_->get_int32()},
                                 {"TestEnum", enum_TestEnum},
                                 {"TestStruct", nested_struct_type_}}));

  const ProtoType* proto_recursive_type =
      GetProtoType(googlesql_test::RecursivePB::descriptor());
  AddOwnedTable(new SimpleTable("RecursivePBTable",
                                {{"RecursivePB", proto_recursive_type}}));

  AddOwnedTable(new SimpleTable(
      "KeywordTable",
      {{"current_date", types_->get_date()},
       {"current_timestamp", types_->get_timestamp()},
       // The corresponding legacy types were removed, but we keep the fields in
       // the sample table in order not to disturb analyzer test results too
       // much (all the variable ids would change if we were to remove them).
       // TODO: Eventually remove these.
       {"current_timestamp_seconds", types_->get_timestamp()},
       {"current_timestamp_millis", types_->get_timestamp()},
       {"current_timestamp_micros", types_->get_timestamp()}}));

  AddOwnedTable(new SimpleTable("TestStructValueTable", struct_type_));

  AddOwnedTable(new SimpleTable("TestNestedStructValueTable",
                                doubly_nested_struct_type_));

  AddOwnedTable(new SimpleTable("StructWithOneFieldValueTable",
                                struct_with_one_field_type_));

  AddOwnedTable(new SimpleTable("Int32ValueTable", types_->get_int32()));

  AddOwnedTable(new SimpleTable("Int32ArrayValueTable", int32array_type_));

  AddOwnedTable(
      new SimpleTable("AnnotatedEnumTable", enum_TestEnumWithAnnotations_));
}

void SampleCatalogImpl::LoadViews(const LanguageOptions& language_options) {
  // Ensure the language options used allow CREATE FUNCTION
  LanguageOptions language = language_options;
  language.AddSupportedStatementKind(RESOLVED_CREATE_VIEW_STMT);
  language.EnableLanguageFeature(FEATURE_CREATE_VIEW_WITH_COLUMN_LIST);
  AnalyzerOptions analyzer_options;
  analyzer_options.set_language(language);

  auto add_view = [&analyzer_options, this](absl::string_view create_view) {
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_CHECK_OK(AddViewFromCreateView(create_view, analyzer_options,
                                   /*allow_non_temp=*/true, analyzer_output,
                                   *catalog_));
    sql_object_artifacts_.emplace_back(std::move(analyzer_output));
  };
  add_view("CREATE VIEW TwoIntsView SQL SECURITY INVOKER AS SELECT 1 a, 2 b;");
  add_view(
      "CREATE VIEW UnprojectedColumnView SQL SECURITY INVOKER AS "
      "SELECT a, b FROM (SELECT 1 AS a, 2 AS b, 3 AS c);");
  add_view(
      "CREATE VIEW ColumnListView(a, b) SQL SECURITY INVOKER AS "
      "SELECT 1, 2;");
  add_view(
      "CREATE VIEW CteView SQL SECURITY INVOKER AS "
      "WITH t AS (SELECT 1 a, 2 b) SELECT * FROM t;");
  add_view(
      "CREATE VIEW OneStructView SQL SECURITY INVOKER AS "
      "SELECT STRUCT(1 AS a, 2 AS b) AS ab;");
  add_view(
      "CREATE VIEW AsStructView SQL SECURITY INVOKER AS "
      "SELECT AS STRUCT 1 a, 2 b;");
  add_view(
      "CREATE VIEW OneScalarView SQL SECURITY INVOKER AS "
      "SELECT '123' AS ab");
  add_view(
      "CREATE VIEW AsScalarView SQL SECURITY INVOKER AS "
      "SELECT AS VALUE '123'");
  add_view(
      "CREATE VIEW ScanTableView SQL SECURITY INVOKER AS "
      "SELECT key AS a, value AS b FROM TwoIntegers;");
  add_view(
      "CREATE VIEW ScanViewView SQL SECURITY INVOKER AS "
      "SELECT a, 'b' AS b FROM ScanTableView;");
  add_view(
      "CREATE VIEW UnspecifiedRightsView SQL SECURITY DEFINER AS "
      "SELECT 1 AS a;");
  add_view(
      "CREATE VIEW DefinerRightsView SQL SECURITY DEFINER AS "
      "SELECT 1 AS a, 'x' AS b, false AS c;");
}

void SampleCatalogImpl::LoadNestedCatalogs() {
  SimpleCatalog* nested_catalog =
      catalog_->MakeOwnedSimpleCatalog("nested_catalog");

  // Add nested_catalog with some tables with the same and different names.
  nested_catalog->AddTable(key_value_table_);
  nested_catalog->AddTable("NestedKeyValue", key_value_table_);

  {
    // Add a table that only appears in this nested catalog (and in turn, can
    // only be found via the nested name).
    SimpleTable* nested_key_value_table = new SimpleTable(
        "KeyValueNested",
        {{"Key", types_->get_int64()}, {"Value", types_->get_string()}});
    GOOGLESQL_CHECK_OK(
        nested_key_value_table->set_full_name("nested_catalog.KeyValueNested"));
    nested_catalog->AddOwnedTable(nested_key_value_table);
  }

  // Add nested_catalog with some connections
  nested_catalog->AddConnection(
      owned_connections_.find("connection1")->second.get());
  nested_catalog->AddConnection(
      owned_connections_.find("connection2")->second.get());
  nested_catalog->AddConnection(
      owned_connections_.find("NestedConnection")->second.get());

  // Add recursive_catalog which points back to the same catalog.
  // This allows resolving names like
  //   recursive_catalog.recursive_catalog.recursive_catalog.TestTable
  catalog_->AddCatalog("recursive_catalog", catalog_.get());

  // Add a function to the nested catalog:
  //   nested_catalog.nested_function(<int64>) -> <int64>
  FunctionSignature signature(
      {types_->get_int64(), {types_->get_int64()}, /*context_id=*/-1});
  std::vector<std::string> function_name_path = {"nested_catalog",
                                                 "nested_function"};
  Function* function = new Function(function_name_path, "sample_functions",
                                    Function::SCALAR, {signature});
  nested_catalog->AddOwnedFunction(function);

  // A scalar function with argument alias support in the nested catalog.
  {
    FunctionArgumentType aliased(
        ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_ALIASED));
    FunctionArgumentType non_aliased(ARG_TYPE_ANY_2);
    std::vector<FunctionSignature> signatures = {
        {types_->get_int64(), {aliased, non_aliased}, /*context_id=*/-1}};
    Function* function = new Function(
        std::vector<std::string>{"nested_catalog", "fn_for_argument_alias"},
        "sample_functions", Function::SCALAR, signatures);
    nested_catalog->AddOwnedFunction(function);
  }
  // Add a procedure to the nested catalog:
  //   nested_catalog.nested_procedure(<int64>) -> <int64>
  Procedure* procedure =
      new Procedure({"nested_catalog", "nested_procedure"}, signature);
  nested_catalog->AddOwnedProcedure(procedure);

  // Add a doubly nested catalog, and a function to the doubly nested catalog:
  //   nested_catalog.nested_nested_catalog.nested_function(<int64>) -> <int64>
  SimpleCatalog* nested_nested_catalog =
      nested_catalog->MakeOwnedSimpleCatalog("nested_nested_catalog");
  function_name_path = {"nested_catalog", "nested_nested_catalog",
                        "nested_function"};
  function = new Function(function_name_path, "sample_functions",
                          Function::SCALAR, {signature});
  nested_nested_catalog->AddOwnedFunction(function);

  // Add table "nested" to the nested catalog and doubly nested catalog
  // with the same name "nested":
  //   nested_catalog.nested
  //   nested_catalog.nested.nested
  nested_catalog->AddTable("nested", key_value_table_);
  SimpleCatalog* duplicate_name_nested_catalog =
      nested_catalog->MakeOwnedSimpleCatalog("nested");
  duplicate_name_nested_catalog->AddTable("nested", key_value_table_);

  // Add a struct-typed constant to the doubly nested catalog.
  const StructType* nested_constant_struct_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"eee", types_->get_int32()}, {"fff", nested_struct_type_}},
      &nested_constant_struct_type));
  std::unique_ptr<SimpleConstant> constant_struct;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"nested_catalog", "nested_nested_catalog",
                               "TestConstantStruct"},
      Value::Struct(nested_constant_struct_type,
                    {Value::Int32(-3456),
                     Value::Struct(nested_struct_type_,
                                   {Value::Int32(3),
                                    Value::Struct(struct_type_,
                                                  {Value::Int32(223),
                                                   Value::String("foo")})})}),
      &constant_struct));
  nested_nested_catalog->AddOwnedConstant(constant_struct.release());

  // Add an enum and a proto to the nested catalog.
  nested_catalog->AddType(enum_TestEnum_->enum_descriptor()->full_name(),
                          enum_TestEnum_);
  nested_catalog->AddType(proto_KitchenSinkPB_->descriptor()->full_name(),
                          proto_KitchenSinkPB_);
  nested_catalog->AddType(
      proto_CivilTimeTypesSinkPB_->descriptor()->full_name(),
      proto_CivilTimeTypesSinkPB_);

  // Add TVFs to the nested catalogs. We use this to test name resolution during
  // serialization/deserialization.
  const std::string kColumnNameKey = "key";
  TVFRelation::ColumnList columns;
  columns.emplace_back(kColumnNameKey, types::Int64Type());
  TVFRelation single_key_col_schema(columns);

  int context_id = -1;
  nested_catalog->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"nested_catalog", "nested_tvf_one"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              single_key_col_schema,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameKey, types::Int64Type()}}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id)},
      single_key_col_schema));
  nested_nested_catalog->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"nested_catalog", "nested_nested_catalog", "nested_tvf_two"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              single_key_col_schema,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameKey, types::Int64Type()}}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id)},
      single_key_col_schema));

  // Load a nested catalog with a constant whose names conflict with a table
  // and its field.
  SimpleCatalog* name_conflict_catalog =
      catalog_->MakeOwnedSimpleCatalog("name_conflict_table");
  std::unique_ptr<SimpleConstant> constant;
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create({"name_conflict_table", "name_conflict_field"},
                             Value::Bool(false), &constant));
  name_conflict_catalog->AddOwnedConstant(constant.release());

  // Add <nested_catalog_with_constant> for testing named constants in catalogs.
  SimpleCatalog* nested_catalog_with_constant =
      catalog_->MakeOwnedSimpleCatalog("nested_catalog_with_constant");
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create({"nested_catalog_with_constant", "KnownConstant"},
                             Value::Bool(false), &constant));
  nested_catalog_with_constant->AddOwnedConstant(constant.release());

  // Add <nested_catalog_with_catalog> for testing conflicts with named
  // constants.
  SimpleCatalog* nested_catalog_with_catalog =
      catalog_->MakeOwnedSimpleCatalog("nested_catalog_with_catalog");
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      {"nested_catalog_with_catalog", "TestConstantBool"}, Value::Bool(false),
      &constant));
  nested_catalog_with_catalog->AddOwnedConstant(constant.release());
  GOOGLESQL_CHECK_OK(SimpleConstant::Create({"nested_catalog_with_catalog", "c"},
                                  Value::Double(-9999.999), &constant));
  nested_catalog_with_catalog->AddOwnedConstant(constant.release());
  SimpleCatalog* nested_catalog_catalog =
      nested_catalog_with_catalog->MakeOwnedSimpleCatalog(
          "nested_catalog_catalog");
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      {"nested_catalog_with_catalog", "nested_catalog_catalog", "a"},
      Value::Float(-1.4987f), &constant));
  nested_catalog_catalog->AddOwnedConstant(constant.release());
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      {"nested_catalog_with_catalog", "nested_catalog_catalog", "c"},
      Value::String("foo"), &constant));
  nested_catalog_catalog->AddOwnedConstant(constant.release());

  // Add a constant to <nested_catalog>.
  GOOGLESQL_CHECK_OK(SimpleConstant::Create({"nested_catalog", "TestConstantBool"},
                                  Value::Bool(false), &constant));
  nested_catalog->AddOwnedConstant(constant.release());

  // Add another constant to <nested_catalog> that conflicts with a procedure.
  GOOGLESQL_CHECK_OK(SimpleConstant::Create({"nested_catalog", "nested_procedure"},
                                  Value::Int64(2345), &constant));
  nested_catalog->AddOwnedConstant(constant.release());

  // Add a constant to <nested_catalog> which requires backticks.
  std::unique_ptr<SimpleConstant> string_constant_nonstandard_name;

  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"nested_catalog", "Test Constant-String"},
      Value::String("Test constant in nested catalog"),
      &string_constant_nonstandard_name));
  nested_catalog->AddOwnedConstant(string_constant_nonstandard_name.release());

  // Add struct constant with the same name as a nested catalog
  const StructType* nested_nested_catalog_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType({{"xxxx", types_->get_int64()}},
                                  &nested_nested_catalog_type));

  SimpleCatalog* wwww_catalog = nested_catalog->MakeOwnedSimpleCatalog("wwww");

  std::unique_ptr<SimpleConstant> wwww_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"nested_catalog", "wwww"},
      Value::Struct(nested_nested_catalog_type, {Value::Int64(8)}),
      &wwww_constant));
  nested_catalog->AddOwnedConstant(wwww_constant.release());

  std::unique_ptr<SimpleConstant> xxxx_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"nested_catalog", "wwww", "xxxx"},
      Value::Struct(nested_nested_catalog_type, {Value::Int64(8)}),
      &xxxx_constant));
  wwww_catalog->AddOwnedConstant(xxxx_constant.release());

  // Load a nested catalog with a name that resembles a system variable.
  SimpleCatalog* at_at_nested_catalog =
      catalog_->MakeOwnedSimpleCatalog("@@nested_catalog");
  std::unique_ptr<SimpleConstant> at_at_nested_catalog_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"@@nested_catalog", "sysvar2"}, Value::Int64(8),
      &at_at_nested_catalog_constant));
  at_at_nested_catalog->AddOwnedConstant(
      at_at_nested_catalog_constant.release());

  {
    std::unique_ptr<SimpleConstant> rounding_mode_constant;
    GOOGLESQL_CHECK_OK(SimpleConstant::Create(
        std::vector<std::string>{"nested_catalog", "constant_rounding_mode"},
        Value::Enum(types::RoundingModeEnumType(), "ROUND_HALF_EVEN"),
        &rounding_mode_constant));
    nested_catalog->AddOwnedConstant(std::move(rounding_mode_constant));
  }

  auto udf_catalog = nested_catalog->MakeOwnedSimpleCatalog("udf");
  function =
      new Function("timestamp_add", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_int64(),
       {{types_->get_int64(), FunctionArgumentType::REQUIRED},
        {types_->get_int64(), FunctionArgumentType::REQUIRED},
        {types_->get_int64(), FunctionArgumentType::REQUIRED}},
       /*context_id=*/-1});
  udf_catalog->AddOwnedFunction(function);
}

static FreestandingDeprecationWarning CreateDeprecationWarning(
    int id,
    DeprecationWarning_Kind kind = DeprecationWarning::PROTO3_FIELD_PRESENCE) {
  FreestandingDeprecationWarning warning;
  const std::string foo_id = absl::StrCat("foo_", id);
  warning.set_message(absl::StrCat("Operation <foo", id, "> is deprecated"));
  warning.set_caret_string(absl::StrCat("some caret string for ", foo_id, "\n",
                                        "                      ^"));
  warning.mutable_deprecation_warning()->set_kind(kind);

  ErrorLocation* warning_location = warning.mutable_error_location();
  warning_location->set_line(10 + id);
  warning_location->set_column(20 + id);
  warning_location->set_filename(absl::StrCat("module", id, ".sqlm"));

  return warning;
}

void SampleCatalogImpl::AddFunctionWithArgumentType(std::string type_name,
                                                    const Type* arg_type) {
  auto function = std::make_unique<Function>(
      absl::StrCat("fn_on_", type_name), "sample_functions", Function::SCALAR);
  function->AddSignature({types_->get_bool(), {arg_type}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(std::move(function));
}

absl::Status SampleCatalogImpl::LoadExtendedSubscriptFunctions(
    const LanguageOptions& language_options) {
  // Add new signatures for '$subscript_with_offset' so we can do some
  // additional testing.  The signatures are:
  // 1) <string>[OFFSET(<int64>)]:
  //    $subscript_with_offset(string, int64) -> (string)
  // 2) <string>[OFFSET(<string>)]:
  //    $subscript_with_offset(string, string) -> (string)
  const Function* subscript_offset_function;
  GOOGLESQL_RET_CHECK_OK(catalog_->GetFunction("$subscript_with_offset",
                                     &subscript_offset_function));
  GOOGLESQL_RET_CHECK(subscript_offset_function != nullptr);
  // If we ever update the builtin function implementation to actually include
  // a signature, then take a look at this code to see if it is still needed.
  GOOGLESQL_RET_CHECK_EQ(subscript_offset_function->NumSignatures(), 0);
  Function* mutable_subscript_offset_function =
      const_cast<Function*>(subscript_offset_function);

  FunctionSignature int64_on_string{types_->get_string(),
                                    {types_->get_string(), types_->get_int64()},
                                    /*context_id=*/-1};
  FunctionSignature string_on_string{
      types_->get_string(),
      {types_->get_string(), types_->get_string()},
      /*context_id=*/-1};

  mutable_subscript_offset_function->AddSignature(int64_on_string);
  mutable_subscript_offset_function->AddSignature(string_on_string);

  if (language_options.LanguageFeatureEnabled(FEATURE_SAFE_FUNCTION_CALL)) {
    // Same for the SAFE version:
    const Function* safe_subscript_offset_function;
    GOOGLESQL_RET_CHECK_OK(catalog_->GetFunction("$safe_subscript_with_offset",
                                       &safe_subscript_offset_function));
    GOOGLESQL_RET_CHECK(safe_subscript_offset_function != nullptr);
    // If we ever update the builtin function implementation to actually include
    // a signature, then take a look at this code to see if it is still needed.
    GOOGLESQL_RET_CHECK_EQ(safe_subscript_offset_function->NumSignatures(), 0);
    Function* mutable_safe_subscript_offset_function =
        const_cast<Function*>(safe_subscript_offset_function);

    mutable_safe_subscript_offset_function->AddSignature(int64_on_string);
    mutable_safe_subscript_offset_function->AddSignature(string_on_string);
  }
  return absl::OkStatus();
}

const Function* SampleCatalogImpl::AddFunction(
    absl::string_view name, Function::Mode mode,
    std::vector<FunctionSignature> function_signatures,
    FunctionOptions function_options) {
  for (const FunctionSignature& sig : function_signatures) {
    GOOGLESQL_CHECK_OK(sig.IsValid(PRODUCT_INTERNAL));
    GOOGLESQL_CHECK_OK(sig.IsValid(PRODUCT_EXTERNAL));
  }
  auto function = std::make_unique<Function>(name, "sample_functions", mode,
                                             std::move(function_signatures),
                                             std::move(function_options));
  const Function* function_ptr = function.get();
  catalog_->AddOwnedFunction(std::move(function));
  return function_ptr;
}

namespace {

ABSL_CONST_INIT static absl::Mutex catalog_registry_mu(absl::kConstInit);
ABSL_CONST_INIT static bool catalog_registry_closed = false;

std::vector<absl::FunctionRef<void(googlesql::SimpleCatalog* catalog)>>&
CatalogRegistry() {
  static absl::NoDestructor<
      std::vector<absl::FunctionRef<void(googlesql::SimpleCatalog * catalog)>>>
      registry;
  return *registry;
}

struct RegisterForSampleCatalog {
  explicit RegisterForSampleCatalog(
      absl::FunctionRef<void(googlesql::SimpleCatalog* catalog)> fn) {
    absl::MutexLock lock(&catalog_registry_mu);
    ABSL_CHECK(!catalog_registry_closed)
        << "Cannot add new registrations functions for the sample catalog "
           "after one was already created.";
    CatalogRegistry().push_back(fn);
  }
};
}  // namespace

void SampleCatalogImpl::LoadAllRegisteredCatalogChanges() {
  if (!catalog_registry_closed) {
    absl::MutexLock lock(&catalog_registry_mu);
    catalog_registry_closed = true;
  }
  for (auto const& fn : CatalogRegistry()) {
    fn(this->catalog_.get());
  }
}

namespace {

RegisterForSampleCatalog test_function =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      // Add a function to illustrate how repeated/optional arguments are
      // resolved.
      Function* function =
          new Function("test_function", "sample_functions", Function::SCALAR);
      function->AddSignature(
          {types_->get_int64(),
           {{types_->get_int64(), FunctionArgumentType::REQUIRED},
            {types_->get_int64(), FunctionArgumentType::REPEATED},
            {types_->get_int64(), FunctionArgumentType::REPEATED},
            {types_->get_int64(), FunctionArgumentType::REQUIRED},
            {types_->get_int64(), FunctionArgumentType::OPTIONAL}},
           /*context_id=*/-1});
      catalog_->AddOwnedFunction(function);
    });

RegisterForSampleCatalog volatile_function =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      Function* function = new Function(
          "volatile_function", "sample_functions", Function::SCALAR,
          {{types_->get_int64(),
            {{types_->get_int64(), FunctionArgumentType::REQUIRED}},
            /*context_id=*/-1}},
          FunctionOptions().set_volatility(FunctionEnums::VOLATILE));
      catalog_->AddOwnedFunction(function);
    });

RegisterForSampleCatalog stable_function =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      Function* function = new Function(
          "stable_function", "sample_functions", Function::SCALAR,
          {{types_->get_int64(),
            {{types_->get_int64(), FunctionArgumentType::REQUIRED}},
            /*context_id=*/-1}},
          FunctionOptions().set_volatility(FunctionEnums::STABLE));
      catalog_->AddOwnedFunction(function);
    });
}  // namespace

void SampleCatalogImpl::LoadFunctions() {
  Function* function;
  // Add a function that takes a specific proto as an argument.
  function =
      new Function("fn_on_KitchenSinkPB", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {proto_KitchenSinkPB_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes a specific enum as an argument.
  function =
      new Function("fn_on_TestEnum", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {enum_TestEnum_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // These sample functions are named 'fn_on_<typename>' with one argument of
  // type <typename> that returns a bool.
  AddFunctionWithArgumentType("bool", types_->get_bool());
  AddFunctionWithArgumentType("int32", types_->get_int32());
  AddFunctionWithArgumentType("int64", types_->get_int64());
  AddFunctionWithArgumentType("uint32", types_->get_uint32());
  AddFunctionWithArgumentType("uint64", types_->get_uint64());
  AddFunctionWithArgumentType("float", types_->get_float());
  AddFunctionWithArgumentType("double", types_->get_double());
  AddFunctionWithArgumentType("date", types_->get_date());
  AddFunctionWithArgumentType("timestamp", types_->get_timestamp());
  AddFunctionWithArgumentType("string", types_->get_string());
  AddFunctionWithArgumentType("bytes", types_->get_bytes());

  // Add a function with bytes and string overload.
  function = new Function("fn_overloaded_bytes_and_string", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_string(), {types_->get_string()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bytes(), {types_->get_bytes()}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and date overload.
  function = new Function("fn_overloaded_bytes_and_date", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {types_->get_date()}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and timestamp overload.
  function = new Function("fn_overloaded_bytes_and_timestamp",
                          "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {types_->get_timestamp()}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and time overload.
  function = new Function("fn_overloaded_bytes_and_time", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {types_->get_time()}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and datetime overload.
  function = new Function("fn_overloaded_bytes_and_datetime",
                          "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {types_->get_datetime()}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and enum overload.
  function = new Function("fn_overloaded_bytes_and_enum", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {enum_TestEnum_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with bytes and proto overload.
  function = new Function("fn_overloaded_bytes_and_proto", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {types_->get_bytes()}, /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(), {proto_KitchenSinkPB_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes an arbitrary type argument.
  function = new Function("fn_on_arbitrary_type_argument", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {ARG_TYPE_ARBITRARY}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes any type enum.
  function =
      new Function("fn_on_any_enum", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {ARG_ENUM_ANY}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes any type proto.
  function =
      new Function("fn_on_any_proto", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {ARG_PROTO_ANY}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes any type struct.
  function =
      new Function("fn_on_any_struct", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {ARG_STRUCT_ANY}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes any array and returns element type.
  function = new Function("fn_on_any_array_returns_element", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{ARG_TYPE_ANY_1}, {ARG_ARRAY_TYPE_ANY_1}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes any type and returns an array of that type.
  function = new Function("fn_on_any_element_returns_array", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{ARG_ARRAY_TYPE_ANY_1}, {ARG_TYPE_ANY_1}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes an array<int32> and returns an int32 type.
  function = new Function("fn_on_int32_array_returns_int32", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{types_->get_int32()}, {int32array_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes an array<int64> and returns an int64 type.
  function = new Function("fn_on_int64_array_returns_int64", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{types_->get_int64()}, {int64array_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes a STRUCT<int32, string> and returns bool.
  function = new Function("fn_on_struct_int32_string", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {struct_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  const StructType* struct_int32_date_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int32()}, {"b", types_->get_date()}},
      &struct_int32_date_type));

  // Add a function that takes a STRUCT<int32, date> and returns bool.
  function = new Function("fn_on_struct_int32_date", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {struct_int32_date_type}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  const StructType* struct_int64_string_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int64()}, {"b", types_->get_string()}},
      &struct_int64_string_type));

  // Add a function that takes a STRUCT<int64, string> and returns bool.
  function = new Function("fn_on_struct_int64_string", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {types_->get_bool(), {struct_int64_string_type}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(absl::WrapUnique(function));

  // Add a function that takes a MAP<int32, int32> and returns an int32 type.
  function = new Function("fn_on_int32_map_returns_int32", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{types_->get_int32()}, {int32map_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes a MAP<int64, int64> and returns an int64 type.
  function = new Function("fn_on_int64_map_returns_int64", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{types_->get_int64()}, {int64map_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes a MAP<bytes, bytes> and returns a bytes type.
  function = new Function("fn_on_bytes_map_returns_bytes", "sample_functions",
                          Function::SCALAR);
  function->AddSignature(
      {{types_->get_bytes()}, {bytesmap_type_}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Adds an scalar function that takes multiple repeated and optional
  // arguments.
  function = new Function(
      "fn_rep_opt", "sample_functions", Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("a0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("r0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REPEATED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("r1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REPEATED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("r2", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REPEATED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("a1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);
  GOOGLESQL_CHECK_OK(function->signatures()[0].IsValid(ProductMode::PRODUCT_EXTERNAL));

  AddFunction("fn_repeated_with_optional_named_only", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Optional().String().NameOnly("o1"))
                   .Build()});

  AddFunction("fn_repeated_diff_args_optional_named_only", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().Int64())
                   .AddArg(ArgBuilder().Optional().Bool().NameOnly("o1"))
                   .Build()});

  AddFunction("fn_repeated_arbitrary_with_optional_named_only",
              Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Optional().Any().Name("o1"))
                   .Build()});

  AddFunction("fn_repeated_with_optional_named_or_positional", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Optional().String().Name("o1"))
                   .Build()});

  AddFunction("fn_repeated_diff_args_optional_named_or_positional",
              Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().Int64())
                   .AddArg(ArgBuilder().Optional().Bool().Name("o1"))
                   .Build()});

  AddFunction("fn_repeated_arbitrary_with_optional_named_or_positional",
              Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Optional().Any().Name("o1"))
                   .Build()});

  AddFunction("fn_repeated_with_required_named", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().String().NameOnly("r1"))
                   .Build()});

  AddFunction("fn_repeated_diff_args_required_named", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().String())
                   .AddArg(ArgBuilder().Repeated().Int64())
                   .AddArg(ArgBuilder().Bool().NameOnly("r1"))
                   .Build()});

  AddFunction("fn_repeated_arbitrary_with_required_named", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Any().NameOnly("r1"))
                   .Build()});

  AddFunction("fn_repeated_t1_t2_with_optional_named_t1", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().T1())
                   .AddArg(ArgBuilder().Repeated().T2())
                   .AddArg(ArgBuilder().Optional().T1().NameOnly("o1"))
                   .Build()});

  AddFunction("fn_repeated_t1_arbitrary_with_optional_named_t1",
              Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().T1())
                   .AddArg(ArgBuilder().Repeated().Any())
                   .AddArg(ArgBuilder().Optional().T1().NameOnly("o1"))
                   .Build()});

  AddFunction(
      "fn_optional_any", Function::SCALAR,
      {SignatureBuilder().AddArg(ArgBuilder().Optional().Any()).Build()});

  AddFunction(
      "fn_repeated_any", Function::SCALAR,
      {SignatureBuilder().AddArg(ArgBuilder().Repeated().Any()).Build()});

  AddFunction(
      "fn_optional_t1", Function::SCALAR,
      {SignatureBuilder().AddArg(ArgBuilder().Optional().T1()).Build()});

  AddFunction("fn_optional_t1_ret_t1", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Optional().T1())
                   .Returns(ArgBuilder().T1())
                   .Build()});

  AddFunction(
      "fn_repeated_t1", Function::SCALAR,
      {SignatureBuilder().AddArg(ArgBuilder().Repeated().T1()).Build()});

  AddFunction("fn_repeated_t1_ret_t1", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().Repeated().T1())
                   .Returns(ArgBuilder().T1())
                   .Build()});

  // Adds an aggregate function that takes no argument but supports order by.
  function = new Function("sort_count", "sample_functions", Function::AGGREGATE,
                          {{types_->get_int64(), {}, /*context_id=*/-1}},
                          FunctionOptions().set_supports_order_by(true));
  catalog_->AddOwnedFunction(function);

  // Adds an aggregate function that takes multiple arguments and supports
  // order by arguments.
  function = new Function(
      "multi_sort_count", "sample_functions", Function::AGGREGATE,
      {{types_->get_int64(),
        {types_->get_int32(), types_->get_int64(), types_->get_string()},
        /*context_id=*/-1}},
      FunctionOptions().set_supports_order_by(true));
  catalog_->AddOwnedFunction(function);

  // Adds fn_agg_string_string_collation(STRING, STRING) -> INT64.
  // Enables uses_operation_collation to test collation resolution.
  function = new Function(
      "fn_agg_string_string_collation", "sample_functions", Function::AGGREGATE,
      {{types_->get_int64(),
        {types_->get_string(), types_->get_string()},
        /*context_id=*/-1,
        FunctionSignatureOptions().set_uses_operation_collation(true)}},
      FunctionOptions(FunctionOptions::ORDER_UNSUPPORTED,
                      /*window_framing_support_in=*/true));
  catalog_->AddOwnedFunction(function);

  // Do not add more to this function. Instead, use RegisterForSampleCatalog
  // inside an unnamed namespace. Context: Under some compilation modes all
  // local variables get their own stack location and this causes the stack
  // limit to be exceeded.
}

namespace {

RegisterForSampleCatalog fn_reject_collation =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();

      // Adds fn_reject_collation(STRING, ANY TYPE) -> INT64.
      // Enables rejects_collation to test collation resolution.
      catalog_->AddOwnedFunction(new Function(
          "fn_reject_collation", "sample_functions", Function::SCALAR,
          {{types_->get_int64(),
            {types_->get_string(),
             {ARG_TYPE_ANY_1,
              FunctionArgumentTypeOptions()
                  .set_argument_name("second_arg", kPositionalOrNamed)
                  .set_cardinality(FunctionArgumentType::OPTIONAL)}},
            /*context_id=*/-1,
            FunctionSignatureOptions().set_rejects_collation(true)}}));
    });

// Add the following test analytic functions. All functions have the same
// list of function signatures:
//     arguments: (), (ARG_TYPE_ANY_1) and (<int64>, <string>))
//     return: <int64>
//
// They differ in the window support:
// ---------------------------------------------------------------------------
//                           Mode      ORDER BY    Window Frame  Null Handling
//                                                                 Modifier
//                         --------   -----------  ---------------------------
//  afn_order              ANALYTIC    Required    Unsupported   Unsupported
//  afn_no_order_no_frame  ANALYTIC   Unsupported  Unsupported   Unsupported
//  afn_agg                AGGREGATE   Optional     Supported    Unsupported
//  afn_null_handling      AGGREGATE   Optional    Unsupported    Supported
//  --------------------------------------------------------------------------

RegisterForSampleCatalog test_analytic_functions =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      std::vector<FunctionSignature> function_signatures;
      function_signatures.push_back(
          {types_->get_int64(), {}, /*context_id=*/-1});
      function_signatures.push_back(
          {types_->get_int64(), {ARG_TYPE_ANY_1}, /*context_id=*/-1});
      function_signatures.push_back(
          {types_->get_int64(),
           {types_->get_int64(),
            FunctionArgumentType(
                types_->get_string(),
                FunctionArgumentTypeOptions().set_argument_name(
                    "weight", kPositionalOrNamed))},
           -1 /* context */});

      Function* function =
          new Function("afn_order", "sample_functions", Function::ANALYTIC,
                       function_signatures,
                       FunctionOptions(FunctionOptions::ORDER_REQUIRED,
                                       /*window_framing_support_in=*/false));
      catalog_->AddOwnedFunction(function);

      function =
          new Function("afn_no_order_no_frame", "sample_functions",
                       Function::ANALYTIC, function_signatures,
                       FunctionOptions(FunctionOptions::ORDER_UNSUPPORTED,
                                       /*window_framing_support_in=*/false));
      catalog_->AddOwnedFunction(function);

      function =
          new Function("afn_agg", "sample_functions", Function::AGGREGATE,
                       function_signatures,
                       FunctionOptions(FunctionOptions::ORDER_OPTIONAL,
                                       /*window_framing_support_in=*/true));
      catalog_->AddOwnedFunction(function);

      function =
          new Function("afn_null_handling", "sample_functions",
                       Function::AGGREGATE, function_signatures,
                       FunctionOptions(FunctionOptions::ORDER_OPTIONAL,
                                       /*window_framing_support_in=*/false)
                           .set_supports_order_by(true)
                           .set_supports_limit(true)
                           .set_supports_null_handling_modifier(true));
      catalog_->AddOwnedFunction(function);
    });

RegisterForSampleCatalog null_of_type =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      // NULL_OF_TYPE(string) -> (a NULL of type matching the named simple
      // type). This is testing resolving functions where the return type is
      // determined dynamically based on literal values of the arguments. The
      // callback overrides the INT64 return type in the signature.
      catalog_->AddOwnedFunction(new Function(
          "null_of_type", "sample_functions", Function::SCALAR,
          {{{types::Int64Type()}, {types::StringType()}, /*context_id=*/-1}},
          FunctionOptions().set_compute_result_type_callback(
              &ComputeResultTypeCallbackForNullOfType)));
    });

RegisterForSampleCatalog safe_supported_function =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      catalog_->AddOwnedFunction(new Function(
          "safe_supported_function", "sample_functions", Function::SCALAR,
          {{types_->get_int64(), {}, /*context_id=*/-1}}, FunctionOptions()));
    });

RegisterForSampleCatalog safe_unsupported_function =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();
      catalog_->AddOwnedFunction(new Function(
          "safe_unsupported_function", "sample_functions", Function::SCALAR,
          {{types_->get_int64(), {}, /*context_id=*/-1}},
          FunctionOptions().set_supports_safe_error_mode(false)));
    });

RegisterForSampleCatalog deprecation_warnings =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      // Add a function that triggers a deprecation warning.
      Function* function = new Function("deprecation_warning",
                                        "sample_functions", Function::SCALAR);

      FunctionSignature deprecation_warning_signature(
          types::Int64Type(), /*arguments=*/{}, /*context_id=*/-1);
      deprecation_warning_signature.SetAdditionalDeprecationWarnings(
          {CreateDeprecationWarning(/*id=*/1)});
      function->AddSignature(deprecation_warning_signature);
      catalog_->AddOwnedFunction(function);

      function = new Function("deprecation_warning2", "sample_functions",
                              Function::SCALAR);
      FunctionSignature deprecation_warning2_signature(
          types::Int64Type(), /*arguments=*/{}, /*context_id=*/-1);
      deprecation_warning2_signature.SetAdditionalDeprecationWarnings(
          {CreateDeprecationWarning(/*id=*/2)});
      function->AddSignature(deprecation_warning2_signature);
      catalog_->AddOwnedFunction(function);

      // Add a function that triggers two deprecation warnings with the same
      // kind.
      function = new Function("two_deprecation_warnings_same_kind",
                              "sample_functions", Function::SCALAR);

      FunctionSignature two_deprecation_warnings_same_kind_signature(
          types::Int64Type(), /*arguments=*/{}, /*context_id=*/-1);
      two_deprecation_warnings_same_kind_signature
          .SetAdditionalDeprecationWarnings(
              {CreateDeprecationWarning(/*id=*/2),
               CreateDeprecationWarning(/*id=*/3)});
      function->AddSignature(two_deprecation_warnings_same_kind_signature);
      catalog_->AddOwnedFunction(function);

      // Add a function that triggers two deprecation warnings with different
      // kinds.
      function = new Function("two_deprecation_warnings", "sample_functions",
                              Function::SCALAR);
      FunctionSignature two_deprecation_warnings_signature(
          types::Int64Type(), /*arguments=*/{}, /*context_id=*/-1);
      two_deprecation_warnings_signature.SetAdditionalDeprecationWarnings(
          {CreateDeprecationWarning(/*id=*/4),
           CreateDeprecationWarning(
               /*id=*/5, DeprecationWarning::DEPRECATED_FUNCTION_SIGNATURE)});
      function->AddSignature(two_deprecation_warnings_signature);
      catalog_->AddOwnedFunction(function);
    });

RegisterForSampleCatalog fn_map_type_any_1_2_lambda_any_1_any_2_return_bool =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      // Function taking a map and a lambda with key argument type and value
      // return type, and returning a bool.
      const auto mode = Function::SCALAR;
      auto function = std::make_unique<Function>(
          "fn_map_type_any_1_2_lambda_any_1_any_2_return_bool",
          "sample_functions", mode);
      function->AddSignature(
          {types::BoolType(),
           {ARG_MAP_TYPE_ANY_1_2,
            FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2)},
           /*context_id=*/-1});
      catalog_->AddOwnedFunction(std::move(function));
    });

RegisterForSampleCatalog intentionally_ret_check =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      // During resolution, this function's result type callback will GOOGLESQL_RET_CHECK.
      catalog_->AddOwnedFunction(new Function(
          "intentionally_ret_check", "sample_functions", Function::SCALAR,
          {{{types::Int64Type()}, {}, /*context_id=*/-1}},
          FunctionOptions().set_compute_result_type_callback(
              &ComputeResultTypeCallbackForIntentionallyRetCheck)));
    });

RegisterForSampleCatalog intentionally_ret_check_rewrite =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      // During rewrite, this function's rewrite SQL will cause the analyzer
      // to GOOGLESQL_RET_CHECK.
      catalog_->AddOwnedFunction(new Function(
          "intentionally_ret_check_rewrite", "sample_functions",
          Function::SCALAR,
          {{{types::Int64Type()},
            {},
            /*context_id=*/-1,
            FunctionSignatureOptions().set_rewrite_options(
                FunctionSignatureRewriteOptions()
                    .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                    .set_sql(R"sql(intentionally_ret_check())sql")
                    .set_allowed_function_groups({"sample_functions"}))}}));
    });

}  // namespace

void SampleCatalogImpl::LoadFunctions2() {
  // Do not add more to this function. Instead, use RegisterForSampleCatalog
  // inside an unnamed namespace. Context: Under some compilation modes all
  // local variables get their own stack location and this causes the stack
  // limit to be exceeded.

  Function* function;

  catalog_->AddOwnedFunction(new Function(
      "anon_non_anon", "sample_functions", Function::SCALAR,
      {{types_->get_int64(), {}, /*context_id=*/-1}}, FunctionOptions()));

  function = new AnonFunction(
      "anon_test", "sample_functions",
      {{types_->get_int64(),
        {/*expr=*/types_->get_int64(),
         /*lower_bound=*/{types_->get_int64(), FunctionArgumentType::OPTIONAL},
         /*upper_bound=*/{types_->get_int64(), FunctionArgumentType::OPTIONAL}},
        /*context_id=*/-1}},
      FunctionOptions()
          .set_supports_clamped_between_modifier(true)
          .set_supports_over_clause(false),
      "sum");
  catalog_->AddOwnedFunction(function);

  // Add a function that takes two named arguments with one signature.
  const auto named_required_format_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "format_string", kPositionalOrNamed));
  const auto named_required_date_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "date_string", kPositionalOrNamed));
  const auto named_required_format_arg_error_if_positional =
      FunctionArgumentType(types_->get_string(),
                           FunctionArgumentTypeOptions().set_argument_name(
                               "format_string", kNamedOnly));
  const auto named_required_date_arg_error_if_positional = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "date_string", kNamedOnly));
  const auto named_optional_date_arg_error_if_positional = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::OPTIONAL)
                                .set_argument_name("date_string", kNamedOnly));
  const auto named_optional_format_arg = FunctionArgumentType(
      types_->get_string(),
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_argument_name("format_string", kPositionalOrNamed));
  const auto named_optional_date_arg = FunctionArgumentType(
      types_->get_string(),
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_argument_name("date_string", kPositionalOrNamed));
  const auto named_optional_const_format_arg = FunctionArgumentType(
      types_->get_string(),
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_must_be_constant()
          .set_argument_name("format_string", kPositionalOrNamed));
  const auto non_named_required_format_arg =
      FunctionArgumentType(types_->get_string(), FunctionArgumentTypeOptions());
  const auto non_named_required_date_arg =
      FunctionArgumentType(types_->get_string(), FunctionArgumentTypeOptions());
  const auto non_named_optional_format_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_cardinality(
                                FunctionArgumentType::OPTIONAL));
  const auto non_named_optional_date_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_cardinality(
                                FunctionArgumentType::OPTIONAL));
  const auto named_optional_arg_named_not_null = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::OPTIONAL)
                                .set_must_be_non_null()
                                .set_argument_name("arg", kNamedOnly));
  const auto named_rounding_mode =
      FunctionArgumentType(types::RoundingModeEnumType(),
                           FunctionArgumentTypeOptions().set_argument_name(
                               "rounding_mode", kPositionalOrNamed));

  const auto mode = Function::SCALAR;

  function = new Function("fn_named_args", "sample_functions", mode);
  function->AddSignature({types_->get_bool(),
                          {named_required_format_arg, named_required_date_arg},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);
  function = new Function("fn_const_named_arg", "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {named_optional_const_format_arg, named_optional_date_arg},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add functions with two named optional/repeated arguments on one signature.
  function = new Function("fn_named_args_optional", "sample_functions", mode);
  function->AddSignature({types_->get_bool(),
                          {named_optional_format_arg, named_optional_date_arg},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes two named arguments with two signatures with
  // optional argument types.
  function =
      new Function("fn_named_args_two_signatures", "sample_functions", mode);
  function->AddSignature({types_->get_bool(),
                          {named_required_format_arg, named_required_date_arg},
                          /*context_id=*/-1});
  function->AddSignature({types_->get_bool(),
                          {named_required_date_arg, named_required_format_arg},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function that takes two non-named arguments and one named argument in
  // each of two signatures.
  function = new Function("fn_three_named_args_two_signatures",
                          "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {non_named_required_format_arg, non_named_required_date_arg,
        named_required_format_arg},
       /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(),
       {non_named_required_format_arg, non_named_required_date_arg,
        named_required_date_arg},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two named arguments where neither may be specified
  // positionally.
  function = new Function("fn_named_args_error_if_positional",
                          "sample_functions", mode);
  function->AddSignature({types_->get_bool(),
                          {named_required_format_arg_error_if_positional,
                           named_required_date_arg_error_if_positional},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two named arguments where the first may not be
  // specified positionally.
  function = new Function("fn_named_args_error_if_positional_first_arg",
                          "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {named_required_format_arg_error_if_positional, named_required_date_arg},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two named arguments where the second may not be
  // specified positionally.
  function = new Function("fn_named_args_error_if_positional_second_arg",
                          "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {named_required_format_arg, named_required_date_arg_error_if_positional},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two named arguments, one required and one optional,
  // and neither may be specified positionally.
  function = new Function("fn_named_optional_args_error_if_positional",
                          "sample_functions", mode);
  function->AddSignature({types_->get_bool(),
                          {named_required_format_arg_error_if_positional,
                           named_optional_date_arg_error_if_positional},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two optional arguments, one regular and one named that
  // cannot be specified positionally.
  function =
      new Function("fn_optional_named_optional_args", "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {{types_->get_string(), FunctionArgumentType::OPTIONAL},
        named_optional_date_arg_error_if_positional},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with three optional arguments, one regular and two named,
  // both cannot be specified positionally and first cannot be NULL.
  function = new Function("fn_optional_named_optional_not_null_args",
                          "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {{types_->get_string(), FunctionArgumentType::OPTIONAL},
        named_optional_arg_named_not_null,
        named_optional_date_arg_error_if_positional},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with two signatures, one using regular arguments and one
  // using named arguments that cannot be specified positionally.
  function =
      new Function("fn_regular_and_named_signatures", "sample_functions", mode);
  function->AddSignature(
      {types_->get_bool(),
       {{types_->get_string(), FunctionArgumentType::REQUIRED},
        {types_->get_string(), FunctionArgumentType::OPTIONAL}},
       /*context_id=*/-1});
  function->AddSignature(
      {types_->get_bool(),
       {{types_->get_string(), FunctionArgumentType::REQUIRED},
        named_optional_date_arg_error_if_positional},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add a function with one unnamed and two named STRING arguments and
  // returning a STRING.
  function = new Function("fn_named_arguments_returns_string",
                          "sample_functions", mode);
  function->AddSignature(
      {types_->get_string(),
       {{types_->get_string(), FunctionArgumentType::REQUIRED},
        named_required_format_arg,
        named_required_date_arg},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // A FunctionSignatureArgumentConstraintsCallback that checks for NULL
  // arguments.
  auto sanity_check_nonnull_arg_constraints =
      [](const FunctionSignature& signature,
         absl::Span<const InputArgumentType> arguments) -> std::string {
    ABSL_CHECK(signature.IsConcrete());
    ABSL_CHECK_EQ(signature.NumConcreteArguments(), arguments.size());
    for (int i = 0; i < arguments.size(); ++i) {
      ABSL_CHECK(arguments[i].type()->Equals(signature.ConcreteArgumentType(i)));
      if (arguments[i].is_null()) {
        if (signature.ConcreteArgument(i).has_argument_name()) {
          return absl::StrCat("Argument `",
                              signature.ConcreteArgument(i).argument_name(),
                              "`: NULL argument is not allowed");
        } else {
          return absl::StrCat("Argument ", i + 1,
                              ": NULL argument is not allowed");
        }
      }
    }
    return "";
  };

  // A PostResolutionArgumentConstraintsCallback that restricts all the provided
  // INT64 arguments to be nonnegative if they are literals.
  auto post_resolution_arg_constraints =
      [](const FunctionSignature& signature,
         absl::Span<const InputArgumentType> arguments,
         const LanguageOptions& language_options) -> absl::Status {
    for (int i = 0; i < arguments.size(); ++i) {
      ABSL_CHECK(arguments[i].type()->Equals(signature.ConcreteArgumentType(i)));
      if (!arguments[i].type()->IsInt64() || !arguments[i].is_literal()) {
        continue;
      }
      if (arguments[i].literal_value()->int64_value() < 0) {
        return MakeSqlError()
               << "Argument "
               << (signature.ConcreteArgument(i).has_argument_name()
                       ? signature.ConcreteArgument(i).argument_name()
                       : std::to_string(i + 1))
               << " must not be negative";
      }
    }
    return absl::OkStatus();
  };

  // Add a function with an argument constraint that verifies the concrete
  // arguments in signature matches the input argument list, and rejects
  // any NULL arguments.
  function =
      new Function("fn_named_opt_args_nonnull_nonnegative_constraints",
                   "sample_functions", mode,
                   FunctionOptions().set_post_resolution_argument_constraint(
                       post_resolution_arg_constraints));
  FunctionSignature signature_with_constraints{
      types_->get_bool(),
      {{types_->get_string(),
        FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o1_string", kPositionalOrNamed)},
       {types_->get_int64(),
        FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o2_int64", kPositionalOrNamed)},
       {types_->get_double(),
        FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o3_double", kPositionalOrNamed)}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(
          sanity_check_nonnull_arg_constraints)};
  function->AddSignature(signature_with_constraints);
  catalog_->AddOwnedFunction(function);

  // Similar as the previous function, but the arguments are unnamed.
  function =
      new Function("fn_unnamed_opt_args_nonnull_nonnegative_constraints",
                   "sample_functions", mode,
                   FunctionOptions().set_post_resolution_argument_constraint(
                       post_resolution_arg_constraints));
  FunctionSignature signature_with_unnamed_args_constraints{
      types_->get_bool(),
      {{types_->get_string(), FunctionArgumentType::OPTIONAL},
       {types_->get_int64(), FunctionArgumentType::OPTIONAL},
       {types_->get_double(), FunctionArgumentType::OPTIONAL}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(
          sanity_check_nonnull_arg_constraints)};
  function->AddSignature(signature_with_unnamed_args_constraints);
  catalog_->AddOwnedFunction(function);

  // Adds a templated function that generates its result type via the callback.
  function = new Function("fn_result_type_from_arg", "sample_functions", mode,
                          FunctionOptions().set_compute_result_type_callback(
                              &ComputeResultTypeFromStringArgumentValue));
  function->AddSignature(
      {{types_->get_string()},
       {{types_->get_string(),
         FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
             .set_argument_name("o1", kPositionalOrNamed)},
        {types_->get_string(),
         FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
             .set_argument_name("type_name", kPositionalOrNamed)}},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Function with signatures that should not show up in signature mismatch
  // error messages.
  function = new Function("fn_with_hidden_signatures", "sample_functions", mode,
                          FunctionOptions());
  // Two good signatures
  function->AddSignature({{types_->get_string()},
                          {{types_->get_int64()}},
                          /*context_id=*/-1});
  function->AddSignature({{types_->get_string()},
                          {{types_->get_int32()}},
                          /*context_id=*/-1});
  // Two hidden signatures
  FunctionSignature deprecated_signature{{types_->get_string()},
                                         {{types_->get_string()}},
                                         /*context_id=*/-1};
  deprecated_signature.SetIsDeprecated(true);
  function->AddSignature(deprecated_signature);
  FunctionSignature internal_signature{
      {types_->get_string()},
      {{types_->get_string()}, {types_->get_string()}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_is_internal(true)};
  function->AddSignature(internal_signature);
  catalog_->AddOwnedFunction(function);

  // Adds a function accepting a Sequence argument.
  FunctionOptions function_options;
  std::function<absl::StatusOr<Value>(const absl::Span<const Value>)>
      evaluator = [&](absl::Span<const Value> args) -> absl::StatusOr<Value> {
    return Value::Int64(1);
  };
  function_options.set_evaluator(FunctionEvaluator(evaluator));

  function = new Function("fn_with_sequence_arg", "sample_functions", mode,
                          function_options);
  function->AddSignature({{types_->get_int64()},
                          {FunctionArgumentType::AnySequence()},
                          /*context_id=*/-1});
  // Adds a function signature for accepting both a Sequence argument and a
  // lambda to ensure one doesn't break the other.
  function->AddSignature(
      {{types_->get_int64()},
       {ARG_TYPE_ANY_1, FunctionArgumentType::AnySequence(),
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2)},
       /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add function with constant expression argument.
  function = new Function("fn_with_constant_expr_arg", "sample_functions",
                          Function::SCALAR);
  const auto string_const_expression_arg = googlesql::FunctionArgumentType(
      types_->get_string(), googlesql::FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::REQUIRED)
                                .set_must_be_constant_expression());
  function->AddSignature(
      {types_->get_bool(), {string_const_expression_arg}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  // Add function with analysis constant argument.
  function = new Function("fn_with_analysis_constant_arg", "sample_functions",
                          Function::SCALAR);
  const auto any_analysis_const_arg = googlesql::FunctionArgumentType(
      ARG_TYPE_ARBITRARY, googlesql::FunctionArgumentTypeOptions()
                              .set_cardinality(FunctionArgumentType::REQUIRED)
                              .set_must_be_analysis_constant());
  function->AddSignature(
      {types_->get_bool(), {any_analysis_const_arg}, /*context_id=*/-1});
  catalog_->AddOwnedFunction(function);

  {
    // Add function with immutable constant argument.
    auto function = std::make_unique<Function>("fn_with_immutable_constant_arg",
                                               "sample_functions", mode);
    const auto any_immutable_const_arg = googlesql::FunctionArgumentType(
        ARG_TYPE_ARBITRARY, googlesql::FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::REQUIRED)
                                .set_must_be_immutable_constant());
    function->AddSignature(
        {types_->get_bool(), {any_immutable_const_arg}, /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  {
    auto function = std::make_unique<Function>("fn_with_named_rounding_mode",
                                               "sample_functions", mode);
    function->AddSignature({types_->get_bool(),
                            {named_rounding_mode},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Function with non-concrete return type but with a
  // ComputeResultTypeCallback. Calling this function should not cause function
  // resolver to complain the signature is not concrete.
  {
    auto function = std::make_unique<Function>(
        "non_concrete_return_type_with_compute_result_type_callback",
        "sample_functions", mode,
        FunctionOptions().set_compute_result_type_callback(
            &ComputeResultTypeCallbackToStruct));
    const FunctionArgumentType positional(ARG_TYPE_ANY_1);
    function->AddSignature({ARG_TYPE_ARBITRARY,
                            {positional, positional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Arguments and return type are both ARG_TYPE_ANY_3.
  {
    auto function = std::make_unique<Function>("fn_with_arg_type_any_3",
                                               "sample_functions", mode);
    const FunctionArgumentType positional(ARG_TYPE_ANY_3);
    function->AddSignature({ARG_TYPE_ANY_3,
                            {positional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_TYPE_ANY_3 arguments + ARRAY_ARG_TYPE_ANY_3 result.
  {
    auto function = std::make_unique<Function>("fn_arg_type_any_3_array_result",
                                               "sample_functions", mode);
    const FunctionArgumentType optional(
        ARG_TYPE_ANY_3, FunctionArgumentTypeOptions()
                            .set_cardinality(FunctionArgumentType::OPTIONAL)
                            .set_argument_name("o1", kPositionalOrNamed));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_3,
                            {optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_3 arguments + ARG_TYPE_ANY_3 result.
  {
    auto function = std::make_unique<Function>(
        "fn_arg_type_array_any_3_result_type_any_3", "sample_functions", mode);
    const FunctionArgumentType named_optional(
        ARG_ARRAY_TYPE_ANY_3,
        FunctionArgumentTypeOptions()
            .set_cardinality(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o1", kNamedOnly));
    function->AddSignature({ARG_TYPE_ANY_3,
                            {named_optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_3 arguments + ARG_ARRAY_TYPE_ANY_3 result.
  {
    auto function = std::make_unique<Function>(
        "fn_repeated_array_any_3_return_type_array_any_3", "sample_functions",
        mode);
    const FunctionArgumentType repeated(
        ARG_ARRAY_TYPE_ANY_3, FunctionArgumentTypeOptions().set_cardinality(
                                  FunctionArgumentType::REPEATED));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_3,
                            {repeated},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Arguments and return type are both ARG_TYPE_ANY_4.
  {
    auto function = std::make_unique<Function>("fn_with_arg_type_any_4",
                                               "sample_functions", mode);
    const FunctionArgumentType positional(ARG_TYPE_ANY_4);
    function->AddSignature({ARG_TYPE_ANY_4,
                            {positional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_TYPE_ANY_4 arguments + ARRAY_ARG_TYPE_ANY_4 result.
  {
    auto function = std::make_unique<Function>("fn_arg_type_any_4_array_result",
                                               "sample_functions", mode);
    const FunctionArgumentType optional(
        ARG_TYPE_ANY_4, FunctionArgumentTypeOptions()
                            .set_cardinality(FunctionArgumentType::OPTIONAL)
                            .set_argument_name("o1", kPositionalOrNamed));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_4,
                            {optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_4 arguments + ARG_TYPE_ANY_4 result.
  {
    auto function = std::make_unique<Function>(
        "fn_arg_type_array_any_4_result_type_any_4", "sample_functions", mode);
    const FunctionArgumentType named_optional(
        ARG_ARRAY_TYPE_ANY_4,
        FunctionArgumentTypeOptions()
            .set_cardinality(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o1", kNamedOnly));
    function->AddSignature({ARG_TYPE_ANY_4,
                            {named_optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_4 arguments + ARG_ARRAY_TYPE_ANY_4 result.
  {
    auto function = std::make_unique<Function>(
        "fn_repeated_array_any_4_return_type_array_any_4", "sample_functions",
        mode);
    const FunctionArgumentType repeated(
        ARG_ARRAY_TYPE_ANY_4, FunctionArgumentTypeOptions().set_cardinality(
                                  FunctionArgumentType::REPEATED));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_4,
                            {repeated},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Arguments and return type are both ARG_TYPE_ANY_5.
  {
    auto function = std::make_unique<Function>("fn_with_arg_type_any_5",
                                               "sample_functions", mode);
    const FunctionArgumentType positional(ARG_TYPE_ANY_5);
    function->AddSignature({ARG_TYPE_ANY_5,
                            {positional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_TYPE_ANY_5 arguments + ARRAY_ARG_TYPE_ANY_5 result.
  {
    auto function = std::make_unique<Function>("fn_arg_type_any_5_array_result",
                                               "sample_functions", mode);
    const FunctionArgumentType optional(
        ARG_TYPE_ANY_5, FunctionArgumentTypeOptions()
                            .set_cardinality(FunctionArgumentType::OPTIONAL)
                            .set_argument_name("o1", kPositionalOrNamed));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_5,
                            {optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_5 arguments + ARG_TYPE_ANY_5 result.
  {
    auto function = std::make_unique<Function>(
        "fn_arg_type_array_any_5_result_type_any_5", "sample_functions", mode);
    const FunctionArgumentType named_optional(
        ARG_ARRAY_TYPE_ANY_5,
        FunctionArgumentTypeOptions()
            .set_cardinality(FunctionArgumentType::OPTIONAL)
            .set_argument_name("o1", kNamedOnly));
    function->AddSignature({ARG_TYPE_ANY_5,
                            {named_optional},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // ARG_ARRAY_TYPE_ANY_5 arguments + ARG_ARRAY_TYPE_ANY_5 result.
  {
    auto function = std::make_unique<Function>(
        "fn_repeated_array_any_5_return_type_array_any_5", "sample_functions",
        mode);
    const FunctionArgumentType repeated(
        ARG_ARRAY_TYPE_ANY_5, FunctionArgumentTypeOptions().set_cardinality(
                                  FunctionArgumentType::REPEATED));
    function->AddSignature({ARG_ARRAY_TYPE_ANY_5,
                            {repeated},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Regular non-aggregate function with argument alias support.
  {
    auto function = std::make_unique<Function>("fn_for_argument_alias",
                                               "sample_functions", mode);
    // Signature with alias on an optional argument.
    const FunctionArgumentType aliased_1(
        types_->get_bool(),
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_ALIASED));
    const FunctionArgumentType non_aliased_1(types_->get_bool());
    const FunctionArgumentType optional_arg(
        types_->get_string(),
        FunctionArgumentTypeOptions()
            .set_cardinality(FunctionArgumentType::OPTIONAL)
            .set_argument_alias_kind(FunctionEnums::ARGUMENT_ALIASED));
    function->AddSignature({types_->get_bool(),
                            {aliased_1, non_aliased_1, optional_arg},
                            /*context_id=*/-1});

    // Signature with alias on a repeated argument.
    const FunctionArgumentType aliased_2(
        types_->get_string(),
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_ALIASED));
    const FunctionArgumentType non_aliased_2(types_->get_bool());
    const FunctionArgumentType repeated(
        types_->get_int64(),
        FunctionArgumentTypeOptions()
            .set_cardinality(FunctionArgumentType::REPEATED)
            .set_argument_alias_kind(FunctionEnums::ARGUMENT_ALIASED));
    function->AddSignature({types_->get_bool(),
                            {aliased_2, non_aliased_2, repeated},
                            /*context_id=*/-1});

    catalog_->AddOwnedFunction(std::move(function));
  }
  // Aggregate function with argument alias support.
  {
    FunctionArgumentType aliased(
        ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_ALIASED));
    FunctionArgumentType non_aliased(ARG_TYPE_ANY_2);
    std::vector<FunctionSignature> function_signatures = {
        {types_->get_int64(), {aliased, non_aliased}, /*context_id=*/-1}};
    catalog_->AddOwnedFunction(
        new Function("aggregate_fn_for_argument_alias", "sample_functions",
                     Function::AGGREGATE, function_signatures));
  }
  // Analytic functions with argument alias support.
  {
    FunctionArgumentType aliased(
        ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_ALIASED));
    FunctionArgumentType non_aliased(ARG_TYPE_ANY_2);
    std::vector<FunctionSignature> function_signatures = {
        {types_->get_int64(), {aliased, non_aliased}, /*context_id=*/-1}};
    catalog_->AddOwnedFunction(
        new Function("analytic_fn_for_argument_alias", "sample_functions",
                     Function::ANALYTIC, function_signatures,
                     FunctionOptions(FunctionOptions::ORDER_REQUIRED,
                                     /*window_framing_support_in=*/false)));
  }

  // Function with argument aliases and ComputeReturnTypeCallback.
  {
    auto function = std::make_unique<Function>(
        "fn_to_struct_with_optional_aliases", "sample_functions", mode,
        FunctionOptions().set_compute_result_type_callback(
            &ComputeResultTypeCallbackToStructUseArgumentAliases));
    // Signature where no argument aliases are allowed.
    {
      const FunctionArgumentType arg_1(types_->get_bool());
      const FunctionArgumentType arg_2(types_->get_bool());
      function->AddSignature({ARG_TYPE_ARBITRARY,
                              {arg_1, arg_2},
                              /*context_id=*/-1});
    }
    // Signature where one of the arguments can have an alias.
    {
      const FunctionArgumentType arg_1(
          types_->get_int64(),
          FunctionArgumentTypeOptions().set_argument_alias_kind(
              FunctionEnums::ARGUMENT_ALIASED));
      const FunctionArgumentType arg_2(types_->get_int64());
      function->AddSignature({ARG_TYPE_ARBITRARY,
                              {arg_1, arg_2},
                              /*context_id=*/-1});
    }
    // Signature where both the arguments can have aliases.
    {
      const FunctionArgumentType arg_1(
          types_->get_string(),
          FunctionArgumentTypeOptions().set_argument_alias_kind(
              FunctionEnums::ARGUMENT_ALIASED));
      const FunctionArgumentType arg_2(
          types_->get_string(),
          FunctionArgumentTypeOptions().set_argument_alias_kind(
              FunctionEnums::ARGUMENT_ALIASED));
      function->AddSignature({ARG_TYPE_ARBITRARY,
                              {arg_1, arg_2},
                              /*context_id=*/-1});
    }
    // Signature with optional arguments.
    {
      const FunctionArgumentType optional_supports_alias(
          types_->get_string(),
          FunctionArgumentTypeOptions()
              .set_argument_alias_kind(FunctionEnums::ARGUMENT_ALIASED)
              .set_cardinality(FunctionArgumentType::OPTIONAL)
              .set_default(values::String("default_string")));
      const FunctionArgumentType optional_not_supports_alias(
          types_->get_int64(),
          FunctionArgumentTypeOptions()
              .set_cardinality(FunctionArgumentType::OPTIONAL)
              .set_default(values::Int64(100)));
      function->AddSignature(
          {ARG_TYPE_ARBITRARY,
           {optional_supports_alias, optional_not_supports_alias},
           /*context_id=*/-1});
    }
    // Signature with repeated arguments.
    {
      // Use one required positional arguments to avoid ambiguity.
      const FunctionArgumentType positional(types_->get_bytes());
      const FunctionArgumentType repeated_supports_alias(
          types_->get_int64(),
          FunctionArgumentTypeOptions()
              .set_argument_alias_kind(FunctionEnums::ARGUMENT_ALIASED)
              .set_cardinality(FunctionArgumentType::REPEATED));
      const FunctionArgumentType repeated_not_supports_alias(
          types_->get_string(), FunctionArgumentTypeOptions().set_cardinality(
                                    FunctionArgumentType::REPEATED));
      function->AddSignature(
          {ARG_TYPE_ARBITRARY,
           {positional, repeated_supports_alias, repeated_not_supports_alias},
           /*context_id=*/-1});
    }
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Window function with named lambda.
  {
    FunctionArgumentType named_lambda = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda",
                                                        kNamedOnly));
    Function* function =
        new Function("afn_named_lambda", "sample_functions", Function::ANALYTIC,
                     {{ARG_TYPE_ANY_1,
                       {ARG_TYPE_ANY_1, named_lambda},
                       /*context_id=*/-1}},
                     FunctionOptions(FunctionOptions::ORDER_REQUIRED,
                                     /*window_framing_support_in=*/false));
    catalog_->AddOwnedFunction(function);
  }
  // Scalar function with named lambda.
  {
    FunctionArgumentType named_lambda = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda",
                                                        kNamedOnly));
    Function* function =
        new Function("fn_named_lambda", "sample_functions", Function::SCALAR,
                     {{ARG_TYPE_ANY_1,
                       {ARG_TYPE_ANY_1, named_lambda},
                       /*context_id=*/-1}});
    catalog_->AddOwnedFunction(function);
  }
  // Aggregate function with named lambda.
  {
    FunctionArgumentType named_lambda = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1,
        FunctionArgumentTypeOptions()
            .set_argument_name("named_lambda", kNamedOnly)
            .set_is_not_aggregate());
    Function* function = new Function("fn_aggregate_named_lambda",
                                      "sample_functions", Function::AGGREGATE,
                                      {{ARG_TYPE_ANY_1,
                                        {ARG_TYPE_ANY_1, named_lambda},
                                        /*context_id=*/-1}});
    catalog_->AddOwnedFunction(function);
  }
  // Function with multiple named arguments, including named lambdas. This
  // function can be used to verify that named lambdas can be specified in any
  // order.
  {
    FunctionArgumentType named_1 = FunctionArgumentType(
        ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_name(
                            "named_1", kPositionalOrNamed));
    FunctionArgumentType named_2 = FunctionArgumentType(
        ARG_TYPE_ANY_2, FunctionArgumentTypeOptions().set_argument_name(
                            "named_2", kPositionalOrNamed));
    FunctionArgumentType named_only_lambda = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_2}, ARG_TYPE_ANY_3,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda",
                                                        kPositionalOrNamed));
    std::vector<FunctionSignature> function_signatures = {
        {ARG_TYPE_ANY_3,
         {named_1, named_2, named_only_lambda},
         /*context_id=*/-1}};
    catalog_->AddOwnedFunction(
        new Function("fn_multiple_named_arguments", "sample_functions",
                     Function::SCALAR, function_signatures));
  }
  // Function with multiple named arguments.
  {
    FunctionArgumentType named_lambda_1 = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda_1",
                                                        kPositionalOrNamed));
    FunctionArgumentType named_lambda_2 = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_3,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda_2",
                                                        kPositionalOrNamed));
    std::vector<FunctionSignature> function_signatures = {
        {ARG_TYPE_ANY_3,
         {ARG_TYPE_ANY_1, named_lambda_1, named_lambda_2},
         /*context_id=*/-1}};
    catalog_->AddOwnedFunction(
        new Function("fn_multiple_named_lambda_arguments", "sample_functions",
                     Function::SCALAR, function_signatures));
  }
  // Function exercising LATERAL JOIN in its SQL rewrite using the
  // BUILTIN_FUNCTION_INLINER.
  {
    // Built-in which is rewritten through a LATERAL join, to exercise the
    // builtin function inliner.
    auto* function = new Function(
        "builtin_rewrites_to_lateral_join", "sample_functions",
        Function::SCALAR,
        {{types::Int64Type(),
          {{types::Int64Type(), FunctionArgumentTypeOptions().set_argument_name(
                                    "arg", kPositionalOrNamed)}},
          /*context_id=*/-1,
          FunctionSignatureOptions().set_rewrite_options(
              FunctionSignatureRewriteOptions()
                  .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                  .set_sql(R"sql(
              (
               SELECT z
               FROM
                (SELECT arg + x AS z      # Bury the lateral join in a subquery.
                  FROM
                  (SELECT 1 AS x) AS t1
                  CROSS JOIN LATERAL
                  (SELECT b FROM (
                    SELECT (SELECT y + arg > x) AS b
                    FROM (SELECT 1 AS y)
                  )
                )
               ) AS t2
              ))sql"))}},
        FunctionOptions().set_supports_safe_error_mode(true));
    catalog_->AddOwnedFunction(function);
  }
  // Function exercising LATERAL JOIN to a TVF rather than a subquery
  {
    // Built-in which is rewritten through a LATERAL join, to exercise the
    // builtin function inliner.
    auto* function = new Function(
        "builtin_rewrites_to_lateral_join_with_tvf", "sample_functions",
        Function::SCALAR,
        {{types::Int64Type(),
          {{types::Int64Type(), FunctionArgumentTypeOptions().set_argument_name(
                                    "arg", kPositionalOrNamed)}},
          /*context_id=*/-1,
          FunctionSignatureOptions().set_rewrite_options(
              FunctionSignatureRewriteOptions()
                  .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                  .set_sql(R"sql(
              (
               SELECT z
               FROM
                (SELECT arg + x AS z      # Bury the lateral join in a subquery.
                  FROM
                    (SELECT 1 AS x) AS t1
                    CROSS JOIN LATERAL
                    builtin_tvf_exactly_one_arg(t1.x + arg) AS t2
                )
              ))sql"))}},
        FunctionOptions().set_supports_safe_error_mode(true));
    catalog_->AddOwnedFunction(function);
  }
  // Function with signatures having a custom ComputeResultAnnotationsCallback.
  {
    auto function = std::make_unique<Function>("fn_custom_annotation_callback",
                                               "sample_functions", mode);
    // Signature with custom annotation callback.
    const StructType* struct_type;
    GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
        {{"key", type_factory()->get_string()},
         {"value", type_factory()->get_string()}},
        &struct_type));
    function->AddSignature(
        {struct_type,
         {types_->get_string(), types_->get_string()},
         /*context_id=*/-1,
         FunctionSignatureOptions().set_compute_result_annotations_callback(
             &ComputeResultAnnotationsCallbackToStruct)});
    // Signature without custom annotation callback.
    function->AddSignature(
        {struct_type,
         {types_->get_int64(), types_->get_string(), types_->get_string()},
         /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function with array input and a custom ComputeResultAnnotationsCallback.
  {
    auto function = std::make_unique<Function>(
        "fn_custom_annotation_callback_array_arg", "sample_functions", mode);
    const Type* array_string_type = nullptr;
    const Type* result_array_type = nullptr;
    {
      GOOGLESQL_CHECK_OK(types_->MakeArrayType(types_->get_string(), &array_string_type));
      const StructType* struct_type;
      GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
          {{"key", type_factory()->get_string()},
           {"value", type_factory()->get_string()}},
          &struct_type));
      GOOGLESQL_CHECK_OK(types_->MakeArrayType(struct_type, &result_array_type));
    }
    // Signature with custom annotation callback.
    function->AddSignature(
        {result_array_type,
         {array_string_type, array_string_type},
         /*context_id=*/-1,
         FunctionSignatureOptions().set_compute_result_annotations_callback(
             &ComputeResultAnnotationsCallbackArraysToArrayOfStruct)});
    // Signature without custom annotation callback.
    function->AddSignature(
        {result_array_type,
         {types_->get_int64(), array_string_type, array_string_type},
         /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function that throws an error when input arguments have annotations.
  {
    auto function = std::make_unique<Function>(
        "fn_custom_annotation_callback_sql_error", "sample_functions", mode);
    function->AddSignature(
        {types_->get_string(),
         {types_->get_string(), types_->get_string()},
         /*context_id=*/-1,
         FunctionSignatureOptions().set_compute_result_annotations_callback(
             &ComputeResultAnnotationsCallbackSqlError)});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function with generic argument list with custom annotation callback.
  {
    auto function = std::make_unique<Function>(
        "fn_custom_annotation_with_generic_argument_list", "sample_functions",
        mode);
    FunctionArgumentType lambda = FunctionArgumentType::Lambda(
        {types_->get_string()}, types_->get_string());
    function->AddSignature(
        {types_->get_string(),
         {lambda, types_->get_string(), types_->get_string()},
         /*context_id=*/-1,
         FunctionSignatureOptions().set_compute_result_annotations_callback(
             &ComputeResultAnnotationsCallbackUseTheFinalAnnotation)});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking an array of any element type and returning the element
  // type.
  {
    auto function = std::make_unique<Function>(
        "fn_req_any_array_returns_any_arg", "sample_functions", mode);
    function->AddSignature({ARG_TYPE_ANY_1,
                            {ARG_ARRAY_TYPE_ANY_1},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a map type and returning the key arg type
  {
    auto function = std::make_unique<Function>(
        "fn_map_type_any_1_2_return_type_any_1", "sample_functions", mode);
    function->AddSignature({ARG_TYPE_ANY_1,
                            {ARG_MAP_TYPE_ANY_1_2},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a map type and returning the value arg type
  {
    auto function = std::make_unique<Function>(
        "fn_map_type_any_1_2_return_type_any_2", "sample_functions", mode);
    function->AddSignature({ARG_TYPE_ANY_2,
                            {ARG_MAP_TYPE_ANY_1_2},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a map type and optional key arg type, and returning value
  // arg type.
  // Models MAP_GET().
  {
    auto function = std::make_unique<Function>(
        "fn_req_map_type_any_1_2_req_any_1_opt_any_2_returns_any_2",
        "sample_functions", mode);
    function->AddSignature({ARG_TYPE_ANY_2,
                            {ARG_MAP_TYPE_ANY_1_2,
                             ARG_TYPE_ANY_1,
                             {ARG_TYPE_ANY_2, FunctionEnums::OPTIONAL}},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking ANY_1 and ANY_2 types, and returning a MAP_TYPE_ANY_1_2
  // arg type.
  {
    auto function = std::make_unique<Function>(
        "fn_any_1_any_2_return_map_type_any_1_2", "sample_functions", mode);
    function->AddSignature({ARG_MAP_TYPE_ANY_1_2,
                            {ARG_TYPE_ANY_1, ARG_TYPE_ANY_2},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a MAP_TYPE_ANY_1_2 arg type, and returning a bool.
  {
    auto function = std::make_unique<Function>(
        "fn_map_type_any_1_2_any_2_return_bool", "sample_functions", mode);
    function->AddSignature({types_->get_bool(),
                            {ARG_MAP_TYPE_ANY_1_2, ARG_TYPE_ANY_2},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a bool, and returning a bool. Requires that
  // FEATURE_MAP_TYPE is enabled.
  {
    auto function = std::make_unique<Function>("fn_requires_map_type",
                                               "sample_functions", mode);
    function->AddSignature(
        {types_->get_bool(),
         {types_->get_bool()},
         /*context_id=*/-1,
         FunctionSignatureOptions().AddRequiredLanguageFeature(
             FEATURE_MAP_TYPE)});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking a bool, and returning a bool. Requires that
  // FEATURE_MAP_TYPE is enabled.
  {
    auto function = std::make_unique<Function>(
        "fn_requires_map_type_for_bool_signature", "sample_functions", mode);
    function->AddSignature(
        {types_->get_string(), {types_->get_string()}, /*context_id=*/-1});
    function->AddSignature(
        {types_->get_bool(),
         {types_->get_bool()},
         /*context_id=*/-1,
         FunctionSignatureOptions().AddRequiredLanguageFeature(
             FEATURE_MAP_TYPE)});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function with an alternating repeated argument pair.
  {
    auto function = std::make_unique<Function>(
        "fn_alternating_repeated_arg_pair", "sample_functions", mode);
    function->AddSignature({ARG_MAP_TYPE_ANY_1_2,
                            {{ARG_TYPE_ANY_1, FunctionEnums::REPEATED},
                             {ARG_TYPE_ANY_2, FunctionEnums::REPEATED}},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function with an alternating repeated argument triple.
  {
    auto function = std::make_unique<Function>(
        "fn_alternating_repeated_arg_triple", "sample_functions", mode);
    function->AddSignature({ARG_MAP_TYPE_ANY_1_2,
                            {{ARG_TYPE_ANY_1, FunctionEnums::REPEATED},
                             {ARG_TYPE_ANY_2, FunctionEnums::REPEATED},
                             {ARG_TYPE_ANY_3, FunctionEnums::REPEATED}},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }
  // Function taking two arrays of key and value element types, and returning a
  // map.
  {
    auto function = std::make_unique<Function>(
        "fn_array_type_any_1_and_array_type_any_2_return_map_type_any_1_2",
        "sample_functions", mode);
    function->AddSignature({ARG_MAP_TYPE_ANY_1_2,
                            {ARG_ARRAY_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_2},
                            /*context_id=*/-1});
    catalog_->AddOwnedFunction(std::move(function));
  }

  // Do not add more to this function. Instead, use RegisterForSampleCatalog
  // inside an unnamed namespace. Context: Under some compilation modes all
  // local variables get their own stack location and this causes the stack
  // limit to be exceeded.
}  // NOLINT(readability/fn_size)

namespace {

RegisterForSampleCatalog fn_range_any_1_returns_bool =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();

      // Adds fn_range_any_1_returns_bool(RANGE<ANY>) -> BOOL.
      // Tests behavior of RANGE null inference (b/349178294)
      catalog_->AddOwnedFunction(new Function(
          "fn_range_any_1_returns_bool", "sample_functions", Function::SCALAR,
          {{types_->get_bool(),
            {ARG_RANGE_TYPE_ANY_1},
            /*context_id=*/-1}}));
    });
RegisterForSampleCatalog fn_range_any_1_any_1_returns_bool =
    RegisterForSampleCatalog([](googlesql::SimpleCatalog* catalog_) {
      googlesql::TypeFactory* types_ = catalog_->type_factory();

      // Adds fn_range_any_1_any_1_returns_bool(RANGE<ANY>, ANY) -> BOOL.
      // Tests behavior of RANGE null inference (b/349178294)
      catalog_->AddOwnedFunction(
          new Function("fn_range_any_1_any_1_returns_bool", "sample_functions",
                       Function::SCALAR,
                       {{types_->get_bool(),
                         {ARG_RANGE_TYPE_ANY_1, ARG_TYPE_ANY_1},
                         /*context_id=*/-1}}));
    });
}  // namespace

void SampleCatalogImpl::LoadFunctionsWithDefaultArguments() {
  // Adds an scalar function that takes multiple optional named arguments with
  // some of them having default values.
  Function* function = new Function(
      "fn_optional_named_default_args", "sample_functions", Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("a0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("o1_default"))},
               {types_->get_double(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o2", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::Double(0.2))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // Similar to the one above, but the optional arguments are templated.
  function = new Function(
      "fn_optional_named_default_args_templated", "sample_functions",
      Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("a0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REQUIRED)},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
               {ARG_TYPE_ANY_2,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("o1_default"))},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o2", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::Int32(2))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // Similar to the one above, but the default argument value is -2 rather than
  // 2.
  function = new Function(
      "fn_optional_named_default_args_templated_negative", "sample_functions",
      Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("a0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::REQUIRED)},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o0", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
               {ARG_TYPE_ANY_2,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("o1_default"))},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_argument_name("o2", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::Int32(-2))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // Adds an scalar function that takes multiple unnamed optional arguments with
  // some of them having default values.
  function = new Function(
      "fn_optional_unnamed_default_args", "sample_functions", Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("o1_default"))},
               {types_->get_double(),
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::Double(0.3))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // Adds an scalar function that takes (concrete typed, templated type). See
  // b/333445090.
  function = new Function(
      "fn_optional_unnamed_default_any_args", "sample_functions",
      Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("abc"))},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("def"))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // Similar to the one above, but the optional arguments are templated.
  function = new Function(
      "fn_optional_unnamed_default_args_templated", "sample_functions",
      Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::REQUIRED)},
               {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_cardinality(
                                    FunctionArgumentType::OPTIONAL)},
               {ARG_TYPE_ANY_2,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("o1_default"))},
               {ARG_TYPE_ANY_1,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::Int32(5))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);

  // A scalar function with both named and unnamed optional arguments plus the
  // last argument with a default value.
  function = new Function(
      "fn_req_opt_unnamed_named_default", "sample_functions", Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o2", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_default(values::String("dv"))},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  GOOGLESQL_CHECK_OK(function->signatures()[0].IsValid(ProductMode::PRODUCT_EXTERNAL));
  catalog_->AddOwnedFunction(function);

  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_named_default_args"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("relation", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("r1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::REQUIRED)},
              {types_->get_string(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o0", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)},
              {types_->get_double(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Double(3.14))},
              {types_->get_uint32(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o2", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Uint32(10086))},
          },
          /*context_id=*/-1)}));

  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_named_default_args_templated"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("relation", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("r1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::REQUIRED)},
              {ARG_TYPE_ANY_1,
               FunctionArgumentTypeOptions()
                   .set_argument_name("o0", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)},
              {ARG_TYPE_ANY_2,
               FunctionArgumentTypeOptions()
                   .set_argument_name("o1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Double(3.14))},
              {ARG_TYPE_ANY_1,
               FunctionArgumentTypeOptions()
                   .set_argument_name("o2", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::String("abc"))},
          },
          /*context_id=*/-1)}));

  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_unnamed_default_args"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_cardinality(
                                      FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::REQUIRED)},
              {types_->get_string(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::OPTIONAL)},
              {types_->get_float(),
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Float(0.618))},
              {types_->get_uint32(),
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Uint32(168))},
          },
          /*context_id=*/-1)}));

  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_unnamed_default_args_templated"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_cardinality(
                                      FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::REQUIRED)},
              {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_cardinality(
                                   FunctionArgumentType::OPTIONAL)},
              {ARG_TYPE_ANY_2,
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::String("xyz"))},
              {ARG_TYPE_ANY_1,
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Int32(-1))},
          },
          /*context_id=*/-1)}));

  // A TVF with a combined of named and unnamed optional arguments.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_unnamed_named"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_cardinality(
                                      FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::REQUIRED)},
              {types_->get_string(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::OPTIONAL)},
              {types_->get_string(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)},
          },
          /*context_id=*/-1)}));

  // A TVF with a combined of named and unnamed optional arguments plus a
  // default argument at last.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_optional_unnamed_named_default"},
      {FunctionSignature(
          /*result_type=*/ARG_TYPE_RELATION,
          /*arguments=*/
          {
              {ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_cardinality(
                                      FunctionArgumentType::REQUIRED)},
              {types_->get_bool(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::REQUIRED)},
              {types_->get_string(),
               FunctionArgumentTypeOptions().set_cardinality(
                   FunctionArgumentType::OPTIONAL)},
              {types_->get_string(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o1", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)},
              {types_->get_int32(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("o2", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_default(values::Int32(314))},
          },
          /*context_id=*/-1)}));

  // A scalar function with both unnamed and named optional arguments.
  function = new Function(
      "fn_req_opt_unnamed_named", "sample_functions", Function::SCALAR,
      /*function_signatures=*/
      {
          {/*result_type=*/types_->get_int64(),
           /*arguments=*/
           {
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::REQUIRED)},
               {types_->get_string(),
                FunctionArgumentTypeOptions().set_cardinality(
                    FunctionArgumentType::OPTIONAL)},
               {types_->get_string(),
                FunctionArgumentTypeOptions()
                    .set_argument_name("o1", kPositionalOrNamed)
                    .set_cardinality(FunctionArgumentType::OPTIONAL)},
           },
           /*context_id=*/-1},
      },
      FunctionOptions());
  catalog_->AddOwnedFunction(function);
  GOOGLESQL_CHECK_OK(function->signatures()[0].IsValid(ProductMode::PRODUCT_EXTERNAL));

  // A scalar function with one optional argument of any type, returning that
  // type.
  function = new Function("fn_optional_any_arg_returns_any", "sample_functions",
                          Function::SCALAR,
                          /*function_signatures=*/
                          {{ARG_TYPE_ANY_1,
                            {{ARG_TYPE_ANY_1, FunctionEnums::OPTIONAL}},
                            /*context_id=*/-1}},
                          FunctionOptions());
  catalog_->AddOwnedFunction(std::move(function));

  // A scalar function with one optional argument of any type, returning an
  // array with that element type.
  function = new Function("fn_optional_any_arg_returns_any_array",
                          "sample_functions", Function::SCALAR,
                          /*function_signatures=*/
                          {{ARG_ARRAY_TYPE_ANY_1,
                            {{ARG_TYPE_ANY_1, FunctionEnums::OPTIONAL}},
                            /*context_id=*/-1}},
                          FunctionOptions());
  catalog_->AddOwnedFunction(std::move(function));

  // A scalar function with a required array of any type and an optional of any
  // type, returning the element type.
  function = new Function(
      "fn_req_any_array_optional_any_arg_returns_any_arg", "sample_functions",
      Function::SCALAR,
      /*function_signatures=*/
      {{ARG_TYPE_ANY_1,
        {ARG_ARRAY_TYPE_ANY_1, {ARG_TYPE_ANY_1, FunctionEnums::OPTIONAL}},
        /*context_id=*/-1}},
      FunctionOptions());
  catalog_->AddOwnedFunction(std::move(function));
}

void SampleCatalogImpl::LoadTemplatedSQLUDFs() {
  // Return an empty struct as the result type for now.
  // The function resolver will dynamically compute a different result type at
  // analysis time based on the function SQL body.
  const FunctionArgumentType result_type(ARG_TYPE_ARBITRARY);
  int context_id = 0;

  // Add a UDF with a simple valid templated SQL body.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_one"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{}, ParseResumeLocation::FromString("1")));

  // Add a templated SQL function that calls another templated SQL function.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_call_udf_templated_return_one"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_templated_return_one()")));

  // Add a templated SQL function that calls another templated SQL function
  // twice.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_call_udf_templated_return_one_twice"},
      FunctionSignature(result_type, {}, context_id++), /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_call_udf_templated_return_one() + "
                                      "udf_call_udf_templated_return_one()")));

  // Add a UDF with a valid templated SQL body that refers to an argument.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_bool_arg"},
      FunctionSignature(result_type, {FunctionArgumentType(types::BoolType())},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a valid templated SQL body that refers to an argument.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_int64_arg"},
      FunctionSignature(result_type, {FunctionArgumentType(types::Int64Type())},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a valid templated SQL body that refers to an argument.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_any_scalar_arg"},
      FunctionSignature(result_type, {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a valid templated SQL body that performs addition on an
  // argument. The function signature accepts a single argument of any type.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_arg_plus_integer"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x + 42")));

  // Add a UDF with a valid templated SQL body that accepts an input argument
  // where the name contains '$'. The function signature accepts a single
  // argument of any type.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_arg_plus_integer_accept_dollars_col_name"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"$col1"},
      ParseResumeLocation::FromString("`$col1`")));

  // Add a UDF with a valid templated SQL body that performs concatenation on an
  // argument. The function signature accepts a single argument of any type.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_arg_concat_string"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("concat(x, 'abc')")));

  // Add a UDF with a valid templated SQL body that performs concatenation on
  // two arguments. The function signature accepts two arguments of any type.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_arg_concat_two_strings"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED),
                         FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x", "y"},
      ParseResumeLocation::FromString("concat(x, y)")));

  // Add a UDF with a valid templated SQL body that performs a proto field
  // access on an argument. The function signature accepts a single argument of
  // any type.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_arg_proto_field_access"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("x.int32_field")));

  // Add an invalid templated SQL function with a parse error in the function
  // body.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_parse_error"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{}, ParseResumeLocation::FromString("a b c d e")));

  // Add an invalid templated SQL function with an analysis error in the
  // function body.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_analysis_error"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{}, ParseResumeLocation::FromString("'abc' + 42")));

  // Add a UDF that refers to 'udf_templated_analysis_error' to show two levels
  // of nested error messages.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_call_udf_templated_analysis_error"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_templated_analysis_error() + 1")));

  // Add a UDF that refers to 'udf_call_udf_templated_analysis_error' to show
  // three levels of nested error messages.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_call_udf_call_udf_templated_analysis_error"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString(
          "udf_call_udf_templated_analysis_error() + 1")));

  // Add a UDF that refers to 'udf_call_udf_call_udf_templated_analysis_error'
  // to show four levels of nested error messages.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_call_udf_call_udf_call_udf_templated_analysis_error"},
      FunctionSignature(result_type, {}, context_id++), /*argument_names=*/{},
      ParseResumeLocation::FromString(
          "udf_call_udf_call_udf_templated_analysis_error() + 1")));

  // Add an invalid templated SQL function that attempts to refer to a query
  // parameter.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_function_body_refer_to_parameter"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString("@test_param_bool")));

  // Add an invalid templated SQL function where the function body is empty.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_function_body_empty"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{}, ParseResumeLocation::FromString("")));

  // Add an invalid templated SQL function that directly calls itself.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_recursive"}, FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_recursive()")));

  // Add two invalid templated SQL functions that indirectly call themselves
  // through one or more other templated SQL function calls.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_calls_self_indirectly_1"},
      FunctionSignature(result_type, {}, context_id++), /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_calls_self_indirectly_2()")));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_calls_self_indirectly_2"},
      FunctionSignature(result_type, {}, context_id++),
      /*argument_names=*/{},
      ParseResumeLocation::FromString("udf_calls_self_indirectly_1()")));

  // Add a templated SQL function that calls a templated SQL TVF that calls the
  // original templated SQL function again, to make sure cycle detection works.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_calls_tvf_calls_same_udf"},
      FunctionSignature(result_type, {}, context_id++), /*argument_names=*/{},
      ParseResumeLocation::FromString(
          "(select * from tvf_calls_udf_calls_same_tvf())")));

  // Add a templated SQL function with duplicate argument names.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_duplicate_arg_names"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED),
                         FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x", "x"},
      ParseResumeLocation::FromString("concat(x, 'abc')")));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_like_any"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("x like any ('z')")));

  // Add a templated SQL function that triggers a deprecation warning.
  FunctionSignature deprecation_warning_signature(result_type, /*arguments=*/{},
                                                  /*context_id=*/-1);
  deprecation_warning_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/101)});
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_calls_deprecation_warning"}, deprecation_warning_signature,
      /*argument_names=*/{},
      ParseResumeLocation::FromString("deprecation_warning()")));

  // Add a UDF with a simple valid templated SQL body that returns its one input
  // argument, and has an expected result type of a 32-bit integer. We will use
  // this to test comparing the result type against the expected type when
  // testing templated SQL function calls.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_one_templated_arg_return_int32"},
      FunctionSignature(types::Int32Type(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a simple valid templated SQL body that returns its one input
  // argument, and has an expected result type of a 64-bit integer. We will use
  // this to test comparing the result type against the expected type when
  // testing templated SQL function calls.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_one_templated_arg_return_int64"},
      FunctionSignature(types::Int64Type(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a simple valid templated SQL body that returns its one input
  // argument, and has an expected result type of a date. We will use this to
  // test comparing the result type against the expected type when testing
  // templated SQL function calls.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_one_templated_arg_return_date"},
      FunctionSignature(types::DateType(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a simple valid templated SQL body that returns its one input
  // argument, and has an expected result type of a struct. We will use
  // this to test comparing the result type against the expected type when
  // testing templated SQL function calls.
  const StructType* return_type = nullptr;
  std::vector<StructType::StructField> fields;
  fields.emplace_back("int_field", types::Int64Type());
  fields.emplace_back("string_field", types::StringType());
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(fields, &return_type));
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_one_templated_arg_return_struct_int64_string"},
      FunctionSignature(return_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x")));

  // Add a UDF with a simple valid templated SQL body that returns a constant
  // integer, and has an expected result type of a string. We will use
  // this to test comparing the result type against the expected type when
  // testing templated SQL function calls.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_42_return_type_string"},
      FunctionSignature(types::StringType(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("42")));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_42_return_type_int32"},
      FunctionSignature(types::Int32Type(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("42")));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_return_999999999999999_return_type_int32"},
      FunctionSignature(types::Int32Type(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("999999999999999")));

  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"udf_any_and_string_args_return_string_arg"},
      FunctionSignature(FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                             FunctionArgumentType::REQUIRED),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED),
                         FunctionArgumentType(types::StringType(),
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString("x")));

  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"udf_any_and_double_args_return_any"},
      FunctionSignature(FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                             FunctionArgumentType::REQUIRED),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED),
                         FunctionArgumentType(types::DoubleType(),
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString("IF(x < 0, 'a', 'b')")));

  const ArrayType* double_array_type = nullptr;
  GOOGLESQL_CHECK_OK(
      type_factory()->MakeArrayType(types::DoubleType(), &double_array_type));
  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"udf_any_and_double_array_args_return_any"},
      FunctionSignature(FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                             FunctionArgumentType::REQUIRED),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED),
                         FunctionArgumentType(double_array_type,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString("IF(x[SAFE_OFFSET(0)] < 0, 'a', 'b')")));

  // Add a SQL UDA with a valid templated SQL body that refers to an aggregate
  // argument only.
  FunctionArgumentType int64_aggregate_arg_type(types::Int64Type());
  FunctionArgumentTypeOptions agg_options;
  agg_options.set_is_not_aggregate();
  FunctionArgumentType int64_not_aggregate_arg_type(types::Int64Type(),
                                                    agg_options);

  FunctionOptions fn_opts_allow_null_handling;
  fn_opts_allow_null_handling.set_supports_null_handling_modifier(true);

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_valid_templated_return_sum_int64_arg"},
      FunctionSignature(result_type, {int64_aggregate_arg_type}, context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("sum(x)"),
      Function::AGGREGATE, fn_opts_allow_null_handling));

  // Add a SQL UDA with a valid templated SQL body that refers to a NOT
  // AGGREGATE argument only.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_valid_templated_return_int64_not_aggregate_arg"},
      FunctionSignature(result_type, {int64_not_aggregate_arg_type},
                        context_id++),
      /*argument_names=*/{"y"}, ParseResumeLocation::FromString("y"),
      Function::AGGREGATE));

  // Add a SQL UDA with a valid templated SQL body that refers to an AGGREGATE
  // argument and also a NOT AGGREGATE argument in the same script.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_valid_templated_return_int64_aggregate_and_not_aggregate_arg"},
      FunctionSignature(
          result_type, {int64_aggregate_arg_type, int64_not_aggregate_arg_type},
          context_id++),
      /*argument_names=*/{"x", "y"},
      ParseResumeLocation::FromString("sum(x) + y"), Function::AGGREGATE));

  // Add a SQL UDA with an invalid templated SQL body that refers to an
  // AGGREGATE argument and also a NOT AGGREGATE argument in the same script.
  // The function attempts to refer to the AGGREGATE argument outside of an
  // aggregate function, which is invalid. Note that NOT AGGREGATE arguments can
  // still be aggregated.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_invalid_templated_return_int64_aggregate_and_not_aggregate_arg"},
      FunctionSignature(
          result_type, {int64_aggregate_arg_type, int64_not_aggregate_arg_type},
          context_id++),
      /*argument_names=*/{"x", "y"},
      ParseResumeLocation::FromString("sum(y) + x"), Function::AGGREGATE));

  // Add a SQL UDA with an invalid templated SQL body, since there is an ORDER
  // BY clause in an aggregate function nested inside.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_invalid_templated_return_sum_int64_arg_nested_order_by"},
      FunctionSignature(result_type, {int64_aggregate_arg_type}, context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("sum(x order by x)"),
      Function::AGGREGATE));

  FunctionArgumentTypeOptions required_non_agg_options =
      FunctionArgumentTypeOptions(FunctionArgumentType::REQUIRED)
          .set_is_not_aggregate(true);
  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"uda_any_and_string_args_return_string"},
      FunctionSignature(
          FunctionArgumentType(ARG_TYPE_ARBITRARY,
                               FunctionArgumentType::REQUIRED),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::REQUIRED),
           FunctionArgumentType(types::StringType(), required_non_agg_options)},
          context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString("IF(LOGICAL_OR(a), x, x || '_suffix')"),
      Function::AGGREGATE));

  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"uda_any_and_double_args_return_any"},
      FunctionSignature(
          FunctionArgumentType(ARG_TYPE_ARBITRARY,
                               FunctionArgumentType::REQUIRED),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY, required_non_agg_options),
           FunctionArgumentType(types::DoubleType(),
                                FunctionArgumentType::REQUIRED)},
          context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString("STRING_AGG(IF(x < 0, 'a', 'b'))"),
      Function::AGGREGATE));

  GOOGLESQL_CHECK_OK(
      type_factory()->MakeArrayType(types::DoubleType(), &double_array_type));
  catalog_->AddOwnedFunction(std::make_unique<TemplatedSQLFunction>(
      std::vector<std::string>{"uda_any_and_double_array_args_return_any"},
      FunctionSignature(
          FunctionArgumentType(ARG_TYPE_ARBITRARY,
                               FunctionArgumentType::REQUIRED),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::REQUIRED),
           FunctionArgumentType(double_array_type, required_non_agg_options)},
          context_id++),
      /*argument_names=*/std::vector<std::string>{"a", "x"},
      ParseResumeLocation::FromString(
          "IF(x[SAFE_OFFSET(0)] < 0, MAX(a), MIN(a))"),
      Function::AGGREGATE));

  // This function template cannot be invoked because the UDA does not have
  // type information for the `GROUP_ROWS()` TVF. We added it here to reproduce
  // unhelpful error messages.
  // See discussion in: b/285159284.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"WrappedGroupRows"},
      FunctionSignature(result_type, {ARG_TYPE_ARBITRARY}, context_id++),
      /*argument_names=*/{"e"},
      ParseResumeLocation::FromString(
          R"sql(
            SUM(e) WITH GROUP ROWS(SELECT e
                                   FROM GROUP_ROWS()
                                   WHERE e IS NOT NULL))sql"),
      Function::AGGREGATE));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"WrappedMultiLevelAgg"},
      FunctionSignature(result_type, {ARG_TYPE_ARBITRARY}, context_id++),
      /*argument_names=*/{"e"},
      ParseResumeLocation::FromString("SUM(e GROUP BY e)"),
      Function::AGGREGATE));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"WrappedAggWithWhereModifier"},
      FunctionSignature(result_type, {ARG_TYPE_ARBITRARY}, context_id++),
      /*argument_names=*/{"e"},
      ParseResumeLocation::FromString("SUM(e WHERE e > 10)"),
      Function::AGGREGATE));

  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"WrappedAggWithHavingModifier"},
      FunctionSignature(result_type, {ARG_TYPE_ARBITRARY}, context_id++),
      /*argument_names=*/{"e"},
      ParseResumeLocation::FromString("SUM(e GROUP BY e HAVING COUNT(*) > 10)"),
      Function::AGGREGATE));

  // Invalid UDA
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"InvalidReferenceUda"},
      FunctionSignature(result_type, {ARG_TYPE_ARBITRARY, ARG_TYPE_ARBITRARY},
                        context_id++),
      /*argument_names=*/{"value", "key"},
      ParseResumeLocation::FromString("SUM(value GROUP BY key)"),
      Function::AGGREGATE));

  // Add a SQL UDA with a valid templated SQL body with two NOT AGGREGATE
  // arguments having default values.
  FunctionArgumentType int64_default_not_aggregate_arg_type(
      types::Int64Type(),
      FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
          .set_is_not_aggregate()
          .set_default(values::Int64(0))
          .set_argument_name("delta", kPositionalOrNamed));
  FunctionArgumentType bool_default_not_aggregate_arg_type(
      types::BoolType(),
      FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
          .set_is_not_aggregate()
          .set_default(values::Bool(false))
          .set_argument_name("allow_nulls", kPositionalOrNamed));
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_templated_two_not_aggregate_args"},
      FunctionSignature(result_type,
                        {result_type, int64_default_not_aggregate_arg_type,
                         bool_default_not_aggregate_arg_type},
                        context_id++),
      /*argument_names=*/{"cval", "delta", "allow_nulls"},
      ParseResumeLocation::FromString(R"(
        IF(COUNT(1) = COUNT(DISTINCT cval) + delta OR
           (allow_nulls AND COUNTIF(cval IS NOT NULL) = COUNT(DISTINCT cval)),
           NULL, "NOT NULL"))"),
      Function::AGGREGATE));

  // Add a templated (scalar) SQL function with definer rights
  auto templated_scalar_definer_rights_function =
      std::make_unique<TemplatedSQLFunction>(
          std::vector<std::string>{"templated_scalar_definer_rights"},
          FunctionSignature(
              result_type,
              {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                    FunctionArgumentType::REQUIRED)},
              context_id++),
          /*argument_names=*/std::vector<std::string>{"x"},
          ParseResumeLocation::FromString(
              "x + (select count(*) from KeyValue)"));
  templated_scalar_definer_rights_function->set_sql_security(
      ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER);
  catalog_->AddOwnedFunction(
      std::move(templated_scalar_definer_rights_function));

  // Add a templated UDF whose function body returns collated value by calling
  // COLLATE function.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_collate_function"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("collate(x, 'und:ci')")));

  // Add a templated UDF whose function body returns collated array value by
  // using COLLATE function result as an array element.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_collate_function_in_array"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("[collate(x, 'und:ci')]")));

  // Add a templated UDF whose function body returns collated struct value by
  // using COLLATE function result as a struct field.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_collate_function_in_struct"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("(collate(x, 'und:ci'), 1)")));

  // Add a templated UDF whose function body calls COLLATE function but has an
  // explicit STRING return type without collation.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_collate_function_return_string"},
      FunctionSignature(types::StringType(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("collate(x, 'und:ci')")));

  // Add a templated SQL UDA which calls MIN aggregate function with COLLATE
  // function inside.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"uda_templated_aggregate_with_min_collate_function"},
      FunctionSignature(result_type,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("min(collate(x, 'und:ci'))"),
      Function::AGGREGATE));

  // Add a templated SQL UDF which calls TYPEOF which is implemented via
  // GoogleSQL rewrites.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"udf_templated_typeof_function"},
      FunctionSignature(types::StringType(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"}, ParseResumeLocation::FromString("TYPEOF(x)")));

  // Add a templated SQL UDF which calls another templated SQL UDF which calls
  // TYPEOF which is implemented via GoogleSQL rewrites.
  catalog_->AddOwnedFunction(new TemplatedSQLFunction(
      {"nested_udf_templated_typeof_function"},
      FunctionSignature(types::StringType(),
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*argument_names=*/{"x"},
      ParseResumeLocation::FromString("udf_templated_typeof_function(x)")));
}

absl::Status SampleCatalogImpl::LoadAmlBasedPropertyGraphs() {
  typedef std::pair<std::string, const Type*> NameAndType;
  // First, add all the common underlying tables to be shared across the
  // different AML graphs.
  auto* person = new SimpleTable("Person", std::vector<NameAndType>{
                                               {"id", types_->get_int64()},
                                               {"name", types_->get_string()},
                                               {"gender", types_->get_string()},
                                               {"birthday", types_->get_date()},
                                               {"email", types_->get_string()},
                                               {"age", types_->get_uint32()},
                                               {"data", types_->get_bytes()},
                                           });
  GOOGLESQL_RET_CHECK_OK(person->SetPrimaryKey({0}));
  person->SetContents({
      {Value::Int64(1), Value::String("Alice"), Value::String("Female"),
       Value::Date(0), Value::String("alice@example.com"), Value::Uint32(30),
       Value::Bytes("data1")},
      {Value::Int64(2), Value::String("Bob"), Value::String("Male"),
       Value::Date(0), Value::String("bob@example.com"), Value::Uint32(40),
       Value::Bytes("data2")},
  });
  AddOwnedTable(person);

  auto* account = new SimpleTable(
      "Account", std::vector<NameAndType>{{"id", types_->get_int64()},
                                          {"name", types_->get_string()},
                                          {"balance", types_->get_uint64()}});
  GOOGLESQL_RET_CHECK_OK(account->SetPrimaryKey({0}));
  account->SetContents({
      {Value::Int64(1), Value::String("Alice"), Value::Uint64(100)},
      {Value::Int64(2), Value::String("Bob"), Value::Uint64(200)},
  });
  AddOwnedTable(account);

  auto* syndicate = new SimpleTable(
      "Syndicate",
      std::vector<NameAndType>{{"syndicateId", types_->get_int64()},
                               {"syndicateName", types_->get_string()},
                               {"syndicateData", int64array_type_}});
  GOOGLESQL_RET_CHECK_OK(syndicate->SetPrimaryKey({0}));
  syndicate->SetContents({
      {Value::Int64(1), Value::String("Syndicate1"),
       Value::Array(int64array_type_, {Value::Int64(1), Value::Int64(2)})},
      {Value::Int64(2), Value::String("Syndicate2"),
       Value::Array(int64array_type_, {Value::Int64(3), Value::Int64(4)})},
  });
  AddOwnedTable(syndicate);

  auto* person_own_acc = new SimpleTable(
      "PersonOwnAccount",
      std::vector<NameAndType>{{"personId", types_->get_int64()},
                               {"accountId", types_->get_int64()},
                               {"startDate", types_->get_timestamp()}});
  GOOGLESQL_RET_CHECK_OK(person_own_acc->SetPrimaryKey({0, 1}));
  person_own_acc->SetContents({
      {Value::Int64(1), Value::Int64(1), Value::TimestampFromUnixMicros(10000)},
      {Value::Int64(2), Value::Int64(2), Value::TimestampFromUnixMicros(20000)},
  });
  AddOwnedTable(person_own_acc);

  auto* transfer =
      new SimpleTable("Transfer", std::vector<NameAndType>{
                                      {"txnId", types_->get_int64()},
                                      {"fromAccountId", types_->get_int64()},
                                      {"toAccountId", types_->get_int64()},
                                      {"time", types_->get_timestamp()},
                                      {"amount", types_->get_uint64()},
                                  });
  GOOGLESQL_RET_CHECK_OK(transfer->SetPrimaryKey({0}));
  transfer->SetContents({
      {Value::Int64(1), Value::Int64(1), Value::Int64(2),
       Value::TimestampFromUnixMicros(10000000), Value::Uint64(50)},
  });
  AddOwnedTable(transfer);

  const std::vector<const Column*> columns = {
      new SimpleColumn("pg.Place", "city", types_->get_string()),
      new SimpleColumn("pg.Place", "country", types_->get_string()),
      new SimpleColumn("pg.Place", "zip", types_->get_string()),
  };
  auto place_table = std::make_unique<SimpleTable>("Place", columns,
                                                   /*take_ownership=*/true);
  GOOGLESQL_RET_CHECK_OK(place_table->set_full_name("pg.Place"));
  SimpleCatalog* nested_pg_catalog = catalog_->MakeOwnedSimpleCatalog("pg");
  nested_pg_catalog->AddOwnedTable(std::move(place_table));

  auto* test_table_with_same_column_name =
      new SimpleTable("TestTableWithSameColumnName",
                      std::vector<NameAndType>{
                          {"id", types_->get_int64()},
                          {"TestTableWithSameColumnName", types_->get_string()},
                      });
  GOOGLESQL_RET_CHECK_OK(test_table_with_same_column_name->SetPrimaryKey({0}));
  AddOwnedTable(test_table_with_same_column_name);

  auto* test_value_table_with_same_column_name = new SimpleTable(
      "TestValueTableWithSameColumnName",
      std::vector<NameAndType>{
          {"id", types_->get_int64()},
          {"TestValueTableWithSameColumnName", types_->get_string()},
      });
  GOOGLESQL_RET_CHECK_OK(test_value_table_with_same_column_name->SetPrimaryKey({0}));
  AddOwnedTable(test_value_table_with_same_column_name);

  // Additional underlying tables for enhanced AML graph
  auto* city = new SimpleTable(
      "City", std::vector<NameAndType>{{"id", types_->get_int64()},
                                       {"name", types_->get_string()}});
  GOOGLESQL_RET_CHECK_OK(city->SetPrimaryKey({0}));
  AddOwnedTable(city);

  auto* country = new SimpleTable(
      "Country", std::vector<NameAndType>{{"id", types_->get_int64()},
                                          {"name", types_->get_string()}});
  GOOGLESQL_RET_CHECK_OK(country->SetPrimaryKey({0}));
  AddOwnedTable(country);

  auto* knows = new SimpleTable(
      "Knows", std::vector<NameAndType>{{"personId", types_->get_int64()},
                                        {"toPersonId", types_->get_int64()}});
  GOOGLESQL_RET_CHECK_OK(knows->SetPrimaryKey({0, 1}));
  AddOwnedTable(knows);

  auto* located_in = new SimpleTable(
      "LocatedIn",
      std::vector<NameAndType>{{"cityId", types_->get_int64()},
                               {"countryId", types_->get_int64()}});
  GOOGLESQL_RET_CHECK_OK(located_in->SetPrimaryKey({0, 1}));
  AddOwnedTable(located_in);

  auto* group = new SimpleTable("Group", std::vector<NameAndType>{
                                             {"id", types_->get_int64()},
                                             {"group", types_->get_int64()},
                                         });
  GOOGLESQL_RET_CHECK_OK(group->SetPrimaryKey({0}));
  AddOwnedTable(group);

  GOOGLESQL_RETURN_IF_ERROR(LoadBasicAmlPropertyGraph());
  GOOGLESQL_RETURN_IF_ERROR(LoadEnhancedAmlPropertyGraph());

  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadBasicAmlPropertyGraph() {
  std::vector<std::string> property_graph_name_path{"aml"};

  const Table* person = nullptr;
  const Table* account = nullptr;
  const Table* syndicate = nullptr;
  const Table* person_own_acc = nullptr;
  const Table* transfer = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindTable({"Person"}, &person));
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindTable({"Account"}, &account));
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindTable({"Syndicate"}, &syndicate));
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindTable({"PersonOwnAccount"}, &person_own_acc));
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindTable({"Transfer"}, &transfer));

  // Property Declaration
  auto id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "id", property_graph_name_path, types_->get_int64());
  auto personId_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "personId", property_graph_name_path, types_->get_int64());
  auto accountId_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "accountId", property_graph_name_path, types_->get_int64());
  auto target_account_id_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "targetAccountId", property_graph_name_path, types_->get_int64());
  auto amount_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "amount", property_graph_name_path, types_->get_uint64());
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", property_graph_name_path, types_->get_string());
  auto bday_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "birthday", property_graph_name_path, types_->get_date());
  auto balance_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "balance", property_graph_name_path, types_->get_uint64());
  auto age_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "age", property_graph_name_path, types_->get_uint32());
  auto data_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "data", property_graph_name_path, types_->get_bytes());
  auto syndicateid_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "syndicateId", property_graph_name_path, types_->get_int64());
  auto syndicatename_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "syndicateName", property_graph_name_path, types_->get_string());
  auto syndicatedata_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "syndicateData", property_graph_name_path, int64array_type_);

  // Labels
  auto property_dcls_person =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          name_property_dcl.get(), bday_property_dcl.get(),
          age_property_dcl.get(), data_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", property_graph_name_path, property_dcls_person);

  auto property_dcls_account =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get()};
  auto account_label = std::make_unique<SimpleGraphElementLabel>(
      "Account", property_graph_name_path, property_dcls_account);

  auto property_dcls_syndicate =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          syndicateid_property_dcl.get(), syndicatename_property_dcl.get(),
          syndicatedata_property_dcl.get()};
  auto syndicate_label = std::make_unique<SimpleGraphElementLabel>(
      "Syndicate", property_graph_name_path, property_dcls_syndicate);

  auto property_dcls_person_own_acc =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          personId_property_dcl.get(), accountId_property_dcl.get()};
  auto person_own_acc_label = std::make_unique<SimpleGraphElementLabel>(
      "PersonOwnAccount", property_graph_name_path,
      property_dcls_person_own_acc);

  auto property_dcls_transfer =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          accountId_property_dcl.get(), target_account_id_property_dcl.get(),
          amount_property_dcl.get()};
  auto transfer_label = std::make_unique<SimpleGraphElementLabel>(
      "Transfer", property_graph_name_path, property_dcls_transfer);

  auto property_decls_has_id =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get()};
  auto id_label = std::make_unique<SimpleGraphElementLabel>(
      "HasId", property_graph_name_path, property_decls_has_id);

  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_person, property_defs_account, property_defs_syndicate,
      property_defs_person_own_acc, property_defs_transfer;

  // Person node table
  auto person_name_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), person->FindColumnByName("name"));
  auto person_name_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      name_property_dcl.get(), "name");
  InternalPropertyGraph::InternalSetResolvedExpr(person_name_prop_def.get(),
                                                 person_name_col_ref.get());

  property_defs_person.push_back(std::move(person_name_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_name_col_ref));

  auto person_bday_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_date(), person->FindColumnByName("birthday"));
  auto person_bday_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      bday_property_dcl.get(), "birthday");
  InternalPropertyGraph::InternalSetResolvedExpr(person_bday_prop_def.get(),
                                                 person_bday_col_ref.get());

  property_defs_person.push_back(std::move(person_bday_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_bday_col_ref));

  auto person_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person->FindColumnByName("id"));
  auto person_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(person_id_prop_def.get(),
                                                 person_id_col_ref.get());
  property_defs_person.push_back(std::move(person_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_id_col_ref));

  auto person_age_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_uint32(), person->FindColumnByName("age"));
  auto person_age_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      age_property_dcl.get(), "age");
  InternalPropertyGraph::InternalSetResolvedExpr(person_age_prop_def.get(),
                                                 person_age_col_ref.get());
  property_defs_person.push_back(std::move(person_age_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_age_col_ref));

  auto person_data_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_bytes(), person->FindColumnByName("data"));
  auto person_data_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      data_property_dcl.get(), "data");
  InternalPropertyGraph::InternalSetResolvedExpr(person_data_prop_def.get(),
                                                 person_data_col_ref.get());
  property_defs_person.push_back(std::move(person_data_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_data_col_ref));

  auto person_node_table = std::make_unique<const SimpleGraphNodeTable>(
      person->Name(), property_graph_name_path, person, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{person_label.get(),
                                                    id_label.get()},
      std::move(property_defs_person));

  // Account node table
  auto id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), account->FindColumnByName("id"));
  auto account_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(account_id_prop_def.get(),
                                                 id_col_ref.get());

  property_defs_account.push_back(std::move(account_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(std::move(id_col_ref));

  auto balance_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_uint64(), account->FindColumnByName("balance"));
  auto account_balance_prop_def =
      std::make_unique<SimpleGraphPropertyDefinition>(
          balance_property_dcl.get(), "balance");
  InternalPropertyGraph::InternalSetResolvedExpr(account_balance_prop_def.get(),
                                                 balance_col_ref.get());

  property_defs_account.push_back(std::move(account_balance_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(balance_col_ref));

  auto account_node_table = std::make_unique<const SimpleGraphNodeTable>(
      account->Name(), property_graph_name_path, account, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{account_label.get(),
                                                    id_label.get()},
      std::move(property_defs_account));

  // Syndicate node table
  auto syndicateid_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), syndicate->FindColumnByName("syndicateId"));
  auto syndicateid_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      syndicateid_property_dcl.get(), "syndicateId");
  InternalPropertyGraph::InternalSetResolvedExpr(syndicateid_prop_def.get(),
                                                 syndicateid_col_ref.get());

  property_defs_syndicate.push_back(std::move(syndicateid_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(syndicateid_col_ref));

  auto syndicatename_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), syndicate->FindColumnByName("syndicateName"));
  auto syndicatename_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      syndicatename_property_dcl.get(), "syndicateName");
  InternalPropertyGraph::InternalSetResolvedExpr(syndicatename_prop_def.get(),
                                                 syndicatename_col_ref.get());

  property_defs_syndicate.push_back(std::move(syndicatename_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(syndicatename_col_ref));

  auto syndicatedata_col_ref = MakeResolvedCatalogColumnRef(
      int64array_type_, syndicate->FindColumnByName("syndicateData"));
  auto syndicatedata_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      syndicatedata_property_dcl.get(), "syndicateData");
  InternalPropertyGraph::InternalSetResolvedExpr(syndicatedata_prop_def.get(),
                                                 syndicatedata_col_ref.get());

  property_defs_syndicate.push_back(std::move(syndicatedata_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(syndicatedata_col_ref));

  auto syndicate_node_table = std::make_unique<const SimpleGraphNodeTable>(
      syndicate->Name(), property_graph_name_path, syndicate,
      std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{syndicate_label.get()},
      std::move(property_defs_syndicate));

  // PersonOwnAccount edge table
  auto pa_personid_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person_own_acc->FindColumnByName("personId"));
  auto pa_personid_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      personId_property_dcl.get(), "personId");
  InternalPropertyGraph::InternalSetResolvedExpr(pa_personid_prop_def.get(),
                                                 pa_personid_col_ref.get());

  property_defs_person_own_acc.push_back(std::move(pa_personid_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(pa_personid_col_ref));

  auto pa_accountid_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person_own_acc->FindColumnByName("accountId"));
  auto pa_accountid_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      accountId_property_dcl.get(), "accountId");
  InternalPropertyGraph::InternalSetResolvedExpr(pa_accountid_prop_def.get(),
                                                 pa_accountid_col_ref.get());

  property_defs_person_own_acc.push_back(std::move(pa_accountid_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(pa_accountid_col_ref));

  auto person_own_acc_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      person_own_acc->Name(), property_graph_name_path, person_own_acc,
      std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{person_own_acc_label.get()},
      std::move(property_defs_person_own_acc),
      std::make_unique<const SimpleGraphNodeTableReference>(
          person_node_table.get(), std::vector<int>{0}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{1}, std::vector<int>{0}));

  // AccountTransferAccount edge table
  auto transfer_account_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), transfer->FindColumnByName("fromAccountId"));
  auto transfer_account_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          accountId_property_dcl.get(), "fromAccountId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      transfer_account_id_property.get(), transfer_account_id_col_ref.get());
  property_defs_transfer.emplace_back(std::move(transfer_account_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(transfer_account_id_col_ref));

  auto transfer_target_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), transfer->FindColumnByName("toAccountId"));
  auto transfer_target_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          target_account_id_property_dcl.get(), "toAccountId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      transfer_target_id_property.get(), transfer_target_id_col_ref.get());
  property_defs_transfer.emplace_back(std::move(transfer_target_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(transfer_target_id_col_ref));

  auto amount_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_uint64(), transfer->FindColumnByName("amount"));
  auto amount_property = std::make_unique<SimpleGraphPropertyDefinition>(
      amount_property_dcl.get(), "amount");
  InternalPropertyGraph::InternalSetResolvedExpr(amount_property.get(),
                                                 amount_col_ref.get());
  property_defs_transfer.emplace_back(std::move(amount_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(amount_col_ref));

  auto transfer_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      transfer->Name(), property_graph_name_path, transfer,
      std::vector<int>{0, 1, 2},
      absl::flat_hash_set<const GraphElementLabel*>{transfer_label.get()},
      std::move(property_defs_transfer),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{1}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{2}, std::vector<int>{0}));

  // property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(person_node_table));
  node_tables.push_back(std::move(account_node_table));
  node_tables.push_back(std::move(syndicate_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(person_own_acc_edge_table));
  edge_tables.push_back(std::move(transfer_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(person_label));
  labels.push_back(std::move(account_label));
  labels.push_back(std::move(person_own_acc_label));
  labels.push_back(std::move(transfer_label));
  labels.push_back(std::move(id_label));
  labels.push_back(std::move(syndicate_label));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(id_property_dcl));
  property_dcls.push_back(std::move(personId_property_dcl));
  property_dcls.push_back(std::move(accountId_property_dcl));
  property_dcls.push_back(std::move(target_account_id_property_dcl));
  property_dcls.push_back(std::move(amount_property_dcl));
  property_dcls.push_back(std::move(name_property_dcl));
  property_dcls.push_back(std::move(bday_property_dcl));
  property_dcls.push_back(std::move(balance_property_dcl));
  property_dcls.push_back(std::move(age_property_dcl));
  property_dcls.push_back(std::move(data_property_dcl));
  property_dcls.push_back(std::move(syndicateid_property_dcl));
  property_dcls.push_back(std::move(syndicatename_property_dcl));
  property_dcls.push_back(std::move(syndicatedata_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));
  catalog_->AddOwnedPropertyGraph(std::move(property_graph));

  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadEnhancedAmlPropertyGraph() {
  std::vector<std::string> property_graph_name_path{"aml_enhanced"};

  const Table* person = nullptr;
  const Table* account = nullptr;
  const Table* city = nullptr;
  const Table* country = nullptr;
  const Table* person_own_acc = nullptr;
  const Table* transfer = nullptr;
  const Table* knows = nullptr;
  const Table* located_in = nullptr;

  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"Person"}, &person));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"Account"}, &account));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"City"}, &city));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"Country"}, &country));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"PersonOwnAccount"}, &person_own_acc));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"Transfer"}, &transfer));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"Knows"}, &knows));
  GOOGLESQL_RET_CHECK_OK(catalog_->FindTable({"LocatedIn"}, &located_in));

  // Property Declaration
  auto id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "id", property_graph_name_path, types_->get_int64());
  auto personId_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "personId", property_graph_name_path, types_->get_int64());
  auto to_personId_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "toPersonId", property_graph_name_path, types_->get_int64());
  auto accountId_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "accountId", property_graph_name_path, types_->get_int64());
  auto target_account_id_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "targetAccountId", property_graph_name_path, types_->get_int64());
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", property_graph_name_path, types_->get_string());
  auto city_id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "cityId", property_graph_name_path, types_->get_int64());
  auto country_id_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "countryId", property_graph_name_path, types_->get_int64());

  // Labels
  auto property_dcls_person =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          name_property_dcl.get()};
  auto person_label = std::make_unique<SimpleGraphElementLabel>(
      "Person", property_graph_name_path, property_dcls_person);

  auto property_dcls_account =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get()};
  auto account_label = std::make_unique<SimpleGraphElementLabel>(
      "Account", property_graph_name_path, property_dcls_account);

  auto property_dcls_city =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          city_id_property_dcl.get()};
  auto city_label = std::make_unique<SimpleGraphElementLabel>(
      "City", property_graph_name_path, property_dcls_city);

  auto property_dcls_country =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          country_id_property_dcl.get()};
  auto country_label = std::make_unique<SimpleGraphElementLabel>(
      "Country", property_graph_name_path, property_dcls_country);

  auto property_dcls_person_own_acc =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          personId_property_dcl.get(), accountId_property_dcl.get()};
  auto person_own_acc_label = std::make_unique<SimpleGraphElementLabel>(
      "PersonOwnAccount", property_graph_name_path,
      property_dcls_person_own_acc);

  auto property_dcls_transfer =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          accountId_property_dcl.get(), target_account_id_property_dcl.get()};
  auto transfer_label = std::make_unique<SimpleGraphElementLabel>(
      "Transfer", property_graph_name_path, property_dcls_transfer);

  auto property_dcls_knows =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          personId_property_dcl.get(), to_personId_property_dcl.get()};
  auto knows_label = std::make_unique<SimpleGraphElementLabel>(
      "Knows", property_graph_name_path, property_dcls_knows);

  auto located_in_label = std::make_unique<SimpleGraphElementLabel>(
      "LocatedIn", property_graph_name_path, property_dcls_knows);

  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_person, property_defs_account, property_defs_city,
      property_defs_country, property_defs_person_own_acc,
      property_defs_transfer, property_defs_knows, property_defs_located_in;

  // Person node table
  auto person_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person->FindColumnByName("id"));
  auto person_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(person_id_prop_def.get(),
                                                 person_id_col_ref.get());

  property_defs_person.push_back(std::move(person_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_id_col_ref));

  auto person_name_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), person->FindColumnByName("name"));
  auto person_name_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      name_property_dcl.get(), "name");
  InternalPropertyGraph::InternalSetResolvedExpr(person_name_prop_def.get(),
                                                 person_name_col_ref.get());

  property_defs_person.push_back(std::move(person_name_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(person_name_col_ref));

  auto person_node_table = std::make_unique<const SimpleGraphNodeTable>(
      person->Name(), property_graph_name_path, person, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{person_label.get()},
      std::move(property_defs_person));

  // Account node table
  auto account_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), account->FindColumnByName("id"));
  auto account_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(account_id_prop_def.get(),
                                                 account_id_col_ref.get());

  property_defs_account.push_back(std::move(account_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(account_id_col_ref));

  auto account_node_table = std::make_unique<const SimpleGraphNodeTable>(
      account->Name(), property_graph_name_path, account, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{account_label.get()},
      std::move(property_defs_account));

  // City node table
  auto city_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), city->FindColumnByName("id"));
  auto city_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      city_id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(city_id_prop_def.get(),
                                                 city_id_col_ref.get());

  property_defs_city.push_back(std::move(city_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(city_id_col_ref));

  auto city_name_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), city->FindColumnByName("name"));
  auto city_name_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      name_property_dcl.get(), "name");
  InternalPropertyGraph::InternalSetResolvedExpr(city_name_prop_def.get(),
                                                 city_name_col_ref.get());

  property_defs_city.push_back(std::move(city_name_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(city_name_col_ref));

  auto city_node_table = std::make_unique<const SimpleGraphNodeTable>(
      city->Name(), property_graph_name_path, city, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{city_label.get()},
      std::move(property_defs_city));

  // Country node table
  auto country_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), country->FindColumnByName("id"));
  auto country_id_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      country_id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(country_id_prop_def.get(),
                                                 country_id_col_ref.get());

  property_defs_country.push_back(std::move(country_id_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(country_id_col_ref));

  auto country_name_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), country->FindColumnByName("name"));
  auto country_name_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      name_property_dcl.get(), "name");
  InternalPropertyGraph::InternalSetResolvedExpr(country_name_prop_def.get(),
                                                 country_name_col_ref.get());

  property_defs_country.push_back(std::move(country_name_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(country_name_col_ref));

  auto country_node_table = std::make_unique<const SimpleGraphNodeTable>(
      country->Name(), property_graph_name_path, country, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{country_label.get()},
      std::move(property_defs_country));

  // PersonOwnAccount edge table
  auto pa_personid_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person_own_acc->FindColumnByName("personId"));
  auto pa_personid_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      personId_property_dcl.get(), "personId");
  InternalPropertyGraph::InternalSetResolvedExpr(pa_personid_prop_def.get(),
                                                 pa_personid_col_ref.get());

  property_defs_person_own_acc.push_back(std::move(pa_personid_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(pa_personid_col_ref));

  auto pa_accountid_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), person_own_acc->FindColumnByName("accountId"));
  auto pa_accountid_prop_def = std::make_unique<SimpleGraphPropertyDefinition>(
      accountId_property_dcl.get(), "accountId");
  InternalPropertyGraph::InternalSetResolvedExpr(pa_accountid_prop_def.get(),
                                                 pa_accountid_col_ref.get());

  property_defs_person_own_acc.push_back(std::move(pa_accountid_prop_def));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(pa_accountid_col_ref));
  property_defs_person_own_acc.emplace_back(
      std::make_unique<SimpleGraphPropertyDefinition>(
          personId_property_dcl.get(), "personId"));

  auto person_own_acc_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      person_own_acc->Name(), property_graph_name_path, person_own_acc,
      std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{person_own_acc_label.get()},
      std::move(property_defs_person_own_acc),
      std::make_unique<const SimpleGraphNodeTableReference>(
          person_node_table.get(), std::vector<int>{0}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{1}, std::vector<int>{0}));

  // AccountTransferAccount edge table
  auto transfer_account_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), transfer->FindColumnByName("fromAccountId"));
  auto transfer_account_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          accountId_property_dcl.get(), "fromAccountId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      transfer_account_id_property.get(), transfer_account_id_col_ref.get());
  property_defs_transfer.emplace_back(std::move(transfer_account_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(transfer_account_id_col_ref));

  auto transfer_target_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), transfer->FindColumnByName("toAccountId"));
  auto transfer_target_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          target_account_id_property_dcl.get(), "toAccountId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      transfer_target_id_property.get(), transfer_target_id_col_ref.get());
  property_defs_transfer.emplace_back(std::move(transfer_target_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(transfer_target_id_col_ref));

  auto transfer_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      transfer->Name(), property_graph_name_path, transfer,
      std::vector<int>{0, 1, 2},
      absl::flat_hash_set<const GraphElementLabel*>{transfer_label.get()},
      std::move(property_defs_transfer),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{1}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          account_node_table.get(), std::vector<int>{2}, std::vector<int>{0}));

  // PersonKnowsPerson edge table
  auto knows_person_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), knows->FindColumnByName("personId"));
  auto knows_person_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          personId_property_dcl.get(), "personId");
  InternalPropertyGraph::InternalSetResolvedExpr(knows_person_id_property.get(),
                                                 knows_person_id_col_ref.get());
  property_defs_knows.emplace_back(std::move(knows_person_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(knows_person_id_col_ref));

  auto knows_target_person_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), knows->FindColumnByName("toPersonId"));
  auto knows_target_person_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          to_personId_property_dcl.get(), "toPersonId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      knows_target_person_id_property.get(),
      knows_target_person_id_col_ref.get());
  property_defs_knows.emplace_back(std::move(knows_target_person_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(knows_target_person_id_col_ref));

  auto knows_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      knows->Name(), property_graph_name_path, knows, std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{knows_label.get()},
      std::move(property_defs_knows),
      std::make_unique<const SimpleGraphNodeTableReference>(
          person_node_table.get(), std::vector<int>{0}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          person_node_table.get(), std::vector<int>{1}, std::vector<int>{0}));

  // CityLocatedInCountry edge table
  auto located_in_city_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), located_in->FindColumnByName("cityId"));
  auto located_in_city_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          country_id_property_dcl.get(), "countryId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      located_in_city_id_property.get(), located_in_city_id_col_ref.get());

  property_defs_located_in.emplace_back(std::move(located_in_city_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(located_in_city_id_col_ref));

  auto located_in_country_id_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(), located_in->FindColumnByName("countryId"));
  auto located_in_country_id_property =
      std::make_unique<SimpleGraphPropertyDefinition>(
          country_id_property_dcl.get(), "countryId");
  InternalPropertyGraph::InternalSetResolvedExpr(
      located_in_country_id_property.get(),
      located_in_country_id_col_ref.get());
  property_defs_located_in.emplace_back(
      std::move(located_in_country_id_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(located_in_country_id_col_ref));

  auto located_in_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      located_in->Name(), property_graph_name_path, located_in,
      std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{located_in_label.get()},
      std::move(property_defs_located_in),
      std::make_unique<const SimpleGraphNodeTableReference>(
          city_node_table.get(), std::vector<int>{0}, std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          country_node_table.get(), std::vector<int>{1}, std::vector<int>{0}));

  // property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(person_node_table));
  node_tables.push_back(std::move(account_node_table));
  node_tables.push_back(std::move(city_node_table));
  node_tables.push_back(std::move(country_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(person_own_acc_edge_table));
  edge_tables.push_back(std::move(transfer_edge_table));
  edge_tables.push_back(std::move(knows_edge_table));
  edge_tables.push_back(std::move(located_in_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(person_label));
  labels.push_back(std::move(account_label));
  labels.push_back(std::move(city_label));
  labels.push_back(std::move(country_label));
  labels.push_back(std::move(person_own_acc_label));
  labels.push_back(std::move(transfer_label));
  labels.push_back(std::move(knows_label));
  labels.push_back(std::move(located_in_label));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(id_property_dcl));
  property_dcls.push_back(std::move(personId_property_dcl));
  property_dcls.push_back(std::move(to_personId_property_dcl));
  property_dcls.push_back(std::move(accountId_property_dcl));
  property_dcls.push_back(std::move(target_account_id_property_dcl));
  property_dcls.push_back(std::move(name_property_dcl));
  property_dcls.push_back(std::move(city_id_property_dcl));
  property_dcls.push_back(std::move(country_id_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));

  catalog_->AddOwnedPropertyGraph(std::move(property_graph));

  return absl::OkStatus();
}

void SampleCatalogImpl::LoadMultiSrcDstEdgePropertyGraphs() {
  const std::vector<std::string> property_graph_name_path{"aml_multi"};
  typedef std::pair<std::string, const Type*> NameAndType;

  auto* entity = new SimpleTable(
      "Entity", std::vector<NameAndType>{{"id", types_->get_int64()},
                                         {"namespace", types_->get_int64()},
                                         {"version", types_->get_int64()},
                                         {"value", types_->get_string()}});
  GOOGLESQL_CHECK_OK(entity->SetPrimaryKey({0, 1, 2}));
  AddOwnedTable(entity);

  auto* relation = new SimpleTable(
      "Relation",
      std::vector<NameAndType>{{"source_entity_id", types_->get_int64()},
                               {"dest_entity_id", types_->get_int64()},
                               {"namespace", types_->get_int64()},
                               {"type", types_->get_int64()},
                               {"value", types_->get_string()}});
  GOOGLESQL_CHECK_OK(relation->SetPrimaryKey({0, 1, 2, 3}));
  AddOwnedTable(relation);

  // Property Declaration
  auto value_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "value", property_graph_name_path, types_->get_string());

  // Labels
  auto property_dcls_person =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          value_property_dcl.get()};
  auto entity_label = std::make_unique<SimpleGraphElementLabel>(
      "Entity", property_graph_name_path, property_dcls_person);
  auto relation_label = std::make_unique<SimpleGraphElementLabel>(
      "RelatesTo", property_graph_name_path, property_dcls_person);

  // Entity node table
  auto entity_value_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), entity->FindColumnByName("value"));
  auto entity_value_property = std::make_unique<SimpleGraphPropertyDefinition>(
      value_property_dcl.get(), "value");
  InternalPropertyGraph::InternalSetResolvedExpr(entity_value_property.get(),
                                                 entity_value_column_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_entity;
  property_defs_entity.push_back(std::move(entity_value_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(entity_value_column_ref));
  // Entity
  // KEYS (id, version)
  // PROPERTIES (value)
  //
  // Intentionally skipped `namespace` column which is not scanned by `Entity`
  // node scan, but it will be used in `Relation` edge scan.
  auto entity_node_table = std::make_unique<const SimpleGraphNodeTable>(
      entity->Name(), property_graph_name_path, entity, std::vector<int>{0, 2},
      absl::flat_hash_set<const GraphElementLabel*>{entity_label.get()},
      std::move(property_defs_entity));

  // Relation edge table
  auto relation_value_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), relation->FindColumnByName("value"));
  auto relation_value_property =
      std::make_unique<SimpleGraphPropertyDefinition>(value_property_dcl.get(),
                                                      "value");
  InternalPropertyGraph::InternalSetResolvedExpr(
      relation_value_property.get(), relation_value_column_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_relation;
  property_defs_relation.push_back(std::move(relation_value_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(relation_value_column_ref));

  // KEYS (source_entity_id, dest_entity_id, type)
  const std::vector<int> element_keys = {0, 1, 3};
  // SOURCE KEY (source_entity_id, namespace)
  // REFERENCES Entity(id, namespace)
  const std::vector<int> source_reference_edge_columns = {0, 2};
  const std::vector<int> source_reference_node_columns = {0, 1};
  // DESTINATION KEY (dest_entity_id, namespace)
  // REFERENCES Entity(id, namespace)
  const std::vector<int> dest_reference_edge_columns = {1, 2};
  const std::vector<int> dest_reference_node_columns = {0, 1};
  auto relation_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      relation->Name(), property_graph_name_path, relation, element_keys,
      absl::flat_hash_set<const GraphElementLabel*>{relation_label.get()},
      std::move(property_defs_relation),
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), source_reference_edge_columns,
          source_reference_node_columns),
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), dest_reference_edge_columns,
          dest_reference_node_columns));

  // property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(entity_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(relation_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(entity_label));
  labels.push_back(std::move(relation_label));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(value_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));
  catalog_->AddOwnedPropertyGraph(std::move(property_graph));
}

// The loading function is based on the schema below, featuring dynamic
// multi-labels for nodes, single label for edges and properties.
//
// CREATE TABLE DynamicMultiLabelGraphNode (
//   id INT64 PRIMARY KEY,
//   nodeLabelCol ARRAY<STRING>,
//   code INT64,
//   nodeJsonProp JSON
// );
//
// CREATE TABLE DynamicSingleLabelGraphEdge (
//   id INT64 PRIMARY KEY,
//   dst_id INT64,
//   edgeLabelCol STRING,
//   category STRING,
//   edgeJsonProp JSON
// );
//
// CREATE PROPERTY GRAPH aml_dynamic_multi
//  NODE TABLES(
//   DynamicMultiLabelGraphNode
//     DEFAULT LABEL PROPERTIES ALL COLUMNS
//     LABEL Entity PROPERTIES (code)
//     DYNAMIC LABEL (nodeLabelCol)
//     DYNAMIC PROPERTIES (nodeJsonProp)
//  )
//  EDGE TABLES(
//   DynamicSingleLabelGraphEdge
//     SOURCE KEY (id) REFERENCES DynamicMultiLabelGraphNode(id)
//     DESTINATION KEY (dst_id) REFERENCES DynamicMultiLabelGraphNode(id)
//     DEFAULT LABEL PROPERTIES ALL COLUMNS
//     LABEL Connection PROPERTIES (category)
//     DYNAMIC LABEL (edgeLabelCol)
//     DYNAMIC PROPERTIES (edgeJsonProp)
//  );

void SampleCatalogImpl::LoadPropertyGraphWithDynamicMultiLabelsAndProperties() {
  const std::vector<std::string> property_graph_name_path{"aml_dynamic_multi"};
  typedef std::pair<std::string, const Type*> NameAndType;

  // Define the node table with an ARRAY<STRING> for multi-label.
  auto* graph_dynamic_node_source_table = new SimpleTable(
      "DynamicMultiLabelGraphNode",
      std::vector<NameAndType>{{"id", types_->get_int64()},
                               {"nodeLabelCol", types::StringArrayType()},
                               {"code", types_->get_int64()},
                               {"nodeJsonProp", types_->get_json()}});
  GOOGLESQL_CHECK_OK(graph_dynamic_node_source_table->SetPrimaryKey({0}));
  AddOwnedTable(graph_dynamic_node_source_table);

  auto* graph_dynamic_edge_source_table = new SimpleTable(
      "DynamicSingleLabelGraphEdge",
      std::vector<NameAndType>{{"id", types_->get_int64()},
                               {"dst_id", types_->get_int64()},
                               {"edgeLabelCol", types_->get_string()},
                               {"category", types_->get_string()},
                               {"edgeJsonProp", types_->get_json()}});
  GOOGLESQL_CHECK_OK(graph_dynamic_edge_source_table->SetPrimaryKey({0, 1}));
  AddOwnedTable(graph_dynamic_edge_source_table);

  // Property declarations
  auto id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "id", property_graph_name_path, types_->get_int64());
  auto dst_id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "dst_id", property_graph_name_path, types_->get_int64());
  // The nodeLabelCol property is now an array of strings.
  auto node_label_col_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "nodeLabelCol", property_graph_name_path, types::StringArrayType());
  auto edge_label_col_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "edgeLabelCol", property_graph_name_path, types_->get_string());
  auto code_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "code", property_graph_name_path, types_->get_int64());
  auto node_json_prop_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "nodeJsonProp", property_graph_name_path, types_->get_json());
  auto edge_json_prop_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "edgeJsonProp", property_graph_name_path, types_->get_json());
  auto category_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "category", property_graph_name_path, types_->get_string());

  // Labels
  auto graph_node_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "DynamicMultiLabelGraphNode", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get(), node_label_col_property_dcl.get(),
          code_property_dcl.get(), node_json_prop_property_dcl.get()});

  auto graph_edge_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "DynamicSingleLabelGraphEdge", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get(), dst_id_property_dcl.get(),
          edge_label_col_property_dcl.get(), category_property_dcl.get(),
          edge_json_prop_property_dcl.get()});

  auto entity_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "Entity", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          code_property_dcl.get()});

  auto connection_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "Connection", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          category_property_dcl.get()});

  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_node, property_defs_edge;

  // DynamicMultiLabelGraphNode node table static properties definitions.
  auto graph_node_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_node_source_table->FindColumnByName("id"));
  auto graph_node_id_prop_ref = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_id_prop_ref.get(), graph_node_id_column_ref.get());
  property_defs_node.push_back(std::move(graph_node_id_prop_ref));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_id_column_ref));

  // Resolve the nodeLabelCol with the updated ARRAY<STRING> type.
  auto graph_node_label_col_ref = MakeResolvedCatalogColumnRef(
      types::StringArrayType(),
      graph_dynamic_node_source_table->FindColumnByName("nodeLabelCol"));
  const ResolvedExpr* graph_node_label_col_ref_ptr =
      graph_node_label_col_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_label_col_ref));

  auto graph_node_label_col_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          node_label_col_property_dcl.get(), "nodeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_label_col_prop_ref.get(), graph_node_label_col_ref_ptr);
  property_defs_node.push_back(std::move(graph_node_label_col_prop_ref));

  auto graph_node_code_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_node_source_table->FindColumnByName("code"));
  auto graph_node_code_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(code_property_dcl.get(),
                                                      "code");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_code_prop_ref.get(), graph_node_code_column_ref.get());
  property_defs_node.push_back(std::move(graph_node_code_prop_ref));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_code_column_ref));

  auto graph_node_json_prop_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_json(),
      graph_dynamic_node_source_table->FindColumnByName("nodeJsonProp"));
  const ResolvedExpr* graph_node_json_prop_column_ref_ptr =
      graph_node_json_prop_column_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_json_prop_column_ref));

  auto graph_node_json_prop_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          node_json_prop_property_dcl.get(), "nodeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_json_prop_prop_ref.get(), graph_node_json_prop_column_ref_ptr);
  property_defs_node.push_back(std::move(graph_node_json_prop_prop_ref));

  // DynamicMultiLabelGraphNode dynamic label.
  auto graph_node_dynamic_label =
      std::make_unique<SimpleGraphDynamicLabel>("nodeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(graph_node_dynamic_label.get(),
                                                 graph_node_label_col_ref_ptr);

  // DynamicMultiLabelGraphNode dynamic properties.
  auto graph_node_dynamic_properties =
      std::make_unique<SimpleGraphDynamicProperties>("nodeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_dynamic_properties.get(), graph_node_json_prop_column_ref_ptr);

  auto dynamic_graph_node_table = std::make_unique<const SimpleGraphNodeTable>(
      graph_dynamic_node_source_table->Name(), property_graph_name_path,
      graph_dynamic_node_source_table, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{graph_node_label_dcl.get(),
                                                    entity_label_dcl.get()},
      std::move(property_defs_node), std::move(graph_node_dynamic_label),
      std::move(graph_node_dynamic_properties));

  // DynamicSingleLabelGraphEdge edge table static properties definitions.
  auto graph_edge_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_edge_source_table->FindColumnByName("id"));
  auto graph_edge_id_prop_ref = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_id_prop_ref.get(), graph_edge_id_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_id_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_id_prop_ref));

  auto graph_edge_dst_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_edge_source_table->FindColumnByName("dst_id"));
  auto graph_edge_dst_id_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(dst_id_property_dcl.get(),
                                                      "dst_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_dst_id_prop_ref.get(), graph_edge_dst_id_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_dst_id_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_dst_id_prop_ref));

  auto graph_edge_label_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(),
      graph_dynamic_edge_source_table->FindColumnByName("edgeLabelCol"));
  const ResolvedExpr* graph_edge_label_col_ref_ptr =
      graph_edge_label_col_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_label_col_ref));
  auto graph_edge_label_col_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          edge_label_col_property_dcl.get(), "edgeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_label_col_prop_ref.get(), graph_edge_label_col_ref_ptr);
  property_defs_edge.push_back(std::move(graph_edge_label_col_prop_ref));

  auto graph_edge_category_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(),
      graph_dynamic_edge_source_table->FindColumnByName("category"));
  auto graph_edge_category_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          category_property_dcl.get(), "category");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_category_prop_ref.get(), graph_edge_category_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_category_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_category_prop_ref));

  auto graph_edge_json_prop_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_json(),
      graph_dynamic_edge_source_table->FindColumnByName("edgeJsonProp"));
  const ResolvedExpr* graph_edge_json_prop_column_ref_ptr =
      graph_edge_json_prop_column_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_json_prop_column_ref));
  auto graph_edge_json_prop_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          edge_json_prop_property_dcl.get(), "edgeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_json_prop_prop_ref.get(), graph_edge_json_prop_column_ref_ptr);
  property_defs_edge.push_back(std::move(graph_edge_json_prop_prop_ref));

  // DynamicSingleLabelGraphEdge dynamic label.
  auto graph_edge_dynamic_label =
      std::make_unique<SimpleGraphDynamicLabel>("edgeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(graph_edge_dynamic_label.get(),
                                                 graph_edge_label_col_ref_ptr);

  // DynamicSingleLabelGraphEdge dynamic properties.
  auto graph_edge_dynamic_properties =
      std::make_unique<SimpleGraphDynamicProperties>("edgeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_dynamic_properties.get(), graph_edge_json_prop_column_ref_ptr);

  auto dynamic_graph_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      graph_dynamic_edge_source_table->Name(), property_graph_name_path,
      graph_dynamic_edge_source_table, std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{connection_label_dcl.get(),
                                                    graph_edge_label_dcl.get()},
      std::move(property_defs_edge),
      std::make_unique<const SimpleGraphNodeTableReference>(
          dynamic_graph_node_table.get(), std::vector<int>{0},
          std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          dynamic_graph_node_table.get(), std::vector<int>{1},
          std::vector<int>{0}),
      std::move(graph_edge_dynamic_label),
      std::move(graph_edge_dynamic_properties));

  // Property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(dynamic_graph_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(dynamic_graph_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(graph_node_label_dcl));
  labels.push_back(std::move(entity_label_dcl));
  labels.push_back(std::move(connection_label_dcl));
  labels.push_back(std::move(graph_edge_label_dcl));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(id_property_dcl));
  property_dcls.push_back(std::move(dst_id_property_dcl));
  property_dcls.push_back(std::move(node_label_col_property_dcl));
  property_dcls.push_back(std::move(edge_label_col_property_dcl));
  property_dcls.push_back(std::move(code_property_dcl));
  property_dcls.push_back(std::move(node_json_prop_property_dcl));
  property_dcls.push_back(std::move(edge_json_prop_property_dcl));
  property_dcls.push_back(std::move(category_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));
  catalog_->AddOwnedPropertyGraph(std::move(property_graph));
}

// The loading function is based on below schema with dynamic label and
// properties.
//
// CREATE TABLE DynamicGraphNode (
//   id INT64 PRIMARY KEY,
//   nodeLabelCol STRING,
//   code INT64,
//   nodeJsonProp JSON
// )
//
// CREATE TABLE DynamicGraphEdge (
//   id INT64 PRIMARY KEY,
//   dst_id INT64,
//   edgeLabelCol STRING,
//   category STRING,
//   edgeJsonProp JSON
// )
//
// CREATE PROPERTY GRAPH aml_dynamic
//  NODE TABLES(
//   DynamicGraphNode
//     DEFAULT LABEL PROPERTIES ALL COLUMNS
//     LABEL Entity PROPERTIES (code)
//     DYNAMIC LABEL (nodeLabelCol)
//     DYNAMIC PROPERTIES (nodeJsonProp)
//  )
//  EDGE TABLES(
//   DynamicGraphEdge
//     SOURCE KEY (id) REFERENCES DynamicGraphNode(id)
//     DESTINATION KEY (dst_id) REFERENCES DynamicGraphNode(id)
//     DEFAULT LABEL PROPERTIES ALL COLUMNS
//     LABEL Connection PROPERTIES (category)
//     DYNAMIC LABEL (edgeLabelCol)
//     DYNAMIC PROPERTIES (edgeJsonProp)
//  )
void SampleCatalogImpl::LoadPropertyGraphWithDynamicLabelAndProperties() {
  const std::vector<std::string> property_graph_name_path{"aml_dynamic"};
  typedef std::pair<std::string, const Type*> NameAndType;

  auto* graph_dynamic_node_source_table = new SimpleTable(
      "DynamicGraphNode",
      std::vector<NameAndType>{{"id", types_->get_int64()},
                               {"nodeLabelCol", types_->get_string()},
                               {"code", types_->get_int64()},
                               {"nodeJsonProp", types_->get_json()}});
  GOOGLESQL_CHECK_OK(graph_dynamic_node_source_table->SetPrimaryKey({0}));
  AddOwnedTable(graph_dynamic_node_source_table);

  auto* graph_dynamic_edge_source_table = new SimpleTable(
      "DynamicGraphEdge",
      std::vector<NameAndType>{{"id", types_->get_int64()},
                               {"dst_id", types_->get_int64()},
                               {"edgeLabelCol", types_->get_string()},
                               {"category", types_->get_string()},
                               {"edgeJsonProp", types_->get_json()}});
  GOOGLESQL_CHECK_OK(graph_dynamic_edge_source_table->SetPrimaryKey({0, 1}));
  AddOwnedTable(graph_dynamic_edge_source_table);

  // Property declaration
  auto id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "id", property_graph_name_path, types_->get_int64());
  auto dst_id_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "dst_id", property_graph_name_path, types_->get_int64());
  auto node_label_col_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "nodeLabelCol", property_graph_name_path, types_->get_string());
  auto edge_label_col_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "edgeLabelCol", property_graph_name_path, types_->get_string());

  auto code_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "code", property_graph_name_path, types_->get_int64());
  auto node_json_prop_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "nodeJsonProp", property_graph_name_path, types_->get_json());
  auto edge_json_prop_property_dcl =
      std::make_unique<SimpleGraphPropertyDeclaration>(
          "edgeJsonProp", property_graph_name_path, types_->get_json());
  auto name_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "name", property_graph_name_path, types_->get_string());
  auto category_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "category", property_graph_name_path, types_->get_string());

  // Labels
  auto graph_node_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "DynamicGraphNode", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get(), node_label_col_property_dcl.get(),
          code_property_dcl.get(), node_json_prop_property_dcl.get()});

  auto graph_edge_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "DynamicGraphEdge", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          id_property_dcl.get(), dst_id_property_dcl.get(),
          edge_label_col_property_dcl.get(), category_property_dcl.get(),
          edge_json_prop_property_dcl.get()});

  auto entity_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "Entity", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          code_property_dcl.get()});

  auto connection_label_dcl = std::make_unique<SimpleGraphElementLabel>(
      "CONNECTION", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          category_property_dcl.get()});

  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_node, property_defs_edge;

  // DynamicGraphNode node table static properties definitions.
  auto graph_node_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_node_source_table->FindColumnByName("id"));
  auto graph_node_id_prop_ref = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_id_prop_ref.get(), graph_node_id_column_ref.get());
  property_defs_node.push_back(std::move(graph_node_id_prop_ref));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_id_column_ref));

  auto graph_node_label_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(),
      graph_dynamic_node_source_table->FindColumnByName("nodeLabelCol"));
  // Take pointer before moving so the same ResolvedExpr can be shared.
  const ResolvedExpr* graph_node_label_col_ref_ptr =
      graph_node_label_col_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_label_col_ref));

  auto graph_node_label_col_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          node_label_col_property_dcl.get(), "nodeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_label_col_prop_ref.get(), graph_node_label_col_ref_ptr);
  property_defs_node.push_back(std::move(graph_node_label_col_prop_ref));

  auto graph_node_code_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_node_source_table->FindColumnByName("code"));
  auto graph_node_code_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(code_property_dcl.get(),
                                                      "code");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_code_prop_ref.get(), graph_node_code_column_ref.get());
  property_defs_node.push_back(std::move(graph_node_code_prop_ref));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_code_column_ref));

  auto graph_node_json_prop_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_json(),
      graph_dynamic_node_source_table->FindColumnByName("nodeJsonProp"));
  const ResolvedExpr* graph_node_json_prop_column_ref_ptr =
      graph_node_json_prop_column_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_node_json_prop_column_ref));

  auto graph_node_json_prop_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          node_json_prop_property_dcl.get(), "nodeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_json_prop_prop_ref.get(), graph_node_json_prop_column_ref_ptr);
  property_defs_node.push_back(std::move(graph_node_json_prop_prop_ref));

  // DynamicGraphNode dynamic label.
  auto graph_node_dynamic_label =
      std::make_unique<SimpleGraphDynamicLabel>("nodeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(graph_node_dynamic_label.get(),
                                                 graph_node_label_col_ref_ptr);

  // DynamicGraphNode dynamic properties.
  auto graph_node_dynamic_properties =
      std::make_unique<SimpleGraphDynamicProperties>("nodeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_node_dynamic_properties.get(), graph_node_json_prop_column_ref_ptr);

  auto dynamic_graph_node_table = std::make_unique<const SimpleGraphNodeTable>(
      graph_dynamic_node_source_table->Name(), property_graph_name_path,
      graph_dynamic_node_source_table, std::vector<int>{0},
      absl::flat_hash_set<const GraphElementLabel*>{graph_node_label_dcl.get(),
                                                    entity_label_dcl.get()},
      std::move(property_defs_node), std::move(graph_node_dynamic_label),
      std::move(graph_node_dynamic_properties));

  // DynamicGraphEdge edge table static properties definitions.
  auto graph_edge_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_edge_source_table->FindColumnByName("id"));
  auto graph_edge_id_prop_ref = std::make_unique<SimpleGraphPropertyDefinition>(
      id_property_dcl.get(), "id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_id_prop_ref.get(), graph_edge_id_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_id_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_id_prop_ref));

  auto graph_edge_dst_id_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_int64(),
      graph_dynamic_edge_source_table->FindColumnByName("dst_id"));
  auto graph_edge_dst_id_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(dst_id_property_dcl.get(),
                                                      "dst_id");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_dst_id_prop_ref.get(), graph_edge_dst_id_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_dst_id_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_dst_id_prop_ref));

  auto graph_edge_label_col_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(),
      graph_dynamic_edge_source_table->FindColumnByName("edgeLabelCol"));
  const ResolvedExpr* graph_edge_label_col_ref_ptr =
      graph_edge_label_col_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_label_col_ref));
  auto graph_edge_label_col_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          edge_label_col_property_dcl.get(), "edgeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_label_col_prop_ref.get(), graph_edge_label_col_ref_ptr);
  property_defs_edge.push_back(std::move(graph_edge_label_col_prop_ref));

  auto graph_edge_category_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(),
      graph_dynamic_edge_source_table->FindColumnByName("category"));
  auto graph_edge_category_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          category_property_dcl.get(), "category");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_category_prop_ref.get(), graph_edge_category_column_ref.get());
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_category_column_ref));
  property_defs_edge.push_back(std::move(graph_edge_category_prop_ref));

  auto graph_edge_json_prop_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_json(),
      graph_dynamic_edge_source_table->FindColumnByName("edgeJsonProp"));
  const ResolvedExpr* graph_edge_json_prop_column_ref_ptr =
      graph_edge_json_prop_column_ref.get();
  owned_resolved_graph_property_definitions_.push_back(
      std::move(graph_edge_json_prop_column_ref));
  auto graph_edge_json_prop_prop_ref =
      std::make_unique<SimpleGraphPropertyDefinition>(
          edge_json_prop_property_dcl.get(), "edgeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_json_prop_prop_ref.get(), graph_edge_json_prop_column_ref_ptr);
  property_defs_edge.push_back(std::move(graph_edge_json_prop_prop_ref));

  // DynamicGraphEdge dynamic label.
  auto graph_edge_dynamic_label =
      std::make_unique<SimpleGraphDynamicLabel>("edgeLabelCol");
  InternalPropertyGraph::InternalSetResolvedExpr(graph_edge_dynamic_label.get(),
                                                 graph_edge_label_col_ref_ptr);

  // DynamicGraphEdge dynamic properties.
  auto graph_edge_dynamic_properties =
      std::make_unique<SimpleGraphDynamicProperties>("edgeJsonProp");
  InternalPropertyGraph::InternalSetResolvedExpr(
      graph_edge_dynamic_properties.get(), graph_edge_json_prop_column_ref_ptr);

  auto dynamic_graph_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      graph_dynamic_edge_source_table->Name(), property_graph_name_path,
      graph_dynamic_edge_source_table, std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{connection_label_dcl.get(),
                                                    graph_edge_label_dcl.get()},
      std::move(property_defs_edge),
      std::make_unique<const SimpleGraphNodeTableReference>(
          dynamic_graph_node_table.get(), std::vector<int>{0},
          std::vector<int>{0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          dynamic_graph_node_table.get(), std::vector<int>{1},
          std::vector<int>{0}),
      std::move(graph_edge_dynamic_label),
      std::move(graph_edge_dynamic_properties));

  // property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(dynamic_graph_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(dynamic_graph_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(graph_node_label_dcl));
  labels.push_back(std::move(entity_label_dcl));
  labels.push_back(std::move(connection_label_dcl));
  labels.push_back(std::move(graph_edge_label_dcl));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(id_property_dcl));
  property_dcls.push_back(std::move(dst_id_property_dcl));
  property_dcls.push_back(std::move(node_label_col_property_dcl));
  property_dcls.push_back(std::move(edge_label_col_property_dcl));
  property_dcls.push_back(std::move(code_property_dcl));
  property_dcls.push_back(std::move(node_json_prop_property_dcl));
  property_dcls.push_back(std::move(edge_json_prop_property_dcl));
  property_dcls.push_back(std::move(category_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));
  catalog_->AddOwnedPropertyGraph(std::move(property_graph));
}

void SampleCatalogImpl::LoadCompositeKeyPropertyGraphs() {
  std::vector<std::string> property_graph_name_path{"aml_composite_key"};
  typedef std::pair<std::string, const Type*> NameAndType;

  auto* entity = new SimpleTable(
      "CompositeKeyEntity",
      std::vector<NameAndType>{{"key_int", types_->get_int64()},
                               {"key_string", types_->get_string()},
                               {"value", types_->get_string()}});
  GOOGLESQL_CHECK_OK(entity->SetPrimaryKey({0, 1}));
  AddOwnedTable(entity);

  auto* relation = new SimpleTable(
      "CompositeKeyRelation",
      std::vector<NameAndType>{{"source_entity_key_int", types_->get_int64()},
                               {"dest_entity_key_int", types_->get_int64()},
                               {"entity_key_string", types_->get_string()},
                               {"value", types_->get_string()}});
  GOOGLESQL_CHECK_OK(relation->SetPrimaryKey({0, 1, 2}));
  AddOwnedTable(relation);

  // Property Declaration
  auto value_property_dcl = std::make_unique<SimpleGraphPropertyDeclaration>(
      "value", property_graph_name_path, types_->get_string());
  // Labels
  auto property_dcls_value =
      absl::flat_hash_set<const GraphPropertyDeclaration*>{
          value_property_dcl.get()};
  auto entity_label = std::make_unique<SimpleGraphElementLabel>(
      "Entity", property_graph_name_path, property_dcls_value);
  auto relation_label = std::make_unique<SimpleGraphElementLabel>(
      "RelatesTo", property_graph_name_path, property_dcls_value);
  auto value_relation_label = std::make_unique<SimpleGraphElementLabel>(
      "RelatesByValue", property_graph_name_path,
      absl::flat_hash_set<const GraphPropertyDeclaration*>{});

  // Entity node table
  auto entity_value_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), entity->FindColumnByName("value"));
  auto entity_value_property = std::make_unique<SimpleGraphPropertyDefinition>(
      value_property_dcl.get(), "value");
  InternalPropertyGraph::InternalSetResolvedExpr(entity_value_property.get(),
                                                 entity_value_column_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_entity;
  property_defs_entity.push_back(std::move(entity_value_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(entity_value_column_ref));
  auto entity_node_table = std::make_unique<const SimpleGraphNodeTable>(
      entity->Name(), property_graph_name_path, entity, std::vector<int>{0, 1},
      absl::flat_hash_set<const GraphElementLabel*>{entity_label.get()},
      std::move(property_defs_entity));

  // Relation edge table
  auto relation_value_column_ref = MakeResolvedCatalogColumnRef(
      types_->get_string(), relation->FindColumnByName("value"));
  auto relation_value_property =
      std::make_unique<SimpleGraphPropertyDefinition>(value_property_dcl.get(),
                                                      "value");
  InternalPropertyGraph::InternalSetResolvedExpr(
      relation_value_property.get(), relation_value_column_ref.get());
  std::vector<std::unique_ptr<const GraphPropertyDefinition>>
      property_defs_relation;
  property_defs_relation.push_back(std::move(relation_value_property));
  owned_resolved_graph_property_definitions_.push_back(
      std::move(relation_value_column_ref));

  // source reference fully contains the source node's key but is defined in a
  // different order:
  //   SOURCE KEY (entity_key_string, source_entity_key_int)
  //   REFERENCES CompositeKeyEntity (key_string, key_int)
  // destination reference fully contains the destination node's key and is
  // defined in the same order:
  //   DESTINATION KEY (dest_entity_key_int, entity_key_string)
  //   REFERENCES CompositeKeyEntity (key_int, key_string)
  auto relation_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      relation->Name(), property_graph_name_path, relation,
      std::vector<int>{0, 1, 2},
      absl::flat_hash_set<const GraphElementLabel*>{relation_label.get()},
      std::move(property_defs_relation),
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), std::vector<int>{2, 0},
          std::vector<int>{1, 0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), std::vector<int>{1, 2},
          std::vector<int>{0, 1}));

  // source reference contains exactly the source node's key:
  //   SOURCE KEY (entity_key_string, source_entity_key_int)
  //   REFERENCES CompositeKeyEntity (key_string, key_int)
  // destination reference contains more than the destination node's key:
  //   DESTINATION KEY (dest_entity_key_int, entity_key_string, value)
  //   REFERENCES CompositeKeyEntity (key_int, key_string, value)
  auto value_relation_edge_table = std::make_unique<const SimpleGraphEdgeTable>(
      value_relation_label->Name(), property_graph_name_path, relation,
      std::vector<int>{0, 1, 2},
      absl::flat_hash_set<const GraphElementLabel*>{value_relation_label.get()},
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>{},
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), std::vector<int>{2, 0},
          std::vector<int>{1, 0}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          entity_node_table.get(), std::vector<int>{1, 2, 3},
          std::vector<int>{0, 1, 2}));

  // property graph
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  node_tables.push_back(std::move(entity_node_table));

  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  edge_tables.push_back(std::move(relation_edge_table));
  edge_tables.push_back(std::move(value_relation_edge_table));

  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  labels.push_back(std::move(entity_label));
  labels.push_back(std::move(relation_label));
  labels.push_back(std::move(value_relation_label));

  std::vector<std::unique_ptr<const GraphPropertyDeclaration>> property_dcls;
  property_dcls.push_back(std::move(value_property_dcl));

  auto property_graph = std::make_unique<SimplePropertyGraph>(
      std::move(property_graph_name_path), std::move(node_tables),
      std::move(edge_tables), std::move(labels), std::move(property_dcls));
  catalog_->AddOwnedPropertyGraph(std::move(property_graph));
}

namespace {
const char kColumnNameBool[] = "column_bool";
const char kColumnNameBytes[] = "column_bytes";
const char kColumnNameDate[] = "column_date";
const char kColumnNameDouble[] = "column_double";
const char kColumnNameFloat[] = "column_float";
const char kColumnNameInt32[] = "column_int32";
const char kColumnNameInt64[] = "column_int64";
const char kColumnNameString[] = "column_string";
const char kColumnNameTime[] = "column_time";
const char kColumnNameUInt32[] = "column_uint32";
const char kColumnNameUInt64[] = "column_uint64";
const char kColumnNameKey[] = "key";
const char kColumnNameValue[] = "value";
const char kColumnNameFilename[] = "filename";

const char kTypeBool[] = "bool";
const char kTypeBytes[] = "bytes";
const char kTypeDate[] = "date";
const char kTypeDouble[] = "double";
const char kTypeFloat[] = "float";
const char kTypeInt32[] = "int32";
const char kTypeInt64[] = "int64";
const char kTypeString[] = "string";
const char kTypeTime[] = "time";
const char kTypeUInt32[] = "uint32";
const char kTypeUInt64[] = "uint64";
const char kTypeInterval[] = "interval";

struct OutputColumn {
  std::string description;
  std::string name;
  const Type* type;
};

struct TVFArgument {
  std::string description;
  const Type* type;
};

}  // namespace

static std::vector<OutputColumn> GetOutputColumnsForAllTypes(
    TypeFactory* types) {
  return {{kTypeBool, kColumnNameBool, types->get_bool()},
          {kTypeBytes, kColumnNameBytes, types->get_bytes()},
          {kTypeDate, kColumnNameDate, types->get_date()},
          {kTypeDouble, kColumnNameDouble, types->get_double()},
          {kTypeFloat, kColumnNameFloat, types->get_float()},
          {kTypeInt32, kColumnNameInt32, types->get_int32()},
          {kTypeInt64, kColumnNameInt64, types->get_int64()},
          {kTypeString, kColumnNameString, types->get_string()},
          {kTypeTime, kColumnNameTime, types->get_time()},
          {kTypeUInt32, kColumnNameUInt32, types->get_uint32()},
          {kTypeUInt64, kColumnNameUInt64, types->get_uint64()}};
}

static std::vector<TVFArgument> GetTVFArgumentsForAllTypes(TypeFactory* types) {
  return {{kTypeBool, types->get_bool()},
          {kTypeBytes, types->get_bytes()},
          {kTypeDate, types->get_date()},
          {kTypeDouble, types->get_double()},
          {kTypeFloat, types->get_float()},
          {kTypeInt32, types->get_int32()},
          {kTypeInt64, types->get_int64()},
          {kTypeString, types->get_string()},
          {kTypeTime, types->get_time()},
          {kTypeUInt32, types->get_uint32()},
          {kTypeUInt64, types->get_uint64()},
          {kTypeInterval, types->get_interval()}};
}

static TVFRelation GetOutputSchemaWithTwoTypes(
    absl::Span<const OutputColumn> output_columns_for_all_types) {
  TVFRelation::ColumnList columns;
  for (int i = 0; i < 2; ++i) {
    columns.emplace_back(output_columns_for_all_types[i].name,
                         output_columns_for_all_types[i].type);
  }
  return TVFRelation(columns);
}

void SampleCatalogImpl::LoadTableValuedFunctions1() {
  TVFRelation empty_output_schema({});

  const std::vector<OutputColumn> kOutputColumnsAllTypes =
      GetOutputColumnsForAllTypes(types_);

  const std::vector<TVFArgument> kArgumentsAllTypes =
      GetTVFArgumentsForAllTypes(types_);

  TVFRelation output_schema_two_types =
      GetOutputSchemaWithTwoTypes(kOutputColumnsAllTypes);

  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_int64_value_table =
      TVFRelation::ValueTable(types_->get_int64());

  // Generate an output schema that returns a proto value table.
  TVFRelation output_schema_proto_value_table = TVFRelation::ValueTable(
      GetProtoType(googlesql_test::TestExtraPB::descriptor()));

  // Generate an output schema that returns every possible type.
  TVFRelation::ColumnList columns;
  columns.reserve(kOutputColumnsAllTypes.size());
  for (const auto& kv : kOutputColumnsAllTypes) {
    columns.emplace_back(kv.name, kv.type);
  }
  TVFRelation output_schema_all_types(columns);

  // Add a TVF that takes no arguments.
  int context_id = 0;
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      output_schema_two_types));

  // Add a TVF that returns an empty output schema.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_empty_output_schema"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             empty_output_schema,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      empty_output_schema));

  // Add a TVF that takes no arguments and returns all POD types.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_return_all_pod_types"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_all_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      output_schema_all_types));

  // Add a TVF for each POD type that accepts exactly one argument of that type.
  for (const auto& kv : kArgumentsAllTypes) {
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {absl::StrCat("tvf_exactly_1_", kv.description, "_arg")},
        {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                               output_schema_two_types,
                               /*extra_relation_input_columns_allowed=*/false),
                           {FunctionArgumentType(kv.type)}, context_id++)},
        output_schema_two_types));
  }

  // Add TVFs that accept between two and nine INT64 arguments.
  for (int i = 2; i < 10; ++i) {
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {absl::StrCat("tvf_exactly_", i, "_int64_args")},
        {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                               output_schema_two_types,
                               /*extra_relation_input_columns_allowed=*/false),
                           FunctionArgumentTypeList(i, types::Int64Type()),
                           context_id++)},
        output_schema_two_types));
  }

  // For each templated argument type, add a TVF that accepts exactly one
  // argument of that type.
  const std::vector<std::pair<std::string, SignatureArgumentKind>>
      kSignatureArgumentKinds = {
          {"arg_type_any_1", ARG_TYPE_ANY_1},
          {"arg_type_any_2", ARG_TYPE_ANY_2},
          {"arg_array_type_any_1", ARG_ARRAY_TYPE_ANY_1},
          {"arg_array_type_any_2", ARG_ARRAY_TYPE_ANY_2},
          {"arg_proto_any", ARG_PROTO_ANY},
          {"arg_struct_any", ARG_STRUCT_ANY},
          {"arg_enum_any", ARG_ENUM_ANY},
          {"arg_type_relation", ARG_TYPE_RELATION},
          {"arg_type_arbitrary", ARG_TYPE_ARBITRARY},
      };
  for (const std::pair<std::string, SignatureArgumentKind>& kv :
       kSignatureArgumentKinds) {
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {absl::StrCat("tvf_exactly_one_", kv.first)},
        {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                               output_schema_two_types,
                               /*extra_relation_input_columns_allowed=*/false),
                           {FunctionArgumentType(kv.second)}, context_id++)},
        output_schema_two_types));
  }

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_exactly_one_proto"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(proto_KitchenSinkPB_)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with a repeating final argument of type int64.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_repeating_int64_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(types::Int64Type(),
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with a repeating final argument of ARG_TYPE_ANY_1.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_repeating_any_one_type_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(ARG_TYPE_ANY_1,
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with a repeating final argument of ARG_TYPE_ARBITRARY.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_repeating_arbitrary_type_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      output_schema_two_types));

  // Stop here to avoid hitting lint limits for function length.
}

void SampleCatalogImpl::LoadTableValuedFunctions2() {
  TVFRelation empty_output_schema({});

  const std::vector<OutputColumn> kOutputColumnsAllTypes =
      GetOutputColumnsForAllTypes(types_);

  TVFRelation output_schema_two_types =
      GetOutputSchemaWithTwoTypes(kOutputColumnsAllTypes);

  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_int64_value_table =
      TVFRelation::ValueTable(types_->get_int64());

  // Generate an output schema that returns a proto value table.
  TVFRelation output_schema_proto_value_table = TVFRelation::ValueTable(
      GetProtoType(googlesql_test::TestExtraPB::descriptor()));

  TVFRelation output_schema_two_types_with_pseudo_columns(
      {TVFSchemaColumn(kOutputColumnsAllTypes[0].name,
                       kOutputColumnsAllTypes[0].type),
       TVFSchemaColumn(kOutputColumnsAllTypes[1].name,
                       kOutputColumnsAllTypes[1].type),
       TVFSchemaColumn("RowId", types_->get_int64(),
                       /*is_pseudo_column_in=*/true),
       TVFSchemaColumn("PartitionName", types_->get_string(),
                       /*is_pseudo_column_in=*/true)});

  // Generate an output schema that returns an int64 value table.
  TVFRelation output_schema_value_table_with_pseudo_columns =
      TVFRelation::ValueTable(
          GetProtoType(googlesql_test::TestExtraPB::descriptor()),
          {TVFSchemaColumn("RowId", types_->get_int64(),
                           /*is_pseudo_column_in=*/true),
           TVFSchemaColumn("PartitionName", types_->get_string(),
                           /*is_pseudo_column_in=*/true)})
          .value();

  int64_t context_id = 0;

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_arg_returning_fixed_output_with_pseudo_columns"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types_with_pseudo_columns,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      output_schema_two_types_with_pseudo_columns));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_arg_returning_value_table_with_pseudo_columns"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_value_table_with_pseudo_columns,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      output_schema_value_table_with_pseudo_columns));

  // Add a TVF with exactly one relation argument.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_with_fixed_output"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation()}, context_id++)},
      output_schema_two_types));

  // Add a TVF which always returns failure for argument constraint check.
  TVFPostResolutionArgumentConstraintsCallback tvf_argument_check =
      [](const FunctionSignature& signature,
         absl::Span<const TVFInputArgumentType> arguments,
         const LanguageOptions& language_options) {
        return absl::InvalidArgumentError(
            "Invalid argument, this TVF returns failure for argument "
            "constraint check");
      };
  catalog_->AddOwnedTableValuedFunction(new TableValuedFunction(
      {"tvf_with_arg_constraint_check_failure"},
      FunctionSignature(FunctionArgumentType::AnyRelation(), {}, context_id++),
      TableValuedFunctionOptions().set_post_resolution_argument_constraint(
          tvf_argument_check)));

  // Add a TVF with non empty group name
  TVFComputeResultTypeCallback result_type_callback =
      [](Catalog* catalog, TypeFactory* type_factory,
         const FunctionSignature& signature,
         const std::vector<TVFInputArgumentType>& actual_arguments,
         const AnalyzerOptions& analyzer_options) {
        return std::make_unique<TVFSignature>(
            actual_arguments,
            TVFRelation(
                {TVFRelation::Column("col", type_factory->get_string())}),
            TVFSignatureOptions());
      };
  catalog_->AddOwnedTableValuedFunction(new TableValuedFunction(
      {"tvf_with_group_name"}, "Group",
      {FunctionSignature(FunctionArgumentType::AnyRelation(), {},
                         context_id++)},
      TableValuedFunctionOptions().set_compute_result_type_callback(
          result_type_callback)));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_models_with_fixed_output"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              TVFRelation({{"label", types::DoubleType()}}),
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyModel(), FunctionArgumentType::AnyModel()},
          context_id++)},
      TVFRelation({{"label", types::DoubleType()}})));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_one_model_arg_with_fixed_output"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kTypeDouble, types::DoubleType()},
                           {kTypeString, types::StringType()}}),
              /*extra_relation_input_columns_allowed=*/false),
          {
              FunctionArgumentType::RelationWithSchema(
                  TVFRelation({{kColumnNameKey, types::Int64Type()},
                               {kColumnNameValue, types::StringType()}}),
                  /*extra_relation_input_columns_allowed=*/false),
              FunctionArgumentType::AnyModel(),
          },
          context_id++)},
      TVFRelation({{kTypeDouble, types::DoubleType()},
                   {kTypeString, types::StringType()}})));

  // Add a TVF with exactly one relation argument. The output schema is set to
  // be the same as the input schema.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_one_relation_arg_output_schema_is_input_schema"},
      {FunctionSignature(ARG_TYPE_RELATION,
                         {FunctionArgumentType::AnyRelation()},
                         context_id++)}));

  // Add a TVF with exactly one optional relation argument. The output schema is
  // set to be the same as the input schema.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_optional_relation_arg_return_int64_value_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_int64_value_table,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(ARG_TYPE_RELATION,
                                               FunctionArgumentType::OPTIONAL)},
                         context_id++)},
      output_schema_int64_value_table));

  // Add a TVF with one relation argument and one integer argument. The output
  // schema is set to be the same as the input schema of the relation argument.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_one_relation_arg_output_schema_is_input_schema_plus_int64_arg"},
      {FunctionSignature(ARG_TYPE_RELATION,
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType(types::Int64Type())},
                         context_id++)}));

  // Add one TVF with two relation arguments that forwards the schema of the
  // first relation argument to the output of the TVF.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_two_relation_args_output_schema_is_input_schema"},
      {FunctionSignature(ARG_TYPE_RELATION,
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType::AnyRelation()},
                         context_id++)}));

  // Add one TVF with two relation arguments with the second one optional that
  // forwards the schema of the first relation argument to the output of the
  // TVF.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_two_relation_args_second_optional_output_schema_is_input_schema"},
      {FunctionSignature(ARG_TYPE_RELATION,
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType(ARG_TYPE_RELATION,
                                               FunctionArgumentType::OPTIONAL)},
                         context_id++)}));

  // Add one TVF with three arguments: The first one is required model; The
  // second is optional table; The third is optional struct.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_model_evaluation_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyModel(),
                          FunctionArgumentType(ARG_TYPE_RELATION,
                                               FunctionArgumentType::OPTIONAL),
                          FunctionArgumentType(ARG_STRUCT_ANY,
                                               FunctionArgumentType::OPTIONAL)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly two relation arguments.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_relation_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType::AnyRelation()},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly two relation arguments that returns an int64 value
  // table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_relation_args_return_int64_value_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_int64_value_table,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType::AnyRelation()},
                         context_id++)},
      output_schema_int64_value_table));

  // Add a TVF with exactly two relation arguments that returns a proto value
  // table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_relation_args_return_proto_value_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_proto_value_table,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType::AnyRelation()},
                         context_id++)},
      output_schema_proto_value_table));

  // Add a TVF with exactly one argument of ARG_TYPE_RELATION and another
  // argument of type int64.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_int64_arg_one_relation_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(types::Int64Type()),
                          FunctionArgumentType::AnyRelation()},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one argument of ARG_TYPE_RELATION and another
  // argument of type int64.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_one_int64_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType(types::Int64Type())},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument and repeating int64 arguments.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_repeating_int64_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType(types::Int64Type(),
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with one relation argument and also a repeating final argument of
  // ARG_TYPE_ANY_1.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_repeating_any_one_type_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyRelation(),
                          FunctionArgumentType(ARG_TYPE_ANY_1,
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column and one string column.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_int64_string_input_columns"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeInt64, types::Int64Type()},
                                          {kTypeString, types::StringType()}}),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column and one string column, and no extra columns are allowed
  // in the input relation.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_only_int64_string_input_columns"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeInt64, types::Int64Type()},
                                          {kTypeString, types::StringType()}}),
                             /*extra_relation_input_columns_allowed=*/false)},
                         context_id++)},
      output_schema_two_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_only_int64_struct_int64_input_columns"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kTypeInt64, types::Int64Type()},
                           {"struct_int64", struct_with_one_field_type_}}),
              /*extra_relation_input_columns_allowed=*/false)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two relation arguments, one with a required input schema of
  // one uint64 column and one string column, and the other with a required
  // input schema of one date column and one string column.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_relation_args_uint64_string_and_date_string_input_columns"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                              TVFRelation({{kTypeUInt64, types::Uint64Type()},
                                           {kTypeString, types::StringType()}}),
                              /*extra_relation_input_columns_allowed=*/true),
                          FunctionArgumentType::RelationWithSchema(
                              TVFRelation({{kTypeDate, types::DateType()},
                                           {kTypeString, types::StringType()}}),
                              /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF one relation argument with a required input schema of many
  // supported types.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_required_input_schema_many_types"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeBool, types_->get_bool()},
                                          {kTypeBytes, types_->get_bytes()},
                                          {kTypeDate, types_->get_date()},
                                          {kTypeDouble, types_->get_double()},
                                          {kTypeFloat, types_->get_float()},
                                          {kTypeInt32, types_->get_int32()},
                                          {kTypeInt64, types_->get_int64()},
                                          {kTypeString, types_->get_string()},
                                          {kTypeTime, types_->get_timestamp()},
                                          {kTypeUInt32, types_->get_uint32()},
                                          {kTypeUInt64, types_->get_uint64()}}),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  const std::string kMyEnum = "myenum";
  const std::string kMyDate = "mydate";
  const std::string kInt64a = "int64a";
  const std::string kInt64b = "int64b";
  const std::string kInt64c = "int64c";

  // Add a TVF with exactly one relation argument with a required input schema
  // of one enum column.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_one_enum_input_column"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({TVFRelation::Column(
                  kMyEnum,
                  GetEnumType(googlesql_test::TestEnum_descriptor()))}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one date column.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_one_date_input_column"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({TVFRelation::Column(kMyDate, types_->get_date())}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of three int64 columns.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_three_int64_input_columns"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64()),
                           TVFRelation::Column(kInt64b, types_->get_int64()),
                           TVFRelation::Column(kInt64c, types_->get_int64())}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one proto column value table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_input_proto_value_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation::ValueTable(GetProtoType(
                                 googlesql_test::TestExtraPB::descriptor())),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column value table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_int64_input_value_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation::ValueTable(types::Int64Type()),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column value table, and forwards the input schema to the
  // output schema.
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_one_relation_arg_int64_input_value_table_forward_schema"},
      {FunctionSignature(ARG_TYPE_RELATION,
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation::ValueTable(types::Int64Type()),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)}));

  // Add a TVF with exactly one relation argument with a fixed schema that
  // returns a proto value table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_return_proto_value_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_proto_value_table,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation(
                  {TVFRelation::Column(kTypeString, types_->get_string()),
                   TVFRelation::Column(kTypeInt64, types_->get_int64())}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_proto_value_table));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column, and extra input columns are allowed.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_key_input_column_extra_input_columns_allowed"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameKey, types::Int64Type()}}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one string column, and extra input columns are allowed.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_filename_input_column_extra_input_columns_allowed"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameFilename, types::StringType()}}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column, and extra input columns are not allowed.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_key_input_column_extra_input_columns_banned"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameKey, types::Int64Type()}}),
              /*extra_relation_input_columns_allowed=*/false)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one column whose name is "uint32" but whose type is actually uint64.
  // Then it is possible to call this TVF with the SimpleTypes table and type
  // coercion should coerce the provided column named "uint32" to type uint64.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_uint64_input_column_named_uint32"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeUInt32, types::Uint64Type()}}),
                             /*extra_relation_input_columns_allowed=*/true)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument with a required input schema
  // of one int64 column and one string column.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_key_filename_input_columns"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
              TVFRelation({{kColumnNameKey, types::Int64Type()},
                           {kColumnNameFilename, types::StringType()}}),
              /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF that takes two scalar named arguments.
  const auto named_required_format_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "format_string", kPositionalOrNamed));
  const auto named_required_date_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "date_string", kPositionalOrNamed));
  const auto named_required_any_relation_arg = FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_argument_name(
                             "any_relation_arg", kPositionalOrNamed));
  const auto named_required_schema_relation_arg = FunctionArgumentType(
      ARG_TYPE_RELATION,
      FunctionArgumentTypeOptions(
          output_schema_two_types,
          /*extra_relation_input_columns_allowed=*/false)
          .set_argument_name("schema_relation_arg", kPositionalOrNamed));
  const auto named_required_value_table_relation_arg = FunctionArgumentType(
      ARG_TYPE_RELATION,
      FunctionArgumentTypeOptions(
          output_schema_proto_value_table,
          /*extra_relation_input_columns_allowed=*/false)
          .set_argument_name("value_table_relation_arg", kPositionalOrNamed));
  const auto named_optional_string_arg = FunctionArgumentType(
      types_->get_string(),
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_argument_name("format_string", kPositionalOrNamed));
  const auto named_optional_date_arg = FunctionArgumentType(
      types_->get_string(),
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_argument_name("date_string", kPositionalOrNamed));
  const auto named_optional_any_relation_arg = FunctionArgumentType(
      ARG_TYPE_RELATION,
      FunctionArgumentTypeOptions()
          .set_cardinality(FunctionArgumentType::OPTIONAL)
          .set_argument_name("any_relation_arg", kPositionalOrNamed));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_required_scalar_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_required_format_arg, named_required_date_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with two named optional arguments.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_optional_scalar_args"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_optional_string_arg, named_optional_date_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with one optional named "any table" relation argument.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_optional_any_relation_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_optional_any_relation_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with one optional named "any table" relation argument and an
  // optional scalar argument.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_optional_any_relation_arg_optional_scalar_arg"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {named_optional_any_relation_arg, named_optional_string_arg},
          /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with one required named "any table" relation argument.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_required_any_relation_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_required_any_relation_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with one named relation argument with a required schema.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_required_schema_relation_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_required_schema_relation_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with one named relation argument with a value table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_required_value_table_relation_arg"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {named_required_value_table_relation_arg},
                         /*context_id=*/-1)},
      output_schema_two_types));

  // Add a TVF with a combination of named scalar and relation arguments.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_scalar_and_relation_args"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {named_required_format_arg, named_required_schema_relation_arg},
          /*context_id=*/-1)},
      output_schema_two_types));
  {
    // Add a TVF with relation arg + scalar arg + named lambda.
    FunctionArgumentType relation_arg = FunctionArgumentType(
        ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_argument_name(
                               "any_relation", kPositionalOrNamed));
    FunctionArgumentType scalar_arg = FunctionArgumentType(
        ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_name(
                            "any_scalar", kPositionalOrNamed));
    FunctionArgumentType named_lambda_arg = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda",
                                                        kNamedOnly));
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {"tvf_relation_arg_scalar_arg_named_lambda"},
        {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                               output_schema_two_types,
                               /*extra_relation_input_columns_allowed=*/false),
                           {relation_arg, scalar_arg, named_lambda_arg},
                           /*context_id=*/-1)},
        output_schema_two_types));

    FunctionArgumentType scalar_arg_positional_must_be_not_null =
        FunctionArgumentType(
            types_->get_int64(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_must_be_non_null(true)
                .set_min_value(3));
    FunctionArgumentType scalar_arg_positional_must_be_not_null_with_default =
        FunctionArgumentType(
            types_->get_double(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_must_be_non_null(true)
                .set_default(values::Double(3.14)));
    FunctionArgumentType scalar_arg_positional_must_be_constant =
        FunctionArgumentType(
            types_->get_string(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_must_be_constant(true));
    FunctionArgumentType scalar_arg_positional_must_be_constant_expression =
        FunctionArgumentType(
            types_->get_double(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_must_be_constant_expression(true)
                .set_max_value(100));
    FunctionArgumentType scalar_arg_positional_must_support_equality =
        FunctionArgumentType(
            ARG_STRUCT_ANY, FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::OPTIONAL)
                                .set_must_support_equality(true));
    FunctionArgumentType scalar_arg_positional_must_support_ordering =
        FunctionArgumentType(
            ARG_TYPE_ANY_1, FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::OPTIONAL)
                                .set_must_support_ordering(true));
    FunctionArgumentType scalar_arg_positional_must_support_grouping =
        FunctionArgumentType(
            ARG_TYPE_ANY_2, FunctionArgumentTypeOptions()
                                .set_cardinality(FunctionArgumentType::OPTIONAL)
                                .set_must_support_grouping(true));
    FunctionArgumentType
        scalar_arg_positional_array_element_must_support_equality =
            FunctionArgumentType(
                ARG_ARRAY_TYPE_ANY_3,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_array_element_must_support_equality(true));
    FunctionArgumentType
        scalar_arg_positional_array_element_must_support_ordering =
            FunctionArgumentType(
                ARG_ARRAY_TYPE_ANY_4,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_array_element_must_support_ordering(true));
    FunctionArgumentType
        scalar_arg_positional_array_element_must_support_grouping =
            FunctionArgumentType(
                ARG_ARRAY_TYPE_ANY_5,
                FunctionArgumentTypeOptions()
                    .set_cardinality(FunctionArgumentType::OPTIONAL)
                    .set_array_element_must_support_grouping(true));
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {"tvf_positional_scalar_args_with_constraints"},
        {FunctionSignature(
            FunctionArgumentType::RelationWithSchema(
                output_schema_two_types,
                /*extra_relation_input_columns_allowed=*/false),
            {
                scalar_arg_positional_must_be_not_null,
                scalar_arg_positional_must_be_constant,
                scalar_arg_positional_must_be_constant_expression,
                scalar_arg_positional_must_support_equality,
                scalar_arg_positional_must_support_ordering,
                scalar_arg_positional_must_support_grouping,
                scalar_arg_positional_array_element_must_support_equality,
                scalar_arg_positional_array_element_must_support_ordering,
                scalar_arg_positional_array_element_must_support_grouping,
                scalar_arg_positional_must_be_not_null_with_default,
            },
            /*context_id=*/-1)},
        output_schema_two_types));

    FunctionArgumentType scalar_arg_named_must_be_not_null =
        FunctionArgumentType(
            types_->get_int64(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_1", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_be_non_null(true)
                .set_min_value(3));
    FunctionArgumentType scalar_arg_named_must_be_constant =
        FunctionArgumentType(
            types_->get_string(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_2", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_be_constant(true));
    FunctionArgumentType scalar_arg_named_must_be_constant_expression =
        FunctionArgumentType(
            types_->get_double(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_3", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_be_constant_expression(true)
                .set_max_value(100));
    FunctionArgumentType scalar_arg_named_must_support_equality =
        FunctionArgumentType(
            ARG_STRUCT_ANY,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_4", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_support_equality(true));
    FunctionArgumentType scalar_arg_named_must_support_ordering =
        FunctionArgumentType(
            ARG_TYPE_ANY_1,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_5", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_support_ordering(true));
    FunctionArgumentType scalar_arg_named_must_support_grouping =
        FunctionArgumentType(
            ARG_TYPE_ANY_2,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_6", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_support_grouping(true));
    FunctionArgumentType scalar_arg_named_array_element_must_support_equality =
        FunctionArgumentType(
            ARG_ARRAY_TYPE_ANY_3,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_7", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_array_element_must_support_equality(true));
    FunctionArgumentType scalar_arg_named_array_element_must_support_ordering =
        FunctionArgumentType(
            ARG_ARRAY_TYPE_ANY_4,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_8", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_array_element_must_support_ordering(true));
    FunctionArgumentType scalar_arg_named_array_element_must_support_grouping =
        FunctionArgumentType(
            ARG_ARRAY_TYPE_ANY_5,
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_9", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_array_element_must_support_grouping(true));
    FunctionArgumentType scalar_arg_named_must_be_not_null_with_default =
        FunctionArgumentType(
            types_->get_double(),
            FunctionArgumentTypeOptions()
                .set_cardinality(FunctionArgumentType::OPTIONAL)
                .set_argument_name("arg_10", FunctionEnums::POSITIONAL_OR_NAMED)
                .set_must_be_non_null(true)
                .set_default(values::Double(3.14)));
    catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
        {"tvf_named_scalar_args_with_constraints"},
        {FunctionSignature(
            FunctionArgumentType::RelationWithSchema(
                output_schema_two_types,
                /*extra_relation_input_columns_allowed=*/false),
            {
                scalar_arg_named_must_be_not_null,
                scalar_arg_named_must_be_constant,
                scalar_arg_named_must_be_constant_expression,
                scalar_arg_named_must_support_equality,
                scalar_arg_named_must_support_ordering,
                scalar_arg_named_must_support_grouping,
                scalar_arg_named_array_element_must_support_equality,
                scalar_arg_named_array_element_must_support_ordering,
                scalar_arg_named_array_element_must_support_grouping,
                scalar_arg_named_must_be_not_null_with_default,
            },
            /*context_id=*/-1)},
        output_schema_two_types));
  }

  // TVFs which are in the GoogleSQL group (builtins).
  catalog_->AddOwnedTableValuedFunction(new TableValuedFunction(
      {"builtin_tvf_exactly_one_arg"}, Function::kGoogleSQLFunctionGroupName,
      {FunctionSignature(FunctionArgumentType::AnyRelation(),
                         {types::Int64Type()}, context_id++)},
      TableValuedFunctionOptions().set_compute_result_type_callback(
          result_type_callback)));
  TVFComputeResultTypeCallback result_type_callback_with_empty_relation =
      [](Catalog* catalog, TypeFactory* type_factory,
         const FunctionSignature& signature,
         const std::vector<TVFInputArgumentType>& actual_arguments,
         const AnalyzerOptions& analyzer_options) {
        return std::make_unique<TVFSignature>(actual_arguments, TVFRelation({}),
                                              TVFSignatureOptions());
      };
  catalog_->AddOwnedTableValuedFunction(new TableValuedFunction(
      {"builtin_tvf_exactly_one_relation_arg"},
      Function::kGoogleSQLFunctionGroupName,
      {FunctionSignature(
          FunctionArgumentType::AnyRelation(),
          {FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentTypeOptions().set_argument_name(
                                    "t", FunctionEnums::POSITIONAL_OR_NAMED))},
          context_id++)},
      TableValuedFunctionOptions().set_compute_result_type_callback(
          result_type_callback_with_empty_relation)));
}  // NOLINT(readability/fn_size)

// Tests handling of optional relation arguments.
// If the relation is present, it doubles the value of each row.
// If the input relation is absent, returns a single zero.
//
// It has one optional value table argument with a single int64 column.
// The output schema is also an int64 value table.
class TvfOptionalRelation : public FixedOutputSchemaTVF {
  class TvfOptionalRelationIterator : public EvaluatorTableIterator {
   public:
    explicit TvfOptionalRelationIterator(
        std::unique_ptr<EvaluatorTableIterator> input)
        : input_(std::move(input)) {}

    bool NextRow() override {
      if (!input_) {
        if (rows_returned_ > 0) {
          return false;
        }
        value_ = values::Int64(0);
        ++rows_returned_;
        return true;
      }

      if (!input_->NextRow()) {
        return false;
      }

      value_ = values::Int64(input_->GetValue(0).int64_value() * 2);
      ++rows_returned_;
      return true;
    }

    int NumColumns() const override { return 1; }
    std::string GetColumnName(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return "";
    }
    const Type* GetColumnType(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return types::Int64Type();
    }
    const Value& GetValue(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return value_;
    }
    absl::Status Status() const override {
      return input_ ? input_->Status() : absl::OkStatus();
    }
    absl::Status Cancel() override { return input_->Cancel(); }

   private:
    int64_t rows_returned_ = 0;
    std::unique_ptr<EvaluatorTableIterator> input_;
    Value value_;
  };

 public:
  explicit TvfOptionalRelation()
      : FixedOutputSchemaTVF(
            {R"(tvf_optional_relation)"},
            {FunctionSignature(
                FunctionArgumentType::RelationWithSchema(
                    TVFRelation::ValueTable(types::Int64Type()),
                    /*extra_relation_input_columns_allowed=*/false),
                {FunctionArgumentType(
                    ARG_TYPE_RELATION,
                    FunctionArgumentTypeOptions(
                        TVFRelation::ValueTable(types::Int64Type()),
                        /*extra_relation_input_columns_allowed=*/true)
                        .set_cardinality(FunctionArgumentType::OPTIONAL))},
                /*context_id=*/-1)},
            TVFRelation::ValueTable(types::Int64Type())) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TvfEvaluatorArg> input_arguments,
      const std::vector<TVFSchemaColumn>& output_columns,
      const FunctionSignature* function_call_signature) const override {
    GOOGLESQL_RET_CHECK_LE(input_arguments.size(), 1);

    std::unique_ptr<EvaluatorTableIterator> input;
    if (input_arguments.size() == 1) {
      GOOGLESQL_RET_CHECK(input_arguments[0].relation);
      input = std::move(input_arguments[0].relation);
      GOOGLESQL_RET_CHECK_EQ(input->NumColumns(), 1);
      GOOGLESQL_RET_CHECK_EQ(input->GetColumnType(0), types::Int64Type());
    }

    GOOGLESQL_RET_CHECK_EQ(output_columns.size(), 1);
    return std::make_unique<TvfOptionalRelationIterator>(std::move(input));
  }
};

// Tests handling of optional scalar and named arguments.
//
// Calculates and emits the value of y=xa+b, `steps` numbers of times
// incrementing x by `dx` each time.
class TvfOptionalArguments : public FixedOutputSchemaTVF {
  class Evaluator : public EvaluatorTableIterator {
   public:
    Evaluator(double x, int64_t a, int64_t b, double dx, int64_t steps)
        : x_(x), a_(a), b_(b), dx_(dx), steps_(steps) {}

    bool NextRow() override {
      if (current_step_ >= steps_) {
        return false;
      }

      value_ = values::Double(x_ * a_ + b_);

      ++current_step_;
      x_ += dx_;
      return true;
    }
    int NumColumns() const override { return 1; }
    std::string GetColumnName(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return "y";
    }
    const Type* GetColumnType(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return types::DoubleType();
    }
    const Value& GetValue(int i) const override {
      ABSL_DCHECK_EQ(i, 0);
      return value_;
    }
    absl::Status Status() const override { return absl::OkStatus(); }
    absl::Status Cancel() override { return absl::OkStatus(); }

   private:
    double x_;
    int64_t a_;
    int64_t b_;
    double dx_;
    int64_t steps_;
    int64_t current_step_ = 0;
    Value value_;
  };

 public:
  explicit TvfOptionalArguments()
      : FixedOutputSchemaTVF(
            {R"(tvf_optional_arguments)"},
            {FunctionSignature(
                FunctionArgumentType::RelationWithSchema(
                    TVFRelation({{"value", types::Int64Type()}}),
                    /*extra_relation_input_columns_allowed=*/false),
                {
                    // Starting x value.
                    FunctionArgumentType(types::DoubleType(),
                                         FunctionArgumentType::OPTIONAL),
                    // A constant.
                    FunctionArgumentType(
                        types::Int64Type(),
                        FunctionArgumentTypeOptions().set_cardinality(
                            FunctionArgumentType::OPTIONAL)),
                    // B constant.
                    FunctionArgumentType(
                        types::Int64Type(),
                        FunctionArgumentTypeOptions().set_cardinality(
                            FunctionArgumentType::OPTIONAL)),
                    // X increment.
                    FunctionArgumentType(
                        types::DoubleType(),
                        FunctionArgumentTypeOptions()
                            .set_argument_name(
                                "dx", FunctionEnums::POSITIONAL_OR_NAMED)
                            .set_cardinality(FunctionArgumentType::OPTIONAL)),
                    // Number of steps.
                    FunctionArgumentType(
                        types::Int64Type(),
                        FunctionArgumentTypeOptions()
                            .set_argument_name(
                                "steps", FunctionEnums::POSITIONAL_OR_NAMED)
                            .set_cardinality(FunctionArgumentType::OPTIONAL)),
                },
                /*context_id=*/-1)},
            TVFRelation(TVFRelation({{"y", types::DoubleType()}}))) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TvfEvaluatorArg> input_arguments,
      const std::vector<TVFSchemaColumn>& output_columns,
      const FunctionSignature* function_call_signature) const override {
    GOOGLESQL_RET_CHECK_LE(input_arguments.size(), 5);

    double x = 1;
    if (!input_arguments.empty()) {
      GOOGLESQL_RET_CHECK(input_arguments[0].value);
      GOOGLESQL_RET_CHECK(input_arguments[0].value->type()->IsDouble());
      if (!input_arguments[0].value->is_null()) {
        x = input_arguments[0].value->double_value();
      }
    }

    int64_t a = 2;
    if (input_arguments.size() >= 2) {
      GOOGLESQL_RET_CHECK(input_arguments[1].value);
      GOOGLESQL_RET_CHECK(input_arguments[1].value->type()->IsInt64());
      if (!input_arguments[1].value->is_null()) {
        a = input_arguments[1].value->int64_value();
      }
    }

    int64_t b = 3;
    if (input_arguments.size() >= 3) {
      GOOGLESQL_RET_CHECK(input_arguments[2].value);
      GOOGLESQL_RET_CHECK(input_arguments[2].value->type()->IsInt64());
      if (!input_arguments[2].value->is_null()) {
        b = input_arguments[2].value->int64_value();
      }
    }

    double dx = 1;
    if (input_arguments.size() >= 4) {
      GOOGLESQL_RET_CHECK(input_arguments[3].value);
      GOOGLESQL_RET_CHECK(input_arguments[3].value->type()->IsDouble());
      if (!input_arguments[3].value->is_null()) {
        dx = input_arguments[3].value->double_value();
      }
    }

    int64_t steps = 1;
    if (input_arguments.size() >= 5) {
      GOOGLESQL_RET_CHECK(input_arguments[4].value);
      GOOGLESQL_RET_CHECK(input_arguments[4].value->type()->IsInt64());
      if (!input_arguments[4].value->is_null()) {
        steps = input_arguments[4].value->int64_value();
      }
    }

    return std::make_unique<TvfOptionalArguments::Evaluator>(x, a, b, dx,
                                                             steps);
  }
};

// Tests handling of repeated arguments.
// Takes pairs of string and int arguments and produces a table with a row for
// each pair.
class TvfRepeatedArguments : public FixedOutputSchemaTVF {
  class TvfRepeatedArgumentsIterator : public EvaluatorTableIterator {
   public:
    explicit TvfRepeatedArgumentsIterator(std::vector<TvfEvaluatorArg> args)
        : args_(std::move(args)) {}

    bool NextRow() override {
      if (current_ + 1 >= args_.size()) {
        return false;
      }

      if (!args_[current_].value ||
          !args_[current_].value->type()->IsString()) {
        status_ = absl::InternalError("Bad key");
        return false;
      }

      if (!args_[current_ + 1].value ||
          !args_[current_ + 1].value->type()->IsInt64()) {
        status_ = absl::InternalError("Bad value");
        return false;
      }

      key_ = *args_[current_].value;
      value_ = *args_[current_ + 1].value;
      current_ += 2;
      return true;
    }

    int NumColumns() const override { return 2; }
    std::string GetColumnName(int i) const override {
      ABSL_DCHECK_GE(i, 0);
      ABSL_DCHECK_LT(i, NumColumns());
      return i == 0 ? "key" : "value";
    }
    const Type* GetColumnType(int i) const override {
      ABSL_DCHECK_GE(i, 0);
      ABSL_DCHECK_LT(i, NumColumns());
      return i == 0 ? types::StringType() : types::Int64Type();
    }
    const Value& GetValue(int i) const override {
      ABSL_DCHECK_GE(i, 0);
      ABSL_DCHECK_LT(i, NumColumns());
      return i == 0 ? key_ : value_;
    }
    absl::Status Status() const override { return status_; }
    absl::Status Cancel() override { return absl::OkStatus(); }

   private:
    std::vector<TvfEvaluatorArg> args_;
    int64_t current_ = 0;
    Value key_;
    Value value_;
    absl::Status status_;
  };

 public:
  explicit TvfRepeatedArguments()
      : FixedOutputSchemaTVF(
            {R"(tvf_repeated_arguments)"},
            {FunctionSignature(
                FunctionArgumentType::RelationWithSchema(
                    TVFRelation({{"key", types::StringType()},
                                 {"value", types::Int64Type()}}),
                    /*extra_relation_input_columns_allowed=*/false),
                {
                    FunctionArgumentType(types::StringType(),
                                         FunctionArgumentType::REPEATED),
                    FunctionArgumentType(types::Int64Type(),
                                         FunctionArgumentType::REPEATED),
                },
                /*context_id=*/-1)},
            TVFRelation({{"key", types::StringType()},
                         {"value", types::Int64Type()}})) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TvfEvaluatorArg> input_arguments,
      const std::vector<TVFSchemaColumn>& output_columns,
      const FunctionSignature* function_call_signature) const override {
    GOOGLESQL_RET_CHECK(input_arguments.size() % 2 == 0);
    return std::make_unique<TvfRepeatedArgumentsIterator>(
        std::move(input_arguments));
  }
};

// Tests forwarding input schema in a TVF.
//
// This function will pass through values from input columns to matching output
// columns. For INT64 columns it will additionally add the value of the integer
// argument.
//
// It has one relation argument and one integer argument. The output
// schema is set to be the same as the input schema of the relation argument.
class TvfIncrementBy : public ForwardInputSchemaToOutputSchemaTVF {
  class TvfIncrementByIterator : public EvaluatorTableIterator {
   public:
    explicit TvfIncrementByIterator(
        std::unique_ptr<EvaluatorTableIterator> input, int64_t value_arg,
        std::vector<TVFSchemaColumn> output_columns)
        : input_(std::move(input)),
          value_arg_(value_arg),
          output_columns_(std::move(output_columns)),
          values_(output_columns_.size()) {}

    bool NextRow() override {
      if (!input_->NextRow()) {
        status_ = input_->Status();
        return false;
      }

      for (int o = 0; o < output_columns_.size(); ++o) {
        std::string output_column_name = GetColumnName(o);
        const Value* value = nullptr;
        for (int i = 0; i < input_->NumColumns(); ++i) {
          if (input_->GetColumnName(i) == output_column_name) {
            value = &input_->GetValue(i);
            break;
          }
        }

        if (value == nullptr) {
          status_ = ::googlesql_base::InternalErrorBuilder()
                    << "Could not find input column for " << output_column_name;
          return false;
        }

        values_[o] = value->type()->IsInt64()
                         ? values::Int64(value->ToInt64() + value_arg_)
                         : *value;
      }

      return true;
    }

    int NumColumns() const override {
      return static_cast<int>(output_columns_.size());
    }
    std::string GetColumnName(int i) const override {
      ABSL_DCHECK_LT(i, output_columns_.size());
      return output_columns_[i].name;
    }
    const Type* GetColumnType(int i) const override {
      ABSL_DCHECK_LT(i, output_columns_.size());
      return output_columns_[i].type;
    }
    const Value& GetValue(int i) const override {
      ABSL_DCHECK_LT(i, values_.size());
      return values_[i];
    }
    absl::Status Status() const override { return status_; }
    absl::Status Cancel() override { return input_->Cancel(); }

   private:
    std::unique_ptr<EvaluatorTableIterator> input_;
    int64_t value_arg_;
    absl::Status status_;
    std::vector<TVFSchemaColumn> output_columns_;
    std::vector<Value> values_;
  };

 public:
  explicit TvfIncrementBy()
      : ForwardInputSchemaToOutputSchemaTVF(
            {R"(tvf_increment_by)"},
            {FunctionSignature(
                ARG_TYPE_RELATION,
                {FunctionArgumentType::AnyRelation(),
                 FunctionArgumentType(
                     types::Int64Type(),
                     FunctionArgumentTypeOptions()
                         .set_cardinality(FunctionArgumentType::OPTIONAL)
                         .set_default(values::Int64(1)))},
                /*context_id=*/-1)}) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TvfEvaluatorArg> input_arguments,
      const std::vector<TVFSchemaColumn>& output_columns,
      const FunctionSignature* function_call_signature) const override {
    GOOGLESQL_RET_CHECK_EQ(input_arguments.size(), 2);
    GOOGLESQL_RET_CHECK(input_arguments[0].relation);
    GOOGLESQL_RET_CHECK(input_arguments[1].value);
    GOOGLESQL_RET_CHECK_EQ(input_arguments[1].value->type_kind(), TypeKind::TYPE_INT64);
    return std::make_unique<TvfIncrementByIterator>(
        std::move(input_arguments[0].relation),
        input_arguments[1].value->ToInt64(), output_columns);
  }
};

// This function takes two integer values and provides both sum and difference.
//
// Has a fixed input and output schema.
class TvfSumAndDiff : public FixedOutputSchemaTVF {
  class TvfSumAndDiffIterator : public EvaluatorTableIterator {
   public:
    explicit TvfSumAndDiffIterator(
        std::unique_ptr<EvaluatorTableIterator> input)
        : input_(std::move(input)) {
      output_columns_["sum"] = values::Int64(0);
      output_columns_["diff"] = values::Int64(0);
    }

    bool NextRow() override {
      if (!input_->NextRow()) {
        return false;
      }
      int64_t a = input_->GetValue(0).int64_value();
      int64_t b = input_->GetValue(1).int64_value();
      output_columns_["sum"] = values::Int64(a + b);
      output_columns_["diff"] = values::Int64(a - b);
      return true;
    }

    int NumColumns() const override {
      return static_cast<int>(output_columns_.size());
    }
    std::string GetColumnName(int i) const override {
      ABSL_DCHECK_LT(i, output_columns_.size());
      auto iter = output_columns_.cbegin();
      std::advance(iter, i);
      return iter->first;
    }
    const Type* GetColumnType(int i) const override {
      return GetValue(i).type();
    }
    const Value& GetValue(int i) const override {
      ABSL_DCHECK_LT(i, output_columns_.size());
      auto iter = output_columns_.cbegin();
      std::advance(iter, i);
      return iter->second;
    }
    absl::Status Status() const override { return input_->Status(); }
    absl::Status Cancel() override { return input_->Cancel(); }

   private:
    std::unique_ptr<EvaluatorTableIterator> input_;
    absl::btree_map<std::string, Value> output_columns_;
  };

 public:
  TvfSumAndDiff()
      : FixedOutputSchemaTVF(
            {R"(tvf_sum_diff)"},
            {FunctionSignature(
                FunctionArgumentType::RelationWithSchema(
                    TVFRelation({{"sum", types::Int64Type()},
                                 {"diff", types::Int64Type()}}),
                    /*extra_relation_input_columns_allowed=*/false),
                {FunctionArgumentType::RelationWithSchema(
                    TVFRelation(
                        {{"a", types::Int64Type()}, {"b", types::Int64Type()}}),
                    /*extra_relation_input_columns_allowed=*/false)},
                /*context_id=*/-1)},
            TVFRelation(
                {{"sum", types::Int64Type()}, {"diff", types::Int64Type()}})) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TvfEvaluatorArg> input_arguments,
      const std::vector<TVFSchemaColumn>& output_columns,
      const FunctionSignature* function_call_signature) const override {
    GOOGLESQL_RET_CHECK_EQ(input_arguments.size(), 1);
    GOOGLESQL_RET_CHECK(input_arguments[0].relation);
    return std::make_unique<TvfSumAndDiffIterator>(
        std::move(input_arguments[0].relation));
  }
};

void SampleCatalogImpl::LoadTableValuedFunctionsWithEvaluators() {
  catalog_->AddOwnedTableValuedFunction(new TvfOptionalRelation());
  catalog_->AddOwnedTableValuedFunction(new TvfOptionalArguments());
  catalog_->AddOwnedTableValuedFunction(new TvfRepeatedArguments());
  catalog_->AddOwnedTableValuedFunction(new TvfIncrementBy());
  catalog_->AddOwnedTableValuedFunction(new TvfSumAndDiff());
}

void SampleCatalogImpl::LoadFunctionsWithStructArgs() {
  const std::vector<OutputColumn> kOutputColumnsAllTypes =
      GetOutputColumnsForAllTypes(types_);
  TVFRelation output_schema_two_types =
      GetOutputSchemaWithTwoTypes(kOutputColumnsAllTypes);

  const Type* array_string_type = nullptr;
  GOOGLESQL_CHECK_OK(types_->MakeArrayType(types_->get_string(), &array_string_type));

  const Type* struct_type1 = nullptr;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"field1", array_string_type}, {"field2", array_string_type}},
      &struct_type1));
  const Type* struct_type2 = nullptr;
  GOOGLESQL_CHECK_OK(types_->MakeStructType({{"field1", array_string_type},
                                   {"field2", array_string_type},
                                   {"field3", array_string_type}},
                                  &struct_type2));

  const auto named_struct_arg1 = FunctionArgumentType(
      struct_type1, FunctionArgumentTypeOptions().set_argument_name(
                        "struct_arg1", kPositionalOrNamed));
  const auto named_struct_arg2 = FunctionArgumentType(
      struct_type2, FunctionArgumentTypeOptions().set_argument_name(
                        "struct_arg2", kPositionalOrNamed));

  // A TVF with struct args.
  auto tvf = std::make_unique<FixedOutputSchemaTVF>(
      std::vector<std::string>{"tvf_named_struct_args"},
      std::vector<FunctionSignature>({FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {named_struct_arg1, named_struct_arg2},
          /*context_id=*/-1)}),
      output_schema_two_types);
  catalog_->AddOwnedTableValuedFunction(std::move(tvf));

  auto function = std::make_unique<Function>(
      "fn_named_struct_args", "sample_functions", Function::SCALAR);
  function->AddSignature({FunctionArgumentType(array_string_type),
                          {named_struct_arg1, named_struct_arg2},
                          /*context_id=*/-1});
  catalog_->AddOwnedFunction(std::move(function));
}

void SampleCatalogImpl::LoadTVFWithExtraColumns() {
  int64_t context_id = 0;

  // Add a TVF with appended columns of valid GoogleSQL types.
  catalog_->AddOwnedTableValuedFunction(
      new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_columns"},
          {FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION},
                             context_id++)},
          {TVFSchemaColumn("append_col_int64", types::Int64Type()),
           TVFSchemaColumn("append_col_int32", types::Int32Type()),
           TVFSchemaColumn("append_col_uint32", types::Uint32Type()),
           TVFSchemaColumn("append_col_uint64", types::Uint64Type()),
           TVFSchemaColumn("append_col_bytes", types::BytesType()),
           TVFSchemaColumn("append_col_bool", types::BoolType()),
           TVFSchemaColumn("append_col_float", types::FloatType()),
           TVFSchemaColumn("append_col_double", types::DoubleType()),
           TVFSchemaColumn("append_col_date", types::DateType()),
           TVFSchemaColumn("append_col_timestamp", types::TimestampType()),
           TVFSchemaColumn("append_col_numeric", types::NumericType()),
           TVFSchemaColumn("append_col_bignumeric", types::BigNumericType()),
           TVFSchemaColumn("append_col_json", types::JsonType()),
           TVFSchemaColumn("append_col_string", types::StringType()),
           TVFSchemaColumn("append_col_uuid", types::UuidType())}));

  // Add a TVF with an appended column that has empty name.
  catalog_->AddOwnedTableValuedFunction(
      new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_no_column"},
          {FunctionSignature(ARG_TYPE_RELATION, {ARG_TYPE_RELATION},
                             context_id++)},
          {}));

  const auto named_required_any_relation_arg = FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions().set_argument_name(
                             "any_relation_arg", kPositionalOrNamed));

  // Add a TVF with one required named "any table" relation argument.
  catalog_->AddOwnedTableValuedFunction(
      new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          {"tvf_append_columns_any_relation_arg"},
          {FunctionSignature(ARG_TYPE_RELATION,
                             {named_required_any_relation_arg},
                             /*context_id=*/context_id++)},
          {TVFSchemaColumn("append_col_int32", types::Int32Type())}));
}

void SampleCatalogImpl::LoadDescriptorTableValuedFunctions() {
  int64_t context_id = 0;
  const std::vector<OutputColumn> kOutputColumnsAllTypes =
      GetOutputColumnsForAllTypes(types_);

  TVFRelation output_schema_two_types =
      GetOutputSchemaWithTwoTypes(kOutputColumnsAllTypes);

  const std::string kInt64a = "int64a";
  const std::string kInt64b = "int64b";

  // Add a TVF with a table parameter and a descriptor with -1 table offset.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_one_descriptor"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true),
           FunctionArgumentType::AnyDescriptor()},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two table parameters, one descriptor with 0 table offset
  // and one descriptor with 1 table offset.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_relations_arg_two_descriptors_resolved_names"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64b, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true),
           FunctionArgumentType::AnyDescriptor(0),
           FunctionArgumentType::AnyDescriptor(1)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with a table parameter and a descriptor with 0 table offset.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_one_descriptor_resolved_names"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true),
           FunctionArgumentType::AnyDescriptor(0)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with a descriptor with 1 table offset and a table parameter.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_descriptor_resolved_names_one_relation_arg"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyDescriptor(1),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64()),
                            TVFRelation::Column(kInt64b, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with a descriptor with 1 table offset and a table parameter with
  // ambiguous column naming problem.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_descriptor_resolved_names_one_relation_arg_ambiguous_naming"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyDescriptor(1),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64()),
                            TVFRelation::Column(kInt64b, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true)},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with a descriptor with 1 table offset, a table parameter and a
  // descriptor with -1 table offset.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_descriptor_resolved_names_one_relation_arg_one_descriptor_arg"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyDescriptor(1),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({TVFRelation::Column(kInt64a, types_->get_int64())}),
               /*extra_relation_input_columns_allowed=*/true),
           FunctionArgumentType::AnyDescriptor()},
          context_id++)},
      output_schema_two_types));
}

void SampleCatalogImpl::LoadConnectionTableValuedFunctions() {
  int64_t context_id = 0;

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_connection_arg_with_fixed_output"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeString, types::StringType()}}),
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyConnection()},
                         context_id++)},
      TVFRelation({{kTypeString, types::StringType()}})));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_connection_one_string_arg_with_fixed_output"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeInt64, types::Int64Type()},
                                          {kTypeString, types::StringType()}}),
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyConnection(),
                          FunctionArgumentType(types::StringType())},
                         context_id++)},
      TVFRelation({{kTypeInt64, types::Int64Type()},
                   {kTypeString, types::StringType()}})));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_connections_with_fixed_output"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             TVFRelation({{kTypeDouble, types::DoubleType()},
                                          {kTypeString, types::StringType()}}),
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyConnection(),
                          FunctionArgumentType::AnyConnection()},
                         context_id++)},
      TVFRelation({{kTypeDouble, types::DoubleType()},
                   {kTypeString, types::StringType()}})));
}

void SampleCatalogImpl::LoadTableValuedFunctionsWithDeprecationWarnings() {
  // Generate an empty output schema.
  TVFRelation empty_output_schema({});

  const std::vector<OutputColumn> kOutputColumnsAllTypes =
      GetOutputColumnsForAllTypes(types_);

  TVFRelation output_schema_two_types =
      GetOutputSchemaWithTwoTypes(kOutputColumnsAllTypes);

  int context_id = 0;

  // Add a TVF that triggers a deprecation warning.
  FunctionSignature deprecation_warning_signature(
      FunctionArgumentType::RelationWithSchema(
          empty_output_schema,
          /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList(), context_id++);
  deprecation_warning_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/11)});
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_deprecation_warning"}, {deprecation_warning_signature},
      output_schema_two_types));

  // Add a TVF that triggers two deprecation warnings with the same kind.
  FunctionSignature two_deprecation_warnings_same_kind_signature(
      FunctionArgumentType::RelationWithSchema(
          empty_output_schema,
          /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList(), context_id++);
  two_deprecation_warnings_same_kind_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/12),
       CreateDeprecationWarning(/*id=*/13)});
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_deprecation_warnings_same_kind"},
      {two_deprecation_warnings_same_kind_signature}, output_schema_two_types));

  // Add a TVF that triggers two deprecation warnings with different kinds.
  FunctionSignature two_deprecation_warnings_signature(
      FunctionArgumentType::RelationWithSchema(
          empty_output_schema,
          /*extra_relation_input_columns_allowed=*/false),
      FunctionArgumentTypeList(), context_id++);
  two_deprecation_warnings_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/14),
       CreateDeprecationWarning(
           /*id=*/15, DeprecationWarning::DEPRECATED_FUNCTION_SIGNATURE)});
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_deprecation_warnings"}, {two_deprecation_warnings_signature},
      output_schema_two_types));

  // Add a TVF with exactly one relation argument. The output schema is set to
  // be the same as the input schema. The TVF also triggers a deprecation
  // warning.
  FunctionSignature forward_deprecation_signature(
      ARG_TYPE_RELATION, {FunctionArgumentType::AnyRelation()}, context_id++);
  forward_deprecation_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/16)});
  catalog_->AddOwnedTableValuedFunction(new ForwardInputSchemaToOutputSchemaTVF(
      {"tvf_one_relation_arg_output_schema_is_input_schema_deprecation"},
      {forward_deprecation_signature}));

  // Add a TVF with three arguments: The first one is required model; The
  // second is optional table; The third is named optional struct.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_model_optional_table_named_struct"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyModel(),
           FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_STRUCT_ANY,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two arguments: The first is named optional struct. The
  // second is a named optional table;
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_struct_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(
               ARG_STRUCT_ANY,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("barfoo", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two arguments: The first is named optional struct. The
  // second is a named optional table;
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_named_proto_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(
               ARG_PROTO_ANY,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("barfoo", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required model; The
  // second is optional scalar; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_model_optional_scalar_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyModel(),
           FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first two are optional scalars; The
  // third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_scalars_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required table; The
  // second is optional scalar; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_table_optional_scalar_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyRelation(),
           FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is optional table; The
  // second is optional scalar; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_table_optional_scalar_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required scalar; The
  // second is optional model; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_scalar_optional_model_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY),
           FunctionArgumentType(ARG_TYPE_MODEL, FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required scalar; The
  // second is optional model; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_scalar_optional_model_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(ARG_TYPE_MODEL, FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is optional scalar; The
  // second is named optional table; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_scalar_named_tables"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("barfoo", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is optional scalar; The
  // second is named optional model; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_scalar_named_model_named_table"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               ARG_TYPE_MODEL,
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("barfoo", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with a required table, a named optional table, a mandatory
  // named table and a required mandatory named table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_required_named_optional_required_tables"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentType::REQUIRED),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("table2", kNamedOnly)
                   .set_cardinality(FunctionArgumentType::REQUIRED)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("table3", kPositionalOrNamed)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("table4", kNamedOnly)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required model; The
  // second is optional string; The third is named optional table.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_model_optional_string_optional_table"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType::AnyModel(),
                          FunctionArgumentType(types::StringType(),
                                               FunctionArgumentType::OPTIONAL),
                          FunctionArgumentType(ARG_TYPE_RELATION,
                                               FunctionArgumentType::OPTIONAL)},
                         context_id++)},
      output_schema_two_types));

  // Add a TVF with three arguments: The first one is required model; The
  // second is optional table; The third is named optional string with default.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_model_optional_table_named_string_default"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyModel(),
           FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               types::StringType(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kPositionalOrNamed)
                   .set_default(values::String("default"))
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two arguments: The first one is an optional table; The
  // second one is a named optional string with default.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_table_default_mandatory_string"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               types::StringType(),
               FunctionArgumentTypeOptions()
                   .set_argument_name("foobar", kNamedOnly)
                   .set_default(values::String("default"))
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  // Add a TVF with two table arguments which are both named-only.
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_two_named_only_tables"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("table1", kNamedOnly)
                   .set_cardinality(FunctionArgumentType::OPTIONAL)),
           FunctionArgumentType(
               ARG_TYPE_RELATION,
               FunctionArgumentTypeOptions()
                   .set_argument_name("table2", kNamedOnly)
                   .set_cardinality(FunctionArgumentType::OPTIONAL))},
          context_id++)},
      output_schema_two_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_graph"},
      FunctionSignature(FunctionArgumentType::RelationWithSchema(
                            output_schema_two_types,
                            /*extra_relation_input_columns_allowed=*/false),
                        {FunctionArgumentType::AnyGraph()}, context_id++),
      output_schema_two_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_graph_with_named_arg"},
      FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_two_types,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType::AnyGraph(),
           FunctionArgumentType(types::StringType(),
                                FunctionArgumentTypeOptions().set_argument_name(
                                    "name", kNamedOnly))},
          context_id++),
      output_schema_two_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"bad_tvf_graph_not_first_argument"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_two_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         {types::Int64Type(), FunctionArgumentType::AnyGraph()},
                         context_id++)},
      output_schema_two_types));
}

void SampleCatalogImpl::LoadTableValuedFunctionsWithMultipleSignatures() {
  TVFRelation tvf_relation({{kColumnNameKey, types::Int64Type()},
                            {kColumnNameValue, types::StringType()}});
  FunctionArgumentType tvf_schema(FunctionArgumentType::RelationWithSchema(
      tvf_relation, /*extra_relation_input_columns_allowed=*/false));
  FunctionArgumentType output_schema(tvf_schema);
  int64_t context_id = 0;

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_single_arg"},
      std::vector<FunctionSignature>{
          FunctionSignature(tvf_schema, {types::BoolType()}, context_id++),
          FunctionSignature(tvf_schema, {types::BytesType()}, context_id++),
          FunctionSignature(tvf_schema, {types::DoubleType()}, context_id++),
          FunctionSignature(tvf_schema, {types::StringType()}, context_id++),
          FunctionSignature(tvf_schema, {types::IntervalType()}, context_id++),
          FunctionSignature(tvf_schema, {FunctionArgumentType::AnyRelation()},
                            context_id++),
          FunctionSignature(tvf_schema, {FunctionArgumentType::AnyModel()},
                            context_id++),
          FunctionSignature(tvf_schema, {FunctionArgumentType::AnyConnection()},
                            context_id++),
          FunctionSignature(tvf_schema,
                            {FunctionArgumentType::AnyRelation(),
                             FunctionArgumentType::AnyDescriptor()},
                            context_id++),
      },
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_int64_or_float_or_double_args"},
      std::vector<FunctionSignature>{
          FunctionSignature(tvf_schema, {types::Int64Type()}, context_id++),
          FunctionSignature(tvf_schema, {types::FloatType()}, context_id++),
          FunctionSignature(tvf_schema, {types::DoubleType()}, context_id++),
      },
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_date_time_timestamp_args"},
      std::vector<FunctionSignature>{
          FunctionSignature(tvf_schema, {types::DateType()}, context_id++),
          FunctionSignature(tvf_schema, {types::TimeType()}, context_id++),
          FunctionSignature(tvf_schema, {types::TimestampType()}, context_id++),
      },
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_templated_or_fixed_relation"},
      std::vector<FunctionSignature>{
          FunctionSignature(tvf_schema, {FunctionArgumentType::AnyRelation()},
                            context_id++),
          FunctionSignature(tvf_schema, {tvf_schema}, context_id++),
      },
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_int64_or_relation_arg"},
      std::vector<FunctionSignature>{
          FunctionSignature(tvf_schema, {types::Int64Type()}, context_id++),
          FunctionSignature(tvf_schema, {FunctionArgumentType::AnyRelation()},
                            context_id++),
      },
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_1_2_4_int64_args"},
      {FunctionSignature(tvf_schema,
                         FunctionArgumentTypeList(1, types::Int64Type()),
                         context_id++),
       FunctionSignature(tvf_schema,
                         FunctionArgumentTypeList(2, types::Int64Type()),
                         context_id++),
       FunctionSignature(tvf_schema,
                         FunctionArgumentTypeList(4, types::Int64Type()),
                         context_id++)},
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_with_repeated_args"},
      {FunctionSignature(tvf_schema, {types::StringType()}, context_id++),
       FunctionSignature(tvf_schema,
                         {FunctionArgumentType(types::StringType(),
                                               FunctionArgumentType::REPEATED)},
                         context_id++)},
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_with_optional_args"},
      {FunctionSignature(tvf_schema, {types::Int64Type()}, context_id++),
       FunctionSignature(tvf_schema,
                         {types::Int64Type(),
                          FunctionArgumentType(types::Int64Type(),
                                               FunctionArgumentType::OPTIONAL)},
                         context_id++)},
      tvf_relation));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_overload_positional_or_named_args"},
      {FunctionSignature(tvf_schema,
                         {FunctionArgumentType(
                              types::Int64Type(),
                              FunctionArgumentTypeOptions().set_argument_name(
                                  "arg_a", kPositionalOnly)),
                          FunctionArgumentType(
                              types::Int64Type(),
                              FunctionArgumentTypeOptions().set_argument_name(
                                  "arg_b", kPositionalOnly))},
                         context_id++),
       FunctionSignature(tvf_schema,
                         {FunctionArgumentType(
                              types::Int64Type(),
                              FunctionArgumentTypeOptions().set_argument_name(
                                  "arg_c", kNamedOnly)),
                          FunctionArgumentType(
                              types::Int64Type(),
                              FunctionArgumentTypeOptions().set_argument_name(
                                  "arg_d", kNamedOnly))},
                         context_id++)},
      tvf_relation));
}

// Creates and adds a table-backed TVF to `catalog`. The TVF is backed by a
// table whose columns are the same as `tvf_columns`. The table is also added to
// `catalog`.
static absl::Status AddTableBackedTvf(
    absl::string_view tvf_name, absl::Span<const TVFSchemaColumn> tvf_columns,
    SimpleCatalog& catalog, bool is_value_table = false) {
  std::string table_name = absl::StrCat("table_for_", tvf_name);
  std::vector<const Column*> columns;
  columns.reserve(tvf_columns.size());
  for (const auto& col : tvf_columns) {
    // `table` takes ownership of the columns.
    columns.push_back(
        new SimpleColumn(table_name, col.name, col.annotated_type(),
                         {.is_pseudo_column = col.is_pseudo_column}));
  }
  auto table = std::make_unique<SimpleTable>(table_name, columns,
                                             /*take_ownership=*/true);
  table->set_is_value_table(is_value_table);
  auto tvf = std::make_unique<TvfWithTableSchema>(
      std::vector<std::string>{std::string(tvf_name)},
      std::vector<FunctionSignature>{
          FunctionSignature(ARG_TYPE_RELATION, /*arguments=*/{},
                            /*context_id=*/0, /*options=*/{}),
      },
      table.get());
  catalog.AddOwnedTable(std::move(table));
  catalog.AddOwnedTableValuedFunction(std::move(tvf));
  return absl::OkStatus();
}

absl::Status SampleCatalogImpl::LoadTvfsWithTableSchema() {
  GOOGLESQL_RETURN_IF_ERROR(
      AddTableBackedTvf("tvf_key_value",
                        /*tvf_columns=*/
                        {TVFSchemaColumn("Key", types_->get_int64()),
                         TVFSchemaColumn("Value", types_->get_string())},
                        *catalog_));

  const StructType* key_value_struct = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(types_->MakeStructType(
      {{"Key", types_->get_int64()}, {"Value", types_->get_string()}},
      &key_value_struct));
  GOOGLESQL_RETURN_IF_ERROR(
      AddTableBackedTvf("tvf_key_value_vt",
                        /*tvf_columns=*/
                        {
                            TVFSchemaColumn("$value", key_value_struct),
                        },
                        *catalog_,
                        /*is_value_table=*/true));

  GOOGLESQL_RETURN_IF_ERROR(
      AddTableBackedTvf("tvf_key_value_vt_with_pseudo",
                        /*tvf_columns=*/
                        {
                            TVFSchemaColumn("$value", key_value_struct),
                            TVFSchemaColumn("pseudo_col", types_->get_int64(),
                                            /*is_pseudo_column_in=*/true),
                        },
                        *catalog_,
                        /*is_value_table=*/true));

  // A TVF that dynamically looks up a table via its first argument which
  // must be a string literal specifying table name. The TVF returns all
  // columns from the looked up table.
  catalog_->AddOwnedTableValuedFunction(std::make_unique<TvfWithTableSchema>(
      std::vector<std::string>{"tvf_lookup_table"},
      std::vector<FunctionSignature>{FunctionSignature(
          ARG_TYPE_RELATION, /*arguments=*/{types_->get_string()},
          /*context_id=*/0, /*options=*/{})}));

  return absl::OkStatus();
}

// Add a SQL table function to catalog starting from a full create table
// function statement.
void SampleCatalogImpl::AddSqlDefinedTableFunctionFromCreate(
    absl::string_view create_table_function,
    const LanguageOptions& language_options, absl::string_view user_id_column) {
  // Ensure the language options used allow CREATE FUNCTION
  LanguageOptions language = language_options;
  language.AddSupportedStatementKind(RESOLVED_CREATE_TABLE_FUNCTION_STMT);
  language.EnableLanguageFeature(FEATURE_CREATE_TABLE_FUNCTION);
  language.EnableLanguageFeature(FEATURE_TABLE_VALUED_FUNCTIONS);
  language.EnableLanguageFeature(FEATURE_TEMPLATE_FUNCTIONS);
  language.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  language.EnableLanguageFeature(FEATURE_GROUP_BY_STRUCT);
  language.EnableLanguageFeature(FEATURE_INLINE_LAMBDA_ARGUMENT);
  language.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
  AnalyzerOptions analyzer_options;
  analyzer_options.set_language(language);
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  CHECK_OK_UNLESS_DBG(AnalyzeStatement(create_table_function, analyzer_options,
                                       catalog_.get(), catalog_->type_factory(),
                                       &analyzer_output));
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  ABSL_CHECK(resolved->Is<ResolvedCreateTableFunctionStmt>());
  const auto* resolved_create =
      resolved->GetAs<ResolvedCreateTableFunctionStmt>();

  std::unique_ptr<TableValuedFunction> function;
  if (resolved_create->query() != nullptr) {
    std::unique_ptr<SQLTableValuedFunction> sql_tvf;
    CHECK_OK_UNLESS_DBG(
        SQLTableValuedFunction::Create(resolved_create, &sql_tvf));
    function = std::move(sql_tvf);
  } else {
    function = std::make_unique<TemplatedSQLTVF>(
        resolved_create->name_path(), resolved_create->signature(),
        resolved_create->argument_name_list(),
        ParseResumeLocation::FromString(resolved_create->code()));
  }

  if (!user_id_column.empty()) {
    GOOGLESQL_CHECK_OK(function->SetUserIdColumnNamePath({std::string(user_id_column)}));
  }
  catalog_->AddOwnedTableValuedFunction(std::move(function));
  sql_object_artifacts_.emplace_back(std::move(analyzer_output));
}

void SampleCatalogImpl::LoadNonTemplatedSqlTableValuedFunctions(
    const LanguageOptions& language_options) {
  LanguageOptions language_options_with_aggregate_filtering = language_options;
  language_options_with_aggregate_filtering.EnableLanguageFeature(
      FEATURE_AGGREGATE_FILTERING);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectWithUserId()
         AS SELECT 1 AS a, 2 AS b;)",
      language_options, "a");

  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelect()
         AS SELECT 1 AS a, 2 AS b;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectUnion()
         AS SELECT 1 AS a, 2 AS b
            UNION ALL
            SELECT 1, 4;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectCTE()
         AS WITH t AS (SELECT 1 AS a, 2 AS b) SELECT * FROM t;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectFromTvf()
         AS SELECT * FROM NullarySelect();)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectCallingScalarUDF()
         AS SELECT NullaryPi() AS pi;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullarySelectCallingLambdaArgFunction()
         AS SELECT ARRAY_FILTER([1, 2, 3], e -> e > 1) as arr;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryScalarArg(arg0 INT64)
         AS SELECT arg0;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryScalarArgMultipleReferences(arg0 INT64)
         AS SELECT arg0 + arg0 AS ret0, arg0 AS ret1;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION BinaryScalarArg(arg0 INT64, arg1 INT64)
         AS SELECT arg0, arg1, arg0 + arg1 AS ret2;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryScalarArgSubqueryReference(arg0 INT64)
         AS SELECT (SELECT arg0) AS ret0;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryScalarArgSubqueryWithReference(arg0 INT64)
         AS SELECT (WITH t AS (SELECT arg0) SELECT t.arg0 FROM t) AS ret0;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryTableArg(arg0 TABLE<a INT64>)
         AS SELECT * FROM arg0;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION UnaryTableArgColumnsUnused(arg0 TABLE<key INT64, value INT64>)
           AS SELECT 1 as result FROM arg0;)",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
            CREATE TABLE FUNCTION ScalarParamUsedAsTVFArgument(arg0 INT64)
            AS (SELECT * FROM UnaryScalarArg(arg0))
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION UnaryAbTableArg(arg0 TABLE<a INT64, b STRING>)
          AS SELECT * FROM arg0;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION UnaryAbTableArgSelfJoin(
            arg0 TABLE<a INT64, b STRING>)
          AS SELECT * FROM arg0 CROSS JOIN arg0 AS t1;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION UnaryAbTableArgScannedInCTE(
             arg0 TABLE<a INT64, b STRING>)
          AS WITH t AS (SELECT * FROM arg0) SELECT * FROM t;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION BinaryTableArg(
             arg0 TABLE<a INT64, b STRING>, arg1 TABLE<c INT64, d STRING>)
          AS SELECT * FROM arg0 CROSS JOIN arg1;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION BinaryAbTableArg(
            arg0 TABLE<a INT64, b STRING>, arg1 TABLE<a INT64, b STRING>)
          AS SELECT * FROM arg0 CROSS JOIN arg1;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION UnaryAbTableArgWithScalarArgs(
            x INT64, arg0 TABLE<a INT64, b STRING>, y STRING)
          AS SELECT * FROM arg0 WHERE arg0.a = x AND arg0.b = y;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION UnaryAbTableArgWithScalarArgsTempl(
            x ANY TYPE, arg0 ANY TABLE, y ANY TYPE)
          AS SELECT * FROM arg0 WHERE arg0.a = x AND arg0.b = y;
      )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION CallsUnaryAbTableArgWithScalarArgsTempl(
            ignored_param ANY TYPE)
          AS SELECT * FROM UnaryAbTableArgWithScalarArgsTempl(
              1, (SELECT 1 a, "2" b, DATE '2020-08-22' AS c), "b");
      )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION JoinsTableArgToScannedTable(
            arg_table TABLE<key INT64, value INT64>, arg_scalar STRING
          )
          AS SELECT arg_scalar AS c, *
          FROM TwoIntegers JOIN arg_table USING (key, value);
      )sql",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION TvfMultiAggGroupingConstants(
            TwoIntsTableArg TABLE<first INT64, second INT64>, int64_arg INT64)
            AS
          SELECT
            SUM(first + int64_arg GROUP BY first) AS result
          FROM TwoIntsTableArg;
      )sql",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION TvfMultiAggGroupingConstantsWithStruct(
            arg_table TABLE<STRUCT<a STRUCT<b INT64>>>, int64_arg INT64)
            AS
          SELECT
            SUM(arg_table.a.b + int64_arg GROUP BY arg_table.a) AS result
          FROM arg_table;
      )sql",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION TvfMultiAggNullary()
            AS
          WITH t AS (SELECT 1 AS a, 2 AS b)
          SELECT SUM(COUNT(a GROUP BY b) + COUNT(b GROUP BY a)
          GROUP BY a, b) as result FROM t;
        )sql",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION TvfMultiAggReferencesScannedTable(
            arg_table TABLE<key INT64, value INT64>, arg_scalar INT64
          )
          AS
          SELECT SUM(MAX(value) group by key) / SUM(MIN(key) group by value)
          + arg_scalar AS result
          FROM TwoIntegers JOIN arg_table USING (key, value);
        )sql",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION TvfMultiAggScalarArgGroupingConst(
            arg_table TABLE<key INT64, value INT64>, arg_scalar INT64
          )
          AS
          SELECT SUM(COUNT(key) + arg_scalar GROUP BY value) as result
          FROM arg_table;
        )sql",
      language_options);

  // Functions for definer-rights inlining
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION DefinerRightsTvf(a INT64) SQL SECURITY DEFINER
          AS SELECT * FROM  KeyValue WHERE KeyValue.Key = a; )sql",
      language_options);

  if (language_options.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT) &&
      language_options.LanguageFeatureEnabled(FEATURE_ANNOTATION_FRAMEWORK)) {
    AddSqlDefinedTableFunctionFromCreate(
        R"(CREATE TABLE FUNCTION ScalarArgWithCollatedOutputCols(arg0 STRING)
         AS SELECT
           COLLATE(arg0, 'und:ci') AS col_ci,
           [COLLATE(arg0, 'und:ci')] AS col_array_ci,
           ([COLLATE(arg0, 'und:ci')], 1) AS col_struct_ci;)",
        language_options);
    AddSqlDefinedTableFunctionFromCreate(
        R"(CREATE TABLE FUNCTION TableArgWithCollatedOutputCols(
          arg0 TABLE<a STRING>)
         AS SELECT
           COLLATE(a, 'und:ci') AS col_ci,
           [COLLATE(a, 'und:ci')] AS col_array_ci,
           ([COLLATE(a, 'und:ci')], 1) AS col_struct_ci
         FROM arg0;)",
        language_options);
    AddSqlDefinedTableFunctionFromCreate(
        R"(CREATE TABLE FUNCTION ValueTableArgWithCollatedOutputCols(
          arg0 TABLE<STRUCT<str_field STRING>>)
         AS SELECT
           COLLATE(str_field, 'und:ci') AS col_ci,
           [COLLATE(str_field, 'und:ci')] AS col_array_ci,
           ([COLLATE(str_field, 'und:ci')], 1) AS col_struct_ci
         FROM arg0;)",
        language_options);
    AddSqlDefinedTableFunctionFromCreate(
        R"(CREATE TABLE FUNCTION ScalarArgWithCollatedOutputValueTableCol(
          arg0 STRING)
         AS SELECT AS VALUE COLLATE(arg0, 'und:ci');)",
        language_options);
    AddSqlDefinedTableFunctionFromCreate(
        R"(CREATE TABLE FUNCTION ScalarArgsOfCollatableTypes(
          arg0 STRING, arg1 ARRAY<STRING>, arg2 STRUCT<STRING, INT64>)
         AS SELECT arg0, arg1, arg2;)",
        language_options);
  }
  if (language_options.LanguageFeatureEnabled(FEATURE_ORDER_BY_IN_AGGREGATE)) {
    AddSqlDefinedTableFunctionFromCreate(
        R"sql(
          CREATE TABLE FUNCTION UnaryTableArgAggregatedWithOrderBy(
             arg0 TABLE<a INT64>)
          AS WITH t AS (SELECT ARRAY_AGG(a ORDER BY a) AS arr FROM arg0)
             SELECT * FROM t;
        )sql",
        language_options);
  }

  // We want a TVF whose name is close to a scalar function to trigger
  // Did you mean logic when accidentally calling the TVF.
  AddSqlDefinedTableFunctionFromCreate(
      R"(CREATE TABLE FUNCTION NullaryPIT()
         AS SELECT 3.141 AS a;)",
      language_options);

  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION select_star_from_pseudo_columns_table(
            table_with_pseudo_columns ANY TABLE)
        AS SELECT * FROM table_with_pseudo_columns;
      )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION select_pseudo_columns_from_table(
            table_with_pseudo_columns ANY TABLE)
        AS SELECT key, Filename, RowId FROM table_with_pseudo_columns;
      )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION select_dot_pseudo_columns_from_table(
            table_with_pseudo_columns ANY TABLE)
        AS SELECT t.key, t.Filename, t.RowId FROM table_with_pseudo_columns t;
      )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION no_arg_tvf_containing_filter_returns_one()
        AS (
          (SELECT COUNT(* WHERE x > 2) AS result FROM UNNEST([1,2,3]) AS x)
        );
      )sql",
      language_options_with_aggregate_filtering);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
        CREATE TABLE FUNCTION templated_tvf_containing_filter_returns_one(unused ANY TABLE)
        AS (
          (SELECT COUNT(* WHERE x > 2) AS result FROM UNNEST([1,2,3]) AS x)
        );
      )sql",
      language_options_with_aggregate_filtering);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION select_all_from_pseudo_columns_value_table(
              value_table_with_pseudo_columns ANY TABLE)
          AS SELECT * FROM value_table_with_pseudo_columns;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION select_pseudo_columns_from_value_table(
              value_table_with_pseudo_columns ANY TABLE)
          AS SELECT t, Filename, RowId FROM value_table_with_pseudo_columns t;
        )sql",
      language_options);
  AddSqlDefinedTableFunctionFromCreate(
      R"sql(
          CREATE TABLE FUNCTION select_dot_pseudo_columns_from_value_table(
              value_table_with_pseudo_columns ANY TABLE)
          AS SELECT t, t.Filename, t.RowId FROM value_table_with_pseudo_columns t;
        )sql",
      language_options);
}

void SampleCatalogImpl::LoadTemplatedSQLTableValuedFunctions() {
  const std::string kColumnNameKey = "key";
  const std::string kColumnNameDate = "date";
  int context_id = 0;

  // Add a TVF with a simple valid templated SQL body.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_one"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{}, ParseResumeLocation::FromString("select 1 as x")));

  // Add a templated SQL TVF that calls another templated SQL TVF.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_call_tvf_templated_select_one"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select * from tvf_templated_select_one()")));

  // Add a templated SQL TVF that calls another templated SQL TVF twice.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_call_tvf_templated_select_one_twice"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select * from tvf_templated_select_one() union all "
          "select * from tvf_templated_select_one()")));

  // Add a TVF with a valid templated SQL body that refers to a scalar argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_int64_arg"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(types::Int64Type())},
                        context_id++),
      /*arg_name_list=*/{"x"}, ParseResumeLocation::FromString("select x")));

  // Add a TVF with a valid templated SQL body that refers to a scalar argument
  // with a name that is potentially ambiguous with an in scope column name.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_int64_arg_with_name_ambiguity"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(types::Int64Type())},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("SELECT x FROM (SELECT -99 AS x)")));

  // Add a TVF with a valid templated SQL body that refers to a scalar argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_any_scalar_arg"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x"}, ParseResumeLocation::FromString("select x")));

  // Add a TVF with a valid templated SQL body that performs addition on a
  // scalar argument. The function signature accepts a single argument of any
  // scalar type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_arg_plus_integer"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select x + 42")));

  // Add a TVF with a valid templated SQL body that accepts an input argument
  // where the name contains '$'. The function signature accepts a single
  // argument of any scalar type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_arg_plus_integer_accept_dollars_col_name"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"$col1"},
      ParseResumeLocation::FromString("select `$col1` as x")));

  // Add a TVF with a valid templated SQL body that returns an output column
  // where the name contains '$'. The function signature accepts a single
  // argument of any scalar type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_arg_plus_integer_return_dollars_col_name"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select x as `$col1`")));

  // Add a TVF with a valid templated SQL body that performs concatenation on a
  // scalar argument. The function signature accepts a single argument of any
  // scalar type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_arg_concat_string"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select concat(x, 'abc') as y")));

  // Add a TVF with a valid templated SQL body that performs a proto field
  // access on a scalar argument. The function signature accepts a single
  // argument of any scalar type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_arg_proto_field_access"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select x.int32_field as y")));

  // Add a TVF with a valid templated SQL body that refers to a relation
  // argument using specific column names.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_relation_arg_using_column_names"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"t"},
      ParseResumeLocation::FromString("select key, value from t")));

  // Add a TVF with a simple valid templated SQL body that selects a name from
  // a templated input table argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_a"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select a from x")));

  // Add a TVF with a valid templated SQL body that refers to a relation
  // argument using "select *".
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_relation_arg_using_select_star"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"t"},
      ParseResumeLocation::FromString("select * from t")));

  // Add a TVF with a valid templated SQL body that refers to a relation
  // argument but does not use its columns.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_relation_arg_columns_unused"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"t"},
      ParseResumeLocation::FromString("select 1 as result from t")));

  // Add a TVF with a templated SQL body that refers to a relation argument
  // using "select 1". The TVF is missing an output column name.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_relation_arg_using_select_one"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"t"},
      ParseResumeLocation::FromString("(select 1 from t limit 1)")));

  // Add a TVF with a valid templated SQL body that refers to a relation
  // argument and also a table in the catalog.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_relation_arg_and_catalog_table"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"t"},
      ParseResumeLocation::FromString(
          "(select * from t) union all (select * from keyvalue)")));

  // Add a TVF with a valid templated SQL body that refers to two relation
  // arguments and uses a SQL WITH clause.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_two_relation_args"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation(),
                         FunctionArgumentType::AnyRelation()},
                        context_id++),
      /*arg_name_list=*/{"s", "t"},
      ParseResumeLocation::FromString(
          "with w1 as (select * from s),\n"
          "     w2 as (select * from t)\n"
          "select * from w1 inner join w2 using (key) order by key limit 1")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_multi_agg_valid_grouping_constants"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"int64_arg"},
      ParseResumeLocation::FromString("SELECT SUM(key + int64_arg GROUP BY "
                                      "key) AS result FROM TwoIntegers")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_multi_agg_invalid_grouping_constants"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                              FunctionArgumentType::REQUIRED)},
                        context_id++),
      /*arg_name_list=*/{"int64_arg"},
      ParseResumeLocation::FromString("SELECT SUM(value + int64_arg GROUP BY "
                                      "key) AS result FROM TwoIntegers")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_multi_agg_valid_grouping_constants_with_struct"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"arg_table"},
      ParseResumeLocation::FromString(
          "SELECT SUM(arg_table.a.b GROUP BY arg_table.a) AS "
          "result FROM arg_table")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_call_udf_with_multi_agg"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"arg_table"},
      ParseResumeLocation::FromString("SELECT NullaryWithMultilevelAgg() AS "
                                      "result")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_call_uda_with_multi_agg"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType::AnyRelation()}, context_id++),
      /*arg_name_list=*/{"arg_table"},
      ParseResumeLocation::FromString(
          "SELECT SumOfValuesForDistinctKey(key, value) AS "
          "result FROM arg_table")));

  // Add a TVF with a valid templated SQL body that refers to both a scalar
  // argument and a relation argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_scalar_and_relation_args"},
      FunctionSignature(
          ARG_TYPE_RELATION,
          {FunctionArgumentType(types::Int64Type()),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({{kColumnNameKey, types::Int64Type()}}),
               /*extra_relation_input_columns_allowed=*/true)},
          context_id++),
      /*arg_name_list=*/{"x", "t"},
      ParseResumeLocation::FromString("select key from t where key < x")));

  // This is the same TVF as above, but without a schema specified for the
  // relation argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_scalar_and_relation_args_no_schema"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(types::Int64Type()),
                         FunctionArgumentType::AnyRelation()},
                        context_id++),
      /*arg_name_list=*/{"x", "t"},
      ParseResumeLocation::FromString("select key from t where key < x")));

  // This is the same TVF as above, but without a schema specified for the
  // relation argument.
  const StructType* arg_type = nullptr;
  std::vector<StructType::StructField> fields{{"y", types::Int64Type()},
                                              {"z", types::StringType()}};
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(fields, &arg_type));
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_with_struct_param"},
      FunctionSignature(
          ARG_TYPE_RELATION,
          {FunctionArgumentType(arg_type), FunctionArgumentType::AnyRelation()},
          context_id++),
      /*arg_name_list=*/{"x", "t"},
      ParseResumeLocation::FromString("select x.y from t as x")));

  // Add a TVF with a valid templated SQL body that refers to both a scalar
  // date argument and a relation argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_scalar_date_and_relation_args"},
      FunctionSignature(
          ARG_TYPE_RELATION,
          {FunctionArgumentType(types::DateType()),
           FunctionArgumentType::RelationWithSchema(
               TVFRelation({{kColumnNameDate, types::DateType()}}),
               /*extra_relation_input_columns_allowed=*/true)},
          context_id++),
      /*arg_name_list=*/{"d", "t"},
      ParseResumeLocation::FromString(
          "select `date` from t where `date` < d")));

  // Add a TVF with a valid templated SQL body that refers to both a scalar
  // argument of any type and a relation argument of any table.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_any_scalar_and_relation_args"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY),
                         FunctionArgumentType::AnyRelation()},
                        context_id++),
      /*arg_name_list=*/{"s", "t"},
      ParseResumeLocation::FromString("select *, s from t")));

  // Add an invalid TVF with a simple templated SQL body missing an output
  // column name.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_one_missing_col_name"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{}, ParseResumeLocation::FromString("select 1")));

  // Add an invalid templated SQL TVF with a parse error in the function body.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_parse_error"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{}, ParseResumeLocation::FromString("a b c d e")));

  // Add an invalid templated SQL TVF with an analysis error in the function
  // body.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_analysis_error"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString("select * from invalidtable")));

  // Add an invalid templated SQL TVF where the function body is not a query.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_function_body_not_query"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "insert keyvalue (key, value) values (1, 'one')")));

  // Add an invalid templated SQL TVF that attempts to refer to a query
  // parameter.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_function_body_refer_to_parameter"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select @test_param_bool from keyvalue")));

  // Add an invalid templated SQL TVF where the function body is empty.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_function_body_empty"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{}, ParseResumeLocation::FromString("")));

  // Add an invalid templated SQL TVF that directly calls itself.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_recursive"}, FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString("select * from tvf_recursive()")));

  // Add two invalid templated SQL TVFs that indirectly call themselves.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_calls_self_indirectly_1"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select * from tvf_calls_self_indirectly_2()")));

  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_calls_self_indirectly_2"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select * from tvf_calls_self_indirectly_1()")));

  // Add a templated SQL TVF that calls a templated SQL function that calls the
  // original templated SQL TVF again, to make sure cycle detection works.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_calls_udf_calls_same_tvf"},
      FunctionSignature(ARG_TYPE_RELATION, {}, context_id++),
      /*arg_name_list=*/{},
      ParseResumeLocation::FromString(
          "select udf_calls_tvf_calls_same_udf()")));

  // Add a templated SQL TVF that calls a TVF that triggers a deprecation
  // warning.
  FunctionSignature deprecation_warning_signature(
      ARG_TYPE_RELATION, /*arguments=*/{}, context_id++);
  deprecation_warning_signature.SetAdditionalDeprecationWarnings(
      {CreateDeprecationWarning(/*id=*/1001)});
  catalog_->AddOwnedTableValuedFunction(
      new TemplatedSQLTVF({"tvf_templated_calls_tvf_deprecation_warning"},
                          deprecation_warning_signature,
                          /*arg_name_list=*/{},
                          ParseResumeLocation::FromString(
                              "select * from tvf_deprecation_warning()")));

  FunctionSignature signature_return_key_int64_col(
      FunctionArgumentType::RelationWithSchema(
          TVFRelation({{kColumnNameKey, types::Int64Type()}}),
          /*extra_relation_input_columns_allowed=*/true),
      /*arguments=*/{FunctionArgumentType(ARG_TYPE_ARBITRARY)}, context_id++);
  FunctionSignature signature_return_key_int64_and_value_string_cols(
      FunctionArgumentType::RelationWithSchema(
          TVFRelation({{kColumnNameKey, types::Int64Type()},
                       {kColumnNameValue, types::StringType()}}),
          /*extra_relation_input_columns_allowed=*/true),
      /*arguments=*/
      {FunctionArgumentType(ARG_TYPE_ARBITRARY),
       FunctionArgumentType(ARG_TYPE_ARBITRARY)},
      context_id++);
  FunctionSignature signature_return_value_table_string_col(
      FunctionArgumentType::RelationWithSchema(
          TVFRelation::ValueTable(types::StringType()),
          /*extra_relation_input_columns_allowed=*/true),
      /*arguments=*/{FunctionArgumentType(ARG_TYPE_ARBITRARY)}, context_id++);

  // Add a templated TVF with a required signature of a single INT64 column
  // named "key".
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_x_with_required_output_schema"},
      signature_return_key_int64_col,
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select x as key")));

  // Add an invalid templated TVF that returns a duplicate column name.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_duplicate_output_column_with_required_output_schema"},
      signature_return_key_int64_col,
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select x as key, 42 as key")));

  // Add a templated TVF with a required non-value-table output schema.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_missing_output_column_with_required_output_schema"},
      signature_return_key_int64_col,
      /*arg_name_list=*/{"x"}, ParseResumeLocation::FromString("select x")));

  // Add a templated TVF with a required value-table output schema that returns
  // a value table.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_as_value_x_with_required_"
       "value_table_output_schema"},
      signature_return_value_table_string_col,
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select as value x")));

  // Add a templated TVF with a required value-table output schema that returns
  // a non-value table of the same type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_x_with_required_value_table_output_schema"},
      signature_return_value_table_string_col,
      /*arg_name_list=*/{"x"}, ParseResumeLocation::FromString("select x")));

  // Add a templated TVF with a required value-table output schema that returns
  // NULL.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_null_with_required_value_table_output_schema"},
      signature_return_value_table_string_col,
      /*arg_name_list=*/{"x"}, ParseResumeLocation::FromString("select null")));

  // Add a templated TVF with a required value-table output schema that returns
  // NULL casted to string type.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_null_str_with_required_value_table_output_schema"},
      signature_return_value_table_string_col,
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select cast(null as string)")));

  // Add a templated TVF with a required output schema with two columns. The
  // function body returns the two columns in opposite order.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_return_swapped_cols_required_output_schema"},
      signature_return_key_int64_and_value_string_cols,
      /*arg_name_list=*/{"key", "value"},
      ParseResumeLocation::FromString("select value, key")));

  // Add a templated TVF with a required output schema with two columns. The
  // function body returns the two columns in opposite order, plus an extra
  // column.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_return_swapped_cols_plus_extra_required_output_schema"},
      signature_return_key_int64_and_value_string_cols,
      /*arg_name_list=*/{"key", "value"},
      ParseResumeLocation::FromString("select value, key, 42 as x")));

  // Add a templated TVF which returns three columns of collated string type,
  // collated array type and struct type with collated field by calling COLLATE
  // function over input scalar argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_columns_with_collate_function"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY),
                         FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x", "y"},
      ParseResumeLocation::FromString(
          "select COLLATE(x, 'und:ci') as col_ci, [COLLATE(x, 'und:ci')] as "
          "col_array_ci, (COLLATE(x, 'und:ci'), y) as col_struct_ci")));

  // Add a templated TVF which returns three columns of collated string type,
  // collated array type and struct type with collated field by referencing
  // collated table column.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_columns_with_collated_column_ref"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString(
          "select CONCAT(x, string_ci) as concat_ci, array_with_string_ci, "
          "struct_with_string_ci from CollatedTable")));

  // Add a templated TVF which returns columns of collated types with input
  // relation argument.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_columns_with_relation_arg"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_RELATION),
                         FunctionArgumentType(types::Int64Type())},
                        context_id++),
      /*arg_name_list=*/{"x", "y"},
      ParseResumeLocation::FromString(
          "select COLLATE(col_str, 'und:ci') as col_ci, [COLLATE(col_str, "
          "'und:ci')] as col_array_ci, (COLLATE(col_str, 'und:ci'), y) as "
          "col_struct_ci from x")));

  // Add a templated TVF which returns a value table with collated output
  // column.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_column_as_value_table"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY),
                         FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x", "y"},
      ParseResumeLocation::FromString(
          "select as value CONCAT(COLLATE(x, 'und:ci'), y)")));

  // Add a templated TVF which returns a collated output column and has an
  // explicit result schema.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_column_returns_int64_string_col"},
      signature_return_key_int64_and_value_string_cols,
      /*arg_name_list=*/{"x", "y"},
      ParseResumeLocation::FromString(
          "select x as key, COLLATE(y, 'und:ci') as value")));

  // Add a templated TVF which returns a collated output column and has an
  // explicit value table result schema.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_collated_output_column_returns_value_table_string_"
       "col"},
      signature_return_value_table_string_col,
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString("select as value COLLATE(x, 'und:ci')")));

  // Add a templated TVF which returns a column of type STRING by calling the
  // TYPEOF function. Note that TYPEOF is implemented via a builtin GoogleSQL
  // rewriter.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_typeof_function"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString(R"sql(select TYPEOF(x) AS col)sql")));

  // A templated TVF which calls TYPEOF and has anonymization info.
  {
    catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
        {"tvf_templated_select_typeof_function_with_anonymization_info"},
        FunctionSignature(ARG_TYPE_RELATION,
                          {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                          context_id++),
        /*arg_name_list=*/{"x"},
        ParseResumeLocation::FromString(
            R"sql(select 1 AS user_id, TYPEOF(x) AS value)sql"),
        AnonymizationInfo::Create({"user_id"}).value_or(nullptr),
        /*tvf_options=*/{}));
  }

  {
    catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
        {"tvf_templated_select_array_max_function"},
        FunctionSignature(
            FunctionArgumentType::AnyRelation(),
            {FunctionArgumentType::AnyRelation(),
             FunctionArgumentType(ARG_TYPE_ARBITRARY,
                                  FunctionArgumentType::REQUIRED)},
            /*context_id=*/1),
        /*arg_name_list=*/{"InputTable", "threshold"},
        ParseResumeLocation::FromString(R"sql(
          WITH TempTable AS (
            SELECT
              ARRAY<INT64>[array_col[0], array_col[1]] AS first_two_elements,
              ARRAY<INT64>[array_col[2], array_col[3]] AS next_two_elements
            FROM InputTable
          )
          SELECT * FROM TempTable
          WHERE ARRAY_MAX(first_two_elements) < threshold AND
            ARRAY_MAX(next_two_elements) > threshold)sql")));
  }

  // A templated TVF which calls a templated TVF.
  catalog_->AddOwnedTableValuedFunction(new TemplatedSQLTVF(
      {"tvf_templated_select_nested_typeof_function"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/{"x"},
      ParseResumeLocation::FromString(
          R"sql(select * from tvf_templated_select_typeof_function(x))sql")));

  // Add a templated definer-rights TVF
  auto templated_definer_rights_tvf = std::make_unique<TemplatedSQLTVF>(
      std::vector<std::string>{"definer_rights_templated_tvf"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_ARBITRARY)},
                        context_id++),
      /*arg_name_list=*/std::vector<std::string>{"x"},
      ParseResumeLocation::FromString("select * from KeyValue WHERE key = x"));
  templated_definer_rights_tvf->set_sql_security(
      ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER);
  catalog_->AddOwnedTableValuedFunction(
      std::move(templated_definer_rights_tvf));

  // b/259000660: Add a templated SQL TVF whose code has a braced proto
  // constructor.
  auto templated_proto_braced_ctor_tvf = std::make_unique<TemplatedSQLTVF>(
      std::vector<std::string>{"templated_proto_braced_ctor_tvf"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_RELATION)},
                        context_id++),
      /*arg_name_list=*/std::vector<std::string>{"T"},
      ParseResumeLocation::FromString(R"sql(
  SELECT NEW googlesql_test.TestExtraPB {int32_val1 : v} AS dice_roll
  FROM T)sql"));
  catalog_->AddOwnedTableValuedFunction(
      std::move(templated_proto_braced_ctor_tvf));
  auto templated_struct_braced_ctor_tvf = std::make_unique<TemplatedSQLTVF>(
      std::vector<std::string>{"templated_struct_braced_ctor_tvf"},
      FunctionSignature(ARG_TYPE_RELATION,
                        {FunctionArgumentType(ARG_TYPE_RELATION)},
                        context_id++),
      /*arg_name_list=*/std::vector<std::string>{"T"},
      ParseResumeLocation::FromString(R"sql(
  SELECT STRUCT<int32_val1 INT32> {int32_val1 : v} AS dice_roll
  FROM T)sql"));
  catalog_->AddOwnedTableValuedFunction(
      std::move(templated_struct_braced_ctor_tvf));
}

void SampleCatalogImpl::LoadTableValuedFunctionsWithAnonymizationUid() {
  // Generate an output schema that returns every possible type.
  const std::vector<OutputColumn> output_columns =
      GetOutputColumnsForAllTypes(types_);
  TVFRelation::ColumnList columns;
  columns.reserve(output_columns.size());
  for (const auto& kv : output_columns) {
    columns.emplace_back(kv.name, kv.type);
  }
  TVFRelation output_schema_all_types(columns);

  int context_id = 0;
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_with_anonymization_uid"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_all_types,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      AnonymizationInfo::Create({"column_int64"}).value_or(nullptr),
      output_schema_all_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_relation_arg_with_anonymization_uid"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_all_types,
              /*extra_relation_input_columns_allowed=*/false),
          FunctionArgumentTypeList({FunctionArgumentType(ARG_TYPE_RELATION)}),
          context_id++)},
      AnonymizationInfo::Create({"column_int64"}).value_or(nullptr),
      output_schema_all_types));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_one_templated_arg_with_anonymization_uid"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema_all_types,
              /*extra_relation_input_columns_allowed=*/false),
          FunctionArgumentTypeList({FunctionArgumentType(ARG_TYPE_ARBITRARY)}),
          context_id++)},
      AnonymizationInfo::Create({"column_int64"}).value_or(nullptr),
      output_schema_all_types));

  TVFRelation output_schema_proto(
      {{"user_info", GetProtoType(googlesql_test::TestExtraPB::descriptor())}});

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_with_nested_anonymization_uid"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_proto,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      AnonymizationInfo::Create({"user_info", "int32_val1"}).value_or(nullptr),
      output_schema_proto));

  TVFRelation output_schema_proto_value_table = TVFRelation::ValueTable(
      GetProtoType(googlesql_test::TestExtraPB::descriptor()));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_value_table_with_anonymization_uid"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_proto_value_table,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      AnonymizationInfo::Create({"int32_val1"}).value_or(nullptr),
      output_schema_proto_value_table));

  TVFRelation output_schema_proto_value_table_with_nested_int =
      TVFRelation::ValueTable(
          GetProtoType(googlesql_test::KitchenSinkPB::descriptor()));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_value_table_with_nested_anonymization_uid"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_proto_value_table_with_nested_int,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      AnonymizationInfo::Create({"nested_value", "nested_int64"})
          .value_or(nullptr),
      output_schema_proto_value_table_with_nested_int));

  // Repro for unvalidated AnonymizationInfo::UserIdColumnNamePath() seen in
  // b/254939522
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_no_args_value_table_with_invalid_nested_anonymization_uid"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema_proto_value_table_with_nested_int,
                             /*extra_relation_input_columns_allowed=*/false),
                         FunctionArgumentTypeList(), context_id++)},
      AnonymizationInfo::Create({"nested_value.nested_int64"})
          .value_or(nullptr),
      output_schema_proto_value_table_with_nested_int));
}

void SampleCatalogImpl::LoadTableValuedFunctionsWithOptionalRelations() {
  TVFRelation output_schema = TVFRelation::ValueTable(types_->get_int64());

  int64_t context_id = 0;
  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_relation_named_scalar"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(googlesql::ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               types_->get_string(),
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_argument_name("foobar", kNamedOnly))},
          context_id)},
      output_schema));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_relations_named_scalar"},
      {FunctionSignature(
          FunctionArgumentType::RelationWithSchema(
              output_schema,
              /*extra_relation_input_columns_allowed=*/false),
          {FunctionArgumentType(googlesql::ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(googlesql::ARG_TYPE_RELATION,
                                FunctionArgumentType::OPTIONAL),
           FunctionArgumentType(
               types_->get_string(),
               FunctionArgumentTypeOptions()
                   .set_cardinality(FunctionArgumentType::OPTIONAL)
                   .set_argument_name("foobar", kNamedOnly))},
          context_id)},
      output_schema));

  catalog_->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      {"tvf_optional_relation_optional_scalar"},
      {FunctionSignature(FunctionArgumentType::RelationWithSchema(
                             output_schema,
                             /*extra_relation_input_columns_allowed=*/false),
                         {FunctionArgumentType(googlesql::ARG_TYPE_RELATION,
                                               FunctionArgumentType::OPTIONAL),
                          FunctionArgumentType(googlesql::types::StringType(),
                                               FunctionArgumentType::OPTIONAL)},
                         context_id)},
      output_schema));
}

void SampleCatalogImpl::AddProcedureWithArgumentType(std::string type_name,
                                                     const Type* arg_type) {
  auto procedure = absl::WrapUnique(
      new Procedure({absl::StrCat("proc_on_", type_name)},
                    {types_->get_bool(), {arg_type}, /*context_id=*/-1}));
  catalog_->AddOwnedProcedure(std::move(procedure));
}

void SampleCatalogImpl::LoadProcedures() {
  Procedure* procedure = nullptr;

  // Procedure with no arguments.
  procedure = new Procedure({"proc_no_args"},
                            {types_->get_bool(), {}, /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Procedure with no arguments and void return type.
  procedure = new Procedure({"proc_no_args_void_return"},
                            {ARG_TYPE_VOID, {}, -1 /* context */});
  catalog_->AddOwnedProcedure(procedure);

  // Procedure with arguments and void return type.
  procedure =
      new Procedure({"proc_with_args_void_return"},
                    {ARG_TYPE_VOID, {types_->get_int64()}, -1 /* context */});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure that takes a specific enum as an argument.
  const EnumType* enum_TestEnum =
      GetEnumType(googlesql_test::TestEnum_descriptor());
  procedure =
      new Procedure({"proc_on_TestEnum"},
                    {types_->get_bool(), {enum_TestEnum}, /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure to illustrate how repeated/optional arguments are resolved.
  procedure =
      new Procedure({"proc_on_req_opt_rep"},
                    {types_->get_int64(),
                     {{types_->get_int64(), FunctionArgumentType::REQUIRED},
                      {types_->get_int64(), FunctionArgumentType::REPEATED},
                      {types_->get_int64(), FunctionArgumentType::REPEATED},
                      {types_->get_int64(), FunctionArgumentType::REQUIRED},
                      {types_->get_int64(), FunctionArgumentType::OPTIONAL}},
                     /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure with templated arguments.
  procedure =
      new Procedure({"proc_on_any_any"}, {types_->get_int64(),
                                          {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
                                          /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure with templated arguments of arbitrary type.
  procedure = new Procedure({"proc_on_arbitrary_arbitrary"},
                            {types_->get_int64(),
                             {ARG_TYPE_ARBITRARY, ARG_TYPE_ARBITRARY},
                             /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure with one repeated argument.
  procedure = new Procedure(
      {"proc_on_rep"}, {types_->get_int64(),
                        {{types_->get_int64(), FunctionArgumentType::REPEATED}},
                        /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // Add a procedure with one optional argument.
  procedure = new Procedure(
      {"proc_on_opt"}, {types_->get_int64(),
                        {{types_->get_int64(), FunctionArgumentType::OPTIONAL}},
                        /*context_id=*/-1});
  catalog_->AddOwnedProcedure(procedure);

  // These sample procedures are named 'proc_on_<typename>' with one argument of
  // type <typename> that returns a bool.
  AddProcedureWithArgumentType("bool", types_->get_bool());
  AddProcedureWithArgumentType("int32", types_->get_int32());
  AddProcedureWithArgumentType("int64", types_->get_int64());
  AddProcedureWithArgumentType("uint32", types_->get_uint32());
  AddProcedureWithArgumentType("uint64", types_->get_uint64());
  AddProcedureWithArgumentType("float", types_->get_float());
  AddProcedureWithArgumentType("double", types_->get_double());
  AddProcedureWithArgumentType("date", types_->get_date());
  AddProcedureWithArgumentType("timestamp", types_->get_timestamp());
  AddProcedureWithArgumentType("string", types_->get_string());

  // Add a procedure with named_lambda.
  {
    FunctionArgumentType named_lambda = FunctionArgumentType::Lambda(
        {ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1,
        FunctionArgumentTypeOptions().set_argument_name("named_lambda",
                                                        kNamedOnly));
    auto procedure = std::make_unique<Procedure>(
        std::vector<std::string>{"proc_on_named_lambda"},
        FunctionSignature{types_->get_int64(),
                          {ARG_TYPE_ANY_1, named_lambda},
                          /*context_id=*/-1});
    catalog_->AddOwnedProcedure(std::move(procedure));
  }
}

void SampleCatalogImpl::LoadConstants() {
  // Load constants that are owned by 'catalog_'.
  std::unique_ptr<SimpleConstant> int64_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"TestConstantInt64"},
                                  Value::Int64(1L), &int64_constant));
  catalog_->AddOwnedConstant(int64_constant.release());
  std::unique_ptr<SimpleConstant> string_constant;
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create(std::vector<std::string>{"TestConstantString"},
                             Value::String("foo"), &string_constant));
  catalog_->AddOwnedConstant(string_constant.release());

  std::unique_ptr<SimpleConstant> bool_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"TestConstantTrue"},
                                  Value::Bool(true), &bool_constant));
  catalog_->AddOwnedConstant(std::move(bool_constant));
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"TestConstantFalse"},
                                  Value::Bool(false), &bool_constant));
  catalog_->AddOwnedConstant(std::move(bool_constant));
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create(std::vector<std::string>{"TestConstantNullBool"},
                             Value::NullBool(), &bool_constant));
  catalog_->AddOwnedConstant(std::move(bool_constant));

  std::unique_ptr<SimpleConstant> string_constant_nonstandard_name;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"Test Constant-String"},
      Value::String("foo bar"), &string_constant_nonstandard_name));
  catalog_->AddOwnedConstant(string_constant_nonstandard_name.release());

  // Load a constant that is not owned by 'catalog_'.
  const ProtoType* const proto_type =
      GetProtoType(googlesql_test::KitchenSinkPB::descriptor());
  googlesql_test::KitchenSinkPB proto_value;
  proto_value.set_int64_key_1(1);
  proto_value.set_int64_key_2(-999);
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"TestConstantProto"},
      Value::Proto(proto_type, proto_value.SerializeAsCord()),
      &owned_constant_));
  catalog_->AddConstant(owned_constant_.get());

  // Load a constant that conflicts with a table.
  const StructType* table_struct_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType({{"key", types_->get_int32()}},
                                  &table_struct_type));
  std::unique_ptr<SimpleConstant> table_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"NameConflictTable"},
      Value::Struct(table_struct_type, {Value::Int32(-3456)}),
      &table_constant));
  catalog_->AddOwnedConstant(table_constant.release());

  // Load a constant that conflicts with a value table.
  std::unique_ptr<SimpleConstant> value_table_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"Int32ValueTable"},
                                  Value::Int32(3), &value_table_constant));
  catalog_->AddOwnedConstant(value_table_constant.release());

  // Load a constant that conflicts with a type.
  std::unique_ptr<SimpleConstant> type_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"NameConflictType"},
                                  Value::Bool(false), &type_constant));
  catalog_->AddOwnedConstant(type_constant.release());

  // Load a constant that conflicts with zero-argument functions.
  std::unique_ptr<SimpleConstant> zero_argument_function_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"sort_count"},
                                  Value::Int64(4),
                                  &zero_argument_function_constant));
  catalog_->AddOwnedConstant(zero_argument_function_constant.release());

  std::unique_ptr<SimpleConstant>
      zero_argument_function_constant_with_optional_parentheses;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"CURRENT_DATE"}, Value::Int64(4),
      &zero_argument_function_constant_with_optional_parentheses));
  catalog_->AddOwnedConstant(
      zero_argument_function_constant_with_optional_parentheses.release());

  // Load a constant that conflicts with a multi-argument function.
  std::unique_ptr<SimpleConstant> multi_argument_function_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"concat"},
                                  Value::Int64(5),
                                  &multi_argument_function_constant));
  catalog_->AddOwnedConstant(multi_argument_function_constant.release());

  // Load a constant that conflicts with a zero-argument TVF.
  std::unique_ptr<SimpleConstant> zero_argument_tvf_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"tvf_no_args"},
                                  Value::Int64(6),
                                  &zero_argument_tvf_constant));
  catalog_->AddOwnedConstant(zero_argument_tvf_constant.release());

  // Load a constant that conflicts with a multi-argument TVF.
  std::unique_ptr<SimpleConstant> multi_argument_tvf_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"tvf_exactly_1_int64_arg"}, Value::Int64(7),
      &multi_argument_tvf_constant));
  catalog_->AddOwnedConstant(multi_argument_tvf_constant.release());

  // Load a constant that conflicts with a zero-argument procedure.
  // The multi-argument case is handled in the nested catalog.
  std::unique_ptr<SimpleConstant> constant;
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create({"proc_no_args"}, Value::Bool(true), &constant));
  catalog_->AddOwnedConstant(constant.release());

  // Load a constant that conflicts with a catalog.
  const StructType* nested_struct_type;
  GOOGLESQL_CHECK_OK(types_->MakeStructType(
      {{"a", types_->get_int32()}, {"b", types_->get_int64()}},
      &nested_struct_type));
  const StructType* catalog_struct_type;
  GOOGLESQL_CHECK_OK(
      types_->MakeStructType({{"a", types_->get_int32()},
                              {"nested_catalog_catalog", nested_struct_type},
                              {"TestConstantBool", types_->get_bool()}},
                             &catalog_struct_type));
  std::unique_ptr<SimpleConstant> catalog_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"nested_catalog_with_catalog"},
      Value::Struct(catalog_struct_type,
                    {Value::Int32(-3456),
                     Value::Struct(nested_struct_type,
                                   {Value::Int32(-3434), Value::Int64(4333)}),
                     Value::Bool(false)}),
      &catalog_constant));
  catalog_->AddOwnedConstant(catalog_constant.release());

  // Load a constant that conflicts with an expression column in standalone
  // expression resolution.
  std::unique_ptr<SimpleConstant> standalone_expression_constant;
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create(std::vector<std::string>{"column_KitchenSink"},
                             Value::Int64(8), &standalone_expression_constant));
  catalog_->AddOwnedConstant(standalone_expression_constant.release());

  // Load a constant with a name that resembles a system variable.
  std::unique_ptr<SimpleConstant> sysvar1_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"@@sysvar1"},
                                  Value::Int64(8), &sysvar1_constant));
  catalog_->AddOwnedConstant(sysvar1_constant.release());

  std::unique_ptr<SimpleConstant> sysvar2_constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"@@sysvar2"},
                                  Value::Int64(8), &sysvar2_constant));
  catalog_->AddOwnedConstant(sysvar2_constant.release());

  // Script variables are managed by the ScriptExecutor. Eventually, they get
  // put into the catalog as constants. For testing, we'll add some "variables"
  // here.
  std::unique_ptr<SimpleConstant> string_variable_foo;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"string_variable_foo"},
      Value::String("string_variable_foo_value"), &string_variable_foo));
  catalog_->AddOwnedConstant(std::move(string_variable_foo));

  std::unique_ptr<SimpleConstant> string_variable_bar;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"string_variable_bar"},
      Value::String("string_variable_bar_value"), &string_variable_bar));
  catalog_->AddOwnedConstant(std::move(string_variable_bar));

  std::unique_ptr<SimpleConstant> int_variable_foo;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(std::vector<std::string>{"int_variable_foo"},
                                  Value::Int32(4), &int_variable_foo));
  catalog_->AddOwnedConstant(std::move(int_variable_foo));

  // Load constants with report format enum types
  std::unique_ptr<SimpleConstant> PROTO_ENUM;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"PROTO_ENUM"},
      Value::Enum(types::DifferentialPrivacyReportFormatEnumType(),
                  functions::DifferentialPrivacyEnums::PROTO),
      &PROTO_ENUM));
  catalog_->AddOwnedConstant(PROTO_ENUM.release());
  std::unique_ptr<SimpleConstant> JSON_ENUM;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create(
      std::vector<std::string>{"JSON_ENUM"},
      Value::Enum(types::DifferentialPrivacyReportFormatEnumType(),
                  functions::DifferentialPrivacyEnums::JSON),
      &JSON_ENUM));
  catalog_->AddOwnedConstant(JSON_ENUM.release());
}

void SampleCatalogImpl::LoadConnections() {
  auto connection1 = std::make_unique<SimpleConnection>("connection1");
  auto connection2 = std::make_unique<SimpleConnection>("connection2");
  auto connection3 = std::make_unique<SimpleConnection>("connection3");
  auto nested_connection =
      std::make_unique<SimpleConnection>("NestedConnection");
  auto default_connection =
      std::make_unique<SimpleConnection>("$connection_default");
  owned_connections_[connection1->Name()] = std::move(connection1);
  owned_connections_[connection2->Name()] = std::move(connection2);
  owned_connections_[connection3->Name()] = std::move(connection3);
  owned_connections_[default_connection->Name()] =
      std::move(default_connection);
  // This connection properly should be ONLY added to the nested catalog (e.g.
  // skipped in the loop below). But, due to how lookup works with nested
  // catalogs in simple_catalog currently, it will fail if it's not in the top-
  // level catalog as well.
  owned_connections_[nested_connection->Name()] = std::move(nested_connection);
  for (auto it = owned_connections_.begin(); it != owned_connections_.end();
       ++it) {
    catalog_->AddConnection(it->second.get());
  }
}

void SampleCatalogImpl::LoadSequences() {
  auto sequence1 = std::make_unique<SimpleSequence>("sequence1");
  auto sequence2 = std::make_unique<SimpleSequence>("sequence2");
  owned_sequences_[sequence1->Name()] = std::move(sequence1);
  owned_sequences_[sequence2->Name()] = std::move(sequence2);
  for (auto it = owned_sequences_.begin(); it != owned_sequences_.end(); ++it) {
    catalog_->AddSequence(it->second.get());
  }
}

void SampleCatalogImpl::AddOwnedTable(SimpleTable* table) {
  catalog_->AddOwnedTable(absl::WrapUnique(table));
  googlesql_base::InsertOrDie(&tables_, table->Name(), table);
}

void SampleCatalogImpl::LoadWellKnownLambdaArgFunctions() {
  const Type* int64_type = types_->get_int64();
  const Type* bool_type = types_->get_bool();

  // Models ARRAY_FILTER
  auto function = std::make_unique<Function>(
      "fn_array_filter", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_ARRAY_TYPE_ANY_1,
       {ARG_ARRAY_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, bool_type)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, FUNCTION<<T1>->BOOL>) -> <array<T1>>",
           function->GetSignature(0)->DebugString());
  function->AddSignature(
      {ARG_ARRAY_TYPE_ANY_1,
       {ARG_ARRAY_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1, int64_type}, bool_type)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, FUNCTION<(<T1>, INT64)->BOOL>) -> <array<T1>>",
           function->GetSignature(1)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Models ARRAY_TRANSFORM
  function = std::make_unique<Function>("fn_array_transform",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_ARRAY_TYPE_ANY_2,
       {ARG_ARRAY_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, FUNCTION<<T1>-><T2>>) -> <array<T2>>",
           function->GetSignature(0)->DebugString());
  function->AddSignature({ARG_ARRAY_TYPE_ANY_2,
                          {ARG_ARRAY_TYPE_ANY_1,
                           FunctionArgumentType::Lambda(
                               {ARG_TYPE_ANY_1, int64_type}, ARG_TYPE_ANY_2)},
                          /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, FUNCTION<(<T1>, INT64)-><T2>>) -> <array<T2>>",
           function->GetSignature(1)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  function = std::make_unique<Function>("fn_fp_array_sort", "sample_functions",
                                        Function::SCALAR);
  function->AddSignature({ARG_TYPE_ANY_1,
                          {ARG_ARRAY_TYPE_ANY_1,
                           FunctionArgumentType::Lambda(
                               {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1}, int64_type)},
                          /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, FUNCTION<(<T1>, <T1>)->INT64>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Models REDUCE function, which takes an input array, an initial state and a
  // function to run over each element with the current state to produce the
  // final state.
  function = std::make_unique<Function>("fn_fp_array_reduce",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_2,
       {ARG_ARRAY_TYPE_ANY_1, ARG_TYPE_ANY_2,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_2, ARG_TYPE_ANY_1},
                                     ARG_TYPE_ANY_2)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, <T2>, FUNCTION<(<T2>, <T1>)-><T2>>) -> <T2>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());
}

void SampleCatalogImpl::LoadContrivedLambdaArgFunctions() {
  const Type* int64_type = types_->get_int64();
  const Type* string_type = types_->get_string();
  const Type* bool_type = types_->get_bool();

  // Demonstrate having to get common super type for two different concrete type
  // for a single template type.
  auto function = std::make_unique<Function>(
      "fn_fp_T_T_LAMBDA", "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, bool_type)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<T1>, <T1>, FUNCTION<<T1>->BOOL>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // fn_fp_ArrayT_T is provided here to show current behavior to make it easier
  // for reader to understand fn_fp_ArrayT_T_LAMBDA.
  function = std::make_unique<Function>("fn_fp_ArrayT_T", "sample_functions",
                                        Function::SCALAR);
  function->AddSignature({ARG_TYPE_ANY_1,
                          {ARG_ARRAY_TYPE_ANY_1, ARG_TYPE_ANY_1},
                          /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, <T1>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Demostrate case where we don't have common super type for T1, due to
  // ARRAY<T1>.
  function = std::make_unique<Function>("fn_fp_ArrayT_T_LAMBDA",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {ARG_ARRAY_TYPE_ANY_1, ARG_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, bool_type)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<array<T1>>, <T1>, FUNCTION<<T1>->BOOL>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Demonstrate that lambda argument type inference conflict with final
  // concrete type of templated type influenced by lambda body type.
  function = std::make_unique<Function>("fn_fp_T_LAMBDA_RET_T",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {ARG_TYPE_ANY_1,
        FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_1)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(<T1>, FUNCTION<<T1>-><T1>>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  const auto named_required_format_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentTypeOptions().set_argument_name(
                                "format_string", kPositionalOrNamed));
  // Signature with lambda and named argument before lambda.
  function = std::make_unique<Function>("fn_fp_named_then_lambda",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {named_required_format_arg,
        FunctionArgumentType::Lambda({int64_type}, ARG_TYPE_ANY_1)},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(STRING format_string, FUNCTION<INT64-><T1>>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Signature with lambda and named argument after lambda.
  function = std::make_unique<Function>("fn_fp_lambda_then_named",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {FunctionArgumentType::Lambda({int64_type}, ARG_TYPE_ANY_1),
        named_required_format_arg},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(FUNCTION<INT64-><T1>>, STRING format_string) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Signature with lambda and repeated arguments after lambda.
  const auto repeated_arg =
      FunctionArgumentType(types_->get_int64(), FunctionArgumentType::REPEATED);
  function = std::make_unique<Function>("fn_fp_lambda_then_repeated",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {FunctionArgumentType::Lambda({int64_type}, ARG_TYPE_ANY_1),
        repeated_arg},
       /*context_id=*/-1});
  ABSL_CHECK_EQ("(FUNCTION<INT64-><T1>>, repeated INT64) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  // Signature with lambda and repeated arguments before lambda.
  function = std::make_unique<Function>("fn_fp_repeated_arg_then_lambda",
                                        "sample_functions", Function::SCALAR);
  function->AddSignature({ARG_TYPE_ANY_1,
                          {repeated_arg, FunctionArgumentType::Lambda(
                                             {int64_type}, ARG_TYPE_ANY_1)},
                          /*context_id=*/-1});
  ABSL_CHECK_EQ("(repeated INT64, FUNCTION<INT64-><T1>>) -> <T1>",
           function->GetSignature(0)->DebugString());
  catalog_->AddOwnedFunction(function.release());

  AddFunction(
      "fn_fp_repeated_arg_then_lambda_string", Function::SCALAR,
      {SignatureBuilder()
           .AddArg(ArgBuilder().Repeated().String())
           .AddArg(FunctionArgumentType::Lambda({string_type}, ARG_TYPE_ANY_1))
           .Returns(ArgBuilder().T1())
           .Build()});

  AddFunction("fn_fp_string_arg_then_lambda_no_arg", Function::SCALAR,
              {SignatureBuilder()
                   .AddArg(ArgBuilder().String())
                   .AddArg(FunctionArgumentType::Lambda({}, ARG_TYPE_ANY_1))
                   .Returns(ArgBuilder().T1())
                   .Build()});

  /*
  // Signature with lambda and repeated arguments before lambda.
  function = std::make_unique<Function>("fn_fp_repeated_arg_then_lambda_string",
                                        "sample_functions", Function::SCALAR);
  const auto repeated_string_arg = FunctionArgumentType(
      types_->get_string(), FunctionArgumentType::REPEATED);

  function->AddSignature(
      {ARG_TYPE_ANY_1,
       {repeated_string_arg,
        FunctionArgumentType::Lambda({string_type}, ARG_TYPE_ANY_1)}});
  catalog_->AddOwnedFunction(function.release());
  */
}

void SampleCatalogImpl::AddSqlDefinedFunction(
    absl::string_view name, FunctionSignature signature,
    const std::vector<std::string>& argument_names,
    absl::string_view function_body_sql,
    const LanguageOptions& language_options) {
  AnalyzerOptions analyzer_options;
  analyzer_options.set_language(language_options);
  ABSL_CHECK_EQ(argument_names.size(), signature.arguments().size());
  for (int i = 0; i < argument_names.size(); ++i) {
    ABSL_CHECK_NE(signature.argument(i).type(), nullptr);
    GOOGLESQL_CHECK_OK(analyzer_options.AddExpressionColumn(
        argument_names[i], signature.argument(i).type()));
  }
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_CHECK_OK(AnalyzeExpressionForAssignmentToType(
      function_body_sql, analyzer_options, catalog_.get(),
      catalog_->type_factory(), signature.result_type().type(),
      &analyzer_output));
  std::unique_ptr<SQLFunction> function;
  GOOGLESQL_CHECK_OK(SQLFunction::Create(name, FunctionEnums::SCALAR, {signature},
                               /*function_options=*/{},
                               analyzer_output->resolved_expr(), argument_names,
                               /*aggregate_expression_list=*/{},
                               /*parse_resume_location=*/{}, &function));
  catalog_->AddOwnedFunction(function.release());
  sql_object_artifacts_.emplace_back(std::move(analyzer_output));
}

// Add a SQL function to catalog starting from a full create_function
// statement.
void SampleCatalogImpl::AddSqlDefinedFunctionFromCreate(
    absl::string_view create_function, const LanguageOptions& language_options,
    bool inline_sql_functions,
    const FunctionOptions* /*absl_nullable*/ function_options) {
  // Ensure the language options used allow CREATE FUNCTION
  LanguageOptions language = language_options;
  language.AddSupportedStatementKind(RESOLVED_CREATE_FUNCTION_STMT);
  language.EnableLanguageFeature(FEATURE_INLINE_LAMBDA_ARGUMENT);
  language.EnableLanguageFeature(FEATURE_WITH_ON_SUBQUERY);
  language.EnableLanguageFeature(FEATURE_TEMPLATE_FUNCTIONS);
  language.EnableLanguageFeature(FEATURE_CREATE_AGGREGATE_FUNCTION);
  language.EnableLanguageFeature(FEATURE_HAVING_IN_AGGREGATE);
  language.EnableLanguageFeature(FEATURE_LIMIT_IN_AGGREGATE);
  language.EnableLanguageFeature(FEATURE_NULL_HANDLING_MODIFIER_IN_AGGREGATE);
  language.EnableLanguageFeature(FEATURE_MULTILEVEL_AGGREGATION);
  language.EnableLanguageFeature(FEATURE_AGGREGATE_FILTERING);
  language.EnableLanguageFeature(FEATURE_LATERAL_JOIN);
  language.EnableLanguageFeature(FEATURE_GROUP_BY_STRUCT);
  AnalyzerOptions analyzer_options;
  analyzer_options.set_language(language);
  analyzer_options.set_enabled_rewrites(/*rewrites=*/{});
  analyzer_options.enable_rewrite(REWRITE_INLINE_SQL_FUNCTIONS,
                                  inline_sql_functions);
  analyzer_options.enable_rewrite(REWRITE_INLINE_SQL_UDAS,
                                  inline_sql_functions);
  sql_object_artifacts_.emplace_back();
  CHECK_OK_UNLESS_DBG(AddFunctionFromCreateFunction(
      create_function, analyzer_options, /*allow_persistent_function=*/true,
      function_options, sql_object_artifacts_.back(), *catalog_, *catalog_));
}

void SampleCatalogImpl::LoadSqlFunctions(
    const LanguageOptions& language_options) {
  LoadScalarSqlFunctions(language_options);
  LoadScalarSqlFunctionsFromStandardModule(language_options);
  LoadDeepScalarSqlFunctions(language_options);
  LoadScalarSqlFunctionTemplates(language_options);
  LoadAggregateSqlFunctions(language_options);
}

void SampleCatalogImpl::LoadScalarSqlFunctions(
    const LanguageOptions& language_options) {
  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryPi() RETURNS FLOAT64 AS (3.141597); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryWithSubquery()
          AS (EXISTS (SELECT 1 FROM UNNEST([1,2,3]) AS e WHERE e = 2)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryWithCommonTableExpression()
          AS (EXISTS (WITH t AS (SELECT [1,2,3] AS arr)
                      SELECT 1 FROM t, t.arr as e WHERE e = 2)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryWithLambda()
          AS (ARRAY_INCLUDES([1, 2, 3], e -> e = 2)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryWithSqlFunctionCallPreInlined()
          AS (NullaryPi()); )",
      language_options, /*inline_sql_functions=*/true);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION NullaryWithSqlFunctionCallNotPreInlined()
          AS (NullaryPi()); )",
      language_options, /*inline_sql_functions=*/false);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION UnaryIncrement(a INT64) AS ( a + 1 ); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(
        CREATE FUNCTION GroupingConstantFromUnnest(
          arr ARRAY<INT64>, int64_arg INT64)
        AS (
          (SELECT SUM(X GROUP BY X) FROM UNNEST(arr) AS X)
        );
      )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(
        CREATE FUNCTION GroupingConstantFromArg(
          arr ARRAY<INT64>, int64_arg INT64)
        AS (
          (SELECT SUM(int64_arg GROUP BY X) FROM UNNEST(arr) AS X)
        );
      )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(
        CREATE FUNCTION NullaryWithMultilevelAgg()
        AS (
          (SELECT SUM(x GROUP BY x) FROM UNNEST([1, 2, 3, 3]) AS x)
        );
      )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(
        CREATE FUNCTION NestedNullaryWithMultilevelAgg()
        AS (
          NullaryWithMultilevelAgg() + NullaryWithMultilevelAgg()
        );
      )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(
        CREATE FUNCTION NullaryUdfContainingFilterReturnsZero()
        AS (
          (SELECT COUNT(* WHERE false) FROM UNNEST([1]))
        );
      )",
      language_options);

  // Creating the function using AnalyzeExpressionForAssignmentToType results in
  // a function body expression with ResolvedExpressionColumn where arguments
  // are referenced instead of a ResolvedArgumentRef. That appears to be done
  // in several catalogs, so we want to test that case too.
  const Type* int64_type = types_->get_int64();
  FunctionSignature int_int = {int64_type,
                               {int64_type},
                               /*context_id=*/static_cast<int64_t>(0)};
  AddSqlDefinedFunction("UnaryIncrementRefArg", int_int, {"a"}, "a + 1",
                        language_options);
}

void SampleCatalogImpl::LoadScalarSqlFunctionsFromStandardModule(
    const LanguageOptions& language_options) {
  // Function from sql/modules/math_utils/math_utils.sqlm
  // References each of its arguments more than once.
  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION FloorDiv(n INT64, d INT64) RETURNS INT64
          AS ( DIV(n, d) - IF(n < 0 AND MOD(n, d) != 0, 1, 0) ); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION IgnoresArg(a INT64) AS (1); )", language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION ReferencesArgsInsideSubquery(a INT64, b INT64)
          AS ((SELECT a + b)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION ReferencesArgInsideCte(a INT64)
          AS ((WITH t AS (SELECT a AS c) SELECT c FROM t)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION ReferencesArgOutsideCte(a INT64)
          AS ((WITH t AS (SELECT 1 AS c) SELECT c + a FROM t)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(  CREATE FUNCTION ReferencesArgInsideLambda(a INT64)
           AS (ARRAY_TRANSFORM([1, 2, 3], e->e = a)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION ReferencesArgsInsideLateralJoin(a INT64, b INT64)
            AS ((
            SELECT a + a
            FROM (SELECT 1) AS t
            CROSS JOIN
            LATERAL (
              SELECT a + b
              FROM (
                SELECT * FROM (SELECT 1 AS x) AS t1
                LEFT JOIN LATERAL (SELECT (SELECT a - b > x) FROM (SELECT 1)) AS t2
                ON a > b
              )
            ) AS t2
            )); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION scalar_function_definer_rights() SQL SECURITY DEFINER
              AS ((SELECT COUNT(*) FROM KeyValue)); )",
      language_options,
      /*inline_sql_functions=*/true);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION stable_array_sort(arr ANY TYPE)
              AS ((SELECT ARRAY_AGG(e ORDER BY e, off)
                   FROM UNNEST(arr) AS e WITH OFFSET off)); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
        CREATE FUNCTION template_with_typed_arg(
            arg_any ANY TYPE, arg_string STRING)
        AS (arg_any IS NULL OR arg_string IS NULL);
      )sql",
      language_options);
}

void SampleCatalogImpl::LoadDeepScalarSqlFunctions(
    const LanguageOptions& language_options) {
  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION CallsPi0() AS (NullaryPi());)", language_options,
      /*inline_sql_functions=*/false);

  for (int i = 0; i < 25; ++i) {
    AddSqlDefinedFunctionFromCreate(
        absl::StrCat("CREATE FUNCTION CallsPi", i + 1, "() AS (CallsPi", i,
                     "());"),
        language_options, /*inline_sql_functions=*/false);
  }
}

void SampleCatalogImpl::LoadScalarSqlFunctionTemplates(
    const LanguageOptions& language_options) {
  // This function is logically equivalent to ARRAY_REVERSE
  AddSqlDefinedFunctionFromCreate(
      R"(  CREATE FUNCTION REVERSE_ARRAY(input_arr ANY TYPE)
           AS (IF (input_arr IS NULL,
                   NULL,
                   ARRAY(SELECT e
                         FROM UNNEST(input_arr) AS e WITH OFFSET
                         ORDER BY OFFSET desc))); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION TimesTwo(arg ANY TYPE) AS (arg + arg); )",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"( CREATE FUNCTION SUM_DOUBLE_ARRAY(input_arr ANY TYPE)
          AS ((SELECT SUM(e)
               FROM UNNEST(ARRAY_TRANSFORM(input_arr, e->TimesTwo(e))) e));)",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(CREATE TEMP FUNCTION b290673529(thing_id ANY TYPE) RETURNS BOOL
            AS (
               thing_id IN (SELECT thing_id FROM (SELECT 1 AS thing_id))
            );
        )sql",
      language_options, /*inline_sql_functions=*/false);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
        CREATE FUNCTION TemplatedUdfExplicitReturnType(arg1 ANY TYPE, arg2 ANY TYPE)
        RETURNS STRUCT<x STRING, y STRING> AS (
          STRUCT(arg1, arg2)
        );
      )sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"(CREATE FUNCTION TemplatedUdfContainingFilterReturnsCount(arg ANY TYPE) AS ((SELECT COUNT(arg WHERE true) FROM UNNEST([1]))); )",
      language_options);
}

void SampleCatalogImpl::LoadAggregateSqlFunctions(
    const LanguageOptions& language_options) {
  AddSqlDefinedFunctionFromCreate(
      R"sql( CREATE AGGREGATE FUNCTION NotAggregate() AS (1 + 1);)sql",
      language_options);

  // This function provides some open-box testing of the inliner in that it
  // enables a test to ensure columns internal to the function body are properly
  // re-mapped and don't result in column id collisions in the rewritten
  // query if the function is called twice.
  AddSqlDefinedFunctionFromCreate(
      R"sql( CREATE AGGREGATE FUNCTION NotAggregateInternalColumn() AS (
          (SELECT a + a FROM (SELECT 1 AS a))
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql( CREATE AGGREGATE FUNCTION CountStar() AS (COUNT(*));)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql( CREATE AGGREGATE FUNCTION CallsCountStar() AS (CountStar());)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION NotAggregateArgs(
        a INT64 NOT AGGREGATE
      ) AS (
        a + a
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION SumOfNotAggregateArg(
        not_agg_arg INT64 NOT AGGREGATE
      ) AS (
        SUM(not_agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION ExpressionOutsideSumOfNotAggregate(
        not_agg_arg INT64 NOT AGGREGATE
      ) AS (
        not_agg_arg + SUM(not_agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION ExpressionInsideSumOfNotAggregate(
        not_agg_arg INT64 NOT AGGREGATE
      ) AS (
        SUM(not_agg_arg + not_agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION SumOfAggregateArgs(
        agg_arg INT64
      ) AS (
        SUM(agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION MaxOfAggregateArgs(
        agg_arg INT64
      ) AS (
        MAX(agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION SumExpressionOfAggregateArgs(
        agg_arg INT64
      ) AS (
        SUM(agg_arg + agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION ExprOutsideSumExpressionOfAggregateArgs(
        agg_arg INT64
      ) AS (
        1 + SUM(agg_arg + agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION ExprOutsideAndInsideSum(
        agg_arg INT64,
        not_agg_arg INT64 NOT AGGREGATE
      ) AS (
        not_agg_arg + SUM(not_agg_arg + agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithHavingMax(
        agg_arg STRING,
        another_agg_arg INT64
      ) AS (
        ARRAY_AGG(agg_arg HAVING MAX another_agg_arg)
      );)sql",
      language_options);

  // TODO: Add example with ARRAY_AGG( ... ORDER BY ... )
  //    after that is enabled in UDA bodies.

  // TODO: Add example with WITH GROUP ROWS( ... ) after that
  //     is enabled in UDA bodies.

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaTemplateWithHavingMax(
        agg_arg ANY TYPE
      ) AS (
        ARRAY_AGG(agg_arg.a HAVING MAX agg_arg.b)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaTemplateWithIgnoreNulls(
        agg_arg ANY TYPE
      ) AS (
        STRUCT (
          ARRAY_AGG(agg_arg IGNORE NULLS) AS ignore_nulls,
          ARRAY_AGG(agg_arg RESPECT NULLS) AS respect_nulls,
          ARRAY_AGG(agg_arg) AS whatever_nulls
        )
      );)sql",
      language_options);

  // A token UDA with LIMIT in it. We can't do ORDER BY, so the use of LIMIT
  // is ... questionable.
  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaTemplateWithLimitZero(
        agg_arg ANY TYPE
      ) AS (
        STRUCT (
          ARRAY_AGG(agg_arg LIMIT 0) AS ignore_nulls
        )
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaCallingAnotherUda(
        agg_arg ANY TYPE
      ) AS (
        CountStar() - COUNT(agg_arg.a)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaCallingAnotherUdaWithNonAggregateArgs(
        agg_arg INT64,
        non_agg_arg ANY TYPE NOT AGGREGATE
      ) AS (
        ExprOutsideAndInsideSum(agg_arg + 1, non_agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaSafeCallingAnotherUdaWithNonAggregateArgs(
        agg_arg INT64,
        non_agg_arg ANY TYPE NOT AGGREGATE
      ) AS (
        SAFE.ExprOutsideAndInsideSum(agg_arg + 1, non_agg_arg)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaInvokingUdf(
        agg_arg ANY TYPE
      ) AS (
        ExprOutsideAndInsideSum(TimesTwo(agg_arg), 3)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaInvokingSafeUdf(
        agg_arg ANY TYPE
      ) AS (
        ExprOutsideAndInsideSum(SAFE.TimesTwo(agg_arg), 3)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION SumOfValuesForDistinctKey(
        value INT64,
        key INT64
      ) AS (
        SUM(ANY_VALUE(value) GROUP BY key)
      );)sql",
      language_options);

  // This function is intended to test grouping-constness for multi-level
  // aggregate functions in various scenarios (using struct field paths, in
  // subqueries, aggregate functions, etc.). Avoid altering it where possible.
  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION ComplexUda3(value1 INT64, value2 INT64, key INT64, struct_arg STRUCT<e INT64, f STRUCT<c INT64, d STRUCT<a INT64, b STRING>>>) AS (
        CORR(
          key + CORR(key, value2 + MAX(value1) GROUP BY value2)
            + struct_arg.f.c,
          AVG(
            value2 + MIN(value1) + (SELECT key + value2)
              + struct_arg.f.c + struct_arg.f.d.a
            GROUP BY value2, struct_arg.f.d
          )
          GROUP BY key, struct_arg.f
        )
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION NullaryUdaWithMultilevelAgg() AS (
        (SELECT SUM(x GROUP BY x) FROM UNNEST([1, 2, 3, 3]) AS x)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggOrderBy(key INT64, value INT64) AS (
        ARRAY_AGG(MAX(value) GROUP BY key)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggIgnoreNulls(key INT64, value INT64) AS (
        ARRAY_AGG(SUM(value) IGNORE NULLS GROUP BY key)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggLimitDistinct(key INT64, value INT64) AS (
        ARRAY_AGG(DISTINCT SUM(value) GROUP BY key LIMIT 2)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggMultiArg(
        arg1 INT64, arg2 INT64, arg3 STRING, arg4 BYTES) AS (
          CORR(
            AVG(arg1 GROUP BY arg3),
            CORR(MAX(arg2), COUNT(*) GROUP BY arg4)
            GROUP BY arg1, arg2)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggNonAggregateArgs(
        arg1 INT64, arg2 INT64, arg3 INT64 NOT AGGREGATE, arg4 INT64 NOT AGGREGATE) AS (
          arg4 + SUM(arg1 + CAST(arg3 AS INT64) GROUP BY arg1)
            / COUNT(arg2 + CAST(arg4 AS INT64) GROUP BY arg2)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithMultilevelAggNested(key INT64, value INT64) AS (
        SumOfValuesForDistinctKey(key, value) + COUNT(* GROUP BY key, value)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithWhereFilterModifier(arg1 INT64, arg2 INT64) AS (
        SUM(arg1 WHERE arg1 > arg2)
      );)sql",
      language_options);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaWithHavingFilterModifier(arg1 INT64, arg2 INT64) AS (
        SUM(MAX(arg1) GROUP BY arg2 HAVING MIN(arg1) > 10)
      );)sql",
      language_options);

  auto aggregate_calling_clauses_enabled = std::make_unique<FunctionOptions>();
  // FunctionOptions aggregate_calling_clauses_enabled;
  aggregate_calling_clauses_enabled
      // No need to test clamped between modifier since it is hard-coded to
      // only work on functions with specific names.
      ->set_supports_distinct_modifier(true)
      .set_supports_having_modifier(true)
      .set_supports_null_handling_modifier(true)
      .set_supports_limit(true)
      .set_supports_order_by(true)
      .set_uses_upper_case_sql_name(false);

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaConcreteWithOptionalCallingClausesEnabled(
        agg_arg INT64
      ) AS (  NULL );
      )sql",
      language_options, /*inline_sql_functions=*/false,
      aggregate_calling_clauses_enabled.get());

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaTemplateWithOptionalCallingClausesEnabled(
        agg_arg ANY TYPE
      ) AS (  NULL );
      )sql",
      language_options, /*inline_sql_functions=*/false,
      aggregate_calling_clauses_enabled.get());

  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION UdaInlinedOnCreate(x INT64) AS (
          SumExpressionOfAggregateArgs(x)
      );)sql",
      language_options, /*inline_sql_functions=*/true);

  // Templated UDA which uses TYPEOF, so that the body can be rewritten by the
  // TemplatedFunctionCallRewriter.
  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION TemplatedUdaCountIfTypeOfIsString(x ANY TYPE) AS (
          COUNTIF(TYPEOF(x) = 'STRING')
      );)sql",
      language_options, /*inline_sql_functions=*/false);

  // Templated UDA which calls multiple UDAs, one of which has a body that can
  // be rewritten by the TemplatedFunctionCallRewriter.
  AddSqlDefinedFunctionFromCreate(
      R"sql(
      CREATE AGGREGATE FUNCTION TemplatedUdaCallsRewritableUda(x ANY TYPE) AS (
          TemplatedUdaCountIfTypeOfIsString(x) + COUNT(*)
      );
      )sql",
      language_options, /*inline_sql_functions=*/false);
}

void SampleCatalogImpl::ForceLinkProtoTypes() {
  google::protobuf::LinkMessageReflection<googlesql_test::TestReferencedPB>();
}

}  // namespace googlesql
