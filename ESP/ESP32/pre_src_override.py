"""
PlatformIO pre-script: override PROJECTSRC_DIR per firmware_vN environment.

PlatformIO's src_dir is a project-level setting and cannot be set per-environment
in platformio.ini.  This script remaps the source directory at build time so that
each versioned test binary compiles the matching sketch (FirmwareV1 … FirmwareV5).
"""
import os
Import("env")  # noqa: F821  (PlatformIO injects this)

SKETCH_MAP = {
    "firmware_v1": "examples/FirmwareV1",
    "firmware_v2": "examples/FirmwareV2",
    "firmware_v3": "examples/FirmwareV3",
    "firmware_v4": "examples/FirmwareV4",
    "firmware_v5": "examples/FirmwareV5",
}

pioenv = env["PIOENV"]
if pioenv in SKETCH_MAP:
    sketch_dir = os.path.join(env["PROJECT_DIR"], SKETCH_MAP[pioenv])
    env.Replace(PROJECTSRC_DIR=sketch_dir)
    print(">>> [pre_src_override] %s → %s" % (pioenv, sketch_dir))
