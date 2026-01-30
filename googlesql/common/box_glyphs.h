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

#ifndef GOOGLESQL_COMMON_BOX_GLYPHS_H_
#define GOOGLESQL_COMMON_BOX_GLYPHS_H_

#include "absl/strings/string_view.h"

namespace googlesql {

// This file defines the glyphs (characters) required to render structural
// visualizations in a terminal environment.
//
// These glyphs can be composed to implement various visualizations including:
// - Hierarchical trees (e.g., resolved AST)
// - Tables and boxes (e.g. query results)
//
// Two definitions are provided:
// - `kUnicodeGlyphs`: Uses standard UTF-8 Box Drawing characters
// - `kAsciiGlyphs`: 7-bit ASCII characters (+, -, |)

struct BoxGlyphs {
  // Lines
  absl::string_view horizontal;  // ─ or -
  absl::string_view vertical;    // │ or |

  // Corners
  absl::string_view top_left;      // ┌ or +
  absl::string_view top_right;     // ┐ or +
  absl::string_view bottom_left;   // └ or +
  absl::string_view bottom_right;  // ┘ or +

  // T-Junctions
  absl::string_view vertical_right;   // ├
  absl::string_view vertical_left;    // ┤
  absl::string_view horizontal_down;  // ┬
  absl::string_view horizontal_up;    // ┴

  // Intersection
  absl::string_view cross;  // ┼

  // Tree connectors
  absl::string_view tree_branch;    // ├─
  absl::string_view tree_last;      // └─
  absl::string_view tree_vertical;  // │<space>
  absl::string_view tree_space;     // <space><space>
};

constexpr BoxGlyphs kAsciiBoxGlyphs = {
    .horizontal = "-",
    .vertical = "|",

    .top_left = "+",
    .top_right = "+",
    .bottom_left = "+",
    .bottom_right = "+",

    .vertical_right = "+",
    .vertical_left = "+",
    .horizontal_down = "+",
    .horizontal_up = "+",

    .cross = "+",

    .tree_branch = "+-",
    .tree_last = "+-",
    .tree_vertical = "| ",
    .tree_space = "  ",
};

// Standard Box Drawing block (U+2500 - U+257F).
constexpr BoxGlyphs kUnicodeBoxGlyphs = {
    .horizontal = "─",  // U+2500
    .vertical = "│",    // U+2502

    .top_left = "┌",      // U+250C
    .top_right = "┐",     // U+2510
    .bottom_left = "└",   // U+2514
    .bottom_right = "┘",  // U+2518

    .vertical_right = "├",   // U+251C
    .vertical_left = "┤",    // U+2524
    .horizontal_down = "┬",  // U+252C
    .horizontal_up = "┴",    // U+2534

    .cross = "┼",  // U+253C

    .tree_branch = "├─",
    .tree_last = "└─",
    .tree_vertical = "│ ",
    .tree_space = "  ",
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_BOX_GLYPHS_H_
