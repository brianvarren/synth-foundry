/**
 * @file ladder_filter.cpp
 * @brief 8-pole ladder filters using fixed-point math
 * 
 * This file implements the 8-pole ladder filter classes defined in ladder_filter.h.
 * The implementation uses only integer arithmetic for real-time performance
 * and is designed to be lightweight and efficient for audio processing.
 * 
 * @author Brian Varren
 * @version 1.0
 * @date 2024
 */

#include "ladder_filter.h"

// The filter classes are implemented inline in the header file for efficiency.
// This file exists for future expansion if needed (e.g., more complex filter types).

// Currently, all functionality is implemented as inline functions in the header
// to maximize performance in the audio interrupt context.
