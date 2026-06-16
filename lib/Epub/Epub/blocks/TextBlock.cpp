#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

void TextBlock::render(
    const GfxRenderer& renderer,
    const int fontId,
    const int x,
    const int y
) const {
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

  if (layoutMode == TextLayoutMode::Vertical) {
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
      const int glyphX =
          x + wordXpos[i];

      const int glyphY =
          y + wordYpos[i];

      renderer.drawVerticalText(
          fontId,
          glyphX,
          glyphY,
          words[i].c_str(),
          true,
          wordStyles[i]
      );
    }

    // 直排底線之後再處理。
    return;
  }

  // 以下是原本的橫排 render。
  for (size_t i = 0; i < words.size(); ++i) {
    const int wordX =
        wordXpos[i] + x;

    const EpdFontFamily::Style currentStyle =
        wordStyles[i];

    renderer.drawText(
        fontId,
        wordX,
        y,
        words[i].c_str(),
        true,
        currentStyle
    );

    if ((currentStyle &
         EpdFontFamily::UNDERLINE) != 0) {
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
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (layoutMode == TextLayoutMode::Vertical) {
    LOG_ERR(
        "TXB",
        "Vertical TextBlock serialization "
        "not implemented yet"
    );
    return false;
  }

  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u)\n", words.size(),
            wordXpos.size(), wordStyles.size());
    return false;
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), blockStyle));
}
