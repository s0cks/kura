#ifndef KURA_DOCS_INDEX_H
#define KURA_DOCS_INDEX_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <zstd.h>

namespace kura::docs {

struct DocIndexEntry {
  std::string_view code;
  std::string_view title;
  uint32_t offset;
  uint32_t length;
};

namespace detail {
const std::span<const uint8_t> compressed_blob();
size_t uncompressed_total_size();
const std::span<const DocIndexEntry> doc_index();
}  // namespace detail

// Holds the decompressed buffer alive while the string_view is used
struct DecompressedDoc {
  std::string storage;
  std::string_view text;
};

static inline std::optional<DecompressedDoc> load_explanation(std::string_view code) {
  auto index = detail::doc_index();

  auto it = std::lower_bound(index.begin(), index.end(), code, [](const DocIndexEntry& entry, std::string_view target) {
    return entry.code < target;
  });

  if (it == index.end() || it->code != code) {
    return std::nullopt;
  }

  auto blob = detail::compressed_blob();
  const size_t total_size = detail::uncompressed_total_size();

  DecompressedDoc result;
  result.storage.resize(total_size);

  const size_t rc = ZSTD_decompress(result.storage.data(), total_size, blob.data(), blob.size());

  if (ZSTD_isError(rc)) {
    return std::nullopt;
  }

  result.text = std::string_view(result.storage).substr(it->offset, it->length);
  return result;
}

}  // namespace kura::docs

#endif  // KURA_DOCS_INDEX_H
