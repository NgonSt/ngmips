#pragma once
#include <cstdint>

class MipsBase; // Cross reference

class MipsCopBase {
public:
    virtual void ConnectCpu(MipsBase* cpu) = 0;
    virtual void Reset() = 0;
    virtual void Command(uint32_t command) = 0;
    virtual uint32_t Read32(int idx) = 0;
    virtual void Write32(int idx, uint32_t value) = 0;
    virtual uint64_t Read64(int idx) = 0;
    virtual void Write64(int idx, uint64_t value) = 0;
    virtual uint32_t Read32Internal(int idx) = 0;
    virtual void Write32Internal(int idx, uint32_t value) = 0;
    virtual uint64_t Read64Internal(int idx) = 0;
    virtual void Write64Internal(int idx, uint64_t value) = 0;
    virtual bool GetFlag() = 0;
};