#include "core/md_engine/md_parser.hpp"
#include <sstream>
#include <cctype>
#include <algorithm>

namespace Folio {

namespace {

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.rfind(prefix, 0) == 0;
}

std::vector<std::string> SplitTableCells(const std::string& line) {
    std::vector<std::string> cells;
    size_t start = 0;
    if (!line.empty() && line.front() == '|') start = 1;
    size_t end = line.size();
    if (!line.empty() && line.back() == '|') end = line.size() - 1;

    std::string content = line.substr(start, end - start);
    std::string cell;
    bool inEscape = false;
    for (char c : content) {
        if (inEscape) {
            cell.push_back(c);
            inEscape = false;
        } else if (c == '\\') {
            inEscape = true;
        } else if (c == '|') {
            cells.push_back(Trim(cell));
            cell.clear();
        } else {
            cell.push_back(c);
        }
    }
    cells.push_back(Trim(cell));
    return cells;
}

bool IsTableSeparatorRow(const std::string& line) {
    auto cells = SplitTableCells(line);
    if (cells.empty()) return false;
    for (const auto& c : cells) {
        std::string t = Trim(c);
        if (t.empty()) return false;
        for (char ch : t) {
            if (ch != '-' && ch != ':' && ch != ' ' && ch != '\t') return false;
        }
    }
    return true;
}

} // anonymous namespace

std::vector<MdSpan> MdParser::ParseInlineSpans(const std::string& text, size_t baseOffset) {
    std::vector<MdSpan> spans;
    if (text.empty()) return spans;

    size_t i = 0;
    size_t n = text.size();
    size_t plainStart = 0;

    auto flushPlain = [&](size_t endIdx) {
        if (endIdx > plainStart) {
            MdSpan span;
            span.type = MdSpanType::Text;
            span.text = text.substr(plainStart, endIdx - plainStart);
            span.sourceOffset = baseOffset + plainStart;
            span.sourceLength = endIdx - plainStart;
            spans.push_back(std::move(span));
        }
    };

    while (i < n) {
        // 1. Escaped characters: \* \_ \` etc.
        if (text[i] == '\\' && i + 1 < n) {
            i += 2;
            continue;
        }

        // 2. Inline Code: `...`
        if (text[i] == '`') {
            size_t close = text.find('`', i + 1);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::Code;
                span.text = text.substr(i + 1, close - i - 1);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 1;
                spans.push_back(std::move(span));
                i = close + 1;
                plainStart = i;
                continue;
            }
        }

        // 3. Inline Math: $...$
        if (text[i] == '$' && (i + 1 < n && text[i + 1] != '$')) {
            size_t close = text.find('$', i + 1);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::MathInline;
                span.text = text.substr(i + 1, close - i - 1);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 1;
                spans.push_back(std::move(span));
                i = close + 1;
                plainStart = i;
                continue;
            }
        }

        // 4. Highlight: ==...==
        if (i + 1 < n && text[i] == '=' && text[i + 1] == '=') {
            size_t close = text.find("==", i + 2);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::Highlight;
                span.text = text.substr(i + 2, close - i - 2);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 2;
                spans.push_back(std::move(span));
                i = close + 2;
                plainStart = i;
                continue;
            }
        }

        // 5. Strikethrough: ~~...~~
        if (i + 1 < n && text[i] == '~' && text[i + 1] == '~') {
            size_t close = text.find("~~", i + 2);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::Strikethrough;
                span.text = text.substr(i + 2, close - i - 2);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 2;
                spans.push_back(std::move(span));
                i = close + 2;
                plainStart = i;
                continue;
            }
        }

        // 6. Bold / Italic: ***, **, *, ___, __, _
        if (i + 2 < n && ((text[i] == '*' && text[i + 1] == '*' && text[i + 2] == '*') ||
                          (text[i] == '_' && text[i + 1] == '_' && text[i + 2] == '_'))) {
            std::string delim(3, text[i]);
            size_t close = text.find(delim, i + 3);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::BoldItalic;
                span.text = text.substr(i + 3, close - i - 3);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 3;
                spans.push_back(std::move(span));
                i = close + 3;
                plainStart = i;
                continue;
            }
        }

        if (i + 1 < n && ((text[i] == '*' && text[i + 1] == '*') ||
                          (text[i] == '_' && text[i + 1] == '_'))) {
            std::string delim(2, text[i]);
            size_t close = text.find(delim, i + 2);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::Bold;
                span.text = text.substr(i + 2, close - i - 2);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 2;
                spans.push_back(std::move(span));
                i = close + 2;
                plainStart = i;
                continue;
            }
        }

        if (text[i] == '*' || (text[i] == '_' && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]))))) {
            char delim = text[i];
            size_t close = text.find(delim, i + 1);
            if (close != std::string::npos) {
                flushPlain(i);
                MdSpan span;
                span.type = MdSpanType::Italic;
                span.text = text.substr(i + 1, close - i - 1);
                span.sourceOffset = baseOffset + i;
                span.sourceLength = close - i + 1;
                spans.push_back(std::move(span));
                i = close + 1;
                plainStart = i;
                continue;
            }
        }

        // 7. Links: [text](url)
        if (text[i] == '[') {
            size_t closeBracket = text.find(']', i + 1);
            if (closeBracket != std::string::npos && closeBracket + 1 < n && text[closeBracket + 1] == '(') {
                size_t closeParen = text.find(')', closeBracket + 2);
                if (closeParen != std::string::npos) {
                    flushPlain(i);
                    MdSpan span;
                    span.type = MdSpanType::Link;
                    span.text = text.substr(i + 1, closeBracket - i - 1);
                    span.targetUrl = text.substr(closeBracket + 2, closeParen - closeBracket - 2);
                    span.sourceOffset = baseOffset + i;
                    span.sourceLength = closeParen - i + 1;
                    spans.push_back(std::move(span));
                    i = closeParen + 1;
                    plainStart = i;
                    continue;
                }
            }
        }

        ++i;
    }

    flushPlain(n);
    return spans;
}

