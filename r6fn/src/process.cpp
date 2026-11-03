#include "process.hpp"

#include <tlhelp32.h>
#include <psapi.h>
#include <cwchar>
#include <cstring>
#include <utility>
#include <wchar.h>

#pragma comment(lib, "psapi.lib")

namespace ext {
namespace {

std::wstring file_name_of(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::uint32_t find_pid(const std::wstring& exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::uint32_t pid = 0;

    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exe_name.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return pid;
}

} // namespace

Process::~Process() {
    detach();
}

bool Process::attach(const std::wstring& exe_name) {
    detach();

    const std::uint32_t pid = find_pid(exe_name);
    if (pid == 0) {
        return false;
    }

    HANDLE handle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                                    PROCESS_QUERY_INFORMATION,
                                FALSE, pid);
    if (handle == nullptr) {
        return false;
    }

    handle_ = handle;
    pid_ = pid;

    const std::vector<ModuleInfo> mods = modules();
    const std::wstring wanted = exe_name;

    const ModuleInfo* best = nullptr;
    for (const ModuleInfo& mod : mods) {
        if (_wcsicmp(file_name_of(mod.name).c_str(), wanted.c_str()) == 0) {
            best = &mod;
            break;
        }
    }
    if (best == nullptr) {
        for (const ModuleInfo& mod : mods) {
            if (best == nullptr || mod.size > best->size) {
                best = &mod;
            }
        }
    }

    if (best != nullptr) {
        main_ = *best;
    } else {
        return false;
    }

    return true;
}

void Process::detach() {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
    main_ = {};
}

bool Process::alive() const {
    if (handle_ == nullptr) {
        return false;
    }
    return WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT;
}

std::size_t Process::read_into(std::uintptr_t addr, void* dst, std::size_t size) const {
    if (handle_ == nullptr || addr == 0 || size == 0 || dst == nullptr) {
        return 0;
    }

    SIZE_T got = 0;
    ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr), dst, size, &got);
    return static_cast<std::size_t>(got);
}

bool Process::read_raw(std::uintptr_t addr, void* dst, std::size_t size) const {
    return read_into(addr, dst, size) == size;
}

bool Process::write_raw(std::uintptr_t addr, const void* src, std::size_t size) const {
    if (handle_ == nullptr || addr == 0 || size == 0 || src == nullptr) {
        return false;
    }

    SIZE_T written = 0;
    return WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(addr), src, size, &written) != FALSE &&
           written == size;
}

std::uintptr_t Process::read_ptr(std::uintptr_t addr) const {
    std::uintptr_t value = 0;
    return read(addr, value) ? value : 0;
}

std::vector<ModuleInfo> Process::modules() const {
    std::vector<ModuleInfo> out;
    if (pid_ == 0) {
        return out;
    }

    // EAC blocks external PEB reads. Using Toolhelp32 to enumerate modules instead.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            do {
                ModuleInfo info{};
                info.base = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
                info.size = me.modBaseSize;
                info.name = me.szModule;
                out.push_back(std::move(info));
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
    }
    return out;
}

} // namespace ext
