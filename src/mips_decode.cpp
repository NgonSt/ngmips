#include "mips_decode.h"

#include <fmt/format.h>

namespace {

constexpr bool kAllowMips3Inst = true;

}

MipsInstId DecodeCondBranch(MipsInst inst) {
  uint32_t opcode = inst.GetOpcodeRaw();
  bool is_bgez = opcode & (1 << 16);
  bool is_linked = opcode & (1 << 20);
  bool is_likely = opcode & (1 << 17);

  if (is_bgez) {
    if (is_likely) {
      if (!kAllowMips3Inst) {
        return MipsInstId::kUnknown;
      }
      return is_linked ? MipsInstId::kBgezall : MipsInstId::kBgezl;
    } else {
      return is_linked ? MipsInstId::kBgezal : MipsInstId::kBgez;
    }
  }

  if (is_likely) {
    if (!kAllowMips3Inst) {
      return MipsInstId::kUnknown;
    }
    return is_linked ? MipsInstId::kBltzall : MipsInstId::kBltzl;
  }
  return is_linked ? MipsInstId::kBltzal : MipsInstId::kBltz;
}

MipsInstId DecodeTypeR(RTypeInst inst) {
  if (inst.funct() == 0 && inst.op() == 0 && inst.shamt() == 0 && inst.rd() == 0) {
    return MipsInstId::kNop;
  }
  if (inst.funct() == 0xF && inst.op() == 0 && inst.shamt() == 0 && inst.rd() == 0) {
    return MipsInstId::kSync;
  }

  switch (inst.funct()) {
    case 0b000000:
      return MipsInstId::kSll;
    case 0b000010:
      return MipsInstId::kSrl;
    case 0b000011:
      return MipsInstId::kSra;
    case 0b000100:
      return MipsInstId::kSllv;
    case 0b000110:
      return MipsInstId::kSrlv;
    case 0b000111:
      return MipsInstId::kSrav;
    case 0b001000:
      return MipsInstId::kJr;
    case 0b001001:
      return MipsInstId::kJalr;
    case 0b001100:
      return MipsInstId::kSyscall;
    case 0b001101:
      return MipsInstId::kBreak;
    case 0b010000:
      return MipsInstId::kMfhi;
    case 0b010001:
      return MipsInstId::kMthi;
    case 0b010010:
      return MipsInstId::kMflo;
    case 0b010011:
      return MipsInstId::kMtlo;
    case 0b010100:
      return kAllowMips3Inst ? MipsInstId::kDsllv : MipsInstId::kUnknown;
    case 0b010110:
      return kAllowMips3Inst ? MipsInstId::kDsrlv : MipsInstId::kUnknown;
    case 0b010111:
      return kAllowMips3Inst ? MipsInstId::kDsrav : MipsInstId::kUnknown;
    case 0b011000:
      return MipsInstId::kMult;
    case 0b011001:
      return MipsInstId::kMultu;
    case 0b011010:
      return MipsInstId::kDiv;
    case 0b011011:
      return MipsInstId::kDivu;
    case 0b011100:
      return kAllowMips3Inst ? MipsInstId::kDmult : MipsInstId::kUnknown;
    case 0b011101:
      return kAllowMips3Inst ? MipsInstId::kDmultu : MipsInstId::kUnknown;
    case 0b011110:
      return kAllowMips3Inst ? MipsInstId::kDdiv : MipsInstId::kUnknown;
    case 0b011111:
      return kAllowMips3Inst ? MipsInstId::kDdivu : MipsInstId::kUnknown;
    case 0b100000:
      return MipsInstId::kAdd;
    case 0b100001:
      return MipsInstId::kAddu;
    case 0b100010:
      return MipsInstId::kSub;
    case 0b100011:
      return MipsInstId::kSubu;
    case 0b100100:
      return MipsInstId::kAnd;
    case 0b100101:
      return MipsInstId::kOr;
    case 0b100110:
      return MipsInstId::kXor;
    case 0b100111:
      return MipsInstId::kNor;
    case 0b101010:
      return MipsInstId::kSlt;
    case 0b101011:
      return MipsInstId::kSltu;
    case 0b101100:
      return kAllowMips3Inst ? MipsInstId::kDadd : MipsInstId::kUnknown;
    case 0b101101:
      return kAllowMips3Inst ? MipsInstId::kDaddu : MipsInstId::kUnknown;
    case 0b101110:
      return kAllowMips3Inst ? MipsInstId::kDsub : MipsInstId::kUnknown;
    case 0b101111:
      return kAllowMips3Inst ? MipsInstId::kDsubu : MipsInstId::kUnknown;
    case 0b111000:
      return kAllowMips3Inst ? MipsInstId::kDsll : MipsInstId::kUnknown;
    case 0b111010:
      return kAllowMips3Inst ? MipsInstId::kDsrl : MipsInstId::kUnknown;
    case 0b111011:
      return kAllowMips3Inst ? MipsInstId::kDsra : MipsInstId::kUnknown;
    case 0b111100:
      return kAllowMips3Inst ? MipsInstId::kDsll32 : MipsInstId::kUnknown;
    case 0b111110:
      return kAllowMips3Inst ? MipsInstId::kDsrl32 : MipsInstId::kUnknown;
    case 0b111111:
      return kAllowMips3Inst ? MipsInstId::kDsra32 : MipsInstId::kUnknown;
  }
  return MipsInstId::kUnknown;
}

