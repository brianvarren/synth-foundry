/**
 * SD Card Module using SdFat library on SPI1
 * Handles file operations, WAV parsing, and data loading
 */

#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SdFat.h>
#include <SPI.h>

// ─────────────────────── SD Card Pin Configuration ───────────────────────
#define SD_CS_PIN   9   // Chip Select pin for the SD card
#define SD_SCK_PIN  10  // Clock pin
#define SD_MOSI_PIN 11  // Master Out Slave In
#define SD_MISO_PIN 24  // Master In Slave Out

#define SD_SPI_SPEED 60000000UL  // 60 MHz SPI speed
#define SD_READ_BUFFER_SIZE (32 * 1024)  // 32KB read buffer

// ─────────────────────── WAV File Structures ───────────────────────
struct WavHeader {
  char     riff[4];         // "RIFF" magic bytes
  uint32_t fileSize;        // File size minus 8 bytes
  char     wave[4];         // "WAVE"
  char     fmt[4];          // "fmt " chunk identifier
  uint32_t fmtSize;         // Format chunk size (usually 16 for PCM)
  uint16_t audioFormat;     // PCM = 1
  uint16_t numChannels;     // 1 = mono, 2 = stereo
  uint32_t sampleRate;      // Samples per second (e.g. 44100)
  uint32_t byteRate;        // Bytes per second
  uint16_t blockAlign;      // Bytes per frame
  uint16_t bitsPerSample;   // Usually 8 or 16
};

struct WavInfo {
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint16_t bitsPerSample;
  uint32_t dataSize;
  uint32_t dataOffset;  // Offset to start of audio data
  bool valid;
};

class SDCardClass {
private:
  SdFat* sd;
  SdSpiConfig* cfg;
  bool initialized;
  float cardSizeMB;
  
  // ───────────────── Helper: Get Filename from SdFile ─────────────────
  String fileNameToString(SdFile& file) {
    char buf[64];
    file.getName(buf, sizeof(buf));
    return String(buf);
  }
  
public:
  SDCardClass() : 
    sd(nullptr), 
    cfg(nullptr), 
    initialized(false),
    cardSizeMB(0) {}
  
  ~SDCardClass() {
    if (sd) delete sd;
    if (cfg) delete cfg;
  }
  
  // ───────────────── Initialization ─────────────────
  bool begin() {
    Serial.println("Initializing SD card on SPI1...");
    
    // Configure SPI1 pins
    SPI1.setSCK(SD_SCK_PIN);
    SPI1.setTX(SD_MOSI_PIN);
    SPI1.setRX(SD_MISO_PIN);
    SPI1.begin();
    
    // Create SdFat configuration for SPI1
    cfg = new SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SPI_SPEED, &SPI1);
    sd = new SdFat();
    
    if (!sd || !cfg) {
      Serial.println("Failed to allocate SD objects");
      return false;
    }
    
    // Initialize SD card
    if (!sd->begin(*cfg)) {
      Serial.println("SD card initialization failed");
      return false;
    }
    
    // Get card size
    cardSizeMB = sd->card()->sectorCount() / 2048.0;
    initialized = true;
    
