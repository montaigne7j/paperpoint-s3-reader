#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;

  // true：與前一 token 黏在一起，沒有空格，也不能換行。
  // 用於 NBSP、跨 inline-style 的連續內容。
  std::vector<bool> wordContinues;

  // true：與前一 token 之間沒有空格，但仍允許換行。
  // 用於 CJK 因 MAX_WORD_SIZE 產生的人工分段。
  std::vector<bool> wordNoSpace;
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;

  void applyParagraphIndent();
  std::vector<size_t> computeLineBreaks(
      const GfxRenderer& renderer,
      int fontId,
      int pageWidth,
      std::vector<uint16_t>& wordWidths,
      std::vector<bool>& continuesVec,
      std::vector<bool>& noSpaceVec
  );
  std::vector<size_t> computeHyphenatedLineBreaks(
      const GfxRenderer& renderer,
      int fontId,
      int pageWidth,
      std::vector<uint16_t>& wordWidths,
      std::vector<bool>& continuesVec,
      std::vector<bool>& noSpaceVec
  );
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  void extractLine(
      size_t breakIndex,
      int pageWidth,
      const std::vector<uint16_t>& wordWidths,
      const std::vector<bool>& continuesVec,
      const std::vector<bool>& noSpaceVec,
      const std::vector<size_t>& lineBreakIndices,
      const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
      const GfxRenderer& renderer,
      int fontId
  );
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool hyphenationEnabled = false,
                      const BlockStyle& blockStyle = BlockStyle())
      : blockStyle(blockStyle), extraParagraphSpacing(extraParagraphSpacing), hyphenationEnabled(hyphenationEnabled) {}
  ~ParsedText() = default;

  void addWord(
      std::string word,
      EpdFontFamily::Style fontStyle,
      bool underline = false,
      bool attachToPrevious = false,
      bool noSpaceBefore = false
  );
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};