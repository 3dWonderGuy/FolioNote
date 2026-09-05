#include "core/storage/binary_serializer.hpp"
#include "core/engine/stroke_outline_builder.hpp"
#include "utils/uid_generator.hpp"
#include "utils/guid_generator.hpp"
#include "utils/logger.hpp"
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iomanip>

// Lightweight compression via miniz in SDL
#ifdef MINIZ_NO_INFLATE_APIS
#undef MINIZ_NO_INFLATE_APIS
#endif
#ifdef MINIZ_NO_DEFLATE_APIS
#undef MINIZ_NO_DEFLATE_APIS
#endif
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
#include "../../third_party/SDL/src/video/miniz.h"

namespace Folio {

// --- CRC32 Implementation ---

static constexpr std::array<uint32_t, 256> GenerateCRC32Table() {
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        tbl[i] = c;
    }
    return tbl;
}

uint32_t CRC32::Compute(const uint8_t* data, size_t length) noexcept {
    static constexpr auto table = GenerateCRC32Table();
    uint32_t crc = 0xFFFFFFFFL;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFL;
}

// --- SerializationStats Implementation ---

std::string SerializationStats::ToString() const {
    std::ostringstream oss;
    oss << "Objects: " << objectCount 
        << " | Strokes: " << strokeCount 
        << " | Segments: " << segmentCount
        << " | Raw: " << rawSizeBytes << " B"
        << " | Compressed: " << compressedSizeBytes << " B"
        << " | CRC32: 0x" << std::hex << std::uppercase << crc32Checksum << std::dec
        << " | Ratio: " << std::fixed << std::setprecision(1) << compressionRatio << "%";
    return oss.str();
}

// --- ByteWriter Implementation ---

void ByteWriter::Reserve(size_t capacity) {
    buffer.reserve(capacity);
}

void ByteWriter::Clear() noexcept {
    buffer.clear();
}

size_t ByteWriter::Size() const noexcept {
    return buffer.size();
}

const uint8_t* ByteWriter::Data() const noexcept {
    return buffer.data();
}

void ByteWriter::WriteU8(uint8_t val) {
    buffer.push_back(val);
}

void ByteWriter::WriteBool(bool val) {
    buffer.push_back(val ? 1 : 0);
}

void ByteWriter::WriteU16(uint16_t val) {
    buffer.push_back(static_cast<uint8_t>(val & 0xFF));
    buffer.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void ByteWriter::WriteU32(uint32_t val) {
    buffer.push_back(static_cast<uint8_t>(val & 0xFF));
    buffer.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void ByteWriter::WriteU64(uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buffer.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

void ByteWriter::WriteFloat(float val) {
    static_assert(sizeof(float) == sizeof(uint32_t), "Float must be 32-bit IEEE 754");
    uint32_t asUint;
    std::memcpy(&asUint, &val, sizeof(float));
    WriteU32(asUint);
}

void ByteWriter::WriteDouble(double val) {
    static_assert(sizeof(double) == sizeof(uint64_t), "Double must be 64-bit IEEE 754");
    uint64_t asUint;
    std::memcpy(&asUint, &val, sizeof(double));
    WriteU64(asUint);
}

void ByteWriter::WriteString(const std::string& str) {
    WriteU32(static_cast<uint32_t>(str.size()));
    if (!str.empty()) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(str.data());
        buffer.insert(buffer.end(), ptr, ptr + str.size());
    }
}

void ByteWriter::WriteBytes(const uint8_t* data, size_t size) {
    if (data && size > 0) {
        buffer.insert(buffer.end(), data, data + size);
    }
}

// --- ByteReader Implementation ---

ByteReader::ByteReader(const uint8_t* inData, size_t inSize)
    : data(inData), size(inSize), cursor(0) {}

bool ByteReader::HasBytes(size_t count) const noexcept {
    return (cursor + count) <= size;
}

size_t ByteReader::RemainingBytes() const noexcept {
    return (cursor < size) ? (size - cursor) : 0;
}

uint8_t ByteReader::ReadU8() {
    if (!HasBytes(1)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream while reading U8 at offset " + std::to_string(cursor));
        throw std::runtime_error("Unexpected end of binary stream reading U8");
    }
    return data[cursor++];
}

bool ByteReader::ReadBool() {
    return ReadU8() != 0;
}

uint16_t ByteReader::ReadU16() {
    if (!HasBytes(2)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream while reading U16 at offset " + std::to_string(cursor));
        throw std::runtime_error("Unexpected end of binary stream reading U16");
    }
    uint16_t val = static_cast<uint16_t>(data[cursor]) |
                   (static_cast<uint16_t>(data[cursor + 1]) << 8);
    cursor += 2;
    return val;
}

uint32_t ByteReader::ReadU32() {
    if (!HasBytes(4)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream while reading U32 at offset " + std::to_string(cursor));
        throw std::runtime_error("Unexpected end of binary stream reading U32");
    }
    uint32_t val = static_cast<uint32_t>(data[cursor]) |
                   (static_cast<uint32_t>(data[cursor + 1]) << 8) |
                   (static_cast<uint32_t>(data[cursor + 2]) << 16) |
                   (static_cast<uint32_t>(data[cursor + 3]) << 24);
    cursor += 4;
    return val;
}

uint64_t ByteReader::ReadU64() {
    if (!HasBytes(8)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream while reading U64 at offset " + std::to_string(cursor));
        throw std::runtime_error("Unexpected end of binary stream reading U64");
    }
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<uint64_t>(data[cursor + i]) << (i * 8));
    }
    cursor += 8;
    return val;
}