MipsInstId DecodeCop(uint32_t opcode) {
  if (opcode & (1 << 25)) {
    // return MipsInstId::kCop;
  }

  switch (opcode >> 21) {
    case 0b01000001000:
    case 0b01000101000:
    case 0b01001001000:
    case 0b01001101000:
      if (kAllowMips3Inst) {
        uint8_t funct = (opcode >> 16) & 0b11111;
        switch (funct) {
          case 0b00000:
            return MipsInstId::kBcf;
          case 0b00001:
            return MipsInstId::kBct;
          case 0b00010:
            return MipsInstId::kBcfl;
          case 0b00011:
            return MipsInstId::kBctl;
        }
      }
      return MipsInstId::kUnknown;
    case 0b01000000000:
    case 0b01000100000:
    case 0b01001000000:
    case 0b01001100000:
      return MipsInstId::kMfc;
    case 0b01000000001:
    case 0b01000100001:
    case 0b01001000001:
    case 0b01001100001:
      return kAllowMips3Inst ? MipsInstId::kDmfc : MipsInstId::kUnknown;
    case 0b01000000010:
    case 0b01000100010:
    case 0b01001000010:
    case 0b01001100010:
      return MipsInstId::kCfc;
    case 0b01000000100:
    case 0b01000100100:
    case 0b01001000100:
    case 0b01001100100:
      return MipsInstId::kMtc;
    case 0b01000000101:
    case 0b01000100101:
    case 0b01001000101:
    case 0b01001100101:
      return kAllowMips3Inst ? MipsInstId::kDmtc : MipsInstId::kUnknown;
    case 0b01000000110:
    case 0b01000100110:
    case 0b01001000110:
    case 0b01001100110:
      return MipsInstId::kCtc;
  }

  return MipsInstId::kCop;
}

