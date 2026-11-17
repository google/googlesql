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

#include "googlesql/resolved_ast/resolved_ast_formatter.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/box_glyphs.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/base/nullability.h"
#include "googlesql/base/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// ResolvedASTNodeView Implementation

ResolvedASTNodeView::ResolvedASTNodeView(const ResolvedNode* /*absl_nullable*/ node)
    : node_(node) {
  if (node_ != nullptr) {
    node_->CollectDebugStringFields(&fields_);
  }
}

std::optional<ResolvedNodeKind> ResolvedASTNodeView::node_kind() const {
  return node_ != nullptr ? std::make_optional(node_->node_kind())
                          : std::nullopt;
}

std::string ResolvedASTNodeView::node_name(
    const ResolvedNode::DebugStringConfig& config) const {
  if (node_ == nullptr) {
    return "<nullptr AST node>";
  }
  return node_->GetNameForDebugString(config);
}

ResolvedASTFieldView ResolvedASTNodeView::field(int i) const {
  ABSL_DCHECK_GE(i, 0);
  ABSL_DCHECK_LT(i, fields_.size());
  return ResolvedASTFieldView(fields_[i]);
}

const ResolvedScan* /*absl_nullable*/ ResolvedASTNodeView::pipe_input_scan() const {
  if (node_ != nullptr && node_->IsScan()) {
    return node_->GetAs<ResolvedScan>()->GetPipeInputScan();
  }
  return nullptr;
}

std::vector<ResolvedASTNodeView> ResolvedASTNodeView::GetLinearPipelineChain()
    const {
  if (node_ == nullptr || !node_->IsScan() || pipe_input_scan() == nullptr) {
    return {*this};
  }
  std::vector<ResolvedASTNodeView> pipe_chain;
  for (const ResolvedScan* scan = node_->GetAs<ResolvedScan>(); scan != nullptr;
       scan = scan->GetPipeInputScan()) {
    pipe_chain.push_back(ResolvedASTNodeView(scan));
  }
  std::reverse(pipe_chain.begin(), pipe_chain.end());
  return pipe_chain;
}

// ResolvedASTFieldView Implementation

ResolvedASTFieldView::ResolvedASTFieldView(
    const ResolvedNode::DebugStringField& field)
    : field_(&field) {}

ResolvedASTNodeView ResolvedASTFieldView::child_node(int i) const {
  if (i < 0 || i >= field_->nodes.size()) {
    return ResolvedASTNodeView(nullptr);
  }
  return ResolvedASTNodeView(field_->nodes[i]);
}

bool ResolvedASTFieldView::IsPipeInputScan(
    const ResolvedScan* /*absl_nullable*/ pipe_input_to_elide) const {
  return pipe_input_to_elide != nullptr && field_->nodes.size() == 1 &&
         field_->nodes[0] == pipe_input_to_elide;
}

namespace {

// Internal Tree Renderer that renders a ResolvedASTNodeView text tree. It is
// responsible for text layout, glyphs, and line. The config has options to
// control the rendering style, and also to choose between standard hierarchical
// tree layout and linear pipe chains.
//
// The renderer recursively walks the tree, updating output as it goes. When
// rendering in linear_mode, it renders the node followed by the chain of pipe
// operators in reverse order.
class TreeRenderer {
 public:
  TreeRenderer(const ResolvedNode::DebugStringConfig& config,
               std::string* output)
      : config_(config),
        glyphs_(config.use_box_glyphs ? kUnicodeBoxGlyphs : kAsciiBoxGlyphs),
        output_(output) {}

  void Render(const ResolvedASTNodeView& root) {
    RenderNode(root, /*stem=*/"", /*node_connector=*/"",
               /*field_value_indent=*/"", /*pipe_input_to_elide=*/nullptr);
  }

 private:
  const ResolvedNode::DebugStringConfig& config_;
  const BoxGlyphs& glyphs_;
  std::string* output_;

