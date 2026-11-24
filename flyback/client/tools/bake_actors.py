"""Keep the official voxel mesh and rig; align alternate face plates with the neutral plate."""
import json
import struct
from pathlib import Path

def bake_actors(source,dest):
    dest.mkdir(exist_ok=True,parents=True)
    for p in sorted(source.glob("*.glb")):
        raw=p.read_bytes()
        magic,version,total,n,kind=struct.unpack_from("<4sIIII",raw,0)
        if (magic,version,total,kind)!=(b"glTF",2,len(raw),0x4e4f534a):
            raise ValueError(f"{p}: expected glTF 2")
        doc=json.loads(raw[20:20+n])
        nodes={v["name"]:v for v in doc["nodes"]}
        head=nodes["head"];neutral=nodes["face_open"]
        for expression in ("face_happy","face_tight"):
            alt=nodes[expression]
            if doc["nodes"].index(alt) not in head["children"]:
                raise ValueError(f"{p.name}: {expression} is not attached to the head")
            old=alt.get("translation",[0,0,0]);rest=neutral.get("translation",[0,0,0])
            if old!=rest and any(abs(a+b-c)>1e-5 for a,b,c in zip(old,head["translation"],rest)):
                raise ValueError(f"{p.name}: unexpected {expression} rest pose")
            if "translation" in neutral:alt["translation"]=rest
            else:alt.pop("translation",None)
        meta=json.dumps(doc,ensure_ascii=False,separators=(",",":")).encode()
        meta+=b" "*((-len(meta))%4)
        # Keep the mesh buffers, face colours, joints and all other chunks unchanged.
        tail=raw[20+n:];encoded=struct.pack("<4sIIII",magic,version,20+len(meta)+len(tail),len(meta),kind)+meta+tail
        out=dest/p.name
        if not out.exists() or out.read_bytes()!=encoded:out.write_bytes(encoded)

if __name__=="__main__":
    client=Path(__file__).resolve().parents[1]
    from upstream_art import UPSTREAM
    bake_actors(UPSTREAM/"characters/boomerang",client/"art/actors")
