//
// Copyright 2018 Google LLC
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

#include "googlesql/base/path.h"

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql_base {

TEST(PathTest, ArgumentTypes) {
  // JoinPath/JoinPathRespectAbsolute must be able to accept arguments that are
  // compatible with string_view. So test a few of them here.
  const char char_array[] = "a";
  const char* char_ptr = "b";
  std::string string_type = "c";
  absl::string_view sp_type = "d";

  EXPECT_EQ("a/b/c/d", JoinPath(char_array, char_ptr, string_type, sp_type));
  EXPECT_EQ("a/b/c/d", JoinPathRespectAbsolute(char_array, char_ptr,
                                               string_type, sp_type));
}

TEST(PathTest, JoinPath) {
  EXPECT_EQ("/foo/bar", JoinPath("/foo", "bar"));
  EXPECT_EQ("foo/bar", JoinPath("foo", "bar"));
  EXPECT_EQ("foo/bar", JoinPath("foo", "/bar"));
  EXPECT_EQ("/foo/bar", JoinPath("/foo", "/bar"));

  EXPECT_EQ("/bar", JoinPath("", "/bar"));
  EXPECT_EQ("bar", JoinPath("", "bar"));
  EXPECT_EQ("/foo", JoinPath("/foo", ""));

  EXPECT_EQ("/foo/bar/baz/blah/blink/biz",
            JoinPath("/foo/bar/baz/", "/blah/blink/biz"));

  EXPECT_EQ("/foo/bar/baz", JoinPath("/foo", "bar", "baz"));
  EXPECT_EQ("foo/bar/baz", JoinPath("foo", "bar", "baz"));
  EXPECT_EQ("/foo/bar/baz/blah", JoinPath("/foo", "bar", "baz", "blah"));
  EXPECT_EQ("/foo/bar/baz/blah", JoinPath("/foo", "bar", "/baz", "blah"));
  EXPECT_EQ("/foo/bar/baz/blah", JoinPath("/foo", "/bar/", "/baz", "blah"));
  EXPECT_EQ("/foo/bar/baz/blah", JoinPath("/foo", "/bar/", "baz", "blah"));

  EXPECT_EQ("", JoinPath());
}

TEST(PathTest, JoinPathRespectAbsolute) {
  EXPECT_EQ("/foo/bar", JoinPathRespectAbsolute("/foo", "bar"));
  EXPECT_EQ("foo/bar", JoinPathRespectAbsolute("foo", "bar"));
  EXPECT_EQ("/bar", JoinPathRespectAbsolute("foo", "/bar"));
  EXPECT_EQ("/bar", JoinPathRespectAbsolute("/foo", "/bar"));

  EXPECT_EQ("/bar", JoinPathRespectAbsolute("", "/bar"));
  EXPECT_EQ("bar", JoinPathRespectAbsolute("", "bar"));
  EXPECT_EQ("/foo", JoinPathRespectAbsolute("/foo", ""));

  EXPECT_EQ("/blah/blink/biz",
            JoinPathRespectAbsolute("/foo/bar/baz/", "/blah/blink/biz"));

  EXPECT_EQ("/foo/bar/baz", JoinPathRespectAbsolute("/foo", "bar", "baz"));
  EXPECT_EQ("foo/bar/baz", JoinPathRespectAbsolute("foo", "bar", "baz"));
  EXPECT_EQ("/foo/bar/baz/blah",
            JoinPathRespectAbsolute("/foo", "bar", "baz", "blah"));
  EXPECT_EQ("/baz/blah",
            JoinPathRespectAbsolute("/foo", "bar", "/baz", "blah"));
  EXPECT_EQ("/baz/blah",
            JoinPathRespectAbsolute("/foo", "/bar/", "/baz", "blah"));
  EXPECT_EQ("/bar/baz/blah",
            JoinPathRespectAbsolute("/foo", "/bar/", "baz", "blah"));

  EXPECT_EQ("", JoinPathRespectAbsolute());
}

TEST(PathTest, IsAbs) {
  EXPECT_FALSE(IsAbsolutePath(""));
  EXPECT_FALSE(IsAbsolutePath("../foo"));
  EXPECT_FALSE(IsAbsolutePath("foo"));
  EXPECT_FALSE(IsAbsolutePath("./foo"));
  EXPECT_FALSE(IsAbsolutePath("foo/bar/baz/"));
  EXPECT_TRUE(IsAbsolutePath("/foo"));
  EXPECT_TRUE(IsAbsolutePath("/foo/bar/../baz"));
}

TEST(PathTest, AddSlash) {
  EXPECT_EQ("",                     AddSlash(""));
  EXPECT_EQ("/gfs/home/webmirror/", AddSlash("/gfs/home/webmirror"));
  EXPECT_EQ("/usr/local/",          AddSlash("/usr/local/"));
  EXPECT_EQ("tmp/",                 AddSlash("tmp"));
}

TEST(PathTest, Dirname) {
  EXPECT_EQ("/hello",    Dirname("/hello/"));
  EXPECT_EQ("/",         Dirname("/hello"));
  EXPECT_EQ("hello",     Dirname("hello/world"));
  EXPECT_EQ("hello",     Dirname("hello/"));
  EXPECT_EQ("",          Dirname("world"));
  EXPECT_EQ("/",         Dirname("/"));
  EXPECT_EQ("",          Dirname(""));
}

TEST(PathTest, Basename) {
  EXPECT_EQ("",          Basename("/hello/"));
  EXPECT_EQ("hello",     Basename("/hello"));
  EXPECT_EQ("world",     Basename("hello/world"));
  EXPECT_EQ("",          Basename("hello/"));
  EXPECT_EQ("world",     Basename("world"));
  EXPECT_EQ("",          Basename("/"));
  EXPECT_EQ("",          Basename(""));
}

TEST(PathTest, SplitPath) {
  // We cannot write the type directly within the EXPECT, because the ',' breaks
  // the macro.
  using Pair = std::pair<absl::string_view, absl::string_view>;
  EXPECT_EQ(Pair("/hello", ""),         SplitPath("/hello/"));
  EXPECT_EQ(Pair("/", "hello"),         SplitPath("/hello"));
  EXPECT_EQ(Pair("hello", "world"),     SplitPath("hello/world"));
  EXPECT_EQ(Pair("hello", ""),          SplitPath("hello/"));
  EXPECT_EQ(Pair("", "world"),          SplitPath("world"));
  EXPECT_EQ(Pair("/", ""),              SplitPath("/"));
  EXPECT_EQ(Pair("", ""),               SplitPath(""));
}

}  // namespace googlesql_base
