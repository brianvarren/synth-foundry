#pragma once
// HexGlyphHarmony.h  —  12-bit chord mask → up to 6 frequencies
// Policy: static/global only; no dynamic alloc; no STL; no String.

#ifndef HEX_GLYPH
#define HEX_GLYPH    0x891u
#endif

#ifndef VOICE_COUNT
#define VOICE_COUNT  4u
#endif

#ifndef ROOT_ROTATION_SEMITONES
#define ROOT_ROTATION_SEMITONES  0
#endif

#ifndef INCLUDE_ROOT_IF_ABSENT
#define INCLUDE_ROOT_IF_ABSENT  1
#endif

#ifndef SPREAD_OCTAVES
#define SPREAD_OCTAVES  0
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned _hg_rol12(unsigned m, int r){
  r %= 12; if (r < 0) r += 12;
  return ((m << r) | (m >> (12 - r))) & 0x0FFFu;
}

static inline unsigned char _hg_pop12(unsigned m){
  m &= 0x0FFFu;
  m = (m & 0x555u) + ((m >> 1) & 0x555u);
  m = (m & 0x333u) + ((m >> 2) & 0x333u);
  m = (m + (m >> 4)) & 0x0F0Fu;
  return (unsigned char)((m + (m >> 8)) & 0x1Fu);
}

static const float _HG_SEMITONE_LUT[12] = {
  1.000000000f, 1.059463094f, 1.122462048f, 1.189207115f,
  1.259921050f, 1.334839854f, 1.414213562f, 1.498307077f,
  1.587401052f, 1.681792831f, 1.781797436f, 1.887748625f
};

static inline float _hg_octmul(int oct){
  if (oct == 0) return 1.0f;
  if (oct > 0){
    float m = 1.0f; while (oct--) m *= 2.0f; return m;
  } else {
    float m = 1.0f; while (oct++) m *= 0.5f; return m;
  }
}

static unsigned char _hg_pick_bits_low_to_high(unsigned mask, unsigned char outIdx[6], unsigned char maxOut){
  unsigned char c = 0;
  for (unsigned char i = 0; i < 12 && c < maxOut; ++i){
    if (mask & (1u << i)) outIdx[c++] = i;
  }
  return c;
}

static inline unsigned HexGlyphHarmony_mask(void){
  unsigned mask = HEX_GLYPH & 0x0FFFu;
  if (ROOT_ROTATION_SEMITONES != 0){
    mask = _hg_rol12(mask, ROOT_ROTATION_SEMITONES);
  }
  if (INCLUDE_ROOT_IF_ABSENT && ((mask & 0x001u) == 0u)){
    mask |= 0x001u;
  }
  return mask;
}

static inline unsigned char HexGlyphHarmony_popcount(void){
  return _hg_pop12(HexGlyphHarmony_mask());
}

static inline unsigned char HexGlyphHarmony_indices(unsigned char outIdx[6]){
  if (!outIdx){
    return 0;
  }
  const unsigned char maxVoices = (VOICE_COUNT > 6u) ? 6u : (unsigned char)VOICE_COUNT;
  if (maxVoices == 0u){
    return 0;
  }
  unsigned mask = HexGlyphHarmony_mask();
  return _hg_pick_bits_low_to_high(mask, outIdx, maxVoices);
}

static inline signed char _hg_octave_from_ring(unsigned int ring){
  if (ring == 0u){
    return 0;
  }
  unsigned int magnitude = (ring + 1u) >> 1; // sequence 1,1,2,2,3...
  signed char offset = (signed char)magnitude;
  if ((ring & 1u) == 0u){
    offset = (signed char)(-offset);
  }
  if (offset > 4) offset = 4;
  else if (offset < -4) offset = -4;
  return offset;
}

