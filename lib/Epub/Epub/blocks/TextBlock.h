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

class TextBlock final : public Block {
 private:
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;

  // 橫排時可以為空；直排時必須與 words 數量相同。
  std::vector<int16_t> wordYpos;

  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  TextLayoutMode layoutMode =
      TextLayoutMode::Horizontal;

  public:
  // 保留原本橫排 constructor。
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
        layoutMode(TextLayoutMode::Horizontal) {}

  // 新增直排 constructor。
  explicit TextBlock(
      std::vector<std::string> words,
      std::vector<int16_t> word_xpos,
      std::vector<int16_t> word_ypos,
      std::vector<EpdFontFamily::Style> word_styles,
      const BlockStyle& blockStyle,
      const TextLayoutMode layoutMode
  )
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordYpos(std::move(word_ypos)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle),
        layoutMode(layoutMode) {}

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

  bool isEmpty() override {
    return words.empty();
  }

  size_t wordCount() const {
    return words.size();
  }

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