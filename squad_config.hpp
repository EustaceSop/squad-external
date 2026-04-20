#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include "crypt/xor.hpp"

// ============================================================================
// INI Config System — Save/Load all settings
// Simple key=value format, no external dependencies
// ============================================================================

class Config {
public:
    void bind_bool(const char* key, bool* val)    { m_bools[key] = val; }
    void bind_int(const char* key, int* val)      { m_ints[key] = val; }
    void bind_float(const char* key, float* val)  { m_floats[key] = val; }
    void bind_string(const char* key, char* val, int maxLen) {
        m_strings[key] = { val, maxLen };
    }
    void bind_color(const char* key, uint32_t* val) { m_colors[key] = val; }

    bool save(const char* path) {
        FILE* f = nullptr;
        fopen_s(&f, path, "w");
        if (!f) return false;

        fprintf(f, "; Squad ESP Config\n\n");

        for (auto& [k, v] : m_bools)  fprintf(f, "%s=%d\n", k.c_str(), *v ? 1 : 0);
        for (auto& [k, v] : m_ints)   fprintf(f, "%s=%d\n", k.c_str(), *v);
        for (auto& [k, v] : m_floats) fprintf(f, "%s=%.4f\n", k.c_str(), *v);
        for (auto& [k, v] : m_strings) fprintf(f, "%s=%s\n", k.c_str(), v.ptr);
        for (auto& [k, v] : m_colors) fprintf(f, "%s=0x%08X\n", k.c_str(), *v);

        fclose(f);
        return true;
    }

    bool load(const char* path) {
        FILE* f = nullptr;
        fopen_s(&f, path, "r");
        if (!f) return false;

        char line[512];
        while (fgets(line, sizeof(line), f)) {
            // Skip comments and empty lines
            if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '\r')
                continue;

            // Remove trailing newline
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';

            // Split key=value
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            const char* key = line;
            const char* val = eq + 1;

            // Try match
            auto bi = m_bools.find(key);
            if (bi != m_bools.end()) { *bi->second = (atoi(val) != 0); continue; }

            auto ii = m_ints.find(key);
            if (ii != m_ints.end()) { *ii->second = atoi(val); continue; }

            auto fi = m_floats.find(key);
            if (fi != m_floats.end()) { *fi->second = (float)atof(val); continue; }

            auto si = m_strings.find(key);
            if (si != m_strings.end()) {
                strncpy_s(si->second.ptr, si->second.maxLen, val, _TRUNCATE);
                continue;
            }

            auto ci = m_colors.find(key);
            if (ci != m_colors.end()) {
                *ci->second = (uint32_t)strtoul(val, nullptr, 16);
                continue;
            }
        }

        fclose(f);
        return true;
    }

private:
    struct StrBind { char* ptr; int maxLen; };

    std::map<std::string, bool*>     m_bools;
    std::map<std::string, int*>      m_ints;
    std::map<std::string, float*>    m_floats;
    std::map<std::string, StrBind>   m_strings;
    std::map<std::string, uint32_t*> m_colors;
};
