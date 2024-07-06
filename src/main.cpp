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


// Pause between sends in seconds, so this is every 1 minute. (Delay will be
// longer if regulatory or TTN Fair Use Policy requires it.)
#define MINIMUM_DELAY 60 

#include <heltec_unofficial.h>
#include <LoRaWAN_ESP32.h>
#include <messageFromKid.pb.h>
#include <pb_encode.h>
#include <LinkedList.h>
#include <Preferences.h>
#include <ESP32Time.h>
#include <WiFi.h>
#include <NTP.h>

LoRaWANNode* node;

RTC_DATA_ATTR uint8_t count = 0;

#include "images.h"

OLEDDisplayUi ui     ( &display );

LinkedList<Payload_KidPayload*> messagesToSend = LinkedList<Payload_KidPayload*>();
LinkedList<Payload_KidPayload*> messageBuffer = LinkedList<Payload_KidPayload*>();
Preferences* prefs = new Preferences();
const char* TIMEZONE_OFFSET_KEY = "tzo";
const char* HEARTBEAT = "hb";

long lastMessage = 0;

int8_t lastCode = 0;

HotButton b1 = HotButton(GPIO_NUM_6, true);
HotButton b4 = HotButton(GPIO_NUM_4, true);

bool active = false;

ESP32Time rtc;
WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

long lastHeartbeat = 0;

void goToSleep() {
  Serial.println("Going to deep sleep now");
  // allows recall of the session after deepsleep
  if (node) persist.saveSession(node);
  Serial.printf("Next wakeup in %i s\n", MINIMUM_DELAY);
  esp_sleep_enable_ext0_wakeup(BUTTON, LOW);
  button.waitForRelease();
  prefs->putLong(HEARTBEAT, lastHeartbeat);
  prefs->end();
  delay(100);  // So message prints
  // and off to bed we go
  heltec_deep_sleep(MINIMUM_DELAY);
}

char voltBuf[6];

void msOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(128, 0, String(rtc.getTime("%H:%M")));
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

void setup() {
  heltec_setup();
  prefs->begin("loraKid");

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


  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    active = true;

    // Initialising the UI will init the display too.
    ui.init();
    
    ui.update();

  } else {
    heltec_display_power(false);
    Serial.println(F("wakeup due to heartbeat"));
  }

  lastHeartbeat = prefs->getLong(HEARTBEAT);

  Serial.println("last heartbeat: " + String(lastHeartbeat));

  // initialize radio
  Serial.println("Radio init");
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    both.println(F("Radio did not initialize. We'll try again later."));
    delay(2000);
  }

  String ssid = prefs->getString("ssid");
  String wifipw = prefs->getString("wifipw");
  if (ssid.length() == 0) {
    Serial.setTimeout(15000);
    Serial.print("Bitte WLAN SSID eingeben: ");
    ssid = Serial.readStringUntil('\n');
    Serial.print("Bitte WLAN Passwort eingeben: ");
    wifipw = Serial.readStringUntil('\n');
    if (ssid.length() > 0 && wifipw.length() > 0) {
      prefs->putString("ssid", ssid);
      prefs->putString("wifipw", wifipw);
    }
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, wifipw);

  rtc.offset = prefs->getLong(TIMEZONE_OFFSET_KEY);
}

bool payloadEncodeCb(pb_ostream_t *stream, const pb_field_t *field,
    void * const *arg) {
  Serial.printf("called, field->tag=%d field->type=%d", field->tag, field->type);
  uint8_t encoded = 0;
  while (messageBuffer.size() > 0) {
    Payload_KidPayload* bufferMsg = messagesToSend.shift();
    delete(bufferMsg);
  }
  

  while (messagesToSend.size() > 0 && encoded <= 3) {
    Payload_KidPayload* payload = messagesToSend.shift();
    messageBuffer.add(payload);

    if(pb_encode_tag_for_field(stream, field) == false) {
        Serial.println("encode failed");
        return false;
    }
    
    if(pb_encode_submessage(stream, Payload_KidPayload_fields, payload) == false) {
        Serial.println("encode failed");
        return false;
      }
    delete(payload);
    encoded++;
  }
  return true;
}

