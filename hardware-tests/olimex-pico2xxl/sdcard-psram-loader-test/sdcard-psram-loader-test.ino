#include <Arduino.h>   // Core Arduino functions and types
#include <SdFat.h>     // Fast SD card library (superior to built-in SD)
#include <SPI.h>       // SPI bus communication
#include <U8g2lib.h>   // Display library

// ─────────────────────── Display Configuration (SPI0) ───────────────────────
#define DISP_DC 2
#define DISP_RST 3
#define DISP_CS 5
#define DISP_SCL 6
#define DISP_SDA 7
U8G2_SH1122_256X64_F_4W_HW_SPI u8g2(U8G2_R0, DISP_CS, DISP_DC, DISP_RST);

// ─────────────────────── SD Card Wiring (SPI1 on RP2040) ───────────────────────
const int SD_CS_PIN   = 9;   // Chip Select pin for the SD card
const int SD_SCK_PIN  = 10;  // Clock pin
const int SD_MOSI_PIN = 11;  // Master Out Slave In
const int SD_MISO_PIN = 24;  // Master In Slave Out

const uint32_t SPI_SPEED = 60'000'000; // Set SPI speed (60 MHz)

// ───────────────────────── WAV File Header Struct ─────────────────────────────
struct WavHeader {
  char     riff[4];         // "RIFF" magic bytes
  uint32_t fileSize;        // File size minus 8 bytes
  char     wave[4];          // "WAVE"
  char     fmt[4];          // "fmt " chunk identifier
  uint32_t fmtSize;         // Format chunk size (usually 16 for PCM)
  uint16_t audioFormat;     // PCM = 1
  uint16_t numChannels;     // 1 = mono, 2 = stereo
  uint32_t sampleRate;      // Samples per second (e.g. 44100)
  uint32_t byteRate;        // Bytes per second
  uint16_t blockAlign;      // Bytes per frame
  uint16_t bitsPerSample;   // Usually 8 or 16
};

// ────────────────────────── Globals ───────────────────────────────────────────
SdFat SD;  // SdFat filesystem instance
SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SPI_SPEED, &SPI1);  // Config for SD on SPI1

const uint32_t BUF_SIZE = 32 * 1024;  // 32KB buffer for chunked read
uint8_t* audioData = nullptr;         // Pointer to allocated PSRAM for audio sample
uint32_t audioDataSize = 0;           // Number of sample bytes

// Display buffer for scrolling text
#define MAX_LINES 20
String displayLines[MAX_LINES];
int lineCount = 0;
int scrollOffset = 0;
bool needsRedraw = true;
unsigned long lastScrollTime = 0;
const unsigned long SCROLL_DELAY = 500; // Scroll every 500ms

// ───────────────────────── Helper: Add Line to Display Buffer ─────────────────
void addDisplayLine(String line) {
  Serial.println(line); // Also print to serial
  
  if (lineCount < MAX_LINES) {
    displayLines[lineCount++] = line;
  } else {
    // Shift lines up
    for (int i = 0; i < MAX_LINES - 1; i++) {
      displayLines[i] = displayLines[i + 1];
    }
    displayLines[MAX_LINES - 1] = line;
  }
  needsRedraw = true;
}

// ───────────────────────── Helper: Format size in KB/MB ─────────────────
String formatSize(uint32_t sizeBytes) {
  if (sizeBytes < 1024) {
    return String(sizeBytes) + " B";
  } else if (sizeBytes < 1048576) {
    return String(sizeBytes / 1024) + " KB";
  } else {
    return String(sizeBytes / 1048576.0, 1) + " MB";
  }
}

// ───────────────────────── Helper: Convert Filename to String ─────────────────
static String fnameToString(SdFile& f) {
  char buf[32];
  f.getName(buf, sizeof(buf));
  return String(buf);
}

