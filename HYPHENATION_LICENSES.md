# Hyphenation pattern licences

The slim PaperPoint firmware keeps only English Liang hyphenation data. Chinese text does not need Liang hyphenation, and removing the other generated tries saves flash for the embedded CJK reader font.

| Output | Upstream pattern | Copyright / authors | Licence used here |
|---|---|---|---|
| `hyph-en.trie.h` | `hyph-en-us.tex` | Copyright 1990, 2004, 2005 Gerard D.C. Kuiken | Custom permissive notice; see `LICENSES/Hyphenation-English-Permission.txt` |

Upstream: https://github.com/typst/hypher

When regenerating this header, preserve this table and the applicable licence file. `scripts/update_hypenation.sh` can be adjusted for a broader multilingual build, but the default PaperPoint build intentionally embeds only English hyphenation.