  bool HasExceededMaxLength() const {
    return config_.max_length >= 0 && output_->size() > config_.max_length;
  }

  void RenderNode(const ResolvedASTNodeView& node_view, absl::string_view stem,
                  absl::string_view node_connector,
                  absl::string_view field_value_indent,
                  const ResolvedScan* pipe_input_to_elide);

  void RenderStandardNodeBody(const ResolvedASTNodeView& node_view,
                              absl::string_view stem,
                              absl::string_view node_connector,
                              absl::string_view field_value_indent,
                              const ResolvedScan* pipe_input_to_elide);

  void RenderLinearScanChain(const ResolvedASTNodeView& top_scan_view,
                             absl::string_view stem,
                             absl::string_view connector);

  bool ShouldOmitField(const ResolvedASTNodeView& node_view,
                       const ResolvedASTFieldView& field,
                       const ResolvedScan* pipe_input_to_elide) const;

  absl::string_view FormatDecorations(const ResolvedASTFieldView& field) const;

  std::string FormatInlineNode(
      const ResolvedASTNodeView& node_view,
      absl::Span<const ResolvedASTFieldView> visible_fields,
      const ResolvedScan* pipe_input_to_elide) const;

  void AppendAnnotations(const ResolvedASTNodeView& node_view,
                         std::string* output) const;
};

bool TreeRenderer::ShouldOmitField(
    const ResolvedASTNodeView& node_view, const ResolvedASTFieldView& field,
    const ResolvedScan* pipe_input_to_elide) const {
  // `node_view.node()->IsScan()` is used to detect cases when the pipe
  // input isn't directly an input_scan field of ResolvedScan node (e.g. a TVF
  // argument). In those cases, for clarity, we show the placeholder rather than
  // omitting the field.
  return config_.omit_pipe_input_scan_field && node_view.node() != nullptr &&
         node_view.node()->IsScan() &&
         field.IsPipeInputScan(pipe_input_to_elide);
}

absl::string_view TreeRenderer::FormatDecorations(
    const ResolvedASTFieldView& field) const {
  const bool created =
      config_.print_created_columns && field.is_column_created();
  if (config_.print_accessed) {
    if (field.is_accessed()) {
      return created ? "{c}{*}" : "{*}";
    } else {
      return created ? "{c}{ }" : "{ }";
    }
  }
  return created ? "{c}" : "";
}

void TreeRenderer::AppendAnnotations(const ResolvedASTNodeView& node_view,
                                     std::string* output) const {
  if (node_view.node() != nullptr) {
    for (const auto& annotation : config_.annotations) {
      if (annotation.node == node_view.node()) {
        absl::StrAppend(output, " ", annotation.annotation);
        break;
      }
    }
  }
}

std::string TreeRenderer::FormatInlineNode(
    const ResolvedASTNodeView& node_view,
    absl::Span<const ResolvedASTFieldView> visible_fields,
    const ResolvedScan* pipe_input_to_elide) const {
  std::string header = node_view.node_name(config_);

  if (!visible_fields.empty()) {
    header += '(';
    bool first = true;
    for (const auto& field_view : visible_fields) {
      if (!first) header += ", ";
      first = false;
      const absl::string_view dec = FormatDecorations(field_view);
      const absl::string_view val =
          field_view.IsPipeInputScan(pipe_input_to_elide)
              ? "<pipe_input>"
              : field_view.scalar_value();
      if (field_view.name().empty()) {
        absl::StrAppend(&header, val, dec);
      } else {
        absl::StrAppend(&header, field_view.name(), dec, "=", val);
      }
    }
    header += ')';
  }

  AppendAnnotations(node_view, &header);
  return header;
}

void TreeRenderer::RenderNode(const ResolvedASTNodeView& node_view,
                              absl::string_view stem,
                              absl::string_view node_connector,
                              absl::string_view field_value_indent,
                              const ResolvedScan* pipe_input_to_elide) {
  if (HasExceededMaxLength()) {
    return;
  }
  if (config_.linear_mode && !node_view.is_null() &&
      node_view.pipe_input_scan() != nullptr) {
    RenderLinearScanChain(node_view, stem, node_connector);
    return;
  }

  RenderStandardNodeBody(node_view, stem, node_connector, field_value_indent,
                         pipe_input_to_elide);
}

void TreeRenderer::RenderStandardNodeBody(
    const ResolvedASTNodeView& node_view, absl::string_view stem,
    absl::string_view node_connector, absl::string_view field_value_indent,
    const ResolvedScan* pipe_input_to_elide) {
  if (HasExceededMaxLength()) {
    return;
  }

  if (node_view.is_null()) {
    absl::StrAppend(output_, stem, node_connector, "<nullptr AST node>\n");
    return;
  }

  // Collect and filter fields once for this node frame.
  std::vector<ResolvedASTFieldView> visible_fields;
  visible_fields.reserve(node_view.num_fields());
  for (int i = 0; i < node_view.num_fields(); ++i) {
    const ResolvedASTFieldView field_view = node_view.field(i);
    if (!ShouldOmitField(node_view, field_view, pipe_input_to_elide)) {
      visible_fields.push_back(field_view);
    }
  }

  // Determine if this node can be formatted inline.
  bool can_inline = !visible_fields.empty() &&
                    node_view.node_kind() != RESOLVED_STATIC_DESCRIBE_SCAN;
  if (can_inline) {
    for (const auto& field_view : visible_fields) {
      const bool is_pipe_input =
          field_view.IsPipeInputScan(pipe_input_to_elide);
      if (!is_pipe_input &&
          (!field_view.is_scalar() ||
           absl::StrContains(field_view.scalar_value(), '\n'))) {
        can_inline = false;
        break;
      }
    }
  }

  if (can_inline) {
    // Render single line node.
    absl::StrAppend(
        output_, stem, node_connector,
        FormatInlineNode(node_view, visible_fields, pipe_input_to_elide), "\n");
    return;
  }

  // Multiline node header.
  absl::StrAppend(output_, stem, node_connector, node_view.node_name(config_));
  AppendAnnotations(node_view, output_);
  absl::StrAppend(output_, "\n");

  // Render each field as a branch.
  for (int i = 0; i < visible_fields.size(); ++i) {
    if (HasExceededMaxLength()) {
      return;
    }
    const auto& field_view = visible_fields[i];
    const bool is_last = (i == visible_fields.size() - 1);

    absl::string_view field_connector =
        is_last ? glyphs_.tree_last : glyphs_.tree_branch;
    absl::string_view field_indent =
        is_last ? glyphs_.tree_space : glyphs_.tree_vertical;

    const bool is_pipe_input = field_view.IsPipeInputScan(pipe_input_to_elide);
    const absl::string_view scalar_value =
        is_pipe_input ? "<pipe_input>" : field_view.scalar_value();
    const bool is_scalar = field_view.is_scalar() || is_pipe_input;

    const absl::string_view dec = FormatDecorations(field_view);
    const bool print_field_name = !field_view.name().empty();
    const bool value_has_newlines =
        !is_pipe_input && absl::StrContains(scalar_value, '\n');
    const bool print_one_line = is_scalar && !value_has_newlines;

    if (print_field_name) {
      absl::StrAppend(output_, stem, field_value_indent, field_connector,
                      field_view.name(), dec, "=");
      if (print_one_line) {
        absl::StrAppend(output_, scalar_value);
      }
      absl::StrAppend(output_, "\n");
    } else if (print_one_line) {
      absl::StrAppend(output_, stem, field_value_indent, field_connector,
                      scalar_value, dec, "\n");
    }

    if (!print_one_line) {
      if (value_has_newlines) {
        absl::StrAppend(output_, stem, field_value_indent, field_indent,
                        "  \"\"\"\n");
        for (absl::string_view line : absl::StrSplit(scalar_value, '\n')) {
          if (HasExceededMaxLength()) {
            return;
          }
          std::string line_content = absl::StrCat(field_indent, "  ", line);
          absl::StrAppend(output_, stem, field_value_indent,
                          absl::StripTrailingAsciiWhitespace(line_content),
                          "\n");
        }
        if (HasExceededMaxLength()) {
          return;
        }
        absl::StrAppend(output_, stem, field_value_indent, field_indent,
                        "  \"\"\"\n");
      }

      if (!is_pipe_input) {
        int num_child_nodes = field_view.num_child_nodes();
        for (int j = 0; j < num_child_nodes; ++j) {
          if (HasExceededMaxLength()) {
            return;
          }
          const auto& child = field_view.child_node(j);
          const absl::string_view field_name_indent =
              print_field_name ? field_indent : "";
          const bool is_last_child =
              (j == num_child_nodes - 1) && (print_field_name || is_last);
          const absl::string_view child_field_value_indent =
              is_last_child ? glyphs_.tree_space : glyphs_.tree_vertical;
          const absl::string_view child_node_connector =
              is_last_child ? glyphs_.tree_last : glyphs_.tree_branch;

          RenderNode(child,
                     /*stem=*/
                     absl::StrCat(stem, field_value_indent, field_name_indent),
                     child_node_connector, child_field_value_indent,
                     pipe_input_to_elide);
        }
      }
    }
  }
}

void TreeRenderer::RenderLinearScanChain(
    const ResolvedASTNodeView& top_scan_view, absl::string_view stem,
    absl::string_view connector) {
  if (HasExceededMaxLength()) {
    return;
  }

  // pipe_chain[0] is the first scan (with pipe input null), and
  // pipe_chain.back() is top_scan_view.
  std::vector<ResolvedASTNodeView> pipe_chain =
      top_scan_view.GetLinearPipelineChain();

  // If pipe_chain.size() > 1, this scan is followed by pipe operators below
  // it, so any bottom-left corner ("└─") should be turned into a tee branch
  // ("├─"), because pipe operators that follow are formatted like additional
  // children in the tree (connected by a `tree_vertical_light` line).
  //
  // Without this adjustment, we get trees that look like this:
  //     └──TableScan(...)
  //     ·  └─field_value=...
  //     |> FilterScan(...)
  //
  // The adjustment makes trees look like this, with a better connection to the
  // dotted line and pipe operator below.
  //     ├──TableScan(...)
  //     ·  └─field_value=...
  //     |> FilterScan(...)
  absl::string_view source_connector = connector;
  if (pipe_chain.size() > 1 && source_connector == glyphs_.tree_last) {
    source_connector = glyphs_.tree_branch;
  }

  RenderStandardNodeBody(
      pipe_chain.front(), stem,
      /*node_connector=*/absl::StrCat(source_connector, glyphs_.horizontal),
      /*field_value_indent=*/glyphs_.tree_vertical_light,
      /*pipe_input_to_elide=*/nullptr);

  // Emit each subsequent pipe operator.
  for (int i = 1; i < pipe_chain.size(); ++i) {
    if (HasExceededMaxLength()) {
      return;
    }
    const bool is_last = (i == pipe_chain.size() - 1);
    RenderStandardNodeBody(
        pipe_chain[i], stem,
        /*node_connector=*/"|> ",
        /*field_value_indent=*/
        is_last ? "   " : glyphs_.tree_vertical_light,
        /*pipe_input_to_elide=*/pipe_chain[i].pipe_input_scan());
  }
}

}  // namespace

std::string RenderResolvedAST(const ResolvedNode* root,
                              const ResolvedNode::DebugStringConfig& config) {
  std::string output;
  TreeRenderer(config, &output).Render(ResolvedASTNodeView(root));
  if (config.max_length >= 0 && output.size() > config.max_length) {
    return PrettyTruncateUTF8(output, config.max_length);
  }
  return output;
}

}  // namespace googlesql
