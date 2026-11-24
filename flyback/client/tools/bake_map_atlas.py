"""Bake the shipped course diagrams from referee files, in half-metre cells."""
import json
from pathlib import Path

client = Path(__file__).resolve().parents[1]
repo = client.parents[1]
upstream = repo / "boomerang/renderer/maps"
fork = repo / "flyback/native/game/maps"

def diagram(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    i = next(i for i, line in enumerate(lines) if line.startswith("GRID "))
    _, width, height = lines[i].split()
    grid = lines[i+1:i+1+int(height)]
    assert len(grid)==int(height) and all(len(row)==int(width) for row in grid)
    spawns = {}
    for row in lines[:i]:
        fields = row.split()
        if row.startswith("SPAWN_3 "): coords = fields[1:]
        elif row.startswith(("SPAWN 0 ", "SPAWN 1 ")): coords = fields[2:]
        else: continue
        # Source positions are in metres; emit cells so both axes match the grid.
        v = [round(2*float(x),3) for x in coords]
        spawns[str(len(v)//2)] = list(map(list,zip(v[::2],v[1::2])))
    return {"grid":grid,"spawns":spawns}

sources = [upstream/(k+".txt") for k in ("twinlakes","icefloes","ruins","yinyang","skybridge","stonecircle")]
sources.extend(sorted(fork.glob("*.txt")))
data = {p.stem:diagram(p) for p in sources}
(client/"art/map_atlas.json").write_text(json.dumps(data,separators=(",",":"))+"\n",encoding="utf-8")
