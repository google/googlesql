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

#include "googlesql/compliance/test_driver.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/type_visitors.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/public/annotation.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

constexpr int TestTableOptions::kDefaultExpectedTableSize;
constexpr double TestTableOptions::kDefaultNullableProbability;

absl::Status SerializeTestDatabase(const TestDatabase& database,
                                   TestDatabaseProto* proto) {
  proto->Clear();
  for (const std::string& proto_file : database.proto_files) {
    proto->add_proto_files(proto_file);
  }
  proto->set_runs_as_test(database.runs_as_test);
  for (const std::string& proto_name : database.proto_names) {
    proto->add_proto_names(proto_name);
  }
  for (const std::string& enum_name : database.enum_names) {
    proto->add_enum_names(enum_name);
  }
  for (const auto& entry : database.tables) {
    TestTableProto* t = proto->add_test_tables();
    t->set_name(entry.first);
    const TestTable& table = entry.second;
    GOOGLESQL_RETURN_IF_ERROR(table.table_as_value.type()->SerializeToSelfContainedProto(
        t->mutable_contents()->mutable_type()));
    GOOGLESQL_RETURN_IF_ERROR(
        table.table_as_value.Serialize(t->mutable_contents()->mutable_value()));
    for (const auto& measure_column_def : table.measure_column_defs) {
      MeasureColumnDefProto* measure_col_def_proto =
          t->mutable_measure_column_definitions()->Add();
      measure_col_def_proto->set_name(measure_column_def.name);
      measure_col_def_proto->set_expression(measure_column_def.expression);
      measure_col_def_proto->set_is_pseudo_column(
          measure_column_def.is_pseudo_column);
      if (measure_column_def.row_identity_column_indices.has_value()) {
        for (int index : *measure_column_def.row_identity_column_indices) {
          measure_col_def_proto->add_row_identity_column_indices(index);
        }
      }
    }
    for (int row_identity_column_index : table.row_identity_columns) {
      t->mutable_row_identity_column_indices()->Add(row_identity_column_index);
    }
    const TestTableOptions& options = table.options;

    TestTableOptionsProto* options_proto = t->mutable_options();
    options_proto->set_expected_table_size_min(
        options.expected_table_size_min());
    options_proto->set_expected_table_size_max(
        options.expected_table_size_max());
    options_proto->set_is_value_table(options.is_value_table());
    options_proto->set_nullable_probability(options.nullable_probability());
    for (LanguageFeature feature : options.required_features()) {
      options_proto->add_required_features(feature);
    }
    if (!options.userid_column().empty()) {
      options_proto->set_userid_column(options.userid_column());
    }
    if (!options.column_annotations().empty()) {
      std::unique_ptr<AnnotationMap> empty_annotation =
          AnnotationMap::Create(types::Int64Type());
      for (const AnnotationMap* annotation : options.column_annotations()) {
        AnnotationMapProto* proto = options_proto->add_column_annotations();
        if (annotation != nullptr) {
          GOOGLESQL_RETURN_IF_ERROR(annotation->Serialize(proto));
        } else {
          GOOGLESQL_RETURN_IF_ERROR(empty_annotation->Serialize(proto));
        }
      }
    }
  }

  for (const auto& [name, create_stmt] : database.tvfs) {
    TestTVFProto* tvf = proto->add_tvfs();
    tvf->set_name(name);
    tvf->set_create_stmt(create_stmt);
  }

  for (const auto& [name, create_stmt] : database.property_graph_defs) {
    TestPropertyGraphProto* graph = proto->add_property_graphs();
    graph->set_name(name);
    graph->set_create_stmt(create_stmt);
  }

  for (const std::string& stmt : database.measure_function_defs) {
    proto->add_measure_function_defs(stmt);
  }

  return absl::OkStatus();
}

// Consolidates any descriptors in this type into the target pool & type
// factory.
class TypeConsolidator : public TypeRewriter {
 public:
  TypeConsolidator(const ::google::protobuf::DescriptorPool* descriptor_pool,
                   TypeFactory* type_factory)
      : TypeRewriter(*type_factory), descriptor_pool_(descriptor_pool) {}

