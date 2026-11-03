#include "features.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace ext {
namespace {

void send_mouse_relative(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

} // namespace

bool world_to_screen(const CameraPOV& camera, const Viewport& viewport, const Vec3& world,
                     Vec2& out) {
    if (!camera.valid || viewport.w <= 0 || viewport.h <= 0 || camera.fov <= 1.0f) {
        return false;
    }

    const float pitch = camera.rotation.pitch * kDeg2Rad;
    const float yaw = camera.rotation.yaw * kDeg2Rad;

    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);

    // UE rotator basis. Roll is ignored for projection; for a first-person camera
    // roll is zero and for a spectator camera it only rotates the image.
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{-sy, cy, 0.0f};
    const Vec3 up = forward.cross(right);

    const Vec3 delta = world - camera.location;
    const float camera_x = delta.dot(right);
    const float camera_y = delta.dot(forward);
    const float camera_z = delta.dot(up);

    if (camera_y <= 0.01f) {
        return false;
    }

    const float scale = (static_cast<float>(viewport.h) * 0.5f) / std::tan(camera.fov * 0.5f * kDeg2Rad);

    out.x = static_cast<float>(viewport.w) * 0.5f + (camera_x / camera_y) * scale;
    out.y = static_cast<float>(viewport.h) * 0.5f - (camera_z / camera_y) * scale;
    return true;
}

Features::Features(Process& proc, Discovery& discovery)
    : proc_(proc), discovery_(discovery) {}

void Features::tick(std::chrono::steady_clock::time_point now) {
    discovery_.refresh(snapshot_);
    update_norecoil();
    if (toggles_.aim) {
        run_aim(now);
    }
}

void Features::update_norecoil() {
    if (!toggles_.norecoil) {
        restore_recoil();
        return;
    }

    if (discovery_.cipher().recoil_encrypted) {
        return; // placeholder: no decrypt, nothing is written
    }

    const std::uintptr_t pawn = snapshot_.local_pawn;
    if (pawn == 0) {
        return;
    }

    const std::uintptr_t weapon = proc_.read_ptr(pawn + discovery_.offsets().current_weapon_from_pawn);
    if (weapon == 0) {
        return;
    }

    const std::uintptr_t address = weapon + discovery_.offsets().recoil_multiplier_from_weapon;

    float current = 0.0f;
    if (!proc_.read(address, current)) {
        return;
    }

    if (!recoil_active_) {
        recoil_saved_ = current;
        recoil_address_ = address;
        recoil_active_ = true;
    }

    if (current != 0.0f) {
        const float zero = 0.0f;
        proc_.write(address, zero);
    }
}

void Features::restore_recoil() {
    if (!recoil_active_) {
        return;
    }

    if (recoil_address_ != 0 && !discovery_.cipher().recoil_encrypted) {
        proc_.write(recoil_address_, recoil_saved_);
    }

    recoil_active_ = false;
    recoil_address_ = 0;
    recoil_saved_ = 0.0f;
}

bool Features::aim_delta(const Vec3& target, Vec2& pixels) const {
    const CameraPOV& camera = snapshot_.camera;
    if (!camera.valid) {
        return false;
    }

    const Vec3 delta = target - camera.location;
    const float horizontal = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (horizontal < 1.0f) {
        return false;
    }

    const float target_yaw = std::atan2(delta.y, delta.x) * kRad2Deg;
    const float target_pitch = std::atan2(delta.z, horizontal) * kRad2Deg;

    const float yaw_error = normalize_angle(target_yaw - camera.rotation.yaw);
    const float pitch_error = normalize_angle(target_pitch - camera.rotation.pitch);

    const float scale =
        (static_cast<float>(viewport_.h) * 0.5f) / std::tan(camera.fov * 0.5f * kDeg2Rad);

    pixels.x = std::tan(yaw_error * kDeg2Rad) * scale;
    pixels.y = -std::tan(pitch_error * kDeg2Rad) * scale;
    return true;
}

bool Features::pick_target(Vec3& target, float& distance) const {
    if (!snapshot_.camera.valid || viewport_.w <= 0 || viewport_.h <= 0) {
        return false;
    }

    const float center_x = static_cast<float>(viewport_.w) * 0.5f;
    const float center_y = static_cast<float>(viewport_.h) * 0.5f;
    const float radius = aim.fov_px;

    float best = radius * radius;
    bool found = false;

    for (const PlayerEntity& entity : snapshot_.players) {
        if (!entity.valid || entity.local || entity.dead) {
            continue;
        }
        if (esp_.team_filter && !esp_.show_teammates && entity.team != 0 &&
            entity.team == snapshot_.local_team) {
            continue;
        }
        if (entity.distance > aim.max_distance) {
            continue;
        }
        if (aim.require_visible && !entity.recently_rendered) {
            continue;
        }

        const int bone = (aim.target_bone >= 0 && aim.target_bone < BoneCount) ? aim.target_bone
                                                                              : BoneHead;
        Vec3 point = entity.bone_valid[bone] ? entity.bones[bone] : entity.head;
        if (!entity.bone_valid[bone]) {
            point = entity.head;
        }

        Vec2 screen{};
        if (!world_to_screen(snapshot_.camera, viewport_, point, screen)) {
            continue;
        }

        const float dx = screen.x - center_x;
        const float dy = screen.y - center_y;
        const float squared = dx * dx + dy * dy;
        if (squared > best) {
            continue;
        }

        best = squared;
        target = point;
        distance = entity.distance;
        found = true;
    }

    return found;
}

