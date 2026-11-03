# r6fn

External Fortnite cheat skeleton — C++20, MSVC x64, standalone `.exe`.

Read via `ReadProcessMemory`, write via `WriteProcessMemory`. No DLL injection, no hooks in the
game, no kernel driver, no Present hook. The overlay is a separate layered GDI window.

Everything that points into the game is **signature-driven**: a byte-mask scanner finds an
instruction, the instruction's disp32 is read back as the struct offset, and the pointer chain is
rebuilt from that. No offset in this tree is authoritative — every one is a placeholder that
`--dump` reports as verified or not.

> This is an external skeleton, not a working cheat for the current patch. The signatures and
> offsets must be re-derived from a live dump of the build you are targeting. The anti-cheat
> bypass is not in this codebase and cannot be produced from a prompt.

---

## Layout

```
r6fn/
  CMakeLists.txt
  README.md
  src/
    math.hpp        Vec3/Quat/FTransform, UE rotator math, LWC-aware readers
    process.hpp/cpp attach, PEB-walked module list, Read/Write wrappers
    scanner.hpp/cpp byte-mask scanner, chunked reads, RIP-relative resolution
    sigs.hpp        every signature, its kind, and what it resolves
    discovery.hpp/cpp UWorld/Controller/Pawn/Camera/Bone resolution + snapshot
    features.hpp/cpp ESP layout, aimbot, no-recoil
    overlay.hpp/cpp GDI layered overlay (color-key, click-through, no-activate)
    main.cpp        --dump mode and the feature loop
```

## Build

Visual Studio 2022 with the Desktop C++ workload (MSVC v143 + Windows 10/11 SDK).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Artifact: `build/bin/r6fn.exe`. Static CRT, so it runs without a redistributable.

Alternatively open the folder directly in Visual Studio (`File > Open > Folder`) — the CMake
integration picks up `CMakeLists.txt` without a `.sln`.

## `--dump` workflow

This is the whole point of the skeleton. Run it against a live match:

```powershell
.\r6fn.exe --dump
```

It prints three tables:

1. **signatures** — `OK` with `match`/`slot`/`value`, or `MISS (placeholder)`.
2. **offsets** — current value and whether a signature derived it (`sig-derived OK`) or the
   placeholder is still in use (`PLACEHOLDER (sig not resolved)`).
3. **live chain** — UWorld, controller, pawn, camera POV, FName pool, bone indices, and each
   player state (name, team, health, distance, bone validity).

The scan reads the module once per signature (chunked `ReadProcessMemory`, 1 MiB windows), so a
full dump of `FortniteClient-Win64-Shipping.exe` takes tens of seconds. That is fine for a
diagnostic run; do not run it in the feature loop.

The loop:

```
run --dump  ->  read MISS rows  ->  open the module in IDA/Ghidra  ->  find the real instruction
            ->  replace the pattern in src/sigs.hpp  ->  rebuild  ->  run --dump again
```

Do that until every row you depend on is `OK`. A signature is only good if it survives the next
patch: prefer patterns anchored on control flow around the access, not on absolute call targets.

## Signature kinds

`scanner.cpp` supports three shapes, declared per entry in `sigs.hpp`:

| Kind | Meaning | Used for |
|---|---|---|
| `RipSlot` | `mov reg,[rip+disp32]` — `target = match + instr_len + disp32` | globals: UWorld, GWorld, FNamePool |
| `DispOffset` | `mov reg,[reg+disp32]` — the disp32 **is** the struct offset | every field offset |
| `ByteImm` | `imul reg,reg,imm8` — the imm8 is the element stride | bone/actor array strides |
| `Code` | raw code address, no dereference | optional raycast helper |

`DispOffset` is what makes the build sig-driven rather than offset-driven. The scanner does not
trust `0x2E8`; it finds the instruction that touches `PlayerState::PawnPrivate` and reads the
displacement out of it. When the patch moves the field, the signature still describes the same
access and the offset follows automatically. When the compiler changes the instruction shape, the
signature misses and `--dump` says so instead of silently reading the wrong field.

