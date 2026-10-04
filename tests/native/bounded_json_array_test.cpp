#include "../../main/BoundedJsonArray.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
  StaticJsonDocument<2048> document;
  JsonArray modules = document.to<JsonArray>();
  modules.add("BLETracker");
  modules.add("WebUI");
  modules.add("RF");
  modules.add("RF2");
  modules.add("GPIOInput");
  modules.add("GPIOOutput");
  modules.add("RTL_433");
  struct Guarded {
    unsigned before = 0x12345678;
    char buffer[100];
    unsigned after = 0x87654321;
  } guarded;
  assert(serializeJsonArrayBounded(modules, guarded.buffer));
  assert(strlen(guarded.buffer) == measureJson(modules));
  for (unsigned i = 0; i < 12; ++i) modules.add("AdditionalModule");
  assert(!document.overflowed());
  assert(measureJson(modules) > sizeof(guarded.buffer));
  assert(!serializeJsonArrayBounded(modules, guarded.buffer));
  assert(strcmp(guarded.buffer, "[]") == 0);
  assert(guarded.before == 0x12345678 && guarded.after == 0x87654321);
  document.clear();
  modules = document.to<JsonArray>();
  modules.add("a");
  char exact[6];
  assert(serializeJsonArrayBounded(modules, exact));
  assert(strcmp(exact, "[\"a\"]") == 0);
  char oneShort[5];
  assert(!serializeJsonArrayBounded(modules, oneShort));
  assert(strcmp(oneShort, "[]") == 0);
  modules.clear();
  char empty[3];
  assert(serializeJsonArrayBounded(modules, empty));
  modules.add("quote\"newline\n");
  char escaped[64];
  assert(serializeJsonArrayBounded(modules, escaped));
  assert(strcmp(escaped, "[\"quote\\\"newline\\n\"]") == 0);
  puts("PASS: fixed JSON array capacity, valid fallback, canaries, exact boundary and escaping");
}
