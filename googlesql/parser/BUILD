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

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")
load("@rules_python//python:py_test.bzl", "py_test")
load("//bazel:textmapper.bzl", "tm_syntax")
load(":builddefs.bzl", "gen_parser_test")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

genrule(
    name = "gen_extra_files",
    srcs = [
        "parse_tree_generated.h",
    ],
    outs = [
        "parse_tree_visitor.h",
        "parse_tree_decls.h",
        "parse_tree_accept_methods.inc",
    ],
    cmd = """$(location :gen_extra_files_tool) $(SRCS) $(OUTS)""",
    tools = [":gen_extra_files_tool"],
)

py_binary(
    name = "gen_extra_files_tool",
    srcs = ["gen_extra_files.py"],
    main = "gen_extra_files.py",
    deps = [
    ],
)

genrule(
    name = "gen_parse_tree_h",
    srcs = [
        "parse_tree_generated.h.template",
    ],
    outs = ["parse_tree_generated.h"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_parse_tree_cc",
    srcs = [
        "parse_tree_generated.cc.template",
    ],
    outs = ["parse_tree_generated.cc"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_ast_node_kind_h",
    srcs = [
        "ast_node_kind.h.template",
    ],
    outs = ["ast_node_kind.h"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_protos",
    srcs = [
        "parse_tree.proto.template",
    ],
    outs = ["parse_tree.proto"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_parse_tree_serializer_cc",
    srcs = [
        "parse_tree_serializer.cc.template",
    ],
    outs = ["parse_tree_serializer.cc"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_parse_tree_serializer_headers",
    srcs = [
        "parse_tree_serializer.h.template",
    ],
    outs = ["parse_tree_serializer.h"],
    cmd = "$(location :gen_parse_tree) $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

genrule(
    name = "gen_parse_tree_md",
    srcs = [
        "parse_tree_generated.md.template",
    ],
    outs = ["parse_ast.md"],
    cmd = "$(location :gen_parse_tree) --noallow_hash_prefix $(OUTS) $(SRCS)",
    tools = [":gen_parse_tree"],
)

proto_library(
    name = "parse_tree_proto",
    srcs = ["parse_tree.proto"],
    deps = [
        ":ast_enums_proto",
        "//googlesql/public:parse_location_range_proto",
        "//googlesql/public:type_proto",
    ],
)

cc_proto_library(
    name = "parse_tree_cc_proto",
    deps = [":parse_tree_proto"],
)

py_proto_library(
    name = "parse_tree_py_proto",
    deps = [":parse_tree_proto"],
)

py_library(
    name = "generator_utils",
    srcs = ["generator_utils.py"],
)

py_test(
    name = "generator_utils_test",
    size = "small",
    srcs = ["generator_utils_test.py"],
    deps = [
        ":generator_utils",
        "@io_abseil_py//absl/testing:absltest",
    ],
)

py_binary(
    name = "gen_parse_tree",
    srcs = ["gen_parse_tree.py"],
    main = "gen_parse_tree.py",
    deps = [
        ":ast_enums_py_pb2",
        ":generator_utils",
        "@io_abseil_py//absl:app",
        "@io_abseil_py//absl/flags",
        "@pypi//jinja2",
        "@pypi//markupsafe",
    ],
)

proto_library(
    name = "ast_enums_proto",
    srcs = ["ast_enums.proto"],
)

cc_proto_library(
    name = "ast_enums_cc_proto",
    deps = [":ast_enums_proto"],
)

java_proto_library(
    name = "ast_enums_java_proto",
    deps = [":ast_enums_proto"],
)

java_proto_library(
    name = "ast_java_proto",
    deps = [":parse_tree_proto"],
)

tm_syntax(
    name = "generate_tm_parser",
    src = "googlesql.tm",
    outs = [
        "tm_lexer.cc",
        "tm_lexer.h",
        "tm_parser.cc",
        "tm_parser.h",
        "tm_token.h",
    ],
)

cc_library(
    name = "tm_lexer",
    srcs = [
        "tm_lexer.cc",
    ],
    hdrs = [
        "tm_lexer.h",
        "tm_token.h",
    ],
    deps = [
        ":parser_mode",
        "//googlesql/common:errors",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "tm_parser",
    srcs = [
        "textmapper_lexer_adapter.cc",
        "tm_parser.cc",
    ],
    hdrs = [
        "join_processor.h",
        "parse_tree.h",
        "parse_tree_decls.h",
        "parse_tree_generated.h",
        "parser_internal.h",
        "parser_runtime_info.h",
        "statement_properties.h",
        "textmapper_lexer_adapter.h",
        "tm_parser.cc",
        "tm_parser.h",
    ],
    copts = [
        "-fno-sanitize=object-size",  # can timeout: TODO
        "-g0",  # can timeout: TODO
    ],
    deps = [
        ":ast_enums_cc_proto",
        ":ast_node_factory",
        ":bison_parser_generated_lib",
        ":keywords",
        ":lookahead_transformer",
        ":parse_tree",
        ":parser_mode",
        ":token",
        ":token_with_location",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:arena_allocator",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common:timer_util",
        "//googlesql/common:warning_sink",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/parser/macros:macro_expander",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:strings",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

py_proto_library(
    name = "ast_enums_py_pb2",
    deps = [":ast_enums_proto"],
)

cc_library(
    name = "parser",
    srcs = [
        "ast_node_internal.h",
        "parser.cc",
        "parser_internal.cc",
        "unparser.cc",
    ],
    hdrs = [
        "ast_node.h",
        "ast_node_kind.h",
        "parse_tree.h",
        "parse_tree_decls.h",
        "parse_tree_errors.h",
        "parse_tree_visitor.h",
        "parser.h",
        "parser_internal.h",
        "parser_runtime_info.h",
        "statement_properties.h",
        "unparser.h",
        "visit_result.h",
    ],
    deps = [
        ":ast_enums_cc_proto",
        ":ast_node_factory",
        ":bison_parser_generated_lib",
        ":keywords",
        ":lookahead_transformer",
        ":parse_tree",
        ":parser_mode",
        ":tm_parser",
        ":token",
        ":token_with_location",
        ":tokenizer",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:arena_allocator",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common:thread_stack",
        "//googlesql/common:timer_util",
        "//googlesql/common:utf_util",
        "//googlesql/common:warning_sink",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/parser/macros:macro_expander",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "ast_node_factory",
    srcs = ["ast_node_factory.cc"],
    hdrs = ["ast_node_factory.h"],
    deps = [
        ":parse_tree",
        "//googlesql/base:arena",
        "//googlesql/base:arena_allocator",
        "//googlesql/public:id_string",
        "//googlesql/public:parse_location",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "parser_mode",
    hdrs = [
        "parser_mode.h",
    ],
)

cc_library(
    name = "token_with_location",
    srcs = ["token_with_location.cc"],
    hdrs = ["token_with_location.h"],
    deps = [
        ":token",
        "//googlesql/base:status",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "tokenizer",
    srcs = [
        "tokenizer.cc",
    ],
    hdrs = [
        "tokenizer.h",
    ],
    deps = [
        ":tm_lexer",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "lookahead_transformer",
    srcs = ["lookahead_transformer.cc"],
    hdrs = ["lookahead_transformer.h"],
    deps = [
        ":parser_mode",
        ":token",
        ":token_with_location",
        "//googlesql/base:arena",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/parser/macros:macro_expander",
        "//googlesql/parser/macros:token_provider",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "lookahead_transformer_test",
    srcs = [
        "lookahead_transformer_test.cc",
    ],
    deps = [
        ":lookahead_transformer",
        ":parser",
        ":parser_mode",
        ":token",
        ":token_with_location",
        "//googlesql/base:arena",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/public:language_options",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "ast_node_util",
    srcs = ["ast_node_util.cc"],
    hdrs = ["ast_node_util.h"],
    deps = [
        ":parser",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "ast_node_util_test",
    srcs = ["ast_node_util_test.cc"],
    deps = [
        ":ast_node_util",
        ":parse_tree_serializer",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "bison_parser_generated_lib",
    srcs = [
        "join_processor.cc",
    ],
    hdrs = [
        "join_processor.h",
        "parser_internal.h",
        "parser_runtime_info.h",
        "statement_properties.h",
    ],
    # The -O2 is required because the generated bison parser uses a ton of
    # stack space in non-optimized builds. The entire parser is contained in
    # a single giant function, and non-optimized compilation does not reuse
    # stack slots for local variables. Without -O2, clients are likely to
    # fail in fastbuild with default 64KB stack sizes, or to start failing
    # when the parser is extended further.
    copts = [
        "-O2",
    ],
    tags = ["nofixdeps"],  # generated and real files are interdependent
    deps = [
        ":ast_node_factory",
        ":keywords",
        ":parse_tree",
        ":parser_mode",
        ":token",
        ":token_with_location",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:arena_allocator",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:timer_util",
        "//googlesql/common:warning_sink",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:strings",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "parse_tree_serializer",
    srcs = ["parse_tree_serializer.cc"],
    hdrs = [
        "parse_tree_serializer.h",
        "parser.h",
        "statement_properties.h",
    ],
    deps = [
        ":bison_parser_generated_lib",
        ":parse_tree",
        ":parse_tree_cc_proto",
        ":parser_mode",
        "//googlesql/base:arena",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:warning_sink",
        "//googlesql/parser/macros:macro_catalog",
        "//googlesql/public:error_helpers",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "parse_tree",
    srcs = [
        "ast_node_internal.h",
        "parse_tree.cc",
        "parse_tree_accept_methods.inc",
        "parse_tree_errors.cc",
        "parse_tree_generated.cc",
    ],
    hdrs = [
        "ast_node.h",
        "ast_node_kind.h",
        "parse_tree.h",
        "parse_tree_decls.h",
        "parse_tree_errors.h",
        "parse_tree_generated.h",
        "parse_tree_visitor.h",
        "visit_result.h",
    ],
    deps = [
        ":ast_enums_cc_proto",
        "//googlesql/base",
        "//googlesql/base:arena_allocator",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common:thread_stack",
        "//googlesql/common:utf_util",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/proto:internal_fix_suggestion_cc_proto",
        "//googlesql/public:deprecation_warning_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:id_string",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:strings",
        "//googlesql/public:type_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "parse_tree_test",
    size = "small",
    srcs = ["parse_tree_test.cc"],
    deps = [
        ":parse_tree",
        ":parser",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "parse_tree_errors_test",
    size = "small",
    srcs = ["parse_tree_errors_test.cc"],
    deps = [
        ":parse_tree",
        ":parser",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:status_payload_matchers",
        "//googlesql/proto:internal_fix_suggestion_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "run_parser_test_lib",
    testonly = 1,
    srcs = ["run_parser_test.cc"],
    deps = [
        ":deidentify",
        ":parser",
        ":parser_mode",
        "//googlesql/base:check",
        "//googlesql/base:edit_distance",
        "//googlesql/base:logging",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:error_helpers",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_helpers",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public/testing:test_case_options_util",
        "//googlesql/scripting:parse_helpers",
        "//googlesql/scripting:script_segment",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_file_based_test_driver//file_based_test_driver",
        "@com_google_file_based_test_driver//file_based_test_driver:run_test_case_result",
        "@com_google_file_based_test_driver//file_based_test_driver:test_case_options",
        "@com_googlesource_code_re2//:re2",
    ],
    # Otherwise tests in this .cc file may get dropped by the linker when statically linking tests
    # which depend on this library, since these tests aren't referenced from main.
    alwayslink = True,
)

cc_test(
    name = "unparser_test",
    size = "small",
    srcs = ["unparser_test.cc"],
    deps = [
        ":parser",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "keywords",
    srcs = [
        "keywords.cc",
    ],
    hdrs = [
        "keywords.h",
    ],
    deps = [
        ":token",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:map_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "token",
    hdrs = [
        "tm_token.h",
    ],
    deps = [
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "keywords_test",
    srcs = ["keywords_test.cc"],
    data = [
        "googlesql.tm",
    ],
    deps = [
        ":keywords",
        "//googlesql/base",
        "//googlesql/base:path",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "deidentify",
    srcs = ["deidentify.cc"],
    hdrs = ["deidentify.h"],
    deps = [
        ":parser",
        "//googlesql/base:status",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:strings",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "deidentify_test",
    srcs = ["deidentify_test.cc"],
    deps = [
        ":deidentify",
        ":parse_tree",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
    ],
)

[gen_parser_test(
    filename = f.split("/")[-1],
) for f in glob(
    ["testdata/*.test"],
    exclude = ["testdata/parse_tree_serializer.test"],
)]

bzl_library(
    name = "builddefs_bzl",
    srcs = ["builddefs.bzl"],
    deps = ["@rules_cc//cc:core_rules"],
)

exports_files([
    "keywords.h",
    "unparser.h",
])

cc_test(
    name = "textmapper_lexer_adapter_test",
    srcs = ["textmapper_lexer_adapter_test.cc"],
    deps = [
        ":parser",  # buildcleaner: keep
        ":parser_mode",
        ":tm_parser",
        ":token",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:language_options",
        "@com_google_absl//absl/strings:string_view",
    ],
)