  absl::StatusOr<const Type*> Internalize(const Type* type) {
    GOOGLESQL_ASSIGN_OR_RETURN(AnnotatedType annotated_type,
                     Visit(AnnotatedType(type, /*annotation_map=*/nullptr)));
    return annotated_type.type;
  }

  absl::StatusOr<AnnotatedType> PostVisit(
      AnnotatedType annotated_type) override;

 private:
  const ::google::protobuf::DescriptorPool* descriptor_pool_;
};

// TODO: Should we care about descriptors used in builtins? Probably not coz we
// only care about ptr equality only during the generation phase.
absl::StatusOr<AnnotatedType> TypeConsolidator::PostVisit(
    AnnotatedType annotated_type) {
  const auto& [type, annotation_map] = annotated_type;
  if (type->IsProto()) {
    const auto* descriptor = type->AsProto()->descriptor();
    GOOGLESQL_RET_CHECK(descriptor_pool_->FindFileByName(descriptor->file()->name()))
        << "Filed to find file: " << descriptor->file()->name()
        << " for message: " << descriptor->full_name();
    descriptor =
        descriptor_pool_->FindMessageTypeByName(descriptor->full_name());
    GOOGLESQL_RET_CHECK(descriptor != nullptr)
        << "Failed to load message: " << descriptor->full_name();

    const ProtoType* proto_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory().MakeProtoType(descriptor, &proto_type));
    return AnnotatedType(proto_type, annotation_map);
  } else if (type->IsEnum()) {
    const auto* enum_descriptor = type->AsEnum()->enum_descriptor();
    GOOGLESQL_RET_CHECK(descriptor_pool_->FindFileByName(enum_descriptor->file()->name()))
        << "Filed to find file: " << enum_descriptor->file()->name()
        << " for enum: " << enum_descriptor->full_name();
    enum_descriptor =
        descriptor_pool_->FindEnumTypeByName(enum_descriptor->full_name());
    GOOGLESQL_RET_CHECK(enum_descriptor != nullptr)
        << "Failed to load enum: " << enum_descriptor->full_name();

    const EnumType* enum_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory().MakeEnumType(enum_descriptor, &enum_type));
    return AnnotatedType(enum_type, annotation_map);
  }
  return annotated_type;
}

