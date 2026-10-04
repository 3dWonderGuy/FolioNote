#pragma once

#include <string>
#include <vector>
#include "core/md_engine/md_types.hpp"

namespace Folio {

/**
 * @brief High-speed CommonMark and GitHub Flavored Markdown (GFM) parser.
 * Converts raw Markdown text into a structured AST of MdBlock and MdSpan nodes.
 */
class MdParser {
public:
    /**
     * @brief Parses a complete Markdown document into layout blocks.
     * @param markdown Raw UTF-8 Markdown text.
     * @return Vector of parsed structural block elements.
     */
    static std::vector<MdBlock> ParseDocument(const std::string& markdown);

    /**
     * @brief Parses inline markdown tokens into formatted spans.
     * @param text Raw inline text.
     * @param baseOffset Document offset of this text segment for source mapping.
     * @return Vector of formatted spans.
     */
    static std::vector<MdSpan> ParseInlineSpans(const std::string& text, size_t baseOffset = 0);
};

} // namespace Folio
