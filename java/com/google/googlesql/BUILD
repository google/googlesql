#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Java API for GoogleSQL.

load("@google_bazel_common//tools/jarjar:jarjar.bzl", "jarjar_library")
load("@google_bazel_common//tools/javadoc:javadoc.bzl", "javadoc_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_java//java:defs.bzl", "java_library")
load("//tools:maven.bzl", "pom_file")

package(
    default_visibility = ["//visibility:public"],
)

TYPES_SRCS = [
    "ArrayType.java",
    "BuiltinDescriptorPool.java",
    "CivilTimeEncoder.java",
    "EnumType.java",
    "FileDescriptorSetsBuilder.java",
    "GraphElementType.java",
    "GraphPathType.java",
    "DescriptorPool.java",
    "ImmutableDescriptorPool.java",
    "GoogleSQLDescriptorPool.java",
    "GoogleSQLStrings.java",
    "IntervalValue.java",
    "MapType.java",
    "MeasureType.java",
    "ProtoType.java",
    "RangeType.java",
    "SimpleType.java",
    "StructType.java",
    "Type.java",
    "TypeFactory.java",
    "TypeParameters.java",
    "TVFRelation.java",
    "Value.java",
    "SimpleValue.java",
    "AnnotationMap.java",
    "StructAnnotationMap.java",
]

# There are several libraries that require this explicit list of dependencies
# in order to produce valid maven artifacts.  These must all be kept in sync
# and automated tools (build_cleaner) don't understand this requirement.
BUILTIN_PROTO_DEPS = [
    "//googlesql/proto:anon_output_with_report_java_proto",
    "//googlesql/public/functions:array_find_mode_java_proto",
    "//googlesql/public/functions:array_zip_mode_java_proto",
    "//googlesql/public/functions:rank_type_java_proto",
    "//googlesql/public/functions:bitwise_agg_mode_java_proto",
    "//googlesql/public/functions:datetime_java_proto",
    "//googlesql/public/functions:differential_privacy_java_proto",
    "//googlesql/public/functions:normalize_mode_java_proto",
    "//googlesql/public/functions:range_sessionize_mode_java_proto",
    "//googlesql/public/functions:rounding_mode_java_proto",
    "//googlesql/public/functions:unsupported_fields_java_proto",
    "//googlesql/public:function_java_proto",
    "//googlesql/public:type_proto_java_proto",
    "//googlesql/public:value_java_proto",
    "//googlesql/proto:function_java_proto",
]

java_plugin(
    name = "auto_value_processor_plugin",
    processor_class = "com.google.auto.value.processor.AutoValueProcessor",
    deps = ["@maven//:com_google_auto_value_auto_value"],
)

java_plugin(
    name = "auto_one_of_processor_plugin",
    processor_class = "com.google.auto.value.processor.AutoOneOfProcessor",
    deps = ["@maven//:com_google_auto_value_auto_value"],
)

java_plugin(
    name = "auto_annotation_processor_plugin",
    processor_class = "com.google.auto.value.processor.AutoAnnotationProcessor",
    deps = ["@maven//:com_google_auto_value_auto_value"],
)

java_plugin(
    name = "auto_service_processor_plugin",
    processor_class = "com.google.auto.service.processor.AutoServiceProcessor",
    deps = ["@maven//:com_google_auto_service_auto_service"],
)

java_library(
    name = "types",
    srcs = TYPES_SRCS,
    plugins = [
        ":auto_value_processor_plugin",
        ":auto_one_of_processor_plugin",
        ":auto_annotation_processor_plugin",
    ],
    tags = ["maven_coordinates=com.google.googlesql:googlesql-types:${project.version}"],
    deps = BUILTIN_PROTO_DEPS + [
        "//googlesql/public:annotation_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:simple_value_java_proto",
        "//googlesql/public:type_parameters_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
        "@com_google_protobuf//:protobuf_java",
        "@com_google_protobuf//:protobuf_java_util",
        "@maven//:com_google_api_grpc_proto_google_common_protos",
        "@maven//:com_google_auto_value_auto_value",
        "@maven//:com_google_auto_value_auto_value_annotations",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",  # buildcleaner: keep
        "@maven//:com_google_guava_guava",
        "@maven//:joda_time_joda_time",
    ],
)

