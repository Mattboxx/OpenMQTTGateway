"""PlatformIO PRE hook: libraries are installed, no objects compiled yet."""

import sys
from pathlib import Path

Import("env")

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from rtl433_patch import apply_patch

library = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "rtl_433_ESP"
changed = apply_patch(library)
print("rtl_433_ESP v0.3.3 checked memory patch: " +
      (", ".join(changed) if changed else "already applied"))
