/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_container.cpp
 * @brief Implementation of AudioObject Methods and libVLC Audio Playback Pipeline
 * =========================================================================================
 */

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/objects/media/audio/audio_container.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/text/font_manager.hpp"
#include "core/objects/object_registry.hpp"
#include "utils/logger.hpp"
#include <SDL3/SDL.h>

#if defined(FOLIO_HAS_LIBVLC)
#  if defined(_MSC_VER)
#    include <BaseTsd.h>
     typedef SSIZE_T ssize_t;
#  endif
#  include <vlc/libvlc.h>
#  include <vlc/libvlc_media.h>
#  include <vlc/libvlc_renderer_discoverer.h>
#  include <vlc/libvlc_media_player.h>
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <chrono>

namespace Folio {

// =============================================================================
// OBJECT REGISTRY SELF-REGISTRATION
// =============================================================================

namespace {
    /**
     * @brief Self-registers AudioObject with the global ObjectRegistry.
     *
     * General Working Process:
     * When the translation unit is initialized at runtime, this static constant
     * invokes ObjectRegistry::Register<AudioObject>() with factory constructor,
     * human-readable type name, and emoji icon.
     */
    [[maybe_unused]] static const bool s_registeredAudio = 
        ObjectRegistry::Register<AudioObject>(
            ObjectType::Audio,
            "AudioObject",
            "🎵",
            true
        );
}

// =============================================================================
// CONSTRUCTORS & DESTRUCTOR
// =============================================================================

AudioObject::AudioObject() {
    type = ObjectType::Audio;
    gizmoStyle = GizmoStyle::MoveOnly;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
}

AudioObject::AudioObject(const std::string& path, const std::string& name, double duration)
    : filePath(path), displayName(name), durationSeconds(duration)
{
    type = ObjectType::Audio;
    gizmoStyle = GizmoStyle::MoveOnly;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
}

AudioObject::~AudioObject() {
    ReleasePlayer();
}

AudioObject::AudioObject(const AudioObject& other)
    : CanvasObject(other),
      filePath(other.filePath),
      displayName(other.displayName),
      durationSeconds(other.durationSeconds),
      currentPositionSeconds(other.currentPositionSeconds),
      volume(other.volume)
{
    type = ObjectType::Audio;
    gizmoStyle = GizmoStyle::MoveOnly;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
}

AudioObject& AudioObject::operator=(const AudioObject& other) {
    if (this == &other) return *this;

    ReleasePlayer();
    CanvasObject::operator=(other);
    filePath = other.filePath;
    displayName = other.displayName;
    durationSeconds = other.durationSeconds;
    currentPositionSeconds = other.currentPositionSeconds;
    volume = other.volume;
    isPlaying = false;
    isPaused = false;
    worldWidth = chipW;
    worldHeight = chipH;
    UpdateBounds();
    return *this;
}

// =============================================================================
// LIBVLC AUDIO ENGINE INTEGRATION
// =============================================================================

void AudioObject::InitPlayer() {
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer || filePath.empty()) return;

    const char* vlcArgs[] = {
        "--no-video",
        "--no-video-title-show",
        "--aout=directsound",
    };
    constexpr int argCount = static_cast<int>(sizeof(vlcArgs) / sizeof(vlcArgs[0]));

    auto* inst = libvlc_new(argCount, vlcArgs);
    if (!inst) {
        LOG_ERROR(CanvasObject, "AudioObject: libvlc_new() failed to initialize audio engine");
        return;
    }
    vlcInstance = inst;

    std::filesystem::path p(filePath);
    std::string nativePath = p.make_preferred().string();

    auto* media = libvlc_media_new_path(inst, nativePath.c_str());
    if (!media) {
        LOG_ERROR(CanvasObject, "AudioObject: Failed to open audio media at: " + nativePath);
        return;
    }
    vlcMedia = media;

    auto* player = libvlc_media_player_new_from_media(media);
    if (!player) {
        LOG_ERROR(CanvasObject, "AudioObject: libvlc_media_player_new_from_media() failed");
        return;
    }
    vlcPlayer = player;
    libvlc_audio_set_volume(player, volume);
#endif
}

