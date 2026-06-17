#pragma once
#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include <cstdint>

#include "Block.h"
#include "BlockStyle.h"

// Represents a line of text on a page
enum class TextLayoutMode : uint8_t {
  Horizontal = 0,
  Vertical = 1,
};

// Represents one horizontal line or one vertical text block on a page.
class TextBlock final : public Block {
 private:
  // 橫排時通常是一個或多個 word；
  // 直排時第一版會以單一 Unicode glyph 為一個元素。
  std::vector<std::string> words;

  // 每個元素相對於 PageLine 起點的 X 座標。
  std::vector<int16_t> wordXpos;

  // 每個元素相對於 PageLine 起點的 Y 座標。
  //
  // Horizontal：
  //   可以為空，整行共用 render() 傳入的 y。
  //
  // Vertical：
  //   數量必須和 words 相同。
  std::vector<int16_t> wordYpos;

  // 每個元素的字型樣式。
  std::vector<EpdFontFamily::Style> wordStyles;

  BlockStyle blockStyle;

  TextLayoutMode layoutMode =
      TextLayoutMode::Horizontal;

  // 用於 footnote/anchor 的原始 parser word 數量。
  // 直排時 words 會變成逐字 glyph，不能直接使用 words.size()。
  /*
   * Parser 原始 word 數量。
   *
   * 橫排：
   *   通常等於 words.size()。
   *
   * 直排：
   *   words 會被拆成逐字 glyph，因此不能用 glyph 數量
   *   取代原本的 parser word 數量。
   *
   * 這個數值之後會用於 footnote 與 anchor 位置追蹤。
   */
  uint16_t logicalWordCount = 0;

  public:
  /*
   * 橫排 constructor。
   *
   * 保留原本呼叫格式，避免現有 ParsedText::extractLine()
   * 和舊的橫排流程需要一起修改。
   */
  explicit TextBlock(
      std::vector<std::string> words,
      std::vector<int16_t> word_xpos,
      std::vector<EpdFontFamily::Style> word_styles,
      const BlockStyle& blockStyle = BlockStyle()
  )
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle),
        layoutMode(TextLayoutMode::Horizontal) {
    logicalWordCount =
        static_cast<uint16_t>(this->words.size());
  }

  /*
   * 直排 constructor。
   *
   * word_xpos 和 word_ypos 的數量都必須與 words 相同。
   * logical_word_count 是拆成 glyph 前的 parser word 數量。
   */
  explicit TextBlock(
      std::vector<std::string> words,
      std::vector<int16_t> word_xpos,
      std::vector<int16_t> word_ypos,
      std::vector<EpdFontFamily::Style> word_styles,
      const BlockStyle& blockStyle,
      const TextLayoutMode layoutMode,
      const uint16_t logical_word_count
   )
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordYpos(std::move(word_ypos)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle),
        layoutMode(layoutMode),
        logicalWordCount(logical_word_count) {}

  ~TextBlock() override = default;

  void setBlockStyle(
      const BlockStyle& blockStyle
  ) {
    this->blockStyle = blockStyle;
  }

  const BlockStyle& getBlockStyle() const {
    return blockStyle;
  }

  const std::vector<std::string>& getWords() const {
    return words;
  }

  bool isVertical() const {
    return layoutMode ==
           TextLayoutMode::Vertical;
  }

  TextLayoutMode getLayoutMode() const {
    return layoutMode;
  }

  bool isEmpty() override {
    return words.empty();
  }

  /*
   * 這裡不能直接回傳 words.size()。
   *
   * 直排模式的 words 是 glyph 數量，
   * footnote tracking 需要原始 parser word 數量。
   */
  size_t wordCount() const {
    return logicalWordCount;
  }

  /*
   * 橫排：
   *   x、y 是整行基準位置。
   *
   * 直排：
   *   x、y 是整個直排 block 的基準位置，
   *   再加上每個 glyph 的 wordXpos / wordYpos。
   */
  void render(
      const GfxRenderer& renderer,
      int fontId,
      int x,
      int y
  ) const;

  BlockType getType() override {
    return TEXT_BLOCK;
  }

  bool serialize(FsFile& file) const;

  static std::unique_ptr<TextBlock> deserialize(
      FsFile& file
  );
};