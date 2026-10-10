#pragma once
/**
 * =========================================================================================
 * @file io/platform/system_dialogs.hpp
 * @brief Native OS file picker dialogs and system shell launcher
 * =========================================================================================
 */

#include <string>

namespace Folio {

class SystemDialogs {
public:
    /**
     * @brief Opens a native OS file picker dialog to select an existing file.
     * @param title Dialog window title.
     * @return Selected UTF-8 path string, or empty string on cancellation or error.
     */
    static std::string ShowOpenFileDialog(const std::string& title = "Select File");

    /**
     * @brief Opens a native OS save file dialog to select a destination path.
     * @param title Dialog window title.
     * @param defaultFileName Default file name suggested in the dialog.
     * @return Selected UTF-8 destination path string, or empty string on cancellation or error.
     */
    static std::string ShowSaveFileDialog(const std::string& title = "Save File", const std::string& defaultFileName = "");

    /**
     * @brief Launches a file or URL with the operating system's default application.
     * @param pathOrUrl Target filesystem path or URL to open.
     * @return true if successfully dispatched; false otherwise.
     */
    static bool OpenWithDefaultApp(const std::string& pathOrUrl);
};

} // namespace Folio
