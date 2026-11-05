#!/usr/bin/env python3
"""Fresh, bounded owner build for the R2->R3 WebSocket binding seam.

MODULE: ws_binding_run.py
Compiles api-haven's fan-out registry (R3 shape) together with Vexspoke's
WebSocket client and its WsSource adapter (R2 protocol + socket), then runs the
forked-loopback owner in strict and sanitized C23. Every source is compiled with
its OWN repository's src first, so the shared `oop/type.h` registry path resolves
per-owner as the One Type Registry Law requires.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
API = ROOT / "suite/ecosystem/api-haven"
CPU = ROOT / "suite/ecosystem/vexspoke"
ENGINE = ROOT / "suite/ecosystem/relational-engine"
TEST = HERE / "ws_source_binding_test.c"

SOURCES = [
    (API / "src/api/haven_ws_fanout.c", API),
    (CPU / "src/net/vex_ws_source.c", CPU),
    (CPU / "src/net/websocket.c", CPU),
    (CPU / "src/net/tls_apple.m", CPU),
    (CPU / "src/net/url.c", CPU),
    (CPU / "src/security/sha1.c", CPU),
    (CPU / "src/security/secure_random.c", CPU),
    (CPU / "src/lang/str.c", CPU),
    (CPU / "src/atomic/spin.c", CPU),
    (ENGINE / "src/nio/mem.c", ENGINE),
]

MODES = [("strict", []), ("sanitized", ["-fsanitize=address,undefined"])]


def main():
    with tempfile.TemporaryDirectory(prefix="ws-binding-", dir=os.environ.get("TMPDIR")) as temp:
        build = Path(temp)
        for mode, sanitizer in MODES:
            objects = []
            for index, (source, owner) in enumerate(SOURCES):
                flags = ["-std=gnu23", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                         "-mcpu=apple-m1", "-mmacosx-version-min=14.0", *sanitizer,
                         "-I" + str(owner / "src"), "-I" + str(CPU / "src"),
                         "-I" + str(ENGINE / "src"), "-I" + str(API / "src"),
                         "-I" + str(ROOT / "tests")]
                obj = build / f"{mode}-{index}.o"
                arc = ["-fobjc-arc"] if source.suffix == ".m" else []
                subprocess.run(["clang", *flags, *arc, "-c", str(source), "-o", str(obj)],
                               check=True, timeout=120)
                objects.append(str(obj))
            link_flags = ["-std=gnu23", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                          "-mcpu=apple-m1", "-mmacosx-version-min=14.0", *sanitizer,
                          "-I" + str(API / "src"), "-I" + str(CPU / "src"),
                          "-I" + str(ENGINE / "src"), "-I" + str(ROOT / "tests")]
            binary = build / f"{mode}-ws_binding"
            subprocess.run(["clang", *link_flags, str(TEST), *objects, "-framework",
                            "Foundation", "-framework", "Network", "-o", str(binary)],
                           check=True, timeout=120)
            result = subprocess.run([str(binary)], timeout=120, capture_output=True, text=True)
            if result.stdout:
                print(result.stdout, end="")
            if result.returncode != 0:
                if result.stderr:
                    print(result.stderr, end="", file=__import__("sys").stderr)
                raise AssertionError(f"WS binding owner failed in {mode} mode "
                                     f"(exit {result.returncode})")
        print("PASS: strict + ASan/UBSan WebSocket WsSource binding owner")


if __name__ == "__main__":
    main()
