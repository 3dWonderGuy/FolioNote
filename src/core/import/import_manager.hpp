#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include "core/document/notebook.hpp"
#include "core/document/section.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/library.hpp"
#include "core/objects/text_box.hpp"
#include "core/import/onenote_importer.hpp"
#include "utils/logger.hpp"

namespace Folio {

class ImportManager {
public:
    /**
     * @brief Ingests a Microsoft OneNote notebook folder, .one section, or .onepkg package,
     * converting it into a standard FolioNote .notebook package.
     */
    static std::shared_ptr<Notebook> ImportOneNote(
        const std::string& sourcePath, 
        const std::string& destLibraryPath,
        PageRepository& repo
    ) {
        std::error_code ec;
        if (!std::filesystem::exists(sourcePath, ec)) return nullptr;

        auto nb = OneNoteImporter::Import(sourcePath, destLibraryPath);
        if (!nb) return nullptr;

        // Generate target .notebook package directory
        std::string safeName = nb->name;
        for (char& c : safeName) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }
        std::filesystem::path targetPkg = std::filesystem::path(destLibraryPath) / (safeName + ".notebook");
        int counter = 1;
        while (std::filesystem::exists(targetPkg, ec)) {
            targetPkg = std::filesystem::path(destLibraryPath) / (safeName + " (" + std::to_string(counter++) + ").notebook");
        }

        nb->filePath = targetPkg.string();
        std::filesystem::create_directories(targetPkg / "pages", ec);

        // Persist the converted notebook hierarchy via PageRepository
        if (repo.OpenNotebookPackage(targetPkg.string())) {
            repo.SaveNotebookAsync(nb);
        }

        return nb;
    }

    /**
     * @brief Imports a standalone FolioNote .notebook folder or .folio package into a library.
     */
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

    /**
     * @brief Registers an existing directory as a FolioNote library folder and scans its notebooks.
     */
    static bool ImportLibraryFolder(const std::string& folderPath, LibraryManager& libManager, const std::string& customName = "") {
        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec)) {
            return false;
        }

        std::string libName = customName.empty() ? std::filesystem::path(folderPath).filename().string() : customName;
        libManager.AddLibrary(libName, folderPath);
        return true;
    }
};

} // namespace Folio
