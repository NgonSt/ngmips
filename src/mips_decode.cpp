#include "mips_decode.h"

#include <fmt/format.h>

MipsInstId DecodeCondBranch(MipsInst inst) {
  uint32_t opcode = inst.GetOpcodeRaw();
  bool is_bgez = opcode & (1 << 16);
  bool is_linked = ((opcode >> 17) & 0xF) == 0x8;
  if (is_bgez) {
    return is_linked ? MipsInstId::kBgezal : MipsInstId::kBgez;
  }

  return is_linked ? MipsInstId::kBltzal : MipsInstId::kBltz;
}

MipsInstId DecodeTypeR(RTypeInst inst) {
  if (inst.funct() == 0 && inst.op() == 0 && inst.shamt() == 0 && inst.rd() == 0) {
    return MipsInstId::kNop;
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
    case 0b011000:
      return MipsInstId::kMult;
    case 0b011001:
      return MipsInstId::kMultu;
    case 0b011010:
      return MipsInstId::kDiv;
    case 0b011011:
      return MipsInstId::kDivu;
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
  }
  return MipsInstId::kUnknown;
}

MipsInstId DecodeCop(uint32_t opcode) {
  if (opcode & (1 << 25)) {
    return MipsInstId::kCop;
  }

  switch (opcode >> 21) {
    case 0b01000000000:
    case 0b01000100000:
    case 0b01001000000:
    case 0b01001100000:
      return MipsInstId::kMfc;
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
    case 0b01000000110:
    case 0b01000100110:
    case 0b01001000110:
    case 0b01001100110:
      return MipsInstId::kCtc;
  }

  return MipsInstId::kUnknown;
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
    case 0b101000:
      return MipsInstId::kSb;
    case 0b101001:
      return MipsInstId::kSh;
    case 0b101010:
      return MipsInstId::kSwl;
    case 0b101011:
      return MipsInstId::kSw;
    case 0b101110:
      return MipsInstId::kSwr;
    case 0b110000:
    case 0b110001:
    case 0b110010:
    case 0b110011:
      return MipsInstId::kLwc;
    case 0b111000:
    case 0b111001:
    case 0b111010:
    case 0b111011:
      return MipsInstId::kSwc;
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
    case MipsInstId::kSb:
    case MipsInstId::kSh:
    case MipsInstId::kSw:
      return DisassembleMemoryAccess(raw_, address);
    case MipsInstId::kLui:
      return DisassembleLui(raw_, address);
    case MipsInstId::kNop:
      return std::string("nop");
  }
  return fmt::format("Decode error {:08X} ({})", raw_, GetInstName(raw_));
}