java_library(
    name = "types_proto_resources",
    resources = [
        "//googlesql/public/proto:type_annotation_filegroup",
    ],
)

jarjar_library(
    name = "types_jar",
    jars = BUILTIN_PROTO_DEPS + [
        ":types",
        ":types_proto_resources",
        "//googlesql/public:deprecation_warning_java_proto",
        "//googlesql/public:error_location_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
    ],
    rules = [
        "zap com.google.protobuf.**",
    ],
)

jarjar_library(
    name = "types_src",
    jars = [
        ":libtypes-src.jar",
    ],
)

pom_file(
    name = "types_pom",
    artifact_id = "googlesql-types",
    artifact_name = "Types",
    targets = BUILTIN_PROTO_DEPS + [
        ":types",
        "//googlesql/public:deprecation_warning_java_proto",
        "//googlesql/public:error_location_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
    ],
)

javadoc_library(
    name = "types_javadoc",
    srcs = TYPES_SRCS,
    root_packages = ["com.google.googlesql"],
    deps = [":types"],
)

CHANNEL_PROVIDER_SRCS = [
    "ClientChannelProvider.java",
]

java_library(
    name = "channel_provider",
    srcs = CHANNEL_PROVIDER_SRCS,
    tags = ["maven_coordinates=com.google.googlesql:googlesql-client:${project.version}"],
    deps = [
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
    ],
)

JNI_CHANNEL_SRCS = [
    "JniChannelProvider.java",
]

java_library(
    name = "jni_channel_darwin",
    resources = select({
        "@bazel_tools//src/conditions:darwin": ["//googlesql/local_service:liblocal_service_jni.dylib"],
        "//conditions:default": [],
    }),
    tags = ["maven_coordinates=com.google.googlesql:googlesql-jni-channel-darwin:${project.version}"],
)

jarjar_library(
    name = "jni_channel_darwin_jar",
    jars = [
        ":jni_channel_darwin",
    ],
)

jarjar_library(
    name = "jni_channel_darwin_src",
    jars = [
        ":libjni_channel_darwin-src.jar",
    ],
)

pom_file(
    name = "jni_channel_darwin_pom",
    artifact_id = "googlesql-jni-channel-darwin",
    artifact_name = "JNI Channel for Darwin",
    targets = [
        ":jni_channel_darwin",
    ],
)

javadoc_library(
    name = "jni_channel_darwin_javadoc",
    srcs = JNI_CHANNEL_SRCS,
    root_packages = ["com.google.googlesql"],
    deps = [":jni_channel"],
)

java_library(
    name = "jni_channel_linux",
    resources = select({
        "@bazel_tools//src/conditions:linux_x86_64": ["//googlesql/local_service:liblocal_service_jni.so"],
        "//conditions:default": [],
    }),
    tags = ["maven_coordinates=com.google.googlesql:googlesql-jni-channel-linux:${project.version}"],
)

jarjar_library(
    name = "jni_channel_linux_jar",
    jars = [
        ":jni_channel_linux",
    ],
)

jarjar_library(
    name = "jni_channel_linux_src",
    jars = [
        ":libjni_channel_linux-src.jar",
    ],
)

pom_file(
    name = "jni_channel_linux_pom",
    artifact_id = "googlesql-jni-channel-linux",
    artifact_name = "JNI Channel for Linux",
    targets = [
        ":jni_channel_linux",
    ],
)

javadoc_library(
    name = "jni_channel_linux_javadoc",
    srcs = JNI_CHANNEL_SRCS,
    root_packages = ["com.google.googlesql"],
    deps = [":jni_channel"],
)

java_library(
    name = "jni_channel",
    srcs = JNI_CHANNEL_SRCS,
    plugins = [":auto_service_processor_plugin"],
    tags = ["maven_coordinates=com.google.googlesql:googlesql-jni-channel:${project.version}"],
    runtime_deps = [
        ":jni_channel_darwin",
        ":jni_channel_linux",
    ],
    deps = [
        ":channel_provider",
        "//googlesql/local_service:local_service_jni",
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@io_grpc_grpc_java//netty",
        "@maven//:com_google_auto_service_auto_service",
        "@maven//:io_netty_netty_common",
        "@maven//:io_netty_netty_transport",
        "@native_utils",
    ],
)