void AudioObject::ReleasePlayer() {
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);
        vlcPlayer = nullptr;
    }
    if (vlcMedia) {
        auto* media = static_cast<libvlc_media_t*>(vlcMedia);
        libvlc_media_release(media);
        vlcMedia = nullptr;
    }
    if (vlcInstance) {
        auto* inst = static_cast<libvlc_instance_t*>(vlcInstance);
        libvlc_release(inst);
        vlcInstance = nullptr;
    }
#endif
    isPlaying = false;
    isPaused = false;
}

// =============================================================================
// PLAYBACK CONTROLS
// =============================================================================

void AudioObject::Play() {
    InitPlayer();
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_media_player_play(player);
        isPlaying = true;
        isPaused = false;
    }
#else
    isPlaying = true;
    isPaused = false;
#endif
}

void AudioObject::Pause() {
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_media_player_pause(player);
        isPlaying = false;
        isPaused = true;
    }
#else
    isPlaying = false;
    isPaused = true;
#endif
}

void AudioObject::Stop() {
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_media_player_stop(player);
    }
#endif
    isPlaying = false;
    isPaused = false;
    currentPositionSeconds = 0.0;
}

void AudioObject::TogglePlay() {
    if (isPlaying) {
        Pause();
    } else {
        Play();
    }
}

void AudioObject::Seek(double ratio) {
    double clamped = std::clamp(ratio, 0.0, 1.0);
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_media_player_set_position(player, static_cast<float>(clamped));
    }
#endif
    if (durationSeconds > 0.0) {
        currentPositionSeconds = durationSeconds * clamped;
    }
}

void AudioObject::UpdateProgress() {
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        auto* player = static_cast<libvlc_media_player_t*>(vlcPlayer);
        libvlc_state_t st = libvlc_media_player_get_state(player);

        if (st == libvlc_Ended) {
            isPlaying = false;
            isPaused = false;
            currentPositionSeconds = 0.0;
            return;
        }

        isPlaying = (st == libvlc_Playing);
        isPaused = (st == libvlc_Paused);

        libvlc_time_t len = libvlc_media_player_get_length(player);
        if (len > 0) {
            durationSeconds = static_cast<double>(len) / 1000.0;
        }

        libvlc_time_t cur = libvlc_media_player_get_time(player);
        if (cur >= 0) {
            currentPositionSeconds = static_cast<double>(cur) / 1000.0;
        }
    }
#endif
}

void AudioObject::SetVolume(int vol) {
    volume = std::clamp(vol, 0, 100);
#if defined(FOLIO_HAS_LIBVLC)
    if (vlcPlayer) {
        libvlc_audio_set_volume(static_cast<libvlc_media_player_t*>(vlcPlayer), volume);
    }
#endif
}