MipsInstId Decode(uint32_t opcode) {
  auto inst = MipsInst(opcode);
  switch (inst.op()) {
    case 0b000000:
      return DecodeTypeR(inst.GetRType());
    case 0b000001:
      return DecodeCondBranch(inst);
    case 0b000010:
      return MipsInstId::kJ;
    case 0b000011:
      return MipsInstId::kJal;
    case 0b000100:
      return MipsInstId::kBeq;
    case 0b000101:
      return MipsInstId::kBne;
    case 0b000110:
      return MipsInstId::kBlez;
    case 0b000111:
      return MipsInstId::kBgtz;
    case 0b001000:
      return MipsInstId::kAddi;
    case 0b001001:
      return MipsInstId::kAddiu;
    case 0b001010:
      return MipsInstId::kSlti;
    case 0b001011:
      return MipsInstId::kSltiu;
    case 0b001100:
      return MipsInstId::kAndi;
    case 0b001101:
      return MipsInstId::kOri;
    case 0b001110:
      return MipsInstId::kXori;
    case 0b001111:
      return MipsInstId::kLui;
    case 0b010000:
    case 0b010001:
    case 0b010010:
    case 0b010011:
      return DecodeCop(opcode);
    case 0b010100:
      return kAllowMips3Inst ? MipsInstId::kBeql : MipsInstId::kUnknown;
    case 0b010101:
      return kAllowMips3Inst ? MipsInstId::kBnel : MipsInstId::kUnknown;
    case 0b010110:
      return kAllowMips3Inst ? MipsInstId::kBlezl : MipsInstId::kUnknown;
    case 0b010111:
      return kAllowMips3Inst ? MipsInstId::kBgtzl : MipsInstId::kUnknown;
    case 0b011000:
      return kAllowMips3Inst ? MipsInstId::kDaddi : MipsInstId::kUnknown;
    case 0b011001:
      return kAllowMips3Inst ? MipsInstId::kDaddiu : MipsInstId::kUnknown;
    case 0b011010:
      return MipsInstId::kLdl;
    case 0b011011:
      return MipsInstId::kLdr;
    case 0b100000:
      return MipsInstId::kLb;
    case 0b100001:
      return MipsInstId::kLh;
    case 0b100010:
      return MipsInstId::kLwl;
    case 0b100011:
      return MipsInstId::kLw;
    case 0b100100:
      return MipsInstId::kLbu;
    case 0b100101:
      return MipsInstId::kLhu;
    case 0b100110:
      return MipsInstId::kLwr;
    case 0b100111:
      return kAllowMips3Inst ? MipsInstId::kLwu : MipsInstId::kUnknown;
    case 0b101000:
      return MipsInstId::kSb;
    case 0b101001:
      return MipsInstId::kSh;
    case 0b101010:
      return MipsInstId::kSwl;
    case 0b101011:
      return MipsInstId::kSw;
    case 0b101100:
      return kAllowMips3Inst ? MipsInstId::kSdl : MipsInstId::kUnknown;
    case 0b101101:
      return kAllowMips3Inst ? MipsInstId::kSdr : MipsInstId::kUnknown;
    case 0b101111:
      return kAllowMips3Inst ? MipsInstId::kCache : MipsInstId::kUnknown;
    case 0b101110:
      return MipsInstId::kSwr;
    case 0b110000:
      return kAllowMips3Inst ? MipsInstId::kLl : MipsInstId::kLwc;
    case 0b110001:
    case 0b110010:
      return MipsInstId::kLwc;
    case 0b110011:
      return kAllowMips3Inst ? MipsInstId::kUnknown : MipsInstId::kLwc;
    case 0b110100:
      return kAllowMips3Inst ? MipsInstId::kLld : MipsInstId::kUnknown;
    case 0b110101:
    case 0b110110:
      return kAllowMips3Inst ? MipsInstId::kLdc : MipsInstId::kUnknown;
    case 0b110111:
      return kAllowMips3Inst ? MipsInstId::kLd : MipsInstId::kUnknown;
    case 0b111000:
      return kAllowMips3Inst ? MipsInstId::kSc : MipsInstId::kSwc;
    case 0b111001:
    case 0b111010:
      return MipsInstId::kSwc;
    case 0b111011:
      return kAllowMips3Inst ? MipsInstId::kUnknown : MipsInstId::kSwc;
    case 0b111100:
      return kAllowMips3Inst ? MipsInstId::kScd : MipsInstId::kUnknown;
    case 0b111101:
    case 0b111110:
      return kAllowMips3Inst ? MipsInstId::kSdc : MipsInstId::kUnknown;
    case 0b111111:
      return MipsInstId::kSd;
  }
  return MipsInstId::kUnknown;
}

