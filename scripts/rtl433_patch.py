"""Version-checked, repeatable local patch for rtl_433_ESP tag v0.3.3.

Only the derivative presets call this module. Keep decoder input arrays and
device support intact; compact the unused demodulator copy, not real signals.
Unknown or locally modified dependencies are never overwritten.
"""

import hashlib
from pathlib import Path


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("rtl_433_ESP patch anchor is missing or ambiguous")
    return source.replace(old, new, 1)


def compact_header(source, replace=replace_once):
    source = replace(source, "    pulse_data_t    pulse_data;", """#ifdef OMG_RTL433_COMPACT_METADATA
    // The ESP port's data callback reads only these two metadata fields.
    // Real decoder pulse/gap arrays remain in the queued pulse_data_t.
    struct {
        int signalRssi;
        unsigned long signalDuration;
    } pulse_data;
#else
    pulse_data_t    pulse_data;
#endif""")
    return replace(source, "#endif /* INCLUDE_R_PRIVATE_H_ */", """#ifdef OMG_RTL433_COMPACT_METADATA
static inline void omg_rtl433_set_metadata(struct dm_state *state,
        pulse_data_t const *pulses) {
    state->pulse_data.signalRssi = pulses->signalRssi;
    state->pulse_data.signalDuration = pulses->signalDuration;
}
#endif

#endif /* INCLUDE_R_PRIVATE_H_ */""")


def compact_decoder_v1(source, replace=replace_once):
    return replace(source, "    cfg->demod->pulse_data = *rtl_pulses;", """#ifdef OMG_RTL433_COMPACT_METADATA
    omg_rtl433_set_metadata(cfg->demod, rtl_pulses);
#else
    cfg->demod->pulse_data = *rtl_pulses;
#endif""")


def compact_decoder(source, replace=replace_once):
    source = compact_decoder_v1(source, replace)
    source = replace(source, '#include "signalDecoder.h"',
                     '#include "signalDecoder.h"\n#include "RFTemplateLifetime.h"')
    return replace(source, "    rtl_433_Queue = xQueueCreate(5, sizeof(pulse_data_t*));", """#ifdef OMG_RTL433_RELEASE_TEMPLATES
    // register_protocol owns copies; this setup-only table is no longer read.
    // Refuse release if an unexpected factory/list entry aliases the table.
    const size_t releasedTemplates = omg::releaseRFTemplateTable(
        cfg->devices, cfg->num_r_devices, cfg->demod->r_devs.elems,
        cfg->demod->r_devs.len);
    logprintfLn(LOG_INFO, "RF template table released bytes=%u protocols=%u registered=%u",
                (unsigned)releasedTemplates, (unsigned)cfg->num_r_devices,
                (unsigned)cfg->demod->r_devs.len);
#endif
    rtl_433_Queue = xQueueCreate(5, sizeof(pulse_data_t*));""")


def checked_registration(source, replace=replace_once):
    return replace(source,
        "  p->verbose = dev_verbose ? dev_verbose : (cfg->verbosity > 4 ? cfg->verbosity - 5 : 0);",
        """#ifdef OMG_RTL433_RELEASE_TEMPLATES
  // create_device/factory callbacks can return NULL on allocation failure.
  if (!p) FATAL_CALLOC("register_protocol factory");
#endif
  p->verbose = dev_verbose ? dev_verbose : (cfg->verbosity > 4 ? cfg->verbosity - 5 : 0);""")


def checked_receiver_v1(source, replace=replace_once):
    # Floating-point fields and ordinary memcpy require byte-addressable RAM.
    source = replace(source,
        "RECEIVER_BUFFER_SIZE, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL);",
        "RECEIVER_BUFFER_SIZE, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);")
    source = replace(source,
        "void rtl_433_ESP::initReceiver(byte inputPin, float receiveFrequency) {",
        """void rtl_433_ESP::initReceiver(byte inputPin, float receiveFrequency) {
  if (!_pulseTrains) {
    logprintfLn(LOG_ERR, "RF pulse storage allocation failed; receiver disabled");
    return;
  }""")
    for function in ("resetReceiver", "enableReceiver"):
        anchor = "void rtl_433_ESP::%s() {" % function
        source = replace(source, anchor, anchor + "\n  if (!_pulseTrains) return;")
    source = replace(source,
        "      pulse_data_t* rtl_pulses = (pulse_data_t*)heap_caps_calloc(1, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL);",
        """      pulse_data_t* rtl_pulses = (pulse_data_t*)heap_caps_calloc(
          1, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!rtl_pulses) {
        // Drop this completed signal, release its slot, and retry later signals.
        // Never memcpy into NULL or enqueue an invalid decoder input.
        _pulseTrains[_receiveTrain].num_pulses = 0;
        ignoredSignals++;
        static unsigned long lastAllocationWarning = 0;
        const unsigned long allocationNow = millis();
        if (lastAllocationWarning == 0 ||
            (unsigned long)(allocationNow - lastAllocationWarning) >= 5000UL) {
          lastAllocationWarning = allocationNow;
          logprintfLn(LOG_ERR, "RF signal dropped: pulse copy allocation failed");
        }
        vTaskDelay(1);
        return;
      }""")
    return source


