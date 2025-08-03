#include <Arduino.h>   // Core Arduino functions and types
#include <SdFat.h>     // Fast SD card library (superior to built-in SD)
#include <SPI.h>       // SPI bus communication

// ─────────────────────── SD Card Wiring (SPI1 on RP2040) ───────────────────────
// These are custom SPI1 pins for Olimex Pico2-XXL (not default SPI0)
const int SD_CS_PIN   = 9;   // Chip Select pin for the SD card
const int SD_SCK_PIN  = 10;  // Clock pin
const int SD_MOSI_PIN = 11;  // Master Out Slave In
const int SD_MISO_PIN = 24;  // Master In Slave Out

const uint32_t SPI_SPEED = 60'000'000; // Set SPI speed (60 MHz); fast, but may be tuned lower

// ───────────────────────── WAV File Header Struct ─────────────────────────────
// Basic uncompressed WAV header layout. Only PCM format expected.
struct WavHeader {
  char     riff[4];         // "RIFF" magic bytes
  uint32_t fileSize;        // File size minus 8 bytes
  char     wave[4];         // "WAVE"
  char     fmt[4];          // "fmt " chunk identifier
  uint32_t fmtSize;         // Format chunk size (usually 16 for PCM)
  uint16_t audioFormat;     // PCM = 1
  uint16_t numChannels;     // 1 = mono, 2 = stereo
  uint32_t sampleRate;      // Samples per second (e.g. 44100)
  uint32_t byteRate;        // Bytes per second (sampleRate * numChannels * bytes/sample)
  uint16_t blockAlign;      // Bytes per frame (channels * bytes/sample)
  uint16_t bitsPerSample;   // Usually 8 or 16
};

// ────────────────────────── Globals ───────────────────────────────────────────
SdFat SD;  // SdFat filesystem instance
SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SPI_SPEED, &SPI1);  // Config for SD on SPI1

const uint32_t BUF_SIZE = 32 * 1024;  // 32KB buffer for chunked read
uint8_t* audioData = nullptr;         // Pointer to allocated PSRAM for audio sample
uint32_t audioDataSize = 0;           // Number of sample bytes

// ───────────────────────── Helper: Convert Filename to String ─────────────────
static String fnameToString(SdFile& f) {
  char buf[32];                 // Buffer for filename
  f.getName(buf, sizeof(buf)); // Get the name of the open file
  return String(buf);          // Return as Arduino String
}

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
// Verifies PSRAM is present and prints available size
bool initPSRAM() {
  Serial.println("=== PSRAM ===");
  if (!rp2040.getPSRAMSize()) {
    Serial.println("❌ No PSRAM");
    return false;
  }
  Serial.printf("✅ %u MB, free %u KB\n",
      rp2040.getPSRAMSize() / 1048576,     // Total size in MB
      rp2040.getFreePSRAMHeap() / 1024);   // Free heap in KB
  return true;
}

// ───────────────────────── Initialize SD Card ─────────────────────────────────
// Configures SPI1 pins and mounts the SD card
bool initSD() {
  Serial.println("\n=== SD (SdFat SPI) ===");
  SPI1.setSCK(SD_SCK_PIN);     // Set custom SCK pin
  SPI1.setTX(SD_MOSI_PIN);     // Set custom MOSI pin
  SPI1.setRX(SD_MISO_PIN);     // Set custom MISO pin
  SPI1.begin();                // Begin SPI bus on SPI1

  if (!SD.begin(cfg)) {
    Serial.println("❌ SD init fail");
    return false;
  }

  Serial.printf("✅ Card size: %.1f MB\n", SD.card()->sectorCount() / 2048.0);
  return true;
}

// ───────────────────────── List WAV Files on SD Root ──────────────────────────
bool listWavFiles() {
  SdFile root;
  if (!root.open("/")) return false;

  bool found = false;
  SdFile f;
  while (f.openNext(&root, O_RDONLY)) {
    if (!f.isDir()) {
      String n = fnameToString(f);
      n.toLowerCase(); // Normalize case for comparison
      if (n.endsWith(".wav")) {
        Serial.printf("Found: %s (%u KB)\n", n.c_str(), f.fileSize() / 1024);
        found = true;
      }
    }
    f.close();
  }

  root.close();
  return found;
}

// ───────────────────────── Load First WAV File Found ──────────────────────────
bool loadFirstWav() {
  SdFile root;
  if (!root.open("/")) return false;

  SdFile f;
  String sel;

  // Search for first .wav file
  while (f.openNext(&root, O_RDONLY)) {
    sel = fnameToString(f);
    if (!f.isDir() && sel.endsWith(".wav")) break;
    f.close();
  }
  root.close();

  if (!f.isOpen()) return false;

  Serial.printf("\nLoading %s\n", sel.c_str());

  // Read WAV header
  WavHeader h;
  f.read(&h, sizeof(h));

  // Validate header signature
  if (strncmp(h.riff, "RIFF", 4) || strncmp(h.wave, "WAVE", 4)) {
    Serial.println("Bad WAV");
    return false;
  }

  // ───────── Locate "data" chunk ─────────
  char id[4];
  uint32_t sz;
  bool ok = false;

  while (f.available() >= 8) {
    f.read(id, 4);   // Chunk ID
    f.read(&sz, 4);  // Chunk size
    if (!strncmp(id, "data", 4)) {
      ok = true;
      audioDataSize = sz;
      break;
    }
    f.seekSet(f.curPosition() + sz); // Skip chunk
  }

  if (!ok) {
    Serial.println("No data chunk");
    return false;
  }

  if (audioDataSize > rp2040.getFreePSRAMHeap()) {
    Serial.println("PSRAM too small");
    return false;
  }

  // ───────── Allocate PSRAM buffer ─────────
  audioData = (uint8_t*)pmalloc(audioDataSize);
  if (!audioData) {
    Serial.println("Alloc fail");
    return false;
  }

  // ───────── Chunked Read into PSRAM ───────
  Serial.printf("Reading %u bytes…\n", audioDataSize);
  uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
  uint32_t read = 0;
  uint32_t t0 = micros();

  while (read < audioDataSize) {
    uint32_t n = min<uint32_t>(BUF_SIZE, audioDataSize - read);
    int r = f.read(buf, n);
    if (r <= 0) break;
    memcpy(audioData + read, buf, r);
    read += r;
  }

  free(buf);
  f.close();

  float dt = (micros() - t0) / 1e6; // Convert microseconds to seconds
  Serial.printf("Rate: %.2f MB/s\n", (read / 1048576.0) / dt);

  return true;
}

// ───────────────────────── Dump First 32 Bytes of Audio ───────────────────────
void dump() {
  if (!audioData) return;
  Serial.println("\nFirst 32 bytes:");
  for (int i = 0; i < 32; i++) {
    Serial.printf("%02X ", audioData[i]);
    if ((i + 1) % 16 == 0) Serial.println();  // New line every 16 bytes
  }
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);  // Start serial communication
  while (!Serial);       // Wait for Serial monitor to open (useful for USB CDC)

  if (!initPSRAM()) return;
  if (!initSD()) return;
  if (!listWavFiles()) {
    Serial.println("No WAVs");
    return;
  }
  if (!loadFirstWav()) return;

  dump();  // Show first 32 bytes of loaded sample
}

// ───────────────────────── Empty Loop (One-shot sketch) ───────────────────────
void loop() {}
