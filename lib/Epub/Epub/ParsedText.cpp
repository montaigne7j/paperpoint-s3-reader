#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "hyphenation/Hyphenator.h"

namespace {

uint32_t toVerticalPresentationCodepoint(
    const uint32_t codepoint
) {
    // 不再替換成 FE10～FE48 的直排專用字元。
    // 目前外部字型可能沒有這些 glyph，
    // 旋轉與位置調整交給 GfxRenderer 處理。
    return codepoint;
}

std::string encodeUtf8Codepoint(
    const uint32_t codepoint
) {
  std::string result;
  result.reserve(4);

  if (codepoint <= 0x7F) {
    result.push_back(
        static_cast<char>(codepoint)
    );
  } else if (codepoint <= 0x7FF) {
    result.push_back(
        static_cast<char>(
            0xC0 | (codepoint >> 6)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 | (codepoint & 0x3F)
        )
    );
  } else if (codepoint <= 0xFFFF) {
    result.push_back(
        static_cast<char>(
            0xE0 | (codepoint >> 12)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 |
            ((codepoint >> 6) & 0x3F)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 | (codepoint & 0x3F)
        )
    );
  } else if (codepoint <= 0x10FFFF) {
    result.push_back(
        static_cast<char>(
            0xF0 | (codepoint >> 18)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 |
            ((codepoint >> 12) & 0x3F)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 |
            ((codepoint >> 6) & 0x3F)
        )
    );

    result.push_back(
        static_cast<char>(
            0x80 | (codepoint & 0x3F)
        )
    );
  }

  return result;
}

}  // namespace

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

bool isCjkLayoutCodepoint(const uint32_t cp) {
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
  if (cp >= 0x20000 && cp <= 0x2FA1F) return true;

  if (cp >= 0x3000 && cp <= 0x303F) return true;
  if (cp >= 0x3040 && cp <= 0x309F) return true;
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;

  if (cp >= 0x3100 && cp <= 0x312F) return true;
  if (cp >= 0x31A0 && cp <= 0x31BF) return true;

  if (cp >= 0x3200 && cp <= 0x33FF) return true;
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;

  return false;
}

bool containsCjkLayoutText(const std::string& text) {
  const uint8_t* ptr =
      reinterpret_cast<const uint8_t*>(text.c_str());

  uint32_t cp = 0;

  while ((cp = utf8NextCodepoint(&ptr)) != 0) {
    if (isCjkLayoutCodepoint(cp)) {
      return true;
    }
  }

  return false;
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

}  // namespace