jarjar_library(
    name = "jni_channel_jar",
    jars = [
        ":jni_channel",
        "@native_utils",
    ],
    rules = [
        "rule cz.adamh.utils.** com.google.googlesql.@0",
        "rule io.grpc.** com.google.googlesql.@0",
        "rule io.opencensus.** com.google.googlesql.@0",
    ],
)

jarjar_library(
    name = "jni_channel_src",
    jars = [
        ":libjni_channel-src.jar",
    ],
)

pom_file(
    name = "jni_channel_pom",
    artifact_id = "googlesql-jni-channel",
    artifact_name = "JNI Channel",
    targets = [
        ":jni_channel",
    ],
)

javadoc_library(
    name = "jni_channel_javadoc",
    srcs = JNI_CHANNEL_SRCS,
    root_packages = ["com.google.googlesql"],
    deps = [":jni_channel"],
)

PROPERTY_GRAPH_SRCS = [
    "PropertyGraph.java",
    "GraphElementTable.java",
    "GraphNodeTable.java",
    "GraphEdgeTable.java",
    "GraphElementLabel.java",
    "GraphNodeTableReference.java",
    "GraphPropertyDeclaration.java",
    "GraphPropertyDefinition.java",
    "GraphDynamicLabel.java",
    "GraphDynamicProperties.java",
]

java_library(
    name = "property_graph",
    srcs = PROPERTY_GRAPH_SRCS,
    deps = [
        ":catalog",
        ":types",
        "@maven//:com_google_guava_guava",
    ],
)

COMMON_SRCS = [
    "LanguageOptions.java",
    "Client.java",
]

java_library(
    name = "common",
    srcs = COMMON_SRCS,
    deps = [
        ":channel_provider",
        "//googlesql/local_service:local_service_java_grpc",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
    ],
)

CATALOG_SRCS = [
    "AllowedHintsAndOptions.java",
    "Column.java",
    "Connection.java",
    "Constant.java",
    "DescriptorPoolSerializer.java",
    "Function.java",
    "FunctionArgumentType.java",
    "FunctionSignature.java",
    "GoogleSQLBuiltinFunctionOptions.java",
    "Model.java",
    "NotFoundException.java",
    "Parameter.java",
    "ParseLocationRange.java",
    "ParseResumeLocation.java",
    "Procedure.java",
    "ResolvedFunctionCallInfo.java",
    "Sequence.java",
    "SqlException.java",
    "SqlFormatter.java",
    "TableContent.java",
    "TVFSignature.java",
    "Table.java",
    "TableValuedFunction.java",
]

SIMPLE_CATALOG_SRCS = [
    "AnalyzerOptions.java",
    "EvaluatorTableModifyResponse.java",
    "PreparedExpression.java",
    "PreparedModify.java",
    "PreparedQuery.java",
    "Catalog.java",
    "SimpleCatalog.java",
    "SimpleColumn.java",
    "SimpleConnection.java",
    "SimpleModel.java",
    "SimpleSequence.java",
    "SimpleTable.java",
    "SimplePropertyGraph.java",
    "SimpleGraphElementTable.java",
    "SimpleGraphNodeTable.java",
    "SimpleGraphEdgeTable.java",
    "SimpleGraphPropertyDefinition.java",
    "SimpleGraphPropertyDeclaration.java",
    "SimpleGraphNodeTableReference.java",
    "SimpleGraphElementLabel.java",
    "SimpleGraphDynamicLabel.java",
    "SimpleGraphDynamicProperties.java",
]

