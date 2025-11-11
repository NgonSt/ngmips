#include "mips_base.h"

#include <fmt/format.h>

#include "panic.h"
#include "mips_cache.h"
#include "mips_cop0.h"
#include "mips_cop_dummy.h"
#include "mips_decode.h"

namespace {

const bool kLogCpu = false;
const bool kLogKernel = false;
const bool kPanicOnUnalignedJump = false;
const bool kLogMipsState = false;
const int kMipsCpi = 0x200;

const bool kLogNullWrites = true;
const bool kPanicOnNullWrites = false;
const uint32_t kPsxKnownNullWritePc[] = {
    0x00000F00, 0xBFC04E90, 0xBFC05164, 0x800585E4,
    0x80059C50, 0x80058788};

int inst_log_num = 0;

bool get_overflow_add_i32(uint32_t result, uint32_t lhs, uint32_t rhs) {
  return ((lhs ^ result) & (rhs ^ result)) & (1 << 31);
}

bool get_overflow_add_i64(uint64_t result, uint64_t lhs, uint64_t rhs) {
  return ((lhs ^ result) & (rhs ^ result)) & (1ULL << 63);
}

bool get_overflow_sub_i32(uint32_t result, uint32_t lhs, uint32_t rhs) {
  return ((lhs ^ rhs) & (lhs ^ result)) & (1 << 31);
}

bool get_overflow_sub_i64(uint64_t result, uint64_t lhs, uint64_t rhs) {
  return ((lhs ^ rhs) & (lhs ^ result)) & (1ULL << 63);
}

int64_t sext_i8_to_i64(int8_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int8_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int64_t sext_i16_to_i64(int16_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int16_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int64_t sext_i32_to_i64(int32_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int32_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int128_t sext_i64_to_i128(int64_t value) {
  int128_t result = value;
  int shamt = (sizeof(int128_t) - sizeof(int64_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int32_t sext_itype_imm_i32(ITypeInst inst) {
  int32_t imm = inst.imm();
  int shamt = 16;
  imm <<= shamt;
  imm >>= shamt;
  return imm;
}

int32_t sext_itype_imm_branch(ITypeInst inst) {
  int32_t imm = inst.imm();
  imm <<= 16;
  imm >>= 14;
  return imm;
}

}  // namespace

MipsBase::MipsBase() {
  for (int i = 0; i < 32; i++) {
    gpr_[i] = 0;
    fpr_[i] = 0.;
  }
  hi_ = 0;
  lo_ = 0;
  pc_ = 0;
  next_pc_ = 0;
  fpcr_r0_ = 0;
  fpcr_r31_ = 0;
  llbit_ = false;

  cycle_spent_ = 0;
  has_branch_delay_ = false;
  branch_delay_dst_ = 0;

  mips_log_index_ = 0;

  cop_[0] = std::make_shared<MipsCop0>();
  cop_[1] = std::make_shared<MipsCopDummy>();
  cop_[2] = std::make_shared<MipsCopDummy>();
  cop_[3] = std::make_shared<MipsCopDummy>();
}

void MipsBase::Reset() {
  for (int i = 0; i < 32; i++) {
    gpr_[i] = 0;
    fpr_[i] = 0.;
  }
  hi_ = 0;
  lo_ = 0;
  pc_ = 0;
  next_pc_ = 0;
  fpcr_r0_ = 0;
  fpcr_r31_ = 0;
  llbit_ = false;

  cycle_spent_total_ = 0;
  cpi_counter_ = 0;

  has_branch_delay_ = false;
  branch_delay_dst_ = 0;

  delayed_load_op_.is_active_ = false;

  cache_.Reset();

  mips_log_index_ = 0;

  for (int i = 0; i < 4; i++) {
    cop_[i]->Reset();
  }
}

int MipsBase::Run(int cycle) {
  if (kUseCachedInterp) {
    return RunCached(cycle);
  }
  cycle_spent_ = 0;
  if (!kLazyInterruptPolling) {
    CheckInterrupt();
  }
  while (cycle_spent_ < cycle) {
    RunInst();
  }
  return cycle_spent_;
}

int MipsBase::RunCached(int cycle) {
  cycle_spent_ = 0;
  while (cycle_spent_ < cycle) {
    CheckInterrupt();
    const MipsCacheBlock* block = cache_.GetBlock(pc_);
    if (block == nullptr) {
      OnNewBlock(pc_);
      block = cache_.GetBlock(pc_);
      if (block == nullptr) {
        PANIC("Block creation failed");
      }
    }

    const int length = block->length_;
    const MipsCacheEntry* entries = block->entries_;

    for (int i = 0; i < length; i++) {
      const uint32_t opcode = entries[i].opcode_;
      const uint32_t pc = entries[i].address_;
      const inst_ptr_t fp = entries[i].func_;

      // Verify we're at the expected address (catch execution flow mismatches)
      // if (i > 0 && pc_ != pc) {
      //  fmt::print("PC mismatch in cached block: expected {:08X}, got {:08X}\n", pc, (uint32_t)pc_);
      //  break;
      // }

      MipsLog log;
      if (kLogCpu || kLogMipsState) {
        log.pc_ = pc_;
        log.inst_ = opcode;
        for (int i = 0; i < 32; i++) {
          log.gpr_[i] = ReadGpr32(i);
        }
      }

      if (kLogMipsState) {
        mips_log_[mips_log_index_] = log;
        mips_log_index_++;
        if (mips_log_index_ >= kMipsInstLogCount) {
          mips_log_index_ = 0;
        }
      }

      if (kLogCpu) {
        fmt::print("{}\n", log.ToString());
      }

      if (has_branch_delay_) {
        next_pc_ = branch_delay_dst_;
        has_branch_delay_ = false;
      } else {
        next_pc_ = pc_ + 4;
      }

      (this->*fp)(opcode);
      ExecuteDelayedLoad();

      pc_ = next_pc_ & 0xFFFFFFFF;
    }

    CheckHook();

    cpi_counter_ += block->cycle_;
    int cpi_integer = cpi_counter_ >> 8;
    cpi_counter_ &= 0xFF;
    cycle_spent_ += cpi_integer;
    cycle_spent_total_ += cpi_integer;
  }

  cache_.ExecuteCacheClear();
  return cycle_spent_;
}

void MipsBase::ConnectCop(std::shared_ptr<MipsCopBase> cop, int idx) {
  cop_[idx] = cop;
}

void MipsBase::ConnectBus(std::shared_ptr<BusBase> bus) {
  bus_ = bus;
}

void MipsBase::SetPc(uint64_t pc) {
  pc_ = pc;
  next_pc_ = pc + 4;
}

uint64_t MipsBase::GetPc() {
  return pc_;
}

void MipsBase::SetGpr(int idx, uint64_t value) {
  if (idx == 0) {
    return;
  }
  gpr_[idx] = value;
}

void MipsBase::CheckInterrupt() {
  if (kLazyInterruptPolling) {
    // If we enable lazy polling, this function will be called in the middle of instruction
    // Solution: we run instruction clean-up manually (aka. a dirty hack)
    bool intr = (cop_[0]->Read32Internal(12) & 1) && bus_->GetInterrupt();
    if (intr) {
      pc_ = next_pc_;
      cpi_counter_ += kMipsCpi;
      int cpi_integer = cpi_counter_ >> 8;
      cpi_counter_ &= 0xFF;
      cycle_spent_ += cpi_integer;
      cycle_spent_total_ += cpi_integer;

      TriggerException(ExceptionCause::kInt);
    }
  } else {
    if (cop_[0]->Read32Internal(12) & 1) {
      if (bus_->GetInterrupt()) {
        TriggerException(ExceptionCause::kInt);
      }
    }
  }
}

void MipsBase::RunInst() {
  if (inst_log_num > 0) {
    inst_log_num--;
    if (inst_log_num == 0) {
      DumpProcessorLog();
      PANIC("AAA");
    }
  }

  uint32_t opcode = Fetch(pc_);
  bool is_this_inst_bd = has_branch_delay_;
  if (has_branch_delay_) {
    next_pc_ = branch_delay_dst_;
    has_branch_delay_ = false;
  } else {
    next_pc_ = pc_ + 4;
  }

  MipsLog log;
  if (kLogCpu || kLogMipsState) {
    log.pc_ = pc_;
    log.inst_ = opcode;
    for (int i = 0; i < 32; i++) {
      log.gpr_[i] = ReadGpr32(i);
    }
  }

  if (kLogMipsState) {
    mips_log_[mips_log_index_] = log;
    mips_log_index_++;
    if (mips_log_index_ >= kMipsInstLogCount) {
      mips_log_index_ = 0;
    }
  }

  if (kLogCpu) {
    fmt::print("{}\n", log.ToString());
  }

  inst_ptr_t fp = GetInstFuncPtr(opcode);
  (this->*fp)(opcode);

  ExecuteDelayedLoad();

  pc_ = next_pc_;
  cpi_counter_ += kMipsCpi;
  int cpi_integer = cpi_counter_ >> 8;
  cpi_counter_ &= 0xFF;
  cycle_spent_ += cpi_integer;
  cycle_spent_total_ += cpi_integer;

  if (is_this_inst_bd) {
    CheckHook();
  }
}

inst_ptr_t MipsBase::GetInstFuncPtr(uint32_t opcode) {
  switch (Decode(opcode)) {
    case MipsInstId::kAdd:
      return &MipsBase::InstAdd;
    case MipsInstId::kAddu:
      return &MipsBase::InstAddu;
    case MipsInstId::kAddi:
      return &MipsBase::InstAddi;
    case MipsInstId::kAddiu:
      return &MipsBase::InstAddiu;
    case MipsInstId::kAnd:
      return &MipsBase::InstAnd;
    case MipsInstId::kAndi:
      return &MipsBase::InstAndi;
    case MipsInstId::kDiv:
      return &MipsBase::InstDiv;
    case MipsInstId::kDivu:
      return &MipsBase::InstDivu;
    case MipsInstId::kMult:
      return &MipsBase::InstMult;
    case MipsInstId::kMultu:
      return &MipsBase::InstMultu;
    case MipsInstId::kNor:
      return &MipsBase::InstNor;
    case MipsInstId::kOr:
      return &MipsBase::InstOr;
    case MipsInstId::kOri:
      return &MipsBase::InstOri;
    case MipsInstId::kSll:
      return &MipsBase::InstSll;
    case MipsInstId::kSllv:
      return &MipsBase::InstSllv;
    case MipsInstId::kSra:
      return &MipsBase::InstSra;
    case MipsInstId::kSrav:
      return &MipsBase::InstSrav;
    case MipsInstId::kSrl:
      return &MipsBase::InstSrl;
    case MipsInstId::kSrlv:
      return &MipsBase::InstSrlv;
    case MipsInstId::kSub:
      return &MipsBase::InstSub;
    case MipsInstId::kSubu:
      return &MipsBase::InstSubu;
    case MipsInstId::kXor:
      return &MipsBase::InstXor;
    case MipsInstId::kXori:
      return &MipsBase::InstXori;
    case MipsInstId::kLui:
      return &MipsBase::InstLui;
    case MipsInstId::kSlt:
      return &MipsBase::InstSlt;
    case MipsInstId::kSltu:
      return &MipsBase::InstSltu;
    case MipsInstId::kSlti:
      return &MipsBase::InstSlti;
    case MipsInstId::kSltiu:
      return &MipsBase::InstSltiu;
    case MipsInstId::kBeq:
      return &MipsBase::InstBeq;
    case MipsInstId::kBne:
      return &MipsBase::InstBne;
    case MipsInstId::kBgtz:
      return &MipsBase::InstBgtz;
    case MipsInstId::kBlez:
      return &MipsBase::InstBlez;
    case MipsInstId::kBgez:
      return &MipsBase::InstBgez;
    case MipsInstId::kBgezal:
      return &MipsBase::InstBgezal;
    case MipsInstId::kBltz:
      return &MipsBase::InstBltz;
    case MipsInstId::kBltzal:
      return &MipsBase::InstBltzal;
    case MipsInstId::kJ:
      return &MipsBase::InstJ;
    case MipsInstId::kJal:
      return &MipsBase::InstJal;
    case MipsInstId::kJr:
      return &MipsBase::InstJr;
    case MipsInstId::kJalr:
      return &MipsBase::InstJalr;
    case MipsInstId::kSyscall:
      return &MipsBase::InstSyscall;
    case MipsInstId::kBreak:
      return &MipsBase::InstBreak;
    case MipsInstId::kLb:
      return &MipsBase::InstLb;
    case MipsInstId::kLbu:
      return &MipsBase::InstLbu;
    case MipsInstId::kLh:
      return &MipsBase::InstLh;
    case MipsInstId::kLhu:
      return &MipsBase::InstLhu;
    case MipsInstId::kLw:
      return &MipsBase::InstLw;
    case MipsInstId::kLwl:
      return &MipsBase::InstLwl;
    case MipsInstId::kLwr:
      return &MipsBase::InstLwr;
    case MipsInstId::kLwc:
      return &MipsBase::InstLwc;
    case MipsInstId::kSb:
      return &MipsBase::InstSb;
    case MipsInstId::kSh:
      return &MipsBase::InstSh;
    case MipsInstId::kSw:
      return &MipsBase::InstSw;
    case MipsInstId::kSwl:
      return &MipsBase::InstSwl;
    case MipsInstId::kSwr:
      return &MipsBase::InstSwr;
    case MipsInstId::kSwc:
      return &MipsBase::InstSwc;
    case MipsInstId::kMfhi:
      return &MipsBase::InstMfhi;
    case MipsInstId::kMflo:
      return &MipsBase::InstMflo;
    case MipsInstId::kMthi:
      return &MipsBase::InstMthi;
    case MipsInstId::kMtlo:
      return &MipsBase::InstMtlo;
    case MipsInstId::kCop:
      return &MipsBase::InstCop;
    case MipsInstId::kMfc:
      return &MipsBase::InstMfc;
    case MipsInstId::kCfc:
      return &MipsBase::InstCfc;
    case MipsInstId::kMtc:
      return &MipsBase::InstMtc;
    case MipsInstId::kCtc:
      return &MipsBase::InstCtc;
    case MipsInstId::kNop:
      return &MipsBase::InstNop;
    case MipsInstId::kUnknown:
      return &MipsBase::InstUnknown;
  }
  return &MipsBase::InstUnknown;
}

uint32_t MipsBase::ReadGpr32(int idx) {
  return gpr_[idx];
}

void MipsBase::WriteGpr32(int idx, uint32_t value) {
  if (idx == 0) {
    return;
  }
  if (delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = value;
}

uint64_t MipsBase::ReadGpr64(int idx) {
  return gpr_[idx] & 0xFFFFFFFF;
}

void MipsBase::WriteGpr32Sext(int idx, int32_t value) {
  if (idx == 0) {
    return;
  }
  if (delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = (int64_t)value;
}

void MipsBase::WriteGpr64(int idx, uint64_t value) {
  if (idx == 0) {
    return;
  }
  if (delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = value;
}

void MipsBase::JumpRel(int32_t offset) {
  has_branch_delay_ = true;
  branch_delay_dst_ = pc_ + 4 + offset;

  if (branch_delay_dst_ == 0) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

void MipsBase::LinkForJump(int dst_reg) {
  WriteGpr64(dst_reg, pc_ + 8);
}

void MipsBase::Jump32(uint32_t dst) {
  has_branch_delay_ = true;
  branch_delay_dst_ = dst;

  if (dst == 0) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

void MipsBase::Jump64(uint64_t dst) {
  has_branch_delay_ = true;
  branch_delay_dst_ = dst;

  if (dst == 0) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

void MipsBase::QueueDelayedLoad(int dst, uint64_t value) {
  if (delayed_load_op_.is_active_) {
    ExecuteDelayedLoad();
    if (delayed_load_op_.is_active_) {
      PANIC("Consecutive loads not handled");
    }
  }
  /* Load delay is only in MIPS I. So only lower 32-bit of the passed value is relevant */
  delayed_load_op_.is_active_ = true;
  delayed_load_op_.delay_counter_ = 0;
  delayed_load_op_.dst_ = dst;
  delayed_load_op_.cop_id_ = -1;
  delayed_load_op_.value_ = value;
}

void MipsBase::QueueDelayedCopLoad(int cop_id, int dst, uint64_t value) {
  if (delayed_load_op_.is_active_) {
    ExecuteDelayedLoad();
    if (delayed_load_op_.is_active_) {
      PANIC("Consecutive loads not handled");
    }
  }
  delayed_load_op_.is_active_ = true;
  delayed_load_op_.delay_counter_ = 0;
  delayed_load_op_.dst_ = dst;
  delayed_load_op_.cop_id_ = cop_id;
  delayed_load_op_.value_ = value;
}

void MipsBase::ExecuteDelayedLoad() {
  if (!delayed_load_op_.is_active_) {
    return;
  }

  delayed_load_op_.delay_counter_++;
  if (delayed_load_op_.delay_counter_ != 2) {
    return;
  }

  if (delayed_load_op_.cop_id_ < 0) {
    WriteGpr32(delayed_load_op_.dst_, delayed_load_op_.value_);
  } else {
    cop_[delayed_load_op_.cop_id_]->Write32(delayed_load_op_.dst_, delayed_load_op_.value_);
    if (kLazyInterruptPolling) {
      if (delayed_load_op_.cop_id_ == 0) {
        CheckInterrupt();
      }
    }
  }

  delayed_load_op_.is_active_ = false;
  delayed_load_op_.delay_counter_ = 0;
}

void MipsBase::TriggerException(ExceptionCause cause) {
  if (false) {
    fmt::print("Exception: {}\n", static_cast<int>(cause));
    fmt::print("Cycle: {}\n", cycle_spent_total_);
  }

  uint32_t opcode_next = Fetch(pc_);
  if (((opcode_next >> 24) & 0xFE) == 0x4A) {
    // Interrupt on GTE instruction
    pc_ -= 4;
  }

  uint64_t epc = pc_;
  bool bd = false;
  if (has_branch_delay_) {
    has_branch_delay_ = false;
    bd = true;
    epc -= 4;
    uint32_t opcode = Fetch(epc);
    if (!DoesInstHaveDelaySlot(opcode)) {
      PANIC("NON BRANCH ON BD");
    }
  }
  uint32_t cause_reg_old = cop_[0]->Read32Internal(13);
  uint32_t cause_reg_new = 0;
  // Update excode
  cause_reg_new |= static_cast<uint32_t>(cause) << 2;
  // TODO: BT bit
  cause_reg_new |= bus_->GetInterrupt() ? (1 << 10) : 0;
  cause_reg_new |= (cause_reg_old & 0x0300);
  cause_reg_new |= bd ? (1 << 31) : 0;

  cop_[0]->Write32Internal(13, cause_reg_new);
  cop_[0]->Write64Internal(14, epc);

  uint32_t sr_reg = cop_[0]->Read32Internal(12);
  uint32_t sr_mode = sr_reg & 0x3F;
  sr_reg &= ~0x3F;
  sr_reg |= (sr_mode << 2) & 0x3F;
  cop_[0]->Write32Internal(12, sr_reg);

  // NOTE: Is it really working?
  uint32_t vec_address_general = (sr_reg & (1 << 22)) ? 0xBFC00180 : 0x80000080;
  pc_ = vec_address_general;
  next_pc_ = pc_;
}

void MipsBase::CheckHook() {
  switch (pc_ & 0xFFFFFFFF) {
    case 0xA0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel) {
        fmt::print("Kernel call A: {:02X}\n", func_id);
      }
      if (func_id == 0x3C) {
        char c = ReadGpr32(4);
        bus_->Store8(0x1F802080, c);
      }
      break;
    }
    case 0xB0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel && func_id != 0x0B) {
        fmt::print("Kernel call B: {:02X}\n", func_id);
      }
      if (func_id == 0x3D) {
        char c = ReadGpr32(4);
        bus_->Store8(0x1F802080, c);
      }
      break;
    }
    case 0xC0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel) {
        fmt::print("Kernel call C: {:02X}\n", func_id);
      }
      break;
    }
  }
}

void MipsBase::DumpProcessorLog() {
  if (kLogMipsState) {
    fmt::print("===== Processor log dump =====\n");
    for (int i = 0; i < kMipsInstLogCount; i++) {
      int index = (mips_log_index_ + i) % kMipsInstLogCount;
      fmt::print("{}\n", mips_log_[index].ToString());
    }
  }
}

uint32_t MipsBase::Fetch(uint64_t address) {
  if (false) {
    bool is_pc_in_rom = (address >= 0x1FC00000 && address < 0x1FC80000) || (address >= 0x9FC00000 && address < 0x9FC80000) || (address >= 0xBFC00000 && address < 0xBFC80000);
    bool is_pc_in_ram = (address < 0x00200000) || (address >= 0x80000000 && address < 0x80200000) || (address >= 0xA0000000 && address < 0xA0200000);
    if (!is_pc_in_rom && !is_pc_in_ram) {
      fmt::print("PC OoB: {:08X}\n", address);
      DumpProcessorLog();
      PANIC("PC OoB: {:08X}\n", address);
    }
  }

  LoadResult32 result = bus_->Load32(address);
  if (!result.has_value) {
    fmt::print("PC OoB: {:08X}\n", address);
    DumpProcessorLog();
    PANIC("PC OoB: {:08X}\n", address);
  }
  return result.value;
}

LoadResult8 MipsBase::Load8(uint64_t address) {
  LoadResult8 result = bus_->Load8(address);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
  }
  return result;
}

LoadResult16 MipsBase::Load16(uint64_t address) {
  LoadResult16 result = bus_->Load16(address);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
  }
  return result;
}

LoadResult32 MipsBase::Load32(uint64_t address) {
  LoadResult32 result = bus_->Load32(address);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
  }
  return result;
}

LoadResult64 MipsBase::Load64(uint64_t address) {
  return bus_->Load64(address);
}

void MipsBase::Store8(uint64_t address, uint8_t value) {
  if (cop_[0]->Read32Internal(12) & (1 << 16)) {
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:02X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  bus_->Store8(address, value);
  if (kUseCachedInterp) {
    // InvalidateBlock(address);
  }
}

void MipsBase::Store16(uint64_t address, uint16_t value) {
  if (cop_[0]->Read32Internal(12) & (1 << 16)) {
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:04X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  bus_->Store16(address, value);
  if (kUseCachedInterp) {
    // InvalidateBlock(address);
  }
}

void MipsBase::Store32(uint64_t address, uint32_t value) {
  if (cop_[0]->Read32Internal(12) & (1 << 16)) {
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:08X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  bus_->Store32(address, value);
  if (kUseCachedInterp) {
    // InvalidateBlock(address);
  }
}

void MipsBase::Store64(uint64_t address, uint64_t value) {
  if (cop_[0]->Read32Internal(12) & (1 << 16)) {
    return;
  }

  bus_->Store64(address, value);
  if (kUseCachedInterp) {
    // InvalidateBlock(address);
  }
}

void MipsBase::OnNewBlock(uint64_t address) {
  address &= 0xFFFFFFFF;

  auto& cache = GetMipsCache();
  MipsCacheBlock block;
  block.start_ = address;

  int block_length = 0;
  uint64_t inst_address = address;
  bool has_delay_slot = false;

  for (int i = 0; i < kCacheBlockMaxLength; i++) {
    block.entries_[i].func_ = nullptr;
  }

  for (int i = 0; i < kCacheBlockMaxLength - 1; i++) {
    uint32_t opcode = Fetch(inst_address);
    MipsCacheEntry entry;
    entry.address_ = inst_address;
    entry.opcode_ = opcode;
    entry.func_ = GetInstFuncPtr(opcode);
    block.entries_[i] = entry;
    inst_address += 4;
    block_length++;
    if (IsInstBranch(opcode)) {
      has_delay_slot = DoesInstHaveDelaySlot(opcode);
      break;
    }
  }

  if (has_delay_slot) {
    uint64_t delay_slot_inst_ = Fetch(inst_address);
    MipsCacheEntry delay_slot_entry;
    delay_slot_entry.address_ = inst_address;
    delay_slot_entry.opcode_ = delay_slot_inst_;
    delay_slot_entry.func_ = GetInstFuncPtr(delay_slot_inst_);
    block.entries_[block_length] = delay_slot_entry;
    inst_address += 4;
    block_length++;
  }

  block.end_ = address + block_length * 4;
  block.length_ = block_length;
  block.cycle_ = block_length * kMipsCpi;

  cache.InsertBlock(block);
  if (false) {
    fmt::print("New block #{}: {:08X}-{:08X} ({} inst)\n", cache.GetSize(), address, inst_address, block_length);
    for (int i = 0; i < block_length; i++) {
      const MipsCacheEntry& entry = block.entries_[i];
      fmt::print("{:08X} | {}\n", entry.address_, MipsInst(entry.opcode_).Disassemble(entry.address_));
    }
  }
}

void MipsBase::InvalidateBlock(uint64_t address) {
  if (!kUseCachedInterp) {
    return;
  }

  address &= 0xFFFFFFFF;
  GetMipsCache().InvalidateBlock(address);
}

void MipsBase::InstAdd(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value + rt_value;
  if (get_overflow_add_i32(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32(inst.rd(), rd_value);
  }
}

void MipsBase::InstAddu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value + rt_value;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstAddi(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value + imm;
  if (get_overflow_add_i32(rt_value, rs_value, imm)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32(inst.rt(), rt_value);
  }
}

void MipsBase::InstAddiu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value + imm;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstAnd(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value & rt_value;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstAndi(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint16_t imm = inst.imm();
  uint32_t rt_value = rs_value & imm;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstDiv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t rt_value = ReadGpr32(inst.rt());
  int32_t hi = 0;
  int32_t lo = 0;
  bool rs_msb = rs_value & (1 << 31);
  if (rt_value == 0) {
    hi = rs_value;
    lo = rs_msb ? 1 : 0xFFFFFFFF;
  } else if ((uint32_t)rs_value == 0x80000000 && (uint32_t)rt_value == 0xFFFFFFFF) {
    hi = 0;
    lo = 0x80000000;
  } else {
    int32_t rs_signed = rs_value;
    int32_t rt_signed = rt_value;
    lo = rs_signed / rt_signed;
    hi = rs_signed % rt_signed;
  }
  lo_ = lo;
  hi_ = hi;
}

void MipsBase::InstDivu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t hi = 0;
  uint32_t lo = 0;
  if (rt_value == 0) {
    hi = rs_value;
    lo = 0xFFFFFFFF;
  } else {
    lo = rs_value / rt_value;
    hi = rs_value % rt_value;
  }
  lo_ = lo;
  hi_ = hi;
}

void MipsBase::InstMult(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  int64_t rs_signed = sext_i32_to_i64(rs_value);
  int64_t rt_signed = sext_i32_to_i64(rt_value);
  int64_t result = rs_signed * rt_signed;
  hi_ = result >> 32;
  lo_ = result & 0xFFFFFFFF;
}

void MipsBase::InstMultu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint64_t rs_long = rs_value;
  uint64_t rt_long = rt_value;
  uint64_t result = rs_long * rt_long;
  hi_ = result >> 32;
  lo_ = result & 0xFFFFFFFF;
}

void MipsBase::InstNor(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = 0xFFFFFFFF ^ (rs_value | rt_value);
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstOr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value | rt_value;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstOri(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint16_t imm = inst.imm();
  uint32_t rt_value = rs_value | imm;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstSll(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  if (inst.shamt() == 0) {
    WriteGpr32(inst.rd(), rt_value);
    return;
  }
  uint32_t rd_value = rt_value << inst.shamt();
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSllv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rd_value = rt_value << (rs_value & 31);
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSra(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int32_t rt_value = ReadGpr32(inst.rt());
  int32_t rd_value = rt_value >> inst.shamt();
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSrav(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  int32_t rd_value = rt_value >> (rs_value & 31);
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSrl(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rt_value >> inst.shamt();
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSrlv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rd_value = rt_value >> (rs_value & 31);
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSub(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value - rt_value;
  if (get_overflow_sub_i32(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32(inst.rd(), rd_value);
  }
}

void MipsBase::InstSubu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value - rt_value;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstXor(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value ^ rt_value;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstXori(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = inst.imm();
  uint32_t rt_value = rs_value ^ imm;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstLui(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t imm = inst.imm();
  uint32_t rt_value = imm << 16;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstSlt(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value < rt_value ? 1 : 0;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSltu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value < rt_value ? 1 : 0;
  WriteGpr32(inst.rd(), rd_value);
}

void MipsBase::InstSlti(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value < imm ? 1 : 0;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstSltiu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value < imm ? 1 : 0;
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstBeq(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t rt_value = ReadGpr32(inst.rt());
  if (rs_value == rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBne(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t rt_value = ReadGpr32(inst.rt());
  if (rs_value != rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBgtz(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  if (rs_value > 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBlez(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  if (rs_value <= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBgez(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBgezal(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  LinkForJump(31);
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBltz(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstBltzal(uint32_t opcode) {
  // HACK: No load delay on branch
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  int32_t rs_value = ReadGpr32(inst.rs());
  LinkForJump(31);
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

void MipsBase::InstJ(uint32_t opcode) {
  JTypeInst inst = MipsInst(opcode).GetJType();
#ifdef MIPS_64BIT
  uint64_t dst = ((pc_ + 4) & 0xF000000000000000UL) | (inst.address() << 2);
  Jump64(dst);
#else
  uint32_t dst = ((pc_ + 4) & 0xF0000000) | (inst.address() << 2);
  Jump32(dst);
#endif
}

void MipsBase::InstJal(uint32_t opcode) {
  JTypeInst inst = MipsInst(opcode).GetJType();
#ifdef MIPS_64BIT
  uint64_t dst = ((pc_ + 4) & 0xF000000000000000UL) | (inst.address() << 2);
  LinkForJump(31);
  Jump64(dst);
#else
  uint32_t dst = ((pc_ + 4) & 0xF0000000) | (inst.address() << 2);
  LinkForJump(31);
  Jump32(dst);
#endif
}

void MipsBase::InstJr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value & 3) {
    fmt::print("Unaligned jump\n");
    if (kPanicOnUnalignedJump) {
      DumpProcessorLog();
      PANIC("Unaligned jump");
    }
    rs_value &= ~3;
  }
  Jump64(rs_value);
}

void MipsBase::InstJalr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value & 3) {
    fmt::print("Unaligned jump\n");
    if (kPanicOnUnalignedJump) {
      DumpProcessorLog();
      PANIC("Unaligned jump");
    }
    rs_value &= ~3;
  }
  LinkForJump(inst.rd());
  Jump64(rs_value);
}

void MipsBase::InstSyscall(uint32_t opcode) {
  TriggerException(ExceptionCause::kSyscall);
}

void MipsBase::InstBreak(uint32_t opcode) {
  TriggerException(ExceptionCause::kBkpt);
}

void MipsBase::InstLb(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult8 load_result = Load8(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i8_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

void MipsBase::InstLbu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult8 load_result = Load8(address);
  if (load_result.has_value) {
    uint8_t rt_value = load_result.value;
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

void MipsBase::InstLh(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (address & 1) {
#ifdef MIPS_64BIT
    cop_[0]->Write64Internal(8, address);
#else
    cop_[0]->Write32Internal(8, address);
#endif
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult16 load_result = Load16(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i16_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

void MipsBase::InstLhu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (address & 1) {
#ifdef MIPS_64BIT
    cop_[0]->Write64Internal(8, address);
#else
    cop_[0]->Write32Internal(8, address);
#endif
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult16 load_result = Load16(address);
  if (load_result.has_value) {
    uint16_t rt_value = load_result.value;
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

void MipsBase::InstLw(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (address & 3) {
#ifdef MIPS_64BIT
    cop_[0]->Write64Internal(8, address);
#else
    cop_[0]->Write32Internal(8, address);
#endif
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult32 load_result = Load32(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i32_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

void MipsBase::InstLwl(uint32_t opcode) {
  // HACK: Execute pending load
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  switch (address & 3) {
    case 0: {
      LoadResult8 load_result = Load8(address);
      if (load_result.has_value) {
        rt_value &= 0x00FFFFFF;
        rt_value |= load_result.value << 24;
      }
      break;
    }
    case 1: {
      LoadResult16 load_result = Load16(address - 1);
      if (load_result.has_value) {
        rt_value &= 0x0000FFFF;
        rt_value |= load_result.value << 16;
      }
      break;
    }
    case 2: {
      LoadResult16 load_result = Load16(address - 2);
      LoadResult8 load_result2 = Load8(address);
      if (load_result.has_value && load_result2.has_value) {
        rt_value &= 0x000000FF;
        rt_value |= load_result.value << 8;
        rt_value |= load_result2.value << 24;
      }
      break;
    }
    case 3: {
      LoadResult32 load_result = Load32(address - 3);
      if (load_result.has_value) {
        rt_value = load_result.value;
      }
      break;
    }
  }
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstLwr(uint32_t opcode) {
  // HACK: Execute pending load
  ExecuteDelayedLoad();

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  switch (address & 3) {
    case 0: {
      LoadResult32 load_result = Load32(address);
      if (load_result.has_value) {
        rt_value = load_result.value;
      }
      break;
    }
    case 1: {
      LoadResult8 load_result = Load8(address);
      LoadResult16 load_result2 = Load16(address + 1);
      if (load_result.has_value && load_result2.has_value) {
        rt_value &= 0xFF000000;
        rt_value |= load_result.value;
        rt_value |= load_result2.value << 8;
      }
      break;
    }
    case 2: {
      LoadResult16 load_result = Load16(address);
      if (load_result.has_value) {
        rt_value &= 0xFFFF0000;
        rt_value |= load_result.value;
      }
      break;
    }
    case 3: {
      LoadResult8 load_result = Load8(address);
      if (load_result.has_value) {
        rt_value &= 0xFFFFFF00;
        rt_value |= load_result.value;
      }
      break;
    }
  }
  WriteGpr32(inst.rt(), rt_value);
}

void MipsBase::InstLwc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult32 load_result = Load32(address);
  if (load_result.has_value) {
    uint32_t copt_value = load_result.value;
    QueueDelayedCopLoad(cop_id, inst.rt(), copt_value);
  }
}

void MipsBase::InstSb(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store8(address, rt_value);
}

void MipsBase::InstSh(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (address & 1) {
#ifdef MIPS_64BIT
    cop_[0]->Write64Internal(8, address);
#else
    cop_[0]->Write32Internal(8, address);
#endif
    TriggerException(ExceptionCause::kAddrs);
    return;
  }
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store16(address, rt_value);
}

void MipsBase::InstSw(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (address & 3) {
#ifdef MIPS_64BIT
    cop_[0]->Write64Internal(8, address);
#else
    cop_[0]->Write32Internal(8, address);
#endif
    TriggerException(ExceptionCause::kAddrs);
    return;
  }
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store32(address, rt_value);
}

void MipsBase::InstSwl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  switch (address & 3) {
    case 0:
      Store8(address, rt_value >> 24);
      break;
    case 1:
      Store16(address - 1, rt_value >> 16);
      break;
    case 2:
      Store16(address - 2, rt_value >> 8);
      Store8(address, rt_value >> 24);
      break;
    case 3:
      Store32(address - 3, rt_value);
      break;
  }
}

void MipsBase::InstSwr(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  switch (address & 3) {
    case 0:
      Store32(address, rt_value);
      break;
    case 1:
      Store8(address, rt_value);
      Store16(address + 1, rt_value >> 8);
      break;
    case 2:
      Store16(address, rt_value);
      break;
    case 3:
      Store8(address, rt_value);
      break;
  }
}

void MipsBase::InstSwc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t copt_value = cop_[cop_id]->Read32(inst.rt());
  Store32(address, copt_value);
}

void MipsBase::InstMfhi(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  WriteGpr32(inst.rd(), hi_);
}

void MipsBase::InstMflo(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  WriteGpr32(inst.rd(), lo_);
}

void MipsBase::InstMthi(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  hi_ = rs_value;
}

void MipsBase::InstMtlo(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  lo_ = rs_value;
}

void MipsBase::InstCop(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  uint32_t cop_command = opcode & 0x1FFFFFF;
  cop_[cop_id]->Command(cop_command);
  if (kLazyInterruptPolling) {
    if (cop_id == 0) {
      CheckInterrupt();
    }
  }
}

void MipsBase::InstMfc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  uint64_t cop_value = cop_[cop_id]->Read64(inst.rd());
  WriteGpr64(inst.rt(), cop_value);
}

void MipsBase::InstCfc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  uint64_t cop_value = cop_[cop_id]->Read64(inst.rd() + 32);
  WriteGpr64(inst.rt(), cop_value);
}

void MipsBase::InstMtc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  uint64_t rt_value = ReadGpr64(inst.rt());
  cop_[cop_id]->Write64(inst.rd(), rt_value);
  if (kLazyInterruptPolling) {
    if (cop_id == 0) {
      CheckInterrupt();
    }
  }
}

void MipsBase::InstCtc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  uint64_t rt_value = ReadGpr64(inst.rt());
  cop_[cop_id]->Write64(inst.rd() + 32, rt_value);
}

void MipsBase::InstNop(uint32_t opcode) {
}

void MipsBase::InstUnknown(uint32_t opcode) {
  fmt::print("Unknown instruction: {:08X} @ {:08X}\n", opcode, pc_);
  DumpProcessorLog();
  PANIC("Unknown instruction");
}

std::string MipsLog::ToString() {
  std::string result;
  result += fmt::format("PC: {:08X} | OPCODE: {:08X} | {} | ", pc_, inst_, MipsInst(inst_).Disassemble(pc_));
  for (int i = 1; i < 32; i++) {
    result += fmt::format("{}: {:08X}", GetMipsRegName(i), gpr_[i]);
    if (i != 31) {
      result += " | ";
    }
  }
  return result;
}
