"""Two real Flyback-bridge clients, private profiles, one human-only room; local/VM interoperability."""
import argparse, json, os, queue, subprocess, tempfile, threading, time
from pathlib import Path

class Bridge:
    def __init__(self, exe, home, profile):
        self.events = queue.Queue()
        self.p = subprocess.Popen([str(exe), "client"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env={**os.environ,"FLYBACK_HOME":str(home),"FLYBACK_PROFILE":str(profile)},
                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        def read():
            try:
                for line in self.p.stdout:
                    self.events.put(json.loads(line.decode("utf-8-sig")))
            finally:
                self.events.put(None)
        threading.Thread(target=read, daemon=True).start()
    def cmd(self, op, **kwargs):
        self.p.stdin.write((json.dumps({"op":op,**kwargs},ensure_ascii=False)+"\n").encode("utf-8"));self.p.stdin.flush()
    def get(self, t, pred=lambda _:True, timeout=35):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            try: e=self.events.get(timeout=max(.01,end-time.monotonic()))
            except queue.Empty:break
            if e is None:raise RuntimeError(f"bridge exited: {self.p.stderr.read(1000)!r}")
            if e.get("type") in ("error","trust"):raise RuntimeError(str(e))
            if e.get("type")==t and pred(e):return e
        raise TimeoutError(f"waiting for {t}")
    def close(self):
        if self.p.poll() is None:
            self.p.stdin.close()
            try:self.p.wait(timeout=5)
            except subprocess.TimeoutExpired:self.p.terminate();self.p.wait(timeout=5)

def main():
    a=argparse.ArgumentParser();a.add_argument("bridge",type=Path);a.add_argument("home",type=Path);a.add_argument("endpoint");a.add_argument("fingerprint");args=a.parse_args()
    if args.endpoint.split(":")[0] not in ("127.0.0.1","10.0.2.2"):raise ValueError("local/VM fixture only")
    with tempfile.TemporaryDirectory(prefix="flyback-interop-") as tmp:
        b=[]
        try:
            for k in range(2):
                profile=Path(tmp)/f"p{k}"; profile.mkdir()
                c=Bridge(args.bridge.resolve(),args.home.resolve(),profile);b.append(c);c.get("catalog")
                c.cmd("connect",address=args.endpoint,name=f"Interop-{k}",fingerprint=args.fingerprint);tls=c.get("connected");assert tls["fingerprint"]==args.fingerprint
                directory=c.get("directory")
            b[0].cmd("create",settings={"title":"cross-OS human duel","seat_count":2,"maps":["twinlakes"],"map_policy":"fixed","slots":["human","human"]})
            r=b[0].get("room");rid=r["id"];b[1].cmd("join",room_id=rid,role="player");r1=b[1].get("room");assert r1["self_seat"]==1
            for c in b:c.cmd("ready",ready=True)
            ready=b[0].get("room",lambda x:sum(m["ready"] and m["seat"]>=0 for m in x["members"])==2);b[0].cmd("start",room_rev=ready["room_rev"])
            manifests=[c.get("manifest") for c in b];first=[c.get("snapshot") for c in b];seen=first
            target=max(x["tick"] for x in first)+10
            for _ in range(3):
                b[0].cmd("input",mx=1000,my=0,act=0,ax=1000,ay=0);b[1].cmd("input",mx=0,my=0,act=0,ax=-1000,ay=0);time.sleep(.06)
            seen=[c.get("snapshot",lambda x:x["tick"]>=target and x["last_applied_input_seq"]>=1) for c in b]
            print(json.dumps({"server":directory["name"],"endpoint":args.endpoint,"pinned_tls_clients":2,"room":rid,"human_seats":[0,1],"map":manifests[0].get("map","twinlakes"),"initial_ticks":[x["tick"] for x in first],"observed_ticks":[x["tick"] for x in seen],"consumed_input_seq":[x["last_applied_input_seq"] for x in seen],"first_player_x_before":first[0]["players"][0]["x"],"first_player_x_after":seen[0]["players"][0]["x"]},ensure_ascii=False))
        finally:
            for c in b:c.close()
if __name__=="__main__":main()
