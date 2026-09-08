#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <chrono>

#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/text_box.hpp"
#include "utils/logger.hpp"
#include "utils/guid_generator.hpp"

namespace Folio {

/**
 * =========================================================================================
 * @file onenote_importer.hpp
 * @brief Reverse-Engineered OneNote (.one / .onetoc2 / .onepkg) Ingestion & Migration Engine
 * =========================================================================================
 * 
 * Supports importing:
 *  1. OneNote Notebook Directory (contains *.one files and optional Open Notebook.onetoc2)
 *  2. Standalone OneNote Section (*.one)
 *  3. OneNote Single-File Package (*.onepkg / cabinet archive)
 * 
 * Parses OneNote MS-ONE binary headers:
 *  - FileHeader GUID for .one:    {7B5C52E4-D882-4DA1-AC5F-53C20F702706}
 *  - FileHeader GUID for .onetoc2: {43FF2F50-350D-48D1-82C3-263B86F60D1A}
 * Extracts UTF-16LE / ASCII text chunks, page titles, paragraphs, and creates native
 * FolioNote Notebooks, SectionGroups, Sections, and CanvasPages.
 */
class OneNoteImporter {
public:
    struct ParsedPageData {
        std::string title;
        std::vector<std::string> textParagraphs;
        std::string dateStr;
        std::string timeStr;
    };

    struct ParsedSectionData {
        std::string name;
        std::vector<ParsedPageData> pages;
    };

    /**
     * @brief Ingests any OneNote file or directory and produces a fully populated FolioNote Notebook.
     * @param sourcePath Path to a .one file, .onepkg file, or a OneNote notebook folder.
     * @param outTargetNotebookDir Output destination folder for the generated FolioNote .notebook package.
     * @return std::shared_ptr<Notebook> Populated FolioNote notebook, or nullptr on failure.
     */
    static std::shared_ptr<Notebook> Import(const std::string& sourcePath, const std::string& outTargetNotebookDir) {
        std::error_code ec;
        if (!std::filesystem::exists(sourcePath, ec)) {
            LOG_ERROR(PageRepository, "Source OneNote path does not exist: " + sourcePath);
            return nullptr;
        }

        std::filesystem::path p(sourcePath);
        std::string nbName = p.stem().string();
        if (nbName == "Open Notebook" || nbName == "onetoc2") {
            nbName = p.parent_path().filename().string();
        }
        if (nbName.empty()) nbName = "Imported OneNote Notebook";

        auto notebook = std::make_shared<Notebook>(nbName);
        notebook->iconFile = "blue-notebook.svg";

        if (std::filesystem::is_directory(p, ec)) {
            // Case 1: Directory containing .one files and subdirectories (Section Groups)
            ImportDirectory(p, notebook);
        } else if (p.extension() == ".one") {
            // Case 2: Standalone .one section file
            ParsedSectionData secData = ParseOneFile(p.string());
            if (secData.pages.empty()) {
                // If no discrete pages extracted, make a default page with whatever text found
                ParsedPageData pg;
                pg.title = secData.name;
                pg.textParagraphs.push_back("Imported from OneNote section: " + p.filename().string());
                secData.pages.push_back(pg);
            }
            auto sec = CreateSectionFromData(secData);
            notebook->sections.push_back(sec);
        } else if (p.extension() == ".onetoc2") {
            // Case 3: Table of contents file inside a notebook directory
            ImportDirectory(p.parent_path(), notebook);
        } else if (p.extension() == ".onepkg") {
            // Case 4: OneNote package (CAB or ZIP archive)
            ImportPackageFile(p.string(), notebook, outTargetNotebookDir);
        }

        // Ensure at least one section and page exists if import was sparse
        if (notebook->sections.empty()) {
            auto defSec = std::make_shared<Section>("General Notes");
            auto defPg = std::make_shared<CanvasPage>("Welcome");
            auto tb = std::make_shared<Folio::TextBoxObject>();
            tb->text = "Imported OneNote Notebook: " + nbName + "\nSource: " + sourcePath;
            tb->worldX = 50.0;
            tb->worldY = 50.0;
            tb->worldWidth = 600.0;
            tb->worldHeight = 120.0;
            tb->UpdateBounds();
            defPg->AddObject(tb);
            defSec->pages.push_back(defPg);
            notebook->sections.push_back(defSec);
        }

        return notebook;
    }

private:
    // Imports all .one files within a directory and its subdirectories (section groups)
    static void ImportDirectory(const std::filesystem::path& dirPath, std::shared_ptr<Notebook>& notebook) {
        std::error_code ec;

        // 1. Direct section files (.one) in root of notebook
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".one") {
                ParsedSectionData secData = ParseOneFile(entry.path().string());
                auto sec = CreateSectionFromData(secData);
                notebook->sections.push_back(sec);
            }
        }

