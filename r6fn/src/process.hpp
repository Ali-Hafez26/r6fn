#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ext {

struct ModuleInfo {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    std::wstring name;
};

// Read/write access to one target process. Module enumeration walks the remote
// PEB (Ldr->InMemoryOrderModuleList); Toolhelp32 is only used to find the PID.
class Process {
public:
    Process() = default;
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool attach(const std::wstring& exe_name);
    void detach();
    bool alive() const;

    std::uint32_t pid() const noexcept { return pid_; }
    HANDLE handle() const noexcept { return handle_; }
    const ModuleInfo& main_module() const noexcept { return main_; }

    std::size_t read_into(std::uintptr_t addr, void* dst, std::size_t size) const;
    bool read_raw(std::uintptr_t addr, void* dst, std::size_t size) const;
    bool write_raw(std::uintptr_t addr, const void* src, std::size_t size) const;

    template <typename T>
    bool read(std::uintptr_t addr, T& out) const {
        return read_raw(addr, static_cast<void*>(&out), sizeof(T));
    }

    template <typename T>
    bool write(std::uintptr_t addr, const T& in) const {
        return write_raw(addr, static_cast<const void*>(&in), sizeof(T));
    }

    std::uintptr_t read_ptr(std::uintptr_t addr) const;

    template <typename T>
    T read_or(std::uintptr_t addr, T fallback) const {
        T value{};
        return read(addr, value) ? value : fallback;
    }

    std::vector<ModuleInfo> modules() const;

private:
    HANDLE handle_ = nullptr;
    std::uint32_t pid_ = 0;
    ModuleInfo main_{};
};

} // namespace ext