void Features::run_aim(std::chrono::steady_clock::time_point now) {
    if (now < next_aim_time_) {
        return;
    }

    if (target_window_ != nullptr && GetForegroundWindow() != target_window_) {
        last_error_ = Vec2{};
        return;
    }

    Vec3 target{};
    float distance = 0.0f;
    if (!pick_target(target, distance)) {
        last_error_ = Vec2{};
        return;
    }

    Vec2 pixels{};
    if (!aim_delta(target, pixels)) {
        last_error_ = Vec2{};
        return;
    }

    last_error_ = pixels;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_real_distribution<float> jitter(-aim.jitter_px, aim.jitter_px);

    if (aim.humanize) {
        // Micro-pause: a human does not deliver a correction on every single tick.
        if (chance(rng_) < 0.08f) {
            return;
        }
    }

    const float smooth = std::max(1.0f, aim.smooth);
    float step_x = pixels.x / smooth;
    float step_y = pixels.y / smooth;

    const float magnitude = std::sqrt(step_x * step_x + step_y * step_y);
    if (magnitude < 0.75f && (std::abs(pixels.x) > 1.0f || std::abs(pixels.y) > 1.0f)) {
        const float scale = 0.75f / std::max(magnitude, 0.001f);
        step_x *= scale;
        step_y *= scale;
    }

    if (aim.humanize) {
        step_x += jitter(rng_);
        step_y += jitter(rng_);
    }

    const int dx = static_cast<int>(std::lround(step_x));
    const int dy = static_cast<int>(std::lround(step_y));
    send_mouse_relative(dx, dy);

    if (aim.humanize) {
        std::uniform_int_distribution<int> hold(1, 5);
        next_aim_time_ = now + std::chrono::milliseconds(hold(rng_));
    }
}

DrawFrame Features::build_frame() const {
    DrawFrame frame{};

    if (!toggles_.esp) {
        return frame;
    }

    if (esp_.fov_circle && toggles_.aim) {
        frame.draw_fov = true;
        frame.center_x = static_cast<float>(viewport_.w) * 0.5f;
        frame.center_y = static_cast<float>(viewport_.h) * 0.5f;
        frame.fov_radius = aim.fov_px;
    }

    if (!snapshot_.camera.valid) {
        return frame;
    }

    const float center_x = static_cast<float>(viewport_.w) * 0.5f;
    const float center_y = static_cast<float>(viewport_.h) * 0.5f;
    const float aim_radius_sq = aim.fov_px * aim.fov_px;

    frame.items.reserve(snapshot_.players.size());

    for (const PlayerEntity& entity : snapshot_.players) {
        if (!entity.valid || entity.local || entity.dead) {
            continue;
        }
        if (esp_.team_filter && !esp_.show_teammates && entity.team != 0 &&
            entity.team == snapshot_.local_team) {
            continue;
        }

        Vec2 head_screen{};
        Vec2 feet_screen{};
        if (!world_to_screen(snapshot_.camera, viewport_, entity.head, head_screen)) {
            continue;
        }
        if (!world_to_screen(snapshot_.camera, viewport_, entity.feet, feet_screen)) {
            continue;
        }

        const float height = feet_screen.y - head_screen.y;
        if (height < 6.0f || height > 2000.0f) {
            continue;
        }

        const float width = height * 0.45f;

        DrawItem item{};
        item.x0 = head_screen.x - width * 0.5f;
        item.x1 = head_screen.x + width * 0.5f;
        item.y0 = head_screen.y;
        item.y1 = feet_screen.y;
        item.health = entity.health;
        item.max_health = entity.max_health;
        item.has_health = esp_.health;
        item.distance = entity.distance;
        item.visible = entity.recently_rendered;
        item.box = esp_.boxes;
        item.show_distance = esp_.distance;

        if (esp_.names) {
            item.name = entity.name.empty() ? "player" : entity.name;
        }

        if (toggles_.aim) {
            Vec2 aim_point{};
            const int bone = (aim.target_bone >= 0 && aim.target_bone < BoneCount)
                                 ? aim.target_bone
                                 : BoneHead;
            const Vec3 point = entity.bone_valid[bone] ? entity.bones[bone] : entity.head;
            if (world_to_screen(snapshot_.camera, viewport_, point, aim_point)) {
                const float dx = aim_point.x - center_x;
                const float dy = aim_point.y - center_y;
                item.targeted = (dx * dx + dy * dy) <= aim_radius_sq;
            }
        }

        frame.items.push_back(std::move(item));
    }

    return frame;
}

} // namespace ext
