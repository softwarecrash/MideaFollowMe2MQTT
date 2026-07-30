Import("env")

import gzip
import os


project_dir = env.subst("$PROJECT_DIR")
build_dir = env.subst("$BUILD_DIR")
web_dir = os.path.join(project_dir, "web")
generated_dir = os.path.join(build_dir, "generated")
generated_header = os.path.join(generated_dir, "WebAssets.h")

assets = (
    ("Style", "style.css"),
    ("Index", "index.html"),
    ("Wifi", "wifi.html"),
    ("SettingsMenu", "settings-menu.html"),
    ("Settings", "settings.html"),
    ("Update", "update.html"),
)


def cpp_bytes(data):
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16]))
    return ",\n".join(rows)


os.makedirs(generated_dir, exist_ok=True)
parts = [
    "#pragma once",
    "",
    "#include <Arduino.h>",
    "",
    "namespace WebAssets {",
]

source_total = 0
compressed_total = 0
for symbol, filename in assets:
    source_path = os.path.join(web_dir, filename)
    with open(source_path, "rb") as source_file:
        source = source_file.read()
    compressed = gzip.compress(source, compresslevel=9, mtime=0)
    source_total += len(source)
    compressed_total += len(compressed)
    parts.extend((
        f"const uint8_t k{symbol}[] PROGMEM = {{",
        cpp_bytes(compressed),
        "};",
        f"constexpr size_t k{symbol}Length = sizeof(k{symbol});",
        "",
    ))

parts.extend(("}  // namespace WebAssets", ""))
content = "\n".join(parts)

previous = None
if os.path.isfile(generated_header):
    with open(generated_header, "r", encoding="utf-8") as header_file:
        previous = header_file.read()
if previous != content:
    with open(generated_header, "w", encoding="utf-8", newline="\n") as header_file:
        header_file.write(content)

env.Prepend(CPPPATH=[generated_dir])
print(
    f"Embedded web assets: {source_total} -> {compressed_total} bytes "
    f"({compressed_total * 100 // source_total}% of source)"
)