// ───────────────────────── Initialize Display ───────────────────────────────────
void initDisplay() {
  addDisplayLine("=== Display ===");
  
  pinMode(DISP_RST, OUTPUT);
  digitalWrite(DISP_RST, HIGH);
  delay(5);
  digitalWrite(DISP_RST, LOW);
  delay(20);
  digitalWrite(DISP_RST, HIGH);
  delay(50);
  
  SPI.setSCK(DISP_SCL);
  SPI.setTX(DISP_SDA);
  SPI.begin();
  
  u8g2.begin();
  u8g2.setBusClock(8000000UL); // 8 MHz for stability
  u8g2.setContrast(180);
  u8g2.setFont(u8g2_font_5x7_tf); // Smaller font for more text
  
  addDisplayLine("✅ SH1122 Ready");
}

// ───────────────────────── Initialize PSRAM ───────────────────────────────────
bool initPSRAM() {
  addDisplayLine("=== PSRAM ===");
  if (!rp2040.getPSRAMSize()) {
    addDisplayLine("❌ No PSRAM");
    return false;
  }
  String msg = "✅ " + String(rp2040.getPSRAMSize() / 1048576) + " MB, free " + 
               String(rp2040.getFreePSRAMHeap() / 1024) + " KB";
  addDisplayLine(msg);
  return true;
}

// ───────────────────────── Initialize SD Card ─────────────────────────────────
bool initSD() {
  addDisplayLine("");
  addDisplayLine("=== SD Card ===");
  
  // Configure SPI1 pins
  SPI1.setSCK(SD_SCK_PIN);
  SPI1.setTX(SD_MOSI_PIN);
  SPI1.setRX(SD_MISO_PIN);
  SPI1.begin();

  if (!SD.begin(cfg)) {
    addDisplayLine("❌ SD init fail");
    return false;
  }

  float cardSizeMB = SD.card()->sectorCount() / 2048.0;
  addDisplayLine("✅ Card: " + String(cardSizeMB, 1) + " MB");
  return true;
}

// ───────────────────────── List WAV Files on SD Root ──────────────────────────
bool listWavFiles() {
  addDisplayLine("");
  addDisplayLine("=== WAV Files ===");
  
  SdFile root;
  if (!root.open("/")) {
    addDisplayLine("Can't open root");
    return false;
  }

  bool found = false;
  int fileCount = 0;
  SdFile f;
  
  while (f.openNext(&root, O_RDONLY)) {
    if (!f.isDir()) {
      String n = fnameToString(f);
      String origName = n;
      n.toLowerCase();
      
      if (n.endsWith(".wav")) {
        fileCount++;
        String sizeStr = formatSize(f.fileSize());
        addDisplayLine(String(fileCount) + ". " + origName + " (" + sizeStr + ")");
        found = true;
      }
    }
    f.close();
  }

  if (!found) {
    addDisplayLine("No WAV files found");
  } else {
    addDisplayLine("Total: " + String(fileCount) + " files");
  }

  root.close();
  return found;
}

// ───────────────────────── Load First WAV File Found ──────────────────────────
bool loadFirstWav() {
  addDisplayLine("");
  addDisplayLine("=== Loading WAV ===");
  
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

  if (!f.isOpen()) {
    addDisplayLine("No WAV to load");
    return false;
  }

  addDisplayLine("Loading: " + sel);

  // Read WAV header
  WavHeader h;
  f.read(&h, sizeof(h));

  // Validate header
  if (strncmp(h.riff, "RIFF", 4) || strncmp(h.wave, "WAVE", 4)) {
    addDisplayLine("Bad WAV format");
    return false;
  }

  // Display WAV info
  addDisplayLine("Ch: " + String(h.numChannels) + 
                ", Rate: " + String(h.sampleRate) + "Hz");
  addDisplayLine("Bits: " + String(h.bitsPerSample) + 
                ", Fmt: " + String(h.audioFormat));

  // Locate "data" chunk
  char id[4];
  uint32_t sz;
  bool ok = false;

  while (f.available() >= 8) {
    f.read(id, 4);
    f.read(&sz, 4);
    if (!strncmp(id, "data", 4)) {
      ok = true;
      audioDataSize = sz;
      break;
    }
    f.seekSet(f.curPosition() + sz);
  }

  if (!ok) {
    addDisplayLine("No data chunk");
    return false;
  }

  addDisplayLine("Data: " + formatSize(audioDataSize));

  if (audioDataSize > rp2040.getFreePSRAMHeap()) {
    addDisplayLine("PSRAM too small!");
    return false;
  }

  // Allocate PSRAM
  audioData = (uint8_t*)pmalloc(audioDataSize);
  if (!audioData) {
    addDisplayLine("Alloc failed");
    return false;
  }

  // Read data
  addDisplayLine("Reading...");
  updateDisplay(); // Force display update before long operation
  
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

  float dt = (micros() - t0) / 1e6;
  addDisplayLine("Rate: " + String((read / 1048576.0) / dt, 2) + " MB/s");
  addDisplayLine("✅ Loaded " + formatSize(read));

  return true;
}