float ByteReader::ReadFloat() {
    uint32_t asUint = ReadU32();
    float val;
    std::memcpy(&val, &asUint, sizeof(float));
    return val;
}

double ByteReader::ReadDouble() {
    uint64_t asUint = ReadU64();
    double val;
    std::memcpy(&val, &asUint, sizeof(double));
    return val;
}

std::string ByteReader::ReadString() {
    uint32_t len = ReadU32();
    if (len == 0) return "";
    if (len > MAX_SAFE_STRING_LEN) {
        LOG_ERROR(BinarySerializer, "Corrupted string length header (" + std::to_string(len) + " bytes) exceeds maximum safe limit");
        throw std::runtime_error("Corrupted string length exceeds safe limit");
    }
    if (!HasBytes(len)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream reading String of length " + std::to_string(len) + " at offset " + std::to_string(cursor));
        throw std::runtime_error("Unexpected end of binary stream reading String");
    }
    std::string str(reinterpret_cast<const char*>(data + cursor), len);
    cursor += len;
    return str;
}

std::vector<uint8_t> ByteReader::ReadBytes(size_t count) {
    if (count == 0) return {};
    if (count > MAX_SAFE_STRING_LEN) {
        LOG_ERROR(BinarySerializer, "ReadBytes exceeded maximum safe allocation (" + std::to_string(count) + " B)");
        throw std::runtime_error("ReadBytes exceeded maximum safe allocation");
    }
    if (!HasBytes(count)) {
        LOG_ERROR(BinarySerializer, "Unexpected end of binary stream reading " + std::to_string(count) + " bytes");
        throw std::runtime_error("Unexpected end of binary stream reading bytes");
    }
    std::vector<uint8_t> result(data + cursor, data + cursor + count);
    cursor += count;
    return result;
}

// --- BinarySerializer Implementation ---

static int MinizPutBufCallback(const void* pBuf, int len, void* pUser) {
    auto* outVec = static_cast<std::vector<uint8_t>*>(pUser);
    const uint8_t* bytes = static_cast<const uint8_t*>(pBuf);
    outVec->insert(outVec->end(), bytes, bytes + len);
    return 1;
}

