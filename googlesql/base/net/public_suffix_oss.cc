//
// Copyright 2020 Google LLC
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

#include "googlesql/base/net/public_suffix_oss.h"

#include "absl/functional/function_ref.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/net/public_suffix_list_data.h"
#include "unicode/errorcode.h"
#include "unicode/idna.h"

namespace googlesql::internal {
namespace {

const icu::IDNA* CreateIdnaOrDie() {
  icu::ErrorCode error;
  const icu::IDNA* idna =
      icu::IDNA::createUTS46Instance(UIDNA_NONTRANSITIONAL_TO_ASCII, error);
  ABSL_CHECK(error.isSuccess()) << error.errorName();
  return idna;
}

// Convert from non-ASCII name to the ASCII xn--... Punycode encoding.
bool ConvertToAsciiAndAppend(absl::string_view utf8, std::string* ascii) {
  static const icu::IDNA* idna = CreateIdnaOrDie();

  std::string tmp;
  icu::StringByteSink<std::string> sink(&tmp);
  icu::IDNAInfo info;
  icu::ErrorCode error;
  idna->nameToASCII_UTF8(utf8, sink, info, error);

  if (error.isFailure() || info.hasErrors()) {
    ABSL_LOG(WARNING) << "ToASCII error: " << error.errorName()
                 << ", error bits: " << absl::PrintF("0x%X", info.getErrors())
                 << ", input: " << utf8;
    error.reset();
    return false;
  }

  ascii->append(tmp);
  return true;
}

std::string NormalizeRule(absl::string_view rule) {
  // If there is a ! at the beginning, do not pass it to ToASCII.
  std::string ascii;
  if (rule[0] == '!') {
    rule.remove_prefix(1);
    ascii = "!";
  }

  if (!ConvertToAsciiAndAppend(rule, &ascii)) {
    ascii.clear();
  }

  if (!ascii.empty() && ascii.back() == '.') {
    ascii.resize(ascii.size() - 1);
  }

  return ascii;
}

const PublicSuffixRules* InitLatestRules() {
  PublicSuffixRulesBuilder builder;
  builder.AddRules(googlesql::internal::kPublicSuffixListData);
  return builder.Build();
}

}  // namespace

PublicSuffixRulesBuilder& PublicSuffixRulesBuilder::AddRule(
    absl::string_view rule, PublicSuffixRules::DomainType domain_type) {
  std::string norm = NormalizeRule(rule);
  if (norm.empty()) {
    ABSL_LOG(WARNING) << "bad rule " << rule;
    return *this;
  }

  if (domain_type == PublicSuffixRules::kIcannDomain) {
    rules_->icann_domains_.insert(norm);
  } else {
    if (norm.find('.') == norm.npos) {
      ABSL_LOG(WARNING) << "must have dot in PRIVATE domain " << norm;
    }
    rules_->private_domains_.insert(norm);
  }

  return *this;
}

void PublicSuffixRulesBuilder::ProcessRulesOrChanges(absl::string_view rules) {
  const char* begin = rules.data();
  const char* end = begin + rules.size();
  const char* p = begin;
  PublicSuffixRules::DomainType domain_type = PublicSuffixRules::kIcannDomain;

  while (p < end) {
    // Read up to the first space, slash (comment) or
    // control character (including CR,LF,TAB).
    begin = p;
    while (p < end && *p != ' ' && *p != '/' && !absl::ascii_iscntrl(*p)) {
      p++;
    }
    absl::string_view rule(begin, p - begin);

    if (!rule.empty()) {
      AddRule(rule, domain_type);
    }

    // Read up to the newline characters (CR,LF).
    while (p < end && *p != '\n' && *p != '\r') {
      p++;
    }

    absl::string_view line(begin, p - begin);
    if (absl::StrContains(line, "BEGIN PRIVATE DOMAINS")) {
      domain_type = PublicSuffixRules::kPrivateDomain;
    } else if (absl::StrContains(line, "END PRIVATE DOMAINS")) {
      domain_type = PublicSuffixRules::kIcannDomain;
    }

    // Read past newline characters (CR,LF).
    while (p < end && (*p == '\n' || *p == '\r')) {
      p++;
    }
  }
}

PublicSuffixRulesBuilder& PublicSuffixRulesBuilder::AddRules(
    absl::string_view rules) {
  ProcessRulesOrChanges(rules);
  return *this;
}

// static
const PublicSuffixRules& PublicSuffixRules::GetLatest() {
  static const PublicSuffixRules* latest_rules = InitLatestRules();
  return *latest_rules;
}
void PublicSuffixRulesBuilder::ProcessRules(
    const absl::flat_hash_set<std::string>& rules,
    PublicSuffixRules::DomainType domain_type) {
  for (absl::string_view rule : rules) {
    size_t dot = rule.size();
    size_t prev;
    std::vector<absl::string_view> labels;
    while (dot != absl::string_view::npos) {
      prev = dot;
      dot = rule.rfind('.', dot - 1);
      if (dot == absl::string_view::npos) {
        labels.push_back(rule.substr(0, prev));
      } else {
        labels.push_back(absl::ClippedSubstr(rule, dot + 1, prev - dot - 1));
      }
    }

    int new_flags = PublicSuffixType::kNormal;
    if (labels.back() == "*") {
      new_flags = PublicSuffixType::kWildcard;
      labels.pop_back();
    } else if (absl::StartsWith(labels.back(), "!")) {
      new_flags = PublicSuffixType::kException;
      labels.back().remove_prefix(1);
    }
    if (domain_type == PublicSuffixRules::kPrivateDomain) {
      new_flags |= PublicSuffixType::kPrivate;
    }

    PublicSuffixRules::Node* node = &rules_->root_;
    for (size_t i = 0; i < labels.size(); ++i) {
      auto found = node->map.find(labels[i]);
      if (found == node->map.end()) {
        node = node->map.insert({labels[i], new PublicSuffixRules::Node})
                   .first->second;
      } else {
        node = found->second;
      }
    }

    int* stored_flags = &node->rules;
    if (*stored_flags == 0) {
      *stored_flags = new_flags;
      continue;
    }

    // It is not OK to have "foo.bar" and "!foo.bar".
    if ((*stored_flags & PublicSuffixType::kException) !=
        (new_flags & PublicSuffixType::kException)) {
      ABSL_LOG(ERROR) << "inconsistent exception " << rule;
      *stored_flags &= ~PublicSuffixType::kException;
    }

    // It is not OK to have rules for a given domain in both the ICANN and
    // PRIVATE sections.
    if ((*stored_flags & PublicSuffixType::kPrivate) !=
        (new_flags & PublicSuffixType::kPrivate)) {
      ABSL_LOG(ERROR) << "ICANN/PRIVATE conflict " << rule;
      *stored_flags &= ~PublicSuffixType::kPrivate;
    }

    // It is OK to have "foo" and "*.foo". Just merge the values in.
    *stored_flags |=
        (new_flags & (PublicSuffixType::kNormal | PublicSuffixType::kWildcard));
  }
}

PublicSuffixRules* PublicSuffixRulesBuilder::Build() {
  int root_label = PublicSuffixType::kNormal;

  // This implements Algorithm step 2 from https://publicsuffix.org/list/ :
  //   'If no rules match, the prevailing rule is "*".'
  // Note that the include_unknown_domains option can be used to control
  // whether or not this rule is applied.
  root_label |= PublicSuffixType::kWildcard;

  rules_->root_.rules = root_label;

  ProcessRules(rules_->icann_domains_, PublicSuffixRules::kIcannDomain);
  ProcessRules(rules_->private_domains_, PublicSuffixRules::kPrivateDomain);

  rules_->set_static_memory_size(static_memory_size_);

  return rules_.release();
}

namespace {

absl::string_view GetNextLabel(absl::string_view name, ssize_t* prev_dot) {
  const char* begin = name.data();
  const char* prev = begin + *prev_dot;
  const char* curr = prev;
  do {
    curr--;
  } while (curr >= begin && *curr != '.');
  *prev_dot = curr++ - begin;
  return absl::string_view(curr, prev - curr);
}

}  // namespace

void PublicSuffixRules::GetMatchingRules(absl::string_view name,
                                         MatchVector* matches) const {
  // Start at the root.
  const Node* node = &root_;

  // Name may end with '.'
  if (name[name.size() - 1] == '.') {
    name.remove_suffix(1);
  }
  ssize_t dot = name.size();

  while (dot >= 0) {
    absl::string_view label = GetNextLabel(name, &dot);
    if (label.empty()) {
      return;
    }
    const Node* prev_node = node;
    auto found = node->map.find(label);

    if (found == node->map.end()) {
      // If we cannot find this label and the previous node is not a wildcard
      // (*.foo.bar), there is no match.
      if (!(prev_node->rules & PublicSuffixType::kWildcard)) {
        matches->emplace_back(false, label.data());
        return;
      }

      // If the previous node was a wildcard but it was the root and the caller
      // has asked us not to include unknown domains, just return so that we end
      // up with an empty top private domain and public suffix.
      if (prev_node == &root_) {
        return;
      }

      // If the previous node was a wildcard, this label has a match.
      matches->emplace_back(true, label.data());
      if (dot < 0) {
        return;
      }

      // But the next label has no match, and will be the top private domain.
      label = GetNextLabel(name, &dot);
      if (!label.empty()) {
        matches->emplace_back(false, label.data());
      }
      return;
    }

    node = found->second;
    int rules = node->rules;

    // If we hit a private domain and the caller wants to exclude private
    // domains, this label has no match and we ignore any deeper labels.
    if (rules & PublicSuffixType::kPrivate) {
      matches->emplace_back(false, label.data());
      return;
    }

    // Check whether we hit an exception (!foo.bar).
    if (rules & PublicSuffixType::kException) {
      if (prev_node->rules & PublicSuffixType::kWildcard) {
        // This is the normal case, as specified at publicsuffix.org.
        // E.g. "*.ck" and "!www.ck".
        matches->back().match = true;
      } else {
        // This is an extension of the publicsuffix.org spec. It allows us to
        // deal with cases like www.state.ca.us (public suffix is ca.us).
        // I.e. "!www.state.ca.us" and "state.ca.us". There is no '*'.
        matches->back().match = false;
      }
      // This label hit an exception, so there is no match.
      matches->emplace_back(false, label.data());
      return;
    }

    // This label has a match if there is a normal rule here or a wildcard at
    // the previous node (a.b.c or *.b.c).
    matches->emplace_back((rules & PublicSuffixType::kNormal) ||
                              (prev_node->rules & PublicSuffixType::kWildcard),
                          label.data());
  }
}

absl::string_view PublicSuffixRules::GetPublicSuffixOrTopPrivateDomain(
    absl::string_view name,
    absl::FunctionRef<void(const MatchVector&, const char**)> find_match)
    const {
  if (name.empty()) {
    return "";
  }

  MatchVector matches;
  matches.emplace_back(true, name.end());  // Implicit root always matches.
  GetMatchingRules(name, &matches);

  const char* label = name.end();
  find_match(matches, &label);
  return absl::string_view(label, name.size() - (label - name.data()));
}

void PublicSuffixRules::DoPublicSuffix(const MatchVector& matches,
                                       const char** label) {
  // Starting from the top of the stack, the first domain label with a match
  // is the public suffix.
  const Match* m = matches.data() + matches.size() - 1;
  while (!m->match) {
    m--;
  }
  *label = m->label;
}

void PublicSuffixRules::DoTopPrivate(const MatchVector& matches,
                                     const char** label) {
  // Starting from the top of the stack, look for the last domain label that
  // does not have a matching rule, if any. That is then the top private
  // domain. E.g. google.com.
  const Match* last_no_match = nullptr;
  const Match* m = matches.data() + matches.size() - 1;
  while (!m->match) {
    last_no_match = m--;
  }
  if (last_no_match) {
    *label = last_no_match->label;
  }
}

absl::string_view PublicSuffixRules::GetPublicSuffix(
    absl::string_view name) const {
  return GetPublicSuffixOrTopPrivateDomain(name, DoPublicSuffix);
}

absl::string_view PublicSuffixRules::GetTopPrivateDomain(
    absl::string_view name) const {
  return GetPublicSuffixOrTopPrivateDomain(name, DoTopPrivate);
}

absl::string_view GetPublicSuffix(absl::string_view name) {
  return PublicSuffixRules::GetLatest().GetPublicSuffix(name);
}

absl::string_view GetTopPrivateDomain(absl::string_view name) {
  return PublicSuffixRules::GetLatest().GetTopPrivateDomain(name);
}

}  // namespace googlesql::internal
