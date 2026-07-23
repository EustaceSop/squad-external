#pragma once
#include "squad_driver.hpp"
#include "squad_offsets.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

// ============================================================================
// FName resolution for external reads (v10.5.1 / UE 5.7.4)
// FNamePool base is discovered from FName::AppendString code references and
// validated by resolving entry 0 -> "None". Used for actor class-name checks.
// ============================================================================

class NameResolver {
public:
    bool init(SquadDriver& drv, uint64_t game_base)
    {
        constexpr size_t code_len = 0x400;
        std::vector<uint8_t> code(code_len);
        uint64_t fn = game_base + squad::AppendString;
        if (!drv.read_raw(fn, code.data(), code_len)) return false;

        // scan for RIP-relative lea/mov (REX 48/4C, op 8B/8D, modrm mod=0 rm=101)
        for (size_t i = 0; i + 7 <= code_len; i++) {
            if (code[i] != 0x48 && code[i] != 0x4C) continue;
            if (code[i+1] != 0x8B && code[i+1] != 0x8D) continue;
            if ((code[i+2] & 0xC7) != 0x05) continue;
            int32_t disp = *(const int32_t*)(code.data() + i + 3);
            uint64_t target = fn + i + 7 + disp;
            if (target < 0x10000 || target > 0x800000000000ULL) continue;
            for (int shard_off : { 0x10, 0x18, 0x08, 0x00 }) {
                if (!probe_none(drv, target, shard_off)) continue;
                m_pool = target;
                m_shard_off = shard_off;
                m_ok = true;
                return true;
            }
        }
        return false;
    }

    void init_from(uint64_t pool, int shard_off)
    {
        m_pool = pool;
        m_shard_off = shard_off;
        m_ok = pool != 0;
    }

    bool ok() const { return m_ok; }
    uint64_t pool() const { return m_pool; }
    int shard_off() const { return m_shard_off; }

    // FName ComparisonIndex -> string
    bool resolve(SquadDriver& drv, uint32_t id, std::string& out) const
    {
        if (!m_ok) return false;
        uint64_t shard = drv.read<uint64_t>(m_pool + m_shard_off + (uint64_t)(id >> 16) * 8);
        if (shard < 0x10000 || shard > 0x800000000000ULL) return false;
        uint64_t entry = shard + (uint64_t)(id & 0xFFFF) * 2;
        uint16_t hdr = drv.read<uint16_t>(entry);
        int len   = hdr >> 6;
        bool wide = (hdr & 1) != 0;
        if (len <= 0 || len > 128) return false;
        if (wide) {
            std::wstring w(len, L'\0');
            if (!drv.read_raw(entry + 2, w.data(), len * 2)) return false;
            out.assign(w.begin(), w.end());
        } else {
            out.resize(len);
            if (!drv.read_raw(entry + 2, out.data(), len)) return false;
        }
        return true;
    }

    // UObject -> class name (UObject+0x10 ClassPrivate -> UClass+0x18 NamePrivate)
    bool class_name(SquadDriver& drv, uint64_t uobject, std::string& out) const
    {
        if (!m_ok) return false;
        uint64_t uclass = drv.read<uint64_t>(uobject + 0x10);
        if (uclass < 0x10000 || uclass > 0x800000000000ULL) return false;
        uint32_t id = drv.read<uint32_t>(uclass + 0x18);
        return resolve(drv, id, out);
    }

private:
    static bool probe_none(SquadDriver& drv, uint64_t pool, int shard_off)
    {
        // Entry 0 in shard 0 must be FName "None" (4 ansi chars)
        uint64_t shard0 = drv.read<uint64_t>(pool + shard_off);
        if (shard0 < 0x10000 || shard0 > 0x800000000000ULL) return false;
        uint16_t hdr = drv.read<uint16_t>(shard0);
        if ((hdr & 1) || (hdr >> 6) != 4) return false;
        char s[5]{};
        if (!drv.read_raw(shard0 + 2, s, 4)) return false;
        return memcmp(s, "None", 4) == 0;
    }

    uint64_t m_pool = 0;
    int      m_shard_off = 0x10;
    bool     m_ok = false;
};
