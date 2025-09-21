/**
 * @file context_params.h
 * @brief Context parameters for XYLEM synthesizer
 * 
 * This header defines the four main context parameters that control
 * the synthesizer's behavior and can be adjusted via serial commands.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <cstdint>

// ── Context Parameter Structure ────────────────────────────────────────────────────
// This structure is designed to be compatible with UART packet decoding
struct ContextParams {
  uint8_t consonance;  // 0-255: Harmonic consonance level
  uint8_t precision;   // 0-255: Timing precision
  uint8_t pace;        // 0-255: Tempo/pace control
  uint8_t density;     // 0-255: Note/event density
  uint8_t root_note;   // 0-11: Root note (0=C, 1=C#, 2=D, etc.)
  float tempo;         // Tempo in BPM (e.g., 120.00)
  
  // Default constructor with sensible defaults
  ContextParams() : consonance(128), precision(3), pace(69), density(255), root_note(0), tempo(120.0f) {}
  
  // Method to update from UART packet data (for future implementation)
  void updateFromPacket(const uint8_t* packetData) {
    consonance = packetData[0];
    precision = packetData[1];
    pace = packetData[2];
    density = packetData[3];
    root_note = packetData[4] % 12; // Ensure valid note range
  }
  
  // Method to get packet data (for future implementation)
  void getPacketData(uint8_t* packetData) const {
    packetData[0] = consonance;
    packetData[1] = precision;
    packetData[2] = pace;
    packetData[3] = density;
    packetData[4] = root_note;
  }
};

// ── Global Context Parameters Instance ─────────────────────────────────────────────
extern ContextParams contextParams;

// ── Context Parameter Functions ────────────────────────────────────────────────────

/**
 * @brief Initialize context parameters with default values
 */
void initContextParams();

/**
 * @brief Set a context parameter by name
 * 
 * @param paramName Name of the parameter ("consonance", "precision", "pace", "density")
 * @param value New value (0-255)
 * @return true if parameter was set successfully, false if invalid name or value
 */
bool setContextParam(const char* paramName, uint8_t value);

/**
 * @brief Set tempo parameter
 * @param tempo Tempo in BPM (e.g., 120.0)
 * @return true if tempo was set successfully
 */
bool setTempo(float tempo);

/**
 * @brief Get tempo parameter
 * @return Current tempo in BPM
 */
float getTempo();

/**
 * @brief Get a context parameter value by name
 * 
 * @param paramName Name of the parameter
 * @return Parameter value, or 0 if invalid name
 */
uint8_t getContextParam(const char* paramName);

/**
 * @brief Get parameter name by index
 * 
 * @param index Parameter index (0-3)
 * @return Parameter name string, or nullptr if invalid index
 */
const char* getContextParamName(uint8_t index);

/**
 * @brief Get parameter value by index
 * 
 * @param index Parameter index (0-3)
 * @return Parameter value, or 0 if invalid index
 */
uint8_t getContextParamValue(uint8_t index);

/**
 * @brief Get total number of context parameters
 * 
 * @return Number of parameters (5)
 */
uint8_t getContextParamCount();

/**
 * @brief Get note name from note number
 * 
 * @param noteNumber Note number (0-11)
 * @return Note name string (e.g., "C", "C#", "D")
 */
const char* getNoteName(uint8_t noteNumber);