absl::StatusOr<TestDatabase> DeserializeTestDatabase(
    const TestDatabaseProto& proto, TypeFactory* type_factory,
    const ::google::protobuf::DescriptorPool* descriptor_pool) {
  TestDatabase db;
  for (const std::string& proto_file : proto.proto_files()) {
    if (!db.proto_files.insert(proto_file).second) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate proto file: " << proto_file;
    }
  }
  db.runs_as_test = proto.runs_as_test();
  for (const std::string& proto_name : proto.proto_names()) {
    if (!db.proto_names.insert(proto_name).second) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate proto name: " << proto_name;
    }
  }
  for (const std::string& enum_name : proto.enum_names()) {
    if (!db.enum_names.insert(enum_name).second) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate enum name: " << enum_name;
    }
  }
  for (const std::string& create_fn_stmt : proto.measure_function_defs()) {
    db.measure_function_defs.push_back(create_fn_stmt);
  }
  for (const TestTableProto& table_proto : proto.test_tables()) {
    auto [it, inserted] = db.tables.try_emplace(table_proto.name());
    if (!inserted) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate table name: " << table_proto.name();
    }
    TestTable& table = it->second;
    const Type* contents_type;
    int num_pools = table_proto.contents().type().file_descriptor_set_size();
    std::vector<std::unique_ptr<google::protobuf::DescriptorPool>> owned_descriptor_pools;
    owned_descriptor_pools.reserve(num_pools);
    std::vector<google::protobuf::DescriptorPool*> pools;
    pools.reserve(num_pools);
    for (int i = 0; i < num_pools; ++i) {
      owned_descriptor_pools.push_back(
          std::make_unique<google::protobuf::DescriptorPool>());
      pools.push_back(owned_descriptor_pools.back().get());
    }
    GOOGLESQL_RETURN_IF_ERROR(
        type_factory->DeserializeFromSelfContainedProtoWithDistinctFiles(
            table_proto.contents().type(), pools, &contents_type));

    GOOGLESQL_ASSIGN_OR_RETURN(contents_type,
                     TypeConsolidator(descriptor_pool, type_factory)
                         .Internalize(contents_type));

    GOOGLESQL_ASSIGN_OR_RETURN(
        table.table_as_value,
        Value::Deserialize(table_proto.contents().value(), contents_type));

    for (const MeasureColumnDefProto& measure_col_def_proto :
         table_proto.measure_column_definitions()) {
      std::vector<int> row_identity_column_indices(
          measure_col_def_proto.row_identity_column_indices().begin(),
          measure_col_def_proto.row_identity_column_indices().end());
      table.measure_column_defs.push_back(MeasureColumnDef{
          .name = std::string(measure_col_def_proto.name()),
          .expression = std::string(measure_col_def_proto.expression()),
          .is_pseudo_column = measure_col_def_proto.is_pseudo_column(),
          .row_identity_column_indices =
              row_identity_column_indices.empty()
                  ? std::nullopt
                  : std::make_optional(
                        std::move(row_identity_column_indices))});
    }
    for (int row_identity_column_index :
         table_proto.row_identity_column_indices()) {
      table.row_identity_columns.push_back(row_identity_column_index);
    }

    table.options.set_expected_table_size_range(
        static_cast<int>(table_proto.options().expected_table_size_min()),
        static_cast<int>(table_proto.options().expected_table_size_max()));
    table.options.set_is_value_table(table_proto.options().is_value_table());
    table.options.set_nullable_probability(
        table_proto.options().nullable_probability());

    for (int feature_int : table_proto.options().required_features()) {
      LanguageFeature feature = static_cast<LanguageFeature>(feature_int);
      if (!table.options.mutable_required_features()->insert(feature).second) {
        return googlesql_base::InvalidArgumentErrorBuilder()
               << "Duplicate required language feature: "
               << LanguageFeature_Name(feature);
      }
    }
    table.options.set_userid_column(table_proto.options().userid_column());
    std::vector<const AnnotationMap*> column_annotations;
    for (const AnnotationMapProto& annotation :
         table_proto.options().column_annotations()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnnotationMap> annotation_map,
                       AnnotationMap::Deserialize(annotation));
      GOOGLESQL_ASSIGN_OR_RETURN(column_annotations.emplace_back(),
                       type_factory->TakeOwnership(std::move(annotation_map)));
    }
    table.options.set_column_annotations(std::move(column_annotations));
  }

  for (const TestTVFProto& tvf : proto.tvfs()) {
    absl::string_view name = tvf.name();
    auto [_, is_new] = db.tvfs.emplace(name, tvf.create_stmt());
    if (!is_new) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate TVF name: " << name;
    }
  }

  for (const TestPropertyGraphProto& graph : proto.property_graphs()) {
    absl::string_view name = graph.name();
    auto [_, is_new] =
        db.property_graph_defs.emplace(name, graph.create_stmt());
    if (!is_new) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Duplicate property graph name: " << name;
    }
  }

  return db;
}

absl::StatusOr<Value> TestDriver::MultiStmtResultToValue(
    const absl::StatusOr<MultiStmtResult>& multi_result) {
  GOOGLESQL_RETURN_IF_ERROR(multi_result.status());
  if (multi_result->statement_results.empty()) {
    return absl::OutOfRangeError("No statement results produced");
  }
  if (multi_result->statement_results.size() > 1) {
    return absl::OutOfRangeError("More than one statement result produced");
  }
  return multi_result->statement_results[0].result;
}

}  // namespace googlesql
