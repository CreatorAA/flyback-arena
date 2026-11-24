"""Export the programme's flying voxel dart at its filmed scale, in each player colour."""
import sys
from pathlib import Path
import bpy
root=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(Path(__file__).resolve().parent))
sys.path.insert(0,str(root/"flyback/client/tools"))
import upstream_art
import chars_voxel as vox
from geom import MeshBuilder
from export_backdrops import prepare_material

out=root/"flyback/client/art/darts";out.mkdir(parents=True,exist_ok=True)
for i,col in enumerate(["#22bf92","#ef892a","#568eff","#d74f98"]):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    p=vox.dart_part(col);d=p.mesh(outline_w=0,unit=vox.DART_CELL)
    mb=MeshBuilder()
    mb.add(d["V"]*1.42,d["F"],0,vcolors=d["C"],smooth=False)
    mb.build("Three curved blades",[prepare_material("Flight paint")],bpy.context.scene.collection)
    bpy.ops.export_scene.gltf(filepath=str(out/f"flight_s{i}.glb"),export_format="GLB",export_yup=True,
        export_cameras=False,export_lights=False,export_vertex_color="ACTIVE",export_animations=False)
