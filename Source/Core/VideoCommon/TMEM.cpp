// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <array>

#include "Common/ChunkFile.h"

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/TMEM.h"

namespace TMEM
{
struct TextureUnitState
{
  enum class State
  {
    // Cache is invalid. Configuration has changed
    INVALID,

    // Valid, but not cached due to either being too big, or overlapping with another texture unit
    VALID,

    // Texture unit has cached all of the previous draw
    CACHED,
  };

  struct BankConfig
  {
    u32 width = 0;
    u32 height = 0;
    u32 base = 0;
    u32 size = 0;
    bool Overlaps(const BankConfig& other) const;
  };

  BankConfig even = {};
  BankConfig odd = {};
  State state = State::INVALID;

  bool Overlaps(const TextureUnitState& other) const;
};

static u32 CalculateUnitSize(TextureUnitState::BankConfig bank_config);

static std::array<TextureUnitState, 8> s_unit;

// On TMEM configuration changed:
// 1. invalidate stage.

void ConfigurationChanged(TexUnitAddress bp_addr, u32 config)
{
  TextureUnitState& unit_state = s_unit[bp_addr.GetUnitID()];

  // If anything has changed, we can't assume existing state is still valid.
  unit_state.state = TextureUnitState::State::INVALID;

  // Note: BPStructs has already filtered out NOP changes before calling us
  switch (bp_addr.Reg)
  {
  case TexUnitAddress::Register::SETIMAGE1:
  {
    // Image Type and Even bank's Cache Height, Cache Width, TMEM Offset
    TexImage1 even = {.hex = config};
    unit_state.even = {even.cache_width, even.cache_height, even.tmem_even << 5, 0};
    break;
  }
  case TexUnitAddress::Register::SETIMAGE2:
  {
    // Odd bank's Cache Height, Cache Width, TMEM Offset
    TexImage2 odd = {.hex = config};
    unit_state.odd = {odd.cache_width, odd.cache_height, odd.tmem_odd << 5, 0};
    break;
  }
  default:
    // Something else has changed
    return;
  }
}

void InvalidateAll()
{
  for (auto& unit : s_unit)
  {
    unit.state = TextureUnitState::State::INVALID;
  }
}

// On invalidate cache:
// 1. invalidate all texture units.

void Invalidate([[maybe_unused]] u32 param)
{
  // The exact arguments of Invalidate commands is currently unknown.
  // It appears to contain the TMEM address and a size.

  // For simplicity, we will just invalidate everything
  InvalidateAll();
}

// On bind:
// 1. use mipmapping/32bit status to calculate final sizes
// 2. if texture size is small enough to fit in region mark as cached.
//    otherwise, mark as valid

void Bind(u32 unit, int width, int height, bool is_mipmapped, bool is_32_bit)
{
  TextureUnitState& unit_state = s_unit[unit];

  // All textures use the even bank.
  // It holds the level 0 mipmap (and other even mipmap LODs, if mipmapping is enabled)
  unit_state.even.size = CalculateUnitSize(unit_state.even);

  bool fits = (width * height * 32U) <= unit_state.even.size;

  if (is_mipmapped || is_32_bit)
  {
    // And the odd bank is enabled when either mipmapping is enabled or the texture is 32 bit
    // It holds the Alpha and Red channels of 32 bit textures or the odd layers of a mipmapped
    // texture
    unit_state.odd.size = CalculateUnitSize(unit_state.odd);

    fits = fits && (width * height * 32U) <= unit_state.odd.size;
  }
  else
  {
    unit_state.odd.size = 0;
  }

  if (is_mipmapped)
  {
    // TODO: This is what games appear to expect from hardware. But seems odd, as it doesn't line up
    //       with how much extra memory is required for mipmapping, just 33% more.
    //       Hardware testing is required to see exactly what gets used.

    // When mipmapping is enabled, the even bank is doubled in size
    // The extended region holds the remaining even mipmap layers
    unit_state.even.size *= 2;

    if (is_32_bit)
    {
      // When a 32bit texture is mipmapped, the odd bank is also doubled in size
      unit_state.odd.size *= 2;
    }
  }

  unit_state.state = fits ? TextureUnitState::State::CACHED : TextureUnitState::State::VALID;
}

static u32 CalculateUnitSize(TextureUnitState::BankConfig bank_config)
{
  u32 width = bank_config.width;
  u32 height = bank_config.height;

  // These are the only cache sizes supported by the sdk
  if (width == height)
  {
    switch (width)
    {
    case 3:  // 32KB
      return 32 * 1024;
    case 4:  // 128KB
      return 128 * 1024;
    case 5:  // 512KB
      return 512 * 1024;
    default:
      break;
    }
  }

  // However, the registers allow a much larger amount of configurablity.
  // Maybe other sizes are broken?
  // Until hardware tests are done, this is a guess at the size algorithm

  return 512 * (1 << width) * (1 << height);
}

bool TextureUnitState::BankConfig::Overlaps(const BankConfig& other) const
{
  if (size == 0 || other.size == 0)
    return false;
  return (base <= other.base && (base + size) > other.base) ||
         (other.base <= base && (other.base + other.size) > base);
}

bool TextureUnitState::Overlaps(const TextureUnitState& other) const
{
  if (state == TextureUnitState::State::INVALID || other.state == TextureUnitState::State::INVALID)
    return false;
  return even.Overlaps(other.even) || even.Overlaps(other.odd) || odd.Overlaps(other.even) ||
         odd.Overlaps(other.odd);
}

// Scans though active texture units checks for overlaps.
void FinalizeBinds(BitSet32 used_textures)
{
  for (u32 i : used_textures)
  {
    if (s_unit[i].even.Overlaps(s_unit[i].odd))
    {  // Self-overlap
      s_unit[i].state = TextureUnitState::State::VALID;
    }
    for (size_t j = 0; j < s_unit.size(); j++)
    {
      if (j != i && s_unit[i].Overlaps(s_unit[j]))
      {
        // There is an overlap, downgrade both from CACHED
        // (for there to be an overlap, both must have started as valid or cached)
        s_unit[i].state = TextureUnitState::State::VALID;
        s_unit[j].state = TextureUnitState::State::VALID;
      }
    }
  }
}

bool IsCached(u32 unit)
{
  return s_unit[unit].state == TextureUnitState::State::CACHED;
}

bool IsValid(u32 unit)
{
  return s_unit[unit].state != TextureUnitState::State::INVALID;
}

void Init()
{
  s_unit.fill({});
}

void DoState(PointerWrap& p)
{
  p.DoArray(s_unit);
}

}  // namespace TMEM
