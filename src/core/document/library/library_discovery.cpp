/**
 * =========================================================================================
 * @file library_discovery.cpp
 * @brief Heuristics, marker management, and path sanitization for the Library Subsystem
 * =========================================================================================
 *
 * ARCHITECTURAL ROLE:
 * Encapsulates all path resolution, marker generation, and directory heuristic operations:
 * 1. Marker Management: Writes self-identifying `library.meta` files to safeguard library
 *    identity when users rename bundles in the OS file manager (stripping .foliolib).
 * 2. Dual-Mode Detection: Recognizes both packaged bundles (*.foliolib) and unpacked marker folders.
 * 3. Path Sanitization: Climbs out of child package directories to prevent recursive encapsulation,
 *    normalizes platform-specific directory separators, and ensures unique paths.
 */

#include "core/document/library/library.hpp"

#include <chrono>
#include <string>

#include "utils/file_manager.hpp"
#include "utils/package_marker.hpp"
#include "utils/logger.hpp"

namespace Folio {

// =========================================================================================
// LibraryInfo Implementation
// =========================================================================================

/**
 * @brief Checks if a given notebook path belongs directly to this library bundle.
 *
 * GENERAL WORKING PROCESS & ALGORITHM:
 * 1. Takes the child notebook package path (e.g. "C:/Notes/Default.foliolib/Math.notebook").
 * 2. Extracts the parent directory path via `FileManager::GetParentPath`.
 * 3. Normalizes both paths (resolving trailing slashes, uppercase/lowercase on case-insensitive
 *    filesystems, and directory separator variations like '/' vs '\\') using
 *    `FileManager::AreEquivalent`.
 *
 * @param nbPath Absolute or relative filesystem path to an evaluated .notebook package.
 * @return bool True if `nbPath` is an immediate child inside this library's `rootPath`.
 */
bool LibraryInfo::ContainsNotebook(const std::string& nbPath) const {
    std::string parent = FileManager::GetParentPath(nbPath);
    return FileManager::AreEquivalent(parent, rootPath);
}

// =========================================================================================
// LibraryManager: Path Resolution & Discovery Helpers
// =========================================================================================

/**
 * @brief Writes or refreshes the self-identifying marker file (library.meta).
 *
 * ARCHITECTURAL PURPOSE & EXTENSION STRIPPING RESILIENCE:
 * When users view libraries in OS File Explorers, they may rename the directory and remove
 * the ".foliolib" extension. The `library.meta` file ensures that FolioNote can unambiguously
 * identify the folder as a library and restore its display title.
 *
 * ALGORITHMIC STEPS:
 * 1. Validate that the target path exists and is indeed a directory.
 * 2. Construct the absolute marker filepath: `<folderPath>/library.meta`.
 * 3. Format key-value metadata text including format identifier, schema version, name, and timestamp.
 * 4. Write to disk using `FileManager::WriteTextAtomic` (write to temp file then rename) to prevent
 *    file corruption during sudden shutdowns or power loss.
 *
 * @param folderPath Absolute filesystem directory path of the library bundle.
 * @param libraryName User-visible display label of the library.
 */
void LibraryManager::WriteLibraryMarker(const std::string& folderPath, const std::string& libraryName) {
    if (!FileManager::IsDirectory(folderPath)) {
        LOG_WARN(LibraryManager, "WriteLibraryMarker skipped: Target directory does not exist: " + folderPath);
        return;
    }

    std::string markerPath = FileManager::JoinPath(folderPath, FOLIO_LIBRARY_MARKER_FILE);
    std::string markerContent =
        "# FolioNote Library Metadata\n"
        "format=FolioNoteLibrary\n"
        "version=1\n"
        "name=" + (libraryName.empty() ? "Library" : libraryName) + "\n"
        "timestamp=" + std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) + "\n";

    if (FileManager::WriteTextAtomic(markerPath, markerContent)) {
        LOG_INFO(LibraryManager, "Verified library marker at: " + markerPath);
    } else {
        LOG_ERROR(LibraryManager, "Failed to write library marker file at: " + markerPath);
    }

    // Ensure the library folder is stamped with Windows Explorer package shell identity (desktop.ini)
    PackageMarker::MarkFolderAsPackage(folderPath, "FolioNote Library Package");
}

/**
 * @brief Determines whether a given filesystem path is a recognized FolioNote library folder.
 *
 * DUAL-MODE DETECTION RULES & LOGIC:
 * A directory is classified as a valid library if it meets ANY of the following criteria:
 * 1. Extension Check: The directory path has the extension `.foliolib`.
 * 2. Metadata Marker Check: Contains a file named `library.meta`.
 * 3. Child Notebook Check: Contains at least one subfolder ending with `.notebook`.
 *
 * @param path Target filesystem path to evaluate.
 * @return bool True if the path represents an existing directory fulfilling library criteria.
 */
