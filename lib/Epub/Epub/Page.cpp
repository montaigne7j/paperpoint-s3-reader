#include "Page.h"

#include <Arduino.h>
#include <Logging.h>
#include <Serialization.h>

#include "PageRenderProfiler.h"

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

void summarizeTextBlock(
    const TextBlock& block,
    uint32_t& outBytes,
    uint32_t& outGlyphs
) {
  outBytes = 0;
  outGlyphs = 0;
  for (const auto& word : block.getWords()) {
    outBytes += static_cast<uint32_t>(word.size());
    outGlyphs += countUtf8Codepoints(word);
  }
}

}  // namespace

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize ImageBlock
  return imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto ib = ImageBlock::deserialize(file);
  return std::unique_ptr<PageImage>(new PageImage(std::move(ib), xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  const bool profiling = PageRenderProfiler::isEnabled();
  const unsigned long pageStart = millis();
  unsigned long lineTotal = 0;
  unsigned long imageTotal = 0;
  unsigned long slowestMs = 0;
  uint16_t slowestIndex = 0;
  PageElementTag slowestTag = TAG_PageLine;

  if (profiling) {
    LOG_DBG(
        "PGE",
        "page start: elements=%u font=%d offset=(%d,%d)",
        static_cast<unsigned>(elements.size()),
        fontId,
        xOffset,
        yOffset
    );
  }

  for (size_t i = 0; i < elements.size(); ++i) {
    const auto& element = elements[i];
    const PageElementTag tag = element->getTag();
    const unsigned long t0 = millis();

    element->render(renderer, fontId, xOffset, yOffset);

    const unsigned long dt = millis() - t0;
    if (tag == TAG_PageImage) {
      imageTotal += dt;
    } else {
      lineTotal += dt;
    }
    if (dt > slowestMs) {
      slowestMs = dt;
      slowestIndex = static_cast<uint16_t>(i);
      slowestTag = tag;
    }

    if (!profiling) {
      continue;
    }

    if (tag == TAG_PageLine) {
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = line.getBlock();
      if (block) {
        uint32_t bytes = 0;
        uint32_t glyphs = 0;
        summarizeTextBlock(*block, bytes, glyphs);
        LOG_DBG(
            "PGE",
            "element %u/%u text layout=%s entries=%u logical=%u bytes=%u glyphs=%u pos=(%d,%d) time=%lums",
            static_cast<unsigned>(i + 1),
            static_cast<unsigned>(elements.size()),
            block->isVertical() ? "vertical" : "horizontal",
            static_cast<unsigned>(block->getWords().size()),
            static_cast<unsigned>(block->wordCount()),
            static_cast<unsigned>(bytes),
            static_cast<unsigned>(glyphs),
            element->xPos,
            element->yPos,
            dt
        );
      } else {
        LOG_DBG(
            "PGE",
            "element %u/%u text null-block pos=(%d,%d) time=%lums",
            static_cast<unsigned>(i + 1),
            static_cast<unsigned>(elements.size()),
            element->xPos,
            element->yPos,
            dt
        );
      }
    } else if (tag == TAG_PageImage) {
      const auto& image = static_cast<const PageImage&>(*element);
      LOG_DBG(
          "PGE",
          "element %u/%u image size=%dx%d pos=(%d,%d) time=%lums",
          static_cast<unsigned>(i + 1),
          static_cast<unsigned>(elements.size()),
          image.getImageBlock().getWidth(),
          image.getImageBlock().getHeight(),
          element->xPos,
          element->yPos,
          dt
      );
    } else {
      LOG_DBG(
          "PGE",
          "element %u/%u unknown tag=%u pos=(%d,%d) time=%lums",
          static_cast<unsigned>(i + 1),
          static_cast<unsigned>(elements.size()),
          static_cast<unsigned>(tag),
          element->xPos,
          element->yPos,
          dt
      );
    }
  }

  if (profiling) {
    LOG_DBG(
        "PGE",
        "page done: elements=%u total=%lums textTotal=%lums imageTotal=%lums slowestIndex=%u slowestTag=%u slowest=%lums",
        static_cast<unsigned>(elements.size()),
        millis() - pageStart,
        lineTotal,
        imageTotal,
        static_cast<unsigned>(slowestIndex),
        static_cast<unsigned>(slowestTag),
        slowestMs
    );
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    // Use getTag() method to determine type
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(file, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      page->elements.push_back(std::move(pi));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount;
  serialization::readPod(file, fnCount);
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  return page;
}
