import argparse
from contextlib import nullcontext
from pathlib import Path
from collections.abc import Sequence
import sys
import io
import hashlib
import zstandard as zstd
import tomllib

parser = argparse.ArgumentParser(description="")
parser.add_argument("-i", "--input", type=Path, help="The path to the .dj dir")
parser.add_argument("-o", "--output", type=Path, default=None, help="The output file")
parser.add_argument(
    "-v",
    "--version",
    type=str,
    default="0.0.0",
    help="The version of the kura compiler",
)
parser.add_argument(
    "-n",
    "--namespace",
    type=str,
    default="kura::explain::detail",
    help="The C++ namespace to generate with",
)
args = parser.parse_args()


def split_frontmatter(text: str) -> tuple[dict, str]:
    """
    Extracts frontmatter delimited by `---` (or `+++`) at the top of the file.
    Returns (metadata_dict, body_without_frontmatter).
    """
    normalized = text.replace("\r\n", "\n")

    if not (normalized.startswith("---\n") or normalized.startswith("+++\n")):
        return {}, text

    delim = normalized[:3]
    end_marker = f"\n{delim}\n"
    end_idx = normalized.find(end_marker, 4)

    if end_idx == -1 and normalized.endswith(f"\n{delim}"):
        end_idx = len(normalized) - 4
        body = ""
    elif end_idx != -1:
        body = normalized[end_idx + len(end_marker) :]
    else:
        raise ValueError(f"Unclosed frontmatter delimiter '{delim}'")

    raw_meta = normalized[4:end_idx]

    if delim == "+++":
        meta = tomllib.loads(raw_meta)
    else:
        meta = parse_simple_kv(raw_meta)

    return meta, body.lstrip("\n")


def parse_simple_kv(raw: str) -> dict:
    """
    Zero-dependency fallback parser for simple `key: value` YAML frontmatter
    (strings, booleans, and simple lists like `[a, b]`).
    """
    meta = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        k, _, v = line.partition(":")
        k, v = k.strip(), v.strip()
        if v.startswith("[") and v.endswith("]"):
            meta[k] = [
                item.strip().strip("\"'") for item in v[1:-1].split(",") if item.strip()
            ]
        elif v.lower() in ("true", "false"):
            meta[k] = v.lower() == "true"
        else:
            meta[k] = v.strip("\"'")
    return meta


def cpp_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


class Entry:
    def __init__(self, meta):
        self.code = meta.get("code")
        self.title = meta.get("title")
        self.severity = meta.get("severity")
        self.since = meta.get("since", args.version)
        self.category = meta.get("category")
        self.tags = meta.get("tags", [])
        self.deprecated = meta.get("deprecated", False)
        self.description = meta.get("description")

    def get_formatted_title(self) -> str:
        return f"# {self.code}: {self.title}"

    def get_formatted_footer(self) -> str:
        buffer = io.StringIO()

        buffer.write("::: note\n")
        if self.deprecated:
            buffer.write(f"[`{self.code}` is deprecated]{{.notice}}\n")
        buffer.write(f"Since: v{self.since}\n")
        buffer.write(f"Category: {self.category}\n")
        if len(self.tags) > 0:
            buffer.write(f"Tags: {', '.join(self.tags)}\n")

        # self.severity = meta.get("severity")
        buffer.write(":::\n\n")
        return buffer.getvalue()

    def __repr__(self) -> str:
        return f"Entry(code={self.code!r}, title={self.title!r}, description={self.description!r}, severity={self.severity!r}, since={self.since!r}, category={self.category!r}, tags={self.tags!r}, deprecated={self.deprecated!r})"


def format(entry, body) -> str:
    callout = io.StringIO()
    callout.write(":::: callout\n")
    callout.write("::: source\n")
    callout.write(":::\n")
    callout.write("::: fix\n")
    callout.write(":::\n")
    callout.write("::::\n")
    return f"{entry.get_formatted_title()}\n\n{body}\n\n{callout.getvalue()}\n{entry.get_formatted_footer()}"


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def compute_merkle_root_from_hex(hex_leaves: Sequence[str]) -> str:
    if not hex_leaves:
        raise ValueError("Cannot compute Merkle root of an empty list.")

    # Convert hex strings to 32-byte raw digests
    layer = [bytes.fromhex(h) for h in hex_leaves]

    while len(layer) > 1:
        if len(layer) % 2 == 1:
            layer.append(layer[-1])

        layer = [sha256(layer[i] + layer[i + 1]) for i in range(0, len(layer), 2)]

    return layer[0].hex()


def main():
    dj_files = sorted(args.input.glob("*.dj"))

    raw_blob = bytearray()
    entries = []  # (code, offset, length)

    for path in dj_files:
        raw_bytes = path.read_bytes()
        try:
            text = raw_bytes.decode("utf-8")
        except UnicodeDecodeError:
            continue

        try:
            meta, txt = split_frontmatter(text)
        except ValueError as ex:
            sys.exit(f"{path}: frontmatter error: {ex}")

        entry = Entry(meta)
        body = format(entry, txt).encode("utf-8")
        hash = hashlib.sha256(body).hexdigest()

        offset = len(raw_blob)
        raw_blob.extend(body)
        entries.append((Entry(meta), hash, offset, len(body)))

    cctx = zstd.ZstdCompressor(level=19)
    compressed = cctx.compress(bytes(raw_blob))

    out = (
        args.output.open("w", encoding="utf-8")
        if args.output
        else nullcontext(sys.stdout)
    )
    with out as f:
        f.write('#include "explain/index.h"\n')
        f.write("#include <array>\n")
        f.write("#include <cstdint>\n\n")
        f.write("namespace " + args.namespace + " {\n\n")

        # 1. Compressed byte array
        f.write(f"alignas(8) const uint8_t kCompressedBlob[{len(compressed)}] = {{\n")
        for i in range(0, len(compressed), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in compressed[i : i + 16])
            f.write(f"    {chunk},\n")
        f.write("};\n\n")

        # 2. Sorted index table
        f.write(f"const std::array<DocIndexEntry, {len(entries)}> kDocIndex = {{{{\n")
        leaves = []
        for entry, hash, offset, length in entries:
            f.write(
                f'    DocIndexEntry{{"{cpp_escape(entry.code)}", "{cpp_escape(entry.title)}", '
                f'"{cpp_escape(entry.severity)}", "{cpp_escape(entry.since)}", "{cpp_escape(entry.category)}", "{cpp_escape(hash)}", "{cpp_escape(entry.description)}", '
            )
            f.write("{")
            for tag in entry.tags:
                f.write(f'"{cpp_escape(tag)}", ')
            f.write("}, ")
            f.write(f"{str(entry.deprecated).lower()}, {offset}u, {length}u}},\n")
            leaves.append(hash)
        f.write("}};\n\n")

        index_root = compute_merkle_root_from_hex(leaves)
        f.write(
            "const std::span<const uint8_t> compressed_blob() { return kCompressedBlob; }\n"
        )
        f.write(f"size_t uncompressed_total_size() {{ return {len(raw_blob)}u; }}\n")
        f.write(
            "const std::span<const DocIndexEntry> doc_index() { return kDocIndex; }\n"
        )
        f.write(
            f'const std::string_view index_hash() {{ return "{cpp_escape(index_root)}"; }}\n\n'
        )
        f.write("} // namespace " + args.namespace + "\n")


if __name__ == "__main__":
    main()