## Placeholder map

Every entry in `Offsets` (`discovery.hpp`) carries `// resolve by sig <name>`:

| Field | Sig |
|---|---|
| `game_instance_from_world` | `game_instance` |
| `local_players_from_game_instance` | `local_players` |
| `player_controller_from_local_player` | `player_controller` |
| `acknowledged_pawn_from_pc` | `acknowledged_pawn` |
| `player_state_from_pc` | `player_state_pc` |
| `game_state_from_world` | `game_state` |
| `player_array_from_game_state` | `player_array` |
| `root_component_from_actor` | `root_component` |
| `relative_location_from_component` | `relative_location` |
| `mesh_from_pawn` | `mesh` |
| `pawn_from_player_state` | `pawn_from_player_state` |
| `team_from_player_state` | `team` |
| `name_from_player_state` | `player_name` |
| `health_from_pawn` / `max_health_from_pawn` | `health` / `max_health` |
| `dead_flag_from_pawn` | `dead_flag` |
| `bone_array_from_mesh` | `bone_array` |
| `component_to_world_from_mesh` | `component_to_world` |
| `bone_stride` | `bone_stride` |
| `camera_manager` / `camera_cache` / `camera_pov` | `camera_manager` / `camera_cache` / `camera_pov` |
| `current_weapon_from_pawn` / `recoil_multiplier_from_weapon` | `current_weapon` / `recoil` |

Nothing in that table is real for a shipping build.

## Field notes that must be settled from a dump

- **UE4 vs UE5 LWC.** UE4 stores `FVector` as `float[3]`; UE5 LWC stores `FVector3d` as `double[3]`.
  `CipherConfig::lwc_doubles` switches the readers. Set it after looking at `RelativeLocation`.
  `FTransform` translation/scale offsets shift by 12 bytes when it is flipped.
- **Health encryption.** Some builds encrypt `FortPawn` health. `CipherConfig::health_encrypted`
  plus `decrypt_health` is the hook; until it is filled, health reads as 0 and the player is
  treated as dead — that is deliberate, not a bug to work around.
- **Bone names.** `Discovery::resolve_bone_indices` walks `USkeletalMesh::RefSkeleton`, reads each
  `FMeshBoneInfo` FName comparison index, resolves it through the FNamePool, and builds the
  name → index map. If the build encrypts bone transforms or names, `--dump` shows `-1` indices
  and ESP falls back to a fixed head offset. Do not fabricate the decrypt.
- **Camera layout.** `FCameraCacheEntry` alignment differs between builds (`TimeStamp` padding).
  `camera_pov_from_cache` is the knob; the POV field deltas (`0x0C` rotation, `0x18` FOV) are in
  `read_camera` and shift by 12 bytes under LWC.
- **Visibility.** There is no external call into the game's raycast. `help` uses the pawn's
  `bRecentlyRendered` byte as a best-effort test — it can lock through walls when the target was
  rendered recently. If a live dump gives a better field, swap it into `read_recently_rendered`.
  The `visibility` signature is resolved for the day a safe external LOS path exists; it is not
  used.
- **Aimbot input.** `SendInput` relative mouse movement only. The camera rotation is never written
  directly — that is a much louder signature. Smoothing, a 1 px floor, a probabilistic micro-pause
  and ±0.4 px jitter are in `Features::run_aim`.
- **Anti-cheat.** Not bypassed, not addressed, not hidden. EAC/BattlEye see an externally attached
  handle and a `SendInput` stream. Make of that what you will — the bypass is a separate
  architecture decision and out of scope for this tree.

## Runtime behaviour

```
INSERT  toggle ESP
DELETE  toggle aimbot
END     toggle no-recoil (restores the original value on toggle-off)
HOME    exit
```

The overlay is positioned over the game's client rect, so windowed and borderless work. Exclusive
fullscreen does not — the layered window cannot cover a display-owning swapchain.

## What is not here

No driver, no kernel patch, no IOMMU/DMA work, no injection, no hooking of `Present`, no
packet-level spoofing, no HWID/serial work. Those are separate builds with separate failure modes.
