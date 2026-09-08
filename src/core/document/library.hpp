#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <SDL3/SDL.h>
#include "core/document/notebook.hpp"
#include "core/storage/page_repository.hpp"
#include "utils/logger.hpp"

struct LibraryInfo {
    std::string id;
    std::string name;
    std::string rootPath;
    bool isDefault = false;
    std::vector<std::string> notebookPaths;
};

class LibraryManager {
public:
    std::vector<LibraryInfo> libraries;
    std::vector<std::string> standaloneNotebookPaths;
    std::string defaultLibraryPath;

    // Helper to sanitize any path so it is never pointing to or inside a .notebook folder
    static std::string SanitizeLibraryRoot(const std::string& inputPath) {
        std::filesystem::path root(inputPath);
        while (!root.empty() && (root.extension() == ".notebook" || root.filename().string().find(".notebook") != std::string::npos)) {
            root = root.parent_path();
        }
        if (root.empty() || root == ".") {
            const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
            if (docs) {
                root = std::filesystem::path(docs) / "FolioNote";
            } else {
                root = "FolioNote";
            }
        }
        return root.string();
    }

    void Init(const std::string& defaultRootPath) {
        defaultLibraryPath = SanitizeLibraryRoot(defaultRootPath);
        libraries.clear();
        standaloneNotebookPaths.clear();

        // 1. Always create the Default Library pointing to the primary documents directory
        LibraryInfo defaultLib;
        defaultLib.id = "default_main";
        defaultLib.name = "Main Library";
        defaultLib.rootPath = defaultLibraryPath;
        defaultLib.isDefault = true;
        ScanLibraryNotebooks(defaultLib);
        libraries.push_back(defaultLib);

        // 2. Load any user-added custom libraries from config
        LoadConfig();
    }

    void ScanLibraryNotebooks(LibraryInfo& lib) {
        lib.notebookPaths.clear();
        std::error_code ec;
        if (std::filesystem::exists(lib.rootPath, ec) && std::filesystem::is_directory(lib.rootPath, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(lib.rootPath, ec)) {
                if (std::filesystem::is_directory(entry.status()) && entry.path().extension() == ".notebook") {
                    lib.notebookPaths.push_back(entry.path().string());
                }
            }
        }
    }

    void RefreshAll() {
        for (auto& lib : libraries) {
            ScanLibraryNotebooks(lib);
        }
    }

    bool AddLibrary(const std::string& name, const std::string& path) {
        if (name.empty() || path.empty()) return false;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            std::filesystem::create_directories(path, ec);
        }