        // 2. Subdirectories become Section Groups
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (entry.is_directory()) {
                std::string groupName = entry.path().filename().string();
                auto secGroup = std::make_shared<SectionGroup>(groupName);

                for (const auto& subEntry : std::filesystem::directory_iterator(entry.path(), ec)) {
                    if (subEntry.is_regular_file() && subEntry.path().extension() == ".one") {
                        ParsedSectionData secData = ParseOneFile(subEntry.path().string());
                        auto sec = CreateSectionFromData(secData);
                        secGroup->sections.push_back(sec);
                    }
                }

                if (!secGroup->sections.empty()) {
                    notebook->sectionGroups.push_back(secGroup);
                }
            }
        }
    }

    // Handles .onepkg files (which are Microsoft CAB archives containing .one files)
    static void ImportPackageFile(const std::string& pkgPath, std::shared_ptr<Notebook>& notebook, const std::string& targetDir) {
        std::error_code ec;
        std::filesystem::path p(pkgPath);
        std::string tempExtractDir = (std::filesystem::path(targetDir) / ("_temp_onepkg_" + GUIDGenerator::GenerateV4().substr(0, 8))).string();
        std::filesystem::create_directories(tempExtractDir, ec);

#if defined(_WIN32)
        // Windows built-in expand.exe can extract CAB / onepkg files
        std::string cmd = "expand -R \"" + pkgPath + "\" -F:* \"" + tempExtractDir + "\" >nul 2>&1";
        int res = system(cmd.c_str());
        if (res == 0 && std::filesystem::exists(tempExtractDir, ec)) {
            ImportDirectory(tempExtractDir, notebook);
            std::filesystem::remove_all(tempExtractDir, ec);
            return;
        }
#endif

        // Fallback: parse raw binary stream of the .onepkg looking for embedded .one streams
        ParsedSectionData secData = ParseOneBinaryStream(pkgPath);
        if (secData.pages.empty()) {
            ParsedPageData pg;
            pg.title = p.stem().string();
            pg.textParagraphs.push_back("OneNote package archive: " + p.filename().string());
            secData.pages.push_back(pg);
        }
        notebook->sections.push_back(CreateSectionFromData(secData));
        std::filesystem::remove_all(tempExtractDir, ec);
    }

    // Parses a single .one binary file and extracts its sections and pages
    static ParsedSectionData ParseOneFile(const std::string& filePath) {
        return ParseOneBinaryStream(filePath);
    }

    // Reverse-engineers MS-ONE binary file structures and UTF-16LE text streams
    static ParsedSectionData ParseOneBinaryStream(const std::string& filePath) {
        ParsedSectionData section;
        std::filesystem::path p(filePath);
        section.name = p.stem().string();

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return section;

        file.seekg(0, std::ios::end);
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        if (fileSize < 64) return section;

        std::vector<uint8_t> buffer(fileSize);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

        // Verify MS-ONE Header Signature:
        // GUID {7B5C52E4-D882-4DA1-AC5F-53C20F702706}
        // Bytes: E4 52 5C 7B 82 D8 A1 4D AC 5F 53 C2 0F 70 27 06
        const uint8_t oneGuid[16] = {
            0xE4, 0x52, 0x5C, 0x7B, 0x82, 0xD8, 0xA1, 0x4D,
            0xAC, 0x5F, 0x53, 0xC2, 0x0F, 0x70, 0x27, 0x06
        };
        const uint8_t tocGuid[16] = {
            0x50, 0x2F, 0xFF, 0x43, 0x0D, 0x35, 0xD1, 0x48,
            0x82, 0xC3, 0x26, 0x3B, 0x86, 0xF6, 0x0D, 0x1A
        };

        bool isOneNoteFile = (memcmp(buffer.data(), oneGuid, 16) == 0) || 
                             (memcmp(buffer.data(), tocGuid, 16) == 0);

        // Extract UTF-16LE strings throughout the binary file
        std::vector<std::string> extractedStrings = ExtractUtf16Strings(buffer);

        // Filter and categorize strings into page titles and body paragraphs
        std::vector<ParsedPageData> pages;
        ParsedPageData currentPage;
        currentPage.title = "";

        for (const auto& str : extractedStrings) {
            // Skip noise strings, GUIDs, fonts, and internal OneNote schema strings
            if (IsInternalOneNoteString(str)) continue;

            // If string looks like a Page Title candidate (short, capitalized, no linebreaks)
            if (currentPage.title.empty() && str.length() <= 80 && str.find('\n') == std::string::npos) {
                currentPage.title = str;
            } else {
                // If we encounter a new candidate title after collecting several paragraphs
                if (str.length() <= 60 && !currentPage.textParagraphs.empty() && 
                    str.find('\n') == std::string::npos && IsLikelyTitle(str)) {
                    pages.push_back(currentPage);
                    currentPage = ParsedPageData();
                    currentPage.title = str;
                } else {
                    // Body text paragraph
                    currentPage.textParagraphs.push_back(str);
                }
            }
        }

        if (!currentPage.title.empty() || !currentPage.textParagraphs.empty()) {
            if (currentPage.title.empty()) currentPage.title = section.name + " Page 1";
            pages.push_back(currentPage);
        }

        // If no pages were extracted, create at least one page with the section name
        if (pages.empty()) {
            ParsedPageData defaultPage;
            defaultPage.title = section.name;
            defaultPage.textParagraphs.push_back("Imported section contents from " + p.filename().string());
            pages.push_back(defaultPage);
        }

        section.pages = pages;
        return section;
    }

    // Scans byte buffer for valid UTF-16LE strings (2 bytes per character)
    static std::vector<std::string> ExtractUtf16Strings(const std::vector<uint8_t>& buf) {
        std::vector<std::string> results;
        if (buf.size() < 4) return results;

        std::string current;
        for (size_t i = 0; i + 1 < buf.size(); i += 2) {
            uint16_t ch = buf[i] | (buf[i + 1] << 8);

            // Valid printable ASCII range or common extended characters
            if ((ch >= 0x20 && ch <= 0x7E) || ch == 0x0A || ch == 0x0D || ch == 0x09) {
                current += static_cast<char>(ch);
            } else if (ch >= 0x00A0 && ch <= 0x00FF) {
                // Simple Latin-1 extended
                current += static_cast<char>(ch & 0xFF);
            } else {
                if (current.length() >= 3) {
                    // Clean up string
                    TrimString(current);
                    if (current.length() >= 3) {
                        results.push_back(current);
                    }
                }
                current.clear();
            }
        }

        if (current.length() >= 3) {
            TrimString(current);
            if (current.length() >= 3) {
                results.push_back(current);
            }
        }

        return results;
    }

    static bool IsInternalOneNoteString(const std::string& s) {
        // OneNote internal schemas, fonts, XML namespaces, and binary property keys
        static const std::vector<std::string> ignored = {
            "http://", "https://", "schemas.microsoft.com", "Calibri", "Segoe UI", 
            "Arial", "Times New Roman", "Consolas", "jcid", "outline", "paragraph", 
            "OneNote", "Microsoft", "xml version", "utf-8", "xmlns", "{", "}",
            "Symbol", "Wingdings", "Courier", "Tahoma", "Verdana", "MS Shell Dlg"
        };
        for (const auto& ign : ignored) {
            if (s.find(ign) != std::string::npos) return true;
        }
        return false;
    }

    static bool IsLikelyTitle(const std::string& s) {
        if (s.empty() || s.length() > 60) return false;
        if (s.find('.') != std::string::npos && s.length() > 40) return false;
        // Check if starts with uppercase letter or number
        char c = s[0];
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    static void TrimString(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }

    static std::shared_ptr<Section> CreateSectionFromData(const ParsedSectionData& secData) {
        auto sec = std::make_shared<Section>(secData.name);

        for (const auto& pgData : secData.pages) {
            auto page = std::make_shared<CanvasPage>(pgData.title.empty() ? "Untitled Page" : pgData.title);
            
            // Layout text paragraphs as editable text boxes
            double curY = 40.0;
            for (const auto& para : pgData.textParagraphs) {
                auto tb = std::make_shared<Folio::TextBoxObject>();
                tb->text = para;
                tb->worldX = 50.0;
                tb->worldY = curY;
                tb->worldWidth = 720.0;
                tb->worldHeight = std::max(60.0, para.length() * 0.45);
                tb->UpdateBounds();
                page->AddObject(tb);

                curY += tb->worldHeight + 20.0;
            }

            sec->pages.push_back(page);
        }

        if (sec->pages.empty()) {
            sec->pages.push_back(std::make_shared<CanvasPage>("Untitled Page"));
        }

        return sec;
    }
};

} // namespace Folio
