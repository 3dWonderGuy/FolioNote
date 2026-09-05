#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <array>
#include "core/document/canvas_page.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/ink_container.hpp"
#include "core/objects/text_box.hpp"
#include "core/objects/image_object.hpp"

namespace Folio {

/**
 * =========================================================================================
 * @file binary_serializer.hpp
 * @brief High-Performance Binary Serialization, Compression & Journaling Engine for FolioNote (.ink)
 * =========================================================================================
 * 
 * --- ARCHITECTURE OVERVIEW ---
 * FolioNote pages contain vector ink strokes, rich text containers, raster image objects, 
 * affine transforms, and interactive command histories.
 * 
 * The persistent storage engine provides 4 tightly integrated pillars:
 * 
 * 1. COMPACT DELTA STAGE (1D Continuous Chain Encoding):
 *    - Hand-drawn strokes are stored as continuous 1D trajectory chains instead of 2D polygon outlines.
 *    - The initial segment stores an absolute anchor: p0(x,y), p1(x,y), and initial width (36 bytes).
 *    - Subsequent connected segments store only 32-bit float DELTAS: (dp1.x, dp1.y, dWidth) (12 bytes).
 *    - Saves ~66% raw memory before compression. Outline polygons are dynamically reconstructed on load.
 * 
 * 2. INTEGRITY & COMPRESSION STAGE (CRC32 Checksum + ZLib DEFLATE):
 *    - Every binary blob includes a 4-byte CRC32 checksum over the compressed payload.
 *    - Defends against disk corruption, half-written files, or bitrot with O(1) instant rejection.
 *    - High-entropy delta floats compress at ~85-92% total space reduction.
 * 
 * 3. MULTI-OBJECT TYPE DISPATCH:
 *    - Native binary layouts for:
 *      * InkContainer (Delta-compressed strokes, highlighter blend mode, pressure segments)
 *      * TextBoxObject (Text buffer, font family, font size, text colors, wrapping flags)
 *      * ImageObject (Natural dimensions, format flags, external paths or embedded PNG/JPEG data)
 * 
 * 4. DELTA PATCHING & COMMAND HISTORY JOURNALING (WAL):
 *    - Fine-grained action journal (Add, Delete, Transform, Re-color, Thickness change, Text edit).
 *    - Enables sub-millisecond append-only journaling (.ink.wal) for crash resilience and live sync,
 *      integrated with CommandHistory undo/redo stacks.
 */

/**
 * @brief High-speed CRC32 (IEEE 802.3 polynomial 0xEDB88320) checksum utility.
 */
class CRC32 {
public:
    static uint32_t Compute(const uint8_t* data, size_t length) noexcept;
};

/**
 * @brief Telemetry and diagnostics statistics for serialization / deserialization operations.
 */
struct SerializationStats {
    size_t rawSizeBytes = 0;          ///< Total uncompressed binary payload size in bytes
    size_t compressedSizeBytes = 0;   ///< Total compressed BLOB size in bytes
    uint32_t crc32Checksum = 0;       ///< CRC-32 integrity checksum
    uint32_t objectCount = 0;         ///< Number of canvas objects processed
    uint32_t strokeCount = 0;         ///< Number of ink strokes processed
    uint32_t segmentCount = 0;        ///< Total geometric segments across all strokes
    double compressionRatio = 0.0;    ///< Compression ratio percentage ((1 - comp/raw) * 100)

    [[nodiscard]] std::string ToString() const;
};

/**
 * @brief High-performance binary writer for little-endian data streams with pre-allocation support.
 */
class ByteWriter {
public:
    std::vector<uint8_t> buffer;

    void Reserve(size_t capacity);
    void Clear() noexcept;
    [[nodiscard]] size_t Size() const noexcept;
    [[nodiscard]] const uint8_t* Data() const noexcept;

    void WriteU8(uint8_t val);
    void WriteBool(bool val);
    void WriteU16(uint16_t val);
    void WriteU32(uint32_t val);
    void WriteU64(uint64_t val);
    void WriteFloat(float val);
    void WriteDouble(double val);
    void WriteString(const std::string& str);
    void WriteBytes(const uint8_t* data, size_t size);
};

/**
 * @brief Endian-safe binary reader with boundary validation and corruption defenses.
 */
class ByteReader {
public:
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t cursor = 0;

