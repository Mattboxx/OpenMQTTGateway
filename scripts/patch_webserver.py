"""Opt-in PRE hook: put the isolated patched library before SDK libraries."""

import sys
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "scripts"))
from webserver_patch import generate_webserver

framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
storage = Path(env.subst("$PROJECT_BUILD_DIR")) / "framework-fixes" / env.subst("$PIOENV")
changed = generate_webserver(framework / "libraries/WebServer", project / "main/MultipartBoundary.h",
                             storage / "OMGWebServer")
env.Prepend(LIBSOURCE_DIRS=[str(storage)])
print("Isolated WebServer multipart bounds: " + ("generated" if changed else "already checked"))
