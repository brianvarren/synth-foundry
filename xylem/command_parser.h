/**
 * @file command_parser.h
 * @brief Serial command parser for XYLEM synthesizer
 * 
 * This header provides the interface for parsing serial commands
 * to control synthesizer parameters and display information.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#pragma once
#include <cstdint>

// ── Command Parser Functions ──────────────────────────────────────────────────────

/**
 * @brief Initialize the command parser
 */
void initCommandParser();

/**
 * @brief Check if beat updates should be shown
 * @return true if beat updates should be displayed, false otherwise
 */
bool shouldShowBeatUpdates();

/**
 * @brief Set metronome reference for tempo control
 * @param metroPtr Pointer to the metronome instance
 */
void setMetronomeReference(void* metroPtr);

/**
 * @brief Process available serial input for commands
 * 
 * Reads and processes any available serial input, executing
 * commands as they are received.
 */
void processSerialCommands();

/**
 * @brief Parse a single command line
 * 
 * @param commandLine The command line to parse (null-terminated)
 * @return true if command was recognized and executed, false otherwise
 */
bool parseCommand(const char* commandLine);

/**
 * @brief Show help information
 * 
 * Displays available commands and their usage.
 */
void showHelp();

/**
 * @brief Show current context parameters
 * 
 * Displays all four context parameters with their current values.
 */
void showContextParams();

/**
 * @brief Set metronome reference for commands
 * 
 * @param metroPtr Pointer to metronome instance
 */
void setMetronomeReference(void* metroPtr);
