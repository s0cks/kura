#ifndef KURA_DOCS_SUGGEST_H
#define KURA_DOCS_SUGGEST_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <ostream>
#include <rang.hpp>
#include <span>
#include <string_view>

#include "docs/index.h"

namespace kura::docs {

inline char ascii_lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline size_t levenshtein_icase(std::string_view a, std::string_view b) {
  if (a.size() < b.size()) {
    std::swap(a, b);
  }

  constexpr size_t kMaxCodeLen = 63;
  if (b.size() > kMaxCodeLen) {
    return a.size();
  }

  std::array<size_t, kMaxCodeLen + 1> row{};
  for (size_t j = 0; j <= b.size(); ++j) {
    row[j] = j;
  }

  for (size_t i = 1; i <= a.size(); ++i) {
    size_t prev_diag = row[0];
    row[0] = i;
    const char ca = ascii_lower(a[i - 1]);

    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t temp = row[j];
      const size_t cost = (ca == ascii_lower(b[j - 1])) ? 0 : 1;
      row[j] = std::min({
          row[j] + 1,       // deletion
          row[j - 1] + 1,   // insertion
          prev_diag + cost  // substitution
      });
      prev_diag = temp;
    }
  }

  return row[b.size()];
}

template <size_t MaxSuggestions = 3>
struct SuggestionList {
  std::array<std::string_view, MaxSuggestions> items{};
  size_t count = 0;

  bool empty() const {
    return count == 0;
  }
  std::span<const std::string_view> span() const {
    return {items.data(), count};
  }
};

template <size_t MaxSuggestions = 3>
inline SuggestionList<MaxSuggestions> suggest_error_codes(std::string_view query,
                                                          std::span<const DocIndexEntry> index = detail::doc_index()) {
  SuggestionList<MaxSuggestions> result{};
  if (query.empty() || index.empty() || MaxSuggestions == 0) {
    return result;
  }

  const size_t max_dist = (query.size() <= 3) ? 1 : 2;
  size_t best_dist = max_dist + 1;

  for (const auto& entry : index) {
    const size_t len_diff =
        (entry.code.size() > query.size()) ? (entry.code.size() - query.size()) : (query.size() - entry.code.size());
    if (len_diff > best_dist) {
      continue;
    }

    const size_t dist = levenshtein_icase(query, entry.code);
    if (dist < best_dist) {
      // Found a strictly closer match: reset tied list
      best_dist = dist;
      result.items[0] = entry.code;
      result.count = 1;
    } else if (dist == best_dist && result.count < MaxSuggestions) {
      // Tied with current best distance: append up to MaxSuggestions
      result.items[result.count++] = entry.code;
    }
  }

  return result;
}

inline void print_suggestions(std::ostream& err, std::span<const std::string_view> suggestions) {
  if (suggestions.empty()) {
    return;
  }

  err << "  " << rang::style::bold << rang::fg::cyan << "help" << rang::style::reset << rang::fg::reset
      << ": did you mean ";

  for (size_t i = 0; i < suggestions.size(); ++i) {
    if (i > 0) {
      if (suggestions.size() == 2) {
        err << " or ";
      } else if (i == suggestions.size() - 1) {
        err << ", or ";
      } else {
        err << ", ";
      }
    }
    err << '`' << rang::style::bold << suggestions[i] << rang::style::reset << '`';
  }

  err << "?\n";
}

}  // namespace kura::docs

#endif  // KURA_DOCS_SUGGEST_H