// ───────────────────────── Display First Bytes of Audio ───────────────────────
void dumpAudio() {
  if (!audioData) return;
  
  addDisplayLine("");
  addDisplayLine("=== First 32 bytes ===");
  
  String hexLine = "";
  for (int i = 0; i < 32; i++) {
    char hex[4];
    sprintf(hex, "%02X ", audioData[i]);
    hexLine += hex;
    
    if ((i + 1) % 8 == 0) {
      addDisplayLine(hexLine);
      hexLine = "";
    }
  }
}

// ───────────────────────── Update Display ───────────────────────────────────
void updateDisplay() {
  u8g2.clearBuffer();
  
  // Draw border
  u8g2.drawFrame(0, 0, 256, 64);
  
  // Calculate how many lines fit on screen (with 5x7 font, ~7 lines)
  const int linesPerScreen = 7;
  const int lineHeight = 8;
  const int startY = 10;
  
  // Display lines with scrolling
  int displayStart = scrollOffset;
  int displayEnd = min(displayStart + linesPerScreen, lineCount);
  
  for (int i = displayStart; i < displayEnd; i++) {
    int y = startY + ((i - displayStart) * lineHeight);
    u8g2.drawStr(4, y, displayLines[i].c_str());
  }
  
  // Draw scroll indicator if needed
  if (lineCount > linesPerScreen) {
    int barHeight = max(2, (linesPerScreen * 60) / lineCount);
    int barY = 2 + ((scrollOffset * (60 - barHeight)) / (lineCount - linesPerScreen));
    u8g2.drawBox(250, barY, 4, barHeight);
  }
  
  u8g2.sendBuffer();
  needsRedraw = false;
}

// ───────────────────────── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000); // Give time for serial monitor to connect
  
  Serial.println("\n=== Olimex Pico2-XXL SD Card + Display Test ===\n");
  
  // Initialize display first so we can show progress
  initDisplay();
  updateDisplay();
  delay(500);
  
  // Initialize PSRAM
  if (!initPSRAM()) {
    updateDisplay();
    return;
  }
  updateDisplay();
  delay(500);
  
  // Initialize SD Card
  if (!initSD()) {
    updateDisplay();
    return;
  }
  updateDisplay();
  delay(500);
  
  // List WAV files
  if (!listWavFiles()) {
    addDisplayLine("");
    addDisplayLine("No WAVs to process");
    updateDisplay();
    return;
  }
  updateDisplay();
  delay(1000);
  
  // Load first WAV file
  if (!loadFirstWav()) {
    updateDisplay();
    return;
  }
  updateDisplay();
  delay(500);
  
  // Display first bytes
  dumpAudio();
  updateDisplay();
  
  addDisplayLine("");
  addDisplayLine("=== Complete! ===");
  addDisplayLine("↑/↓ to scroll");
  updateDisplay();
}

// ───────────────────────── Arduino Loop ───────────────────────────────────
void loop() {
  // Auto-scroll if there's more content than fits on screen
  unsigned long currentTime = millis();
  
  if (lineCount > 7 && currentTime - lastScrollTime > SCROLL_DELAY) {
    scrollOffset++;
    if (scrollOffset > lineCount - 7) {
      scrollOffset = 0; // Wrap around
    }
    needsRedraw = true;
    lastScrollTime = currentTime;
  }
  
  // Update display if needed
  if (needsRedraw) {
    updateDisplay();
  }
  
  delay(10);
}