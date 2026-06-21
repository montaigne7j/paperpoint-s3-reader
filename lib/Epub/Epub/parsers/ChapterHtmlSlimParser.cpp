#include "ChapterHtmlSlimParser.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include "../../Epub.h"
#include "../Page.h"
#include "../converters/ImageDecoderFactory.h"
#include "../converters/ImageToFramebufferDecoder.h"
#include "../htmlEntities.h"
#include <algorithm>
#include <cctype>

#include "../../../../src/CrossPointSettings.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;  // 10KB
constexpr size_t PARSE_BUFFER_SIZE = 1024;

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr int NUM_UNDERLINE_TAGS = sizeof(UNDERLINE_TAGS) / sizeof(UNDERLINE_TAGS[0]);

const char* IMAGE_TAGS[] = {"img", "image", "svg:image"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

static bool isVerticalLayoutEnabled() {
  return SETTINGS.readingLayout ==
         CrossPointSettings::VERTICAL_LAYOUT;
}

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// Used by the image-source helpers below and by the parser itself.
const char* getAttribute(const XML_Char** atts, const char* attrName);

namespace {

int hexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string decodePercentEscapes(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const int high = hexDigitValue(input[i + 1]);
      const int low = hexDigitValue(input[i + 2]);
      if (high >= 0 && low >= 0) {
        output.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    output.push_back(input[i]);
  }
  return output;
}

std::string firstSrcsetCandidate(const std::string& srcset) {
  const size_t comma = srcset.find(',');
  std::string candidate = srcset.substr(0, comma);
  const size_t first = candidate.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  candidate.erase(0, first);
  const size_t whitespace = candidate.find_first_of(" \t\r\n");
  if (whitespace != std::string::npos) candidate.erase(whitespace);
  return candidate;
}

std::string getImageSource(const XML_Char** atts) {
  if (atts == nullptr) return {};

  // Prefer the standard XHTML/SVG source attributes. Lazy-load metadata is
  // only a fallback because some EPUB generators leave stale or web-only
  // values in data-src while the packaged PNG/JPEG path in src is correct.
  static const char* ATTRIBUTES[] = {"src", "href", "xlink:href", "data-src",
                                     "data-original", "data-original-src",
                                     "data-lazy-src", "data-echo"};
  for (const char* attribute : ATTRIBUTES) {
    const char* value = getAttribute(atts, attribute);
    if (value != nullptr && value[0] != '\0') return value;
  }

  const char* srcset = getAttribute(atts, "data-srcset");
  if (srcset == nullptr || srcset[0] == '\0') srcset = getAttribute(atts, "srcset");
  return srcset == nullptr ? std::string{} : firstSrcsetCandidate(srcset);
}

std::string resolveImagePath(const std::string& contentBase, const std::string& source) {
  if (source.empty() || source.rfind("data:", 0) == 0) return {};

  std::string clean = source;
  const size_t suffix = clean.find_first_of("?#");
  if (suffix != std::string::npos) clean.erase(suffix);
  clean = decodePercentEscapes(clean);
  std::replace(clean.begin(), clean.end(), '\\', '/');
  if (clean.empty() || clean.find("://") != std::string::npos || clean.rfind("//", 0) == 0) return {};

  // EPUB ZIP item names are root-relative without a leading slash. Relative
  // references are resolved against the XHTML file's directory.
  bool rootRelative = clean.front() == '/';
  if (rootRelative) clean.erase(0, 1);

  // Some EPUB generators write package-root paths such as
  // "OEBPS/Images/pic.jpg" without the leading slash. Avoid turning those
  // into "OEBPS/Text/OEBPS/Images/pic.jpg".
  if (!rootRelative) {
    const size_t rootEnd = contentBase.find('/');
    if (rootEnd != std::string::npos) {
      const std::string packageRoot = contentBase.substr(0, rootEnd + 1);
      rootRelative = clean.rfind(packageRoot, 0) == 0;
    }
  }

  return FsHelpers::normalisePath(rootRelative ? clean : contentBase + clean);
}

std::string lowerExtension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string extension = path.substr(dot);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

bool isSupportedRasterExtension(const std::string& extension) {
  return extension == ".jpg" || extension == ".jpeg" || extension == ".png";
}

std::string sniffRasterExtension(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("EHP", path, file)) return {};

  uint8_t header[12] = {};
  const int bytesRead = file.read(header, sizeof(header));
  file.close();

  if (bytesRead >= 8 && header[0] == 0x89 && header[1] == 0x50 &&
      header[2] == 0x4e && header[3] == 0x47 && header[4] == 0x0d &&
      header[5] == 0x0a && header[6] == 0x1a && header[7] == 0x0a) {
    return ".png";
  }
  if (bytesRead >= 3 && header[0] == 0xff && header[1] == 0xd8 &&
      header[2] == 0xff) {
    return ".jpg";
  }
  return {};
}

bool extractSupportedRasterImage(Epub* epub, const std::string& resolvedPath,
                                 const std::string& cacheBasePath,
                                 std::string* cachedImagePath) {
  if (epub == nullptr || cachedImagePath == nullptr || resolvedPath.empty()) return false;

  std::string extension = lowerExtension(resolvedPath);
  const bool knownExtension = isSupportedRasterExtension(extension);
  if (!knownExtension) extension = ".img";

  std::string temporaryPath = cacheBasePath + extension;
  Storage.remove(temporaryPath.c_str());

  FsFile cachedImageFile;
  bool extractSuccess = false;
  if (Storage.openFileForWrite("EHP", temporaryPath, cachedImageFile)) {
    extractSuccess = epub->readItemContentsToStream(resolvedPath, cachedImageFile, 4096);
    cachedImageFile.flush();
    cachedImageFile.close();
    delay(20);
  }

  if (!extractSuccess) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  const std::string detectedExtension = sniffRasterExtension(temporaryPath);
  if (!knownExtension && detectedExtension.empty()) {
    LOG_ERR("EHP", "Unsupported image data: %s", resolvedPath.c_str());
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  const std::string canonicalDeclared = extension == ".jpeg" ? ".jpg" : extension;
  if (!detectedExtension.empty() && detectedExtension != canonicalDeclared) {
    // The resource has no suffix or is mislabeled (for example PNG bytes in a
    // file named .jpg). Give the cache file the extension expected by the
    // decoder factory.
    const std::string finalPath = cacheBasePath + detectedExtension;
    Storage.remove(finalPath.c_str());
    if (!Storage.rename(temporaryPath.c_str(), finalPath.c_str())) {
      LOG_ERR("EHP", "Failed to rename detected image cache: %s", finalPath.c_str());
      Storage.remove(temporaryPath.c_str());
      return false;
    }
    temporaryPath = finalPath;
  }

  *cachedImagePath = temporaryPath;
  return true;
}

}  // namespace

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char* getAttribute(const XML_Char** atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, NUM_HEADER_TAGS) || matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS);
}

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

