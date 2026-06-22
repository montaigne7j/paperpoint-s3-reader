#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Pin this revision for reproducible releases. Update deliberately and review
# HYPHENATION_LICENSES.md whenever changing it.
HYPHER_REV="v0.1.7"

cd "$ROOT_DIR"

process() {
  local lang="$1"
  local pattern="$2"
  local licence="$3"
  local output="lib/Epub/Epub/hyphenation/generated/hyph-${lang}.trie.h"

  mkdir -p build
  wget -O "build/$lang.bin" "https://raw.githubusercontent.com/typst/hypher/${HYPHER_REV}/tries/$lang.bin"
  python scripts/generate_hyphenation_trie.py --input "build/$lang.bin" --output "$output.tmp"
  {
    printf '/*\n * Generated hyphenation data; do not remove this notice.\n'
    printf ' * Upstream: typst/hypher patterns/%s at %s\n' "$pattern" "$HYPHER_REV"
    printf ' * Licence: %s\n * Full attribution: HYPHENATION_LICENSES.md\n */\n' "$licence"
    cat "$output.tmp"
  } > "$output"
  rm -f "$output.tmp"
}

process en hyph-en-us.tex 'Custom permissive; LICENSES/Hyphenation-English-Permission.txt'
process fr hyph-fr.tex 'MIT; LICENSES/MIT.txt'
process de hyph-de-1996.tex 'MIT; LICENSES/MIT.txt'
process es hyph-es.tex 'MIT/X11; LICENSES/MIT.txt'
process it hyph-it.tex 'MIT option; LICENSES/MIT.txt'
process ru hyph-ru.tex 'LPPL 1.2 or later; LICENSES/LPPL-1.3c.txt'
process uk hyph-uk.tex 'LPPL; LICENSES/LPPL-1.3c.txt'