def checked_receiver(source, replace=replace_once):
    source = checked_receiver_v1(source, replace)
    source = replace(source, '#include <rtl_433_ESP.h>',
                     '#include <rtl_433_ESP.h>\n#include "RFSignalMemoryBudget.h"')
    source = replace(source,
        """      pulse_data_t* rtl_pulses = (pulse_data_t*)heap_caps_calloc(
          1, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);""",
        """      const uint32_t pulseMemoryCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
      const size_t pulseUsable = heap_caps_get_free_size(pulseMemoryCaps);
      const size_t pulseLargest = heap_caps_get_largest_free_block(pulseMemoryCaps);
#ifdef QUEUE_MIN_FREE_HEAP
      const size_t pulseNetworkReserve = QUEUE_MIN_FREE_HEAP;
#else
      const size_t pulseNetworkReserve = 12000U;
#endif
      pulse_data_t* rtl_pulses = nullptr;
      if (omg::rfSignalCopyHasHeadroom(pulseUsable, pulseLargest,
                                      sizeof(pulse_data_t), pulseNetworkReserve)) {
        rtl_pulses = (pulse_data_t*)heap_caps_calloc(
            1, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      }""")
    return replace(source,
        'logprintfLn(LOG_ERR, "RF signal dropped: pulse copy allocation failed");',
        'logprintfLn(LOG_ERR, "RF signal dropped: copy memory unavailable usable=%u largest=%u required=%u reserve=%u",\n'
        '                      (unsigned)pulseUsable, (unsigned)pulseLargest,\n'
        '                      (unsigned)sizeof(pulse_data_t), (unsigned)pulseNetworkReserve);')


PATCHES = (
    ("include/r_private.h", "449fcf3c835b792183c93293ba2cef98cc75901d74eed742b5cbcd8d128273e6", compact_header),
    ("src/signalDecoder.cpp", "5027dbb709200a74dcadc2957782396b7fd6267d5d90ad95cb5c836e0a262a14", compact_decoder),
    ("src/rtl_433_ESP.cpp", "d911692c16eae86642a0a8a79ecf77fa89a863954d21d9e734e07254cbeeb77e", checked_receiver),
    ("src/rtl_433/r_api.c", "0b168c15c9d5b26c230f881434d6905d4d58faf2aac70b930a690f68cd1f6d1f", checked_registration),
)
LEGACY_TRANSFORMS = {"src/rtl_433_ESP.cpp": (checked_receiver_v1,),
                     "src/signalDecoder.cpp": (compact_decoder_v1,)}


def digest(source):
    return hashlib.sha256(source.replace("\r\n", "\n").encode("utf-8")).hexdigest()


def original_from_patched(source, transform):
    # Reverse only exact generated insertions, then check the entire original.
    # Derive replacements from the transformation itself to avoid two versions
    # of patch text drifting apart. Each transformation uses replace_once.
    replacements = []
    def record(text, old, new):
        replacements.append((old, new))
        return text

    transform("", record)
    for old, new in reversed(replacements):
        source = replace_once(source, new, old)
    return source


def apply_patch(library):
    """Validate ALL files before writing ANY; return changed relative paths."""
    updates = []
    for relative, expected, transform in PATCHES:
        path = Path(library) / relative
        source = path.read_text(encoding="utf-8")
        if digest(source) == expected:
            updates.append((path, relative, transform(source)))
            continue
        original = restore_known_source(source, relative, expected, transform)
        patched = transform(original)
        if patched != source:
            updates.append((path, relative, patched))
    helpers = []
    for name in ("RFSignalMemoryBudget.h", "RFTemplateLifetime.h"):
        helper = Path(__file__).resolve().parents[1] / "main" / name
        helper_target = Path(library) / "include" / name
        helper_bytes = helper.read_bytes()
        if helper_target.exists() and helper_target.read_bytes() != helper_bytes:
            raise ValueError("Modified generated " + name + "; refusing overwrite")
        helpers.append((helper_target, helper_bytes))
    for path, _, patched in updates:
        path.write_text(patched, encoding="utf-8", newline="\n")
    for helper_target, helper_bytes in helpers:
        if not helper_target.exists():
            helper_target.write_bytes(helper_bytes)
    return [relative for _, relative, _ in updates]


def restore_known_source(source, relative, expected, transform):
    if digest(source) == expected:
        return source
    for candidate in (transform,) + LEGACY_TRANSFORMS.get(relative, ()):
        try:
            original = original_from_patched(source, candidate)
        except ValueError:
            continue
        if digest(original) == expected and candidate(original) == source:
            return original
    raise ValueError("Unknown or modified rtl_433_ESP source: " + relative)
