"""Bake lightweight render-only cliff meshes for the six archived arenas.

Uses the original programme's signed-distance island generator at realtime resolution.
Godot loads one mesh per collapse state; authoritative walkability stays on the grid.
"""
from pathlib import Path
import json
import struct
import sys
import numpy as np
HERE=Path(__file__).resolve().parents[1]
ROOT=HERE.parents[1]
import upstream_art
import mapfile, terrain
from art_sources import read_map,MAPS

def write_glb(path,scenes):
    chunks=bytearray(); views=[]; access=[]; meshes=[]; nodes=[]
    def acc(arr,typeid,typ,extrema=False):
        off=len(chunks);buf=arr.tobytes();chunks.extend(buf)
        while len(chunks)%4:chunks.append(0)
        i=len(views);views.append({"buffer":0,"byteOffset":off,"byteLength":len(buf)})
        item={"bufferView":i,"componentType":typeid,"count":len(arr),"type":typ}
        if extrema:item.update(min=arr.min(axis=0).tolist(),max=arr.max(axis=0).tolist())
        access.append(item);return len(access)-1
    for tick,(v,f,attrs) in enumerate(scenes):
        if len(f)==0:continue
        v=v[:,[0,2,1]].copy();v[:,2]*=-1
        # Topographic depth asymptotes before reaching the cloud band.
        v[:,1]=np.where(v[:,1]<-1.8,-1.8-1.4*(1-np.exp((v[:,1]+1.8)/2.4)),v[:,1])
        a,b,c=v[f[:,0]],v[f[:,1]],v[f[:,2]]
        face_norm=np.cross(b-a,c-a);normal=np.zeros_like(v)
        for k in range(3):np.add.at(normal,f[:,k],face_norm)
        normal/=np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-8)
        if normal[v[:,1]>-.1,1].mean()<0:normal*=-1;f=f[:,::-1]
        pos=acc(v.astype("<f4"),5126,"VEC3",True);norm=acc(normal.astype("<f4"),5126,"VEC3")
        inds=acc(f.flatten().astype("<u2" if len(v)<65536 else "<u4"),5123 if len(v)<65536 else 5125,"SCALAR")
        meshes.append({"primitives":[{"attributes":{"POSITION":pos,"NORMAL":norm},"indices":inds}]})
        nodes.append({"mesh":len(meshes)-1,"name":f"Phase{tick}"})
    j={"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":list(range(len(nodes)))}],"meshes":meshes,"nodes":nodes,"buffers":[{"byteLength":len(chunks)}],"bufferViews":views,"accessors":access}
    meta=json.dumps(j,separators=(",",":")).encode();meta+=b" "*((-len(meta))%4)
    path.write_bytes(struct.pack("<4sII",b"glTF",2,28+len(meta)+len(chunks))+struct.pack("<I4s",len(meta),b"JSON")+meta+struct.pack("<I4s",len(chunks),b"BIN\x00")+chunks)

for name in (sys.argv[1:] or MAPS):
    m=read_map(name)
    mask=np.array([[c!="~" for c in r] for r in m["grid"]])
    wave=np.array([[int(c,36) if c!="." else 99 for c in r] for r in m["wave"]])
    phases=[]
    for k in range(max(wave[wave<99].tolist() or [-1])+2):
        alive=mask & (wave>=k)
        raster,origin=terrain.rasterize_cells(alive,m["cell"],0.75,0.13)
        phases.append(terrain.build_island(raster,origin,res=.13,mc_step=2,seed=3,corner_r=.19,depth_max=4.2,voro_seed=3))
    path=HERE/"art/ground"/f"{name}.glb";write_glb(path,phases);print(name,path.stat().st_size,flush=True)
