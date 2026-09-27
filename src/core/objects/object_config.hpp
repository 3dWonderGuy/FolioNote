#pragma once
/**
 * =========================================================================================
 * @file core/objects/object_config.hpp
 * @brief Centralized Configuration and Runtime Constraints for Canvas Objects
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & WORKING PROCESS:
 * ---------------------------------------
 * ObjectConfig serves as the single source of truth for runtime limits, physical canvas
 * boundaries, and safety constraints across all canvas object types (images, attachments,
 * hyperlinks, and interactive chips).
 *
 * Key Design Principles:
 *   1. Decoupled Core:
 *      Lives within `src/core/objects/` without dependencies on UI frameworks (ImGui),
 *      JSON serializers, or application lifecycle singletons.
 *   2. Instant Zero-Overhead Access:
 *      Objects query `ObjectConfig::Get()` on the rendering hot path (up to 120 FPS)
 *      with direct POD member reads.
 *   3. Bidirectional Sync with SettingsManager:
 *      SettingsManager loads user configurations from `settings.json`, updates its
 *      internal ObjectConfig instance, and synchronizes the global `ObjectConfig::Get()`
 *      singleton during startup and when the user modifies settings.
 */

#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace Folio {

/**
 * @struct ObjectConfig
 * @brief Centralized constraints, physical size limits, and ingestion safety thresholds.
 */
struct ObjectConfig {
    // =========================================================================
    // IMAGE CONSTRAINTS & LIMITS
    // =========================================================================

    /**
     * @brief Hard maximum pixel boundary for display proxy surfaces (default: 4096 px).
     *
     * Mathematical RAM Protection:
     *   Uncompressed 32-bit RGBA RAM = width * height * 4 bytes.
     *   A 16000x16000 scan = 1,024,000,000 bytes (1.02 GB RAM).
     *   Clamped to 4096px = 4096 * 4096 * 4 = 67.1 MB RAM (94% reduction).
     */
    uint32_t maxDecodedPixelDimension = 4096;

    /**
     * @brief Maximum physical dimension in world millimeters for placed images (default: 200.0 mm).
     */
    double maxImageCanvasDimensionMm = 200.0;

    /**
     * @brief Minimum physical dimension in world millimeters to prevent sub-millimeter disappearance (default: 5.0 mm).
     */
    double minImageCanvasDimensionMm = 5.0;

    /**
     * @brief Fallback display density in dots-per-inch when DPI is unspecified or invalid (default: 96.0 DPI).
     */
    double defaultImageDpi = 96.0;

    /**
     * @brief Maximum raw byte buffer size accepted for image ingestion (default: 100 MB).
     * Protects process memory against corrupted or malicious decompressed payload bombs.
     */
    size_t maxImageFileSizeBytes = 100 * 1024 * 1024;

    // =========================================================================
    // ATTACHMENT CONSTRAINTS & LIMITS
    // =========================================================================

    /**
     * @brief Maximum byte buffer size permitted when embedding external files into package sidecar (default: 100 MB).
     */
    size_t maxAttachmentFileSizeBytes = 100 * 1024 * 1024;

    /**
     * @brief Default width in world millimeters for interactive attachment chips (default: 50.0 mm).
     */
    double attachmentChipWidthMm = 50.0;

    /**
     * @brief Default height in world millimeters for interactive attachment chips (default: 18.0 mm).
     */
    double attachmentChipHeightMm = 18.0;

    // =========================================================================
    // GLOBAL SINGLETON ACCESS
    // =========================================================================

    /**
     * @brief Access the process-wide active ObjectConfig instance.
     * @return Reference to the thread-safe static ObjectConfig singleton.
     */
    static ObjectConfig& Get() noexcept {
        static ObjectConfig instance;
        return instance;
    }
};

} // namespace Folio
