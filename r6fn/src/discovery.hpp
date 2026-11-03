#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "math.hpp"
#include "process.hpp"
#include "scanner.hpp"
#include "sigs.hpp"

namespace ext {

enum BoneId : int {
    BoneHead = 0,
    BoneNeck,
    BoneChest,
    BonePelvis,
    BoneUpperArmL,
    BoneUpperArmR,
    BoneHandL,
    BoneHandR,
    BoneCount,
};

struct CameraPOV {
    Vec3 location{};
    Rotator rotation{};
    float fov = 90.0f;
    bool valid = false;
};

struct PlayerEntity {
    std::uintptr_t pawn = 0;
    std::uintptr_t player_state = 0;
    std::uintptr_t mesh = 0;
    std::uintptr_t root_component = 0;

    Vec3 location{};
    Vec3 head{};
    Vec3 feet{};
    std::array<Vec3, BoneCount> bones{};
    std::array<bool, BoneCount> bone_valid{};

    float health = 0.0f;
    float max_health = 100.0f;
    float distance = 0.0f;
    int team = 0;
    std::string name;

    bool valid = false;
    bool dead = false;
    bool local = false;
    bool recently_rendered = false;
};

struct WorldSnapshot {
    std::uintptr_t uworld = 0;
    std::uintptr_t local_controller = 0;
    std::uintptr_t local_pawn = 0;
    std::uintptr_t local_player_state = 0;
    int local_team = 0;
    CameraPOV camera{};
    std::vector<PlayerEntity> players;
    std::uint64_t tick = 0;
};

// ============================================================================
//  STRUCT OFFSETS
// ----------------------------------------------------------------------------
//  Every field below is a PLACEHOLDER taken from an arbitrary older build. None
//  of them are resolved by this file; `Discovery::init()` overwrites each one
//  with the displacement read out of the matching signature when that signature
//  is found. If the sig misses, the placeholder is what the cheat runs on, and
//  `--dump` marks it as unverified.
//
//  Single-letter truth: never trust a number here. Trust the sig name next to
//  it and the OK column in `--dump`.
// ============================================================================
struct Offsets {
    // ---- UWorld chain ------------------------------------------------------
    std::uint32_t game_instance_from_world = 0x250;              // resolve by sig game_instance
    std::uint32_t local_players_from_game_instance = 0x38;       // resolve by sig local_players
    std::uint32_t player_controller_from_local_player = 0x30;    // resolve by sig player_controller
    std::uint32_t acknowledged_pawn_from_pc = 0x318;             // resolve by sig acknowledged_pawn
    std::uint32_t player_state_from_pc = 0x290;                  // resolve by sig player_state_pc

    // ---- world / player array ---------------------------------------------
    std::uint32_t game_state_from_world = 0x1D8;                 // resolve by sig game_state
    std::uint32_t player_array_from_game_state = 0x288;          // resolve by sig player_array
    std::uint32_t level_from_world = 0x40;                       // resolve by sig level
    std::uint32_t actors_from_level = 0x40;                      // resolve by sig actors
    std::uint32_t actors_stride = 0x8;                           // resolve by sig actors_stride
    std::uint32_t class_name_from_object = 0x10;                 // resolve by sig class_name

    // ---- actor / pawn ------------------------------------------------------
    std::uint32_t root_component_from_actor = 0x1B0;             // resolve by sig root_component
    std::uint32_t relative_location_from_component = 0x140;      // resolve by sig relative_location
    std::uint32_t mesh_from_pawn = 0x2F0;                        // resolve by sig mesh
    std::uint32_t pawn_from_player_state = 0x2E8;                // resolve by sig pawn_from_player_state
    std::uint32_t player_state_from_pawn = 0x290;                // resolve by sig player_state_pc
    std::uint32_t team_from_player_state = 0xF69;                // resolve by sig team
    std::uint32_t name_from_player_state = 0x390;                // resolve by sig player_name
    std::uint32_t health_from_pawn = 0x2E0;                      // resolve by sig health
    std::uint32_t max_health_from_pawn = 0x2E4;                  // resolve by sig max_health
    std::uint32_t dead_flag_from_pawn = 0x728;                   // resolve by sig dead_flag
    std::uint32_t recently_rendered_from_pawn = 0x2E0;           // resolve by sig recently_rendered