std::vector<MdBlock> MdParser::ParseDocument(const std::string& markdown) {
    std::vector<MdBlock> blocks;
    if (markdown.empty()) return blocks;

    std::vector<std::string> lines;
    std::vector<size_t> lineOffsets;
    {
        size_t offset = 0;
        size_t n = markdown.size();
        while (offset < n) {
            lineOffsets.push_back(offset);
            size_t nextNewline = markdown.find('\n', offset);
            if (nextNewline == std::string::npos) {
                lines.push_back(markdown.substr(offset));
                break;
            } else {
                size_t len = nextNewline - offset;
                if (len > 0 && markdown[offset + len - 1] == '\r') {
                    lines.push_back(markdown.substr(offset, len - 1));
                } else {
                    lines.push_back(markdown.substr(offset, len));
                }
                offset = nextNewline + 1;
            }
        }
    }

    size_t lineIdx = 0;
    size_t totalLines = lines.size();

    while (lineIdx < totalLines) {
        const std::string& line = lines[lineIdx];
        std::string trimmed = Trim(line);
        size_t lineStartOffset = lineOffsets[lineIdx];

        // Blank line
        if (trimmed.empty()) {
            ++lineIdx;
            continue;
        }

        // 1. Fenced Code Block: ```[lang] ... ```
        if (StartsWith(trimmed, "```") || StartsWith(trimmed, "~~~")) {
            std::string fence = trimmed.substr(0, 3);
            std::string lang = Trim(trimmed.substr(3));
            MdBlock block;
            block.type = MdBlockType::CodeBlock;
            block.language = lang;
            block.sourceStartOffset = lineStartOffset;

            std::string codeBody;
            ++lineIdx;
            while (lineIdx < totalLines) {
                if (StartsWith(Trim(lines[lineIdx]), fence)) {
                    block.sourceEndOffset = lineOffsets[lineIdx] + lines[lineIdx].size();
                    ++lineIdx;
                    break;
                }
                codeBody += lines[lineIdx] + "\n";
                ++lineIdx;
            }
            if (block.sourceEndOffset == 0 && lineIdx == totalLines) {
                block.sourceEndOffset = markdown.size();
            }
            if (!codeBody.empty() && codeBody.back() == '\n') codeBody.pop_back();
            block.codeContent = codeBody;
            blocks.push_back(std::move(block));
            continue;
        }

        // 2. Math Block: $$ ... $$
        if (StartsWith(trimmed, "$$")) {
            MdBlock block;
            block.type = MdBlockType::MathBlock;
            block.sourceStartOffset = lineStartOffset;

            // Check single line math block: $$ math $$
            if (trimmed.size() > 4 && trimmed.rfind("$$") == trimmed.size() - 2) {
                block.codeContent = Trim(trimmed.substr(2, trimmed.size() - 4));
                block.sourceEndOffset = lineStartOffset + line.size();
                blocks.push_back(std::move(block));
                ++lineIdx;
                continue;
            }

            std::string mathBody;
            ++lineIdx;
            while (lineIdx < totalLines) {
                if (StartsWith(Trim(lines[lineIdx]), "$$")) {
                    block.sourceEndOffset = lineOffsets[lineIdx] + lines[lineIdx].size();
                    ++lineIdx;
                    break;
                }
                mathBody += lines[lineIdx] + "\n";
                ++lineIdx;
            }
            if (!mathBody.empty() && mathBody.back() == '\n') mathBody.pop_back();
            block.codeContent = mathBody;
            blocks.push_back(std::move(block));
            continue;
        }

        // 3. Headings: #, ##, ###, ####, #####, ######
        if (trimmed[0] == '#') {
            size_t hCount = 0;
            while (hCount < trimmed.size() && trimmed[hCount] == '#') ++hCount;
            if (hCount <= 6 && hCount < trimmed.size() && trimmed[hCount] == ' ') {
                std::string headerText = Trim(trimmed.substr(hCount + 1));
                MdBlock block;
                block.type = MdBlockType::Heading;
                block.level = static_cast<int>(hCount);
                block.sourceStartOffset = lineStartOffset;
                block.sourceEndOffset = lineStartOffset + line.size();
                size_t spanOffset = lineStartOffset + (line.find(headerText));
                block.spans = ParseInlineSpans(headerText, spanOffset);
                blocks.push_back(std::move(block));
                ++lineIdx;
                continue;
            }
        }

        // 4. Horizontal Rule: ---, ***, ___
        if ((trimmed == "---" || trimmed == "***" || trimmed == "___") ||
            (trimmed.size() >= 3 && std::all_of(trimmed.begin(), trimmed.end(), [](char c) { return c == '-'; }))) {
            MdBlock block;
            block.type = MdBlockType::HorizontalRule;
            block.sourceStartOffset = lineStartOffset;
            block.sourceEndOffset = lineStartOffset + line.size();
            blocks.push_back(std::move(block));
            ++lineIdx;
            continue;
        }

        // 5. Blockquote: > ...
        if (trimmed[0] == '>') {
            std::string quoteText;
            size_t startOff = lineStartOffset;
            size_t endOff = lineStartOffset + line.size();

            while (lineIdx < totalLines && !Trim(lines[lineIdx]).empty() && Trim(lines[lineIdx])[0] == '>') {
                std::string curTrim = Trim(lines[lineIdx]);
                size_t qIdx = 0;
                while (qIdx < curTrim.size() && curTrim[qIdx] == '>') ++qIdx;
                quoteText += Trim(curTrim.substr(qIdx)) + " ";
                endOff = lineOffsets[lineIdx] + lines[lineIdx].size();
                ++lineIdx;
            }
            if (!quoteText.empty() && quoteText.back() == ' ') quoteText.pop_back();

            MdBlock block;
            block.type = MdBlockType::Blockquote;
            block.sourceStartOffset = startOff;
            block.sourceEndOffset = endOff;
            block.spans = ParseInlineSpans(quoteText, startOff);
            blocks.push_back(std::move(block));
            continue;
        }

        // 6. Task List: - [ ] or - [x]
        if ((StartsWith(trimmed, "- [ ] ") || StartsWith(trimmed, "- [x] ") || StartsWith(trimmed, "- [X] ") ||
             StartsWith(trimmed, "* [ ] ") || StartsWith(trimmed, "* [x] ") || StartsWith(trimmed, "* [X] "))) {
            bool isChecked = (trimmed[3] == 'x' || trimmed[3] == 'X');
            std::string taskText = trimmed.substr(6);

            MdBlock block;
            block.type = MdBlockType::TaskList;
            block.checked = isChecked;
            block.sourceStartOffset = lineStartOffset;
            block.sourceEndOffset = lineStartOffset + line.size();
            size_t spanOffset = lineStartOffset + (line.find(taskText));
            block.spans = ParseInlineSpans(taskText, spanOffset);
            blocks.push_back(std::move(block));
            ++lineIdx;
            continue;
        }

        // 7. Bullet List: - , * , + 
        if (StartsWith(trimmed, "- ") || StartsWith(trimmed, "* ") || StartsWith(trimmed, "+ ")) {
            std::string listText = trimmed.substr(2);
            MdBlock block;
            block.type = MdBlockType::BulletList;
            block.sourceStartOffset = lineStartOffset;
            block.sourceEndOffset = lineStartOffset + line.size();
            size_t spanOffset = lineStartOffset + (line.find(listText));
            block.spans = ParseInlineSpans(listText, spanOffset);
            blocks.push_back(std::move(block));
            ++lineIdx;
            continue;
        }

        // 8. Numbered List: 1. , 2. 
        if (std::isdigit(static_cast<unsigned char>(trimmed[0]))) {
            size_t dotPos = trimmed.find(". ");
            if (dotPos != std::string::npos && dotPos <= 5) {
                int itemIdx = std::stoi(trimmed.substr(0, dotPos));
                std::string listText = trimmed.substr(dotPos + 2);
                MdBlock block;
                block.type = MdBlockType::NumberedList;
                block.listIndex = itemIdx;
                block.sourceStartOffset = lineStartOffset;
                block.sourceEndOffset = lineStartOffset + line.size();
                size_t spanOffset = lineStartOffset + (line.find(listText));
                block.spans = ParseInlineSpans(listText, spanOffset);
                blocks.push_back(std::move(block));
                ++lineIdx;
                continue;
            }
        }

        // 9. Table: line with '|' and next line is separator
        if (trimmed.find('|') != std::string::npos && lineIdx + 1 < totalLines && IsTableSeparatorRow(lines[lineIdx + 1])) {
            MdBlock block;
            block.type = MdBlockType::Table;
            block.sourceStartOffset = lineStartOffset;

            // Headers
            block.table.headers = SplitTableCells(trimmed);
            // Alignments
            auto sepCells = SplitTableCells(lines[lineIdx + 1]);
            for (const auto& sc : sepCells) {
                std::string s = Trim(sc);
                if (s.size() >= 2 && s.front() == ':' && s.back() == ':') {
                    block.table.alignments.push_back(1); // Center
                } else if (!s.empty() && s.back() == ':') {
                    block.table.alignments.push_back(2); // Right
                } else {
                    block.table.alignments.push_back(0); // Left
                }
            }

            lineIdx += 2; // Skip header and separator
            while (lineIdx < totalLines && !Trim(lines[lineIdx]).empty() && lines[lineIdx].find('|') != std::string::npos) {
                block.table.rows.push_back(SplitTableCells(lines[lineIdx]));
                block.sourceEndOffset = lineOffsets[lineIdx] + lines[lineIdx].size();
                ++lineIdx;
            }
            blocks.push_back(std::move(block));
            continue;
        }

        // 10. Standard Paragraph: accumulate text lines until empty line or special token
        {
            std::string paraText;
            size_t pStart = lineStartOffset;
            size_t pEnd = lineStartOffset + line.size();

            while (lineIdx < totalLines) {
                const std::string& cur = lines[lineIdx];
                std::string curTrim = Trim(cur);
                if (curTrim.empty() || curTrim[0] == '#' || StartsWith(curTrim, "```") || StartsWith(curTrim, "$$") ||
                    StartsWith(curTrim, "> ") || StartsWith(curTrim, "- ") || StartsWith(curTrim, "* ") ||
                    curTrim == "---") {
                    break;
                }
                paraText += (paraText.empty() ? "" : " ") + curTrim;
                pEnd = lineOffsets[lineIdx] + cur.size();
                ++lineIdx;
            }

            if (!paraText.empty()) {
                MdBlock block;
                block.type = MdBlockType::Paragraph;
                block.sourceStartOffset = pStart;
                block.sourceEndOffset = pEnd;
                block.spans = ParseInlineSpans(paraText, pStart);
                blocks.push_back(std::move(block));
            }
        }
    }

    return blocks;
}

} // namespace Folio
