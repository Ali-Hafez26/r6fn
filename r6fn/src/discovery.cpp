#include "discovery.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace ext {
namespace {

struct TArrayRaw {
    std::uintptr_t data = 0;
    std::int32_t count = 0;
    std::int32_t capacity = 0;
};

constexpr std::size_t kMaxPlayerArray = 512;
constexpr std::size_t kMaxActors = 16384;
constexpr std::size_t kMaxBoneInfos = 1024;

const char* const kBoneNames[BoneCount] = {
    "head",
    "neck_01",
    "spine_03",
    "pelvis",
    "upperarm_l",
    "upperarm_r",
    "hand_l",
    "hand_r",
};

bool starts_with_ci(const std::string& haystack, const char* needle) {
    const std::size_t n = std::strlen(needle);
    if (haystack.size() < n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool contains_ci(const std::string& haystack, const char* needle) {
    const std::string h = [&] {
        std::string out = haystack;
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }();
    const std::string n = [&] {
        std::string out = needle;
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }();
    return h.find(n) != std::string::npos;
}

} // namespace

Discovery::Discovery(Process& proc)
    : proc_(proc),
      scanner_(proc, proc.main_module().base, proc.main_module().size) {
    bone_index_.fill(-1);
}

bool Discovery::init() {
    results_.clear();
    results_.reserve(sizeof(kSigs) / sizeof(kSigs[0]));

    const std::uintptr_t module_base = proc_.main_module().base;
    const std::size_t module_size = proc_.main_module().size;

    std::printf("[module] %ls base=0x%llX size=0x%zX\n",
                proc_.main_module().name.c_str(),
                static_cast<unsigned long long>(module_base), module_size);

    if (module_base == 0 || module_size == 0) {
        std::printf("[module] FAILED: main module not resolved from the PEB\n");
        return false;
    }

    for (const SigDef& def : kSigs) {
        SigResult result{};
        result.name = def.name;
        result.placeholder = def.placeholder;
        result.resolves = def.resolves;

        const Pattern pattern = compile_pattern(def.pattern);
        if (pattern.valid) {
            const std::uintptr_t match = scanner_.find(pattern);
            if (match != 0) {
                result.match = match;
                result.found = true;

                switch (def.kind) {
                    case SigKind::RipSlot: {
                        result.slot = scanner_.rip_target(match, def.disp_offset, def.instr_len);
                        result.value = proc_.read_ptr(result.slot);
                        break;
                    }
                    case SigKind::DispOffset: {
                        std::int32_t disp = 0;
                        if (proc_.read(match + def.disp_offset, disp)) {
                            result.disp = disp;
                            result.slot = match + def.disp_offset;
                        }
                        break;
                    }
                    case SigKind::ByteImm: {
                        std::uint8_t imm = 0;
                        if (proc_.read(match + def.disp_offset, imm)) {
                            result.disp = imm;
                            result.slot = match + def.disp_offset;
                        }
                        break;
                    }
                    case SigKind::Code: {
                        result.slot = match;
                        break;
                    }
                }
            }
        }

        if (std::strcmp(def.name, "uworld") == 0) {
            uworld_slot_ = result.slot;
        } else if (std::strcmp(def.name, "gworld") == 0) {
            gworld_slot_ = result.slot;
        } else if (std::strcmp(def.name, "visibility") == 0) {
            visibility_address_ = result.slot;
        }

        results_.push_back(std::move(result));
    }

    apply_offset_bindings();
    resolve_fname_pool();

    return true;
}

void Discovery::apply_offset_bindings() {
    using Member = std::uint32_t Offsets::*;

    const std::pair<const char*, Member> bindings[] = {
        {"game_instance", &Offsets::game_instance_from_world},
        {"local_players", &Offsets::local_players_from_game_instance},
        {"player_controller", &Offsets::player_controller_from_local_player},
        {"acknowledged_pawn", &Offsets::acknowledged_pawn_from_pc},
        {"player_state_pc", &Offsets::player_state_from_pc},
        {"game_state", &Offsets::game_state_from_world},
        {"player_array", &Offsets::player_array_from_game_state},
        {"level", &Offsets::level_from_world},
        {"actors", &Offsets::actors_from_level},
        {"actors_stride", &Offsets::actors_stride},
        {"class_name", &Offsets::class_name_from_object},
        {"root_component", &Offsets::root_component_from_actor},
        {"relative_location", &Offsets::relative_location_from_component},
        {"mesh", &Offsets::mesh_from_pawn},
        {"pawn_from_player_state", &Offsets::pawn_from_player_state},
        {"team", &Offsets::team_from_player_state},
        {"player_name", &Offsets::name_from_player_state},
        {"health", &Offsets::health_from_pawn},
        {"max_health", &Offsets::max_health_from_pawn},
        {"dead_flag", &Offsets::dead_flag_from_pawn},
        {"recently_rendered", &Offsets::recently_rendered_from_pawn},
        {"bone_array", &Offsets::bone_array_from_mesh},
        {"component_to_world", &Offsets::component_to_world_from_mesh},
        {"bone_stride", &Offsets::bone_stride},
        {"skeletal_asset", &Offsets::skeletal_asset_from_mesh},
        {"ref_skeleton", &Offsets::ref_skeleton_from_asset},
        {"bone_info_array", &Offsets::bone_info_array_from_skeleton},
        {"bone_info_stride", &Offsets::bone_info_stride},
        {"camera_manager", &Offsets::player_camera_manager_from_pc},
        {"camera_cache", &Offsets::camera_cache_from_manager},
        {"camera_pov", &Offsets::camera_pov_from_cache},
        {"current_weapon", &Offsets::current_weapon_from_pawn},
        {"recoil", &Offsets::recoil_multiplier_from_weapon},
    };

    for (const auto& [name, member] : bindings) {
        for (SigResult& result : results_) {
            if (result.name != name) {
                continue;
            }
            if (result.found) {
                offsets_.*member = static_cast<std::uint32_t>(result.disp);
                result.derived_offset = true;
            }
            break;
        }
    }
}

void Discovery::resolve_fname_pool() {
    fname_pool_ = 0;
    for (const SigResult& result : results_) {
        if (result.name != "fname_pool" || !result.found) {
            continue;
        }
        fname_pool_ = result.value != 0 ? result.value : result.slot;
        break;
    }
}

const SigResult* Discovery::result_named(const char* name) const {
    for (const SigResult& result : results_) {
        if (result.name == name) {
            return &result;
        }
    }
    return nullptr;
}

std::uintptr_t Discovery::read_uworld() const {
    if (uworld_slot_ != 0) {
        const std::uintptr_t value = proc_.read_ptr(uworld_slot_);
        if (value != 0) {
            return value;
        }
    }
    if (gworld_slot_ != 0) {
        const std::uintptr_t value = proc_.read_ptr(gworld_slot_);
        if (value != 0) {
            return value;
        }
    }
    return 0;
}

bool Discovery::read_vector(std::uintptr_t addr, Vec3& out) const {
    if (cipher_.lwc_doubles) {
        double values[3]{};
        if (!proc_.read(addr, values)) {
            return false;
        }
        out = Vec3{static_cast<float>(values[0]), static_cast<float>(values[1]),
                   static_cast<float>(values[2])};
        return true;
    }

    float values[3]{};
    if (!proc_.read(addr, values)) {
        return false;
    }
    out = Vec3{values[0], values[1], values[2]};
    return true;
}

bool Discovery::read_rotator(std::uintptr_t addr, Rotator& out) const {
    float values[3]{};
    if (!proc_.read(addr, values)) {
        return false;
    }
    out = Rotator{values[0], values[1], values[2]};
    return true;
}

bool Discovery::read_camera(std::uintptr_t controller, CameraPOV& out) const {
    out = CameraPOV{};
    if (controller == 0) {
        return false;
    }

    const std::uintptr_t manager = proc_.read_ptr(controller + offsets_.player_camera_manager_from_pc);
    if (manager == 0) {
        return false;
    }

    const std::uintptr_t pov = manager + offsets_.camera_cache_from_manager + offsets_.camera_pov_from_cache;

    // FMinimalViewInfo: Location, Rotation, FOV. Field offsets inside the POV are
    // build-dependent; the vector width shift handles UE4 float vs UE5 double.
    constexpr std::uint32_t kRotationDelta = 0x0C;
    constexpr std::uint32_t kFovDelta = 0x18;
    const std::uint32_t width_shift = cipher_.lwc_doubles ? 12u : 0u;

    Vec3 location{};
    if (!read_vector(pov, location) || !read_rotator(pov + kRotationDelta + width_shift, out.rotation)) {
        return false;
    }

    out.location = location;
    out.fov = proc_.read_or<float>(pov + kFovDelta + width_shift, 90.0f);
    if (!(out.fov > 1.0f) || !(out.fov < 180.0f)) {
        out.fov = 90.0f;
    }

    out.valid = std::isfinite(out.location.x) && std::isfinite(out.location.y) &&
                std::isfinite(out.location.z) && std::isfinite(out.rotation.pitch) &&
                std::isfinite(out.rotation.yaw);
    return out.valid;
}

float Discovery::read_health(std::uintptr_t pawn) const {
    std::uint32_t raw = 0;
    if (!proc_.read(pawn + offsets_.health_from_pawn, raw)) {
        return 0.0f;
    }
    if (cipher_.health_encrypted) {
        return cipher_.decrypt_health != nullptr ? cipher_.decrypt_health(raw) : 0.0f;
    }
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return std::isfinite(value) ? value : 0.0f;
}

float Discovery::read_max_health(std::uintptr_t pawn) const {
    std::uint32_t raw = 0;
    if (!proc_.read(pawn + offsets_.max_health_from_pawn, raw)) {
        return 100.0f;
    }
    if (cipher_.health_encrypted) {
        return cipher_.decrypt_health != nullptr ? cipher_.decrypt_health(raw) : 100.0f;
    }
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return (std::isfinite(value) && value > 0.0f) ? value : 100.0f;
}

bool Discovery::read_dead(std::uintptr_t pawn) const {
    const std::uint8_t flag = proc_.read_or<std::uint8_t>(pawn + offsets_.dead_flag_from_pawn, 0);
    return flag != 0;
}

bool Discovery::read_recently_rendered(std::uintptr_t pawn) const {
    const std::uint8_t flag =
        proc_.read_or<std::uint8_t>(pawn + offsets_.recently_rendered_from_pawn, 0);
    return flag != 0;
}

std::string Discovery::read_fstring(std::uintptr_t addr) const {
    struct FStringRaw {
        std::uintptr_t data;
        std::int32_t count;
        std::int32_t capacity;
    };

    FStringRaw raw{};
    if (!proc_.read(addr, raw) || raw.data == 0 || raw.count <= 0 || raw.count > 128) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(raw.count), L'\0');
    const std::size_t bytes = wide.size() * sizeof(wchar_t);
    if (proc_.read_into(raw.data, wide.data(), bytes) != bytes) {
        return {};
    }

    std::string out;
    out.reserve(wide.size());
    for (const wchar_t c : wide) {
        out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
    }
    return out;
}

std::string Discovery::fname_to_string(std::uint32_t comparison_index) const {
    if (fname_pool_ == 0) {
        return {};
    }

    const std::uint32_t block = comparison_index >> 16;
    const std::uint32_t offset = comparison_index & 0xFFFF;
    if (block >= 8192) {
        return {};
    }

    std::uintptr_t block_ptr = 0;
    if (!proc_.read(fname_pool_ + 0x10 + static_cast<std::uintptr_t>(block) * sizeof(std::uintptr_t),
                    block_ptr) ||
        block_ptr == 0) {
        return {};
    }

    const std::uintptr_t entry = block_ptr + offset;
    std::uint16_t header = 0;
    if (!proc_.read(entry, header)) {
        return {};
    }

    const bool is_wide = (header & 1u) != 0;
    const int length = header >> 1;
    if (length <= 0 || length > 1024) {
        return {};
    }

    std::string out;
    if (is_wide) {
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        const std::size_t bytes = wide.size() * sizeof(wchar_t);
        if (proc_.read_into(entry + 2, wide.data(), bytes) != bytes) {
            return {};
        }
        out.reserve(wide.size());
        for (const wchar_t c : wide) {
            out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
        }
    } else {
        out.resize(static_cast<std::size_t>(length));
        if (proc_.read_into(entry + 2, out.data(), static_cast<std::size_t>(length)) !=
            static_cast<std::size_t>(length)) {
            return {};
        }
    }
    return out;
}

void Discovery::resolve_bone_indices(std::uintptr_t mesh) {
    if (bones_resolved_ || mesh == 0 || fname_pool_ == 0) {
        return;
    }

    const std::uintptr_t asset = proc_.read_ptr(mesh + offsets_.skeletal_asset_from_mesh);
    if (asset == 0) {
        return;
    }

    const std::uintptr_t skeleton = proc_.read_ptr(asset + offsets_.ref_skeleton_from_asset);
    if (skeleton == 0) {
        return;
    }

    TArrayRaw infos{};
    if (!proc_.read(skeleton + offsets_.bone_info_array_from_skeleton, infos) || infos.data == 0 ||
        infos.count <= 0) {
        return;
    }

    const int limit = std::min<int>(infos.count, static_cast<int>(kMaxBoneInfos));
    for (int index = 0; index < limit; ++index) {
        std::uint32_t comparison_index = 0;
        const std::uintptr_t entry =
            infos.data + static_cast<std::uintptr_t>(index) * offsets_.bone_info_stride;
        if (!proc_.read(entry, comparison_index)) {
            continue;
        }

        const std::string name = fname_to_string(comparison_index);
        if (name.empty()) {
            continue;
        }

        for (int bone = 0; bone < BoneCount; ++bone) {
            if (bone_index_[bone] >= 0) {
                continue;
            }
            if (starts_with_ci(name, kBoneNames[bone])) {
                bone_index_[bone] = index;
            }
        }

        bool done = true;
        for (int bone = 0; bone < BoneCount; ++bone) {
            if (bone_index_[bone] < 0) {
                done = false;
                break;
            }
        }
        if (done) {
            break;
        }
    }

    bones_resolved_ = bone_index_[BoneHead] >= 0;
}

bool Discovery::read_bones(std::uintptr_t mesh, PlayerEntity& entity) const {
    TArrayRaw bones{};
    if (!proc_.read(mesh + offsets_.bone_array_from_mesh, bones) || bones.data == 0 ||
        bones.count <= 0) {
        return false;
    }

    Transform component_to_world{};
    const std::uintptr_t c2w_addr = mesh + offsets_.component_to_world_from_mesh;
    const bool have_c2w = [&] {
        Quat rotation{};
        Vec3 translation{};
        Vec3 scale{};
        if (!proc_.read(c2w_addr, rotation) || !read_vector(c2w_addr + 0x10, translation)) {
            return false;
        }
        read_vector(c2w_addr + 0x10 + (cipher_.lwc_doubles ? 0x18u : 0x0Cu), scale);
        component_to_world.rotation = rotation;
        component_to_world.translation = translation;
        component_to_world.scale = (scale.x == 0.0f && scale.y == 0.0f && scale.z == 0.0f)
                                       ? Vec3{1.0f, 1.0f, 1.0f}
                                       : scale;
        return true;
    }();

    bool any = false;
    for (int bone = 0; bone < BoneCount; ++bone) {
        const int index = bone_index_[bone];
        if (index < 0 || index >= bones.count) {
            continue;
        }

        const std::uintptr_t transform_addr =
            bones.data + static_cast<std::uintptr_t>(index) * offsets_.bone_stride;

        Quat rotation{};
        Vec3 translation{};
        if (!proc_.read(transform_addr, rotation) || !read_vector(transform_addr + 0x10, translation)) {
            continue;
        }

        Vec3 world = translation;
        if (have_c2w) {
            world = transform_position(component_to_world, translation);
        } else {
            world = translation + entity.location;
        }

        entity.bones[bone] = world;
        entity.bone_valid[bone] = true;
        any = true;
    }

    return any;
}

void Discovery::fill_entity(PlayerEntity& entity, std::uintptr_t local_pawn) const {
    entity.root_component = proc_.read_ptr(entity.pawn + offsets_.root_component_from_actor);
    if (entity.root_component != 0) {
        read_vector(entity.root_component + offsets_.relative_location_from_component, entity.location);
    }

    if (entity.player_state == 0) {
        entity.player_state = proc_.read_ptr(entity.pawn + offsets_.player_state_from_pawn);
    }

    entity.mesh = proc_.read_ptr(entity.pawn + offsets_.mesh_from_pawn);
    entity.health = read_health(entity.pawn);
    entity.max_health = read_max_health(entity.pawn);
    entity.dead = read_dead(entity.pawn) || entity.health <= 0.0f;
    entity.recently_rendered = read_recently_rendered(entity.pawn);

    if (entity.mesh != 0) {
        const_cast<Discovery*>(this)->resolve_bone_indices(entity.mesh);
        read_bones(entity.mesh, entity);
    }

    entity.head = entity.bone_valid[BoneHead]
                      ? entity.bones[BoneHead]
                      : entity.location + Vec3{0.0f, 0.0f, 72.0f};
    entity.feet = entity.bone_valid[BonePelvis] ? entity.bones[BonePelvis] : entity.location;

    if (entity.max_health <= 0.0f) {
        entity.max_health = 100.0f;
    }

    entity.local = entity.pawn == local_pawn;
    entity.valid = entity.pawn != 0;
}

void Discovery::collect_player(std::uintptr_t player_state, WorldSnapshot& out) const {
    PlayerEntity entity{};
    entity.player_state = player_state;
    entity.pawn = proc_.read_ptr(player_state + offsets_.pawn_from_player_state);
    if (entity.pawn == 0) {
        return;
    }

    entity.name = read_fstring(player_state + offsets_.name_from_player_state);
    entity.team = proc_.read_or<int>(player_state + offsets_.team_from_player_state, 0);

    fill_entity(entity, out.local_pawn);
    out.players.push_back(std::move(entity));
}

void Discovery::collect_from_level(std::uintptr_t uworld, WorldSnapshot& out) const {
    const std::uintptr_t level = proc_.read_ptr(uworld + offsets_.level_from_world);
    if (level == 0) {
        return;
    }

    TArrayRaw actors{};
    if (!proc_.read(level + offsets_.actors_from_level, actors) || actors.data == 0 || actors.count <= 0) {
        return;
    }

    const int limit = std::min<int>(actors.count, static_cast<int>(kMaxActors));
    for (int index = 0; index < limit; ++index) {
        const std::uintptr_t actor =
            proc_.read_ptr(actors.data + static_cast<std::uintptr_t>(index) * offsets_.actors_stride);
        if (actor == 0) {
            continue;
        }

        const std::uintptr_t actor_class = proc_.read_ptr(actor + offsets_.class_name_from_object);
        if (actor_class == 0) {
            continue;
        }

        std::uint32_t class_name_index = 0;
        if (!proc_.read(actor_class + 0x18 /* UObject::NamePrivate, live-RE placeholder */,
                        class_name_index)) {
            continue;
        }

        const std::string class_name = fname_to_string(class_name_index);
        if (class_name.empty() ||
            (!contains_ci(class_name, "PlayerPawn") && !contains_ci(class_name, "FortPawn"))) {
            continue;
        }

        PlayerEntity entity{};
        entity.pawn = actor;
        entity.player_state = proc_.read_ptr(actor + offsets_.player_state_from_pawn);
        if (entity.player_state != 0) {
            entity.name = read_fstring(entity.player_state + offsets_.name_from_player_state);
            entity.team = proc_.read_or<int>(entity.player_state + offsets_.team_from_player_state, 0);
        }

        fill_entity(entity, out.local_pawn);
        out.players.push_back(std::move(entity));
    }
}

bool Discovery::refresh(WorldSnapshot& out) {
    out = WorldSnapshot{};
    out.tick = ++tick_;

    const std::uintptr_t uworld = read_uworld();
    out.uworld = uworld;
    if (uworld == 0) {
        return false;
    }

    const std::uintptr_t game_instance = proc_.read_ptr(uworld + offsets_.game_instance_from_world);
    if (game_instance != 0) {
        const std::uintptr_t local_players =
            proc_.read_ptr(game_instance + offsets_.local_players_from_game_instance);
        if (local_players != 0) {
            const std::uintptr_t local_player = proc_.read_ptr(local_players);
            if (local_player != 0) {
                const std::uintptr_t controller =
                    proc_.read_ptr(local_player + offsets_.player_controller_from_local_player);
                out.local_controller = controller;
                if (controller != 0) {
                    out.local_pawn = proc_.read_ptr(controller + offsets_.acknowledged_pawn_from_pc);
                    out.local_player_state = proc_.read_ptr(controller + offsets_.player_state_from_pc);
                }
            }
        }
    }

    if (out.local_player_state != 0) {
        out.local_team =
            proc_.read_or<int>(out.local_player_state + offsets_.team_from_player_state, 0);
    }

    read_camera(out.local_controller, out.camera);

    const std::uintptr_t game_state = proc_.read_ptr(uworld + offsets_.game_state_from_world);
    if (game_state != 0) {
        TArrayRaw array{};
        if (proc_.read(game_state + offsets_.player_array_from_game_state, array) && array.data != 0 &&
            array.count > 0) {
            const int limit = std::min<int>(array.count, static_cast<int>(kMaxPlayerArray));
            out.players.reserve(static_cast<std::size_t>(limit));
            for (int index = 0; index < limit; ++index) {
                const std::uintptr_t player_state = proc_.read_ptr(array.data + index * 8ull);
                if (player_state != 0) {
                    collect_player(player_state, out);
                }
            }
        }
    }

    if (out.players.empty()) {
        collect_from_level(uworld, out);
    }

    for (PlayerEntity& entity : out.players) {
        entity.distance = (entity.head - out.camera.location).length() * 0.01f;
    }

    return true;
}

void Discovery::print_dump() const {
    std::printf("\n=== signatures ===\n");
    for (const SigResult& result : results_) {
        std::printf("[sig] %-22s %-4s", result.name.c_str(), result.found ? "OK" : "MISS");
        if (result.found) {
            std::printf(" match=0x%llX slot=0x%llX",
                        static_cast<unsigned long long>(result.match),
                        static_cast<unsigned long long>(result.slot));
            if (result.value != 0) {
                std::printf(" value=0x%llX", static_cast<unsigned long long>(result.value));
            }
            if (result.derived_offset) {
                std::printf(" offset=0x%X", static_cast<unsigned>(result.disp));
            }
        } else {
            std::printf(" (placeholder - re-derive from a live dump)");
        }
        std::printf("\n        %s\n", result.resolves);
    }

    std::printf("\n=== offsets ===\n");
    struct OffsetRow {
        const char* name;
        std::uint32_t value;
        const char* sig;
    };

    const OffsetRow rows[] = {
        {"game_instance_from_world", offsets_.game_instance_from_world, "game_instance"},
        {"local_players_from_game_instance", offsets_.local_players_from_game_instance, "local_players"},
        {"player_controller_from_local_player", offsets_.player_controller_from_local_player, "player_controller"},
        {"acknowledged_pawn_from_pc", offsets_.acknowledged_pawn_from_pc, "acknowledged_pawn"},
        {"player_state_from_pc", offsets_.player_state_from_pc, "player_state_pc"},
        {"game_state_from_world", offsets_.game_state_from_world, "game_state"},
        {"player_array_from_game_state", offsets_.player_array_from_game_state, "player_array"},
        {"level_from_world", offsets_.level_from_world, "level"},
        {"actors_from_level", offsets_.actors_from_level, "actors"},
        {"actors_stride", offsets_.actors_stride, "actors_stride"},
        {"class_name_from_object", offsets_.class_name_from_object, "class_name"},
        {"root_component_from_actor", offsets_.root_component_from_actor, "root_component"},
        {"relative_location_from_component", offsets_.relative_location_from_component, "relative_location"},
        {"mesh_from_pawn", offsets_.mesh_from_pawn, "mesh"},
        {"pawn_from_player_state", offsets_.pawn_from_player_state, "pawn_from_player_state"},
        {"team_from_player_state", offsets_.team_from_player_state, "team"},
        {"name_from_player_state", offsets_.name_from_player_state, "player_name"},
        {"health_from_pawn", offsets_.health_from_pawn, "health"},
        {"max_health_from_pawn", offsets_.max_health_from_pawn, "max_health"},
        {"dead_flag_from_pawn", offsets_.dead_flag_from_pawn, "dead_flag"},
        {"recently_rendered_from_pawn", offsets_.recently_rendered_from_pawn, "recently_rendered"},
        {"bone_array_from_mesh", offsets_.bone_array_from_mesh, "bone_array"},
        {"component_to_world_from_mesh", offsets_.component_to_world_from_mesh, "component_to_world"},
        {"bone_stride", offsets_.bone_stride, "bone_stride"},
        {"skeletal_asset_from_mesh", offsets_.skeletal_asset_from_mesh, "skeletal_asset"},
        {"ref_skeleton_from_asset", offsets_.ref_skeleton_from_asset, "ref_skeleton"},
        {"bone_info_array_from_skeleton", offsets_.bone_info_array_from_skeleton, "bone_info_array"},
        {"bone_info_stride", offsets_.bone_info_stride, "bone_info_stride"},
        {"player_camera_manager_from_pc", offsets_.player_camera_manager_from_pc, "camera_manager"},
        {"camera_cache_from_manager", offsets_.camera_cache_from_manager, "camera_cache"},
        {"camera_pov_from_cache", offsets_.camera_pov_from_cache, "camera_pov"},
        {"current_weapon_from_pawn", offsets_.current_weapon_from_pawn, "current_weapon"},
        {"recoil_multiplier_from_weapon", offsets_.recoil_multiplier_from_weapon, "recoil"},
    };

    for (const OffsetRow& row : rows) {
        const SigResult* sig = result_named(row.sig);
        const bool verified = sig != nullptr && sig->derived_offset;
        std::printf("[offset] %-36s = 0x%-6X %s\n", row.name, static_cast<unsigned>(row.value),
                    verified ? "sig-derived OK" : "PLACEHOLDER (sig not resolved)");
    }

    std::printf("\n=== live chain ===\n");
    WorldSnapshot snapshot{};
    const bool ok = const_cast<Discovery*>(this)->refresh(snapshot);
    std::printf("[chain] refresh                 %s\n", ok ? "OK" : "FAILED");

    if (ok) {
        std::printf("[chain] uworld                  0x%llX\n",
                    static_cast<unsigned long long>(snapshot.uworld));
        std::printf("[chain] local_controller        0x%llX\n",
                    static_cast<unsigned long long>(snapshot.local_controller));
        std::printf("[chain] local_pawn              0x%llX\n",
                    static_cast<unsigned long long>(snapshot.local_pawn));
        std::printf("[chain] camera                  valid=%d loc=(%.1f %.1f %.1f) rot=(%.2f %.2f) fov=%.1f\n",
                    snapshot.camera.valid ? 1 : 0, snapshot.camera.location.x,
                    snapshot.camera.location.y, snapshot.camera.location.z,
                    snapshot.camera.rotation.pitch, snapshot.camera.rotation.yaw,
                    snapshot.camera.fov);
        std::printf("[chain] fname_pool              0x%llX\n",
                    static_cast<unsigned long long>(fname_pool_));
        std::printf("[chain] bone_indices            head=%d neck=%d chest=%d pelvis=%d hands=%d/%d\n",
                    bone_index_[BoneHead], bone_index_[BoneNeck], bone_index_[BoneChest],
                    bone_index_[BonePelvis], bone_index_[BoneHandL], bone_index_[BoneHandR]);
        std::printf("[chain] players                 %zu\n", snapshot.players.size());
        for (const PlayerEntity& entity : snapshot.players) {
            std::printf("        pawn=0x%llX name=%-18s team=%d hp=%.0f/%.0f dist=%.0fm dead=%d local=%d "
                        "bones(h/n/p)=%d/%d/%d\n",
                        static_cast<unsigned long long>(entity.pawn), entity.name.c_str(), entity.team,
                        entity.health, entity.max_health, entity.distance, entity.dead ? 1 : 0,
                        entity.local ? 1 : 0, entity.bone_valid[BoneHead] ? 1 : 0,
                        entity.bone_valid[BoneNeck] ? 1 : 0, entity.bone_valid[BonePelvis] ? 1 : 0);
        }
    }

    std::printf("\n[dump] placeholders are placeholders. Re-derive every MISS before trusting a frame.\n");
}

} // namespace ext
