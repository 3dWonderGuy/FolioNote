#pragma once
// ANDROID: SDL_opengl.h is desktop-only. Use GLES3 header on Android.
// GL_RGBA8 and VAOs are GLES3 features (not in GLES2/gl2.h).
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <lunasvg.h>
#include "utils/file_loader.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <filesystem>

class IconManager {
public:
    struct IconTexture {
        GLuint id = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        ~IconTexture() {
            if (id != 0) {
                glDeleteTextures(1, &id);
            }
        }
    };

    std::unordered_map<std::string, std::shared_ptr<IconTexture>> cache;
    std::unordered_set<std::string> failedKeys; // Prevents re-querying missing files every frame

    GLuint LoadOrGetSVG(const std::string& key, const std::string& assetPath, uint32_t rasterSize = 64, bool asWhiteMask = false) {
        std::string fullKey = key + (asWhiteMask ? "_mask" : "");
        auto it = cache.find(fullKey);
        if (it != cache.end()) return it->second->id;
        if (failedKeys.find(fullKey) != failedKeys.end()) return 0;

        // Strip leading "assets/" if present to normalize base relative path
        std::string relPath = assetPath;
        if (relPath.rfind("assets/", 0) == 0) {
            relPath = relPath.substr(7);
        } else if (relPath.rfind("assets\\", 0) == 0) {
            relPath = relPath.substr(7);
        }

        std::vector<std::string> candidates = {
            assetPath,
            relPath,
            "assets/" + relPath,
            "../assets/" + relPath,
            "../../assets/" + relPath,
            "build/bin/assets/" + relPath,
            "bin/assets/" + relPath
        };

        std::vector<uint8_t> fileBytes;
        bool loaded = false;
        for (const auto& c : candidates) {
            // ANDROID: std::filesystem::exists() cannot see files inside the APK archive.
            // SDL_IOFromFile is hooked into the Android AssetManager by SDL3 at startup,
            // so it can transparently read APK-bundled assets. FileLoader::Exists() wraps
            // this and works correctly on both Android (APK assets) and desktop (filesystem).
            if (FileLoader::Exists(c)) {
                if (FileLoader::ReadToBuffer(c, fileBytes)) {
                    loaded = true;
                    break;
                }
            }
        }

        if (!loaded) {
            failedKeys.insert(fullKey);
            return 0;
        }

        auto doc = lunasvg::Document::loadFromData(
            reinterpret_cast<const char*>(fileBytes.data()), 
            fileBytes.size()
        );
        if (!doc) {
            failedKeys.insert(fullKey);
            return 0;
        }

        lunasvg::Bitmap bitmap = doc->renderToBitmap(rasterSize, rasterSize);
        if (!bitmap.valid()) {
            failedKeys.insert(fullKey);
            return 0;
        }

        uint32_t w = bitmap.width();
        uint32_t h = bitmap.height();
        const uint8_t* src = bitmap.data();

        std::vector<uint8_t> rgba(w * h * 4);
        for (size_t i = 0; i < (size_t)(w * h); ++i) {
            uint8_t b = src[i * 4 + 0];
            uint8_t g = src[i * 4 + 1];
            uint8_t r = src[i * 4 + 2];
            uint8_t a = src[i * 4 + 3];

            if (asWhiteMask) {
                // Monochrome alpha mask for dynamic tinting
                if (a > 0) {
                    rgba[i * 4 + 0] = 255;
                    rgba[i * 4 + 1] = 255;
                    rgba[i * 4 + 2] = 255;
                    rgba[i * 4 + 3] = a;
                } else {
                    rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = 0;
                }
            } else {
                if (a > 0) {
                    rgba[i * 4 + 0] = (uint8_t)std::min(255, (r * 255) / a);
                    rgba[i * 4 + 1] = (uint8_t)std::min(255, (g * 255) / a);
                    rgba[i * 4 + 2] = (uint8_t)std::min(255, (b * 255) / a);
                    rgba[i * 4 + 3] = a;
                } else {
                    rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = 0;
                }
            }
        }

        GLuint texId = 0;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        auto icon = std::make_shared<IconTexture>();
        icon->id = texId;
        icon->width = w;
        icon->height = h;
        cache[fullKey] = icon;

        return texId;
    }

    void Clear() {
        cache.clear();
        failedKeys.clear();
    }
};

inline IconManager g_IconManager;