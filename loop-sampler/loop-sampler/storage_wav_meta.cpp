#include <Arduino.h>
#include <SdFat.h>
#include <string.h>
#include "storage_wav_meta.h"

extern SdFat sd;

namespace sf {

#pragma pack(push,1)
struct RiffHdr { char riff[4]; uint32_t size; char wave[4]; };
struct ChunkHdr { char id[4]; uint32_t size; };
struct FmtPCM  { uint16_t fmtTag; uint16_t channels; uint32_t sampleRate;
                 uint32_t byteRate; uint16_t blockAlign; uint16_t bitsPerSample; };
#pragma pack(pop)

static bool eq4(const char* a, const char* b){
  return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3];
}

bool wav_read_info(const char* path, WavInfo& out) {
  out = {}; out.ok = false;

  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return false;

  RiffHdr rh;
  if (f.read(&rh, sizeof(rh)) != sizeof(rh)) { f.close(); return false; }
  if (!eq4(rh.riff, "RIFF") || !eq4(rh.wave, "WAVE"))   { f.close(); return false; }

  bool haveFmt=false, haveData=false;
  FmtPCM fmt = {};
  uint32_t dataSize=0, dataOffset=0;

  while (f.available()) {
    ChunkHdr ch;
    if (f.read(&ch, sizeof(ch)) != sizeof(ch)) break;

    if (eq4(ch.id, "fmt ")) {
      if (ch.size >= sizeof(FmtPCM)) {
        if (f.read(&fmt, sizeof(FmtPCM)) != sizeof(FmtPCM)) break;
        if (ch.size > sizeof(FmtPCM)) f.seekCur(ch.size - sizeof(FmtPCM));
        haveFmt = true;
      } else {
        f.seekCur(ch.size);
      }
    } else if (eq4(ch.id, "data")) {
      dataSize = ch.size;
      dataOffset = (uint32_t)f.position();
      f.seekCur(ch.size);
      haveData = true;
    } else {
      f.seekCur(ch.size);
    }

    if (haveFmt && haveData) break;
  }

  f.close();

  if (!haveFmt || !haveData) return false;
  out.sampleRate    = fmt.sampleRate;
  out.numChannels   = fmt.channels;
  out.bitsPerSample = fmt.bitsPerSample;
  out.dataSize      = dataSize;
  out.dataOffset    = dataOffset;
  out.ok            = true;
  return true;
}

} // namespace sf