std::string GetInstName(uint32_t opcode) {
  switch (Decode(opcode)) {
    case MipsInstId::kAdd:
      return "add";
    case MipsInstId::kAddu:
      return "addu";
    case MipsInstId::kAddi:
      return "addi";
    case MipsInstId::kAddiu:
      return "addiu";
    case MipsInstId::kAnd:
      return "and";
    case MipsInstId::kAndi:
      return "andi";
    case MipsInstId::kDiv:
      return "div";
    case MipsInstId::kDivu:
      return "divu";
    case MipsInstId::kMult:
      return "mult";
    case MipsInstId::kMultu:
      return "multu";
    case MipsInstId::kNor:
      return "nor";
    case MipsInstId::kOr:
      return "or";
    case MipsInstId::kOri:
      return "ori";
    case MipsInstId::kSll:
      return "sll";
    case MipsInstId::kSllv:
      return "sllv";
    case MipsInstId::kSra:
      return "sra";
    case MipsInstId::kSrav:
      return "srav";
    case MipsInstId::kSrl:
      return "srl";
    case MipsInstId::kSrlv:
      return "srlv";
    case MipsInstId::kSub:
      return "sub";
    case MipsInstId::kSubu:
      return "subu";
    case MipsInstId::kXor:
      return "xor";
    case MipsInstId::kXori:
      return "xori";
    case MipsInstId::kLui:
      return "lui";
    case MipsInstId::kSlt:
      return "slt";
    case MipsInstId::kSltu:
      return "sltu";
    case MipsInstId::kSlti:
      return "slti";
    case MipsInstId::kSltiu:
      return "sltiu";
    case MipsInstId::kBeq:
      return "beq";
    case MipsInstId::kBne:
      return "bne";
    case MipsInstId::kBgtz:
      return "bgtz";
    case MipsInstId::kBlez:
      return "blez";
    case MipsInstId::kBgez:
      return "bgez";
    case MipsInstId::kBgezal:
      return "bgezal";
    case MipsInstId::kBltz:
      return "bltz";
    case MipsInstId::kBltzal:
      return "bltzal";
    case MipsInstId::kJ:
      return "j";
    case MipsInstId::kJal:
      return "jal";
    case MipsInstId::kJr:
      return "jr";
    case MipsInstId::kJalr:
      return "jalr";
    case MipsInstId::kSyscall:
      return "syscall";
    case MipsInstId::kBreak:
      return "break";
    case MipsInstId::kLb:
      return "lb";
    case MipsInstId::kLbu:
      return "lbu";
    case MipsInstId::kLh:
      return "lh";
    case MipsInstId::kLhu:
      return "lhu";
    case MipsInstId::kLw:
      return "lw";
    case MipsInstId::kLwl:
      return "lwl";
    case MipsInstId::kLwr:
      return "lwr";
    case MipsInstId::kLwc:
      return "lwc";
    case MipsInstId::kSb:
      return "sb";
    case MipsInstId::kSh:
      return "sh";
    case MipsInstId::kSw:
      return "sw";
    case MipsInstId::kSwl:
      return "swl";
    case MipsInstId::kSwr:
      return "swr";
    case MipsInstId::kSwc:
      return "swc";
    case MipsInstId::kMfhi:
      return "mfhi";
    case MipsInstId::kMflo:
      return "mflo";
    case MipsInstId::kMthi:
      return "mthi";
    case MipsInstId::kMtlo:
      return "mtlo";
    case MipsInstId::kCop:
      return "cop";
    case MipsInstId::kMfc:
      return "mfc";
    case MipsInstId::kCfc:
      return "cfc";
    case MipsInstId::kMtc:
      return "mtc";
    case MipsInstId::kCtc:
      return "ctc";
    case MipsInstId::kNop:
      return "nop";
    case MipsInstId::kBcf:
      return "bcf";
    case MipsInstId::kBcfl:
      return "bcfl";
    case MipsInstId::kBct:
      return "bct";
    case MipsInstId::kBctl:
      return "bctl";
    case MipsInstId::kBeql:
      return "beql";
    case MipsInstId::kBnel:
      return "bnel";
    case MipsInstId::kBgezl:
      return "bgezl";
    case MipsInstId::kBgezall:
      return "bgezall";
    case MipsInstId::kBgtzl:
      return "bgtzl";
    case MipsInstId::kBlezl:
      return "blezl";
    case MipsInstId::kBltzl:
      return "bltzl";
    case MipsInstId::kBltzall:
      return "bltzall";
    case MipsInstId::kCache:
      return "cache";
    case MipsInstId::kDadd:
      return "dadd";
    case MipsInstId::kDaddu:
      return "daddu";
    case MipsInstId::kDaddi:
      return "daddi";
    case MipsInstId::kDaddiu:
      return "daddiu";
    case MipsInstId::kDsub:
      return "dsub";
    case MipsInstId::kDsubu:
      return "dsubu";
    case MipsInstId::kDmult:
      return "dmult";
    case MipsInstId::kDmultu:
      return "dmultu";
    case MipsInstId::kDdiv:
      return "ddiv";
    case MipsInstId::kDdivu:
      return "ddivu";
    case MipsInstId::kDsll:
      return "dsll";
    case MipsInstId::kDsll32:
      return "dsll32";
    case MipsInstId::kDsllv:
      return "dsllv";
    case MipsInstId::kDsra:
      return "dsra";
    case MipsInstId::kDsra32:
      return "dsra32";
    case MipsInstId::kDsrav:
      return "dsrav";
    case MipsInstId::kDsrl:
      return "dsrl";
    case MipsInstId::kDsrl32:
      return "dsrl32";
    case MipsInstId::kDsrlv:
      return "dsrlv";
    case MipsInstId::kDmfc:
      return "dmfc";
    case MipsInstId::kDmtc:
      return "dmtc";
    case MipsInstId::kLd:
      return "ld";
    case MipsInstId::kLdc:
      return "ldc";
    case MipsInstId::kLdl:
      return "ldl";
    case MipsInstId::kLdr:
      return "ldr";
    case MipsInstId::kLwu:
      return "lwu";
    case MipsInstId::kSd:
      return "sd";
    case MipsInstId::kSdc:
      return "sdc";
    case MipsInstId::kSdl:
      return "sdl";
    case MipsInstId::kSdr:
      return "sdr";
    case MipsInstId::kLl:
      return "ll";
    case MipsInstId::kLld:
      return "lld";
    case MipsInstId::kSc:
      return "sc";
    case MipsInstId::kScd:
      return "scd";
    case MipsInstId::kSync:
      return "sync";
    case MipsInstId::kUnknown:
      return "unknown";
  }

  return "error";
}