bool joinAndCheckDutyCycle() {
  if (!(node && node->isActivated() || lastMessage > millis() - (MINIMUM_DELAY * 1000))) {
    node = persist.manage(&radio);
    // Manages uplink intervals to the TTN Fair Use Policy
    if (node->isActivated()) { 
      node->setDutyCycle(true, 1250);
    }
  }

  if (!node->isActivated()) {
    both.println(F("Could not join network. We'll try again later."));
    return false;
  }

  if (node->timeUntilUplink() > 0) {
    return false;
  }
  return true;
}

int16_t encodeAndSendMessage(const void* message, const pb_msgdesc_t* msgType) {
  uint8_t uplinkData[256];
  pb_ostream_t ostream;
  ostream = pb_ostream_from_buffer(uplinkData, sizeof(uplinkData));
  pb_encode(&ostream, msgType, &message);

  uint8_t downlinkData[256];
  size_t lenDown = sizeof(downlinkData);
  Serial.printf("sent %i bytes\n", ostream.bytes_written);

  int16_t state = node->sendReceive(uplinkData, ostream.bytes_written, 1, downlinkData, &lenDown);

  // Serial.printf("received %i bytes%n", lenDown);

  lastMessage = millis();
  return state;
}

void sendLoRaHeartbeat() {
  if (lastHeartbeat <= rtc.getEpoch() - (15 * 60) && joinAndCheckDutyCycle()) {
    lastHeartbeat = rtc.getEpoch();
    Payload_Heartbeat message = {
      heltec_battery_percent(),
      static_cast<int>(heltec_temperature())
    };
    uint16_t state = encodeAndSendMessage(&message, &Payload_Heartbeat_msg);
  }
}

void sendLoRaMessage() {

  if (messagesToSend.size() <= 0 || !joinAndCheckDutyCycle()) {
    // noting to send
    return;
  }

  // If we're still here, it means we joined, and we can send something
  Serial.printf("sending %i messages\n", messagesToSend.size());

  Payload_MessageFromKid message = {
    heltec_battery_percent()
  };
  Serial.println("battery: " + message.batteryLevel);

  message.payload.funcs.encode = payloadEncodeCb;

  uint16_t state = encodeAndSendMessage(&message, &Payload_MessageFromKid_msg);

  if(state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println("Message sent");
  } else {
    Serial.printf("sendReceive returned error %d, we'll try again later.\n", state);
    for (int8_t i = 0; i < messageBuffer.size(); i++) {
      messagesToSend.unshift(messageBuffer.get(i));
    }
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
    b4.update();

    if (button.pressedFor(500)) {
      goToSleep();
    }
    if (b1.pressedFor(200)) {
      Serial.println("next screen");
      ui.nextFrame();
    }

    if (b4.pressedFor(200)) {
      Serial.println("add message");
      Payload_KidPayload message = Payload_KidPayload_init_zero;
      message.code = lastCode;
      message.subSelection = 1;
      lastCode++;
      messagesToSend.add(&message);
    }

    if (WiFi.status() == WL_CONNECTED && (rtc.getEpoch() < 20000 || prefs->getBool("ntp") == false)) {
      Serial.println(WiFi.localIP());
      ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
      ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60); // last sunday in october 3:00, timezone +60min (+1 GMT)
      ntp.begin();
      ntp.update();
      rtc.setTime(ntp.epoch());
      if (rtc.offset == 0) {
        long offset = (ntp.hours() - rtc.getHour(true)) * 60 * 60;
        rtc.offset = offset;
        prefs->putLong(TIMEZONE_OFFSET_KEY, offset);
      }
      Serial.println("offset: " + String(rtc.offset));
      Serial.println(ntp.hours() + "-" + rtc.getHour(true));
      Serial.println(rtc.getDateTime());
      prefs->putBool("ntp", true);
      ntp.stop();
    }

    // TODO: Einmal pro Tag NTP zurücksetzen und neu synchronisieren, falls WLAN vorhanden

    sendLoRaHeartbeat();

    sendLoRaMessage();
    long interval = remainingTimeBudget - (millis() - usedMillis);
    if (interval <= 0) {
      interval = 1;
    }
    delay(interval);
  }

  unsigned long waitTime = 10000;

  if (active) {
    waitTime = 180000;
  }

  if (millis() > waitTime && messagesToSend.size() == 0) {
    goToSleep();
  }
}
