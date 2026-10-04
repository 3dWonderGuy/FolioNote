/**
 * =========================================================================================
 * @file core/objects/object_registry.cpp
 * @brief Implementation of Dynamic CanvasObject Factory and Introspection Registry
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS & CONCURRENCY:
 * --------------------------------------
 * ObjectRegistry maintains an internal hash map of ObjectType to ObjectTypeMetadata descriptors.
 * 1. Thread Safety:
 *    A dedicated std::mutex guards all modifications and queries to the registry map, guaranteeing
 *    safe concurrent reads during multithreaded deserialization or export passes.
 * 2. Fallback Safety:
 *    Attempting to instantiate an unregistered ObjectType yields a nullptr and logs a warning
 *    rather than throwing an unhandled exception or crashing the document loader.
 */

#include "core/objects/object_registry.hpp"
#include "utils/logger.hpp"

#include <unordered_map>
#include <mutex>

namespace Folio {

namespace {

struct RegistryStorage {
    std::mutex mutex;
    std::unordered_map<ObjectType, ObjectTypeMetadata> registry;
};

// Meyers' singleton storage ensures thread-safe, deterministic lazy initialization
RegistryStorage& GetStorage() {
    static RegistryStorage storage;
    return storage;
}

} // anonymous namespace

bool ObjectRegistry::Register(
    ObjectType type,
    const std::string& typeName,
    ObjectFactoryFn factory,
    const std::string& defaultIcon,
    bool isSerializable
) {
    if (!factory) {
        LOG_WARN(ObjectRegistry, "Registration failed: Null factory provided for type=" + typeName);
        return false;
    }

    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);

    ObjectTypeMetadata meta;
    meta.type = type;
    meta.typeName = typeName;
    meta.defaultIcon = defaultIcon;
    meta.isSerializable = isSerializable;
    meta.factory = std::move(factory);

    storage.registry[type] = std::move(meta);
    LOG_INFO(ObjectRegistry, "Registered CanvasObject type: [" + typeName + 
             "] (ID=" + std::to_string(static_cast<uint32_t>(type)) + ")");
    return true;
}

std::shared_ptr<CanvasObject> ObjectRegistry::Create(ObjectType type) {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);

    auto it = storage.registry.find(type);
    if (it != storage.registry.end() && it->second.factory) {
        auto obj = it->second.factory();
        if (obj) {
            obj->type = type; // Ensure type discriminator is synchronized
        }
        return obj;
    }

    LOG_WARN(ObjectRegistry, "Create() failed: No factory registered for ObjectType=" +
             std::to_string(static_cast<uint32_t>(type)));
    return nullptr;
}

bool ObjectRegistry::IsRegistered(ObjectType type) noexcept {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);
    return storage.registry.find(type) != storage.registry.end();
}

std::string ObjectRegistry::GetTypeName(ObjectType type) {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);

    auto it = storage.registry.find(type);
    if (it != storage.registry.end()) {
        return it->second.typeName;
    }
    return "Unknown";
}

ObjectTypeMetadata ObjectRegistry::GetMetadata(ObjectType type) {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);

    auto it = storage.registry.find(type);
    if (it != storage.registry.end()) {
        return it->second;
    }

    ObjectTypeMetadata fallback;
    fallback.type = type;
    fallback.typeName = "Unknown";
    return fallback;
}

std::vector<ObjectType> ObjectRegistry::GetRegisteredTypes() {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);

    std::vector<ObjectType> types;
    types.reserve(storage.registry.size());
    for (const auto& [type, _] : storage.registry) {
        types.push_back(type);
    }
    return types;
}

void ObjectRegistry::Unregister(ObjectType type) {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);
    storage.registry.erase(type);
}

void ObjectRegistry::Reset() {
    auto& storage = GetStorage();
    std::lock_guard<std::mutex> lock(storage.mutex);
    storage.registry.clear();
}

} // namespace Folio
