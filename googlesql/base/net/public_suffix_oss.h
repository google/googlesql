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

// An implementation of the public suffix algorithm and data documented at
// http://publicsuffix.org. A public suffix is a domain under which users and
// organizations can directly register names. For example, "com" and "co.uk"
// are public suffixes.
//

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_OSS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_OSS_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql::internal {

// Extract the public suffix from the given name, using the latest public
// suffix data. Returns the entire string if it is a public suffix. Returns the
// empty string when the name does not contain a public suffix. Examples:
//   "www.berkeley.edu"           -> "edu"
//   "www.clavering.essex.sch.uk" -> "essex.sch.uk"
//   "sch.uk"                     -> ""
// The name must be normalized (lower-cased, Punycoded) before calling this
// method. Use googlesql::internal::ToASCII to normalize the name. The returned
// absl::string_view will live as long as the input absl::string_view lives.
absl::string_view GetPublicSuffix(absl::string_view name);

// Extract the top private domain from the given name, using the latest public
// suffix data. Returns the entire string if it is a top private domain.
// Returns the empty string when the name does not contain a top private
// domain. Examples:
//   "www.google.com"   -> "google.com"
//   "www.google.co.uk" -> "google.co.uk"
//   "co.uk"            -> ""
// The name must be normalized (lower-cased, Punycoded) before calling this
// method. Use googlesql::internal::ToASCII to normalize the name. The returned
// absl::string_view will live as long as the input absl::string_view lives.
absl::string_view GetTopPrivateDomain(absl::string_view name);

// INTERNAL IMPLEMENTATION DETAILS
//

class PublicSuffixRules {
 public:
  enum DomainType {
    kIcannDomain,   // domain marked ICANN at publicsuffix.org
    kPrivateDomain  // domain marked PRIVATE at publicsuffix.org
  };

  // Return a pointer to the latest public suffix rules.
  static const PublicSuffixRules& GetLatest();

  // Extract the public suffix from the given name, using the current
  // rules. See GetPublicSuffix above for examples and usage.
  absl::string_view GetPublicSuffix(absl::string_view name) const;

  // Extract the top private domain from the given name, using the current
  // rules. See GetTopPrivateDomain above for examples and usage.
  absl::string_view GetTopPrivateDomain(absl::string_view name) const;

  // Return a reference to the set of ICANN domain rules.
  const absl::flat_hash_set<std::string>& icann_domains() const {
    return icann_domains_;
  }

  // Return a reference to the set of private domain rules.
  const absl::flat_hash_set<std::string>& private_domains() const {
    return private_domains_;
  }

  bool IsEmpty() const {
    return icann_domains_.empty() && private_domains_.empty();
  }

  size_t static_memory_size() const { return static_memory_size_; }
  void set_static_memory_size(size_t size) { static_memory_size_ = size; }

 private:
  friend class PublicSuffixRulesBuilder;

  struct Node {
    ~Node() {
      for (const auto& entry : map) {
        delete entry.second;
      }
    }

    int rules = 0;
    absl::flat_hash_map<absl::string_view, Node*> map;
  };

  struct Match {
    Match(bool m, const char* l) : match(m), label(l) {}

    bool match;
    const char* label;
  };

  PublicSuffixRules() {}

  // MatchVector can hold a reasonable number of domain labels locally.
  using MatchVector = absl::InlinedVector<Match, 6>;

  void GetMatchingRules(absl::string_view name, MatchVector* matches) const;

  static void DoPublicSuffix(const MatchVector& matches, const char** label);
  static void DoTopPrivate(const MatchVector& matches, const char** label);

  absl::string_view GetPublicSuffixOrTopPrivateDomain(
      absl::string_view name,
      absl::FunctionRef<void(const MatchVector&, const char**)> find_match)
      const;

  absl::flat_hash_set<std::string> icann_domains_;
  absl::flat_hash_set<std::string> private_domains_;

  Node root_;

  size_t static_memory_size_;
};

struct PublicSuffixType {
  // These are ORed to form the integers stored in the data structure.
  enum {
    kNormal = 1,     // foo.bar
    kWildcard = 2,   // *.foo.bar
    kException = 4,  // !foo.bar
    kPrivate = 8,    // PRIVATE DOMAIN vs ICANN DOMAIN
  };
};

class PublicSuffixRulesBuilder {
 public:
  PublicSuffixRulesBuilder() : rules_(new PublicSuffixRules) {}

  // Add a set of public suffix rules. They may contain newlines and comments.
  // This method may be called more than once.
  PublicSuffixRulesBuilder& AddRules(absl::string_view rules);

  // Add a single public suffix rule. It may not contain newlines or comments.
  // This method may be called more than once. The type argument indicates
  // whether the rule is in the PRIVATE or ICANN section of the file.
  PublicSuffixRulesBuilder& AddRule(absl::string_view rule,
                                    PublicSuffixRules::DomainType type);

  // Process a set of changes for the public suffix rules. A line with a '+' at
  // the beginning is an additional rule, while a line with '-' is a removal.
  // This method may be called more than once.
  PublicSuffixRulesBuilder& ProcessChanges(absl::string_view changes);

  // When all rules have been added and all changes have been processed, this
  // method must be called to build the PublicSuffixRules. The caller takes
  // ownership of the returned object.
  PublicSuffixRules* Build();

  void increment_static_memory_counter(size_t size) {
    static_memory_size_ += size;
  }

 private:
  void ProcessRulesOrChanges(absl::string_view rules);

  void ProcessRules(const absl::flat_hash_set<std::string>& rules,
                    PublicSuffixRules::DomainType type);

  std::unique_ptr<PublicSuffixRules> rules_;

  size_t static_memory_size_ = 0;
};

}  // namespace googlesql::internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_OSS_H_
