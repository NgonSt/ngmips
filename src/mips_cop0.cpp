#include "mips_cop0.h"

#include <fmt/core.h>

#include "mips_base.h"
#include "panic.h"

namespace {

const bool kLogCop = false;

uint32_t xorshift_state = 1;

void init_xorshift(uint32_t seed) {
  xorshift_state = 1;
}

uint32_t xorshift() {
  xorshift_state ^= xorshift_state << 13;
  xorshift_state ^= xorshift_state >> 17;
  xorshift_state ^= xorshift_state << 5;
  return xorshift_state;
}

}  // namespace

MipsCop0::MipsCop0() {
  context_ = 0;
  wired_ = 0;
  badvaddr_ = 0;
  compare_ = 0;
  sr_ = 0x34000000;
  cause_ = 0xB000007C;
  epc_ = 0xFFFFFFFFFFFFFFFFUL;
  error_epc_ = 0xFFFFFFFFFFFFFFFFUL;
  count_start_timestamp_ = 0;
  last_compare_check_timestamp_ = 0;
  surpress_compare_interrupt_ = false;
}

void MipsCop0::ConnectCpu(MipsBase* cpu) {
  cpu_ = cpu;
}

void MipsCop0::Reset() {
  context_ = 0;
  wired_ = 0;
  badvaddr_ = 0;
  compare_ = 0;
  sr_ = 0x34000000;
  cause_ = 0xB000007C;
  epc_ = 0xFFFFFFFFFFFFFFFFUL;
  error_epc_ = 0xFFFFFFFFFFFFFFFFUL;
  count_start_timestamp_ = 0;
  last_compare_check_timestamp_ = 0;
  surpress_compare_interrupt_ = false;
}

void MipsCop0::Command(uint32_t command) {
  switch (command & 0x3F) {
    case 0x01: {
      // TLBR
      MipsTlbBase* tlb = cpu_->GetTlb();
      const MipsTlbEntry& entry = tlb->GetTlbEntry(tlb->GetIndex() & 31);
      tlb->SetEntryLo0(entry.entry_lo0_);
      tlb->SetEntryLo1(entry.entry_lo1_);
      tlb->SetEntryHi(entry.entry_hi_);
      tlb->SetPageMask(entry.page_mask_);
      break;
    }
    case 0x02: {
      // TLBWI
      MipsTlbBase* tlb = cpu_->GetTlb();
      MipsTlbEntry entry(tlb->GetEntryLo0(), tlb->GetEntryLo1(), tlb->GetEntryHi(), tlb->GetPageMask());
      tlb->SetTlbEntry(tlb->GetIndex() & 31, entry);
      break;
    }
    case 0x06: {
      // TLBWR
      MipsTlbBase* tlb = cpu_->GetTlb();
      MipsTlbEntry entry(tlb->GetEntryLo0(), tlb->GetEntryLo1(), tlb->GetEntryHi(), tlb->GetPageMask());
      tlb->SetTlbEntry(Read32(1) & 31, entry);
      break;
    }
    case 0x08: {
      // TLBP
      MipsTlbBase* tlb = cpu_->GetTlb();
      tlb->SetIndex(tlb->ProbeTlbEntry());
      break;
    }
    case 0x10: {
      if (kLogCop) {
        fmt::print("RFE\n");
      }
      uint32_t sr_mode = sr_ & 0x3F;
      sr_ &= ~0x0F;
      sr_ |= (sr_mode >> 2) & 0xF;
      break;
    }
    case 0x18:
      if (kLogCop) {
        fmt::print("ERET | EPC: {:08X}\n", epc_);
      }
      if (sr_ & 4) {
        fmt::print("ERET on trap\n");
        uint32_t sr_new = sr_ & ~(1 << 2);
        sr_ = sr_new;
        cpu_->SetPcDuringInst(error_epc_);
      } else {
        uint32_t sr_new = sr_ & ~(1 << 1);
        sr_ = sr_new;
        cpu_->SetPcDuringInst(epc_);
      }
      cpu_->SetLlbit(false);
      break;
    default:
      fmt::print("COP0 command: {:08X}\n", command);
      break;
  }
}

