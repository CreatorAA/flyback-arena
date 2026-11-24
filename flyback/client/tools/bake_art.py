"""Build-time art conversion only. The game needs neither Blender nor Python."""
from pathlib import Path
import json, sys
HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
from upstream_art import UPSTREAM
import ground_paint, mapfile, themes
from art_sources import read_map,MAPS
from arena_palettes import finish_surface
from PIL import Image
ground_paint.PPM=128
from bake_actors import bake_actors
if not sys.argv[1:]: bake_actors(UPSTREAM / "characters/boomerang", HERE / "art/actors")
for name in (sys.argv[1:] or MAPS):
    m = read_map(name)
    th = themes.get(m["theme"] or themes.MAP_THEME.get(name,"meadow"))
    rows = ["".join(mapfile.CH[c] for c in s) for s in m["grid"]]
    if name == "twinlakes":
        (HERE/"art/attract.json").write_text(json.dumps({"map":name,"seats":[],"terrain":{"width":m["W"],"height":m["H"],"cell_mm":500,"rows":rows,"waves":m["wave"]}}),encoding="utf-8")
    png = HERE/"art/ground"/f"{name}.png"
    ground_paint.paint([[int(c) for c in s] for s in rows],m["cell"],m["spawn"][1],str(png),seed=7,colors=th["ground"],style=th["gstyle"])
    im=Image.open(png).convert("RGB")
    finish_surface(im,name,m,ground_paint.PPM).save(png.with_suffix(".webp"),lossless=True,method=3)
    png.unlink()
