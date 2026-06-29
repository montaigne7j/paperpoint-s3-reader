#include "TextBlock.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include <ReaderMemoryDiagnostics.h>

#include "../PageRenderProfiler.h"

namespace {

uint32_t countUtf8Codepoints(const std::string& text) {
  uint32_t count = 0;
  for (size_t i = 0; i < text.size();) {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    size_t step = 1;
    if ((c & 0x80) == 0x00) {
      step = 1;
    } else if ((c & 0xE0) == 0xC0) {
      step = 2;
    } else if ((c & 0xF0) == 0xE0) {
      step = 3;
    } else if ((c & 0xF8) == 0xF0) {
      step = 4;
    }
    if (i + step > text.size()) {
      step = 1;
    }
    i += step;
    ++count;
  }
  return count;
}

void summarizeWords(
    const std::vector<std::string>& words,
    uint32_t& outBytes,
    uint32_t& outGlyphs
) {
  outBytes = 0;
  outGlyphs = 0;
  for (const auto& word : words) {
    outBytes += static_cast<uint32_t>(word.size());
    outGlyphs += countUtf8Codepoints(word);
  }
}

}  // namespace

void TextBlock::render(
    const GfxRenderer& renderer,
    const int fontId,
    const int x,
    const int y
) const {
  const bool profiling = PageRenderProfiler::isEnabled();
  const ReaderMemoryDiagTrace blockMemBefore = ReaderMemoryDiagnostics::capture();
  const unsigned long blockStart = millis();
  const bool vertical = layoutMode == TextLayoutMode::Vertical;

  if (words.size() != wordXpos.size() ||
      words.size() != wordStyles.size()) {
    LOG_ERR(
        "TXB",
        "Render skipped: size mismatch "
        "(words=%u, xpos=%u, styles=%u)",
        static_cast<uint32_t>(words.size()),
        static_cast<uint32_t>(wordXpos.size()),
        static_cast<uint32_t>(wordStyles.size())
    );
    return;
  }

  uint32_t totalBytes = 0;
  uint32_t totalGlyphs = 0;
  if (profiling) {
    summarizeWords(words, totalBytes, totalGlyphs);
  }

  unsigned long drawTotal = 0;
  unsigned long underlineTotal = 0;
  unsigned long slowestWordMs = 0;
  size_t slowestWordIndex = 0;

  if (vertical) {
    if (words.size() != wordYpos.size()) {
      LOG_ERR(
          "TXB",
          "Vertical render skipped: "
          "words=%u, ypos=%u",
          static_cast<uint32_t>(words.size()),
          static_cast<uint32_t>(wordYpos.size())
      );
      return;
    }

    for (size_t i = 0; i < words.size(); ++i) {
      const int glyphX = x + wordXpos[i];
      const int glyphY = y + wordYpos[i];

      const ReaderMemoryDiagTrace wordMemBefore = ReaderMemoryDiagnostics::capture();
      const unsigned long tWord = millis();
      renderer.drawVerticalText(
          fontId,
          glyphX,
          glyphY,
          words[i].c_str(),
          true,
          wordStyles[i]
      );
      const unsigned long wordMs = millis() - tWord;
      const ReaderMemoryDiagTrace wordMemAfter = ReaderMemoryDiagnostics::capture();
      {
        char memPhase[96];
        std::snprintf(memPhase, sizeof(memPhase), "textblock-drawVerticalText[%u]", static_cast<unsigned>(i));
        ReaderMemoryDiagnostics::logDeltaIfChanged(memPhase, wordMemBefore, wordMemAfter, wordMs, 512, 4096, 120);
      }
      drawTotal += wordMs;
      if (wordMs > slowestWordMs) {
        slowestWordMs = wordMs;
        slowestWordIndex = i;
      }
    }

    if (profiling) {
      const unsigned long total = millis() - blockStart;
      const unsigned long avgDraw = words.empty() ? 0 : drawTotal / words.size();
      LOG_DBG(
          "TXB",
          "summary layout=vertical entries=%u logical=%u bytes=%u glyphs=%u draw=%lums underline=0ms total=%lums avgDraw=%lums slowestIndex=%u slowest=%lums",
          static_cast<unsigned>(words.size()),
          static_cast<unsigned>(logicalWordCount),
          static_cast<unsigned>(totalBytes),
          static_cast<unsigned>(totalGlyphs),
          drawTotal,
          total,
          avgDraw,
          static_cast<unsigned>(slowestWordIndex),
          slowestWordMs
      );
    }

    {
      const ReaderMemoryDiagTrace blockMemAfter = ReaderMemoryDiagnostics::capture();
      ReaderMemoryDiagnostics::logDeltaIfChanged(
          "textblock-render-total[vertical]",
          blockMemBefore,
          blockMemAfter,
          millis() - blockStart,
          512,
          4096,
          120);
    }

    // 直排底線之後再處理。
    return;
  }

  // 以下是原本的橫排 render。
  for (size_t i = 0; i < words.size(); ++i) {
    const int wordX = wordXpos[i] + x;

    const EpdFontFamily::Style currentStyle = wordStyles[i];

    const unsigned long tWord = millis();
    renderer.drawText(
        fontId,
        wordX,
        y,
        words[i].c_str(),
        true,
        currentStyle
    );
    const unsigned long wordMs = millis() - tWord;
    drawTotal += wordMs;
    if (wordMs > slowestWordMs) {
      slowestWordMs = wordMs;
      slowestWordIndex = i;
    }

    if ((currentStyle &
         EpdFontFamily::UNDERLINE) != 0) {
      const unsigned long tUnderline = millis();
      const std::string& word = words[i];

      const int fullWordWidth =
          renderer.getTextWidth(
              fontId,
              word.c_str(),
              currentStyle
          );

      const int underlineY =
          y +
          renderer.getFontAscenderSize(fontId) +
          2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      if (word.size() >= 3 &&
          static_cast<uint8_t>(word[0]) == 0xE2 &&
          static_cast<uint8_t>(word[1]) == 0x80 &&
          static_cast<uint8_t>(word[2]) == 0x83) {
        const char* visiblePtr =
            word.c_str() + 3;

        const int prefixWidth =
            renderer.getTextAdvanceX(
                fontId,
                "\xe2\x80\x83",
                currentStyle
            );

        const int visibleWidth =
            renderer.getTextWidth(
                fontId,
                visiblePtr,
                currentStyle
            );

        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(
          startX,
          underlineY,
          startX + underlineWidth,
          underlineY,
          true
      );
      underlineTotal += millis() - tUnderline;
    }
  }

  {
    const ReaderMemoryDiagTrace blockMemAfter = ReaderMemoryDiagnostics::capture();
    ReaderMemoryDiagnostics::logDeltaIfChanged(
        "textblock-render-total[horizontal]",
        blockMemBefore,
        blockMemAfter,
        millis() - blockStart,
        512,
        4096,
        120);
  }

  if (profiling) {
    const unsigned long total = millis() - blockStart;
    const unsigned long avgDraw = words.empty() ? 0 : drawTotal / words.size();
    LOG_DBG(
        "TXB",
        "summary layout=horizontal entries=%u logical=%u bytes=%u glyphs=%u draw=%lums underline=%lums total=%lums avgDraw=%lums slowestIndex=%u slowest=%lums",
        static_cast<unsigned>(words.size()),
        static_cast<unsigned>(logicalWordCount),
        static_cast<unsigned>(totalBytes),
        static_cast<unsigned>(totalGlyphs),
        drawTotal,
        underlineTotal,
        total,
        avgDraw,
        static_cast<unsigned>(slowestWordIndex),
        slowestWordMs
    );
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() ||
      words.size() != wordStyles.size()) {
    LOG_ERR(
        "TXB",
        "Serialization failed: size mismatch "
        "(words=%u, xpos=%u, styles=%u)",
        static_cast<unsigned>(words.size()),
        static_cast<unsigned>(wordXpos.size()),
        static_cast<unsigned>(wordStyles.size())
    );
    return false;
  }

  const bool vertical =
      layoutMode == TextLayoutMode::Vertical;

  if (words.size() > 65535) {
    LOG_ERR(
        "TXB",
        "Serialization failed: too many glyphs (%u)",
        static_cast<unsigned>(words.size())
    );
    return false;
  }

  // Layout header
  serialization::writePod(
      file,
      static_cast<uint8_t>(layoutMode)
  );

  serialization::writePod(
      file,
      logicalWordCount
  );

  // Word/glyph count
  const uint16_t wordCount =
      static_cast<uint16_t>(words.size());

  serialization::writePod(
      file,
      wordCount
  );

  for (const auto& word : words) {
    serialization::writeString(file, word);
  }

  for (const int16_t x : wordXpos) {
    serialization::writePod(file, x);
  }

  // 橫排不寫 Y vector；直排才寫。
  if (vertical) {
    for (const int16_t y : wordYpos) {
      serialization::writePod(file, y);
    }
  }

  for (const auto style : wordStyles) {
    serialization::writePod(file, style);
  }

  serialization::writePod(
      file,
      blockStyle.alignment
  );
  serialization::writePod(
      file,
      blockStyle.textAlignDefined
  );
  serialization::writePod(
      file,
      blockStyle.marginTop
  );
  serialization::writePod(
      file,
      blockStyle.marginBottom
  );
  serialization::writePod(
      file,
      blockStyle.marginLeft
  );
  serialization::writePod(
      file,
      blockStyle.marginRight
  );
  serialization::writePod(
      file,
      blockStyle.paddingTop
  );
  serialization::writePod(
      file,
      blockStyle.paddingBottom
  );
  serialization::writePod(
      file,
      blockStyle.paddingLeft
  );
  serialization::writePod(
      file,
      blockStyle.paddingRight
  );
  serialization::writePod(
      file,
      blockStyle.textIndent
  );
  serialization::writePod(
      file,
      blockStyle.textIndentDefined
  );

  return true;
}

std::unique_ptr<TextBlock>
TextBlock::deserialize(FsFile& file) {
  uint8_t layoutModeRaw = 0;
  uint16_t logicalWordCount = 0;
  uint16_t storedWordCount = 0;

  serialization::readPod(
      file,
      layoutModeRaw
  );

  if (layoutModeRaw >
      static_cast<uint8_t>(
          TextLayoutMode::Vertical)) {
    LOG_ERR(
        "TXB",
        "Deserialization failed: "
        "invalid layout mode %u",
        layoutModeRaw
    );
    return nullptr;
  }

  const TextLayoutMode layoutMode =
      static_cast<TextLayoutMode>(
          layoutModeRaw
      );

  serialization::readPod(
      file,
      logicalWordCount
  );

  serialization::readPod(
      file,
      storedWordCount
  );

  if (storedWordCount > 10000) {
    LOG_ERR(
        "TXB",
        "Deserialization failed: "
        "word count %u exceeds maximum",
        storedWordCount
    );
    return nullptr;
  }

  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<int16_t> wordYpos;
  std::vector<EpdFontFamily::Style>
      wordStyles;

  words.resize(storedWordCount);
  wordXpos.resize(storedWordCount);
  wordStyles.resize(storedWordCount);

  if (layoutMode ==
      TextLayoutMode::Vertical) {
    wordYpos.resize(storedWordCount);
  }

  for (auto& word : words) {
    serialization::readString(file, word);
  }

  for (auto& x : wordXpos) {
    serialization::readPod(file, x);
  }

  if (layoutMode ==
      TextLayoutMode::Vertical) {
    for (auto& y : wordYpos) {
      serialization::readPod(file, y);
    }
  }

  for (auto& style : wordStyles) {
    serialization::readPod(file, style);
  }

  BlockStyle blockStyle;

  serialization::readPod(
      file,
      blockStyle.alignment
  );
  serialization::readPod(
      file,
      blockStyle.textAlignDefined
  );
  serialization::readPod(
      file,
      blockStyle.marginTop
  );
  serialization::readPod(
      file,
      blockStyle.marginBottom
  );
  serialization::readPod(
      file,
      blockStyle.marginLeft
  );
  serialization::readPod(
      file,
      blockStyle.marginRight
  );
  serialization::readPod(
      file,
      blockStyle.paddingTop
  );
  serialization::readPod(
      file,
      blockStyle.paddingBottom
  );
  serialization::readPod(
      file,
      blockStyle.paddingLeft
  );
  serialization::readPod(
      file,
      blockStyle.paddingRight
  );
  serialization::readPod(
      file,
      blockStyle.textIndent
  );
  serialization::readPod(
      file,
      blockStyle.textIndentDefined
  );

  if (layoutMode ==
      TextLayoutMode::Vertical) {
    return std::unique_ptr<TextBlock>(
        new TextBlock(
            std::move(words),
            std::move(wordXpos),
            std::move(wordYpos),
            std::move(wordStyles),
            blockStyle,
            TextLayoutMode::Vertical,
            logicalWordCount
        )
    );
  }

  auto result =
      std::unique_ptr<TextBlock>(
          new TextBlock(
              std::move(words),
              std::move(wordXpos),
              std::move(wordStyles),
              blockStyle
          )
      );

  result->logicalWordCount =
      logicalWordCount;

  return result;
}
