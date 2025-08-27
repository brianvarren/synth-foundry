/*
 * SSD1306 OLED Display Test Sketch
 * Uses 4 consecutive GPIO pins for direct connection
 * Compatible with Arduino and RP2040
 * 
 * Pin Configuration (example using pins 2-5):
 * Pin 2: GND (held LOW)
 * Pin 3: VCC (held HIGH) 
 * Pin 4: SCL (I2C Clock)
 * Pin 5: SDA (I2C Data)
 * 
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration - Change these as needed
#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64   // OLED display height, in pixels (32 or 64)
#define OLED_ADDRESS 0x3C  // Common addresses: 0x3C or 0x3D

// Pin assignments (consecutive)
#define PIN_GND   7
#define PIN_VCC   6
#define PIN_SCL   5
#define PIN_SDA   4

// Create display object without reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  while(!Serial);
  delay(100);
  
  Serial.println("SSD1306 OLED Test Starting...");
  Serial.print("Using pins: GND=");
  Serial.print(PIN_GND);
  Serial.print(", VCC=");
  Serial.print(PIN_VCC);
  Serial.print(", SCL=");
  Serial.print(PIN_SCL);
  Serial.print(", SDA=");
  Serial.println(PIN_SDA);
  
  // SAFETY FIRST: Set all pins to safe states before powering
  // 1. First set GND and VCC as outputs but keep VCC LOW initially
  pinMode(PIN_GND, OUTPUT);
  digitalWrite(PIN_GND, LOW);   // Set ground
  
  pinMode(PIN_VCC, OUTPUT);
  digitalWrite(PIN_VCC, LOW);   // Keep power OFF initially
  
  // 2. Configure I2C pins (they'll be high-Z until Wire.begin)
  pinMode(PIN_SCL, INPUT);
  pinMode(PIN_SDA, INPUT);
  
  // 3. Small delay for pin states to settle
  delay(10);
  
  // 4. Initialize I2C with custom pins

  // For RP2040 (Raspberry Pi Pico)
    Wire.setSDA(PIN_SDA);
    Wire.setSCL(PIN_SCL);
    Wire.begin();
    
  // 5. Add pull-ups for I2C (internal pull-ups)
  pinMode(PIN_SCL, INPUT_PULLUP);
  pinMode(PIN_SDA, INPUT_PULLUP);
  
  // 6. Small delay before powering on
  delay(50);
  
  // 7. NOW power on the display
  digitalWrite(PIN_VCC, HIGH);
  Serial.println("Display powered on");
  
  // 8. Wait for display to power up
  delay(100);
  
  // 9. Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    // Flash the power pin to indicate error
    for(;;) {
      digitalWrite(PIN_VCC, LOW);
      delay(500);
      digitalWrite(PIN_VCC, HIGH);
      delay(500);
    }
  }
  
  Serial.println("Display initialized successfully!");
  
  // Clear the buffer
  display.clearDisplay();
  
  // Set text properties
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  
  // Display test message
  display.println(F("Hello,"));
  display.setCursor(10, 40);
  display.println(F("OLED!"));
  
  // Show the display buffer on the screen
  display.display();
  
  // Also display some info
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Test OK!"));
  display.print(F("Addr: 0x"));
  display.println(OLED_ADDRESS, HEX);
  display.print(F("Size: "));
  display.print(SCREEN_WIDTH);
  display.print(F("x"));
  display.println(SCREEN_HEIGHT);
  display.display();
}

void loop() {
  // Simple animation to show it's running
  static unsigned long lastUpdate = 0;
  static int x = 0;
  
  if (millis() - lastUpdate > 100) {
    lastUpdate = millis();
    
    // Draw a moving dot at the bottom
    display.fillRect(0, 56, 128, 8, SSD1306_BLACK);
    display.fillCircle(x, 60, 2, SSD1306_WHITE);
    display.display();
    
    x += 4;
    if (x > 128) x = 0;
  }
}

/*
 * Troubleshooting:
 * 
 * 1. If display doesn't work, try:
 *    - Different I2C address (0x3C or 0x3D)
 *    - Different SCREEN_HEIGHT (32 or 64)
 *    - Check wiring order matches pin definitions
 * 
 * 2. For different boards:
 *    - Arduino Uno/Nano: Pins 2-5 work well
 *    - ESP8266: Avoid pins 0, 2, 15 for PIN_START
 *    - ESP32: Most pins work, avoid input-only pins (34-39)
 *    - RP2040: Any GPIO pins work
 * 
 * 3. Power considerations:
 *    - SSD1306 typically draws 20-30mA
 *    - Most GPIO pins can source 20-40mA safely
 *    - For extended use, consider proper power supply
 */