#include <Arduino.h>
#include <SdFat.h>
#include <SPI.h>

// ──────────────────────────── SD-card wiring (SPI) ────────────────────────────
const int SD_CS_PIN   = 9;
const int SD_SCK_PIN  = 10;
const int SD_MOSI_PIN = 11;
const int SD_MISO_PIN = 24;
const uint32_t SPI_SPEED = 60'000'000;   // 25 MHz

// ───────────────────────── WAV header ─────────────────────────
struct WavHeader {
  char riff[4];       uint32_t fileSize;   char wave[4];
  char fmt[4];        uint32_t fmtSize;    uint16_t audioFormat;
  uint16_t numChannels; uint32_t sampleRate; uint32_t byteRate;
  uint16_t blockAlign; uint16_t bitsPerSample;
};

// ───────────────────────── Globals ────────────────────────────
SdFat SD;
SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SPI_SPEED, &SPI1);

const uint32_t BUF_SIZE = 32 * 1024;
uint8_t *audioData = nullptr;
uint32_t audioDataSize = 0;

// ───────────────────────── Helpers ────────────────────────────
static String fnameToString(SdFile& f) {
  char buf[32];
  f.getName(buf, sizeof(buf));
  return String(buf);
}
// ───────────────────────── Init PSRAM ─────────────────────────
bool initPSRAM() {
  Serial.println("=== PSRAM ===");
  if (!rp2040.getPSRAMSize()) { Serial.println("❌ No PSRAM"); return false; }
  Serial.printf("✅ %u MB, free %u KB\n",
      rp2040.getPSRAMSize()/1048576, rp2040.getFreePSRAMHeap()/1024);
  return true;
}
// ───────────────────────── Init SD (SPI) ──────────────────────
bool initSD() {
  Serial.println("\n=== SD (SdFat SPI) ===");
  SPI1.setSCK(SD_SCK_PIN); SPI1.setTX(SD_MOSI_PIN); SPI1.setRX(SD_MISO_PIN);
  SPI1.begin();
  if (!SD.begin(cfg)) { Serial.println("❌ SD init fail"); return false; }
  Serial.printf("✅ Card size: %.1f MB\n",
      SD.card()->sectorCount()/2048.0);
  return true;
}
// ───────────────────────── List WAVs ──────────────────────────
bool listWavFiles() {
  SdFile root; if (!root.open("/")) return false;
  bool found=false; SdFile f;
  while (f.openNext(&root, O_RDONLY)) {
    if (!f.isDir()) {
      String n = fnameToString(f); n.toLowerCase();
      if (n.endsWith(".wav")) {
        Serial.printf("Found: %s (%u KB)\n", n.c_str(), f.fileSize()/1024);
        found = true;
      }
    }
    f.close();
  }
  root.close();
  return found;
}
// ───────────────────────── Load first WAV ─────────────────────
bool loadFirstWav() {
  SdFile root; if (!root.open("/")) return false;
  SdFile f; String sel;
  while (f.openNext(&root, O_RDONLY)) {
    sel = fnameToString(f); if (!f.isDir() && sel.endsWith(".wav")) break;
    f.close();
  }
  root.close();
  if (!f.isOpen()) return false;

  Serial.printf("\nLoading %s\n", sel.c_str());
  WavHeader h; f.read(&h, sizeof(h));
  if (strncmp(h.riff,"RIFF",4)||strncmp(h.wave,"WAVE",4)){Serial.println("Bad WAV");return false;}

  // locate data chunk
  char id[4]; uint32_t sz; bool ok=false;
  while (f.available()>=8){ f.read(id,4); f.read(&sz,4);
    if(!strncmp(id,"data",4)){ ok=true; audioDataSize=sz; break; }
    f.seekSet(f.curPosition()+sz);
  }
  if(!ok){Serial.println("No data chunk");return false;}
  if(audioDataSize > rp2040.getFreePSRAMHeap()){Serial.println("PSRAM too small");return false;}

  audioData = (uint8_t*)pmalloc(audioDataSize);
  if(!audioData){Serial.println("Alloc fail"); return false;}

  Serial.printf("Reading %u bytes…\n",audioDataSize);
  uint8_t *buf=(uint8_t*)malloc(BUF_SIZE);
  uint32_t read=0,t0=micros();
  while(read<audioDataSize){
    uint32_t n=min<uint32_t>(BUF_SIZE,audioDataSize-read);
    int r=f.read(buf,n); if(r<=0) break;
    memcpy(audioData+read,buf,r); read+=r;
  }
  free(buf); f.close();
  float dt=(micros()-t0)/1e6;
  Serial.printf("Rate: %.2f MB/s\n", (read/1048576.0)/dt);
  return true;
}
// ───────────────────────── Display dump ───────────────────────
void dump() {
  if(!audioData) return;
  Serial.println("\nFirst 32 bytes:");
  for(int i=0;i<32;i++){ Serial.printf("%02X ",audioData[i]); if((i+1)%16==0)Serial.println();}
}
// ───────────────────────── Arduino ────────────────────────────
void setup() {
  Serial.begin(115200); while(!Serial);
  if(!initPSRAM())return;
  if(!initSD())return;
  if(!listWavFiles()) {Serial.println("No WAVs"); return;}
  if(!loadFirstWav()) return;
  dump();
}
void loop(){}