bool LibraryManager::IsLibraryFolder(const std::string& path) {
    if (path.empty() || !FileManager::IsDirectory(path)) {
        return false;
    }

    // Rule 1: Packaged bundle check (.foliolib extension)
    if (FileManager::HasExtension(path, FOLIO_LIBRARY_EXTENSION)) {
        return true;
    }

    // Rule 2: Self-identifying marker check (library.meta)
    std::string markerPath = FileManager::JoinPath(path, FOLIO_LIBRARY_MARKER_FILE);
    if (FileManager::Exists(markerPath)) {
        return true;
    }

    // Rule 3: Child .notebook packages check
    auto entries = FileManager::ListEntries(path, FOLIO_NOTEBOOK_EXTENSION);
    for (const auto& entry : entries) {
        if (entry.type == FileType::Directory) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Determines whether a given filesystem path is a valid .notebook package bundle.
 *
 * VALIDATION LOGIC:
 * Path must not be empty, must be a directory on disk, and must have the `.notebook` extension.
 *
 * @param path Filesystem path to evaluate.
 * @return bool True if directory is a `.notebook` bundle.
 */
bool LibraryManager::IsNotebookPackage(const std::string& path) {
    return !path.empty() && FileManager::IsDirectory(path) && FileManager::HasExtension(path, FOLIO_NOTEBOOK_EXTENSION);
}

/**
 * @brief Resolves a library root path on disk, tolerating user modification of the `.foliolib` extension.
 *
 * HEURISTIC ADAPTATION PROCESS:
 * - Case 1 (Direct Match): The declared path exists directly as-is on the disk.
 * - Case 2 (Extension Stripped): The declared path ended with `.foliolib`, but the user removed
 *   the extension in the OS file explorer. Checks if `<parent>/<stem>` exists and satisfies `IsLibraryFolder`.
 * - Case 3 (Extension Added): The declared path was plain, but the user renamed it to `.foliolib`.
 *   Checks if `<parent>/<filename>.foliolib` exists and satisfies `IsLibraryFolder`.
 * - Fallback: Returns `declaredPath` unchanged if no alternate match is found.
 *
 * @param declaredPath Path currently stored in configuration or runtime memory.
 * @return std::string Resolved existing filesystem path, or `declaredPath` if neither variant exists.
 */
std::string LibraryManager::ResolveActualLibraryPath(const std::string& declaredPath) {
    if (declaredPath.empty()) return declaredPath;

    // Case 1: Path exists directly as declared
    if (FileManager::IsDirectory(declaredPath)) {
        return declaredPath;
    }

    std::string parent = FileManager::GetParentPath(declaredPath);

    // Case 2: Declared path had .foliolib, but user stripped it in Explorer/Finder
    if (FileManager::HasExtension(declaredPath, FOLIO_LIBRARY_EXTENSION)) {
        std::string stripped = FileManager::JoinPath(parent, FileManager::GetStem(declaredPath));
        if (FileManager::IsDirectory(stripped) && IsLibraryFolder(stripped)) {
            LOG_INFO(LibraryManager, "Auto-detected unpacked library folder (user removed .foliolib): " + stripped);
            return stripped;
        }
    }

    // Case 3: Declared path was plain, but user appended .foliolib to package it
    std::string withExt = FileManager::JoinPath(parent, FileManager::GetFileName(declaredPath) + FOLIO_LIBRARY_EXTENSION);
    if (FileManager::IsDirectory(withExt) && IsLibraryFolder(withExt)) {
        LOG_INFO(LibraryManager, "Auto-detected packaged library bundle (user added .foliolib): " + withExt);
        return withExt;
    }

    LOG_WARN(LibraryManager, "Library path unresolved or missing on disk: " + declaredPath);
    return declaredPath;
}

/**
 * @brief Extracts a clean, user-friendly display name from a library bundle folder path.
 *
 * PROCESS:
 * Strips directory components and the `.foliolib` extension.
 * Example: `"C:/Notes/Libraries/Medical Research.foliolib"` -> `"Medical Research"`
 *
 * @param bundlePath Filesystem path to the library folder.
 * @return std::string Cleaned display string (fallback: "Unnamed Library").
 */
std::string LibraryManager::ExtractLibraryNameFromPath(const std::string& bundlePath) {
    std::string stem = FileManager::GetStem(bundlePath);
    return stem.empty() ? "Unnamed Library" : stem;
}

/**
 * @brief Cleans and canonicalizes an input path to ensure it represents a valid library directory.
 *
 * GENERAL WORKING PROCESS & SANITIZATION RULES:
 * 1. Loop-strips any trailing `.notebook` package paths to prevent nested encapsulation errors.
 * 2. If the path is empty, ".", or matches "FolioNote", routes to `<defaultParentDir>/<defaultBundleName>`.
 *    (If a legacy bundle `<defaultParentDir>/<defaultBundleName>.foliolib` exists on disk, that is favored).
 * 3. Normalizes all path separators to platform-canonical format without forcing a `.foliolib` extension.
 *
 * @param inputPath User-supplied or configuration directory path.
 * @param defaultParentDir Fallback parent directory to place bundle in.
 * @param defaultBundleName Fallback base name (defaults to "Default").
 * @return std::string Cleaned, canonical absolute path to a library directory.
 */
std::string LibraryManager::SanitizeLibraryRoot(
    const std::string& inputPath,
    const std::string& defaultParentDir,
    const std::string& defaultBundleName
) {
    std::string p = inputPath;

    // 1. Climb out of any .notebook packages to prevent recursion errors
    while (!p.empty() && FileManager::HasExtension(p, FOLIO_NOTEBOOK_EXTENSION)) {
        p = FileManager::GetParentPath(p);
    }

    // 2. If empty or relative dot, place default bundle in defaultParentDir
    if (p.empty() || p == "." || p == "FolioNote") {
        std::string candidateWithExt = FileManager::JoinPath(defaultParentDir, defaultBundleName + FOLIO_LIBRARY_EXTENSION);
        if (FileManager::IsDirectory(candidateWithExt)) {
            return FileManager::NormalizeSeparators(candidateWithExt);
        }
        return FileManager::NormalizeSeparators(FileManager::JoinPath(defaultParentDir, defaultBundleName));
    }

    // 3. For any provided paths, accept as-is and normalize path separators
    return FileManager::NormalizeSeparators(p);
}

} // namespace Folio