size_t BinarySerializer::EstimatePageByteSize(const CanvasPage& page) {
    size_t estimatedBytes = 256;
    for (const auto& obj : page.objects) {
        if (!obj) continue;
        estimatedBytes += 128;
        if (obj->type == ObjectType::InkContainer) {
            auto ink = std::static_pointer_cast<InkContainer>(obj);
            for (const auto& stroke : ink->strokes) {
                estimatedBytes += 16;
                if (!stroke.segments.empty()) {
                    estimatedBytes += 36;
                    estimatedBytes += (stroke.segments.size() - 1) * 12;
                }
            }
        } else if (obj->type == ObjectType::Text) {
            auto txt = std::static_pointer_cast<TextBoxObject>(obj);
            estimatedBytes += 64 + txt->text.size();
        } else if (obj->type == ObjectType::Image) {
            auto img = std::static_pointer_cast<ImageObject>(obj);
            estimatedBytes += 64 + img->imagePath.size() + img->embeddedData.size();
        }
    }
    return estimatedBytes;
}

bool BinarySerializer::Compress(const std::vector<uint8_t>& uncompressed, std::vector<uint8_t>& outCompressed) {
    if (uncompressed.empty()) {
        outCompressed.clear();
        return true;
    }

    outCompressed.clear();
    auto comp = std::make_unique<tdefl_compressor>();
    tdefl_status status = tdefl_init(comp.get(), MinizPutBufCallback, &outCompressed, TDEFL_WRITE_ZLIB_HEADER | 128);
    if (status != TDEFL_STATUS_OKAY) {
        LOG_ERROR(BinarySerializer, "Failed to initialize miniz tdefl_compressor (status: " + std::to_string(status) + ")");
        return false;
    }

    status = tdefl_compress_buffer(comp.get(), uncompressed.data(), uncompressed.size(), TDEFL_FINISH);
    if (status != TDEFL_STATUS_DONE) {
        LOG_ERROR(BinarySerializer, "tdefl_compress_buffer failed with status: " + std::to_string(status));
        return false;
    }

    return true;
}

bool BinarySerializer::Decompress(const uint8_t* compressedData, size_t compressedSize, size_t uncompressedSize, std::vector<uint8_t>& outUncompressed) {
    if (compressedSize == 0 || uncompressedSize == 0) {
        outUncompressed.clear();
        return true;
    }

    if (uncompressedSize > ByteReader::MAX_SAFE_STRING_LEN * 4) {
        LOG_ERROR(BinarySerializer, "Decompress rejected: uncompressed size header (" + std::to_string(uncompressedSize) + " B) exceeds safety limit");
        return false;
    }

    outUncompressed.resize(uncompressedSize);
    tinfl_decompressor decomp;
    tinfl_init(&decomp);
    size_t inBytes = compressedSize;
    size_t outBytes = uncompressedSize;
    
    tinfl_status status = tinfl_decompress(
        &decomp, 
        compressedData, 
        &inBytes, 
        outUncompressed.data(), 
        outUncompressed.data(), 
        &outBytes, 
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
    );

    if (status != TINFL_STATUS_DONE) {
        LOG_ERROR(BinarySerializer, "tinfl_decompress failed with status code: " + std::to_string(status) + " (InBytes: " + std::to_string(inBytes) + ", OutBytes: " + std::to_string(outBytes) + ")");
        return false;
    }

    return true;
}