java_library(
    name = "client",
    srcs = COMMON_SRCS + SIMPLE_CATALOG_SRCS + CATALOG_SRCS,
    plugins = [
        ":auto_value_processor_plugin",
        ":auto_one_of_processor_plugin",
        ":auto_annotation_processor_plugin",
    ],
    deps = BUILTIN_PROTO_DEPS + [
        ":channel_provider",
        ":property_graph",
        ":types",
        "//googlesql/local_service:local_service_java_grpc",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/proto:simple_catalog_java_proto",
        "//googlesql/proto:simple_property_graph_java_proto",
        "//googlesql/public:builtin_function_java_proto",
        "//googlesql/public:constness_level_java_proto",
        "//googlesql/public:deprecation_warning_java_proto",
        "//googlesql/public:error_location_java_proto",
        "//googlesql/public:formatter_options_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:parse_location_range_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_connection_java_proto",
        "//googlesql/public:simple_constant_java_proto",
        "//googlesql/public:simple_model_java_proto",
        "//googlesql/public:simple_sequence_java_proto",
        "//googlesql/public:simple_table_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",  # unuseddeps: keep # buildcleaner: keep
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",  # buildcleaner: keep
        "//googlesql/resolved_ast:serialization_java_proto",
        "//java/com/google/googlesql/resolvedast:resolvedast_no_simple_catalog",  # unuseddeps: keep # buildcleaner: keep
        "@com_google_protobuf//:protobuf_java",  # unuseddeps: keep # buildcleaner: keep
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@io_grpc_grpc_java//stub",
        "@maven//:com_google_api_grpc_proto_google_common_protos",  # unuseddeps: keep # buildcleaner: keep
        "@maven//:com_google_auto_value_auto_value",
        "@maven//:com_google_auto_value_auto_value_annotations",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
        "@maven//:javax_annotation_javax_annotation_api",  # unuseddeps: keep # buildcleaner: keep
    ],
)

java_library(
    name = "catalog",
    srcs = CATALOG_SRCS,
    plugins = [
        ":auto_value_processor_plugin",
        ":auto_one_of_processor_plugin",
        ":auto_annotation_processor_plugin",
    ],
    deps = [
        ":common",
        ":types",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:function_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/public:builtin_function_java_proto",
        "//googlesql/public:constness_level_java_proto",
        "//googlesql/public:deprecation_warning_java_proto",
        "//googlesql/public:error_location_java_proto",
        "//googlesql/public:formatter_options_java_proto",
        "//googlesql/public:function_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:parse_location_range_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_constant_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "@com_google_protobuf//:protobuf_java",
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@maven//:com_google_auto_value_auto_value",
        "@maven//:com_google_auto_value_auto_value_annotations",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
        "@maven//:javax_annotation_javax_annotation_api",  # unuseddeps: keep # buildcleaner: keep
    ],
)

java_library(
    name = "simple_catalog",
    srcs = SIMPLE_CATALOG_SRCS,
    plugins = [
        ":auto_value_processor_plugin",
        ":auto_one_of_processor_plugin",
        ":auto_annotation_processor_plugin",
    ],
    deps = [
        ":catalog",
        ":channel_provider",
        ":common",
        ":property_graph",
        ":types",
        "//googlesql/local_service:local_service_java_grpc",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:function_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/proto:simple_catalog_java_proto",
        "//googlesql/proto:simple_property_graph_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:simple_connection_java_proto",
        "//googlesql/public:simple_constant_java_proto",
        "//googlesql/public:simple_model_java_proto",
        "//googlesql/public:simple_sequence_java_proto",
        "//googlesql/public:simple_table_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",  # unuseddeps: keep # buildcleaner: keep
        "//googlesql/resolved_ast:serialization_java_proto",  # unuseddeps: keep # buildcleaner: keep
        "//java/com/google/googlesql/resolvedast:resolvedast_no_simple_catalog",  # unuseddeps: keep # buildcleaner: keep
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@io_grpc_grpc_java//stub",
        "@maven//:com_google_auto_value_auto_value",
        "@maven//:com_google_auto_value_auto_value_annotations",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
        "@maven//:javax_annotation_javax_annotation_api",  # unuseddeps: keep # buildcleaner: keep
    ],
)

# This is a separate package to avoid circular dependency.
ANALYZER_SRCS = [
    "Analyzer.java",
    "AnalyzerHelper.java",
]

java_library(
    name = "debug_printable_node",
    srcs = ["DebugPrintableNode.java"],
    deps = ["@maven//:com_google_guava_guava"],
)

java_library(
    name = "analyzer",
    srcs = ANALYZER_SRCS,
    deps = [
        ":client",
        ":types",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/public/functions:datetime_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",  # buildcleaner: keep
        "//java/com/google/googlesql/resolvedast",
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@maven//:com_google_errorprone_error_prone_annotations",
    ],
)

java_library(
    name = "parser",
    srcs = ["Parser.java"],
    deps = [
        ":client",
        "//googlesql/local_service:local_service_java_proto",
        "//java/com/google/googlesql/parser",
        "@io_grpc_grpc_java//api",
        "@io_grpc_grpc_java//core",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
    ],
)

