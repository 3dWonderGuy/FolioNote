#pragma once
/**
 * =========================================================================================
 * @file core/objects/object_registry.hpp
 * @brief Centralized Dynamic Object Factory, Reflection, and Metadata Registry for Canvas Objects
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * -------------------------------
 * ObjectRegistry establishes an Inversion-of-Control (IoC) dynamic factory and reflection
 * registry for the FolioNote canvas object hierarchy.
 *
 * 1. Open-Closed Principle (SOLID):
 *    Subsystems like DocumentSession, BinarySerializer, and ClipboardManager no longer need
 *    to maintain monolithic `switch (obj->type)` or `if/else` ladders or include every
 *    concrete container header in the application.
 *
 * 2. Self-Registration via Translation Units:
 *    Each object container (e.g., AttachmentObject, InkContainer, ImageObject, VideoObject)
 *    registers its constructor factory function inside its own `.cpp` file during static
 *    initialization. New object types or plugins can be introduced with zero edits to
 *    existing storage or clipboard translation units.
 *
 * 3. Thread-Safe Global Access:
 *    All registration, introspection, and instantiation routines are synchronized via a
 *    shared mutex to support concurrent deserialization across worker thread pools.
 */

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "core/objects/canvas_object.hpp"

namespace Folio {

/**
 * @typedef ObjectFactoryFn
 * @brief Factory function signature producing a freshly allocated, managed CanvasObject instance.
 */
using ObjectFactoryFn = std::function<std::shared_ptr<CanvasObject>()>;

/**
 * @struct ObjectTypeMetadata
 * @brief Introspection metadata descriptor registered alongside an object factory.
 */
struct ObjectTypeMetadata {
    ObjectType type = ObjectType::Other; ///< Discriminated type tag
    std::string typeName;                ///< Human-readable identifier (e.g. "AttachmentChip", "InkContainer")
    std::string defaultIcon;             ///< Unicode glyph or icon key (e.g. "📎", "✒")
    bool isSerializable = true;          ///< True if persistent storage should save/load this object
    ObjectFactoryFn factory = nullptr;   ///< Instantiation delegate
};

/**
 * @class ObjectRegistry
 * @brief Master registry managing polymorphic instantiation and reflection for CanvasObject entities.
 */
class ObjectRegistry {
public:
    /**
     * @brief Registers a custom factory function and metadata descriptor for an ObjectType.
     *
     * GENERAL WORKING PROCESS:
     * 1. Acquires unique lock over the internal registration table.
     * 2. Emplaces or overrides the ObjectTypeMetadata entry keyed by `type`.
     * 3. Emits an info diagnostic log confirming registration.
     *
     * @param[in] type Discriminated type tag.
     * @param[in] typeName Human-readable type string.
     * @param[in] factory Callable returning a new std::shared_ptr<CanvasObject>.
     * @param[in] defaultIcon Optional default UI icon glyph.
     * @param[in] isSerializable Whether this entity type participates in binary persistence.
     * @return true if registration succeeded; false if factory was null.
     */
    static bool Register(
        ObjectType type,
        const std::string& typeName,
        ObjectFactoryFn factory,
        const std::string& defaultIcon = "⬚",
        bool isSerializable = true
    );

    /**
     * @brief Type-safe template registration helper for default-constructible CanvasObject types.
     *
     * @tparam T Concrete CanvasObject derived type (e.g. AttachmentObject, ImageObject).
     * @param[in] type Discriminated type tag.
     * @param[in] typeName Human-readable type string.
     * @param[in] defaultIcon Optional default UI icon glyph.
     * @param[in] isSerializable Whether this entity type participates in binary persistence.
     * @return true if registration succeeded.
     */
    template <typename T>
    static bool Register(
        ObjectType type,
        const std::string& typeName,
        const std::string& defaultIcon = "⬚",
        bool isSerializable = true
    ) {
        return Register(type, typeName, []() -> std::shared_ptr<CanvasObject> {
            return std::make_shared<T>();
        }, defaultIcon, isSerializable);
    }

    /**
     * @brief Dynamically instantiates a default-initialized CanvasObject by its type tag.
     *
     * @param[in] type Discriminated type tag.
     * @return std::shared_ptr<CanvasObject> Freshly allocated instance, or nullptr if unregistered.
     */
    [[nodiscard]] static std::shared_ptr<CanvasObject> Create(ObjectType type);

    /**
     * @brief Checks if a factory has been registered for the specified type tag.
     *
     * @param[in] type Discriminated type tag.
     * @return true if factory exists; false otherwise.
     */
    [[nodiscard]] static bool IsRegistered(ObjectType type) noexcept;

    /**
     * @brief Retrieves the human-readable type identifier for an ObjectType.
     *
     * @param[in] type Discriminated type tag.
     * @return std::string Registered name, or "Unknown" if unregistered.
     */
    [[nodiscard]] static std::string GetTypeName(ObjectType type);

    /**
     * @brief Retrieves the registered metadata descriptor for an ObjectType.
     *
     * @param[in] type Discriminated type tag.
     * @return ObjectTypeMetadata Copy of metadata entry, or fallback empty struct if unregistered.
     */
    [[nodiscard]] static ObjectTypeMetadata GetMetadata(ObjectType type);

    /**
     * @brief Enumerates all currently registered object types in the application.
     *
     * @return std::vector<ObjectType> List of active registered type tags.
     */
    [[nodiscard]] static std::vector<ObjectType> GetRegisteredTypes();

    /**
     * @brief Unregisters an object type (used during unit testing or plugin teardown).
     *
     * @param[in] type Discriminated type tag to remove.
     */
    static void Unregister(ObjectType type);

    /**
     * @brief Clears all registered object factories (used in unit test teardowns).
     */
    static void Reset();
};

} // namespace Folio
