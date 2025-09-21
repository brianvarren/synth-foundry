/**
 * @file context_params.cpp
 * @brief Context parameters implementation for XYLEM synthesizer
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "context_params.h"
#include <cstring>

// ── Global Context Parameters Instance ─────────────────────────────────────────────
ContextParams contextParams;

// ── Context Parameter Names ────────────────────────────────────────────────────────
static const char* paramNames[] = {
  "consonance",
  "precision", 
  "pace",
  "density",
  "root_note",
  "tempo"
};

// ── Note Names ──────────────────────────────────────────────────────────────────────
static const char* noteNames[] = {
  "C", "C#", "D", "D#", "E", "F", 
  "F#", "G", "G#", "A", "A#", "B"
};

// ── Context Parameter Functions ────────────────────────────────────────────────────

void initContextParams() {
  // Initialize with default values (already set in constructor)
  contextParams = ContextParams();
}

bool setContextParam(const char* paramName, uint8_t value) {
  if (!paramName) return false;
  
  if (strcmp(paramName, "consonance") == 0) {
    contextParams.consonance = value;
    return true;
  }
  else if (strcmp(paramName, "precision") == 0) {
    contextParams.precision = value;
    return true;
  }
  else if (strcmp(paramName, "pace") == 0) {
    contextParams.pace = value;
    return true;
  }
  else if (strcmp(paramName, "density") == 0) {
    contextParams.density = value;
    return true;
  }
  else if (strcmp(paramName, "root_note") == 0) {
    contextParams.root_note = value % 12; // Ensure valid note range (0-11)
    return true;
  }
  
  return false; // Invalid parameter name
}

bool setTempo(float tempo) {
  if (tempo < 30.0f || tempo > 300.0f) {
    return false; // Invalid tempo range
  }
  contextParams.tempo = tempo;
  return true;
}

float getTempo() {
  return contextParams.tempo;
}

uint8_t getContextParam(const char* paramName) {
  if (!paramName) return 0;
  
  if (strcmp(paramName, "consonance") == 0) {
    return contextParams.consonance;
  }
  else if (strcmp(paramName, "precision") == 0) {
    return contextParams.precision;
  }
  else if (strcmp(paramName, "pace") == 0) {
    return contextParams.pace;
  }
  else if (strcmp(paramName, "density") == 0) {
    return contextParams.density;
  }
  else if (strcmp(paramName, "root_note") == 0) {
    return contextParams.root_note;
  }
  
  return 0; // Invalid parameter name
}

const char* getContextParamName(uint8_t index) {
  if (index >= 5) return nullptr;
  return paramNames[index];
}

uint8_t getContextParamValue(uint8_t index) {
  switch (index) {
    case 0: return contextParams.consonance;
    case 1: return contextParams.precision;
    case 2: return contextParams.pace;
    case 3: return contextParams.density;
    case 4: return contextParams.root_note;
    default: return 0;
  }
}

uint8_t getContextParamCount() {
  return 6;
}

const char* getNoteName(uint8_t noteNumber) {
  if (noteNumber >= 12) return "?";
  return noteNames[noteNumber];
}