void BinarySerializer::SerializeObject(const std::shared_ptr<CanvasObject>& obj, ByteWriter& writer, SerializationStats& stats) {
    if (!obj) return;

    writer.WriteU8(static_cast<uint8_t>(obj->type));
    std::string objGuid = obj->guuid.empty() ? GUIDGenerator::GenerateV4() : obj->guuid;
    writer.WriteString(objGuid);

    writer.WriteDouble(obj->bounds.minX);
    writer.WriteDouble(obj->bounds.minY);
    writer.WriteDouble(obj->bounds.maxX);
    writer.WriteDouble(obj->bounds.maxY);

    writer.WriteDouble(obj->transform.m00);
    writer.WriteDouble(obj->transform.m01);
    writer.WriteDouble(obj->transform.m10);
    writer.WriteDouble(obj->transform.m11);
    writer.WriteDouble(obj->transform.m20);
    writer.WriteDouble(obj->transform.m21);

    writer.WriteU32(static_cast<uint32_t>(obj->zOrder));
    writer.WriteFloat(obj->opacity);
    writer.WriteBool(obj->isVisible != 0);
    writer.WriteBool(obj->isLocked != 0);
    writer.WriteBool(obj->isSelectable != 0);

    if (obj->type == ObjectType::InkContainer) {
        auto ink = std::static_pointer_cast<InkContainer>(obj);
        writer.WriteBool(ink->isHighlighter);
        writer.WriteU32(static_cast<uint32_t>(ink->strokes.size()));
        stats.strokeCount += static_cast<uint32_t>(ink->strokes.size());

        for (const auto& stroke : ink->strokes) {
            writer.WriteU32(stroke.color.value);
            writer.WriteDouble(stroke.baseWidth);

            uint32_t segCount = static_cast<uint32_t>(stroke.segments.size());
            writer.WriteU32(segCount);
            stats.segmentCount += segCount;

            if (segCount > 0) {
                const auto& seg0 = stroke.segments[0];
                writer.WriteDouble(seg0.p0.x);
                writer.WriteDouble(seg0.p0.y);
                writer.WriteDouble(seg0.p1.x);
                writer.WriteDouble(seg0.p1.y);
                writer.WriteFloat(seg0.width);

                for (size_t s = 1; s < stroke.segments.size(); ++s) {
                    const auto& prev = stroke.segments[s - 1];
                    const auto& cur = stroke.segments[s];
                    float dp1x = static_cast<float>(cur.p1.x - prev.p1.x);
                    float dp1y = static_cast<float>(cur.p1.y - prev.p1.y);
                    float dWidth = cur.width - prev.width;
                    writer.WriteFloat(dp1x);
                    writer.WriteFloat(dp1y);
                    writer.WriteFloat(dWidth);
                }
            }
        }
    } else if (obj->type == ObjectType::Text) {
        auto txt = std::static_pointer_cast<TextBoxObject>(obj);
        writer.WriteDouble(txt->worldX);
        writer.WriteDouble(txt->worldY);
        writer.WriteDouble(txt->worldWidth);
        writer.WriteDouble(txt->worldHeight);
        writer.WriteString(txt->text);
        writer.WriteString(txt->fontFamily);
        writer.WriteFloat(txt->fontSize);
        writer.WriteU32(txt->textColor.value);
        writer.WriteBool(txt->isWrap);
    } else if (obj->type == ObjectType::Image) {
        auto img = std::static_pointer_cast<ImageObject>(obj);
        writer.WriteDouble(img->worldX);
        writer.WriteDouble(img->worldY);
        writer.WriteDouble(img->worldWidth);
        writer.WriteDouble(img->worldHeight);
        writer.WriteU32(img->naturalWidth);
        writer.WriteU32(img->naturalHeight);
        writer.WriteU8(static_cast<uint8_t>(img->imageFormat));
        writer.WriteString(img->imagePath);
        writer.WriteU32(static_cast<uint32_t>(img->embeddedData.size()));
        writer.WriteBytes(img->embeddedData.data(), img->embeddedData.size());
    } else {
        LOG_WARN(BinarySerializer, "Serializing generic object with type ID: " + std::to_string(static_cast<int>(obj->type)));
    }
}

