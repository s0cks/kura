import sys
import pathlib
import zstandard as zstd


def main():
    if len(sys.argv) != 3:
        sys.exit("Usage: pack_docs.py <errors_dir> <output_cpp>")

    errors_dir = pathlib.Path(sys.argv[1])
    out_cpp = pathlib.Path(sys.argv[2])

    dj_files = sorted(errors_dir.glob("*.dj"))

    raw_blob = bytearray()
    entries = []  # (code, offset, length)

    for path in dj_files:
        raw_bytes = path.read_bytes()
        try:
            text = raw_bytes.decode("utf-8")
            # lint_djot_file(path, text)
        except UnicodeDecodeError as e:
            # lint_errors.append(f"{path}:1: error: invalid UTF-8 ({e})")
            continue
        # except LintError as e:
        #     # lint_errors.append(str(e))
        #     continue

        # First line is guaranteed to be "# K0001: Title" by lint_djot_file
        first_line, _, _ = text.partition("\n")
        _, _, title_part = first_line.partition(":")
        title = title_part.strip()

        offset = len(raw_blob)
        raw_blob.extend(raw_bytes)
        entries.append((path.stem, title, offset, len(raw_bytes)))

    # Compress the combined blob at high compression (level 19)
    cctx = zstd.ZstdCompressor(level=19)
    compressed = cctx.compress(bytes(raw_blob))

    with out_cpp.open("w", encoding="utf-8") as f:
        f.write('#include "docs/index.h"\n')
        f.write("#include <array>\n")
        f.write("#include <cstdint>\n\n")
        f.write("namespace kura::docs::detail {\n\n")

        # 1. Compressed byte array
        f.write(f"alignas(8) const uint8_t kCompressedBlob[{len(compressed)}] = {{\n")
        for i in range(0, len(compressed), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in compressed[i : i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        # 2. Sorted index table
        f.write(f"const std::array<DocIndexEntry, {len(entries)}> kDocIndex = {{{{\n")
        for code, title, offset, length in entries:
            escaped_title = title.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'    {{"{code}", "{escaped_title}", {offset}u, {length}u}},\n')
        f.write("}};\n\n")

        f.write(
            "const std::span<const uint8_t> compressed_blob() { return kCompressedBlob; }\n"
        )
        f.write(f"size_t uncompressed_total_size() {{ return {len(raw_blob)}u; }}\n")
        f.write(
            "const std::span<const DocIndexEntry> doc_index() { return kDocIndex; }\n\n"
        )
        f.write("} // namespace kura::docs::detail\n")


if __name__ == "__main__":
    main()
