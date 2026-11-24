"""Export the original art director's grass per actual rule cell, ready for local bending/collapse.
Blender -b --python export_grass.py   (from any directory, after the repo is available)
"""
import sys
from pathlib import Path
from types import SimpleNamespace
import bpy,numpy as np
folder=Path(__file__).resolve().parent;sys.path.insert(0,str(folder))
from export_backdrops import root,env,sl,prepare_material,mapfile
def main():
    dst=root/"flyback/client/art/backdrops";dst.mkdir(exist_ok=True,parents=True)
    from art_sources import MAPS,LOOKS
    for key in (sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else MAPS):
        p=root/"boomerang/renderer/maps"/(key+".txt")
        if not p.exists():p=root/"flyback/native/game/maps"/(key+".txt")
        lines=p.read_text(encoding="utf-8").splitlines()
        idx=next(i for i,l in enumerate(lines) if l.startswith("GRID "))
        _,w,h=lines[idx].split();grid=[[{".":0,"#":1,"~":2,'"':3}[char] for char in row] for row in lines[idx+1:idx+1+int(h)]]
        assert all(len(r)==int(w) for r in grid)
        bpy.ops.wm.read_factory_settings(use_empty=True)
        env.apply_theme(SimpleNamespace(mapname=key,theme=key if key in LOOKS else next((l[9:].strip() for l in lines if l.startswith("// THEME ")),None)))
        vc=prepare_material("Leaf gradients")
        vc.use_backface_culling=False
        sl.mat_vcol=lambda *a,**k:vc
        coll=env.new_coll("C_GRASS");groups=env.Groups();blocks={}
        env.add_tall_grass(groups,lambda x,y:-1,grid,.5,np.random.default_rng(5),blocks=blocks)
        for (x,y),mb in sorted(blocks.items()):
            if mb.n:
                ob=mb.build("Grass_%d_%d"%(x,y),groups.materials(),coll)
                ob.location=((x+.5)*.5,-(y+.5)*.5,0)
        path=dst/(key+"_grass.glb")
        bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",export_yup=True,export_animations=False,export_cameras=False,export_lights=False,export_vertex_color="ACTIVE",export_attributes=False)
        print("GRASS",key,len(blocks),path.stat().st_size,flush=True)
if __name__=="__main__":main()
