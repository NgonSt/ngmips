#include "mips_tlb_normal.h"

#include <fmt/format.h>

#include "panic.h"

MipsTlbNormal::MipsTlbNormal() {
}

void MipsTlbNormal::Reset() {
  for (int i = 0; i < 32; i++) {
    entry_[i] = MipsTlbEntry();
  }
}

MipsTlbTranslationResult MipsTlbNormal::TranslateAddress(uint64_t address) {
  address &= 0xFFFFFFFF;

  MipsTlbTranslationResult result;
  result.found_ = false;
  result.read_only_ = false;
  result.address_ = 0;

  bool is_kseg0 = address >= 0x80000000 && address < 0xA0000000;
  bool is_kseg1 = address >= 0xA0000000 && address < 0xC0000000;
  if (is_kseg0 || is_kseg1) {
    result.found_ = true;
    result.read_only_ = false;
    result.address_ = address & 0x1FFFFFFF;
    return result;
  }

  uint8_t asid = entry_hi_ & 0xFF;
  for (int i = 0; i < 32; i++) {
    const MipsTlbEntry& entry = entry_[i];
    uint64_t mask = (~entry.page_mask_) & 0xFFFFE000;
    bool asid_match = (entry_[i].entry_hi_ & 0xFF) == asid;
    bool vpn_match = (entry_[i].entry_hi_ & mask) == (address & mask);
    bool g_bit = entry.entry_lo0_ & 1;
    if (vpn_match && (g_bit || asid_match)) {
      uint32_t odd_mask = ((entry.page_mask_ | 0x1FFF) + 1) >> 1;
      bool is_odd = address & odd_mask;
      uint64_t entry_lo = is_odd ? entry.entry_lo1_ : entry.entry_lo0_;
      bool v_bit = entry_lo & 2;
      bool d_bit = entry_lo & 4;
      if (!v_bit) {
        continue;
      }
      uint32_t offset_mask = odd_mask - 1;
      uint32_t pfn_offset = (entry_lo & ~(0xFC00003F)) << 6;
      result.address_ = (pfn_offset & ~offset_mask) | (address & offset_mask);
      result.found_ = true;
      result.read_only_ = !d_bit;
      // fmt::print("TLB HIT {} (odd: {}) | {:08X} -> {:08X}\n", i, is_odd, address, result.address_);
      return result;
    }
  }

  return result;
}

const MipsTlbEntry& MipsTlbNormal::GetTlbEntry(int idx) {
  return entry_[idx];
}

void MipsTlbNormal::SetTlbEntry(int idx, const MipsTlbEntry& entry) {
  fmt::print("TLB WRITE {} | lo0: {:08X} | lo1: {:08X} | hi: {:08X} | mask: {:08X}\n",
             idx, entry.entry_lo0_, entry.entry_lo1_, entry.entry_hi_, entry.page_mask_);
  entry_[idx] = entry;
  bool g_bit = entry.entry_lo0_ & entry.entry_lo1_ & 1;
  if (g_bit) {
    entry_[idx].entry_lo0_ |= 1;
    entry_[idx].entry_lo1_ |= 1;
  } else {
    entry_[idx].entry_lo0_ &= ~1;
    entry_[idx].entry_lo1_ &= ~1;
  }
}

uint64_t MipsTlbNormal::GetEntryHi() {
  return entry_hi_;
}

void MipsTlbNormal::SetEntryHi(uint64_t value) {
  entry_hi_ = value & 0xFFFFE0FF;
}

uint64_t MipsTlbNormal::GetEntryLo0() {
  return entry_lo0_;
}

void MipsTlbNormal::SetEntryLo0(uint64_t value) {
  entry_lo0_ = value;
}

uint64_t MipsTlbNormal::GetEntryLo1() {
  return entry_lo1_;
}

void MipsTlbNormal::SetEntryLo1(uint64_t value) {
  entry_lo1_ = value;
}

uint64_t MipsTlbNormal::GetPageMask() {
  return page_mask_;
}

void MipsTlbNormal::SetPageMask(uint64_t value) {
  page_mask_ = value & 0x1FFE000;
}

uint32_t MipsTlbNormal::GetIndex() {
  return index_;
}

void MipsTlbNormal::SetIndex(uint32_t value) {
  index_ = value & 0x3F;
}

void MipsTlbNormal::InformTlbException(uint64_t address) {
  // Preserve ASID from current EntryHi, set VPN2 from faulting address
  entry_hi_ = (address & 0xFFFE000) | (entry_hi_ & 0xFF);
}

uint32_t MipsTlbNormal::ProbeTlbEntry() {
  uint8_t asid = entry_hi_ & 0xFF;
  for (int i = 0; i < 32; i++) {
    bool g_bit = entry_[i].entry_lo0_ & entry_[i].entry_lo1_ & 1;
    bool asid_match = (entry_[i].entry_hi_ & 0xFF) == asid;
    bool vpn_match = (entry_[i].entry_hi_ & 0xFFFE000) == (entry_hi_ & 0xFFFE000);
    if (vpn_match && (g_bit || asid_match)) {
      return i;
    }
  }
  return 0x80000000;
}
