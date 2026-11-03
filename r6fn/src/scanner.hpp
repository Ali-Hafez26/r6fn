#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ext {

class Process;

struct Pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask; // 1 = compare byte, 0 = wildcard
    bool valid = false;
};

// "48 8B 05 ? ? ? ? 48 85 C0" -> bytes/mask. Whitespace separated, '?' or '??'
// is a wildcard.
Pattern compile_pattern(const char* text);

class Scanner {
public:
    Scanner(const Process& proc, std::uintptr_t base, std::size_t size);

    std::uintptr_t find(const Pattern& pattern) const;
    std::vector<std::uintptr_t> find_all(const Pattern& pattern, std::size_t max_hits) const;

    // Reads the int32 displacement at match + disp_offset, then returns
    // match + instr_len + disp = the absolute RIP-relative target.
    std::uintptr_t rip_target(std::uintptr_t match, std::size_t disp_offset,
                              std::size_t instr_len) const;

private:
    bool matches_at(const std::uint8_t* data, std::size_t available, const Pattern& pattern,
                    std::size_t at) const;

    const Process& proc_;
    std::uintptr_t base_;
    std::size_t size_;
};

} // namespace ext
