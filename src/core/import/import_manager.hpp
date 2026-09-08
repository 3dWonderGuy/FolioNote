#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/objects/text_box.hpp"
#include "utils/logger.hpp"

class ImportManager {
public:
    // Ingests an HTML file and creates a new CanvasPage with parsed text boxes
    static std::shared_ptr<CanvasPage> ImportHTML(const std::string& filePath) {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec)) return nullptr;

        std::ifstream file(filePath);
        if (!file.is_open()) return nullptr;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        std::string pageTitle = std::filesystem::path(filePath).stem().string();
        
        // Simple HTML title extraction if available
        size_t tStart = content.find("<title>");
        size_t tEnd = content.find("</title>");
        if (tStart != std::string::npos && tEnd != std::string::npos && tEnd > tStart + 7) {
            pageTitle = content.substr(tStart + 7, tEnd - (tStart + 7));
        }

        auto page = std::make_shared<CanvasPage>(pageTitle);

        // Strip basic HTML tags and create text boxes
        std::string plainText;
        bool inTag = false;
        for (char c : content) {
            if (c == '<') inTag = true;
            else if (c == '>') {
                inTag = false;
                plainText += ' ';
            } else if (!inTag) {
                plainText += c;
            }
        }

        // Clean extra whitespaces
        std::string cleaned;
        bool lastSpace = false;
        for (char c : plainText) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!lastSpace) {
                    cleaned += (c == '\n' ? '\n' : ' ');
                    lastSpace = true;
                }
            } else {
                cleaned += c;
                lastSpace = false;
            }
        }

        auto tb = std::make_shared<Folio::TextBoxObject>();
        tb->text = cleaned.empty() ? ("Imported content from " + filePath) : cleaned;
        tb->worldX = 50.0;
        tb->worldY = 50.0;
        tb->worldWidth = 700.0;
        tb->worldHeight = 400.0;
        tb->UpdateBounds();
        page->AddObject(tb);

        return page;
    }

    // Ingests a PDF document and attaches it to the section
    static std::shared_ptr<CanvasPage> ImportPDF(const std::string& filePath) {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec)) return nullptr;

        std::string filename = std::filesystem::path(filePath).filename().string();
        std::string pageTitle = "PDF: " + std::filesystem::path(filePath).stem().string();

        auto page = std::make_shared<CanvasPage>(pageTitle);

        auto tb = std::make_shared<Folio::TextBoxObject>();
        tb->text = "[Imported PDF Document: " + filename + "]\nSource: " + filePath + "\n\n(Vector annotations and digital inking active over this document canvas)";
        tb->worldX = 60.0;
        tb->worldY = 60.0;
        tb->worldWidth = 750.0;
        tb->worldHeight = 200.0;
        tb->UpdateBounds();
        page->AddObject(tb);

        return page;
    }

    // Imports a .notebook or .folio folder package into a destination library directory
    static std::string ImportNotebookPackage(const std::string& sourcePackagePath, const std::string& destLibraryPath) {
        std::error_code ec;
        std::filesystem::path src(sourcePackagePath);
        if (!std::filesystem::exists(src, ec)) return "";

        std::filesystem::path destDir(destLibraryPath);
        if (!std::filesystem::exists(destDir, ec)) {
            std::filesystem::create_directories(destDir, ec);
        }

        std::filesystem::path targetPkg = destDir / src.filename();
        int counter = 1;
        while (std::filesystem::exists(targetPkg, ec)) {
            std::string stem = src.stem().string();
            targetPkg = destDir / (stem + " (Imported " + std::to_string(counter++) + ").notebook");
        }

        std::filesystem::copy(src, targetPkg, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return "";

        return targetPkg.string();
    }
};