void ParsedText::addWord(
    std::string word,
    const EpdFontFamily::Style fontStyle,
    const bool underline,
    const bool attachToPrevious,
    const bool noSpaceBefore
) {
  if (word.empty()) {
    return;
  }

  EpdFontFamily::Style combinedStyle = fontStyle;

  if (underline) {
    combinedStyle =
        static_cast<EpdFontFamily::Style>(
            combinedStyle | EpdFontFamily::UNDERLINE
        );
  }

  /*
   * noSpaceBefore 目前是由 MAX_WORD_SIZE 的內部 buffer
   * 分段產生。它不是 EPUB 中真正的 word boundary。
   *
   * 若樣式相同，直接把新 chunk 接回上一個 word，
   * 不建立新的排版 token，也就不會產生人工換行點。
   */
  if (noSpaceBefore &&
      !words.empty() &&
      !wordStyles.empty() &&
      wordStyles.back() == combinedStyle) {

    words.back().append(word);

    // 不 push wordStyles、wordContinues、wordNoSpace，
    // 因為仍然只是同一個 token。
    return;
  }

  words.push_back(std::move(word));
  wordStyles.push_back(combinedStyle);
  wordContinues.push_back(attachToPrevious);

  wordNoSpace.push_back(
      noSpaceBefore && !attachToPrevious
  );
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent();

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  if (hyphenationEnabled) {
    lineBreakIndices =
        computeHyphenatedLineBreaks(
            renderer,
            fontId,
            pageWidth,
            wordWidths,
            wordContinues,
            wordNoSpace
        );
  } else {
    lineBreakIndices =
        computeLineBreaks(
            renderer,
            fontId,
            pageWidth,
            wordWidths,
            wordContinues,
            wordNoSpace
        );
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(
        i,
        pageWidth,
        wordWidths,
        wordContinues,
        wordNoSpace,
        lineBreakIndices,
        processLine,
        renderer,
        fontId
    );
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);

    wordNoSpace.erase(
    wordNoSpace.begin(),
    wordNoSpace.begin() + consumed
);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, words[i], wordStyles[i]));
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(
    const GfxRenderer& renderer,
    const int fontId,
    const int pageWidth,
    std::vector<uint16_t>& wordWidths,
    std::vector<bool>& continuesVec,
    std::vector<bool>& noSpaceVec
) {
  if (words.empty()) {
    return {};
  }

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;

      if (j > static_cast<size_t>(i)) {
        if (continuesVec[j]) {
          // 不加空格，也不能在此邊界換行。
          gap = renderer.getKerning(
              fontId,
              lastCodepoint(words[j - 1]),
              firstCodepoint(words[j]),
              wordStyles[j - 1]
          );
        } else if (noSpaceVec[j]) {
          // CJK 人工分段：不加空格，但允許換行。
          gap = 0;
        } else {
          gap = renderer.getSpaceAdvance(
              fontId,
              lastCodepoint(words[j - 1]),
              firstCodepoint(words[j]),
              wordStyles[j - 1]
          );
        }
      }
      currlen += wordWidths[j] + gap;

      if (currlen > effectivePageWidth) {
        break;
      }

      // Cannot break after word j if the next word attaches to it (continuation group)
      if (j + 1 < totalWordCount && continuesVec[j + 1]) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent() {
  if (extraParagraphSpacing || words.empty()) {
    return;
  }

  if (blockStyle.textIndentDefined) {
    // CSS text-indent is explicitly set (even if 0) - don't use fallback EmSpace
    // The actual indent positioning is handled in extractLine()
  } else if (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left) {
    // No CSS text-indent defined - use EmSpace fallback for visual indent
    words.front().insert(0, "\xe2\x80\x83");
  }
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t>
ParsedText::computeHyphenatedLineBreaks(
    const GfxRenderer& renderer,
    const int fontId,
    const int pageWidth,
    std::vector<uint16_t>& wordWidths,
    std::vector<bool>& continuesVec,
    std::vector<bool>& noSpaceVec
) {
  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;

      if (!isFirstWord) {
        if (continuesVec[currentIndex]) {
          spacing = renderer.getKerning(
              fontId,
              lastCodepoint(words[currentIndex - 1]),
              firstCodepoint(words[currentIndex]),
              wordStyles[currentIndex - 1]
          );
        } else if (noSpaceVec[currentIndex]) {
          // CJK chunk 邊界沒有空格，但仍是合法換行點。
          spacing = 0;
        } else {
          spacing = renderer.getSpaceAdvance(
              fontId,
              lastCodepoint(words[currentIndex - 1]),
              firstCodepoint(words[currentIndex]),
              wordStyles[currentIndex - 1]
          );
        }
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
int chosenWidth = -1;
bool chosenNeedsHyphen = true;

// CJK 的斷點按 byte offset 遞增，而且不需要插入連字號。
// 使用二分搜尋，避免逐一量測所有前綴。
const bool useFastCjkSearch =
    containsCjkLayoutText(word) &&
    std::all_of(
        breakInfos.begin(),
        breakInfos.end(),
        [](const Hyphenator::BreakInfo& info) {
          return !info.requiresInsertedHyphen;
        }
    );

  if (useFastCjkSearch) {
    size_t low = 0;
    size_t high = breakInfos.size();

    while (low < high) {
      const size_t mid = low + (high - low) / 2;
      const auto& info = breakInfos[mid];
      const size_t offset = info.byteOffset;

      if (offset == 0 || offset >= word.size()) {
        // 正常的 CJK break offset 不應進入這裡。
        high = mid;
        continue;
      }

      const int prefixWidth =
          measureWordWidth(
              renderer,
              fontId,
              word.substr(0, offset),
              style,
              false
          );

      if (prefixWidth <= availableWidth) {
        chosenWidth = prefixWidth;
        chosenOffset = offset;
        chosenNeedsHyphen = false;

        // 再嘗試更後面的斷點。
        low = mid + 1;
      } else {
        // 前綴太寬，往前半部搜尋。
        high = mid;
      }
    }
  } else {
    // 英文與其他語言維持原本逐一檢查，
    // 避免改變既有的連字與 soft-hyphen 行為。
    for (const auto& info : breakInfos) {
      const size_t offset = info.byteOffset;

      if (offset == 0 || offset >= word.size()) {
        continue;
      }

      const bool needsHyphen =
          info.requiresInsertedHyphen;

      const int prefixWidth =
          measureWordWidth(
              renderer,
              fontId,
              word.substr(0, offset),
              style,
              needsHyphen
          );

      if (prefixWidth > availableWidth ||
          prefixWidth <= chosenWidth) {
        continue;
      }

      chosenWidth = prefixWidth;
      chosenOffset = offset;
      chosenNeedsHyphen = needsHyphen;
    }
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
    wordNoSpace.insert(
      wordNoSpace.begin() + wordIndex + 1,
      false
  );

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(
    const size_t breakIndex,
    const int pageWidth,
    const std::vector<uint16_t>& wordWidths,
    const std::vector<bool>& continuesVec,
    const std::vector<bool>& noSpaceVec,
    const std::vector<size_t>& lineBreakIndices,
    const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
    const GfxRenderer& renderer,
    const int fontId
) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const bool isFirstLine = breakIndex == 0;
  const int firstLineIndent =
      isFirstLine && blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    // Count gaps: each word after the first creates a gap, unless it's a continuation
    if (wordIdx > 0) {
      const size_t index =
          lastBreakAt + wordIdx;

      if (continuesVec[index]) {
        totalNaturalGaps += renderer.getKerning(
            fontId,
            lastCodepoint(words[index - 1]),
            firstCodepoint(words[index]),
            wordStyles[index - 1]
        );
      } else if (noSpaceVec[index]) {
        // 不加空格，也不參與 justified space 分配。
      } else {
        actualGapCount++;

        totalNaturalGaps +=
            renderer.getSpaceAdvance(
                fontId,
                lastCodepoint(words[index - 1]),
                firstCodepoint(words[index]),
                wordStyles[index - 1]
            );
      }
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  // For justified text, compute per-gap extra to distribute remaining space evenly
  const int spareSpace = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
  const int justifyExtra = (blockStyle.alignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1)
                               ? spareSpace / static_cast<int>(actualGapCount)
                               : 0;

  // Calculate initial x position (first line starts at indent for left/justified text;
  // may be negative for hanging indents, e.g. margin-left:3em; text-indent:-1em).
  auto xpos = static_cast<int16_t>(firstLineIndent);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
  }

  // Pre-calculate X positions for words
  // Continuation words attach to the previous word with no space before them
  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineXPos.push_back(xpos);

    const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
    const bool nextHasNoSpace =
      wordIdx + 1 < lineWordCount &&
      noSpaceVec[lastBreakAt + wordIdx + 1];

    if (nextIsContinuation) {
      int advance =
          wordWidths[lastBreakAt + wordIdx];

      advance += renderer.getKerning(
          fontId,
          lastCodepoint(words[lastBreakAt + wordIdx]),
          firstCodepoint(
              words[lastBreakAt + wordIdx + 1]
          ),
          wordStyles[lastBreakAt + wordIdx]
      );

      xpos += advance;

    } else if (nextHasNoSpace) {
      // CJK chunk 邊界：直接接續，無空白。
      xpos += wordWidths[lastBreakAt + wordIdx];

    } else {
      int gap = 0;

      if (wordIdx + 1 < lineWordCount) {
        gap = renderer.getSpaceAdvance(
            fontId,
            lastCodepoint(words[lastBreakAt + wordIdx]),
            firstCodepoint(
                words[lastBreakAt + wordIdx + 1]
            ),
            wordStyles[lastBreakAt + wordIdx]
        );
      }

      if (blockStyle.alignment ==
              CssTextAlign::Justify &&
          !isLastLine) {
        gap += justifyExtra;
      }

      xpos +=
          wordWidths[lastBreakAt + wordIdx] +
          gap;
    }
  }

  // Build line data by moving from the original vectors using index range
  std::vector<std::string> lineWords(std::make_move_iterator(words.begin() + lastBreakAt),
                                     std::make_move_iterator(words.begin() + lineBreak));
  std::vector<EpdFontFamily::Style> lineWordStyles(wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(
      std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), blockStyle));
}

void ParsedText::layoutAndExtractColumns(
    const GfxRenderer& renderer,
    const int fontId,
    const uint16_t viewportHeight,
    const std::function<void(
        std::shared_ptr<TextBlock>
    )>& processColumn
) {
  if (words.empty()) {
    return;
  }

  /*
   * 沿用原本段首縮排處理。
   *
   * applyParagraphIndent() 可能會在第一個 word 前加入
   * em-space；下方會把 em-space 當成一個空白直排 cell。
   */
  applyParagraphIndent();

  // A 21x30 fixed-cell CJK glyph needs visible air between vertically
  // stacked characters.  Previously advance == line height, which made ink
  // from adjacent characters appear almost connected.
  constexpr int verticalGlyphGap = 4;
  const int glyphAdvance =
      std::max(
          1,
          renderer.getLineHeight(fontId) + verticalGlyphGap
      );

  const int maxCellsPerColumn =
      std::max(
          1,
          static_cast<int>(viewportHeight) /
              glyphAdvance
      );

  std::vector<std::string> columnGlyphs;
  std::vector<int16_t> columnXPos;
  std::vector<int16_t> columnYPos;
  std::vector<EpdFontFamily::Style>
      columnStyles;

  columnGlyphs.reserve(maxCellsPerColumn);
  columnXPos.reserve(maxCellsPerColumn);
  columnYPos.reserve(maxCellsPerColumn);
  columnStyles.reserve(maxCellsPerColumn);

  int currentCell = 0;

  /*
   * 此欄包含多少個原始 ParsedText word。
   *
   * 直排時 words 會拆成 glyph，但 footnote tracking
   * 仍然需要原始 word 數量。
   */
  uint16_t logicalWordsInColumn = 0;

  const auto flushColumn = [&]() {
    if (!columnGlyphs.empty()) {
      processColumn(
          std::make_shared<TextBlock>(
              std::move(columnGlyphs),
              std::move(columnXPos),
              std::move(columnYPos),
              std::move(columnStyles),
              blockStyle,
              TextLayoutMode::Vertical,
              logicalWordsInColumn
          )
      );
    }

    columnGlyphs.clear();
    columnXPos.clear();
    columnYPos.clear();
    columnStyles.clear();

    columnGlyphs.reserve(maxCellsPerColumn);
    columnXPos.reserve(maxCellsPerColumn);
    columnYPos.reserve(maxCellsPerColumn);
    columnStyles.reserve(maxCellsPerColumn);

    currentCell = 0;
    logicalWordsInColumn = 0;
  };

  /*
   * 往下移動一個空白 cell。
   *
   * 用於：
   * - 英文 word 之間的空格
   * - 段首 em-space
   * - 全形空白
   */
  const auto advanceBlankCell = [&]() {
    if (currentCell >= maxCellsPerColumn) {
      flushColumn();
    }

    ++currentCell;
  };

  for (size_t wordIndex = 0;
       wordIndex < words.size();
       ++wordIndex) {
    /*
     * 一般 word 邊界需要保留一格空白。
     *
     * wordContinues：
     *   不加空格，也不可視為普通 word 邊界。
     *
     * wordNoSpace：
     *   parser buffer 的人工切割，不加空格。
     */
    if (wordIndex > 0 &&
        !wordContinues[wordIndex] &&
        !wordNoSpace[wordIndex]) {
      advanceBlankCell();
    }

    const std::string& word =
        words[wordIndex];

    const EpdFontFamily::Style style =
        wordStyles[wordIndex];

    const uint8_t* cursor =
        reinterpret_cast<const uint8_t*>(
            word.c_str()
        );

    bool logicalWordCounted = false;

    while (*cursor != 0) {
      const uint8_t* glyphStart = cursor;

      const uint32_t codepoint =
          utf8NextCodepoint(&cursor);

      if (codepoint == 0) {
        break;
      }

      const size_t glyphByteLength =
          static_cast<size_t>(
              cursor - glyphStart
          );

      if (glyphByteLength == 0 ||
          glyphByteLength > 4) {
        continue;
      }

      // CR 不處理。
      if (codepoint == '\r') {
        continue;
      }

      // 文字中的換行符號強制開始下一欄。
      if (codepoint == '\n') {
        flushColumn();
        continue;
      }

      // Soft hyphen 在直排 MVP 中不顯示。
      if (codepoint == 0x00AD) {
        continue;
      }

      /*
       * 空白字元只佔一格，不建立可見 glyph。
       *
       * U+0020：一般空格
       * U+00A0：NBSP
       * U+2003：em-space
       * U+3000：全形空格
       */
      if (codepoint == 0x0020 ||
          codepoint == 0x00A0 ||
          codepoint == 0x2003 ||
          codepoint == 0x3000) {
        advanceBlankCell();
        continue;
      }

      /*
       * Combining mark 優先附加到前一個 glyph，
       * 不額外占用一格。
       */
      if (utf8IsCombiningMark(codepoint) &&
          !columnGlyphs.empty()) {
        columnGlyphs.back().append(
            reinterpret_cast<const char*>(
                glyphStart
            ),
            glyphByteLength
        );
        continue;
      }

      if (currentCell >= maxCellsPerColumn) {
        flushColumn();
      }

      /*
       * 原始 word 只計數一次。
       *
       * 若一個很長的中文 chunk 跨越多欄，
       * logical word count 只會記在第一欄。
       */
      if (!logicalWordCounted) {
        ++logicalWordsInColumn;
        logicalWordCounted = true;
      }

      columnGlyphs.emplace_back(
          reinterpret_cast<const char*>(
              glyphStart
          ),
          glyphByteLength
      );

      // 一個 TextBlock 代表一欄，所以欄內 X 都是 0。
      columnXPos.push_back(0);

      columnYPos.push_back(
          static_cast<int16_t>(
              currentCell * glyphAdvance
          )
      );

      columnStyles.push_back(style);

      ++currentCell;
    }
  }

  // 輸出最後尚未滿的欄。
  flushColumn();

  /*
   * 所有內容都已轉移到直排 TextBlock。
   * 清空原始資料，避免下一次重複輸出。
   */
  words.clear();
  wordStyles.clear();
  wordContinues.clear();
  wordNoSpace.clear();
}