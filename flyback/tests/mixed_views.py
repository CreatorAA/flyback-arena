"""One LAN room, one real AI. Compare the same match on three observation clocks.
Usage: python flyback/tests/mixed_views.py APP_BIN
"""
import json
import pathlib
import queue
import tempfile
import time

from live import Proc


def run(tmp):
    clients = []
    def client(name):
        p = Proc("flyback-bridge", ["client"], tmp, tmp/name)
        clients.append(p)
        p.wait("catalog")
        return p
    try:
        human = client("human")
        human.send("host", name="Player", bind="127.0.0.1", port=0,
                   settings={"seat_count": 2, "slots": ["human", "claude-fable-5.2"],
                             "maps": ["stonecircle"], "map_policy": "fixed"})
        room = human.wait("room")
        spectators = []
        for name in ["human-pov", "ai-pov"]:
            p = client(name)
            p.send("connect", name=name, address=f"127.0.0.1:{room['port']}")
            p.wait("trust")
            p.send("trust", accept=True)
            p.wait("connected")
            p.send("join", room_id=room["id"], role="spectator")
            p.wait("room", lambda r: r["self_seat"] == -1)
            spectators.append(p)
        human.send("ready", ready=True)
        r = human.wait("room", lambda r: all(s["ready"] for s in r["slots"]))
        human.send("start", room_rev=r["room_rev"])
        man = human.wait("manifest")
        assert (man["human_observation_delay_ticks"], man["ai_observation_delay_ticks"]) == (0, 4)
        for i, p in enumerate(spectators):
            p.wait("manifest")
            p.send("watch", view=i)

        # Alternate a short strafe so the AI's t-4 differs measurably from t.
        samples = [{}, {}, {}]
        by_tick, watcher, ai = samples
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and not (max(watcher, default=-1)>=26 and max(ai, default=-1)>=26):
            for i, p in enumerate(clients):
                while True:
                    try: e = p.q.get_nowait()
                    except queue.Empty: break
                    if e.get("type") == "error": raise AssertionError(e)
                    if e.get("type") != "snapshot" or e["pov"] != (0, 0, 1)[i]: continue
                    t = e["tick"]
                    samples[i][t] = (e, time.monotonic())
                    if i == 0 and t < 26:
                        human.send("input", seq=t+1, mx=1000 if t%8<4 else -1000, my=0,
                                   act=1 if t==3 else 0, ax=0, ay=1000)
            time.sleep(.003)
        checks = changed = flying = 0
        waits = []
        for t, (v, released) in watcher.items():
            if t not in by_tick: continue
            live, seen = by_tick[t]
            assert v["players"] == live["players"] and v["darts"] == live["darts"], t
            waits.append(released - seen)
        for t, (v, released) in ai.items():
            old = max(0, t-4)
            if old not in by_tick or t not in by_tick: continue
            live, seen = by_tick[t]
            waits.append(released - seen)
            prior = by_tick[old][0]
            player, dart = v["players"][0], v["darts"][0]
            if player["vis"]:
                assert (player["x"], player["y"]) == (prior["players"][0]["x"], prior["players"][0]["y"]), t
                checks += 1
                changed += player["x"] != live["players"][0]["x"]
            if dart["vis"]:
                assert dart["st"] == prior["darts"][0]["st"] and (dart["x"], dart["y"]) == (prior["darts"][0]["x"], prior["darts"][0]["y"]), t
                flying += dart["st"] != 0 and dart["y"] != live["darts"][0]["y"]
        assert len(watcher)>=5 and checks>=5 and changed>=2 and flying>=1, (len(watcher),checks,changed,flying)
        # Wall timestamps are application delivery times on this loopback machine.
        assert min(waits) >= 2.85, waits
        print(json.dumps({"human_view_frames":len(watcher),"visible_ai_view_matches_t_minus_4":checks,
                          "movement_frames_distinguishing_t_from_t_minus_4":changed,
                          "dart_frames_distinguishing_t_from_t_minus_4":flying,
                          "minimum_observed_spectator_lag_sec":round(min(waits),3)}))
    finally:
        for p in reversed(clients): p.close()

if __name__=="__main__":
    with tempfile.TemporaryDirectory(prefix="flyback-mixed-pov-") as d:
        run(pathlib.Path(d))