jarjar_library(
    name = "client_jar",
    jars = BUILTIN_PROTO_DEPS + [
        ":analyzer",
        ":parser",
        ":channel_provider",
        ":client",
        ":debug_printable_node",
        "//java/com/google/googlesql/parser",
        "//java/com/google/googlesql/resolvedast",
        "//java/com/google/googlesql/testing:sql_compliance_service_java_grpc",
        "//java/com/google/googlesql/testing:sql_compliance_service_java_proto",
        "//googlesql/compliance:test_driver_java_proto",
        "//googlesql/local_service:local_service_java_grpc",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/proto:simple_catalog_java_proto",
        "//googlesql/public:builtin_function_java_proto",
        "//googlesql/public:parse_location_range_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_connection_java_proto",
        "//googlesql/public:simple_constant_java_proto",
        "//googlesql/public:simple_model_java_proto",
        "//googlesql/public:simple_sequence_java_proto",
        "//googlesql/public:simple_table_java_proto",
        "//googlesql/resolved_ast:resolved_ast_enums_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "@io_grpc_grpc_java//context",
        "@io_grpc_grpc_java//core",
        "@io_grpc_grpc_java//netty",
        "@io_grpc_grpc_java//protobuf",
        "@io_grpc_grpc_java//protobuf-lite",
        "@io_grpc_grpc_java//stub",
        "@io_grpc_grpc_java//api",
        "@maven//:com_google_code_gson_gson",
        "@maven//:io_opencensus_opencensus_api",
        "@maven//:io_opencensus_opencensus_contrib_grpc_metrics",
    ],
    rules = [
        "rule com.google.gson.** com.google.googlesql.@0",
        "rule io.grpc.** com.google.googlesql.@0",
        "rule io.opencensus.** com.google.googlesql.@0",
        "zap com.google.protobuf.**",
    ],
)

jarjar_library(
    name = "client_src",
    jars = [
        ":libanalyzer-src.jar",
        ":libchannel_provider-src.jar",
        ":libclient-src.jar",
        ":libdebug_printable_node-src.jar",
        "//java/com/google/googlesql/resolvedast:libresolvedast-src.jar",
    ],
)

pom_file(
    name = "client_pom",
    artifact_id = "googlesql-client",
    artifact_name = "Core Client",
    targets = BUILTIN_PROTO_DEPS + [
        ":analyzer",
        ":channel_provider",
        ":client",
        ":debug_printable_node",
        "//java/com/google/googlesql/resolvedast",
        "//java/com/google/googlesql/testing:sql_compliance_service_java_grpc",
        "//java/com/google/googlesql/testing:sql_compliance_service_java_proto",
        "//googlesql/compliance:test_driver_java_proto",
        "//googlesql/local_service:local_service_java_grpc",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/proto:simple_catalog_java_proto",
        "//googlesql/public:builtin_function_java_proto",
        "//googlesql/public:parse_location_range_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_connection_java_proto",
        "//googlesql/public:simple_constant_java_proto",
        "//googlesql/public:simple_model_java_proto",
        "//googlesql/public:simple_sequence_java_proto",
        "//googlesql/public:simple_table_java_proto",
        "//googlesql/resolved_ast:resolved_ast_enums_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "@io_grpc_grpc_java//context",
        "@io_grpc_grpc_java//core",
        "@io_grpc_grpc_java//netty",
        "@io_grpc_grpc_java//protobuf",
        "@io_grpc_grpc_java//protobuf-lite",
        "@io_grpc_grpc_java//api",
        "@maven//:io_opencensus_opencensus_api",
        "@maven//:io_opencensus_opencensus_contrib_grpc_metrics",
    ],
)

javadoc_library(
    name = "client_javadoc",
    srcs = ANALYZER_SRCS + CHANNEL_PROVIDER_SRCS + CATALOG_SRCS +
           SIMPLE_CATALOG_SRCS + COMMON_SRCS + ["DebugPrintableNode.java"],
    root_packages = ["com.google.googlesql"],
    deps = [
        ":analyzer",
        ":channel_provider",
        ":client",
        ":debug_printable_node",
    ],
)

java_library(
    name = "testaccess",
    testonly = 1,
    srcs = ["TestAccess.java"],
    deps = [
        ":types",
        "@maven//:com_google_guava_guava",
    ],
)
