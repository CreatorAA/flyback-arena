"""Windows-only Unicode path regression. Usage: python windows_unicode.py APP_BIN"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"native/net"))
from interop_clients import Bridge

def run(app: Path, tmp: Path):
    home=tmp/"飞返 比赛资源";profile=tmp/"主持 档案 🎮";profile.mkdir()
    for folder in ["data/bots","data/maps","admin-web"]:
        shutil.copytree(app/folder,home/folder)
    config=tmp/"值班 配置"/"比赛.toml";config.parent.mkdir()
    config.write_text('[server]\nname="中文测试"\nlisten="127.0.0.1:0"\n[admin.web]\nenabled=false\n'
                      '[storage]\ndata_dir="./专服 存档"\n',encoding="utf-8")
    env={**os.environ,"FLYBACK_HOME":str(home),"FLYBACK_PROFILE":str(profile)}
    cmd=[str(app/"arena-server.exe"),"--config",str(config),"--assets",str(home/"data")]
    checked=subprocess.run(cmd+["check-config"],env=env,cwd=tmp,capture_output=True,timeout=10,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    assert checked.returncode==0 and str(config) in checked.stdout.decode("utf-8"),checked.stderr
    started=subprocess.Popen(cmd,env=env,cwd=tmp,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        line=started.stdout.readline();assert line,started.stderr.read()
        assert json.loads(line)["udp_port"]>0
    finally:
        if started.poll() is None:started.communicate(b"stop\n",timeout=8)
    assert (config.parent/"专服 存档/identity/server.crt").is_file()
    seed=Path(__file__).resolve().parents[2]/"docs/boomerang-fork/evidence/runtime/twinlakes-seed7.txt"
    tape=tmp/"双湖 原局"/"实况 🎮.txt";tape.parent.mkdir();shutil.copyfile(seed,tape)
    client=Bridge(app/"flyback-bridge.exe",home,profile)
    try:
        catalog=client.get("catalog")
        assert {m["id"] for m in catalog["maps"]} == {
            "twinlakes","yinyang","icefloes","ruins","skybridge","stonecircle",
            "trident","beacon","tidelock","kiln","windrose","moonwell"}
        assert all(b["available"] for b in catalog["bots"])
        client.cmd("host",name="主持",bind="127.0.0.1",port=0,settings={"title":"中文测试房","seat_count":2,
                   "slots":["human","human"],"maps":["twinlakes"],"map_policy":"fixed"})
        room=client.get("room");assert room["mode"]=="lan" and room["title"]=="中文测试房" and room["self_seat"]==0
        assert (profile/"identity/server.crt").is_file()
        client.cmd("replay",path=str(tape));m=client.get("manifest");s=client.get("snapshot",lambda v:v["tick"]>=1)
        assert m["map"]=="twinlakes" and m["seed"]==7
        print("PASS UTF-16 HOME/PROFILE, LAN room, host identity in requested profile, absolute config/assets, server start, JSON replay seed 7 tick",s["tick"])
    finally:client.close()
if __name__=="__main__":
    if os.name!="nt":raise SystemExit("Windows regression only")
    with tempfile.TemporaryDirectory(prefix="flyback-unicode-") as d:run(Path(sys.argv[1]).resolve(),Path(d))