    // ---- bones -------------------------------------------------------------
    std::uint32_t bone_array_from_mesh = 0xA00;                  // resolve by sig bone_array
    std::uint32_t component_to_world_from_mesh = 0x1E0;          // resolve by sig component_to_world
    std::uint32_t bone_stride = 0x30;                            // resolve by sig bone_stride
    std::uint32_t skeletal_asset_from_mesh = 0x580;              // resolve by sig skeletal_asset
    std::uint32_t ref_skeleton_from_asset = 0x2E0;               // resolve by sig ref_skeleton
    std::uint32_t bone_info_array_from_skeleton = 0x30;          // resolve by sig bone_info_array
    std::uint32_t bone_info_stride = 0x18;                       // resolve by sig bone_info_stride

    // ---- camera ------------------------------------------------------------
    std::uint32_t player_camera_manager_from_pc = 0x328;         // resolve by sig camera_manager
    std::uint32_t camera_cache_from_manager = 0x1B30;            // resolve by sig camera_cache
    std::uint32_t camera_pov_from_cache = 0x10;                  // resolve by sig camera_pov

    // ---- weapon ------------------------------------------------------------
    std::uint32_t current_weapon_from_pawn = 0x9D0;              // resolve by sig current_weapon
    std::uint32_t recoil_multiplier_from_weapon = 0x6A8;         // resolve by sig recoil

    // ---- FName -------------------------------------------------------------
    std::uint32_t fname_entry_header = 0x0;                      // FNameEntry header (sanity, not sig-resolved)
};

// Fields that are known to be encrypted on some builds. Leave the callbacks
// null until a live dump gives you the scheme; the cheat then reports the value
// as unresolved instead of writing nonsense into the target.
struct CipherConfig {
    bool health_encrypted = false;
    float (*decrypt_health)(std::uint32_t raw) = nullptr;

    bool recoil_encrypted = false;
    float (*decrypt_recoil)(std::uint32_t raw) = nullptr;

    // UE4 stores FVector as float[3]; UE5 LWC stores FVector3d as double[3].
    // Flip after checking the dump: if RelativeLocation reads like garbage at
    // float width but sane at double width, this is true.
    bool lwc_doubles = false;
};

struct SigResult {
    std::string name;
    std::uintptr_t match = 0;
    std::uintptr_t slot = 0;
    std::uintptr_t value = 0;
    std::int32_t disp = 0;
    bool found = false;
    bool placeholder = true;
    bool derived_offset = false;
    const char* resolves = "";
};

class Discovery {
public:
    explicit Discovery(Process& proc);

    bool init();
    bool refresh(WorldSnapshot& out);

    void print_dump() const;

    const Offsets& offsets() const noexcept { return offsets_; }
    Offsets& offsets() noexcept { return offsets_; }
    const CipherConfig& cipher() const noexcept { return cipher_; }
    CipherConfig& cipher() noexcept { return cipher_; }
    const std::vector<SigResult>& sig_results() const noexcept { return results_; }
    std::uintptr_t uworld_slot() const noexcept { return uworld_slot_; }
    std::uintptr_t visibility_address() const noexcept { return visibility_address_; }

private:
    const SigResult* result_named(const char* name) const;
    void apply_offset_bindings();
    void resolve_fname_pool();
    void resolve_bone_indices(std::uintptr_t mesh);

    std::uintptr_t read_uworld() const;
    bool read_vector(std::uintptr_t addr, Vec3& out) const;
    bool read_rotator(std::uintptr_t addr, Rotator& out) const;
    bool read_camera(std::uintptr_t controller, CameraPOV& out) const;
    bool read_bones(std::uintptr_t mesh, PlayerEntity& entity) const;

    float read_health(std::uintptr_t pawn) const;
    float read_max_health(std::uintptr_t pawn) const;
    bool read_dead(std::uintptr_t pawn) const;
    bool read_recently_rendered(std::uintptr_t pawn) const;

    std::string read_fstring(std::uintptr_t addr) const;
    std::string fname_to_string(std::uint32_t comparison_index) const;

    void collect_player(std::uintptr_t player_state, WorldSnapshot& out) const;
    void collect_from_level(std::uintptr_t uworld, WorldSnapshot& out) const;
    void fill_entity(PlayerEntity& entity, std::uintptr_t local_pawn) const;

    Process& proc_;
    Scanner scanner_;
    Offsets offsets_{};
    CipherConfig cipher_{};
    std::vector<SigResult> results_{};

    std::uintptr_t uworld_slot_ = 0;
    std::uintptr_t gworld_slot_ = 0;
    std::uintptr_t fname_pool_ = 0;
    std::uintptr_t visibility_address_ = 0;

    std::array<int, BoneCount> bone_index_{};
    bool bones_resolved_ = false;
    mutable std::uint64_t tick_ = 0;
};

} // namespace ext