    Serial.printf("SD card initialized. Size: %.1f MB\n", cardSizeMB);
    return true;
  }
  
  // ───────────────── File Listing ─────────────────
  int listWavFiles(String* fileList, uint32_t* sizeList, int maxFiles) {
    if (!initialized) return 0;
    
    SdFile root;
    if (!root.open("/")) {
      Serial.println("Failed to open root directory");
      return 0;
    }
    
    int count = 0;
    SdFile file;
    
    while (file.openNext(&root, O_RDONLY) && count < maxFiles) {
      if (!file.isDir()) {
        String name = fileNameToString(file);
        String nameLower = name;
        nameLower.toLowerCase();
        
        if (nameLower.endsWith(".wav")) {
          fileList[count] = name;
          sizeList[count] = file.fileSize();
          count++;
        }
      }
      file.close();
    }
    
    root.close();
    return count;
  }
  
  // ───────────────── Get First WAV File ─────────────────
  String getFirstWavFile() {
    if (!initialized) return "";
    
    SdFile root;
    if (!root.open("/")) return "";
    
    SdFile file;
    String result = "";
    
    while (file.openNext(&root, O_RDONLY)) {
      String name = fileNameToString(file);
      if (!file.isDir() && name.endsWith(".wav")) {
        result = name;
        file.close();
        break;
      }
      file.close();
    }
    
    root.close();
    return result;
  }
  
  // ───────────────── WAV File Parsing ─────────────────
  bool getWavInfo(const char* filename, WavInfo& info) {
    if (!initialized) return false;
    
    info.valid = false;
    
    SdFile file;
    if (!file.open(filename, O_RDONLY)) {
      Serial.printf("Failed to open %s\n", filename);
      return false;
    }
    
    // Read WAV header
    WavHeader header;
    if (file.read(&header, sizeof(header)) != sizeof(header)) {
      file.close();
      return false;
    }
    
    // Validate header
    if (strncmp(header.riff, "RIFF", 4) != 0 || 
        strncmp(header.wave, "WAVE", 4) != 0) {
      Serial.println("Invalid WAV header");
      file.close();
      return false;
    }
    
    // Store format info
    info.audioFormat = header.audioFormat;
    info.numChannels = header.numChannels;
    info.sampleRate = header.sampleRate;
    info.bitsPerSample = header.bitsPerSample;
    
    // Find data chunk
    char chunkId[4];
    uint32_t chunkSize;
    bool foundData = false;
    
    while (file.available() >= 8) {
      file.read(chunkId, 4);
      file.read(&chunkSize, 4);
      
      if (strncmp(chunkId, "data", 4) == 0) {
        info.dataSize = chunkSize;
        info.dataOffset = file.curPosition();
        foundData = true;
        break;
      }
      
      // Skip this chunk
      file.seekSet(file.curPosition() + chunkSize);
    }
    
    file.close();
    
    if (foundData) {
      info.valid = true;
      return true;
    }
    
    return false;
  }
  
  // ───────────────── Load WAV Data ─────────────────
  float loadWavData(const char* filename, uint8_t* buffer, uint32_t bufferSize, uint32_t* bytesRead) {
    if (!initialized || !buffer) {
      *bytesRead = 0;
      return 0;
    }
    
    WavInfo info;
    if (!getWavInfo(filename, info)) {
      *bytesRead = 0;
      return 0;
    }
    
    SdFile file;
    if (!file.open(filename, O_RDONLY)) {
      *bytesRead = 0;
      return 0;
    }
    
    // Seek to data start
    file.seekSet(info.dataOffset);
    
    // Allocate temporary read buffer
    uint8_t* readBuf = (uint8_t*)malloc(SD_READ_BUFFER_SIZE);
    if (!readBuf) {
      file.close();
      *bytesRead = 0;
      return 0;
    }
    
    // Read data in chunks
    uint32_t totalRead = 0;
    uint32_t toRead = min(bufferSize, info.dataSize);
    uint32_t startTime = micros();
    
    while (totalRead < toRead) {
      uint32_t chunkSize = min((uint32_t)SD_READ_BUFFER_SIZE, toRead - totalRead);
      int bytesReadNow = file.read(readBuf, chunkSize);
      
      if (bytesReadNow <= 0) break;
      
      memcpy(buffer + totalRead, readBuf, bytesReadNow);
      totalRead += bytesReadNow;
    }
    
    uint32_t elapsedTime = micros() - startTime;
    
    free(readBuf);
    file.close();
    
    *bytesRead = totalRead;
    
    // Return speed in MB/s
    float elapsedSeconds = elapsedTime / 1000000.0;
    float sizeMB = totalRead / 1048576.0;
    return (elapsedSeconds > 0) ? (sizeMB / elapsedSeconds) : 0;
  }
  
  // ───────────────── Utility Methods ─────────────────
  static String formatSize(uint32_t sizeBytes) {
    if (sizeBytes < 1024) {
      return String(sizeBytes) + " B";
    } else if (sizeBytes < 1048576) {
      return String(sizeBytes / 1024) + " KB";
    } else {
      return String(sizeBytes / 1048576.0, 2) + " MB";
    }
  }
  
  bool exists(const char* filename) {
    if (!initialized) return false;
    return sd->exists(filename);
  }
  
  bool remove(const char* filename) {
    if (!initialized) return false;
    return sd->remove(filename);
  }
  
  bool mkdir(const char* path) {
    if (!initialized) return false;
    return sd->mkdir(path);
  }
  
  float getCardSizeMB() const { return cardSizeMB; }
  bool isInitialized() const { return initialized; }
  
  // ───────────────── File Writing (for future use) ─────────────────
  bool writeFile(const char* filename, const uint8_t* data, uint32_t size) {
    if (!initialized) return false;
    
    SdFile file;
    if (!file.open(filename, O_WRONLY | O_CREAT | O_TRUNC)) {
      return false;
    }
    
    uint32_t written = file.write(data, size);
    file.close();
    
    return (written == size);
  }
  
  // ───────────────── Directory iteration ─────────────────
  template<typename Callback>
  void forEachFile(const char* path, Callback callback) {
    if (!initialized) return;
    
    SdFile dir;
    if (!dir.open(path)) return;
    
    SdFile file;
    while (file.openNext(&dir, O_RDONLY)) {
      if (!file.isDir()) {
        String name = fileNameToString(file);
        uint32_t size = file.fileSize();
        callback(name, size);
      }
      file.close();
    }
    
    dir.close();
  }
};

// Global SD card instance
extern SDCardClass SDCard;
SDCardClass SDCard;

#endif // SDCARD_H