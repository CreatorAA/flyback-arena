"""Measure real bridge/QUIC opening events and reject pre-start inputs. Pass the built bin directory."""
import json
import pathlib
import queue
import tempfile
import time
from live import Proc, settings


def exercise(tmp):
    peers = []
    def client(name):
        p = Proc("flyback-bridge", ["client"], tmp, tmp/name)
        peers.append(p)
        assert "field_ready" in p.wait("catalog")["local_features"]
        p.send("enable_field_ready")
        return p

    def field(p):
        p.wait("manifest")
        zero = p.wait("snapshot", lambda s:s["tick"] == 0)
        loading = p.wait("room", lambda r:r.get("field_ready_required") and r["state"] == "loading")
        assert loading["speed"] == .8 and zero["pov"] == loading["self_seat"]
        return {"room_id":loading["id"], "round_epoch":loading["round_epoch"]}

    def no_start(p, duration):
        end = time.monotonic() + duration
        while True:
            try: e = p.q.get(timeout=max(0, end-time.monotonic()))
            except queue.Empty: break
            assert not (e["type"] == "snapshot" and e["tick"] > 0), e
            assert not (e["type"] == "room" and e["state"] in ("countdown","playing")), e
            if e["type"] == "error": raise AssertionError(e)

    def measure(p):
        marks = []
        for count in (3,2,1):
            p.wait("room",lambda r:r["state"]=="countdown" and r["countdown_seconds"]==count)
            marks.append(time.monotonic())
            p.send("input",seq=100+(3-count),mx=1000,my=0,ax=1000,ay=0,act=1)
        playing = p.wait("room",lambda r:r["state"]=="playing")
        marks.append(time.monotonic())
        assert all(.78 < b-a < 1.23 for a,b in zip(marks,marks[1:])), marks
        assert 2.85 <= marks[-1]-marks[0] <= 3.20
        first = p.wait("snapshot",lambda s:s["tick"]>=1)
        assert first["last_applied_input_seq"] < 0 and first["darts"][first["pov"]]["st"] == 0, first
        begin = time.monotonic()
        # A fresh legal neutral input must not pull a buffered pre-start throw into play.
        p.send("input",seq=103,mx=0,my=0,ax=1000,ay=0,act=0)
        ack=p.wait("snapshot",lambda s:s.get("last_applied_input_seq")==103)
        assert ack["darts"][ack["pov"]]["st"] == 0, ack
        last=p.wait("snapshot",lambda s:s["tick"]>=first["tick"]+14)
        tps=(last["tick"]-first["tick"])/(time.monotonic()-begin)
        assert 15.2 < tps < 16.8, tps
        return {"speed":playing["speed"],"countdown_seconds":round(marks[-1]-marks[0],3),
                "steps_per_second":round(tps,2),"first_step_applied_seq":first["last_applied_input_seq"]}

    def leave(p):
        p.send("leave");p.wait("left");p.past.clear()

    try:
        h=client("human")
        h.send("local",name="试跑",settings=settings(2,["human","gpt-6.2-astra"],maps=["tidelock"]))
        room=h.wait("room",lambda r:r["mode"]=="offline" and r["self_seat"]==0)
        h.send("start",room_rev=room["room_rev"])
        token=field(h)
        h.send("field_ready",room_id=token["room_id"],round_epoch=token["round_epoch"]-1)
        no_start(h,.5)
        h.send("field_ready",**token)
        result={"local_human":measure(h)}
        for speed in (1,.5,2,.8):
            h.send("speed",value=speed);h.wait("room",lambda r:r["speed"]==speed)
        leave(h)

        b=client("guest")
        h.send("host",name="主场",bind="127.0.0.1",port=0,
               settings=settings(2,["human","human"],maps=["stonecircle"]))
        room=h.wait("room");b.send("connect",address=f"127.0.0.1:{room['port']}",name="来客")
        b.wait("trust");b.send("trust",accept=True);b.wait("directory")
        b.send("join",room_id=room["id"],role="player");b.wait("room",lambda r:r["self_seat"]==1)
        for p in (h,b):p.send("ready",ready=True)
        r=h.wait("room",lambda r:all(s["ready"] for s in r["slots"]))
        h.send("start",room_rev=r["room_rev"]);host=field(h);guest=field(b)
        h.send("field_ready",**host);no_start(h,.6)
        b.send("field_ready",**guest);result["lan_guest"]=measure(b)
        h.send("speed",value=2);h.wait("error",lambda e:e["code"]=="fixed_rate")
        leave(b);leave(h)

        h.send("local",settings=settings(2,["gpt-6.2-astra","claude-fable-5.2"],maps=["twinlakes"]))
        room=h.wait("room",lambda r:r["mode"]=="offline" and r["self_seat"]==-1)
        h.send("start",room_rev=room["room_rev"])
        h.wait("manifest")
        # An observer cannot delay the competition by withholding field_ready.
        c=h.wait("room",lambda r:r["state"]=="countdown" and r["countdown_seconds"]==3)
        assert c["speed"]==1
        h.wait("room",lambda r:r["state"]=="playing")
        f=h.wait("snapshot",lambda s:s["tick"]>=1);at=time.monotonic()
        g=h.wait("snapshot",lambda s:s["tick"]>=f["tick"]+20)
        actual=(g["tick"]-f["tick"])/(time.monotonic()-at)
        assert 18 < actual < 22, actual
        result["pure_ai"]={"speed":c["speed"],"steps_per_second":round(actual,2)}
        print(json.dumps(result,indent=2))
    finally:
        for p in reversed(peers):p.close()

if __name__=="__main__":
    with tempfile.TemporaryDirectory(prefix="flyback-opening-") as d:exercise(pathlib.Path(d))