uint32_t MipsCop0::Read32(int idx) {
  MipsTlbBase* tlb = cpu_->GetTlb();
  switch (idx) {
    case 0:
      return tlb->GetIndex();
    case 1:
      // NOTE: in theory, one can get REALLY unlucky...
      while (1) {
        uint32_t rn = xorshift() & 0x1F;
        if (rn >= wired_) {
          return rn;
        }
      }
    case 2:
      return tlb->GetEntryLo0();
    case 3:
      return tlb->GetEntryLo1();
    case 4:
      return context_;
    case 5:
      return tlb->GetPageMask();
    case 6:
      return wired_;
    case 8:
      return badvaddr_;
    case 9:
      return GetCount();
    case 10:
      return tlb->GetEntryHi();
    case 11:
      return compare_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
    case 30:
      return error_epc_;
    default:
      fmt::print("COP0 read32: {}\n", idx);
      break;
  }
  return 0;
}

void MipsCop0::Write32(int idx, uint32_t value) {
  MipsTlbBase* tlb = cpu_->GetTlb();
  if (kLogCop) {
    fmt::print("COP0 write: {}, {:08X}\n", idx, value);
  }
  switch (idx) {
    case 0:
      tlb->SetIndex(value);
      break;
    case 2:
      tlb->SetEntryLo0(value);
      break;
    case 3:
      tlb->SetEntryLo1(value );
      break;
    case 4:
      context_ = value & 0xFFFFFFF0;
      break;
    case 5:
      tlb->SetPageMask(value);
      break;
    case 6:
      wired_ = value;
      break;
    case 8:
      // I don't know why, but this seems to be writable
      badvaddr_ = value;
      break;
    case 9:
      fmt::print("Write to count: {:08X}\n", value);
      WriteCount(value);
      break;
    case 10:
      tlb->SetEntryHi(value);
      break;
    case 11:
      compare_ = value;
      surpress_compare_interrupt_ = true;
      cpu_->ClearCompareInterrupt();
      break;
    case 12:
      if (false) {
        bool fr_old = (sr_ & (1 << 26)) != 0;
        bool fr_new = (value & (1 << 26)) != 0;
        if (fr_old != fr_new) {
          cpu_->DumpProcessorLog();
          PANIC("AAAA");
        }
      }
      sr_ = value;
      break;
    case 13:
      fmt::print("Write to cause: {:08X}\n", value);
      cause_ = (cause_ & ~0x300) | (value & 0x300);
      break;
    case 14:
      epc_ = value;
      break;
    case 30:
      error_epc_ = value;
      break;
    default:
      fmt::print("COP0 write32: {}, {:08X}\n", idx, value);
      break;
  }
}

uint64_t MipsCop0::Read64(int idx) {
  MipsTlbBase* tlb = cpu_->GetTlb();
  switch (idx) {
    case 0:
      return tlb->GetIndex();
    case 1:
      while (1) {
        uint32_t rn = xorshift() & 0x1F;
        if (rn >= wired_) {
          return rn;
        }
      }
    case 2:
      return tlb->GetEntryLo0();
    case 3:
      return tlb->GetEntryLo1();
    case 4:
      return context_;
    case 5:
      return tlb->GetPageMask();
    case 6:
      return wired_;
    case 8:
      return badvaddr_;
    case 9:
      return GetCount();
    case 10:
      return tlb->GetEntryHi();
    case 11:
      return compare_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
    case 30:
      return error_epc_;
    default:
      fmt::print("COP0 read64: {}\n", idx);
      break;
  }
  return 0;
}

void MipsCop0::Write64(int idx, uint64_t value) {
  MipsTlbBase* tlb = cpu_->GetTlb();
  if (kLogCop) {
    fmt::print("COP0 write: {}, {:08X}\n", idx, value);
  }
  switch (idx) {
    case 0:
      tlb->SetIndex(value);
      break;
    case 2:
      tlb->SetEntryLo0(value);
      break;
    case 3:
      tlb->SetEntryLo1(value);
      break;
    case 4:
      context_ = value;
      break;
    case 5:
      tlb->SetPageMask(value);
      break;
    case 6:
      wired_ = value;
      break;
    case 8:
      badvaddr_ = value;
      break;
    case 9:
      WriteCount(value);
      break;
    case 10:
      tlb->SetEntryHi(value);
      break;
    case 11:
      compare_ = value;
      surpress_compare_interrupt_ = true;
      cpu_->ClearCompareInterrupt();
      break;
    case 12:
      // fmt::print("SR: {:08X} -> {:08X} | PC:{:08X}\n", sr_, value, cpu_->GetPc());
      if (false) {
        bool fr_old = (sr_ & (1 << 26)) != 0;
        bool fr_new = (value & (1 << 26)) != 0;
        if (fr_old != fr_new) {
          PANIC("AAAA");
        }
      }
      sr_ = value;
      break;
    case 13:
      fmt::print("Write to cause: {:08X}\n", value);
      cause_ = (cause_ & ~0x300) | (value & 0x300);
      break;
    case 14:
      epc_ = value;
      break;
    case 30:
      error_epc_ = value;
      break;
    default:
      fmt::print("COP0 write64: {}, {:016X}\n", idx, value);
      break;
  }
}

