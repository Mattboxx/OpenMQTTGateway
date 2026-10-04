"""Generate an isolated WebServer copy, leaving the installed SDK untouched."""

import hashlib
import json
from pathlib import Path, PurePosixPath

PARSING_SHA256 = "6ffc23d69143515d65d7fcf89528ebd5b9767a7d710d4bd6e652c95ba8ddd26b"
STAMP = ".omg-generated.json"


def digest(content):
    return hashlib.sha256(content).hexdigest()


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("Pinned WebServer patch anchor is missing or ambiguous")
    return source.replace(old, new, 1)


def patch_parser(source):
    if digest(source.encode("utf-8")) != PARSING_SHA256:
        raise ValueError("Unknown WebServer parser; requires unmodified Arduino-ESP32 2.0.7")
    source = '#include "MultipartBoundary.h"\n#ifndef OMG_MULTIPART_BOUNDS\n#error "Use the isolated WebServer only in the derivative presets"\n#endif\n' + source
    source = replace_once(source,
        "  (void) len;\n  log_v(\"Parse Form: Boundary: %s Length: %d\", boundary.c_str(), len);",
        """  (void) len;
  if (!omg::validMultipartBoundaryLength(boundary.length())) {
    log_e("Invalid multipart boundary length");
    return false;
  }
  log_v("Parse Form: Boundary: %s Length: %d", boundary.c_str(), len);""")
    source = replace_once(source, "uint8_t endBuf[boundary.length()];",
                          "uint8_t endBuf[omg::multipartBoundaryMax];")
    return replace_once(source,
        "strstr((const char*)endBuf, boundary.c_str()) != NULL",
        "omg::matchesMultipartBoundary(endBuf, i, boundary.c_str(), boundary.length())")


def generate_webserver(sdk_library, helper, output):
    sdk_library, helper, output = map(Path, (sdk_library, helper, output))
    parser = (sdk_library / "src/Parsing.cpp").read_text(encoding="utf-8")
    patched = patch_parser(parser)
    files = {path.relative_to(sdk_library).as_posix(): path.read_bytes()
             for path in (sdk_library / "src").rglob("*") if path.is_file()}
    properties = (sdk_library / "library.properties").read_text(encoding="utf-8")
    files["library.properties"] = replace_once(properties, "name=WebServer\n",
                                               "name=OMGWebServer\n").encode("utf-8")
    files["src/Parsing.cpp"] = patched.encode("utf-8")
    files["src/MultipartBoundary.h"] = helper.read_bytes()
    stamp = output / STAMP
    if output.exists():
        if not stamp.is_file():
            raise ValueError("Refusing to overwrite an unknown WebServer copy")
        old = json.loads(stamp.read_text(encoding="utf-8"))
        for relative, expected in old.items():
            path = PurePosixPath(relative)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("Invalid generated WebServer manifest path")
            target = output / relative
            if not target.is_file() or digest(target.read_bytes()) != expected:
                raise ValueError("Modified generated WebServer file: " + relative)
        actual = {path.relative_to(output).as_posix() for path in output.rglob("*")
                  if path.is_file() and path != stamp}
        if actual != set(old) or set(old) != set(files):
            raise ValueError("Unknown files in generated WebServer copy")
    changed = []
    for relative, content in files.items():
        target = output / relative
        if target.is_file() and target.read_bytes() == content:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        changed.append(relative)
    manifest = json.dumps({relative: digest(content) for relative, content in files.items()},
                          sort_keys=True, indent=2) + "\n"
    if not stamp.is_file() or stamp.read_text(encoding="utf-8") != manifest:
        stamp.write_text(manifest, encoding="utf-8")
    return changed
