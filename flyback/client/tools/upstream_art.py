"""Locate optional programme-renderer sources for regenerating baked art."""
import os
import sys
from pathlib import Path

base = os.environ.get("FLYBACK_UPSTREAM")
if not base:
    raise SystemExit("Set FLYBACK_UPSTREAM to a checkout of chen-006/meow-ai-arena; see flyback/client/README.md.")
UPSTREAM = Path(base).resolve()
RENDERER = UPSTREAM / "boomerang/renderer/src"
if not (RENDERER / "mapfile.py").is_file():
    raise SystemExit(f"Original renderer not found: {RENDERER}")
sys.path.insert(0, str(RENDERER))
