"""Live black-box acceptance; use a built application directory with data/{maps,bots}, admin-web and server.toml.
python flyback/tests/live.py out/flyback-win/bin
"""
import contextlib, json, os, pathlib, queue, subprocess, sys, tempfile, threading, time, urllib.request, urllib.error, http.cookiejar
from itertools import count
ROOT=pathlib.Path(sys.argv[1]).resolve()
EXE=".exe" if os.name=="nt" else ""
class Proc:
    def __init__(self,name,args,cwd,profile):
        self.p=subprocess.Popen([str(ROOT/(name+EXE)),*args],cwd=cwd,env={**os.environ,"FLYBACK_HOME":str(ROOT),"FLYBACK_PROFILE":str(profile)},stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,encoding="utf-8",errors="replace",bufsize=1)
        self.q=queue.Queue();self.past=[];self.errors=[]
        def read():
            for line in self.p.stdout:
                try: v=json.loads(line)
                except ValueError: continue
                self.q.put(v)
        threading.Thread(target=read,daemon=True).start()
        threading.Thread(target=lambda:self.errors.extend(self.p.stderr),daemon=True).start()
    def write(self,line):
        self.p.stdin.write(line+"\n");self.p.stdin.flush()
    def send(self,op,**data):self.write(json.dumps({"op":op,**data},ensure_ascii=False))
    def wait(self,kind,pred=lambda e:True,timeout=12):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            for i,e in enumerate(self.past):
                if e.get("type")==kind and pred(e):return self.past.pop(i)
            try:e=self.q.get(timeout=.15)
            except queue.Empty:
                if self.p.poll() is not None:break
                continue
            if e.get("type")==kind and pred(e):return e
            self.past.append(e)
        raise AssertionError(f"{kind} not received; exit={self.p.poll()}, recent={[(v.get('type'),v.get('state'),v.get('code'),v.get('message')) for v in self.past[-12:]]}; stderr={self.errors[-5:]}")
    def close(self):
        if self.p.poll() is None:
            self.p.stdin.close()
            try:self.p.wait(timeout=2)
            except subprocess.TimeoutExpired:self.p.terminate();self.p.wait(5)
