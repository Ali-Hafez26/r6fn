#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "discovery.hpp"
#include "features.hpp"
#include "overlay.hpp"
#include "process.hpp"

namespace {

constexpr wchar_t kDefaultProcess[] = L"FortniteClient-Win64-Shipping.exe";

constexpr int kKeyEsp = VK_INSERT;
constexpr int kKeyAim = VK_DELETE;
constexpr int kKeyRecoil = VK_END;
constexpr int kKeyExit = VK_HOME;

struct Options {
    bool dump = false;
    bool overlay = true;
    std::wstring process = kDefaultProcess;
};

std::wstring widen(const char* text) {
    if (text == nullptr || *text == '\0') {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 1) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), needed);
    out.resize(static_cast<std::size_t>(needed - 1));
    return out;
}

Options parse_options(int argc, char** argv) {
    Options options{};
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--dump") == 0) {
            options.dump = true;
        } else if (std::strcmp(arg, "--no-overlay") == 0) {
            options.overlay = false;
        } else if (std::strcmp(arg, "--process") == 0 && i + 1 < argc) {
            const std::wstring name = widen(argv[++i]);
            if (!name.empty()) {
                options.process = name;
            }
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            std::printf("r6fn [--dump] [--no-overlay] [--process <exe name>]\n");
            std::exit(0);
        }
    }
    return options;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK enum_window(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != search->pid || !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return TRUE;
    }
    if ((client.right - client.left) < 320 || (client.bottom - client.top) < 240) {
        return TRUE;
    }

    search->hwnd = hwnd;
    return FALSE;
}

HWND find_game_window(DWORD pid) {
    WindowSearch search{};
    search.pid = pid;
    EnumWindows(&enum_window, reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}

class KeyEdge {
public:
    explicit KeyEdge(int virtual_key) : key_(virtual_key) {}

    bool pressed() {
        const bool now = (GetAsyncKeyState(key_) & 0x8000) != 0;
        const bool edge = now && !down_;
        down_ = now;
        return edge;
    }

private:
    int key_ = 0;
    bool down_ = false;
};

void print_banner() {
    std::printf("\n");
    std::printf("r6fn - external skeleton (sig-driven)\n");
    std::printf("------------------------------------\n");
    std::printf("keys: INSERT esp | DELETE aimbot | END no-recoil | HOME exit\n");
    std::printf("offsets marked PLACEHOLDER must be re-derived from a live dump.\n\n");
}

} // namespace

int main(int argc, char** argv) {
    SetConsoleTitleW(L"r6fn");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const Options options = parse_options(argc, argv);

    ext::Process process;
    std::printf("[attach] looking for %ls\n", options.process.c_str());
    if (!process.attach(options.process)) {
        std::printf("[attach] FAILED: process not running or access denied\n");
        return 1;
    }
    std::printf("[attach] pid=%u\n", process.pid());

    ext::Discovery discovery(process);
    if (!discovery.init()) {
        std::printf("[init] FAILED\n");
        process.detach();
        return 1;
    }

    if (options.dump) {
        discovery.print_dump();
        process.detach();
        return 0;
    }

    print_banner();

    const HWND game_window = find_game_window(process.pid());
    ext::Viewport viewport{};

    if (game_window != nullptr) {
        RECT client{};
        GetClientRect(game_window, &client);
        POINT origin{0, 0};
        ClientToScreen(game_window, &origin);
        viewport.x = origin.x;
        viewport.y = origin.y;
        viewport.w = client.right - client.left;
        viewport.h = client.bottom - client.top;
        std::printf("[window] hwnd=0x%p client=%dx%d at (%d,%d)\n", game_window, viewport.w,
                    viewport.h, viewport.x, viewport.y);
    } else {
        std::printf("[window] game window not found; using primary monitor bounds\n");
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = GetSystemMetrics(SM_CXSCREEN);
        viewport.h = GetSystemMetrics(SM_CYSCREEN);
    }

    ext::Overlay overlay;
    if (options.overlay) {
        if (!overlay.start(viewport.x, viewport.y, viewport.w, viewport.h)) {
            std::printf("[overlay] FAILED to start; running headless\n");
        }
    }

    ext::Features features(process, discovery);
    features.set_viewport(viewport);
    features.set_target_window(game_window);

    ext::Toggles toggles{};
    features.set_toggles(toggles);

    KeyEdge key_esp(kKeyEsp);
    KeyEdge key_aim(kKeyAim);
    KeyEdge key_recoil(kKeyRecoil);
    KeyEdge key_exit(kKeyExit);

    std::printf("[run] esp=%d aim=%d norecoil=%d\n", toggles.esp ? 1 : 0, toggles.aim ? 1 : 0,
                toggles.norecoil ? 1 : 0);

    bool running = true;
    std::uint64_t frame = 0;
    auto last_report = std::chrono::steady_clock::now();

    while (running) {
        if (!process.alive()) {
            std::printf("[run] target exited\n");
            break;
        }

        if (key_exit.pressed()) {
            running = false;
            break;
        }
        if (key_esp.pressed()) {
            toggles.esp = !toggles.esp;
            features.set_toggles(toggles);
            std::printf("[toggle] esp=%d\n", toggles.esp ? 1 : 0);
        }
        if (key_aim.pressed()) {
            toggles.aim = !toggles.aim;
            features.set_toggles(toggles);
            std::printf("[toggle] aim=%d\n", toggles.aim ? 1 : 0);
        }
        if (key_recoil.pressed()) {
            toggles.norecoil = !toggles.norecoil;
            features.set_toggles(toggles);
            std::printf("[toggle] norecoil=%d\n", toggles.norecoil ? 1 : 0);
        }

        features.tick(std::chrono::steady_clock::now());

        if (options.overlay && overlay.running()) {
            overlay.set_frame(features.build_frame());
        }

        if ((++frame & 0x3FF) == 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report).count();
            if (elapsed >= 5000) {
                const ext::WorldSnapshot& snapshot = features.snapshot();
                std::printf("[stat] players=%zu camera=%d pawn=0x%llX\n", snapshot.players.size(),
                            snapshot.camera.valid ? 1 : 0,
                            static_cast<unsigned long long>(snapshot.local_pawn));
                last_report = now;
            }
        }

        Sleep(1);
    }

    overlay.stop();
    process.detach();
    return 0;
}