bool IsInstBranch(uint32_t opcode) {
  switch (Decode(opcode)) {
    case MipsInstId::kBeq:
    case MipsInstId::kBne:
    case MipsInstId::kBgtz:
    case MipsInstId::kBlez:
    case MipsInstId::kBgez:
    case MipsInstId::kBgezal:
    case MipsInstId::kBltz:
    case MipsInstId::kBltzal:
    case MipsInstId::kJ:
    case MipsInstId::kJal:
    case MipsInstId::kJr:
    case MipsInstId::kJalr:
    case MipsInstId::kSyscall:
    case MipsInstId::kBreak:
    case MipsInstId::kBcf:
    case MipsInstId::kBcfl:
    case MipsInstId::kBct:
    case MipsInstId::kBctl:
    case MipsInstId::kBeql:
    case MipsInstId::kBnel:
    case MipsInstId::kBgezl:
    case MipsInstId::kBgezall:
    case MipsInstId::kBgtzl:
    case MipsInstId::kBlezl:
    case MipsInstId::kBltzl:
    case MipsInstId::kBltzall:
      return true;
    default:
      return false;
  }
  return false;
}

bool DoesInstHaveDelaySlot(uint32_t opcode) {
  switch (Decode(opcode)) {
    case MipsInstId::kBeq:
    case MipsInstId::kBne:
    case MipsInstId::kBgtz:
    case MipsInstId::kBlez:
    case MipsInstId::kBgez:
    case MipsInstId::kBgezal:
    case MipsInstId::kBltz:
    case MipsInstId::kBltzal:
    case MipsInstId::kJ:
    case MipsInstId::kJal:
    case MipsInstId::kJr:
    case MipsInstId::kJalr:
    case MipsInstId::kBcf:
    case MipsInstId::kBcfl:
    case MipsInstId::kBct:
    case MipsInstId::kBctl:
    case MipsInstId::kBeql:
    case MipsInstId::kBnel:
    case MipsInstId::kBgezl:
    case MipsInstId::kBgezall:
    case MipsInstId::kBgtzl:
    case MipsInstId::kBlezl:
    case MipsInstId::kBltzl:
    case MipsInstId::kBltzall:
      return true;
    default:
      return false;
  }
  return false;
}

