#pragma once
#include <cstdint>
#include <memory>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include <string>


/**
 * @brief Enumeration of all possible object types in the canvas.
 * InkContainer - is for pen/mouse strokes
 * Text - is for text boxes
 * Image - is for images
 * Video - is for videos
 * PDF - is for pdf files
 * Table - is for tables
 * AttachmentFile is for attached files
 */
enum class ObjectType {
    InkContainer,
    Text,
    Image,
    Video,
    PDF,
    Table,
    AttachmentFile,
};

/**
 * @brief Base class for all drawable objects on the canvas. This class provides a common interface for different types of objects,
 * such as ink strokes, text boxes, and images.
 */
class CanvasObject {
public:
    std::string guuid = "";                              // to have searchable text and prevent user to user collisions and for persistent storage id (ink is not tracked)
    uint32_t uid = 0;                                   // Matches RTree UID index (deleted upon closing notebook)
    ObjectType type = ObjectType::InkContainer;         // Fast type discriminator
    AABB bounds;                                        // Cached World-space AABB
    BLMatrix2D transform = BLMatrix2D::make_identity();  // Local-to-world affine transform
    int32_t zOrder = 1;                                 // Draw order (higher = front, 0 = background)
    float opacity = 1.0f;                               // Alpha scalar (0.0 to 1.0)

    // Packed state bitfields (1 byte total)
    uint8_t isVisible    : 1 = 1;  // Soft-visibility flag (skips rendering without spatial eviction)
    uint8_t isLocked     : 1 = 0;  // Modification guard against transforms/deletion
    uint8_t isSelectable : 1 = 1;  // Interactive hit-test filter
    uint8_t isSelected   : 1 = 0;  // Selection highlight & bounding box grip trigger
    uint8_t isTemporary  : 1 = 0;  // Transient guide / preview stroke
    uint8_t reserved     : 3 = 0;  // Reserved for future use

    // forward decleration

    class Serializer;
    class Deserializer;
    
    virtual ~CanvasObject() = default;                  // Virtual destructor for proper cleanup of derived classes

    /********************************************* */
    // Bounds & Spatial
    /********************************************* */

    virtual void UpdateBounds() = 0;
    virtual bool HitTest(double worldX, double worldY) const = 0;
    virtual bool Intersects(const AABB& selectionBounds) const = 0;

    /********************************************* */
    // Geometry & Transforms
    /********************************************* */

    virtual void ApplyTransform(const BLMatrix2D& matrix) = 0;

    /********************************************* */
    // Rendering
    /********************************************* */

    virtual void Render(BLContext& ctx, const Viewport& viewport) const = 0;

    /********************************************* */
    // Duplication & Persistence
    /********************************************* */

    virtual std::unique_ptr<CanvasObject> Clone() const = 0;
    virtual void Serialize(Serializer& writer) const = 0;
    virtual void Deserialize(Deserializer& reader) = 0;
};