void AudioObject::CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) {
    // ── Action: Play / Pause toggle ───────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = isPlaying ? "Pause Audio" : "Play Audio";
        act.icon    = isPlaying ? "⏸" : "▶";
        act.iconKey = isPlaying ? "pause" : "play";
        act.order   = 100;
        act.isSeparatorBefore = true;
        act.onTrigger = [this]() {
            TogglePlay();
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Stop & Rewind ─────────────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = "Stop & Rewind";
        act.icon    = "⏹";
        act.iconKey = "stop";
        act.order   = 110;
        act.onTrigger = [this]() {
            Stop();
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Mute / Unmute ────────────────────────────────────────────
    {
        ContextMenuItem act;
        act.label   = (volume == 0) ? "Unmute Audio" : "Mute Audio";
        act.icon    = (volume == 0) ? "🔇" : "🔊";
        act.iconKey = (volume == 0) ? "unmute" : "mute";
        act.order   = 120;
        act.onTrigger = [this]() {
            if (volume == 0) {
                SetVolume(100);
            } else {
                SetVolume(0);
            }
        };
        actions.push_back(std::move(act));
    }

    // ── Action: Open in Native Player ────────────────────────────────────
    if (!filePath.empty()) {
        ContextMenuItem act;
        act.label   = "Open in Default Player";
        act.icon    = "🎵";
        act.iconKey = "open_player";
        act.order   = 130;
        act.onTrigger = [this]() {
            SDL_OpenURL(filePath.c_str());
        };
        actions.push_back(std::move(act));
    }
}

// =============================================================================
// HIT TESTING & INTERACTION
// =============================================================================

bool AudioObject::HandleCanvasClick(double wx, double wy, std::function<void()> onDirty) {
    const double rx = wx - worldX;
    const double ry = wy - worldY;

    if (rx < 0.0 || rx > chipW || ry < 0.0 || ry > chipH) {
        return false;
    }

    // 1. Play/Pause circular button zone: Center at (10.0mm, chipH / 2) with radius 7.5mm
    const double btnDist = std::hypot(rx - 10.0, ry - (chipH * 0.5));
    if (btnDist <= 7.5) {
        TogglePlay();
        if (onDirty) onDirty();
        return true;
    }

    // 2. Scrubber bar zone: rx in [22.0, chipW - 5.0], ry in [11.0, 16.5]
    const double scrubX0 = 22.0;
    const double scrubX1 = chipW - 5.0;
    const double scrubY0 = 11.0;
    const double scrubY1 = 16.5;

    if (rx >= scrubX0 && rx <= scrubX1 && ry >= scrubY0 && ry <= scrubY1) {
        double ratio = (rx - scrubX0) / (scrubX1 - scrubX0);
        Seek(ratio);
        if (onDirty) onDirty();
        return true;
    }

    return false; // Landed on badge body (allows dragging with selection gizmo)
}

// =============================================================================
// TRANSFORM & GIZMO INVARIANTS
// =============================================================================

void AudioObject::ApplyTransform(const BLMatrix2D& matrix) {
    const double newX = (worldX * matrix.m00) + (worldY * matrix.m10) + matrix.m20;
    const double newY = (worldX * matrix.m01) + (worldY * matrix.m11) + matrix.m21;

    const double dx = newX - worldX;
    const double dy = newY - worldY;

    BLMatrix2D translationOnly = BLMatrix2D::make_translation(dx, dy);
    transform.post_transform(translationOnly);
    UpdateBounds();
}

void AudioObject::BakeTransform() {
    worldX += transform.m20;
    worldY += transform.m21;
    transform = BLMatrix2D::make_identity();
    UpdateBounds();
}

// =============================================================================
// RENDERING
// =============================================================================

static void FormatTimeSeconds(double sec, char* buf, size_t bufSize) {
    if (sec < 0.0) sec = 0.0;
    int total = static_cast<int>(sec);
    int m = total / 60;
    int s = total % 60;
    std::snprintf(buf, bufSize, "%02d:%02d", m, s);
}

void AudioObject::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible) return;

    // Refresh dynamic playback timing
    const_cast<AudioObject*>(this)->UpdateProgress();

    ctx.save();
    ctx.apply_transform(transform);

    const double x = worldX;
    const double y = worldY;
    const double w = chipW;
    const double h = chipH;
    const double r = 3.0; // corner radius in mm

    // 1. Draw sleek dark slate chip card body
    ctx.set_fill_style(BLRgba32(0x13, 0x16, 0x22, static_cast<uint8_t>(opacity * 245)));
    ctx.fill_round_rect(BLRoundRect(x, y, w, h, r, r));

    // 2. Purple brand accent border outline
    ctx.set_stroke_style(BLRgba32(0x7C, 0x3A, 0xED, static_cast<uint8_t>(opacity * 200))); // Violet-600
    const double strokeW = (viewport.zoom > 0.001) ? (0.6 / viewport.zoom) : 0.6;
    ctx.set_stroke_width(strokeW);
    ctx.stroke_round_rect(BLRoundRect(x, y, w, h, r, r));

    // 3. Play / Pause Button Circle (Left side)
    const double btnCx = x + 10.0;
    const double btnCy = y + h * 0.5;
    const double btnRadius = 6.2;

    if (isPlaying) {
        ctx.set_fill_style(BLRgba32(0x8B, 0x5C, 0xF6, 0xFF)); // Electric violet
    } else {
        ctx.set_fill_style(BLRgba32(0x6D, 0x28, 0xD9, 0xEE)); // Deep violet
    }
    ctx.fill_circle(btnCx, btnCy, btnRadius);

    // Button subtle border
    ctx.set_stroke_style(BLRgba32(0xC4, 0xB5, 0xFD, 0x88));
    ctx.set_stroke_width(0.4);
    ctx.stroke_circle(btnCx, btnCy, btnRadius);

    // Play ▶ or Pause ⏸ glyph
    ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
    if (isPlaying) {
        // Two pause bars
        const double barW = 1.3;
        const double barH = 5.2;
        ctx.fill_round_rect(BLRoundRect(btnCx - 2.1, btnCy - barH * 0.5, barW, barH, 0.3, 0.3));
        ctx.fill_round_rect(BLRoundRect(btnCx + 0.8, btnCy - barH * 0.5, barW, barH, 0.3, 0.3));
    } else {
        // Centered play triangle
        BLPath tri;
        tri.move_to(btnCx - 1.8, btnCy - 3.2);
        tri.line_to(btnCx + 3.0, btnCy);
        tri.line_to(btnCx - 1.8, btnCy + 3.2);
        tri.close();
        ctx.fill_path(tri);
    }

    // 4. Track Title & Animated Equalizer
    BLFont titleFont = FontManager::Instance().GetFont("Segoe UI", 10.0f, true);
    BLFont timeFont = FontManager::Instance().GetFont("Segoe UI", 7.5f, false);

    const double textX = x + 21.0;
    ctx.set_fill_style(BLRgba32(0xF1, 0xF5, 0xF9, 0xFF));
    std::string label = displayName.empty() ? "Audio Clip" : displayName;
    if (label.size() > 22) {
        label = label.substr(0, 19) + "...";
    }
    ctx.fill_utf8_text(BLPoint(textX, y + 6.8), titleFont, label.c_str());

    // Animated waveform bars next to title
    const double waveX0 = textX + 36.0;
    if (waveX0 + 12.0 < x + w - 4.0) {
        auto nowMs = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

        const double barW = 1.1;
        const double barGap = 1.6;
        for (int i = 0; i < 4; ++i) {
            double hScale = 0.3;
            if (isPlaying) {
                // Sinusoidal oscillating bouncing frequency bars
                double phase = (nowMs * 0.008) + (i * 1.3);
                hScale = 0.3 + 0.65 * (0.5 + 0.5 * std::sin(phase));
            }
            double barH = 5.0 * hScale;
            double barY = y + 7.0 - barH;
            ctx.set_fill_style(BLRgba32(0xA7, 0x8B, 0xFA, 0xDD));
            ctx.fill_round_rect(BLRoundRect(waveX0 + i * (barW + barGap), barY, barW, barH, 0.4, 0.4));
        }
    }

    // 5. Scrubber Track & Progress
    const double scrubX0 = x + 21.0;
    const double scrubW = w - 26.0;
    const double scrubY = y + 12.8;
    const double scrubH = 1.4;

    // Background track rail
    ctx.set_fill_style(BLRgba32(0x2A, 0x2E, 0x40, 0xFF));
    ctx.fill_round_rect(BLRoundRect(scrubX0, scrubY, scrubW, scrubH, 0.7, 0.7));

    // Progress bar fill
    double ratio = 0.0;
    if (durationSeconds > 0.0) {
        ratio = std::clamp(currentPositionSeconds / durationSeconds, 0.0, 1.0);
    }
    if (ratio > 0.001) {
        ctx.set_fill_style(BLRgba32(0x8B, 0x5C, 0xF6, 0xFF)); // Violet progress
        ctx.fill_round_rect(BLRoundRect(scrubX0, scrubY, scrubW * ratio, scrubH, 0.7, 0.7));

        // Scrub knob bead
        const double knobX = scrubX0 + (scrubW * ratio);
        ctx.set_fill_style(BLRgba32(0xDD, 0xD6, 0xFE, 0xFF));
        ctx.fill_circle(knobX, scrubY + scrubH * 0.5, 1.2);
    }

    // 6. Time Elapsed / Total Duration Label
    char timeStr[64];
    char curBuf[16];
    char durBuf[16];
    FormatTimeSeconds(currentPositionSeconds, curBuf, sizeof(curBuf));
    FormatTimeSeconds(durationSeconds, durBuf, sizeof(durBuf));
    std::snprintf(timeStr, sizeof(timeStr), "%s / %s", curBuf, durBuf);

    ctx.set_fill_style(BLRgba32(0x94, 0xA3, 0xB8, 0xFF)); // Muted slate
    ctx.fill_utf8_text(BLPoint(textX, y + 18.8), timeFont, timeStr);

    ctx.restore();
}

std::unique_ptr<CanvasObject> AudioObject::Clone() const {
    return std::make_unique<AudioObject>(*this);
}

} // namespace Folio
