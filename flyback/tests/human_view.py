"""Two real human seats, distinct pinned QUIC clients. Compare the authorized fields at one simulation tick.
Usage: python human_view.py APP_BIN [--report result.json]. No editor, external server or test-only room API.
"""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"native/net"))
from interop_clients import Bridge

def check(home,tmp):
    ext=".exe" if os.name=="nt" else ""
    config=tmp/"server.toml"
    config.write_text('[server]\nname="Human view fixture"\nlisten="127.0.0.1:0"\n[storage]\ndata_dir="./data"\n'
                      '[admin.web]\nenabled=false\n[replays]\nenabled=false\n',encoding="utf-8")
    env={**os.environ,"FLYBACK_HOME":str(home)}
    proc=subprocess.Popen([str(home/("arena-server"+ext)),"--config",str(config)],env=env,cwd=tmp,stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    peers=[]
    try:
        start=json.loads(proc.stdout.readline());assert start["type"]=="server_started"
        for i in range(2):
            profile=tmp/f"p{i}";profile.mkdir()
            b=Bridge(home/("flyback-bridge"+ext),home,profile);peers.append(b);b.get("catalog")
            b.cmd("connect",address=f"127.0.0.1:{start['udp_port']}",name=f"Peer-{i}",fingerprint=start["fingerprint"])
            assert b.get("connected")["fingerprint"]==start["fingerprint"];b.get("directory")
        a,b=peers
        a.cmd("create",settings={"title":"双真人实时视角","seat_count":2,"maps":["stonecircle"],"map_policy":"fixed","slots":["human","human"]})
        room=a.get("room");b.cmd("join",room_id=room["id"],role="player");assert b.get("room")["self_seat"]==1
        for c in peers:c.cmd("ready",ready=True)
        ready=a.get("room",lambda e:sum(m["ready"] and m["seat"]>=0 for m in e["members"])==2)
        a.cmd("start",room_rev=ready["room_rev"])
        manifests=[c.get("manifest") for c in peers];m=manifests[0]
        assert m["map"]=="stonecircle" and all(not s.get("bot") for s in m["seats"])
        snap=[c.get("snapshot", lambda frame: frame["tick"] >= 1) for c in peers]
        report={"map":m["map"],"pinned_quic_peers":2,"matched_ticks":0,"moving_seat_pairs":0,"moving_by_seat":[0,0],
                "player_mismatches":0,"airborne_dart_pairs":0,"dart_mismatches":0,"hit_receipts":[],
                "same_tick_player_example":None,"same_tick_dart_example":None}
        stop_at=None;thrown=False; hits=[{},{}]
        deadline=time.monotonic()+15; first_bad={}
        while time.monotonic()<deadline:
            if snap[0]["tick"]!=snap[1]["tick"]:
                k=int(snap[0]["tick"]>snap[1]["tick"]);snap[k]=peers[k].get("snapshot",timeout=3);continue
            t=snap[0]["tick"];report["matched_ticks"]+=1
            for k in (0,1):
                assert snap[k]["pov"]==k
                p=snap[k]["players"][k];other=snap[1-k]["players"][k]
                if p["vis"] and other["vis"] and (p["vx"] or p["vy"]):
                    owner=[p["x"],p["y"]];seen=[other["x"],other["y"]]
                    report["moving_seat_pairs"]+=1;report["moving_by_seat"][k]+=1
                    if owner!=seen:
                        report["player_mismatches"]+=1;first_bad.setdefault("player",[t,k,owner,seen])
                    elif report["same_tick_player_example"] is None:
                        report["same_tick_player_example"]={"tick":t,"seat":k,"owner_xy_mm":owner,"opponent_xy_mm":seen}
                own=snap[k]["darts"][k];seen=snap[1-k]["darts"][k]
                if own["vis"] and seen["vis"] and own["st"] in (1,2):
                    fields=("st","x","y","vx","vy")
                    v=[own[k] for k in fields];w=[seen[k] for k in fields]
                    report["airborne_dart_pairs"]+=1
                    if v!=w:
                        report["dart_mismatches"]+=1;first_bad.setdefault("dart",[t,k,v,w])
                    elif report["same_tick_dart_example"] is None:
                        report["same_tick_dart_example"]={"tick":t,"seat":k,"fields":fields,"owner":v,"opponent":w}
                for ev in snap[k].get("events",[]):
                    if ev["type"]=="HIT":hits[k][(ev["tick"],ev["ordinal"],ev["a"],ev["b"])]=t
            pos=[snap[k]["players"][k] for k in range(2)]
            dx=pos[1]["x"]-pos[0]["x"];dy=pos[1]["y"]-pos[0]["y"];dist=max(1,math.hypot(dx,dy))
            if stop_at is None and dist<=3500:stop_at=t
            ax=round(1000*dx/dist);ay=round(1000*dy/dist)
            for k,c in enumerate(peers):
                sign=1 if k==0 else -1
                # Move along the central corridor, then let both seats stop before the one directed throw.
                mx,my=(sign*ax,sign*ay) if stop_at is None else (0,0)
                act=1 if k==0 and not thrown and stop_at is not None and t>=stop_at+5 else 0
                if act:thrown=True
                c.cmd("input",mx=mx,my=my,ax=sign*ax,ay=sign*ay,act=act)
            if any(s.get("finished") for s in snap) or hits[0].keys()&hits[1].keys():break
            if stop_at is not None and t>=stop_at+40:break
            if t>=180:break
            try:snap=[c.get("snapshot",timeout=3) for c in peers]
            except TimeoutError as e:raise RuntimeError(f"snapshot ended at tick={t}, stop_at={stop_at}, dist={dist}, last={snap[0].get('events')}, report={report}, hit={hits}") from e
        for key in sorted(hits[0].keys()&hits[1].keys()):
            report["hit_receipts"].append({"rule_step":key[0],"ordinal":key[1],"thrower":key[2],"target":key[3],
                                          "delivery_ticks":[hits[0][key],hits[1][key]]})
        report["first_mismatch"]=first_bad
        return report
    finally:
        for c in peers:c.close()
        if proc.poll() is None:proc.communicate(b"stop\n",timeout=12)

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("app_bin",type=Path);p.add_argument("--report",type=Path)
    args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix="flyback-human-view-") as d:result=check(args.app_bin.resolve(),Path(d))
    text=json.dumps(result,ensure_ascii=False,indent=2);print(text)
    if args.report:args.report.write_text(text+"\n",encoding="utf-8")
    assert min(result["moving_by_seat"])>=5 and result["airborne_dart_pairs"]>=2,"insufficient movement/flight"
    assert not result["first_mismatch"],"same-tick human views disagree"
    assert result["hit_receipts"] and all(s["delivery_ticks"]==[s["rule_step"]+1]*2 for s in result["hit_receipts"]),"hit not heard on the current transition by both visible peers"
