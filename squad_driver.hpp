#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <intrin.h>
#include "crypt/lazyimporter.hpp"
#include "crypt/xor.hpp"

// ============================================================================
// Squad Driver Interface - wraps EvoriaSharedMemorySigned for Squad
// Multi-channel support: each thread gets its own channel
// ============================================================================

// ---------------------------------------------------------------------------
// Shared memory protocol (must match kernel driver)
// ---------------------------------------------------------------------------

#define KUSER_SHARED_DATA_ADDR  0x7FFE0000
#define BOOT_ID_OFFSET          0x264

inline uint32_t HashBootId()
{
    const unsigned char* guid = (const unsigned char*)(uintptr_t)(KUSER_SHARED_DATA_ADDR + BOOT_ID_OFFSET);
    uint32_t h = 0x811C9DC5;
    for (int i = 0; i < 16; i++) {
        h ^= guid[i];
        h *= 0x01000193;
    }
    return h;
}

inline void GenerateSectionName(wchar_t* buf, size_t bufLen)
{
    uint32_t hash = HashBootId();
    // Build prefix at runtime to avoid string in binary
    auto prefix = xorstr_(L"Global\\SM0");
    const wchar_t hexChars[] = L"0123456789abcdef";
    size_t prefixLen = wcslen(prefix);
    if (bufLen < prefixLen + 9) return;
    for (size_t i = 0; i < prefixLen; i++) buf[i] = prefix[i];
    for (int i = 7; i >= 0; i--) {
        buf[prefixLen + i] = hexChars[hash & 0xF];
        hash >>= 4;
    }
    buf[prefixLen + 8] = L'\0';
}

enum class RequestType : uint32_t {
    None = 0,
    GetProcessPid = 1,
    GetModuleBase = 2,
    ReadMemory = 3,
    WriteMemory = 4,
    BatchReadMemory = 5,
    GetSectionBase = 6,
    SetKnownAddress = 7,
    BatchWriteMemory = 8,
    Shutdown = 0xFF
};

enum class RequestStatus : uint32_t {
    Idle = 0,
    Pending = 1,
    Complete = 2,
    Error = 3
};

#pragma pack(push, 1)
struct SharedRequest {
    volatile RequestStatus status;
    RequestType            type;
    uint32_t               targetPid;
    uint64_t               address;
    uint64_t               size;
    uint64_t               result;
    wchar_t                processName[64];
    wchar_t                moduleName[64];
    uint32_t               batchCount;
    uint32_t               _pad;
};

struct BatchEntry {
    uint64_t address;
    uint32_t size;
    uint32_t offset;
};

struct BatchWriteEntry {
    uint64_t address;
    uint32_t size;
    uint32_t offset;
};
#pragma pack(pop)

#define NUM_CHANNELS             8
#define CHANNEL_SIZE             0x10000
#define SHARED_TOTAL_SIZE_DRV    (NUM_CHANNELS * CHANNEL_SIZE)

#define SHARED_HEADER_OFFSET     0x0000
#define BATCH_ARRAY_OFFSET       0x0200
#define BATCH_WRITE_ARRAY_OFFSET 0x0400
#define DATA_BUFFER_OFFSET       0x0800
#define MAX_DATA_SIZE_DRV        (CHANNEL_SIZE - DATA_BUFFER_OFFSET)
#define MAX_BATCH_ENTRIES        16
#define MAX_BATCH_WRITE_ENTRIES  64

// ---------------------------------------------------------------------------
// Per-channel handle
// ---------------------------------------------------------------------------
struct DriverChannel {
    SharedRequest*  header;
    BatchEntry*     batch;
    BatchWriteEntry* batchWrite;
    uint8_t*        data;
};

// ---------------------------------------------------------------------------
// SquadDriver - main interface
// ---------------------------------------------------------------------------
class SquadDriver {
public:
    SquadDriver() = default;
    ~SquadDriver() { disconnect(); }

    SquadDriver(const SquadDriver&) = delete;
    SquadDriver& operator=(const SquadDriver&) = delete;

