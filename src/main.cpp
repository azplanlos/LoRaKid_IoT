/**
 * 
 * FOR THIS EXAMPLE TO WORK, YOU MUST INSTALL THE "LoRaWAN_ESP32" LIBRARY USING
 * THE LIBRARY MANAGER IN THE ARDUINO IDE.
 * 
 * This code will send a two-byte LoRaWAN message every 15 minutes. The first
 * byte is a simple 8-bit counter, the second is the ESP32 chip temperature
 * directly after waking up from its 15 minute sleep in degrees celsius + 100.
 *
 * If your NVS partition does not have stored TTN / LoRaWAN provisioning
 * information in it yet, you will be prompted for them on the serial port and
 * they will be stored for subsequent use.
 *
 * See https://github.com/ropg/LoRaWAN_ESP32
*/


// Pause between sends in seconds, so this is every 15 minutes. (Delay will be
// longer if regulatory or TTN Fair Use Policy requires it.)
#define MINIMUM_DELAY 60 

#include <heltec_unofficial.h>
#include <LoRaWAN_ESP32.h>
#include <messageFromKid.pb.h>
#include <pb_encode.h>

LoRaWANNode* node;

RTC_DATA_ATTR uint8_t count = 0;

#include "images.h"

OLEDDisplayUi ui     ( &display );

void goToSleep() {
  Serial.println("Going to deep sleep now");
  // allows recall of the session after deepsleep
  persist.saveSession(node);
  Serial.printf("Next wakeup in %i s\n", MINIMUM_DELAY);
  esp_sleep_enable_ext0_wakeup(BUTTON, LOW);
  button.waitForRelease();
  delay(100);  // So message prints
  // and off to bed we go
  heltec_deep_sleep(MINIMUM_DELAY);
}

char voltBuf[6];

void msOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(128, 0, String(millis()));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawStringf(0, 0, voltBuf, "%i %%", heltec_battery_percent());
}

void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // draw an xbm image.
  // Please note that everything that should be transitioned
  // needs to be drawn relative to x and y

  display->drawXbm(x + 34, y + 14, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
}

void drawFrame3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display->setFont(ArialMT_Plain_16);
  display->drawString(x + 64, y + 10, "Andi");
}

void drawFrame4(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display->setFont(ArialMT_Plain_16);
  display->drawString(x + 64, y + 10, "Lukas");
}

void drawFrame5(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display->setFont(ArialMT_Plain_16);
  display->drawString(x+ 64, y + 10, "Asli");
}

// This array keeps function pointers to all frames
// frames are the single views that slide in
FrameCallback frames[] = { drawFrame1, drawFrame3, drawFrame4, drawFrame5 };

// how many frames are there?
int frameCount = 4;

// Overlays are statically drawn on top of a frame eg. a clock
OverlayCallback overlays[] = { msOverlay };
int overlaysCount = 1;

long lastMessage = 0;
HotButton b1 = HotButton(GPIO_NUM_6, true);

void setup() {
  heltec_setup();

  // The ESP is capable of rendering 60fps in 80Mhz mode
  // but that won't give you much time for anything else
  // run it in 160Mhz mode or just set it to 30 fps
  ui.setTargetFPS(15);

  ui.disableAutoTransition();

  // Customize the active and inactive symbol
  ui.setActiveSymbol(activeSymbol);
  ui.setInactiveSymbol(inactiveSymbol);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_UP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  // Add frames
  ui.setFrames(frames, frameCount);

  // Add overlays
  ui.setOverlays(overlays, overlaysCount);

  if (esp_sleep_get_ext1_wakeup_status() > 0) {
    // Initialising the UI will init the display too.
    ui.init();

    ui.update();
  }


  // Obtain directly after deep sleep
  // May or may not reflect room temperature, sort of. 
  float temp = heltec_temperature();
  both.printf("Temperature: %.1f °C\n", temp);

  // initialize radio
  Serial.println("Radio init");
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    both.println("Radio did not initialize. We'll try again later.");
    delay(2000);
  }
}

void sendLoRaMessage() {
  if (!(node && node->isJoined())) {
    node = persist.manage(&radio);
    // Manages uplink intervals to the TTN Fair Use Policy
    if (node->isJoined()) { 
      node->setDutyCycle(true, 1250);
    }
  }

  if (!node->isJoined()) {
    both.println("Could not join network. We'll try again later.");
    return;
  }

  if (node->timeUntilUplink() > 0 || lastMessage > millis() - (MINIMUM_DELAY * 1000)) {
    return;
  }

  // If we're still here, it means we joined, and we can send something
  persist_MessageFromKid message = {
    heltec_battery_percent()
  };
  uint8_t uplinkData[256];
  Serial.println("battery: " + message.batteryLevel);

  pb_ostream_t ostream;
  ostream = pb_ostream_from_buffer(uplinkData, sizeof(uplinkData));
  pb_encode(&ostream, &persist_MessageFromKid_msg, &message);

  uint8_t downlinkData[256];
  size_t lenDown = sizeof(downlinkData);

  int16_t state = node->sendReceive(uplinkData, ostream.bytes_written, 1, downlinkData, &lenDown);

  lastMessage = millis();

  if(state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println("Message sent");
  } else {
    Serial.printf("sendReceive returned error %d, we'll try again later.\n", state);
  }

}

void loop() {
  // This is never called. There is no repetition: we always go back to
  // deep sleep one way or the other at the end of setup()
  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    long usedMillis = millis();
    heltec_loop();
    b1.update();

    if (button.pressedFor(1000)) {
      goToSleep();
    }
    if (b1.pressedFor(200)) {
      Serial.println("next screen");
      ui.nextFrame();
    }

    sendLoRaMessage();
    long interval = remainingTimeBudget - (millis() - usedMillis);
    if (interval <= 0) {
      interval = 1;
    }
    delay(interval);
  }

  if (millis() > 180000) {
    goToSleep();
  }
}