def settings(n,slots,title="Smoke",maps=None):return {"seat_count":n,"title":title,"slots":slots,"maps":maps or [],"map_policy":"rotate","allow_mirrors":False}
def run(tmp):
    live=[]
    def proc(name,args,tag):
        p=Proc(name,args,tmp,tmp/tag);live.append(p);return p
    def client(tag):
        p=proc("flyback-bridge",["client"],tag);return p,p.wait("catalog")
    try:
        local,cat=client("local")
        assert len(cat["bots"])>=13 and len([m for m in cat["maps"] if 3 in m["seats"]])>=2
        for n in (2,3,4):
            local.send("local",name="选手",settings=settings(n,["human"]+["random"]*(n-1)))
            room=local.wait("room",lambda x:x.get("mode")=="offline" and len(x["slots"])==n)
            local.send("start",room_rev=room["room_rev"]);man=local.wait("manifest",lambda m:len(m["seats"])==n)
            assert len([s for s in man["seats"] if s["bot"]])==n-1
            snap=local.wait("snapshot",lambda x:x["tick"]>=1 and len(x["players"])==n)
            local.send("input",seq=n,mx=1000,my=0,act=0,ax=1000,ay=0)
            local.wait("snapshot",lambda x:x.get("last_applied_input_seq")==n)
            local.send("speed",value=2);local.wait("room",lambda r:r["speed"]==2)
            local.send("pause");local.wait("room",lambda r:r["state"]=="paused")
            local.send("pause");local.wait("room",lambda r:r["state"]=="playing" and r["speed"]==2)
            local.send("leave");local.wait("left");local.past.clear()
        cfg=tmp/"server.toml";cfg.write_text((ROOT/"server.toml").read_text(encoding="utf-8").replace('0.0.0.0:27860','127.0.0.1:0').replace('127.0.0.1:27861','127.0.0.1:0').replace('max_spectators_per_room = 32','max_spectators_per_room = 1'))
        pw="flyback-live-test-admin";init=subprocess.run([str(ROOT/("arena-server"+EXE)),"admin","init","--config",str(cfg)],input=f"{pw}\n{pw}\n",cwd=tmp,text=True,capture_output=True)
        assert init.returncode==0,init.stderr
        server=proc("arena-server",["--config",str(cfg)],"server");boot=server.wait("server_started");addr=f"127.0.0.1:{boot['udp_port']}"
        def connect(tag):
            c,_=client(tag);c.send("connect",name=tag,address=addr);challenge=c.wait("trust");assert challenge["fingerprint"]==boot["fingerprint"];c.send("trust",accept=True);c.wait("connected");c.wait("directory");return c
        owner,p2,p3,p4=[connect(f"net-{k}") for k in range(4)]
        owner.send("create",settings=settings(2,["human","human"]),password="入场");r=owner.wait("room");rid=r["id"]
        p2.send("join",room_id=rid,password="wrong",role="player");p2.wait("error",lambda e:e["code"]=="bad_password")
        p2.send("join",room_id=rid,password="入场",role="player");p2.wait("room",lambda x:x["self_seat"]==1)
        p2.send("ready",ready=True);owner.send("ready",ready=True)
        full=owner.wait("room",lambda r:len(r["members"])==2 and all(s["ready"] for s in r["slots"]))
        owner.send("start",room_rev=full["room_rev"]);owner.wait("manifest");p2.wait("manifest");owner.wait("snapshot",lambda s:s["tick"]>=1)
        owner.send("speed",value=2);owner.wait("error",lambda e:e["code"]=="fixed_rate")
        p3.send("join",room_id=rid,password="入场",role="player");p3.wait("room",lambda r:r["self_seat"]==-1);p3.wait("manifest")
        p4.send("join",room_id=rid,password="入场",role="spectator");p4.wait("error",lambda e:e["code"]=="spectator_full")
        owner.send("pause");p2.wait("room",lambda r:r["vote"] is not None and r["vote"]["yes"]==1)
        p3.send("vote",yes=True);p3.wait("error",lambda e:e["code"]=="not_voter")
        p2.send("vote",yes=True);owner.wait("room",lambda r:r["state"]=="paused")
        owner.send("pause");p2.wait("room",lambda r:r["state"]=="paused" and r["vote"] is not None);p2.send("vote",yes=True)
        owner.wait("room",lambda r:r["state"]=="playing" and r["timeline_epoch"]>=2)
        p3.wait("snapshot",lambda s:s["pov"]==-1,timeout=7)
        # A second room shares the same UDP listener and is not paused by the first.
        p4.send("create",settings=settings(2,["human","random"]));r2=p4.wait("room");assert r2["id"]!=rid and r2["port"]==boot["udp_port"]
        p4.send("ready",ready=True);p4.send("start",room_rev=r2["room_rev"]);p4.wait("snapshot",lambda s:s["tick"]>=1)
        base=f"http://127.0.0.1:{boot['admin_port']}"; web=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        def fetch(path,data=None,csrf=None):
            req=urllib.request.Request(base+path,data=json.dumps(data).encode() if data is not None else None,headers={"Content-Type":"application/json",**({"X-CSRF-Token":csrf} if csrf else {})})
            with web.open(req,timeout=6) as res:return json.load(res)
        assert not fetch("/api/v1/session")["authenticated"];auth=fetch("/api/v1/auth/login",{"username":"admin","password":pw})
        state=fetch("/api/v1/state");assert state["rooms"];rev=state["config_revision"]
        cmd={"action":"set_capacity","target":"capacity","expected_revision":rev,"payload":{"max_spectators_per_room":0}}
        res=fetch("/api/v1/commands",cmd,auth["csrf_token"]);assert res["status"]=="accepted"
        for _ in range(25):
            state=fetch("/api/v1/state")
            if state["config_revision"]>rev:break
            time.sleep(.1)
        room=next(r for r in state["rooms"] if r["room_id"]==rid);assert (room["spectator_count"],room["effective_spectator_limit"])==(1,0)
        try:fetch("/api/v1/commands",cmd,auth["csrf_token"]);raise AssertionError("stale write accepted")
        except urllib.error.HTTPError as exc:assert exc.code==409
        server.write("stop");assert server.p.wait(10)==0  # Keep the pipe open while waiting.
        if os.name=="posix":
            signalled=proc("arena-server",["--config",str(cfg)],"signal-test")
            signalled.wait("server_started")
            signalled.write("rooms");signalled.wait("directory")
            signalled.p.stdin.write("partial");signalled.p.stdin.flush()
            signalled.p.terminate()
            assert signalled.p.wait(10)==0
        print("PASS 2/3/4 real AI, player inputs, offline speed/pause, encrypted QUIC with pin, password, late spectator and cap, public vote/resume, two rooms, admin login/command/revision", flush=True)
    finally:
        for p in reversed(live):p.close()
if __name__=="__main__":
    with tempfile.TemporaryDirectory(prefix="flyback-live-") as p:run(pathlib.Path(p))
