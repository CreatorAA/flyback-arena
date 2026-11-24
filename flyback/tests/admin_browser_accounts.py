"""Fast live regression for bound addresses and a CLI reset overlapping one sign-in.
Usage: python admin_browser_accounts.py APP_BIN [--server CANDIDATE_EXE]
No browser installation or existing game server required. Uses isolated accounts and ports.
"""
import argparse
import concurrent.futures
import http.cookiejar
import json
import os
import secrets
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

def check(app, exe, root):
    env={**os.environ,"FLYBACK_HOME":str(app),"FLYBACK_PROFILE":str(root/"profile")}
    cfg=root/"server.toml"
    cfg.write_text('[server]\nlisten="127.0.0.1:0"\n[admin.web]\nenabled=true\nlisten="127.0.0.1:0"\n'
                   '[storage]\ndata_dir="./state"\n',encoding="utf-8")
    flags=getattr(subprocess,"CREATE_NO_WINDOW",0)
    cmd=[str(exe),"--config",str(cfg)]
    server=subprocess.Popen(cmd,env=env,cwd=root,creationflags=flags,
                            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        live=json.loads(server.stdout.readline());assert live["type"]=="server_started"
        base=f"http://127.0.0.1:{live['admin_port']}/api/v1/"
        def session():return urllib.request.build_opener(urllib.request.ProxyHandler({}),urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        def fetch(jar,path,data=None):
            body=json.dumps(data).encode() if data is not None else None
            req=urllib.request.Request(base+path,data=body,headers={"Content-Type":"application/json"})
            try:
                with jar.open(req,timeout=10) as r:return r.status,json.load(r)
            except urllib.error.HTTPError as r:return r.code,json.load(r)
        def reset(p,role):
            r=subprocess.run(cmd+["admin","reset","--user","check","--role",role],env=env,cwd=root,
                             creationflags=flags,input=(p+"\n"+p+"\n").encode(),capture_output=True,timeout=15)
            assert r.returncode==0,r.stderr
        def login(jar,p):
            start=time.monotonic()
            code,res=fetch(jar,"auth/login",{"username":"check","password":p})
            return start,time.monotonic(),code,res
        def rotation(p):
            start=time.monotonic();reset(p,"viewer");return start,time.monotonic()
        old,new=secrets.token_urlsafe(26),secrets.token_urlsafe(26)
        normal,concurrent_login=session(),session()
        assert fetch(normal,"session")[1]["bootstrap_required"]
        reset(old,"admin");assert login(normal,old)[2]==200
        public=fetch(normal,"state")[1]["server"]
        assert public["listen_udp"]==f"127.0.0.1:{live['udp_port']}" and public["admin_listen"]==f"127.0.0.1:{live['admin_port']}",public
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            resetting=pool.submit(rotation,new)
            # The CLI starts its new KDF first. The old login is one ordinary HTTP sign-in
            # during the CLI operation; completion order can vary with OS scheduling.
            time.sleep(.035)
            signing=pool.submit(login,concurrent_login,old)
            s0,s1=resetting.result();v0,v1,code,_=signing.result()
        assert max(s0,v0)<min(s1,v1),(s1-s0,v1-v0)
        assert code in (200,401)
        assert fetch(normal,"state")[0]==401 and fetch(concurrent_login,"state")[0]==401
        assert login(normal,old)[2]==401
        assert login(normal,new)[3]["role"]=="viewer"
        print(json.dumps({"ephemeral_udp":live["udp_port"],"ephemeral_admin":live["admin_port"],
            "published_listeners_match":True,"overlap_ms":round(1000*(min(s1,v1)-max(s0,v0)),1),
            "overlapping_old_login_status":code,"no_usable_old_session":True,"role":"viewer"}))
    finally:
        if server.poll() is None:server.communicate(b"stop\n",timeout=10)

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("bin",type=Path);p.add_argument("--server",type=Path)
    a=p.parse_args();home=a.bin.resolve();server=a.server.resolve() if a.server else home/("arena-server.exe" if os.name=="nt" else "arena-server")
    with tempfile.TemporaryDirectory(prefix="flyback-admin-reset-") as tmp:check(home,server,Path(tmp))
