"""Bake the original programme's non-playable islands, trees and falling water to glTF.
First run build_decor_cache.py out/flyback-art-cache with the renderer Python environment.
Blender -b --python export_backdrops.py -- --map twinlakes --cache out/flyback-art-cache
The output transforms game metres (x,y) to Godot (x,0,y). No combat geometry or collision is baked.
"""
import argparse, os, sys
from pathlib import Path
from types import SimpleNamespace
import bpy
import numpy as np
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(root/"flyback/client/tools"))
import upstream_art
import env, scene_lib as sl, mapfile, props
sys.path.insert(0,str(Path(__file__).resolve().parent))
from art_sources import LOOKS
import arena_landmarks
arena_landmarks.register()

def prepare_material(name="OriginalPaint",water=False):
    mat=bpy.data.materials.new(name)
    mat.use_nodes=True
    nodes=mat.node_tree.nodes
    p=nodes.get("Principled BSDF")
    p.inputs["Roughness"].default_value=.91
    p.inputs["Metallic"].default_value=0
    col=nodes.new("ShaderNodeVertexColor");col.layer_name="col"
    mat.node_tree.links.new(col.outputs["Color"],p.inputs["Base Color"])
    if water:
        mat.surface_render_method="DITHERED"
        mat.node_tree.links.new(col.outputs["Alpha"],p.inputs["Alpha"])
    return mat

def terrain_paint(ob):
    mesh=ob.data;n=len(mesh.vertices)
    depth=np.zeros(n,np.float32);mesh.attributes["depth"].data.foreach_get("value",depth)
    facet=np.zeros_like(depth);mesh.attributes["facet"].data.foreach_get("value",facet)
    edge=np.zeros_like(depth);mesh.attributes["edge_s"].data.foreach_get("value",edge)
    pts=np.zeros(n*3,np.float32);mesh.vertices.foreach_get("co",pts);pts=pts.reshape((-1,3))
    norm=np.zeros(n*3,np.float32);mesh.vertices.foreach_get("normal",norm);nz=norm.reshape((-1,3))[:,2]
    palette=env.PAL
    def c(k):return np.array(sl.hexc(palette[k])[:3])
    palette_keys=["grass","lip","soil","rock_a","rock_b","rock_c","rock_deep","rock_under"]
    levels=[0,.09,.21,.33,1.4,2.94,5.25,8.0]
    colors=np.stack([c(k) for k in palette_keys]);rgb=np.column_stack([np.interp(depth,levels,colors[:,i]) for i in range(3)])
    top=(nz>.48)&(depth<.16)
    rim=np.clip((.28-edge)/.25,0,1)[:,None]
    grass=c("grass")*(1-rim*.5)+c("grass_rim")*rim*.5
    rgb[top]=grass[top]
    rgb*=1+np.clip(facet,-1,1)[:,None]*.08
    haze=np.clip((-pts[:,2]-2)/13,0,.52)[:,None]
    rgb=rgb*(1-haze)+c("haze")*haze
    col=np.ones((n,4),np.float32);col[:,:3]=np.clip(rgb,0,1)
    attr=mesh.color_attributes.new(name="col",type="FLOAT_COLOR",domain="POINT")
    attr.data.foreach_set("color",col.ravel())

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--map",nargs="+",default=["twinlakes"])
    parser.add_argument("--cache",type=Path,default=root/"out/flyback-art-cache")
    parser.add_argument("--dest",type=Path,default=root/"flyback/client/art/backdrops")
    args=parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    args.dest.mkdir(exist_ok=True,parents=True)
    dd=np.load(args.cache/"decor.npz")
    for map_id in args.map:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        # The programme uses procedural Cycles graphs. Its vertex-colour gradients travel
        # in glTF; terrain cliffs are sampled here before decimation, rather than exporting white.
        vc=prepare_material();water=prepare_material("FallingWater",True)
        env.mat_terrain=lambda *a,**kw:vc
        sl.mat_vcol=lambda *a,**kw:vc
        sl.mat_water=lambda *a,**kw:water
        sl.mat_cloud=lambda *a,**kw:vc
        definition=root/"boomerang/renderer/maps"/(map_id+".txt")
        if definition.is_file():
            m=mapfile.read_map(str(definition));W,H=m["W"]*m["cell"],m["H"]*m["cell"];theme=m["theme"]
        else:
            lines=(root/"flyback/native/game/maps"/(map_id+".txt")).read_text(encoding="utf-8").splitlines()
            _,w,h=next(line for line in lines if line.startswith("GRID ")).split()
            W,H=int(w)*.5,int(h)*.5
            theme=map_id if map_id in LOOKS else ("meadow" if map_id=="trident" else "tropical")
        env.apply_theme(SimpleNamespace(mapname=map_id,theme=theme))
        coll=env.new_coll("Distant islands");rng=np.random.default_rng(5)
        placement=env.auto_decor(W,H)
        env.place_decor(placement,coll,rng,dd,center=(W/2,-H/2))
        nm,X,Y,Z,rot,_=placement[0]
        if env.THEME.get("decor",{}).get("waterfall",True):
            env.waterfall_from(dd,nm,X,Y,Z+env.THEME.get("decor",{}).get("dz",0),rot,-H*.62,coll)
        for ob in list(coll.objects):
            if ob.type!="MESH":continue
            if ob.data.attributes.get("depth"):terrain_paint(ob)
            # This is distant scenery. Keep canopies round; spend fewer triangles on deep cliffs.
            faces=len(ob.data.polygons)
            if faces<1400:continue
            ratio=.19 if ob.data.attributes.get("depth") else .4
            mod=ob.modifiers.new("Distant silhouette","DECIMATE");mod.ratio=ratio
            bpy.context.view_layer.objects.active=ob
            bpy.ops.object.modifier_apply(modifier=mod.name)
        path=args.dest/(map_id+".glb")
        bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",export_yup=True,export_animations=False,export_cameras=False,export_lights=False,export_attributes=False,export_vertex_color="ACTIVE",export_all_vertex_colors=False,export_extras=False)
        print("EXPORTED",path,path.stat().st_size,flush=True)
if __name__=="__main__":main()
