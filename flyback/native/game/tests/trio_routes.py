"""Compare routes in metres from the actual three spawns to the contest objective."""
import heapq
import math
from pathlib import Path


def distances(file):
    text = file.read_text(encoding="utf-8").splitlines()
    i = next(i for i, line in enumerate(text) if line.startswith("GRID "))
    w, h = map(int, text[i].split()[1:])
    rows = text[i + 1 : i + 1 + h]
    coords = list(map(float, next(s for s in text if s.startswith("SPAWN_3 ")).split()[1:]))
    # Walkable eight-neighbour grid with Euclidean edge weights, no diagonal corner cutting.
    def walk(x,y):
        return 0 <= x < w and 0 <= y < h and rows[y][x] in '."'
    centre={(x,y) for x in (w//2-1,w//2) for y in (h//2-1,h//2) if walk(x,y)}
    # Moonwell is a well, not a solid centre tile. Race to the final courtyard ring
    # instead of inventing a path through the pit.
    if not centre:
        j=text.index("COLLAPSE")+1
        goals={(x,y) for x in range(w) for y in range(h) if walk(x,y) and text[j+y][x]=="."}
    else: goals=centre
    result = []
    for x, y in zip(coords[::2], coords[1::2]):
        sx, sy = int(x*2), int(y*2)
        todo = [(math.hypot(x-(sx+.5)/2,y-(sy+.5)/2), sx, sy)]
        seen = set()
        while todo:
            d, cx, cy = heapq.heappop(todo)
            if (cx,cy) in seen:
                continue
            seen.add((cx,cy))
            if (cx,cy) in goals:
                result.append(d + (math.sqrt(2)/4 if centre else 0))
                break
            for dx in (-1,0,1):
                for dy in (-1,0,1):
                    tx, ty = cx+dx, cy+dy
                    if (dx or dy) and walk(tx,ty) and (not(dx and dy) or walk(cx,ty) and walk(tx,cy)):
                        heapq.heappush(todo, (d + math.hypot(dx,dy)/2,tx,ty))
        else:
            raise ValueError(f"unreachable centre in {file.name}: {x},{y}")
    return result


if __name__ == "__main__":
    for file in sorted((Path(__file__).resolve().parents[1] / "maps").glob("*.txt")):
        if "MODES_EXT ffa3" not in file.read_text(encoding="utf-8").splitlines():
            continue
        ds = distances(file)
        spread = max(ds)/min(ds)-1
        print(f"{file.stem}: " + ", ".join(f"{v:.3f} m" for v in ds) + f"; spread {spread:.2%}")
        if spread > .10:
            raise SystemExit("map route spread exceeds 10%")