uint32_t MipsCop0::Read32Internal(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 9:
      return GetCount();
    case 11:
      return compare_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
    case 128:
      return CheckCompareInterrupt();
    default:
      fmt::print("Read32Internal unhadled: {}\n", idx);
      break;
  }
  return 0;
}

void MipsCop0::Write32Internal(int idx, uint32_t value) {
  switch (idx) {
    case 8:
      badvaddr_ = value;
      break;
    case 12:
      // fmt::print("SR: {:08X} -> {:08X} | PC(INT):{:08X}\n", sr_, value, cpu_->GetPc());
      sr_ = value;
      break;
    case 13:
      cause_ = value;
      break;
    case 14:
      epc_ = value;
      break;
    case 30:
      error_epc_ = value;
      break;
    default:
      PANIC("Write32Internal unhadled: {}\n", idx);
      break;
  }
}

uint64_t MipsCop0::Read64Internal(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
    default:
      fmt::print("Read64Internal unhadled: {}\n", idx);
      break;
  }
  return 0;
}

void MipsCop0::Write64Internal(int idx, uint64_t value) {
  switch (idx) {
    case 8:
      badvaddr_ = value;
      break;
    case 12:
      sr_ = value;
      break;
    case 13:
      cause_ = value;
      break;
    case 14:
      epc_ = value;
      break;
    case 30:
      error_epc_ = value;
      break;
    default:
      PANIC("Write64Internal unhadled: {}\n", idx);
      break;
  }
}

bool MipsCop0::GetFlag() {
  return false;
}

bool MipsCop0::CheckCompareInterrupt() {
  uint64_t timestamp = cpu_->GetTimestamp() >> 1;
  if (surpress_compare_interrupt_) {
    surpress_compare_interrupt_ = false;
    last_compare_check_timestamp_ = timestamp;
    return false;
  }

  if (last_compare_check_timestamp_ > timestamp) {
    fmt::print("Time went backwards\n");
    last_compare_check_timestamp_ = timestamp;
    return false;
  }

  uint64_t delta = timestamp - last_compare_check_timestamp_;
  if (delta == 0) {
    return false;
  }
  bool result = false;
  if (true) {
    uint32_t count = last_compare_check_timestamp_ - count_start_timestamp_;
    // FIXME: THIS IS SO SLOW
    for (int i = 0; i < delta; i++) {
      count++;
      if (count == compare_) {
        result = true;
        break;
      }
    }
  } else {
    uint32_t count_start = (uint32_t)(last_compare_check_timestamp_ - count_start_timestamp_);
    uint32_t count_end = (uint32_t)(timestamp - count_start_timestamp_);
    // Check if compare_ is in (count_start, count_end] accounting for wrap
    bool crossed = (compare_ - count_start - 1) <= (count_end - count_start - 1);
    result = crossed;
  }
  last_compare_check_timestamp_ = timestamp;
  return result;
}

void MipsCop0::WriteCount(uint32_t value) {
  uint64_t timestamp = cpu_->GetTimestamp() >> 1;
  if (timestamp < value) {
    count_start_timestamp_ = timestamp;
    return;
  }

  count_start_timestamp_ = timestamp - value;
  surpress_compare_interrupt_ = true;
}

uint32_t MipsCop0::GetCount() {
  uint64_t timestamp = cpu_->GetTimestamp() >> 1;
  if (timestamp < count_start_timestamp_) {
    count_start_timestamp_ = timestamp;
    return 0;
  }
  return timestamp - count_start_timestamp_;
}
