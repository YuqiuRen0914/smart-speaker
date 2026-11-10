#pragma once

class JsonVariant {
public:
  template <typename T> T to() const { return {}; }
};

class JsonObject {
public:
  JsonVariant operator[](const char *) const { return {}; }
};
class JsonArrayConst {};
class JsonVariantConst {};
