/**
 * @file command_parser.cpp
 * @brief Serial command parser implementation for XYLEM synthesizer
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "command_parser.h"
#include "context_params.h"
#include "LocalMetronome.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ── Command Parser State ───────────────────────────────────────────────────────────
static char commandBuffer[64];
static uint8_t bufferIndex = 0;
static bool showBeatUpdates = false;  // Default to showing beat updates
static void* metroPtr = nullptr;     // Metronome reference for tempo control

// ── Command Parser Functions ──────────────────────────────────────────────────────

void initCommandParser() {
  bufferIndex = 0;
  memset(commandBuffer, 0, sizeof(commandBuffer));
}

bool shouldShowBeatUpdates() {
  return showBeatUpdates;
}

void setMetronomeReference(void* metroPtr) {
  ::metroPtr = metroPtr;
}

void processSerialCommands() {
  
  while (Serial.available()) {
    char c = Serial.read();
    Serial.printf("Read char: %c (0x%02x)\n", c, c); // Debug each character
    
    // Handle newline/carriage return as command terminator
    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0) {
        commandBuffer[bufferIndex] = '\0'; // Null terminate
        Serial.printf("Processing command: '%s'\n", commandBuffer); // Debug output
        parseCommand(commandBuffer);
        bufferIndex = 0; // Reset buffer
        memset(commandBuffer, 0, sizeof(commandBuffer));
      }
    }
    // Handle backspace
    else if (c == '\b' || c == 127) {
      if (bufferIndex > 0) {
        bufferIndex--;
        commandBuffer[bufferIndex] = '\0';
      }
    }
    // Add character to buffer if there's space
    else if (bufferIndex < sizeof(commandBuffer) - 1) {
      commandBuffer[bufferIndex] = c;
      bufferIndex++;
    }
  }
}

bool parseCommand(const char* commandLine) {
  if (!commandLine || strlen(commandLine) == 0) {
    return false;
  }
  
  // Skip leading whitespace
  while (*commandLine == ' ' || *commandLine == '\t') {
    commandLine++;
  }
  
  // Parse parameter setting commands: "paramname value"
  char paramName[32];
  int value;
  
  if (sscanf(commandLine, "%31s %d", paramName, &value) == 2) {
    // Skip tempo - it's handled by the dedicated tempo command
    if (strcmp(paramName, "tempo") == 0) {
      // Let it fall through to the tempo command handler below
    } else {
      // Validate value range based on parameter type
      if (strcmp(paramName, "root_note") == 0) {
        if (value < 0 || value > 11) {
          Serial.printf("Error: Root note must be 0-11, got %d\n", value);
          return true;
        }
      } else {
        if (value < 0 || value > 255) {
          Serial.printf("Error: Value must be 0-255, got %d\n", value);
          return true;
        }
      }
    
      // Try to set the parameter
      if (setContextParam(paramName, (uint8_t)value)) {
        Serial.printf("Set %s to %d\n", paramName, value);
        return true;
      } else {
        Serial.printf("Error: Unknown parameter '%s'\n", paramName);
        return true;
      }
    }
  }
  
  // Parse single commands
  if (strcmp(commandLine, "help") == 0) {
    showHelp();
    return true;
  }
  else if (strcmp(commandLine, "params") == 0) {
    showContextParams();
    return true;
  }
  else if (strcmp(commandLine, "status") == 0) {
    showContextParams();
    return true;
  }
  else if (strcmp(commandLine, "clear") == 0) {
    Serial.println("---");
    return true;
  }
  else if (strcmp(commandLine, "test") == 0) {
    Serial.println("Command parser is working!");
    return true;
  }
  else if (strcmp(commandLine, "reset") == 0) {
    Serial.println("Metronome reset command - need to implement");
    return true;
  }
  else if (strcmp(commandLine, "start") == 0) {
    Serial.println("Metronome start command - need to implement");
    return true;
  }
  else if (strcmp(commandLine, "stop") == 0) {
    Serial.println("Metronome stop command - need to implement");
    return true;
  }
  else if (strcmp(commandLine, "beat") == 0) {
    showBeatUpdates = !showBeatUpdates;
    Serial.printf("Beat updates %s\n", showBeatUpdates ? "enabled" : "disabled");
    return true;
  }
  else if (strncmp(commandLine, "tempo ", 6) == 0) {
    float tempo = atof(commandLine + 6);
    if (setTempo(tempo)) {
      // Update metronome tempo if reference is available
      if (metroPtr) {
        LocalMetronome* metro = static_cast<LocalMetronome*>(metroPtr);
        metro->setBPM(tempo);
      }
      Serial.printf("Tempo set to %.2f BPM\n", tempo);
    } else {
      Serial.println("Invalid tempo. Range: 30.0 - 300.0 BPM");
    }
    return true;
  }
  
  // Unknown command
  Serial.printf("Unknown command: '%s'. Type 'help' for available commands.\n", commandLine);
  return false;
}

void showHelp() {
  Serial.println("XYLEM Synthesizer Commands:");
  Serial.println("  consonance <0-255>  - Set harmonic consonance level");
  Serial.println("  precision <0-255>   - Set timing precision");
  Serial.println("  pace <0-255>        - Set tempo/pace control");
  Serial.println("  density <0-255>     - Set note/event density");
  Serial.println("  root_note <0-11>    - Set root note (0=C, 1=C#, 2=D, etc.)");
  Serial.println("  tempo <30.0-300.0>  - Set tempo in BPM (e.g., 120.50)");
  Serial.println("  params              - Show current parameter values");
  Serial.println("  status              - Show current parameter values");
  Serial.println("  reset               - Reset metronome");
  Serial.println("  start               - Start metronome");
  Serial.println("  stop                - Stop metronome");
  Serial.println("  beat                - Toggle beat update messages");
  Serial.println("  help                - Show this help");
  Serial.println("  clear               - Clear screen");
  Serial.println("");
  Serial.println("Examples:");
  Serial.println("  consonance 128");
  Serial.println("  precision 3");
  Serial.println("  pace 69");
  Serial.println("  density 255");
  Serial.println("  root_note 0");
  Serial.println("  reset");
}

void showContextParams() {
  Serial.println("Current Context Parameters:");
  Serial.printf("  consonance: %d\n", contextParams.consonance);
  Serial.printf("  precision:  %d\n", contextParams.precision);
  Serial.printf("  pace:       %d\n", contextParams.pace);
  Serial.printf("  density:    %d\n", contextParams.density);
  Serial.printf("  root_note:  %d (%s)\n", contextParams.root_note, getNoteName(contextParams.root_note));
  Serial.printf("  tempo:      %.2f BPM\n", getTempo());
}
