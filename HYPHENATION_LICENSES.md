# Hyphenation pattern licences

The generated files in `lib/Epub/Epub/hyphenation/generated/` are compiled
representations of patterns from `typst/hypher`. Hypher states that pattern
files keep their individual licences even after being embedded into a binary.

| Output | Upstream pattern | Copyright / authors | Licence used here |
|---|---|---|---|
| `hyph-en.trie.h` | `hyph-en-us.tex` | Copyright 1990, 2004, 2005 Gerard D.C. Kuiken | Custom permissive notice; see `LICENSES/Hyphenation-English-Permission.txt` |
| `hyph-fr.trie.h` | `hyph-fr.tex` | Copyright 1994-2002 Daniel Flipo, Bernard Gaulle; 2016 Arthur Reutenauer | MIT; see `LICENSES/MIT.txt` |
| `hyph-de.trie.h` | `hyph-de-1996.tex` | Copyright 2013-2024 Deutschsprachige Trennmustermannschaft contributors listed upstream | MIT; see `LICENSES/MIT.txt` |
| `hyph-es.trie.h` | `hyph-es.tex` | Copyright 1993, 1997 Javier Bezos; 2001-2019 Javier Bezos and CervanTeX | MIT/X11; see `LICENSES/MIT.txt` |
| `hyph-it.trie.h` | `hyph-it.tex` | Copyright 2008-2011 Claudio Beccari | MIT option selected; see `LICENSES/MIT.txt` |
| `hyph-ru.trie.h` | `hyph-ru.tex` | Copyright 1999-2003 Alexander I. Lebedev | LPPL 1.2 or later; this distribution uses LPPL 1.3c, see `LICENSES/LPPL-1.3c.txt` |
| `hyph-uk.trie.h` | `hyph-uk.tex` | Copyright 1998-2001 Maksym Polyakov | LPPL; see `LICENSES/LPPL-1.3c.txt` |

Upstream: https://github.com/typst/hypher

When regenerating these headers, preserve this table and the applicable
licence files. `scripts/update_hypenation.sh` records the upstream revision
and prepends a provenance notice to each generated header.