// Update effective bold/italic/underline based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveUnderline =
      currentCssStyle.hasTextDecoration() && currentCssStyle.textDecoration == CssTextDecoration::Underline;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    if (entry.hasUnderline) {
      effectiveUnderline = entry.underline;
    }
  }
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;
  const bool isUnderline = underlineUntilDepth < depth || effectiveUnderline;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  if (isUnderline) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::UNDERLINE);
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(
    partWordBuffer,
    fontStyle,
    false,
    nextWordContinues,
    nextWordNoSpace
  );

  partWordBufferIndex = 0;
  nextWordContinues = false;
  nextWordNoSpace = false;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  nextWordContinues = false;
  nextWordNoSpace = false;
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // Merge with existing block style to accumulate CSS styling from parent block elements.
      // This handles cases like <div style="margin-bottom:2em"><h1>text</h1></div> where the
      // div's margin should be preserved, even though it has no direct text content.
      currentTextBlock->setBlockStyle(currentTextBlock->getBlockStyle().getCombinedBlockStyle(blockStyle));

      if (!pendingAnchorId.empty()) {
        anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
        pendingAnchorId.clear();
      }
      return;
    }

    makePages();
  }
  // Record deferred anchor after previous block is flushed
  if (!pendingAnchorId.empty()) {
    anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
    pendingAnchorId.clear();
  }
  currentTextBlock.reset(new ParsedText(extraParagraphSpacing, hyphenationEnabled, blockStyle));
  wordsExtractedInBlock = 0;
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  // Extract class, style, and id attributes
  std::string classAttr;
  std::string styleAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        // Defer recording until startNewTextBlock, after previous block is flushed to pages
        self->pendingAnchorId = atts[i + 1];
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Special handling for tables/cells: flatten into per-cell paragraphs with a prefixed header.
  if (strcmp(name, "table") == 0) {
    // skip nested tables
    if (self->tableDepth > 0) {
      self->tableDepth += 1;
      return;
    }

    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->tableDepth += 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "tr") == 0) {
    self->tableRowIndex += 1;
    self->tableColIndex = 0;
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->tableColIndex += 1;

    auto tableCellBlockStyle = BlockStyle();
    tableCellBlockStyle.textAlignDefined = true;
    const auto align = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                           ? CssTextAlign::Justify
                           : static_cast<CssTextAlign>(self->paragraphAlignment);
    tableCellBlockStyle.alignment = align;
    self->startNewTextBlock(tableCellBlockStyle);

    const std::string headerText =
        "Tab Row " + std::to_string(self->tableRowIndex) + ", Cell " + std::to_string(self->tableColIndex) + ":";
    StyleStackEntry headerStyle;
    headerStyle.depth = self->depth;
    headerStyle.hasBold = true;
    headerStyle.bold = false;
    headerStyle.hasItalic = true;
    headerStyle.italic = true;
    headerStyle.hasUnderline = true;
    headerStyle.underline = false;
    self->inlineStyleStack.push_back(headerStyle);
    self->updateEffectiveInlineStyle();
    self->characterData(userData, headerText.c_str(), static_cast<int>(headerText.length()));
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->nextWordContinues = false;
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();

    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string src = getImageSource(atts);
    std::string alt;
    if (atts != nullptr) {
      const char* altValue = getAttribute(atts, "alt");
      if (altValue != nullptr) alt = altValue;

      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      if (!src.empty() && self->imageRendering != 1) {
        LOG_DBG("EHP", "Found image: src=%s", src.c_str());

        {
          // Resolve standard, lazy-loaded, srcset, and SVG image references.
          const std::string resolvedPath = resolveImagePath(self->contentBase, src);
          LOG_DBG("EHP", "Resolved image: %s -> %s", src.c_str(), resolvedPath.c_str());

          if (!resolvedPath.empty()) {
            // Extract first, then inspect the bytes when the EPUB uses no file
            // extension or a misleading extension. This covers many generator-
            // produced EPUBs whose JPEG/PNG resources are named as generic items.
            const std::string cacheBasePath =
                self->imageBasePath + std::to_string(self->imageCounter++);
            std::string cachedImagePath;
            bool extractSuccess = false;

            // SD cards and ZIP streams can occasionally fail while a rendered
            // page still has hot file/cache state. Retry before degrading the
            // chapter to alt text. The destination is removed by the helper on
            // each failed attempt, so retries never consume a partial image.
            for (int attempt = 0; attempt < 3 && !extractSuccess; ++attempt) {
              if (attempt > 0) {
                LOG_DBG("EHP", "Retrying image extraction (%d/3): %s", attempt + 1, resolvedPath.c_str());
                delay(40 * attempt);
              }
              extractSuccess = extractSupportedRasterImage(
                  self->epub.get(), resolvedPath, cacheBasePath, &cachedImagePath);
            }

            // Keep compatibility with the original parser's simple path join.
            // A few EPUBs use package-relative paths that are ambiguous after
            // normalization; retry the historical resolution before falling
            // back to alt text.
            if (!extractSuccess) {
              const std::string legacyPath =
                  FsHelpers::normalisePath(self->contentBase + src);
              if (!legacyPath.empty() && legacyPath != resolvedPath) {
                LOG_DBG("EHP", "Retrying legacy image path: %s", legacyPath.c_str());
                for (int attempt = 0; attempt < 3 && !extractSuccess; ++attempt) {
                  if (attempt > 0) delay(40 * attempt);
                  extractSuccess = extractSupportedRasterImage(
                      self->epub.get(), legacyPath, cacheBasePath, &cachedImagePath);
                }
              }
            }

            if (extractSuccess) {
              // Getting PNG/JPEG dimensions also opens and parses the extracted
              // file. Retry this independently to tolerate short SD timing faults.
              ImageDimensions dims = {0, 0};
              ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(cachedImagePath);
              bool dimensionsReady = false;
              for (int attempt = 0; decoder && attempt < 3 && !dimensionsReady; ++attempt) {
                if (attempt > 0) {
                  LOG_DBG("EHP", "Retrying image dimensions (%d/3): %s", attempt + 1, cachedImagePath.c_str());
                  delay(30 * attempt);
                }
                dims = {0, 0};
                dimensionsReady = decoder->getDimensions(cachedImagePath, dims);
              }
              if (dimensionsReady) {
                LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);

                int displayWidth = 0;
                int displayHeight = 0;
                const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
                CssStyle imgStyle = self->cssParser ? self->cssParser->resolveStyle("img", classAttr) : CssStyle{};
                // Merge inline style (e.g. style="height: 2em") so it overrides stylesheet rules
                if (!styleAttr.empty()) {
                  imgStyle.applyOver(CssParser::parseInlineStyle(styleAttr));
                }
                const bool hasCssHeight = imgStyle.hasImageHeight();
                const bool hasCssWidth = imgStyle.hasImageWidth();

                if (hasCssHeight && hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Both CSS height and width set: resolve both, then clamp to viewport preserving requested ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  displayWidth = static_cast<int>(
                      imgStyle.imageWidth.toPixels(emSize, static_cast<float>(self->viewportWidth)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  if (displayWidth < 1) displayWidth = 1;
                  if (displayWidth > self->viewportWidth || displayHeight > self->viewportHeight) {
                    float scaleX = (displayWidth > self->viewportWidth)
                                       ? static_cast<float>(self->viewportWidth) / displayWidth
                                       : 1.0f;
                    float scaleY = (displayHeight > self->viewportHeight)
                                       ? static_cast<float>(self->viewportHeight) / displayHeight
                                       : 1.0f;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
                    displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", displayWidth, displayHeight);
                } else if (hasCssHeight && !hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Use CSS height (resolve % against viewport height) and derive width from aspect ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  displayWidth =
                      static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayWidth > self->viewportWidth) {
                    displayWidth = self->viewportWidth;
                    // Rescale height to preserve aspect ratio when width is clamped
                    displayHeight =
                        static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  if (displayWidth < 1) displayWidth = 1;
                  LOG_DBG("EHP", "Display size from CSS height: %dx%d", displayWidth, displayHeight);
                } else if (hasCssWidth && !hasCssHeight && dims.width > 0 && dims.height > 0) {
                  // Use CSS width (resolve % against viewport width) and derive height from aspect ratio
                  displayWidth = static_cast<int>(
                      imgStyle.imageWidth.toPixels(emSize, static_cast<float>(self->viewportWidth)) + 0.5f);
                  if (displayWidth > self->viewportWidth) displayWidth = self->viewportWidth;
                  if (displayWidth < 1) displayWidth = 1;
                  displayHeight =
                      static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayHeight < 1) displayHeight = 1;
                  LOG_DBG("EHP", "Display size from CSS width: %dx%d", displayWidth, displayHeight);
                } else {
                  // Scale to fit viewport while maintaining aspect ratio
                  int maxWidth = self->viewportWidth;
                  int maxHeight = self->viewportHeight;
                  float scaleX = (dims.width > maxWidth) ? (float)maxWidth / dims.width : 1.0f;
                  float scaleY = (dims.height > maxHeight) ? (float)maxHeight / dims.height : 1.0f;
                  float scale = (scaleX < scaleY) ? scaleX : scaleY;
                  if (scale > 1.0f) scale = 1.0f;

                  displayWidth = (int)(dims.width * scale);
                  displayHeight = (int)(dims.height * scale);
                  LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", displayWidth, displayHeight, scale);
                }

                if (isVerticalLayoutEnabled()) {
                  /*
                  * 直排 MVP：
                  * 圖片使用獨立頁面，避免 currentPageNextY 與
                  * currentPageNextX 兩套座標系互相重疊。
                  */

                  // 先送出圖片前尚未 flush 的文字。
                  if (self->partWordBufferIndex > 0) {
                    self->flushPartWordBuffer();
                  }

                  // 將圖片前的直排文字完成排版。
                  if (self->currentTextBlock &&
                      !self->currentTextBlock->isEmpty()) {
                    self->makePages();
                  }

                  // 若目前已有文字欄，先完成目前頁。
                  if (self->currentPage &&
                      !self->currentPage->elements.empty()) {
                    self->completePageFn(
                        std::move(self->currentPage)
                    );

                    ++self->completedPageCount;
                    self->currentPage.reset();
                  }

                  // 建立獨立圖片頁。
                  self->currentPage.reset(new Page());

                  if (!self->currentPage) {
                    LOG_ERR(
                        "EHP",
                        "Failed to create vertical image page"
                    );
                    return;
                  }

                  auto imageBlock =
                      std::make_shared<ImageBlock>(
                          cachedImagePath,
                          displayWidth,
                          displayHeight
                      );

                  if (!imageBlock) {
                    LOG_ERR(
                        "EHP",
                        "Failed to create vertical ImageBlock"
                    );
                    return;
                  }

                  // 圖片水平與垂直置中。
                  const int imageX =
                      std::max(
                          0,
                          (self->viewportWidth - displayWidth) / 2
                      );

                  const int imageY =
                      std::max(
                          0,
                          (self->viewportHeight - displayHeight) / 2
                      );

                  auto pageImage =
                      std::make_shared<PageImage>(
                          imageBlock,
                          static_cast<int16_t>(imageX),
                          static_cast<int16_t>(imageY)
                      );

                  if (!pageImage) {
                    LOG_ERR(
                        "EHP",
                        "Failed to create vertical PageImage"
                    );
                    return;
                  }

                  self->currentPage->elements.push_back(
                      pageImage
                  );

                  // 圖片頁立即完成，後續文字從新頁開始。
                  self->completePageFn(
                      std::move(self->currentPage)
                  );

                  ++self->completedPageCount;

                  self->currentPage.reset();

                  self->currentPageNextY = 0;
                  self->currentPageNextX = -1;

                  self->depth += 1;
                  return;
                }

                // Create page for image - only break if image won't fit remaining space
                if (self->currentPage && !self->currentPage->elements.empty() &&
                    (self->currentPageNextY + displayHeight > self->viewportHeight)) {
                  self->completePageFn(std::move(self->currentPage));
                  self->completedPageCount++;
                  self->currentPage.reset(new Page());
                  if (!self->currentPage) {
                    LOG_ERR("EHP", "Failed to create new page");
                    return;
                  }
                  self->currentPageNextY = 0;
                } else if (!self->currentPage) {
                  self->currentPage.reset(new Page());
                  if (!self->currentPage) {
                    LOG_ERR("EHP", "Failed to create initial page");
                    return;
                  }
                  self->currentPageNextY = 0;
                }

                // Create ImageBlock and add to page
                auto imageBlock = std::make_shared<ImageBlock>(cachedImagePath, displayWidth, displayHeight);
                if (!imageBlock) {
                  LOG_ERR("EHP", "Failed to create ImageBlock");
                  return;
                }
                int xPos = (self->viewportWidth - displayWidth) / 2;
                auto pageImage = std::make_shared<PageImage>(imageBlock, xPos, self->currentPageNextY);
                if (!pageImage) {
                  LOG_ERR("EHP", "Failed to create PageImage");
                  return;
                }
                self->currentPage->elements.push_back(pageImage);
                self->currentPageNextY += displayHeight;

                self->depth += 1;
                return;
              } else {
                LOG_ERR("EHP", "Failed to get image dimensions");
                Storage.remove(cachedImagePath.c_str());
              }
            } else {
              LOG_ERR("EHP", "Failed to extract or identify image: %s", resolvedPath.c_str());
            }
          } else {
            LOG_ERR("EHP", "Invalid image reference: src=%s", src.c_str());
          }
        }
      }

      if (self->imageRendering == CrossPointSettings::IMAGES_PLACEHOLDER) {
        LOG_DBG("EHP", "Image placeholder requested by setting: %s", src.c_str());
      } else if (self->imageRendering == CrossPointSettings::IMAGES_DISPLAY && !src.empty()) {
        // Do not let a transient failure during silent pre-indexing become a
        // permanent degraded section cache.  Section::createSectionFile() can
        // reject the build and retry it later in the foreground.
        self->imageLoadFailure = true;
        LOG_ERR("EHP", "Image load failed in display mode; marking section cache as degraded: %s", src.c_str());
      }

      // Fallback to alt text if image processing fails or placeholder mode is selected.
      // This log is deliberately emitted at the point where [Image: ...] is
      // created, so regressions can be distinguished from stale page rendering.
      if (!alt.empty()) {
        LOG_ERR("EHP", "Using image alt fallback: src=%s mode=%u alt=%s",
                src.c_str(), static_cast<unsigned>(self->imageRendering), alt.c_str());
        alt = "[Image: " + alt + "]";
        self->startNewTextBlock(centeredBlockStyle);
        self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
        self->depth += 1;
        self->characterData(userData, alt.c_str(), alt.length());
        // Skip any child content (skip until parent as we pre-advanced depth above)
        self->skipUntilDepth = self->depth - 1;
        return;
      }

      // No alt text, skip
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      strncpy(self->currentFootnoteLinkHref, href, sizeof(self->currentFootnoteLinkHref) - 1);
      self->currentFootnoteLinkHref[sizeof(self->currentFootnoteLinkHref) - 1] = '\0';
      self->currentFootnoteLinkText[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;

      // Apply underline style to visually indicate the link
      self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
      StyleStackEntry entry;
      entry.depth = self->depth;
      entry.hasUnderline = true;
      entry.underline = true;
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  // Compute CSS style for this element
  CssStyle cssStyle;
  if (self->cssParser) {
    // Get combined tag + class styles
    cssStyle = self->cssParser->resolveStyle(name, classAttr);
    // Merge inline style (highest priority)
    if (!styleAttr.empty()) {
      CssStyle inlineStyle = CssParser::parseInlineStyle(styleAttr);
      cssStyle.applyOver(inlineStyle);
    }
  }

  const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
  const auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->viewportWidth);

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->viewportWidth);
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign()) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    self->startNewTextBlock(headerBlockStyle);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      self->startNewTextBlock(self->currentTextBlock->getBlockStyle());
    } else {
      self->currentCssStyle = cssStyle;
      self->startNewTextBlock(userAlignmentBlockStyle);
      self->updateEffectiveInlineStyle();

      if (strcmp(name, "li") == 0) {
        self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);
      }
    }
  } else if (matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
    // Push inline style entry for underline tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasUnderline = true;
    entry.underline = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = true;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasUnderline = true;
      entry.underline = cssStyle.textDecoration == CssTextDecoration::Underline;
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasTextDecoration()) {
      entry.hasUnderline = true;
      entry.underline = cssStyle.textDecoration == CssTextDecoration::Underline;
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    // Handle span and other inline elements for CSS styling
    if (cssStyle.hasFontWeight() || cssStyle.hasFontStyle() || cssStyle.hasTextDecoration()) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (cssStyle.hasFontWeight()) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (cssStyle.hasFontStyle()) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      if (cssStyle.hasTextDecoration()) {
        entry.hasUnderline = true;
        entry.underline = cssStyle.textDecoration == CssTextDecoration::Underline;
      }
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Skip content of nested table
  if (self->tableDepth > 1) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Collect footnote link display text (for the number label)
  // Skip whitespace and brackets to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnoteLinkText) - 1) && (i <= end);
         ++i) {
      self->currentFootnoteLinkText[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnoteLinkText[self->currentFootnoteLinkTextLen] = '\0';
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      self->nextWordNoSpace = false;
      // Skip the whitespace char
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      self->flushPartWordBuffer();

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;
      self->flushPartWordBuffer();

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen =
          utf8SafeTruncateBuffer(
              self->partWordBuffer,
              self->partWordBufferIndex
          );

      if (safeLen < self->partWordBufferIndex &&
          safeLen > 0) {

        const int overflow =
            self->partWordBufferIndex - safeLen;

        char saved[4];

        for (int j = 0; j < overflow; ++j) {
          saved[j] =
              self->partWordBuffer[safeLen + j];
        }

        self->partWordBufferIndex = safeLen;
        self->flushPartWordBuffer();
        // 這只是內部 buffer 分段：
        // 不插入空格，但仍允許排版器在這裡換行。
        self->nextWordNoSpace = true;

        // 這只是 buffer 容量切割，不是實際空白。
        // 下一個 token 必須緊接前一個 token。
        // self->nextWordContinues = true;

        for (int j = 0; j < overflow; ++j) {
          self->partWordBuffer[j] = saved[j];
        }

        self->partWordBufferIndex = overflow;
      } else {
        self->flushPartWordBuffer();
        // 這只是內部 buffer 分段：
        // 不插入空格，但仍允許排版器在這裡換行。
        self->nextWordNoSpace = true;

        // 不允許在自動 buffer 切割處插入空格。
        // self->nextWordContinues = true;
      }
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // If we have > 750 words buffered up, perform the layout and consume out all but the last line
  // There should be enough here to build out 1-2 full pages and doing this will free up a lot of
  // memory.
  // Spotted when reading Intermezzo, there are some really long text blocks in there.
  if (self->currentTextBlock->size() > 750) {
    LOG_DBG(
        "EHP",
        "Text block too long, "
        "splitting into multiple pages"
    );

    if (isVerticalLayoutEnabled()) {
      const BlockStyle& blockStyle =
          self->currentTextBlock
              ->getBlockStyle();

      const int topInset =
          std::max<int>(
              0,
              blockStyle.marginTop
          ) +
          std::max<int>(
              0,
              blockStyle.paddingTop
          );

      const int bottomInset =
          std::max<int>(
              0,
              blockStyle.marginBottom
          ) +
          std::max<int>(
              0,
              blockStyle.paddingBottom
          );

      const int totalVerticalInset =
          topInset + bottomInset;

      const uint16_t effectiveHeight =
          totalVerticalInset <
                  static_cast<int>(
                      self->viewportHeight)
              ? static_cast<uint16_t>(
                    self->viewportHeight -
                    totalVerticalInset)
              : self->viewportHeight;

      self->currentTextBlock
          ->layoutAndExtractColumns(
              self->renderer,
              self->fontId,
              effectiveHeight,
              [self](
                  const std::shared_ptr<
                      TextBlock>& textBlock) {
                self->addColumnToPage(
                    textBlock
                );
              }
          );
    } else {
      const int horizontalInset =
          self->currentTextBlock
              ->getBlockStyle()
              .totalHorizontalInset();

      const uint16_t effectiveWidth =
          horizontalInset <
                  static_cast<int>(
                      self->viewportWidth)
              ? static_cast<uint16_t>(
                    self->viewportWidth -
                    horizontalInset)
              : self->viewportWidth;

      self->currentTextBlock
          ->layoutAndExtractLines(
              self->renderer,
              self->fontId,
              effectiveWidth,
              [self](
                  const std::shared_ptr<
                      TextBlock>& textBlock) {
                self->addLineToPage(
                    textBlock
                );
              },
              false
          );
    }
  }
}

void XMLCALL ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const XML_Char* s, const int len) {
  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool willClearBold = self->boldUntilDepth == self->depth - 1;
  const bool willClearItalic = self->italicUntilDepth == self->depth - 1;
  const bool willClearUnderline = self->underlineUntilDepth == self->depth - 1;

  const bool styleWillChange = willPopStyleStack || willClearBold || willClearItalic || willClearUnderline;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);

  if (self->tableDepth > 1 && strcmp(name, "table") == 0) {
    // get rid of all text inside the nested table
    self->partWordBufferIndex = 0;
    self->tableDepth -= 1;
    LOG_DBG("EHP", "nested table detected, get rid of its content");
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag =
        !headerOrBlockTag && !tableStructuralTag && !matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, NUM_BOLD_TAGS) ||
                             matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) ||
                             matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) || self->depth == 1;

    if (shouldFlush) {
      self->flushPartWordBuffer();
      // If closing an inline element, the next word fragment continues the same visual word
      if (isInlineTag) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth -= 1;

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnoteLinkText[0] != '\0' && self->currentFootnoteLinkHref[0] != '\0') {
      FootnoteEntry entry;
      strncpy(entry.number, self->currentFootnoteLinkText, sizeof(entry.number) - 1);
      entry.number[sizeof(entry.number) - 1] = '\0';
      strncpy(entry.href, self->currentFootnoteLinkHref, sizeof(entry.href) - 1);
      entry.href[sizeof(entry.href) - 1] = '\0';
      int wordIndex =
          self->wordsExtractedInBlock + (self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0);
      self->pendingFootnotes.push_back({wordIndex, entry});
    }
    self->insideFootnoteLink = false;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && (strcmp(name, "tr") == 0)) {
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && strcmp(name, "table") == 0) {
    self->tableDepth -= 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    self->nextWordContinues = false;
  }

  // Leaving bold tag
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic tag
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Leaving underline tag
  if (self->underlineUntilDepth == self->depth) {
    self->underlineUntilDepth = INT_MAX;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag) {
    self->currentCssStyle.reset();
    self->updateEffectiveInlineStyle();

    // Reset alignment on empty text blocks to prevent stale alignment from bleeding
    // into the next sibling element. This fixes issue #1026 where an empty <h1> (default
    // Center) followed by an image-only <p> causes Center to persist through the chain
    // of empty block reuse into subsequent text paragraphs.
    // Margins/padding are preserved so parent element spacing still accumulates correctly.
    if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
      auto style = self->currentTextBlock->getBlockStyle();
      style.textAlignDefined = false;
      style.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                            ? CssTextAlign::Justify
                            : static_cast<CssTextAlign>(self->paragraphAlignment);
      self->currentTextBlock->setBlockStyle(style);
    }
  }
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  // Resolve None sentinel to Justify for initial block (no CSS context yet)
  const auto align = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                         ? CssTextAlign::Justify
                         : static_cast<CssTextAlign>(this->paragraphAlignment);
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);

  XML_Parser parser = XML_ParserCreate(nullptr);
  int done;

  if (!parser) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }

  // Handle HTML entities (like &nbsp;) that aren't in XML spec or DTD
  // Using DefaultHandlerExpand preserves normal entity expansion from DOCTYPE
  XML_SetDefaultHandlerExpand(parser, defaultHandlerExpand);

  FsFile file;
  if (!Storage.openFileForRead("EHP", filepath, file)) {
    destroyXmlParser(parser);
    return false;
  }

  // Get file size to decide whether to show indexing popup.
  const size_t totalFileSize = file.size();
  const bool showPopup = popupFn && totalFileSize >= MIN_SIZE_FOR_POPUP;
  if (showPopup) {
    popupFn();
    if (popupProgressFn) {
      popupProgressFn(0);
    }
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);

  // Compute the time taken to parse and build pages
  const uint32_t chapterStartTime = millis();
  int lastPopupProgress = -1;
  do {
    void* const buf = XML_GetBuffer(parser, PARSE_BUFFER_SIZE);
    if (!buf) {
      LOG_ERR("EHP", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      file.close();
      return false;
    }

    const size_t len = file.read(buf, PARSE_BUFFER_SIZE);

    if (len == 0 && file.available() > 0) {
      LOG_ERR("EHP", "File read error");
      destroyXmlParser(parser);
      file.close();
      return false;
    }

    done = file.available() == 0;

    if (showPopup && popupProgressFn && totalFileSize > 0) {
      const int progress = static_cast<int>((static_cast<uint64_t>(file.position()) * 100ULL) / totalFileSize);
      if (progress >= lastPopupProgress + 4 || (done && progress != lastPopupProgress)) {
        popupProgressFn(progress);
        lastPopupProgress = progress;
      }
    }

    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      LOG_ERR("EHP", "Parse error at line %lu:\n%s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      file.close();
      return false;
    }
  } while (!done);
  LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - chapterStartTime);
  if (showPopup && popupProgressFn) {
    popupProgressFn(100);
  }

  destroyXmlParser(parser);
  file.close();

  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();
    if (!pendingAnchorId.empty()) {
      anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
      pendingAnchorId.clear();
    }
    completePageFn(std::move(currentPage));
    completedPageCount++;
    currentPage.reset();
    currentTextBlock.reset();
  }

  return true;
}

