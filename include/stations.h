#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct Station {
  String name;
  String url;
};

namespace Stations {

void begin();

int count();

bool get(int index, Station& out);

bool add(const Station& station);

bool update(int index, const Station& station);

bool remove(int index);

bool move(int from, int to);

bool replace(JsonArrayConst list);

void toJson(JsonArray out);

}
