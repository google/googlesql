//
// Copyright 2004 Google LLC
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

#include "googlesql/base/general_trie.h"

#include <string.h>

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql_base {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

struct TestIntTraverser : public GeneralTrie<int, 0>::Traverser {
 public:
  void Process(const std::string& s, const int& data) override {
    values.push_back(data);
  }

  std::vector<int> values;
};

TEST(GeneralTrie, Basic) {
  GeneralTrie<int, 0> trie;
  const GeneralTrie<int, 0>& const_trie = trie;

  trie.Insert("one", 1);
  trie.Insert("two", 2);
  trie.Insert("three", 3);
  trie.Insert("oneflew", 4);
  trie.Insert("oneflewover", 5);
  trie.Insert("oneflewoverthe", 6);
  trie.Insert("twoiscompany", 7);

  // Make sure trie traversal works as expected.
  TestIntTraverser traverser;
  trie.PreorderTraverse(&traverser);
  EXPECT_THAT(traverser.values, ElementsAre(1, 4, 5, 6, 3, 2, 7));

  traverser = TestIntTraverser();
  trie.PostorderTraverse(&traverser);
  EXPECT_THAT(traverser.values, ElementsAre(6, 5, 4, 1, 3, 7, 2));

  traverser = TestIntTraverser();
  trie.PreorderTraverseAllMatchingStrings("onefl", &traverser);
  EXPECT_THAT(traverser.values, ElementsAre(4, 5, 6));

  traverser = TestIntTraverser();
  trie.PostorderTraverseAllMatchingStrings("onefl", strlen("onefl"),
                                           &traverser);
  EXPECT_THAT(traverser.values, ElementsAre(6, 5, 4));

  EXPECT_EQ(trie.GetData("one"), 1);
  EXPECT_EQ(trie.GetData("two"), 2);
  EXPECT_EQ(trie.GetData("three"), 3);
  EXPECT_EQ(trie.GetData("oneflew"), 4);
  EXPECT_EQ(trie.GetData("oneflewover"), 5);
  EXPECT_EQ(trie.GetData("oneflewoverthe"), 6);
  EXPECT_EQ(trie.GetData("twoiscompany"), 7);
  // test non existent entry
  EXPECT_EQ(trie.GetData("foo"), 0);

  // Test GetData on const trie.
  EXPECT_EQ(const_trie.GetData("one"), 1);
  EXPECT_EQ(const_trie.GetData("foo"), 0);

  int& one_data = trie.GetData("one");
  one_data--;
  EXPECT_EQ(trie.GetData("one"), 0);
  one_data++;

  int chars_matched = -1;
  EXPECT_EQ(trie.GetDataForMaximalPrefix("one", &chars_matched, nullptr), 1);
  EXPECT_EQ(chars_matched, 3);
  bool is_terminators[256];
  is_terminators['b'] = true;
  EXPECT_EQ(
      trie.GetDataForMaximalPrefix("oneby", &chars_matched, is_terminators), 1);
  EXPECT_EQ(chars_matched, 3);
  std::vector<std::pair<std::string, int> > outdata;
  trie.GetAllMatchingStrings("onefl", &outdata);
  EXPECT_THAT(outdata, ElementsAre(Pair("oneflew", 4), Pair("oneflewover", 5),
                                   Pair("oneflewoverthe", 6)));

  trie.GetAllMatchingStrings("two", &outdata);
  EXPECT_THAT(outdata, ElementsAre(Pair("two", 2), Pair("twoiscompany", 7)));

  trie.GetAllMatchingStrings("three", &outdata);
  EXPECT_THAT(outdata, ElementsAre(Pair("three", 3)));

  trie.GetAllMatchingStrings("foo", &outdata);
  EXPECT_THAT(outdata, IsEmpty());

  // Test with a string_view that is not null-terminated.
  trie.SetData(absl::string_view("twoiscompany").substr(0, 3), -2);
  trie.SetData("twoiscompany", -7);
  // test non-existent entry
  EXPECT_FALSE(trie.SetData("foo", 1));

  EXPECT_EQ(trie.GetData("two"), -2);
  EXPECT_EQ(trie.GetData("twoiscompany"), -7);

  trie.GetAllMatchingStrings("two", &outdata);
  EXPECT_THAT(outdata, ElementsAre(Pair("two", -2), Pair("twoiscompany", -7)));
}

TEST(GeneralTrie, TestTraverseAlongString) {
  GeneralTrie<int, 0> trie;

  trie.Insert("one", 1);
  trie.Insert("two", 2);
  trie.Insert("three", 3);
  trie.Insert("oneflew", 4);
  trie.Insert("oneflewover", 5);
  trie.Insert("oneflewoverthe", 6);
  trie.Insert("twoiscompany", 7);

  {
    TestIntTraverser traverser;
    trie.TraverseAlongString("oneflewoverfoobar", &traverser);
    EXPECT_THAT(traverser.values, ElementsAre(1, 4, 5));
  }

  {
    TestIntTraverser traverser;
    std::string key = "oneflewoverfoobar";
    trie.TraverseAlongString(key, &traverser);
    EXPECT_THAT(traverser.values, ElementsAre(1, 4, 5));
  }

  {
    TestIntTraverser traverser;
    trie.TraverseAlongString("oneFlewoverfoobar", &traverser);
    EXPECT_THAT(traverser.values, ElementsAre(1));
  }

  {
    TestIntTraverser traverser;
    // Simulates a string_view that is not null-terminated.
    char raw[] = {'o', 'n', 'e'};
    absl::string_view key(raw, 3);
    // Ensure that string_view is handled correctly.
    trie.TraverseAlongString(key, &traverser);
    EXPECT_THAT(traverser.values, ElementsAre(1));
  }
}

TEST(GeneralTrie, TraverseIterator) {
  GeneralTrie<int, 0> trie;
  GeneralTrie<int, 0>::TraverseIterator iter = trie.Traverse();
  EXPECT_TRUE(iter.Done());

  trie.Insert("one", 1);
  trie.Insert("two", 2);
  trie.Insert("three", 3);
  trie.Insert("oneflew", 4);
  trie.Insert("oneflewover", 5);
  trie.Insert("oneflewoverthe", 6);
  trie.Insert("twoiscompany", 7);

  std::vector<std::pair<std::string, int> > expected = {
    {"one", 1},
    {"oneflew", 4},
    {"oneflewover", 5},
    {"oneflewoverthe", 6},
    {"three", 3},
    {"two", 2},
    {"twoiscompany", 7},
  };

  int i = 0;
  for (GeneralTrie<int, 0>::TraverseIterator iter = trie.Traverse();
       !iter.Done(); iter.Next()) {
    EXPECT_THAT(expected[i], Pair(iter.Key(), iter.Value()));
    i++;
  }
  EXPECT_EQ(7, i);

  // Test a trie that has data at the root.
  GeneralTrie<int, 0> root_data_trie;
  root_data_trie.Insert("", 1234);

  iter = root_data_trie.Traverse();
  ASSERT_FALSE(iter.Done());
  EXPECT_EQ("", iter.Key());
  EXPECT_EQ(1234, iter.Value());
  iter.Next();
  EXPECT_TRUE(iter.Done());

  // Insert another item, and iterate through again.
  root_data_trie.Insert("a", 5678);
  iter = root_data_trie.Traverse();
  ASSERT_FALSE(iter.Done());
  EXPECT_EQ("", iter.Key());
  EXPECT_EQ(1234, iter.Value());
  iter.Next();
  ASSERT_FALSE(iter.Done());
  EXPECT_EQ("a", iter.Key());
  EXPECT_EQ(5678, iter.Value());
  iter.Next();
  EXPECT_TRUE(iter.Done());
}

TEST(GeneralTrie, TestCompression) {
  GeneralTrie<int, 0> trie;

  trie.Insert("abcde", 2);
  // Inserting a smaller key will correctly break up the key compression.
  trie.Insert("abcd", 1);
  EXPECT_EQ(trie.GetData("abcd"), 1);
  EXPECT_EQ(trie.GetData("abcde"), 2);
  // Open box testing: this ensures that we don't accidentally read past the key
  // when it matches a compressed path in the trie.
  EXPECT_EQ(trie.GetData(absl::string_view("abcd", 3)), 0);

  TestIntTraverser traverser;
  // Open box testing: this ensures that we don't read past the key when it
  // matches a compressed path in the trie.
  trie.TraverseAlongString(absl::string_view("abcde", 3), &traverser);
  EXPECT_THAT(traverser.values, IsEmpty());

  // Open box testing: this ensures that we don't read past the key when it
  // matches a compressed path in the trie.
  int matched = -1;
  EXPECT_EQ(trie.GetDataForMaximalPrefix(absl::string_view("abcd", 3), &matched,
                                         nullptr),
            0);
  EXPECT_EQ(matched, -1);
}

//
// Test the legacy idiom used to have class type values in GeneralTrie. This
// idiom goes against our coding guidelines because it demands a global
// variable of class type, and new code should use ClassGeneralTrie instead.
//
// Notice that initialization is as complex as it looks like. Any of the
// following changes will result in code that does not compile:
// * declare kNullValueClass outside an enclosing class
// * make kNullValueClass const
// * pass a null pointer as the second argument for the typedef
// * pass static_cast<DummyValueClass*>(nullptr) as the second argument for the
// typedef (http://gcc.gnu.org/bugzilla/show_bug.cgi?id=10541)
class DummyValueClass {
 public:
};

class DummyAppClass {
 public:
  static DummyValueClass kNullValueClass;
  typedef GeneralTrie<DummyValueClass*, &kNullValueClass> ValueClassTrie;
};
DummyValueClass DummyAppClass::kNullValueClass = DummyValueClass();

TEST(GeneralTrie, LegacyGeneralTrieWithClassValue) {
  DummyAppClass::ValueClassTrie trie_instance;
}

}  // namespace
}  // namespace googlesql_base
