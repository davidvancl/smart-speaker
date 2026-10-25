#include "input.h"

#include <ESP32Encoder.h>
#include <OneButton.h>

#include "config.h"

namespace Input {

namespace {

OneButton encoderButton(Pins::ENC_SW, true, true);
OneButton button1(Pins::BTN1, true, false);
OneButton button2(Pins::BTN2, true, false);
OneButton button3(Pins::BTN3, true, false);
ESP32Encoder encoder;
int64_t encoderUsed = 0;
uint32_t encoderActivity = 0;
uint32_t activity = 0;

constexpr uint8_t EVENT_QUEUE = 8;
Event events[EVENT_QUEUE];
uint8_t eventCount = 0;

float touchBaseline = 0;
bool touched = false;
uint8_t touchSamples = 0;
uint32_t touchSampledAt = 0;

void push(Event e) {
  activity = millis();
  if (eventCount < EVENT_QUEUE) events[eventCount++] = e;
}

void pushEvent(void* event) {
  push(static_cast<Event>(reinterpret_cast<uintptr_t>(event)));
}

void* tag(Event e) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(e));
}

void setup(OneButton& button, Event click, Event hold, uint32_t holdMs) {
  button.setDebounceMs(Cfg::BUTTON_DEBOUNCE_MS);
  button.attachClick(pushEvent, tag(click));
  if (hold == Event::None) return;
  button.setPressMs(holdMs);
  button.attachLongPressStart(pushEvent, tag(hold));
}

void calibrateTouch() {
  float sum = 0;
  for (int i = 0; i < 32; i++) {
    sum += touchRead(Pins::TOUCH);
    delay(2);
  }
  touchBaseline = sum / 32;
  Serial.printf("[input] touch baseline %.1f\n", touchBaseline);
}

void updateTouch() {
  uint32_t now = millis();
  if (now - touchSampledAt < Cfg::TOUCH_SAMPLE_MS) return;
  touchSampledAt = now;
  float v = touchRead(Pins::TOUCH);
  if (v <= 0) return;
  if (!touched) {
    if (v < touchBaseline * Cfg::TOUCH_ON_RATIO) {
      if (++touchSamples >= Cfg::TOUCH_CONFIRM_SAMPLES) {
        touched = true;
        touchSamples = 0;
        bool encoderBusy = !encoderButton.isIdle() || now - encoderActivity < Cfg::TOUCH_ENCODER_LOCK_MS;
        Serial.printf("[input] touch %.1f / baseline %.1f%s\n", v, touchBaseline, encoderBusy ? " (ignored, encoder)" : "");
        if (!encoderBusy) push(Event::Touch);
      }
    } else {
      touchSamples = 0;
      touchBaseline = touchBaseline * 0.99f + v * 0.01f;
    }
  } else if (v > touchBaseline * Cfg::TOUCH_OFF_RATIO) {
    touched = false;
  }
}

}

void begin() {
  ESP32Encoder::useInternalWeakPullResistors = puType::none;
  encoder.attachFullQuad(Pins::ENC_CLK, Pins::ENC_DT);
  encoder.setFilter(1023);
  encoder.clearCount();
  setup(encoderButton, Event::EncoderClick, Event::EncoderLong, Cfg::ENC_LONG_PRESS_MS);
  setup(button1, Event::Button1, Event::Button1Hold, Cfg::BTN1_SETUP_HOLD_MS);
  setup(button2, Event::Button2, Event::None, 0);
  setup(button3, Event::Button3, Event::None, 0);
  calibrateTouch();
  activity = millis();
}

void loop() {
  encoderButton.tick();
  if (!encoderButton.isIdle()) encoderActivity = millis();
  button1.tick();
  button2.tick();
  button3.tick();
  updateTouch();
}

int takeEncoderSteps() {
  int64_t count = encoder.getCount();
  int steps = (count - encoderUsed) / Cfg::ENC_STEPS_PER_DETENT;
  if (steps != 0) {
    encoderUsed += steps * Cfg::ENC_STEPS_PER_DETENT;
    encoderActivity = activity = millis();
  }
  return steps;
}

Event poll() {
  if (eventCount == 0) return Event::None;
  Event e = events[0];
  for (uint8_t i = 1; i < eventCount; i++) events[i - 1] = events[i];
  eventCount--;
  return e;
}

bool encoderHeld() {
  return digitalRead(Pins::ENC_SW) == LOW;
}

uint32_t lastActivity() {
  return activity;
}

void markActivity() {
  activity = millis();
}

}