std::shared_ptr<CanvasObject> BinarySerializer::DeserializeObject(ByteReader& reader, SerializationStats& stats) {
    ObjectType type = static_cast<ObjectType>(reader.ReadU8());
    std::string objGuid = reader.ReadString();

    AABB bounds;
    bounds.minX = reader.ReadDouble();
    bounds.minY = reader.ReadDouble();
    bounds.maxX = reader.ReadDouble();
    bounds.maxY = reader.ReadDouble();

    BLMatrix2D transform;
    transform.m00 = reader.ReadDouble();
    transform.m01 = reader.ReadDouble();
    transform.m10 = reader.ReadDouble();
    transform.m11 = reader.ReadDouble();
    transform.m20 = reader.ReadDouble();
    transform.m21 = reader.ReadDouble();

    int32_t zOrder = static_cast<int32_t>(reader.ReadU32());
    float opacity = reader.ReadFloat();
    bool isVisible = reader.ReadBool();
    bool isLocked = reader.ReadBool();
    bool isSelectable = reader.ReadBool();

    if (type == ObjectType::InkContainer) {
        auto ink = std::make_shared<InkContainer>();
        ink->guuid = objGuid;
        ink->uid = UIDGenerator::Next();
        ink->bounds = bounds;
        ink->transform = transform;
        ink->zOrder = zOrder;
        ink->opacity = opacity;
        ink->isVisible = isVisible ? 1 : 0;
        ink->isLocked = isLocked ? 1 : 0;
        ink->isSelectable = isSelectable ? 1 : 0;

        ink->isHighlighter = reader.ReadBool();
        uint32_t strokeCount = reader.ReadU32();
        if (strokeCount > ByteReader::MAX_SAFE_ARRAY_LEN) {
            throw std::runtime_error("Corrupt stroke count: " + std::to_string(strokeCount));
        }

        ink->strokes.reserve(strokeCount);
        stats.strokeCount += strokeCount;

        for (uint32_t s = 0; s < strokeCount; ++s) {
            Stroke stroke;
            stroke.color.value = reader.ReadU32();
            stroke.baseWidth = reader.ReadDouble();

            uint32_t segCount = reader.ReadU32();
            if (segCount > ByteReader::MAX_SAFE_ARRAY_LEN) {
                throw std::runtime_error("Corrupt segment count: " + std::to_string(segCount));
            }

            stroke.segments.reserve(segCount);
            stats.segmentCount += segCount;

            if (segCount > 0) {
                Segment1D seg0;
                seg0.p0.x = reader.ReadDouble();
                seg0.p0.y = reader.ReadDouble();
                seg0.p1.x = reader.ReadDouble();
                seg0.p1.y = reader.ReadDouble();
                seg0.width = reader.ReadFloat();
                stroke.segments.push_back(seg0);

                Point2D lastP1 = seg0.p1;
                float lastWidth = seg0.width;

                for (uint32_t sg = 1; sg < segCount; ++sg) {
                    Segment1D seg;
                    seg.p0 = lastP1;
                    seg.p1.x = lastP1.x + static_cast<double>(reader.ReadFloat());
                    seg.p1.y = lastP1.y + static_cast<double>(reader.ReadFloat());
                    seg.width = lastWidth + reader.ReadFloat();

                    lastP1 = seg.p1;
                    lastWidth = seg.width;
                    stroke.segments.push_back(seg);
                }

                std::vector<StrokeOutlineBuilder::InputPoint> pts;
                pts.reserve(stroke.segments.size() + 1);
                {
                    const auto& s0 = stroke.segments[0];
                    StrokeOutlineBuilder::InputPoint p0;
                    p0.x = s0.p0.x; p0.y = s0.p0.y; p0.width = s0.width;
                    pts.push_back(p0);
                }
                for (const auto& segItem : stroke.segments) {
                    StrokeOutlineBuilder::InputPoint p;
                    p.x = segItem.p1.x; p.y = segItem.p1.y; p.width = segItem.width;
                    pts.push_back(p);
                }
                stroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, CapType::Round);
            }
            ink->strokes.push_back(std::move(stroke));
        }
        return ink;

    } else if (type == ObjectType::Text) {
        auto txt = std::make_shared<TextBoxObject>();
        txt->guuid = objGuid;
        txt->uid = UIDGenerator::Next();
        txt->bounds = bounds;
        txt->transform = transform;
        txt->zOrder = zOrder;
        txt->opacity = opacity;
        txt->isVisible = isVisible ? 1 : 0;
        txt->isLocked = isLocked ? 1 : 0;
        txt->isSelectable = isSelectable ? 1 : 0;

        txt->worldX = reader.ReadDouble();
        txt->worldY = reader.ReadDouble();
        txt->worldWidth = reader.ReadDouble();
        txt->worldHeight = reader.ReadDouble();
        txt->text = reader.ReadString();
        txt->fontFamily = reader.ReadString();
        txt->fontSize = reader.ReadFloat();
        txt->textColor.value = reader.ReadU32();
        txt->isWrap = reader.ReadBool();
        return txt;

    } else if (type == ObjectType::Image) {
        auto img = std::make_shared<ImageObject>();
        img->guuid = objGuid;
        img->uid = UIDGenerator::Next();
        img->bounds = bounds;
        img->transform = transform;
        img->zOrder = zOrder;
        img->opacity = opacity;
        img->isVisible = isVisible ? 1 : 0;
        img->isLocked = isLocked ? 1 : 0;
        img->isSelectable = isSelectable ? 1 : 0;

        img->worldX = reader.ReadDouble();
        img->worldY = reader.ReadDouble();
        img->worldWidth = reader.ReadDouble();
        img->worldHeight = reader.ReadDouble();
        img->naturalWidth = reader.ReadU32();
        img->naturalHeight = reader.ReadU32();
        img->imageFormat = static_cast<ImageFormat>(reader.ReadU8());
        img->imagePath = reader.ReadString();

        uint32_t embeddedSize = reader.ReadU32();
        if (embeddedSize > 0) {
            img->embeddedData = reader.ReadBytes(embeddedSize);
        }
        return img;
    }

    LOG_WARN(BinarySerializer, "Skipping unrecognized object type: " + std::to_string(static_cast<int>(type)));
    return nullptr;
}

