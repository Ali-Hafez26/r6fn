#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <random>

#include "discovery.hpp"
#include "overlay.hpp"
#include "process.hpp"

namespace ext {

struct Viewport {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct Toggles {
    bool esp = true;
    bool aim = false;
    bool norecoil = false;
};

struct AimSettings {
    float fov_px = 140.0f;
    float smooth = 6.0f;
    float max_distance = 400.0f;
    bool humanize = true;
    float jitter_px = 0.4f;
    int target_bone = BoneHead;
    bool require_visible = true;
};

struct EspSettings {
    bool boxes = true;
    bool names = true;
    bool health = true;
    bool distance = true;
    bool team_filter = true;
    bool show_teammates = false;
    bool fov_circle = true;
};

bool world_to_screen(const CameraPOV& camera, const Viewport& viewport, const Vec3& world,
                     Vec2& out);

class Features {
public:
    Features(Process& proc, Discovery& discovery);

    void set_viewport(const Viewport& viewport) { viewport_ = viewport; }
    void set_toggles(const Toggles& toggles) { toggles_ = toggles; }
    void set_target_window(HWND window) { target_window_ = window; }

    void tick(std::chrono::steady_clock::time_point now);
    DrawFrame build_frame() const;

    const WorldSnapshot& snapshot() const noexcept { return snapshot_; }
    const Vec2& last_aim_error() const noexcept { return last_error_; }

    AimSettings aim;
    EspSettings esp;

private:
    void update_norecoil();
    void run_aim(std::chrono::steady_clock::time_point now);
    void restore_recoil();

    bool pick_target(Vec3& target, float& distance) const;
    bool aim_delta(const Vec3& target, Vec2& pixels) const;

    Process& proc_;
    Discovery& discovery_;
    WorldSnapshot snapshot_{};
    Viewport viewport_{};
    Toggles toggles_{};
    HWND target_window_ = nullptr;

    bool recoil_active_ = false;
    std::uintptr_t recoil_address_ = 0;
    float recoil_saved_ = 0.0f;

    std::mt19937 rng_{std::random_device{}()};
    std::chrono::steady_clock::time_point next_aim_time_{};
    Vec2 last_error_{};
};

} // namespace ext