    static constexpr uint32_t MAX_SAFE_STRING_LEN = 16 * 1024 * 1024; // 16 MB max string
    static constexpr uint32_t MAX_SAFE_ARRAY_LEN  = 10 * 1000 * 1000; // 10M items max

    ByteReader(const uint8_t* inData, size_t inSize);

    [[nodiscard]] bool HasBytes(size_t count) const noexcept;
    [[nodiscard]] size_t RemainingBytes() const noexcept;

    uint8_t ReadU8();
    bool ReadBool();
    uint16_t ReadU16();
    uint32_t ReadU32();
    uint64_t ReadU64();
    float ReadFloat();
    double ReadDouble();
    std::string ReadString();
    std::vector<uint8_t> ReadBytes(size_t count);
};

/**
 * @brief Operation types recorded in the persistent write-ahead journal (WAL) / command history stream.
 */
enum class JournalOpType : uint8_t {
    AddObject       = 1,  ///< Append a new stroke, text box, or image object to page
    RemoveObject    = 2,  ///< Delete an existing object by GUID
    TransformObject = 3,  ///< Modify affine matrix and spatial bounds (move/scale/rotate)
    ModifyStyle     = 4,  ///< Modify color, brush width, or opacity
    ModifyZOrder    = 5,  ///< Layering change (bring to front, send to back)
    ModifyText      = 6,  ///< Text edit inside a TextBoxObject
    BatchAction     = 7   ///< Compound undo/redo transaction group
};

/**
 * @brief Journal entry representing an atomic canvas modification for WAL and undo/redo synchronization.
 */
struct JournalEntry {
    JournalOpType opType = JournalOpType::AddObject;
    std::string targetGuid = "";
    uint64_t timestampMs = 0;
    std::vector<uint8_t> payload; // Object binary stream or parameter delta
};

/**
 * @brief Binary serialization, decompression, and journaling engine for FolioNote canvas pages.
 */
class BinarySerializer {
public:
    /// Magic 4-byte header identifying valid FolioNote Canvas Page binary streams: "FNPG" (0x464E5047)
    static constexpr uint32_t MAGIC_HEADER = 0x464E5047;

    /// Current binary format specification version (Version 2 includes CRC32 container checksum & multi-object support)
    static constexpr uint32_t FORMAT_VERSION = 2;

    /**
     * @brief Estimates raw uncompressed memory requirement for a page to optimize vector allocation.
     */
    static size_t EstimatePageByteSize(const CanvasPage& page);

    /**
     * @brief Compresses uncompressed binary data using zlib/deflate (tdefl).
     */
    static bool Compress(const std::vector<uint8_t>& uncompressed, std::vector<uint8_t>& outCompressed);

    /**
     * @brief Decompresses zlib/deflate compressed data (tinfl).
     */
    static bool Decompress(const uint8_t* compressedData, size_t compressedSize, size_t uncompressedSize, std::vector<uint8_t>& outUncompressed);

    /**
     * @brief Serializes a single CanvasObject into a ByteWriter stream.
     */
    static void SerializeObject(const std::shared_ptr<CanvasObject>& obj, ByteWriter& writer, SerializationStats& stats);

    /**
     * @brief Deserializes a single CanvasObject from a ByteReader stream.
     */
    static std::shared_ptr<CanvasObject> DeserializeObject(ByteReader& reader, SerializationStats& stats);

    /**
     * @brief Serializes a CanvasPage into a compressed binary BLOB (.ink format) with CRC32 integrity checksum.
     */
    static bool SerializePage(const CanvasPage& page, std::vector<uint8_t>& outCompressedBlob, SerializationStats* outStats = nullptr);

    /**
     * @brief Deserializes a compressed binary BLOB with CRC32 integrity verification into a CanvasPage.
     */
    static bool DeserializePage(const uint8_t* blobData, size_t blobSize, CanvasPage& outPage, SerializationStats* outStats = nullptr);

    /**
     * @brief Serializes a single atomic journal transaction entry.
     */
    static bool SerializeJournalEntry(const JournalEntry& entry, std::vector<uint8_t>& outBytes);

    /**
     * @brief Deserializes a journal entry from raw stream bytes.
     */
    static bool DeserializeJournalEntry(const uint8_t* data, size_t size, JournalEntry& outEntry);

    /**
     * @brief Applies an atomic journal entry to an active in-memory CanvasPage.
     */
    static bool ApplyJournalEntry(CanvasPage& page, const JournalEntry& entry);
};

} // namespace Folio
