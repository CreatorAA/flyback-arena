"""Check walkable routes after each collapse and each new arena's tactical detour."""
from collections import deque
from pathlib import Path

MAPS = Path(__file__).resolve().parents[1] / "maps"
CARDINAL = ((1, 0), (-1, 0), (0, 1), (0, -1))

def component(nodes, start):
    seen = {start}
    queue = deque([start])
    while queue:
        x, y = queue.popleft()
        for dx, dy in CARDINAL:
            v = x+dx, y+dy
            if v in nodes and v not in seen:
                seen.add(v)
                queue.append(v)
    return seen

def check_map(name):
    lines = (MAPS / (name+".txt")).read_text(encoding="utf-8").splitlines()
    gi = next(i for i, s in enumerate(lines) if s.startswith("GRID "))
    width, height = map(int, lines[gi].split()[1:])
    world = lines[gi+1:gi+1+height]
    wi = lines.index("COLLAPSE")
    stages = lines[wi+1:wi+1+height]
    def xy(p):
        return ((p[0]+.5-width/2)*.5, (p[1]+.5-height/2)*.5)
    def floor(after):
        active = lambda x, y: stages[y][x]=="." or int(stages[y][x], 36)>after
        walls = {(x,y) for y in range(height) for x in range(width) if world[y][x]=="#" and active(x,y)}
        # A 400 mm body intersects a wall in any adjacent cardinal *or diagonal*
        # cell (353.55 mm from the diagonal corner to the centre of a 500 mm tile).
        # This conservative graph permits only safe cell centres and cardinal hops.
        return {(x,y) for y in range(height) for x in range(width) if world[y][x] in '."'
                and active(x,y) and all((x+dx,y+dy) not in walls for dx in (-1,0,1) for dy in (-1,0,1))}
    def connected(nodes, seats):
        assert nodes and all(p in nodes for p in seats), (name,"unsafe endpoint",seats)
        assert set(seats)<=component(nodes,seats[0]), (name,"no bypass",seats)
    spawns = []
    for line in lines[:gi]:
        if line.startswith("SPAWN"):
            raw = list(map(float, line.split()[1 if line.startswith("SPAWN_3") else 2:]))
            spawns.append([(int(x*2),int(y*2)) for x,y in zip(raw[::2],raw[1::2])])
    wave_count = max(int(ch,36) for row in stages for ch in row if ch!=".")+1
    sizes = []
    for wave in range(-1,wave_count):
        cells = floor(wave)
        visited = component(cells,next(iter(cells)))
        assert visited==cells, (name,"surviving paths disconnected",wave,len(cells-visited))
        sizes.append(len(cells))
    initial = floor(-1)
    for seats in spawns: connected(initial,seats)
    if name=="tidelock":
        # With the centre crossing closed, either the north or the south lock
        # can still take a player from the west berth to the east berth.
        for sign in (-1,1):
            bypass={p for p in initial if abs(xy(p)[0])>4.7 or sign*xy(p)[1]>4.45}
            connected(bypass,spawns[0])
    elif name=="kiln":
        # All four hearths remain linked without cutting through the exposed square.
        connected({p for p in initial if sum(v*v for v in xy(p))>3.3**2},spawns[-1])
    elif name=="windrose":
        # The mid-blade loop links all three spawns even if the entire hub is contested.
        connected({p for p in initial if sum(v*v for v in xy(p))>2.65**2},spawns[0])
    else:
        # Clockwise and counterclockwise approaches survive even in the final ring.
        final = floor(wave_count-1)
        for sign in (-1,1):
            half={p for p in final if sign*xy(p)[1]>=-.25}
            a=min(half,key=lambda p:(p[0], abs(xy(p)[1])))
            b=max(half,key=lambda p:(p[0], -abs(xy(p)[1])))
            connected(half,[a,b])
    print(f"{name}: connected safe centre samples {sizes}; alternate routes passed")

if __name__=="__main__":
    for name in ("tidelock","kiln","windrose","moonwell"):check_map(name)
