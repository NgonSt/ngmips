#pragma once
#include <cstdint>

class MipsHookBase {
public:
    virtual void Reset() = 0;
    virtual void OnPreExecute(uint64_t pc, uint32_t opcode) = 0;
    virtual void OnLoad8(uint64_t address) = 0;
    virtual void OnLoad16(uint64_t address) = 0;
    virtual void OnLoad32(uint64_t address) = 0;
    virtual void OnLoad64(uint64_t address) = 0;
    virtual void OnStore8(uint64_t address, uint8_t value) = 0;
    virtual void OnStore16(uint64_t address, uint16_t value) = 0;
    virtual void OnStore32(uint64_t address, uint32_t value) = 0;
    virtual void OnStore64(uint64_t address, uint64_t value) = 0;
};