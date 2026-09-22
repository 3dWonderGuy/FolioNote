#pragma once
#include <cstdint>
#include <string>

namespace Folio {

enum class FolioErrorCode : uint32_t {
    Success = 0,

    // -------------------------------------------------------------------------
    // 1000 - 1999: System, OS & General Infrastructure
    // -------------------------------------------------------------------------
    SysUnknownError            = 1000, ///< Unclassified system-level error
    SysOutOfMemory             = 1001, ///< Memory allocation failed
    SysFileNotFound            = 1002, ///< Target file does not exist on disk
    SysDirectoryNotFound       = 1003, ///< Target directory does not exist
    SysFileAccessDenied        = 1004, ///< OS permissions rejected read/write
    SysFileCorrupted           = 1005, ///< File header or payload invalid/truncated
    SysPathResolutionFailed    = 1006, ///< Relative or symbolic path could not be resolved
    SysFileWriteFailed         = 1007, ///< Write operation failed or truncated
    SysFileReadFailed          = 1008, ///< Read operation failed or file handle invalid
    SysFileRenameFailed        = 1009, ///< Atomic rename/swap failed
    SysDirectoryCreateFailed   = 1010, ///< Failed to create directory hierarchy
    SysDirectoryIterateFailed  = 1011, ///< Directory iterator failed or path inaccessible
    SysFileDeleteFailed        = 1012, ///< Failed to delete file or directory tree

    // 1050 - 1099: Concurrency & ThreadPool
    ThreadPoolShuttingDown     = 1050, ///< Enqueue rejected because pool is shutting down
    ThreadPoolWorkerException  = 1051, ///< Uncaught exception escaped a background task
    ThreadPoolInitFailed       = 1052, ///< Failed to spawn worker threads

    // -------------------------------------------------------------------------
    // 2000 - 2999: Storage & SQLite Database Subsystem
    // -------------------------------------------------------------------------
    DbOpenFailed               = 2001, ///< Failed to connect or allocate SQLite handle
    DbSchemaMigrationFailed    = 2002, ///< DDL execution or schema upgrade failed
    DbIndexCreationFailed      = 2003, ///< Relational or spatial index creation error
    DbDisconnected             = 2004, ///< Operation requested on a closed DB handle
    DbTransactionFailed        = 2005, ///< BEGIN, COMMIT, or ROLLBACK failed
    DbQueryFailed              = 2006, ///< PreparedStatement or exec query error
    RepoBlobWriteFailed        = 2101, ///< Failed to compress or write .ink page blob
    RepoBlobReadFailed         = 2102, ///< Failed to read or decompress .ink page blob
    RepoPackageCorrupt         = 2103, ///< Package bundle structure missing essential directories

    // -------------------------------------------------------------------------
    // 3000 - 3999: Document Hierarchy Subsystem
    // -------------------------------------------------------------------------
    DocWorkspaceLoadFailed     = 3001, ///< Root workspace directory failed to initialize
    DocLibraryNotFound         = 3002, ///< Target .foliolib bundle missing or unregistered
    DocLibraryCreateFailed     = 3003, ///< Failed to create .foliolib bundle folder
    DocNotebookNotFound        = 3010, ///< Notebook package not found in library
    DocNotebookLoadFailed      = 3011, ///< Failed to deserialize notebook metadata
    DocNotebookCreateFailed    = 3012, ///< Failed to construct new notebook package
    DocNotebookCloneFailed     = 3013, ///< Deep copy of notebook package failed
    DocSectionNotFound         = 3020, ///< Section GUID does not exist in notebook
    DocSectionCreateFailed     = 3021, ///< Section creation rejected (invalid name/params)
    DocSectionGroupNotFound    = 3030, ///< SectionGroup GUID does not exist
    DocPageNotFound            = 3040, ///< CanvasPage GUID not found in section
    DocPageCreateFailed        = 3041, ///< Page creation rejected or failed
    DocPageUnloaded            = 3042, ///< Attempted operation on evicted RAM page without reloading
    DocPageCloningFailed       = 3043, ///< Deep clone of canvas page failed

    // -------------------------------------------------------------------------
    // 4000 - 4999: Input Subsystem & Hardware State Machine
    // -------------------------------------------------------------------------
    InputWindowUnstable        = 4001, ///< Input discarded: window is resizing or moving
    InputTargetPageNull        = 4002, ///< Input rejected: no active CanvasPage is loaded
    InputDeviceMismatch        = 4003, ///< Event source does not match active device state
    InputInvalidCoordinates    = 4004, ///< Coordinates out of physical screen/camera bounds
    InputGestureUnrecognized   = 4010, ///< Touch multi-finger pattern does not match any gesture
    InputGestureTimeout        = 4011, ///< Hold or tap gesture exceeded maximum recognition window
    InputStylusHoverLost       = 4020, ///< Active digitizer stylus left proximity unexpectedly
    InputToolAssignmentFailed  = 4030, ///< Tool type (Pen, Eraser, Select) could not be assigned
    InputActionTargetMissing   = 4040, ///< Special action (Undo, Redo, Gizmo) lacks active target