static inline unsigned char HexGlyphHarmony_multipliers(float outMul[6]){
  if (!outMul){
    return 0;
  }
  const unsigned char target = (VOICE_COUNT > 6u) ? 6u : (unsigned char)VOICE_COUNT;
  if (target == 0u){
    return 0;
  }
  unsigned char idx[6];
  unsigned char semiCount = HexGlyphHarmony_indices(idx);
  if (semiCount == 0u){
    return 0;
  }
  unsigned char assigned = 0u;
#if SPREAD_OCTAVES
  const unsigned char maxRing = 8u; // covers octave offsets: 0, ±1, ±2, ±3, ±4
  for (unsigned char ring = 0u; ring <= maxRing && assigned < target; ++ring){
    signed char octave = _hg_octave_from_ring(ring);
    float octaveMul = _hg_octmul(octave);
    for (unsigned char i = 0u; i < semiCount && assigned < target; ++i){
      unsigned char semi = idx[i];
      outMul[assigned++] = _HG_SEMITONE_LUT[semi] * octaveMul;
    }
  }
  return assigned;
#else
  for (unsigned char v = 0u; v < target; ++v){
    unsigned char semi = idx[v % semiCount];
    unsigned int ring = (unsigned int)(v / semiCount);
    signed char octave = _hg_octave_from_ring(ring);
    outMul[v] = _HG_SEMITONE_LUT[semi] * _hg_octmul(octave);
  }
  return target;
#endif
}

static inline unsigned char HexGlyphHarmony_compute(float baseHz, float outHz[6]){
  if (!outHz){
    return 0;
  }
  if (!(baseHz > 0.0f)){ // treat NaN and <=0 as zero
    baseHz = 0.0f;
  }
  float mul[6];
  unsigned char count = HexGlyphHarmony_multipliers(mul);
  for (unsigned char v = 0; v < count; ++v){
    outHz[v] = baseHz * mul[v];
  }
  return count;
}

// Runtime-parameterized versions (do not rely on macros)
static inline unsigned _hg_mask_from(unsigned mask, int root_rotation, int include_root){
  mask &= 0x0FFFu;
  if (root_rotation != 0){
    mask = _hg_rol12(mask, root_rotation);
  }
  if (include_root && ((mask & 0x001u) == 0u)){
    mask |= 0x001u;
  }
  return mask;
}

static inline unsigned char HexGlyphHarmony_indices_from(unsigned mask, unsigned char maxVoices, int root_rotation, int include_root, unsigned char outIdx[6]){
  if (!outIdx){ return 0; }
  if (maxVoices == 0u){ return 0; }
  if (maxVoices > 6u) maxVoices = 6u;
  unsigned m = _hg_mask_from(mask, root_rotation, include_root);
  return _hg_pick_bits_low_to_high(m, outIdx, maxVoices);
}

static inline unsigned char HexGlyphHarmony_multipliers_from(unsigned mask, unsigned char voiceCount, int root_rotation, int include_root, int spread_octaves, float outMul[6]){
  if (!outMul){ return 0; }
  if (voiceCount == 0u){ return 0; }
  if (voiceCount > 6u) voiceCount = 6u;
  unsigned char idx[6];
  unsigned char semiCount = HexGlyphHarmony_indices_from(mask, voiceCount, root_rotation, include_root, idx);
  if (semiCount == 0u){ return 0; }

  unsigned char assigned = 0u;
  if (spread_octaves){
    const unsigned char maxRing = 8u;
    for (unsigned char ring = 0u; ring <= maxRing && assigned < voiceCount; ++ring){
      signed char octave = _hg_octave_from_ring(ring);
      float octaveMul = _hg_octmul(octave);
      for (unsigned char i = 0u; i < semiCount && assigned < voiceCount; ++i){
        unsigned char semi = idx[i];
        outMul[assigned++] = _HG_SEMITONE_LUT[semi] * octaveMul;
      }
    }
    return assigned;
  } else {
    for (unsigned char v = 0u; v < voiceCount; ++v){
      unsigned char semi = idx[v % semiCount];
      unsigned int ring = (unsigned int)(v / semiCount);
      signed char octave = _hg_octave_from_ring(ring);
      outMul[v] = _HG_SEMITONE_LUT[semi] * _hg_octmul(octave);
    }
    return voiceCount;
  }
}

static inline unsigned char HexGlyphHarmony_compute_from(float baseHz, unsigned mask, unsigned char voiceCount, int root_rotation, int include_root, int spread_octaves, float outHz[6]){
  if (!outHz){ return 0; }
  if (!(baseHz > 0.0f)){ baseHz = 0.0f; }
  float mul[6];
  unsigned char count = HexGlyphHarmony_multipliers_from(mask, voiceCount, root_rotation, include_root, spread_octaves, mul);
  for (unsigned char v = 0; v < count; ++v){
    outHz[v] = baseHz * mul[v];
  }
  return count;
}

#ifdef __cplusplus
}
#endif