const char* GetMipsRegName(int index) {
  static const char* kMipsGprName[32] = {
      "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
      "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
      "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
      "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
  return kMipsGprName[index];
}

std::string DisassembleRType(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  RTypeInst inst(opcode);

  std::string rd_name = GetMipsRegName(inst.rd());
  std::string rs_name = GetMipsRegName(inst.rs());
  std::string rt_name = GetMipsRegName(inst.rt());

  std::string result;
  if (inst.shamt() != 0) {
    result = fmt::format("{} {}, {}, {}", name, rd_name, rt_name, inst.shamt());
  } else {
    result = fmt::format("{} {}, {}, {}", name, rd_name, rs_name, rt_name);
  }
  return result;
}

std::string DisassembleJType(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  JTypeInst inst(opcode);

  std::string result = fmt::format("{} {:08X}", name, inst.address());
  return result;
}

std::string DisassembleImmArithmetic(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  ITypeInst inst(opcode);

  std::string rs_name = GetMipsRegName(inst.rs());
  std::string rt_name = GetMipsRegName(inst.rt());

  std::string result = fmt::format("{} {}, {}, {}", name, rt_name, rs_name, inst.imm());
  return result;
}

std::string DisassembleLui(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  ITypeInst inst(opcode);

  std::string rt_name = GetMipsRegName(inst.rt());

  std::string result = fmt::format("{} {}, {}", name, rt_name, inst.imm());
  return result;
}

std::string DisassembleCondBranchWithRt(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  ITypeInst inst(opcode);

  std::string rs_name = GetMipsRegName(inst.rs());
  std::string rt_name = GetMipsRegName(inst.rt());

  std::string result = fmt::format("{} {}, {}, {}", name, rs_name, rt_name, inst.imm());
  return result;
}

std::string DisassembleCondBranchWithoutRt(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  ITypeInst inst(opcode);

  std::string rs_name = GetMipsRegName(inst.rs());

  std::string result = fmt::format("{} {}, {}", name, rs_name, inst.imm());
  return result;
}

std::string DisassembleMemoryAccess(uint32_t opcode, uint64_t address) {
  std::string name = GetInstName(opcode);
  ITypeInst inst(opcode);

  std::string rs_name = GetMipsRegName(inst.rs());
  std::string rt_name = GetMipsRegName(inst.rt());

  std::string result = fmt::format("{} {}, {}({})", name, rt_name, inst.imm(), rs_name);
  return result;
}

std::string MipsInst::Disassemble(uint64_t address) {
  switch (Decode(raw_)) {
    case MipsInstId::kSll:
    case MipsInstId::kSrl:
    case MipsInstId::kSra:
    case MipsInstId::kSllv:
    case MipsInstId::kSrav:
    case MipsInstId::kJr:
    case MipsInstId::kJalr:
    case MipsInstId::kSyscall:
    case MipsInstId::kBreak:
    case MipsInstId::kMfhi:
    case MipsInstId::kMthi:
    case MipsInstId::kMflo:
    case MipsInstId::kMtlo:
    case MipsInstId::kMult:
    case MipsInstId::kMultu:
    case MipsInstId::kDiv:
    case MipsInstId::kDivu:
    case MipsInstId::kAdd:
    case MipsInstId::kAddu:
    case MipsInstId::kSub:
    case MipsInstId::kSubu:
    case MipsInstId::kAnd:
    case MipsInstId::kOr:
    case MipsInstId::kXor:
    case MipsInstId::kNor:
    case MipsInstId::kSlt:
    case MipsInstId::kSltu:
      return DisassembleRType(raw_, address);
    case MipsInstId::kJ:
    case MipsInstId::kJal:
      return DisassembleJType(raw_, address);
    case MipsInstId::kAddi:
    case MipsInstId::kAddiu:
    case MipsInstId::kAndi:
    case MipsInstId::kOri:
    case MipsInstId::kXori:
    case MipsInstId::kSlti:
    case MipsInstId::kSltiu:
      return DisassembleImmArithmetic(raw_, address);
    case MipsInstId::kBeq:
    case MipsInstId::kBne:
      return DisassembleCondBranchWithRt(raw_, address);
    case MipsInstId::kBgez:
    case MipsInstId::kBgtz:
    case MipsInstId::kBlez:
    case MipsInstId::kBltz:
      return DisassembleCondBranchWithoutRt(raw_, address);
    case MipsInstId::kLb:
    case MipsInstId::kLbu:
    case MipsInstId::kLh:
    case MipsInstId::kLhu:
    case MipsInstId::kLw:
    case MipsInstId::kLwl:
    case MipsInstId::kLwr:
    case MipsInstId::kLl:
    case MipsInstId::kLld:
    case MipsInstId::kSb:
    case MipsInstId::kSh:
    case MipsInstId::kSw:
    case MipsInstId::kSc:
    case MipsInstId::kScd:
      return DisassembleMemoryAccess(raw_, address);
    case MipsInstId::kLui:
      return DisassembleLui(raw_, address);
    case MipsInstId::kBeql:
    case MipsInstId::kBnel:
      return DisassembleCondBranchWithRt(raw_, address);
    case MipsInstId::kSync:
      return std::string("sync");
    case MipsInstId::kNop:
      return std::string("nop");
  }
  return fmt::format("Decode error {:08X} ({})", raw_, GetInstName(raw_));
}
