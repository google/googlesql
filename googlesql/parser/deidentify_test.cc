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

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/ast_node_kind.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace parser {
namespace {

using testing::FieldsAre;
using testing::Pair;
using testing::UnorderedElementsAre;
using absl_testing::IsOkAndHolds;

TEST(Deidentify, SimpleExample) {
  EXPECT_THAT(DeidentifySQLIdentifiersAndLiterals(
                  "SELECT X + 1232, USER_FUNCTION(X) FROM business.financial"),
              IsOkAndHolds(
                  R"(SELECT
  A + 0,
  B(A)
FROM
  C.D
)"));
}

TEST(Deidentify, AsExample) {
  EXPECT_THAT(DeidentifySQLIdentifiersAndLiterals(
                  "SELECT X AS private_name FROM business.financial"),
              IsOkAndHolds(
                  R"(SELECT
  A AS B
FROM
  C.D
)"));
}

TEST(Deidentify, BigExample) {
  EXPECT_THAT(
      DeidentifySQLIdentifiersAndLiterals(
          "SELECT "
          "a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,"
          "a20,a21,a22,a23,a24,a25,a26,a27,a28,a29,a30,a31,a32,a33,a34,a35,a36,"
          "a37,a38,a39,a40,a41,a42,a43,a44,a45,a46,a47,a48,a49,a50,a51,a52,a53,"
          "a54,a55,a56,a57,a58,a59,a60,a61,a62,a63,a64,a65,a66,a67,a68,a69,a70,"
          "a71,a72,a73,a74,a75,a76,a77,a78,a79,a80,a81,a82,a83,a84,a85,a86,a87,"
          "a88,a89,a90,a91,a92,a93,a94,a95,a96,a97,a98,a99,a100"),
      absl_testing::IsOkAndHolds(
          R"(SELECT
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  AA,
  AB,
  AC,
  AD,
  AE,
  AF,
  AG,
  AH,
  AI,
  AJ,
  AK,
  AL,
  AM,
  AN,
  AO,
  AP,
  AQ,
  AR,
  AS_,
  AT_,
  AU,
  AV,
  AW,
  AX,
  AY,
  AZ,
  BA,
  BB,
  BC,
  BD,
  BE,
  BF,
  BG,
  BH,
  BI,
  BJ,
  BK,
  BL,
  BM,
  BN,
  BO,
  BP,
  BQ,
  BR,
  BS,
  BT,
  BU,
  BV,
  BW,
  BX,
  BY_,
  BZ,
  CA,
  CB,
  CC,
  CD,
  CE,
  CF,
  CG,
  CH,
  CI,
  CJ,
  CK,
  CL,
  CM,
  CN,
  CO,
  CP,
  CQ,
  CR,
  CS,
  CT,
  CU,
  CV
)"));
}

TEST(Deidentify, NumericsExample) {
  EXPECT_THAT(DeidentifySQLIdentifiersAndLiterals(
                  "SELECT 123, NUMERIC '123', BIGNUMERIC '123.456'"),
              IsOkAndHolds(
                  R"(SELECT
  0,
  NUMERIC "0",
  BIGNUMERIC "0"
)"));
}

TEST(DeidentifyWithMapping, SimpleExample) {
  EXPECT_THAT(
      DeidentifySQLWithMapping(
          "SELECT X + 1232, USER_FUNCTION(X) FROM business.financial", {},
          {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS}),
      IsOkAndHolds(FieldsAre(
          UnorderedElementsAre(Pair("X", "A"), Pair("USER_FUNCTION", "B"),
                               Pair("business", "C"), Pair("financial", "D")),
          R"(SELECT
  A + 1232,
  B(A)
FROM
  C.D
)")));
}

TEST(DeidentifyWithMapping, SimpleExampleWithIntLiteral) {
  EXPECT_THAT(
      DeidentifySQLWithMapping(
          "SELECT X + 1232, USER_FUNCTION(X) FROM business.financial", {},
          {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS,
           ASTNodeKind::AST_INT_LITERAL}),
      IsOkAndHolds(FieldsAre(
          UnorderedElementsAre(Pair("X", "A"), Pair("1232", "B"),
                               Pair("USER_FUNCTION", "C"),
                               Pair("business", "D"), Pair("financial", "E")),
          R"(SELECT
  A + B,
  C(A)
FROM
  D.E
)")));
}

TEST(DeidentifyWithMapping, SimpleExampleWithLiterals) {
  EXPECT_THAT(
      DeidentifySQLWithMapping(
          "SELECT X,Y,Z FROM business.financial WHERE X=2.3 AND Y=\"FOO\" AND "
          "Z=NUMERIC '-9.876e-3'",
          {},
          {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS,
           ASTNodeKind::AST_INT_LITERAL, ASTNodeKind::AST_STRING_LITERAL,
           ASTNodeKind::AST_NUMERIC_LITERAL, ASTNodeKind::AST_FLOAT_LITERAL}),
      IsOkAndHolds(FieldsAre(
          UnorderedElementsAre(Pair("X", "A"), Pair("Y", "B"), Pair("Z", "C"),
                               Pair("business", "D"), Pair("financial", "E"),
                               Pair("2.3", "F"), Pair("\"FOO\"", "G"),
                               Pair("NUMERIC '-9.876e-3'", "H")),
          R"(SELECT
  A,
  B,
  C
FROM
  D.E
WHERE
  A = F AND B = G AND C = H
)")));
}

TEST(DeidentifyWithMapping, SimpleExampleWithComplexLiterals) {
  EXPECT_THAT(
      DeidentifySQLWithMapping(
          "SELECT X,Y,Z FROM business.financial WHERE X=DATETIME '2014-09-27 "
          "12:30:00.45' AND Y=BIGNUMERIC '12345e123' AND Z=JSON '{\"id\": 10}'",
          {},
          {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS,
           ASTNodeKind::AST_DATE_OR_TIME_LITERAL,
           ASTNodeKind::AST_BIGNUMERIC_LITERAL, ASTNodeKind::AST_JSON_LITERAL}),
      IsOkAndHolds(FieldsAre(
          UnorderedElementsAre(Pair("X", "A"), Pair("Y", "B"), Pair("Z", "C"),
                               Pair("business", "D"), Pair("financial", "E"),
                               Pair("DATETIME '2014-09-27 12:30:00.45'", "F"),
                               Pair("BIGNUMERIC '12345e123'", "G"),
                               Pair("JSON '{\"id\": 10}'", "H")),
          R"(SELECT
  A,
  B,
  C
FROM
  D.E
WHERE
  A = F AND B = G AND C = H
)")));
}

TEST(DeidentifyWithMapping, BigExample) {
  EXPECT_THAT(
      DeidentifySQLWithMapping(
          "SELECT "
          "a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,"
          "a20,a21,a22,a23,a24,a25,a26,a27,a28,a29,a30,a31,a32,a33,a34,a35,a36,"
          "a37,a38,a39,a40,a41,a42,a43,a44,a45,a46,a47,a48,a49,a50,a51,a52,a53,"
          "a54,a55,a56,a57,a58,a59,a60,a61,a62,a63,a64,a65,a66,a67,a68,a69,a70,"
          "a71,a72,a73,a74,a75,a76,a77,a78,a79,a80,a81,a82,a83,a84,a85,a86,a87,"
          "a88,a89,a90,a91,a92,a93,a94,a95,a96,a97,a98,a99,a100",
          {}, {ASTNodeKind::AST_IDENTIFIER, ASTNodeKind::AST_ALIAS}),
      IsOkAndHolds(
          FieldsAre(UnorderedElementsAre(
                        Pair("a11", "K"), Pair("a80", "CB"), Pair("a82", "CD"),
                        Pair("a8", "H"), Pair("a26", "Z"), Pair("a60", "BH"),
                        Pair("a7", "G"), Pair("a37", "AK"), Pair("a9", "I"),
                        Pair("a72", "BT"), Pair("a51", "AY"), Pair("a30", "AD"),
                        Pair("a40", "AN"), Pair("a33", "AG"), Pair("a57", "BE"),
                        Pair("a75", "BW"), Pair("a53", "BA"), Pair("a54", "BB"),
                        Pair("a31", "AE"), Pair("a27", "AA"), Pair("a36", "AJ"),
                        Pair("a79", "CA"), Pair("a43", "AQ"), Pair("a67", "BO"),
                        Pair("a63", "BK"), Pair("a45", "AS_"),
                        Pair("a62", "BJ"), Pair("a3", "C"), Pair("a2", "B"),
                        Pair("a97", "CS"), Pair("a76", "BX"), Pair("a23", "W"),
                        Pair("a10", "J"), Pair("a78", "BZ"), Pair("a91", "CM"),
                        Pair("a74", "BV"), Pair("a21", "U"), Pair("a18", "R"),
                        Pair("a87", "CI"), Pair("a16", "P"), Pair("a55", "BC"),
                        Pair("a90", "CL"), Pair("a68", "BP"), Pair("a44", "AR"),
                        Pair("a52", "AZ"), Pair("a49", "AW"), Pair("a70", "BR"),
                        Pair("a73", "BU"), Pair("a29", "AC"), Pair("a19", "S"),
                        Pair("a47", "AU"), Pair("a28", "AB"), Pair("a14", "N"),
                        Pair("a69", "BQ"), Pair("a50", "AX"), Pair("a95", "CQ"),
                        Pair("a5", "E"), Pair("a77", "BY_"), Pair("a39", "AM"),
                        Pair("a1", "A"), Pair("a85", "CG"), Pair("a15", "O"),
                        Pair("a92", "CN"), Pair("a93", "CO"), Pair("a66", "BN"),
                        Pair("a38", "AL"), Pair("a58", "BF"), Pair("a12", "L"),
                        Pair("a13", "M"), Pair("a65", "BM"), Pair("a59", "BG"),
                        Pair("a4", "D"), Pair("a46", "AT_"), Pair("a34", "AH"),
                        Pair("a48", "AV"), Pair("a71", "BS"), Pair("a32", "AF"),
                        Pair("a99", "CU"), Pair("a24", "X"), Pair("a100", "CV"),
                        Pair("a56", "BD"), Pair("a17", "Q"), Pair("a81", "CC"),
                        Pair("a94", "CP"), Pair("a35", "AI"), Pair("a41", "AO"),
                        Pair("a22", "V"), Pair("a25", "Y"), Pair("a96", "CR"),
                        Pair("a6", "F"), Pair("a88", "CJ"), Pair("a89", "CK"),
                        Pair("a61", "BI"), Pair("a86", "CH"), Pair("a98", "CT"),
                        Pair("a20", "T"), Pair("a42", "AP"), Pair("a64", "BL"),
                        Pair("a83", "CE"), Pair("a84", "CF")),
                    R"(SELECT
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  AA,
  AB,
  AC,
  AD,
  AE,
  AF,
  AG,
  AH,
  AI,
  AJ,
  AK,
  AL,
  AM,
  AN,
  AO,
  AP,
  AQ,
  AR,
  AS_,
  AT_,
  AU,
  AV,
  AW,
  AX,
  AY,
  AZ,
  BA,
  BB,
  BC,
  BD,
  BE,
  BF,
  BG,
  BH,
  BI,
  BJ,
  BK,
  BL,
  BM,
  BN,
  BO,
  BP,
  BQ,
  BR,
  BS,
  BT,
  BU,
  BV,
  BW,
  BX,
  BY_,
  BZ,
  CA,
  CB,
  CC,
  CD,
  CE,
  CF,
  CG,
  CH,
  CI,
  CJ,
  CK,
  CL,
  CM,
  CN,
  CO,
  CP,
  CQ,
  CR,
  CS,
  CT,
  CU,
  CV
)")));
}

}  // namespace
}  // namespace parser
}  // namespace googlesql
