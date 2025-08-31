#pragma once
#include <stdint.h>

typedef bool (*UiLoadFn)(const char* filename);  // app-provided loader (keeps coupling low)

namespace sf {

// Initialize the browser (scans SD and renders the first page)
void browser_init(UiLoadFn onLoad);

// Call from loop() so long I/O happens outside encoder callbacks
void browser_tick(void);

// Encoder events
void browser_on_turn(int8_t inc);   // ±1 … ±5 per your library’s acceleration
void browser_on_button(void);       // request load of selected item

} // namespace ui
