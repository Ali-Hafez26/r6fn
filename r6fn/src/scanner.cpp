#include "scanner.hpp"

#include "process.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ext {
namespace {

constexpr std::size_t kChunkSize = 1u << 20;

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Pattern compile_pattern(const char* text) {
    Pattern out;
    if (text == nullptr) {
        return out;
    }

    const char* cursor = text;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        if (*cursor == '?') {
            ++cursor;
            if (*cursor == '?') {
                ++cursor;
            }
            out.bytes.push_back(0);
            out.mask.push_back(0);
            continue;
        }

        const int hi = hex_nibble(*cursor);
        if (hi < 0) {
            out.bytes.clear();
            out.mask.clear();
            return out;
        }
        ++cursor;
        const int lo = hex_nibble(*cursor);
        if (lo < 0) {
            out.bytes.clear();
            out.mask.clear();
            return out;
        }
        ++cursor;

        out.bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        out.mask.push_back(1);
    }

    out.valid = !out.bytes.empty() && out.bytes.size() == out.mask.size();
    return out;
}

Scanner::Scanner(const Process& proc, std::uintptr_t base, std::size_t size)
    : proc_(proc), base_(base), size_(size) {}

bool Scanner::matches_at(const std::uint8_t* data, std::size_t available, const Pattern& pattern,
                         std::size_t at) const {
    if (at + pattern.bytes.size() > available) {
        return false;
    }
    for (std::size_t i = 0; i < pattern.bytes.size(); ++i) {
        if (pattern.mask[i] != 0 && data[at + i] != pattern.bytes[i]) {
            return false;
        }
    }
    return true;
}

std::uintptr_t Scanner::find(const Pattern& pattern) const {
    if (!pattern.valid || base_ == 0 || size_ < pattern.bytes.size()) {
        return 0;
    }

    const std::size_t overlap = pattern.bytes.size() - 1;
    std::vector<std::uint8_t> buffer(kChunkSize + overlap);

    std::size_t offset = 0;
    while (offset < size_) {
        const std::size_t want = std::min(kChunkSize + overlap, size_ - offset);
        const std::size_t got = proc_.read_into(base_ + offset, buffer.data(), want);

        if (got >= pattern.bytes.size()) {
            const std::size_t last = got - pattern.bytes.size();
            const std::uint8_t first = pattern.bytes[0];
            const bool anchor = pattern.mask[0] != 0;

            for (std::size_t i = 0; i <= last; ++i) {
                if (anchor && buffer[i] != first) {
                    continue;
                }
                if (matches_at(buffer.data(), got, pattern, i)) {
                    return base_ + offset + i;
                }
            }
        }

        offset += kChunkSize;
    }

    return 0;
}

std::vector<std::uintptr_t> Scanner::find_all(const Pattern& pattern, std::size_t max_hits) const {
    std::vector<std::uintptr_t> hits;
    if (!pattern.valid || base_ == 0 || size_ < pattern.bytes.size() || max_hits == 0) {
        return hits;
    }

    const std::size_t overlap = pattern.bytes.size() - 1;
    std::vector<std::uint8_t> buffer(kChunkSize + overlap);

    std::size_t offset = 0;
    while (offset < size_ && hits.size() < max_hits) {
        const std::size_t want = std::min(kChunkSize + overlap, size_ - offset);
        const std::size_t got = proc_.read_into(base_ + offset, buffer.data(), want);

        if (got >= pattern.bytes.size()) {
            const std::size_t last = got - pattern.bytes.size();
            for (std::size_t i = 0; i <= last; ++i) {
                if (matches_at(buffer.data(), got, pattern, i)) {
                    hits.push_back(base_ + offset + i);
                    if (hits.size() >= max_hits) {
                        return hits;
                    }
                }
            }
        }

        offset += kChunkSize;
    }

    return hits;
}

std::uintptr_t Scanner::rip_target(std::uintptr_t match, std::size_t disp_offset,
                                   std::size_t instr_len) const {
    std::int32_t disp = 0;
    if (!proc_.read(match + disp_offset, disp)) {
        return 0;
    }
    return match + instr_len + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(disp));
}

} // namespace ext