void ChapterHtmlSlimParser::addLineToPage(
  std::shared_ptr<TextBlock> line
) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    completePageFn(std::move(currentPage));
    completedPageCount++;
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset
  const int16_t xOffset = line->getBlockStyle().leftInset();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::addColumnToPage(
    std::shared_ptr<TextBlock> column
) {
  if (!column) {
    return;
  }

  constexpr int verticalColumnGap = 6;
  const int columnAdvance =
      std::max(
          renderer.getLineHeight(fontId) + verticalColumnGap,
          static_cast<int>(
              renderer.getLineHeight(fontId) *
              lineCompression
          ) + verticalColumnGap
      );

  /*
   * 沒有目前頁面時，建立新頁。
   *
   * 第一欄的左上角位於：
   * viewportWidth - columnAdvance
   *
   * 因此第一欄會貼近右側，但不會超出畫面。
   */
  if (!currentPage) {
    currentPage.reset(new Page());

    currentPageNextY = 0;
    currentPageNextX =
        static_cast<int16_t>(
            viewportWidth - columnAdvance
        );
  }

  /*
   * X 已小於 0，代表目前頁面已沒有空間放下一欄。
   * 完成目前頁面，再從新頁右側重新開始。
   *
   * currentPageNextX == -1 也可能代表目前頁面包含圖片；
   * MVP 會讓後續直排文字從下一頁開始。
   */
  if (currentPageNextX < 0) {
    completePageFn(
        std::move(currentPage)
    );

    ++completedPageCount;

    currentPage.reset(new Page());

    currentPageNextY = 0;
    currentPageNextX =
        static_cast<int16_t>(
            viewportWidth - columnAdvance
        );
  }

  /*
   * Footnote tracking 仍使用 parser 的 logical word count，
   * 不能使用拆成 Unicode glyph 後的 glyph 數量。
   */
  wordsExtractedInBlock +=
      static_cast<int>(
          column->wordCount()
      );

  auto footnoteIt =
      pendingFootnotes.begin();

  while (footnoteIt !=
             pendingFootnotes.end() &&
         footnoteIt->first <=
             wordsExtractedInBlock) {
    currentPage->addFootnote(
        footnoteIt->second.number,
        footnoteIt->second.href
    );

    ++footnoteIt;
  }

  pendingFootnotes.erase(
      pendingFootnotes.begin(),
      footnoteIt
  );

  /*
   * 直排欄頂端仍保留 CSS 的 top margin / padding。
   * 負值先忽略，避免欄位跑出畫面。
   */
  const BlockStyle& blockStyle =
      column->getBlockStyle();

  const int topOffset =
      std::max<int>(0, blockStyle.marginTop) +
      std::max<int>(0, blockStyle.paddingTop);

  currentPage->elements.push_back(
      std::make_shared<PageLine>(
          column,
          currentPageNextX,
          static_cast<int16_t>(topOffset)
      )
  );

  // 下一欄移到左側。
  currentPageNextX -=
      static_cast<int16_t>(
          columnAdvance
      );
}