    // -------------------------------------------------------------------------
    // 5000 - 5999: Canvas Engine & Spatial Subsystems
    // -------------------------------------------------------------------------
    CanvasObjectNotFound       = 5001, ///< Object UID not registered on page
    CanvasInvalidTransform     = 5002, ///< Scale, zoom, or DPI value non-positive or NaN
    CanvasRTreeCorrupted       = 5010, ///< Spatial index node count or bounding box mismatch
    CanvasRenderContextError   = 5020  ///< Blend2D context allocation or rendering failure
};

inline constexpr const char* FolioErrorCodeToString(FolioErrorCode code) noexcept {
    switch (code) {
        case FolioErrorCode::Success:                  return "Success";

        // 1000s: System
        case FolioErrorCode::SysUnknownError:          return "SysUnknownError";
        case FolioErrorCode::SysOutOfMemory:           return "SysOutOfMemory";
        case FolioErrorCode::SysFileNotFound:          return "SysFileNotFound";
        case FolioErrorCode::SysDirectoryNotFound:     return "SysDirectoryNotFound";
        case FolioErrorCode::SysFileAccessDenied:      return "SysFileAccessDenied";
        case FolioErrorCode::SysFileCorrupted:         return "SysFileCorrupted";
        case FolioErrorCode::SysPathResolutionFailed:  return "SysPathResolutionFailed";
        case FolioErrorCode::SysFileWriteFailed:       return "SysFileWriteFailed";
        case FolioErrorCode::SysFileReadFailed:        return "SysFileReadFailed";
        case FolioErrorCode::SysFileRenameFailed:      return "SysFileRenameFailed";
        case FolioErrorCode::SysDirectoryCreateFailed: return "SysDirectoryCreateFailed";
        case FolioErrorCode::SysDirectoryIterateFailed:return "SysDirectoryIterateFailed";
        case FolioErrorCode::SysFileDeleteFailed:      return "SysFileDeleteFailed";
        case FolioErrorCode::ThreadPoolShuttingDown:   return "ThreadPoolShuttingDown";
        case FolioErrorCode::ThreadPoolWorkerException:return "ThreadPoolWorkerException";
        case FolioErrorCode::ThreadPoolInitFailed:     return "ThreadPoolInitFailed";

        // 2000s: Storage & DB
        case FolioErrorCode::DbOpenFailed:             return "DbOpenFailed";
        case FolioErrorCode::DbSchemaMigrationFailed:  return "DbSchemaMigrationFailed";
        case FolioErrorCode::DbIndexCreationFailed:   return "DbIndexCreationFailed";
        case FolioErrorCode::DbDisconnected:           return "DbDisconnected";
        case FolioErrorCode::DbTransactionFailed:      return "DbTransactionFailed";
        case FolioErrorCode::DbQueryFailed:            return "DbQueryFailed";
        case FolioErrorCode::RepoBlobWriteFailed:      return "RepoBlobWriteFailed";
        case FolioErrorCode::RepoBlobReadFailed:       return "RepoBlobReadFailed";
        case FolioErrorCode::RepoPackageCorrupt:       return "RepoPackageCorrupt";

        // 3000s: Document
        case FolioErrorCode::DocWorkspaceLoadFailed:   return "DocWorkspaceLoadFailed";
        case FolioErrorCode::DocLibraryNotFound:       return "DocLibraryNotFound";
        case FolioErrorCode::DocLibraryCreateFailed:   return "DocLibraryCreateFailed";
        case FolioErrorCode::DocNotebookNotFound:      return "DocNotebookNotFound";
        case FolioErrorCode::DocNotebookLoadFailed:    return "DocNotebookLoadFailed";
        case FolioErrorCode::DocNotebookCreateFailed:  return "DocNotebookCreateFailed";
        case FolioErrorCode::DocNotebookCloneFailed:   return "DocNotebookCloneFailed";
        case FolioErrorCode::DocSectionNotFound:       return "DocSectionNotFound";
        case FolioErrorCode::DocSectionCreateFailed:   return "DocSectionCreateFailed";
        case FolioErrorCode::DocSectionGroupNotFound:  return "DocSectionGroupNotFound";
        case FolioErrorCode::DocPageNotFound:          return "DocPageNotFound";
        case FolioErrorCode::DocPageCreateFailed:      return "DocPageCreateFailed";
        case FolioErrorCode::DocPageUnloaded:          return "DocPageUnloaded";
        case FolioErrorCode::DocPageCloningFailed:     return "DocPageCloningFailed";

        // 4000s: Input
        case FolioErrorCode::InputWindowUnstable:      return "InputWindowUnstable";
        case FolioErrorCode::InputTargetPageNull:      return "InputTargetPageNull";
        case FolioErrorCode::InputDeviceMismatch:      return "InputDeviceMismatch";
        case FolioErrorCode::InputInvalidCoordinates:  return "InputInvalidCoordinates";
        case FolioErrorCode::InputGestureUnrecognized: return "InputGestureUnrecognized";
        case FolioErrorCode::InputGestureTimeout:      return "InputGestureTimeout";
        case FolioErrorCode::InputStylusHoverLost:     return "InputStylusHoverLost";
        case FolioErrorCode::InputToolAssignmentFailed:return "InputToolAssignmentFailed";
        case FolioErrorCode::InputActionTargetMissing: return "InputActionTargetMissing";

        // 5000s: Engine & Canvas
        case FolioErrorCode::CanvasObjectNotFound:     return "CanvasObjectNotFound";
        case FolioErrorCode::CanvasInvalidTransform:   return "CanvasInvalidTransform";
        case FolioErrorCode::CanvasRTreeCorrupted:     return "CanvasRTreeCorrupted";
        case FolioErrorCode::CanvasRenderContextError: return "CanvasRenderContextError";

        default:                                       return "UnknownErrorCode";
    }
}

inline std::string FormatError(FolioErrorCode code, const std::string& details) {
    return "[ERR-" + std::to_string(static_cast<uint32_t>(code)) + ": " + 
           FolioErrorCodeToString(code) + "] " + details;
}

} // namespace Folio

using FolioErrorCode = ::Folio::FolioErrorCode;
using ::Folio::FormatError;
using ::Folio::FolioErrorCodeToString;