bool BinarySerializer::SerializePage(const CanvasPage& page, std::vector<uint8_t>& outCompressedBlob, SerializationStats* outStats) {
    ByteWriter writer;
    SerializationStats stats;

    size_t estimatedCapacity = EstimatePageByteSize(page);
    writer.Reserve(estimatedCapacity);

    writer.WriteU32(MAGIC_HEADER);
    writer.WriteU32(FORMAT_VERSION);

    writer.WriteString(page.guid);
    writer.WriteString(page.title);
    writer.WriteString(page.createdDateStr);
    writer.WriteString(page.createdTimeStr);

    writer.WriteU32(static_cast<uint32_t>(page.objects.size()));
    stats.objectCount = static_cast<uint32_t>(page.objects.size());

    for (const auto& obj : page.objects) {
        SerializeObject(obj, writer, stats);
    }

    stats.rawSizeBytes = writer.buffer.size();

    std::vector<uint8_t> compressedData;
    if (!Compress(writer.buffer, compressedData)) {
        LOG_ERROR(BinarySerializer, "Failed to compress page serialization stream for page GUID: " + page.guid);
        return false;
    }

    uint32_t crc = CRC32::Compute(compressedData.data(), compressedData.size());
    stats.crc32Checksum = crc;

    outCompressedBlob.clear();
    outCompressedBlob.reserve(8 + compressedData.size());

    uint32_t rawSize = static_cast<uint32_t>(writer.buffer.size());
    outCompressedBlob.push_back(static_cast<uint8_t>(rawSize & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((rawSize >> 8) & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((rawSize >> 16) & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((rawSize >> 24) & 0xFF));

    outCompressedBlob.push_back(static_cast<uint8_t>(crc & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    outCompressedBlob.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));

    outCompressedBlob.insert(outCompressedBlob.end(), compressedData.begin(), compressedData.end());

    stats.compressedSizeBytes = outCompressedBlob.size();
    if (stats.rawSizeBytes > 0) {
        stats.compressionRatio = (1.0 - (static_cast<double>(stats.compressedSizeBytes) / static_cast<double>(stats.rawSizeBytes))) * 100.0;
    }

    LOG_INFO(BinarySerializer, "Serialized page '" + page.title + "' (" + page.guid + ") -> " + stats.ToString());

    if (outStats) {
        *outStats = stats;
    }
    return true;
}

bool BinarySerializer::DeserializePage(const uint8_t* blobData, size_t blobSize, CanvasPage& outPage, SerializationStats* outStats) {
    if (!blobData || blobSize < 8) {
        LOG_ERROR(BinarySerializer, "DeserializePage failed: input blob is null or smaller than minimum container header (size: " + std::to_string(blobSize) + " B)");
        return false;
    }

    SerializationStats stats;
    stats.compressedSizeBytes = blobSize;

    uint32_t uncompressedSize = static_cast<uint32_t>(blobData[0]) |
                               (static_cast<uint32_t>(blobData[1]) << 8) |
                               (static_cast<uint32_t>(blobData[2]) << 16) |
                               (static_cast<uint32_t>(blobData[3]) << 24);

    stats.rawSizeBytes = uncompressedSize;

    const uint8_t* payloadPtr = nullptr;
    size_t payloadSize = 0;
    bool hasCRC = false;
    uint32_t expectedCRC = 0;

    if (blobSize >= 12) {
        expectedCRC = static_cast<uint32_t>(blobData[4]) |
                      (static_cast<uint32_t>(blobData[5]) << 8) |
                      (static_cast<uint32_t>(blobData[6]) << 16) |
                      (static_cast<uint32_t>(blobData[7]) << 24);

        uint32_t actualCRC = CRC32::Compute(blobData + 8, blobSize - 8);
        if (actualCRC == expectedCRC) {
            hasCRC = true;
            payloadPtr = blobData + 8;
            payloadSize = blobSize - 8;
            stats.crc32Checksum = actualCRC;
        }
    }

    if (!hasCRC) {
        payloadPtr = blobData + 4;
        payloadSize = blobSize - 4;
        stats.crc32Checksum = 0;
    }

    std::vector<uint8_t> uncompressed;
    if (!Decompress(payloadPtr, payloadSize, uncompressedSize, uncompressed)) {
        LOG_ERROR(BinarySerializer, "DeserializePage failed: zlib decompression error for blob size " + std::to_string(blobSize) + " B");
        return false;
    }

    try {
        ByteReader reader(uncompressed.data(), uncompressed.size());

        uint32_t magic = reader.ReadU32();
        if (magic != MAGIC_HEADER) {
            LOG_ERROR(BinarySerializer, "DeserializePage failed: Invalid Magic Header 0x" + std::to_string(magic) + " (Expected: 0x464E5047 / 'FNPG')");
            return false;
        }

        uint32_t version = reader.ReadU32();
        if (version > FORMAT_VERSION) {
            LOG_ERROR(BinarySerializer, "DeserializePage failed: Unsupported binary format version " + std::to_string(version) + " (Current supported: " + std::to_string(FORMAT_VERSION) + ")");
            return false;
        }

        outPage.guid = reader.ReadString();
        outPage.title = reader.ReadString();
        outPage.createdDateStr = reader.ReadString();
        outPage.createdTimeStr = reader.ReadString();

        outPage.Clear();

        uint32_t objectCount = reader.ReadU32();
        if (objectCount > ByteReader::MAX_SAFE_ARRAY_LEN) {
            LOG_ERROR(BinarySerializer, "DeserializePage failed: object count exceeds safe limit (" + std::to_string(objectCount) + ")");
            return false;
        }

        outPage.objects.reserve(objectCount);
        stats.objectCount = objectCount;

        for (uint32_t i = 0; i < objectCount; ++i) {
            auto obj = DeserializeObject(reader, stats);
            if (obj) {
                outPage.AddObject(obj);
            }
        }

        outPage.isModified = false;
        outPage.Touch();

        if (stats.rawSizeBytes > 0) {
            stats.compressionRatio = (1.0 - (static_cast<double>(stats.compressedSizeBytes) / static_cast<double>(stats.rawSizeBytes))) * 100.0;
        }

        LOG_INFO(BinarySerializer, "Successfully deserialized page '" + outPage.title + "' (" + outPage.guid + ") -> " + stats.ToString());

        if (outStats) {
            *outStats = stats;
        }
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(BinarySerializer, "Exception during page binary deserialization: " + std::string(e.what()));
        return false;
    }
}

bool BinarySerializer::SerializeJournalEntry(const JournalEntry& entry, std::vector<uint8_t>& outBytes) {
    ByteWriter writer;
    writer.WriteU8(static_cast<uint8_t>(entry.opType));
    writer.WriteString(entry.targetGuid);
    writer.WriteU64(entry.timestampMs);
    writer.WriteU32(static_cast<uint32_t>(entry.payload.size()));
    writer.WriteBytes(entry.payload.data(), entry.payload.size());

    outBytes = std::move(writer.buffer);
    return true;
}

bool BinarySerializer::DeserializeJournalEntry(const uint8_t* data, size_t size, JournalEntry& outEntry) {
    if (!data || size < 14) return false;
    ByteReader reader(data, size);
    outEntry.opType = static_cast<JournalOpType>(reader.ReadU8());
    outEntry.targetGuid = reader.ReadString();
    outEntry.timestampMs = reader.ReadU64();
    uint32_t payloadSize = reader.ReadU32();
    outEntry.payload = reader.ReadBytes(payloadSize);
    return true;
}

bool BinarySerializer::ApplyJournalEntry(CanvasPage& page, const JournalEntry& entry) {
    page.Touch();
    page.isModified = true;

    switch (entry.opType) {
        case JournalOpType::AddObject: {
            if (entry.payload.empty()) return false;
            ByteReader reader(entry.payload.data(), entry.payload.size());
            SerializationStats stats;
            auto obj = DeserializeObject(reader, stats);
            if (obj) {
                page.AddObject(obj);
                return true;
            }
            return false;
        }

        case JournalOpType::RemoveObject: {
            for (const auto& obj : page.objects) {
                if (obj && obj->guuid == entry.targetGuid) {
                    page.RemoveObject(obj);
                    return true;
                }
            }
            return false;
        }

        case JournalOpType::TransformObject: {
            if (entry.payload.size() < 48 + 32) return false;
            ByteReader reader(entry.payload.data(), entry.payload.size());
            BLMatrix2D transform;
            transform.m00 = reader.ReadDouble();
            transform.m01 = reader.ReadDouble();
            transform.m10 = reader.ReadDouble();
            transform.m11 = reader.ReadDouble();
            transform.m20 = reader.ReadDouble();
            transform.m21 = reader.ReadDouble();

            AABB bounds;
            bounds.minX = reader.ReadDouble();
            bounds.minY = reader.ReadDouble();
            bounds.maxX = reader.ReadDouble();
            bounds.maxY = reader.ReadDouble();

            for (const auto& obj : page.objects) {
                if (obj && obj->guuid == entry.targetGuid) {
                    obj->transform = transform;
                    obj->bounds = bounds;
                    page.UpdateObject(obj);
                    return true;
                }
            }
            return false;
        }

        case JournalOpType::ModifyStyle: {
            if (entry.payload.size() < 4 + 8 + 4) return false;
            ByteReader reader(entry.payload.data(), entry.payload.size());
            uint32_t colorVal = reader.ReadU32();
            double baseWidth = reader.ReadDouble();
            float opacity = reader.ReadFloat();

            for (const auto& obj : page.objects) {
                if (obj && obj->guuid == entry.targetGuid) {
                    obj->opacity = opacity;
                    if (obj->type == ObjectType::InkContainer) {
                        auto ink = std::static_pointer_cast<InkContainer>(obj);
                        for (auto& st : ink->strokes) {
                            st.color.value = colorVal;
                            st.baseWidth = baseWidth;
                        }
                        ink->InvalidateCache();
                    }
                    page.UpdateObject(obj);
                    return true;
                }
            }
            return false;
        }

        case JournalOpType::ModifyZOrder: {
            if (entry.payload.size() < 4) return false;
            ByteReader reader(entry.payload.data(), entry.payload.size());
            int32_t zOrder = static_cast<int32_t>(reader.ReadU32());

            for (const auto& obj : page.objects) {
                if (obj && obj->guuid == entry.targetGuid) {
                    obj->zOrder = zOrder;
                    return true;
                }
            }
            return false;
        }

        case JournalOpType::ModifyText: {
            if (entry.payload.empty()) return false;
            ByteReader reader(entry.payload.data(), entry.payload.size());
            std::string updatedText = reader.ReadString();

            for (const auto& obj : page.objects) {
                if (obj && obj->guuid == entry.targetGuid && obj->type == ObjectType::Text) {
                    auto txt = std::static_pointer_cast<TextBoxObject>(obj);
                    txt->text = updatedText;
                    page.UpdateObject(txt);
                    return true;
                }
            }
            return false;
        }

        default:
            LOG_WARN(BinarySerializer, "Unhandled journal operation type: " + std::to_string(static_cast<int>(entry.opType)));
            return false;
    }
}

} // namespace Folio