    bool connect() {
        wchar_t name[64] = {};
        GenerateSectionName(name, 64);

        m_section = LI_FN(OpenFileMappingW)(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!m_section) return false;

        m_base = (uint8_t*)LI_FN(MapViewOfFile)(m_section, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_TOTAL_SIZE_DRV);
        if (!m_base) {
            LI_FN(CloseHandle)(m_section);
            m_section = NULL;
            return false;
        }

        // Initialize all channels
        for (int i = 0; i < NUM_CHANNELS; i++) {
            uint8_t* ch_base = m_base + (i * CHANNEL_SIZE);
            m_channels[i].header    = (SharedRequest*)(ch_base + SHARED_HEADER_OFFSET);
            m_channels[i].batch     = (BatchEntry*)(ch_base + BATCH_ARRAY_OFFSET);
            m_channels[i].batchWrite = (BatchWriteEntry*)(ch_base + BATCH_WRITE_ARRAY_OFFSET);
            m_channels[i].data      = ch_base + DATA_BUFFER_OFFSET;
        }

        return true;
    }

    void disconnect() {
        if (m_base) { LI_FN(UnmapViewOfFile)(m_base); m_base = nullptr; }
        if (m_section) { LI_FN(CloseHandle)(m_section); m_section = NULL; }
    }

    bool is_connected() const { return m_base != nullptr; }

    // ---------------------------------------------------------------------------
    // Attach to Squad process
    // ---------------------------------------------------------------------------
    bool attach() {
        m_pid = get_process_pid(xorstr_(L"SquadGame.exe"));
        if (!m_pid) return false;

        m_base_addr = get_section_base(m_pid);
        if (!m_base_addr) {
            // Fallback to module base
            m_base_addr = get_module_base(m_pid, xorstr_(L"SquadGame.exe"));
        }
        return m_base_addr != 0;
    }

    uint32_t pid() const { return m_pid; }
    uint64_t base() const { return m_base_addr; }

    // ---------------------------------------------------------------------------
    // Read / Write (channel 0 by default)
    // ---------------------------------------------------------------------------
    bool read_raw(uint64_t address, void* buf, size_t size, int ch = 0) {
        auto& c = m_channels[ch];
        if (!c.header || size == 0 || size > MAX_DATA_SIZE_DRV) return false;

        wait_idle(c);
        c.header->targetPid = m_pid;
        c.header->address = address;
        c.header->size = size;
        c.header->type = RequestType::ReadMemory;
        c.header->result = 0;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;

        if (!wait_complete(c)) return false;
        memcpy(buf, c.data, size);
        return true;
    }

    bool write_raw(uint64_t address, const void* buf, size_t size, int ch = 0) {
        auto& c = m_channels[ch];
        if (!c.header || size == 0 || size > MAX_DATA_SIZE_DRV) return false;

        wait_idle(c);
        memcpy(c.data, buf, size);
        c.header->targetPid = m_pid;
        c.header->address = address;
        c.header->size = size;
        c.header->type = RequestType::WriteMemory;
        c.header->result = 0;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;

        return wait_complete(c);
    }

    template<typename T>
    T read(uint64_t address, int ch = 0) {
        T val{};
        read_raw(address, &val, sizeof(T), ch);
        return val;
    }

    template<typename T>
    bool write(uint64_t address, const T& val, int ch = 0) {
        return write_raw(address, &val, sizeof(T), ch);
    }

    // ---------------------------------------------------------------------------
    // Batch read (one kernel transition for many addresses)
    // ---------------------------------------------------------------------------
    struct BatchReq {
        uint64_t address;
        uint32_t size;
        void*    output;
    };