        LibraryInfo lib;
        lib.id = "lib_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        lib.name = name;
        lib.rootPath = path;
        lib.isDefault = false;
        ScanLibraryNotebooks(lib);
        libraries.push_back(lib);
        SaveConfig();
        return true;
    }

    bool RemoveLibrary(size_t index) {
        if (index >= libraries.size() || libraries[index].isDefault) return false;
        libraries.erase(libraries.begin() + index);
        SaveConfig();
        return true;
    }

    // Creates a brand new standalone notebook or in a selected library
    std::shared_ptr<Notebook> CreateNewNotebook(
        const std::string& name, 
        const std::string& targetDirectory,
        ImVec4 color = ImVec4(0.20f, 0.48f, 0.92f, 1.0f),
        const std::string& icon = ""
    ) {
        std::error_code ec;
        std::string cleanDir = SanitizeLibraryRoot(targetDirectory.empty() ? defaultLibraryPath : targetDirectory);
        std::filesystem::path dir(cleanDir);
        if (!std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
        }

        std::string safeName = name.empty() ? "Untitled Notebook" : name;
        std::filesystem::path pkgPath = dir / (safeName + ".notebook");

        // Disambiguate name if already exists
        int counter = 1;
        while (std::filesystem::exists(pkgPath, ec)) {
            pkgPath = dir / (safeName + " (" + std::to_string(counter++) + ").notebook");
        }

        auto nb = std::make_shared<Notebook>(safeName, color, icon);
        nb->filePath = pkgPath.string();

        Folio::PageRepository repo;
        if (repo.OpenNotebookPackage(nb->filePath)) {
            repo.SaveNotebookAsync(nb);
        }

        RefreshAll();
        return nb;
    }

    // Save As Copy: Duplicates an existing notebook with a new name and GUID into target directory
    std::shared_ptr<Notebook> SaveAsCopy(
        const std::shared_ptr<Notebook>& sourceNb,
        const std::string& newName,
        const std::string& targetDirectory
    ) {
        if (!sourceNb) return nullptr;
        std::error_code ec;
        std::string cleanDir = SanitizeLibraryRoot(targetDirectory.empty() ? defaultLibraryPath : targetDirectory);
        std::filesystem::path dir(cleanDir);
        if (!std::filesystem::exists(dir, ec)) {
            std::filesystem::create_directories(dir, ec);
        }

        std::string safeName = newName.empty() ? (sourceNb->name + " - Copy") : newName;
        std::filesystem::path newPkgPath = dir / (safeName + ".notebook");

        int counter = 1;
        while (std::filesystem::exists(newPkgPath, ec)) {
            newPkgPath = dir / (safeName + " (" + std::to_string(counter++) + ").notebook");
        }

        // Deep copy the notebook structure
        auto copyNb = std::make_shared<Notebook>(safeName, sourceNb->colorTag, sourceNb->iconFile);
        copyNb->filePath = newPkgPath.string();
        copyNb->sections.clear();
        copyNb->sectionGroups.clear();

        // Copy sections and pages
        for (const auto& sec : sourceNb->sections) {
            if (sec) {
                copyNb->sections.push_back(sec->Clone());
            }
        }
        for (const auto& grp : sourceNb->sectionGroups) {
            if (grp) {
                auto newGrp = std::make_shared<SectionGroup>(grp->name);
                for (const auto& sec : grp->sections) {
                    if (sec) {
                        newGrp->AddSection(sec->Clone());
                    }
                }
                copyNb->sectionGroups.push_back(newGrp);
            }
        }

        if (copyNb->sections.empty() && copyNb->sectionGroups.empty()) {
            copyNb->sections.push_back(std::make_shared<Section>("New Section"));
        }
        copyNb->activeSectionIndex = 0;

        // Persist to new SQLite package
        Folio::PageRepository repo;
        if (repo.OpenNotebookPackage(copyNb->filePath)) {
            repo.SaveNotebookAsync(copyNb);
        }

        // If source package has page .ink files, copy them over
        if (!sourceNb->filePath.empty()) {
            std::filesystem::path srcPagesDir = std::filesystem::path(sourceNb->filePath) / "pages";
            std::filesystem::path dstPagesDir = newPkgPath / "pages";
            if (std::filesystem::exists(srcPagesDir, ec)) {
                std::filesystem::create_directories(dstPagesDir, ec);
                for (const auto& entry : std::filesystem::directory_iterator(srcPagesDir, ec)) {
                    if (entry.path().extension() == ".ink") {
                        std::filesystem::copy_file(entry.path(), dstPagesDir / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            }
        }

        RefreshAll();
        return copyNb;
    }

private:
    void LoadConfig() {
        std::error_code ec;
        std::filesystem::path cfgPath = "config/libraries.cfg";
        if (!std::filesystem::exists(cfgPath, ec)) return;

        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t sep = line.find('|');
            if (sep != std::string::npos) {
                std::string name = line.substr(0, sep);
                std::string path = line.substr(sep + 1);
                // Avoid duplicating default library
                if (path != defaultLibraryPath) {
                    LibraryInfo lib;
                    lib.id = "lib_" + std::to_string(libraries.size());
                    lib.name = name;
                    lib.rootPath = path;
                    lib.isDefault = false;
                    ScanLibraryNotebooks(lib);
                    libraries.push_back(lib);
                }
            }
        }
    }

    void SaveConfig() {
        std::error_code ec;
        std::filesystem::create_directories("config", ec);
        std::ofstream out("config/libraries.cfg");
        for (const auto& lib : libraries) {
            if (!lib.isDefault) {
                out << lib.name << "|" << lib.rootPath << "\n";
            }
        }
    }
};
