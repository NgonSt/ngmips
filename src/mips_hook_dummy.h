#pragma once
#include "mips_hook.h"

class MipsHookDummy : public MipsHookBase {
public:
    MipsHookDummy() {};

    void Reset() override {};
    void OnPreExecute(uint64_t pc, uint32_t opcode) override {};
    void OnLoad8(uint64_t address) override {};
    void OnLoad16(uint64_t address) override {};
    void OnLoad32(uint64_t address) override {};
    void OnLoad64(uint64_t address) override {};
    void OnStore8(uint64_t address, uint8_t value) override {};
    void OnStore16(uint64_t address, uint16_t value) override {};
    void OnStore32(uint64_t address, uint32_t value) override {};
    void OnStore64(uint64_t address, uint64_t value) override {};
};