    bool batch_read(const std::vector<BatchReq>& reqs, int ch = 0) {
        auto& c = m_channels[ch];
        if (!c.header || reqs.empty() || reqs.size() > MAX_BATCH_ENTRIES) return false;

        wait_idle(c);

        uint32_t offset = 0;
        for (size_t i = 0; i < reqs.size(); i++) {
            if (offset + reqs[i].size > MAX_DATA_SIZE_DRV) return false;
            c.batch[i].address = reqs[i].address;
            c.batch[i].size = reqs[i].size;
            c.batch[i].offset = offset;
            offset += reqs[i].size;
            offset = (offset + 7) & ~7u;  // align 8
        }

        c.header->targetPid = m_pid;
        c.header->batchCount = (uint32_t)reqs.size();
        c.header->type = RequestType::BatchReadMemory;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;

        if (!wait_complete(c)) return false;

        for (size_t i = 0; i < reqs.size(); i++) {
            if (reqs[i].output)
                memcpy(reqs[i].output, c.data + c.batch[i].offset, reqs[i].size);
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Batch write
    // ---------------------------------------------------------------------------
    struct BatchWriteReq {
        uint64_t address;
        uint32_t size;
        const void* input;
    };

    bool batch_write(const std::vector<BatchWriteReq>& reqs, int ch = 0) {
        auto& c = m_channels[ch];
        if (!c.header || reqs.empty() || reqs.size() > MAX_BATCH_WRITE_ENTRIES) return false;

        wait_idle(c);

        uint32_t offset = 0;
        for (size_t i = 0; i < reqs.size(); i++) {
            if (offset + reqs[i].size > MAX_DATA_SIZE_DRV) return false;
            c.batchWrite[i].address = reqs[i].address;
            c.batchWrite[i].size = reqs[i].size;
            c.batchWrite[i].offset = offset;
            memcpy(c.data + offset, reqs[i].input, reqs[i].size);
            offset += reqs[i].size;
            offset = (offset + 7) & ~7u;
        }

        c.header->targetPid = m_pid;
        c.header->batchCount = (uint32_t)reqs.size();
        c.header->type = RequestType::BatchWriteMemory;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;

        return wait_complete(c);
    }

    // ---------------------------------------------------------------------------
    // Read FString (UE5 FString = TArray<wchar_t>)
    // ---------------------------------------------------------------------------
    std::wstring read_fstring(uint64_t address, int ch = 0) {
        struct { uint64_t data; int32_t count; int32_t max; } arr{};
        if (!read_raw(address, &arr, sizeof(arr), ch)) return L"";
        if (arr.count <= 0 || arr.count > 256 || !arr.data) return L"";

        std::wstring buf(arr.count, L'\0');
        if (!read_raw(arr.data, buf.data(), arr.count * sizeof(wchar_t), ch)) return L"";
        // Remove null terminator if present
        if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
        return buf;
    }

private:
    HANDLE          m_section = NULL;
    uint8_t*        m_base = nullptr;
    DriverChannel   m_channels[NUM_CHANNELS] = {};
    uint32_t        m_pid = 0;
    uint64_t        m_base_addr = 0;

    // ---------------------------------------------------------------------------
    // Process / Module helpers (use channel 7 to avoid conflicts)
    // ---------------------------------------------------------------------------
    uint32_t get_process_pid(const wchar_t* name) {
        auto& c = m_channels[7];
        wait_idle(c);
        memset(c.header->processName, 0, sizeof(c.header->processName));
        wcsncpy_s(c.header->processName, name, 63);
        c.header->type = RequestType::GetProcessPid;
        c.header->result = 0;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;
        if (!wait_complete(c)) return 0;
        return (uint32_t)c.header->result;
    }

    uint64_t get_module_base(uint32_t pid, const wchar_t* mod) {
        auto& c = m_channels[7];
        wait_idle(c);
        c.header->targetPid = pid;
        memset(c.header->moduleName, 0, sizeof(c.header->moduleName));
        if (mod) wcsncpy_s(c.header->moduleName, mod, 63);
        c.header->type = RequestType::GetModuleBase;
        c.header->result = 0;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;
        if (!wait_complete(c)) return 0;
        return c.header->result;
    }

    uint64_t get_section_base(uint32_t pid) {
        auto& c = m_channels[7];
        wait_idle(c);
        c.header->targetPid = pid;
        c.header->type = RequestType::GetSectionBase;
        c.header->result = 0;
        MemoryBarrier();
        c.header->status = RequestStatus::Pending;
        if (!wait_complete(c)) return 0;
        return c.header->result;
    }

    // ---------------------------------------------------------------------------
    // Spinlock helpers
    // ---------------------------------------------------------------------------
    void wait_idle(DriverChannel& c) {
        while (c.header->status == RequestStatus::Pending)
            _mm_pause();
        c.header->status = RequestStatus::Idle;
    }

    bool wait_complete(DriverChannel& c, uint32_t timeout_ms = 5000) {
        auto start = GetTickCount64();
        while (c.header->status == RequestStatus::Pending) {
            _mm_pause();
            if ((GetTickCount64() - start) > timeout_ms) return false;
        }
        return c.header->status == RequestStatus::Complete;
    }
};
