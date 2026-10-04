"""Check actual RF registration/factory ownership and template release.

Uses pinned device/list headers and registration/create functions. C malloc
assignments acquire explicit casts for C++ compilation; log/output handlers
and allocation failures are mocked. This is not live RF decoding.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/rtl_433_ESP"


def extract(path, marker):
    source = path.read_text(encoding="utf-8")
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


register = extract(LIB / "src/rtl_433/r_api.c", "void register_protocol(")
assert register.count("p = malloc(sizeof(*p));") == 1
register = register.replace("p = malloc(sizeof(*p));", "p = static_cast<r_device*>(malloc(sizeof(*p)));")
create = extract(LIB / "src/rtl_433/decoder_util.c", "r_device *create_device(")
assert create.count("r_device *r_dev = malloc(sizeof (*r_dev));") == 1
create = create.replace("r_device *r_dev = malloc(sizeof (*r_dev));",
                        "r_device *r_dev = static_cast<r_device*>(malloc(sizeof (*r_dev)));")
factory = extract(LIB / "src/rtl_433/devices/fineoffset.c", "static r_device *fineoffset_WH2_create(")
assert factory.count("int *quirk = malloc(sizeof (*quirk));") == 1
factory = factory.replace("int *quirk = malloc(sizeof (*quirk));",
                          "int *quirk = static_cast<int*>(malloc(sizeof (*quirk)));")

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>
#include <limits>
#include "r_device.h"
#include "list.h"
#include "RFTemplateLifetime.h"
#define OMG_RTL433_RELEASE_TEMPLATES
#define FATAL_CALLOC(value) throw std::bad_alloc()
#define WARN_MALLOC(value) (void)0
#define LOG_INFO 4
bool failMalloc = false;
void *mockMalloc(size_t size) { return failMalloc ? nullptr : std::malloc(size); }
struct Demod { list_t r_devs; };
struct r_cfg_t { Demod *demod; int verbosity; };
std::vector<void*> registered;
void list_push(list_t *list, void *device) {
  registered.push_back(device); list->elems = registered.data(); list->len = registered.size();
}
void log_device_handler(r_device *, int, data *) {}
void data_acquired_handler(r_device *, data *) {}
extern const r_device fineoffset_WH2;
#define malloc mockMalloc
__CREATE__
__FACTORY__
__REGISTER__
#undef malloc
const char *const fields[] = {"model", "temperature_C", nullptr};
r_device makeDevice() {
  r_device result = {}; result.name = "Mock protocol"; result.fields = fields;
  result.modulation = OOK_PULSE_PWM; result.short_width = 500; result.long_width = 1500;
  return result;
}
const r_device fineoffset_WH2 = makeDevice();
int main() {
  // Register a table with the pinned preset's full OOK template count. Only
  // the factory template differs; the default registration path copies all
  // mutable fields exactly, and the factory owns its own optional context.
  const size_t count = 157;
  r_device *templates = static_cast<r_device*>(std::calloc(count, sizeof(r_device)));
  assert(templates); Demod demod = {}; r_cfg_t config = {&demod, 0};
  std::vector<std::string> names; names.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    names.push_back("Protocol " + std::to_string(i));
    templates[i] = makeDevice(); templates[i].name = names.back().c_str();
    templates[i].protocol_num = static_cast<unsigned>(i);
    templates[i].short_width += static_cast<float>(i);
    if (i == 20) templates[i].create_fn = fineoffset_WH2_create;
    char quirk[] = "no-wh5";
    register_protocol(&config, &templates[i], i == 20 ? quirk : nullptr);
  }
  assert(registered.size() == count);
  for (size_t i = 0; i < count; ++i) {
    const r_device *device = static_cast<const r_device*>(registered[i]);
    assert(device != &templates[i]); assert(device->output_ctx == &config);
    if (i != 20) { assert(device->protocol_num == i); assert(device->short_width == 500 + i); }
    else { assert(device->decode_ctx && *static_cast<int*>(device->decode_ctx) == 1); }
  }
  assert(omg::releaseRFTemplateTable(templates, count, demod.r_devs.elems, demod.r_devs.len) == count*sizeof(r_device));
  assert(!templates && registered.size() == count);
  for (size_t i = 0; i < count; ++i) {
    r_device *device = static_cast<r_device*>(registered[i]);
    assert(device->fields[0] && device->name && device->modulation == OOK_PULSE_PWM);
    if (i != 20) { assert(device->name == names[i]); assert(device->short_width == 500 + i); }
    else assert(*static_cast<int*>(device->decode_ctx) == 1);
    std::free(device->decode_ctx); std::free(device);
  }
  registered.clear();

  // No release on record or nested-pointer aliases, missing entries or an
  // overflowing table range. None of these tests dereferences an alias.
  templates = static_cast<r_device*>(std::calloc(2, sizeof(r_device))); assert(templates);
  r_device device = makeDevice(); void *records[] = {&device};
  records[0] = templates; assert(!omg::releaseRFTemplateTable(templates, 2, records, 1));
  records[0] = nullptr; assert(!omg::releaseRFTemplateTable(templates, 2, records, 1)); records[0] = &device;
  device.name = reinterpret_cast<const char*>(templates);
  assert(!omg::releaseRFTemplateTable(templates, 2, records, 1)); device = makeDevice();
  device.fields = reinterpret_cast<const char *const*>(templates);
  assert(!omg::releaseRFTemplateTable(templates, 2, records, 1)); device = makeDevice();
  device.decode_ctx = templates;
  assert(!omg::releaseRFTemplateTable(templates, 2, records, 1)); device = makeDevice();
  device.output_ctx = templates;
  assert(!omg::releaseRFTemplateTable(templates, 2, records, 1)); device = makeDevice();
  assert(!omg::releaseRFTemplateTable(templates, 2, nullptr, 1));
  assert(!omg::releaseRFTemplateTable(templates, std::numeric_limits<size_t>::max(), records, 1));
  assert(omg::releaseRFTemplateTable(templates, 2, records, 1) == 2*sizeof(r_device));
  assert(!omg::releaseRFTemplateTable(templates, 2, records, 1));

  // Factory allocation failure must stop before register_protocol dereferences
  // NULL, rather than silently dropping a required decoder from the preset.
  r_device factoryTemplate = makeDevice(); factoryTemplate.create_fn = fineoffset_WH2_create;
  failMalloc = true; bool rejected = false;
  try { register_protocol(&config, &factoryTemplate, nullptr); } catch (const std::bad_alloc&) { rejected = true; }
  assert(rejected && registered.empty());
  std::puts("PASS: actual RF registration/factory copies, 157-record lifetime, contexts/fields after release, alias refusal, overflow and NULL factory guard");
}
'''.replace("__CREATE__", create).replace("__FACTORY__", factory).replace("__REGISTER__", register)

with tempfile.TemporaryDirectory(prefix="omg-rf-template-") as directory:
    cpp = Path(directory) / "lifetime.cpp"
    binary = Path(directory) / ("lifetime.exe" if os.name == "nt" else "lifetime")
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I", str(ROOT / "main"), "-I", str(LIB / "include"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
