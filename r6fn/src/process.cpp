#include "process.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <cstring>
#include <utility>
#include <wchar.h>

namespace ext {
namespace {

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct ProcessBasicInformation {
    PVOID reserved1;
    PVOID peb_base_address;
    PVOID reserved2[2];
    ULONG_PTR unique_process_id;
    PVOID reserved3;
};

struct PebPartial {
    BYTE inherited_address_space;
    BYTE read_image_file_exec_options;
    BYTE being_debugged;
    BYTE bit_field;
    BYTE pad0[4];
    PVOID mutant;
    PVOID image_base_address;
    PVOID ldr;
    PVOID process_parameters;
};

struct PebLdrDataPartial {
    ULONG length;
    BOOLEAN initialized;
    BYTE pad0[3];
    PVOID ss_handle;
    LIST_ENTRY in_load_order;
    LIST_ENTRY in_memory_order;
    LIST_ENTRY in_init_order;
};

struct LdrEntryPartial {
    LIST_ENTRY in_load_order;
    LIST_ENTRY in_memory_order;
    LIST_ENTRY in_init_order;
    PVOID dll_base;
    PVOID entry_point;
    ULONG size_of_image;
    BYTE pad0[4];
    UNICODE_STRING full_dll_name;
    UNICODE_STRING base_dll_name;
};

static_assert(offsetof(PebPartial, ldr) == 0x18, "PEB.Ldr must be at 0x18 on x64");
static_assert(offsetof(PebPartial, process_parameters) == 0x20, "PEB.ProcessParameters must be at 0x20");
static_assert(offsetof(PebLdrDataPartial, in_memory_order) == 0x20, "PEB_LDR_DATA.InMemoryOrderModuleList must be at 0x20");
static_assert(offsetof(LdrEntryPartial, dll_base) == 0x30, "LDR_DATA_TABLE_ENTRY.DllBase must be at 0x30");
static_assert(offsetof(LdrEntryPartial, base_dll_name) == 0x58, "LDR_DATA_TABLE_ENTRY.BaseDllName must be at 0x58");

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
    if (handle_ == nullptr) {
        return out;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return out;
    }

    const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (query == nullptr) {
        return out;
    }

    ProcessBasicInformation pbi{};
    ULONG returned = 0;
    if (query(handle_, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &returned) < 0) {
        return out;
    }
    if (pbi.peb_base_address == nullptr) {
        return out;
    }

    PebPartial peb{};
    if (!read_raw(reinterpret_cast<std::uintptr_t>(pbi.peb_base_address), &peb, sizeof(peb))) {
        return out;
    }
    if (peb.ldr == nullptr) {
        return out;
    }

    PebLdrDataPartial ldr{};
    if (!read_raw(reinterpret_cast<std::uintptr_t>(peb.ldr), &ldr, sizeof(ldr))) {
        return out;
    }

    const std::uintptr_t head = reinterpret_cast<std::uintptr_t>(peb.ldr) +
                                offsetof(PebLdrDataPartial, in_memory_order);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(ldr.in_memory_order.Flink);

    int guard = 0;
    while (cursor != 0 && cursor != head && guard++ < 4096) {
        const std::uintptr_t entry_addr = cursor - offsetof(LdrEntryPartial, in_memory_order);

        LdrEntryPartial entry{};
        if (!read_raw(entry_addr, &entry, sizeof(entry))) {
            break;
        }

        ModuleInfo info{};
        info.base = reinterpret_cast<std::uintptr_t>(entry.dll_base);
        info.size = entry.size_of_image;

        if (entry.base_dll_name.Buffer != nullptr && entry.base_dll_name.Length > 0) {
            std::wstring name(entry.base_dll_name.Length / sizeof(wchar_t), L'\0');
            if (read_raw(reinterpret_cast<std::uintptr_t>(entry.base_dll_name.Buffer), name.data(),
                         entry.base_dll_name.Length)) {
                info.name = std::move(name);
            }
        }

        out.push_back(std::move(info));
        cursor = reinterpret_cast<std::uintptr_t>(entry.in_memory_order.Flink);
    }

    return out;
}

} // namespace ext
