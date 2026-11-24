"""Black-box checks against packaged executables and one original replay (no Godot, no relay)."""
import pathlib, tempfile
from live import Proc, ROOT, settings

def main(tmp):
    c=Proc("flyback-bridge",["client"],tmp,tmp/"profile")
    try:
        c.wait("catalog")
        original=pathlib.Path(__file__).resolve().parents[2]/"docs/boomerang-fork/evidence/runtime/twinlakes-seed7.txt"
        c.send("replay",path=str(original))
        m=c.wait("manifest")
        assert [(s["bot"],s["model"]) for s in m["seats"]]==[("gpt-6.2-astra","astra"),("claude-fable-5.2","fable")]
        c.send("seek",tick=382);end=c.wait("result")
        assert (m["seed"],end["tick"],end["winner"])==(7,383,1)
        print("PASS original Astra vs Fable, models, tick 383, winner Fable",flush=True)
        c.send("leave");c.wait("left");c.past.clear()
        s=settings(2,["gpt-6.2-astra","claude-fable-5.2"],maps=["twinlakes","yinyang"])
        s.update(rounds_per_map=1,map_rounds={"twinlakes":2})
        c.send("local",name="裁判",settings=s);room=c.wait("room");c.send("start",room_rev=room["room_rev"])
        series_id=None
        for i,(mapid,roundnum) in enumerate([("twinlakes",1),("twinlakes",2),("yinyang",1)]):
            m=c.wait("manifest",timeout=10);v=m["series"];assert m["map"]==mapid and v["map_round"]==roundnum and v["completed"]==i
            if series_id is None:series_id=v["id"];c.send("speed",value=2)
            assert v["id"]==series_id and v["total"]==3
            for digit in (3,2,1):
                c.wait("room",lambda r:r["round_epoch"]==m["round_epoch"] and r["state"]=="countdown" and r["countdown_seconds"]==digit)
            c.wait("room",lambda r:r["round_epoch"]==m["round_epoch"] and r["state"]=="playing" and r["speed"]==2)
            e=c.wait("result",timeout=60)["series"]
            assert e["completed"]==i+1 and e["finished"]==(i==2) and all(p["rounds"]==i+1 for p in e["standings"])
        print("PASS map rounds 2+1, automatic next match, persistent standings over 3 actual games",flush=True)
        for _ in range(3):c.wait("replay_saved",timeout=10)
        c.send("leave");c.wait("left")
        c.send("replays");files=c.wait("replays")["items"]
        assert len(files)==3 and all(pathlib.Path(v["path"]).is_file() for v in files)
        # A second local room using the same profile must not replace the first room's index.
        s["map_policy"]="fixed";s["maps"]=["twinlakes"];s["map_rounds"]={}
        c.send("local",name="裁判",settings=s);r=c.wait("room",lambda m:m["state"]=="ready" and m["id"]!=room["id"])
        c.send("start",room_rev=r["room_rev"]);c.wait("manifest");c.send("speed",value=2);c.wait("result",timeout=60)
        c.wait("replay_saved");c.send("replays");all_files=c.wait("replays")["items"]
        assert len(all_files)==4 and {v["replay_id"] for v in files}<={v["replay_id"] for v in all_files}
        print("PASS asynchronous 4 full archives; prior local room history kept",flush=True)
    finally:c.close()

if __name__=="__main__":
    with tempfile.TemporaryDirectory(prefix="flyback-series-") as d:main(pathlib.Path(d))
