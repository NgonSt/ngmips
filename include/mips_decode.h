#pragma once
#include <cstdint>
#include <string>

enum class MipsInstId {
  kAdd,
  kAddu,
  kAddi,
  kAddiu,
  kAnd,
  kAndi,
  kDiv,
  kDivu,
  kMult,
  kMultu,
  kNor,
  kOr,
  kOri,
  kSll,
  kSllv,
  kSra,
  kSrav,
  kSrl,
  kSrlv,
  kSub,
  kSubu,
  kXor,
  kXori,
  kLui,
  kSlt,
  kSltu,
  kSlti,
  kSltiu,
  kBeq,
  kBne,
  kBgtz,
  kBlez,
  kBgez,
  kBgezal,
  kBltz,
  kBltzal,
  kJ,
  kJal,
  kJr,
  kJalr,
  kSyscall,
  kBreak,
  kLb,
  kLbu,
  kLh,
  kLhu,
  kLw,
  kLwl,
  kLwr,
  kLwc,
  kSb,
  kSh,
  kSw,
  kSwl,
  kSwr,
  kSwc,
  kMfhi,
  kMflo,
  kMthi,
  kMtlo,
  kCop,
  kMfc,
  kCfc,
  kMtc,
  kCtc,
  kNop,

  kBcf,
  kBcfl,
  kBct,
  kBctl,
  kBeql,
  kBnel,
  kBgezl,
  kBgezall,
  kBgtzl,
  kBlezl,
  kBltzl,
  kBltzall,
  kCache,
  kDadd,
  kDaddu,
  kDaddi,
  kDaddiu,
  kDsub,
  kDsubu,
  kDmult,
  kDmultu,
  kDdiv,
  kDdivu,
  kDsll,
  kDsll32,
  kDsllv,
  kDsra,
  kDsra32,
  kDsrav,
  kDsrl,
  kDsrl32,
  kDsrlv,
  kDmfc,
  kDmtc,
  kLd,
  kLdc,
  kLdl,
  kLdr,
  kLwu,
  kSd,
  kSdc,
  kSdl,
  kSdr,
  kSync,

  kUnknown
};

class RTypeInst {
 private:
  uint32_t raw_;

 public:
  RTypeInst(uint32_t opcode) {
    raw_ = opcode;
  }

  uint8_t funct() { return raw_ & 0b111111; };
  uint8_t shamt() { return (raw_ >> 6) & 0b11111; };
  uint8_t rd() { return (raw_ >> 11) & 0b11111; };
  uint8_t rt() { return (raw_ >> 16) & 0b11111; };
  uint8_t rs() { return (raw_ >> 21) & 0b11111; };
  uint8_t op() { return (raw_ >> 26) & 0b111111; };
};

class ITypeInst {
 private:
  uint32_t raw_;

 public:
  ITypeInst(uint32_t opcode) {
    raw_ = opcode;
  }

  uint16_t imm() { return raw_ & 0xFFFF; };
  uint8_t rt() { return (raw_ >> 16) & 0b11111; };
  uint8_t rs() { return (raw_ >> 21) & 0b11111; };
  uint8_t op() { return (raw_ >> 26) & 0b111111; };
};

class JTypeInst {
 private:
  uint32_t raw_;

 public:
  JTypeInst(uint32_t opcode) {
    raw_ = opcode;
  }

  uint32_t address() { return raw_ & 0x3FFFFFF; };
  uint8_t op() { return (raw_ >> 26) & 0b111111; };
};

class MipsInst {
 private:
  uint32_t raw_;

 public:
  MipsInst(uint32_t opcode) {
    raw_ = opcode;
  }

  RTypeInst GetRType() {
    return RTypeInst(raw_);
  }

  ITypeInst GetIType() {
    return ITypeInst(raw_);
  }

  JTypeInst GetJType() {
    return JTypeInst(raw_);
  }

  uint8_t op() {
    return (raw_ >> 26) & 0b111111;
  }

  uint32_t GetOpcodeRaw() {
    return raw_;
  }

  std::string Disassemble(uint64_t address);
};

std::string GetInstName(uint32_t opcode);
MipsInstId Decode(uint32_t opcode);
bool IsInstBranch(uint32_t opcode);
bool DoesInstHaveDelaySlot(uint32_t opcode);
const char* GetMipsRegName(int index);