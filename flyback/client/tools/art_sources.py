"""Read the shared map geometry for Godot's offline art bakes."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[3]
import upstream_art
import mapfile
from arena_palettes import LOOKS,register

register()
FORK=ROOT/"flyback/native/game/maps"
MAPS=["twinlakes","icefloes","yinyang","ruins","skybridge","stonecircle",*(p.stem for p in sorted(FORK.glob("*.txt")))]

def read_map(name):
    fork=FORK/f"{name}.txt"
    if not fork.exists():return mapfile.read_map(ROOT/f"boomerang/renderer/maps/{name}.txt")
    lines=fork.read_text(encoding="utf-8").splitlines()
    gi=next(i for i,s in enumerate(lines) if s.startswith("GRID "))
    _,w,h=lines[gi].split();w,h=int(w),int(h)
    ci=lines.index("COLLAPSE")
    sp=next(s for s in lines if s.startswith(("SPAWN_3 ","SPAWN 1 "))).split()
    coords=list(map(float,sp[1:] if sp[0]=="SPAWN_3" else sp[2:]))
    return dict(W=w,H=h,cell=.5,theme=name if name in LOOKS else ("meadow" if name=="trident" else "heath"),
                spawn={1:list(zip(coords[::2],coords[1::2]))}, grid=lines[gi+1:gi+1+h],wave=lines[ci+1:ci+1+h])