void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) {
    LOG_ERR(
        "EHP",
        "!! No text block to make pages for !!"
    );
    return;
  }

  constexpr int verticalColumnGap = 6;
  const int baseLineAdvance =
      std::max(
          1,
          static_cast<int>(
              renderer.getLineHeight(fontId) *
              lineCompression
          )
      );
  const int lineOrColumnAdvance =
      isVerticalLayoutEnabled()
          ? std::max(renderer.getLineHeight(fontId) + verticalColumnGap,
                     baseLineAdvance + verticalColumnGap)
          : baseLineAdvance;

  const BlockStyle& blockStyle =
      currentTextBlock->getBlockStyle();

  /*
   * 建立初始頁面。
   */
  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;

    if (isVerticalLayoutEnabled()) {
      currentPageNextX =
          static_cast<int16_t>(
              viewportWidth -
              lineOrColumnAdvance
          );
    } else {
      currentPageNextX = -1;
    }
  }

  /*
   * =========================
   * 直排路徑
   * =========================
   */
  if (isVerticalLayoutEnabled()) {
    const int topInset =
        std::max<int>(0, blockStyle.marginTop) +
        std::max<int>(0, blockStyle.paddingTop);

    const int bottomInset =
        std::max<int>(0, blockStyle.marginBottom) +
        std::max<int>(0, blockStyle.paddingBottom);

    const int totalVerticalInset =
        topInset + bottomInset;

    const uint16_t effectiveHeight =
        totalVerticalInset <
                static_cast<int>(
                    viewportHeight)
            ? static_cast<uint16_t>(
                  viewportHeight -
                  totalVerticalInset)
            : viewportHeight;

    currentTextBlock
        ->layoutAndExtractColumns(
            renderer,
            fontId,
            effectiveHeight,
            [this](
                const std::shared_ptr<
                    TextBlock>& textBlock) {
              addColumnToPage(textBlock);
            }
        );

    /*
     * 正常情況由 addColumnToPage()
     * 依 word index 分配 footnote。
     * 此處保留原本 fallback。
     */
    if (!pendingFootnotes.empty() &&
        currentPage) {
      for (const auto& [idx, fn] :
           pendingFootnotes) {
        currentPage->addFootnote(
            fn.number,
            fn.href
        );
      }

      pendingFootnotes.clear();
    }

    /*
     * 段落之間保留半欄空間。
     *
     * 第一版先使用 X 間距表示段落區隔；
     * 之後再做直排首行縮排。
     */
    if (extraParagraphSpacing) {
      currentPageNextX -=
          static_cast<int16_t>(
              std::max(
                  1,
                  lineOrColumnAdvance / 2
              )
          );
    }

    return;
  }

  /*
   * =========================
   * 原本橫排路徑
   * =========================
   */

  // Apply top spacing before the paragraph.
  if (blockStyle.marginTop > 0) {
    currentPageNextY +=
        blockStyle.marginTop;
  }

  if (blockStyle.paddingTop > 0) {
    currentPageNextY +=
        blockStyle.paddingTop;
  }

  const int horizontalInset =
      blockStyle.totalHorizontalInset();

  const uint16_t effectiveWidth =
      horizontalInset <
              static_cast<int>(
                  viewportWidth)
          ? static_cast<uint16_t>(
                viewportWidth -
                horizontalInset)
          : viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer,
      fontId,
      effectiveWidth,
      [this](
          const std::shared_ptr<
              TextBlock>& textBlock) {
        addLineToPage(textBlock);
      }
  );

  if (!pendingFootnotes.empty() &&
      currentPage) {
    for (const auto& [idx, fn] :
         pendingFootnotes) {
      currentPage->addFootnote(
          fn.number,
          fn.href
      );
    }

    pendingFootnotes.clear();
  }

  if (blockStyle.marginBottom > 0) {
    currentPageNextY +=
        blockStyle.marginBottom;
  }

  if (blockStyle.paddingBottom > 0) {
    currentPageNextY +=
        blockStyle.paddingBottom;
  }

  if (extraParagraphSpacing) {
    currentPageNextY +=
        lineOrColumnAdvance / 2;
  }
}
