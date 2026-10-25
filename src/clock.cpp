#include "clock.h"

#include <time.h>

#include "config.h"
#include "texts.h"

namespace Clock {

namespace {

bool ntpStarted = false;

bool now(tm& out) {
  time_t t = ::time(nullptr);
  if (t < 1700000000) return false;
  localtime_r(&t, &out);
  return true;
}

}

void begin() {
  setenv("TZ", Cfg::TIMEZONE, 1);
  tzset();
  Serial.printf("[clock] %s %s\n", date().c_str(), time().c_str());
}

void startNtp() {
  if (ntpStarted) return;
  ntpStarted = true;
  configTzTime(Cfg::TIMEZONE, Cfg::NTP_SERVER_1, Cfg::NTP_SERVER_2);
  Serial.println("[clock] NTP started");
}

bool valid() {
  tm t;
  return now(t);
}

String time() {
  tm t;
  if (!now(t)) return Text::NO_TIME;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}

String date() {
  tm t;
  if (!now(t)) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %d.%d.%d", Text::WEEKDAYS[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  return buf;
}

}
