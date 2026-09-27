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
    // VIDEO CONSTRAINTS & LIMITS
    // =========================================================================

    /**
     * @brief Maximum video file size allowed for local import (default: 4 GB).
     *
     * Memory Safety Note:
     *   Video files are NOT loaded into RAM — they are streamed from disk by libVLC.
     *   This limit guards only against filesystem indexing of absurdly large files
     *   and prevents accidental import of entire ISO disc images.
     *   Math: 4 * 1024^3 = 4,294,967,296 bytes (4 GB limit)
     */
    size_t maxVideoFileSizeBytes = static_cast<size_t>(4) * 1024 * 1024 * 1024;

    /**
     * @brief Default canvas width in world millimeters for newly dropped video objects (default: 80.0 mm ≈ 3.15 in).
     *
     * At 96 DPI: 80mm * 3.7795 px/mm ≈ 302 pixels wide.
     * Paired with defaultVideoHeightMm = 45.0mm for standard 16:9 aspect ratio.
     */
    double defaultVideoWidthMm  = 80.0;

    /**
     * @brief Default canvas height in world millimeters for newly dropped video objects (default: 45.0 mm).
     *
     * 80.0 / 45.0 = 1.7778 ≈ 16:9 aspect ratio (the global standard for modern video content).
     */
    double defaultVideoHeightMm = 45.0;

    /**
     * @brief Minimum canvas dimension in world millimeters for video objects (default: 20.0 mm).
     *
     * Below 20mm, transport controls would be unreadable and unclickable.
     */
    double minVideoCanvasDimensionMm = 20.0;

    /**
     * @brief Maximum canvas dimension in world millimeters for video objects (default: 500.0 mm).
     *
     * Prevents video containers from growing to canvas-filling sizes that degrade inking performance.
     */
    double maxVideoCanvasDimensionMm = 500.0;

    /**
     * @brief Default audio volume for newly created video objects (range: 0.0 to 2.0).
     *
     * 1.0 = 100% (original media volume).
     * 2.0 = 200% (digital boost via libVLC's software amplifier).
     */
    float defaultVideoVolume = 1.0f;

    /**
     * @brief Enable hardware-accelerated video decoding (D3D11VA on Windows, MediaCodec on Android).
     *
     * When true: libVLC passes "--avcodec-hw=any" to enable hardware acceleration.
     * When false: CPU software decoding only (compatibility fallback for old GPU drivers).
     * Default: true (hardware decoding is strongly preferred for 4K / high-FPS content).
     */
    bool enableHardwareVideoDecoding = true;

    /**
     * @brief Duration in milliseconds before video transport controls auto-hide after mouse inactivity.
     *
     * Keeps the canvas clean during playback; controls reappear on mouse hover.
     * Default: 3000ms (3 seconds).
     */
    uint32_t videoControlsFadeDelayMs = 3000;

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
