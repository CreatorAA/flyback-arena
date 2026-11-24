"""Two real browser sessions and a private real server/32 QUIC spectators (no mocked API).
Requires Python 3.12+, playwright and installed Edge/Chromium:
  python admin_browser.py APP_BIN --browser /path/to/chrome --screenshots OUTPUT
Use --server to test a separately built arena-server, --web-dir to inspect a candidate UI.
"""
import argparse
import contextlib
import json
import os
import queue
import secrets
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

from playwright.sync_api import sync_playwright

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "native/net"))
from interop_clients import Bridge

def check(args, tmp):
    app = args.bin.resolve()
    suffix = ".exe" if os.name == "nt" else ""
    server = args.server.resolve() if args.server else app/("arena-server"+suffix)
    web_home = args.web_dir.resolve().parent if args.web_dir else app
    assert (web_home/"admin-web/index.html").is_file()
    conf = tmp/"server.toml"
    conf.write_text('[server]\nname="Private browser QA"\nlisten="127.0.0.1:0"\nmax_spectators_per_room=32\n'
                    '[admin.web]\nenabled=true\nlisten="127.0.0.1:0"\n[storage]\ndata_dir="./private"\n',
                    encoding="utf-8")
    env={**os.environ,"FLYBACK_HOME":str(web_home),"FLYBACK_PROFILE":str(tmp/"host-profile")}
    flags=getattr(subprocess,"CREATE_NO_WINDOW",0)
    cmd=[str(server),"--config",str(conf),"--assets",str(app/"data")]
    def set_user(name, password, role="admin", action="init"):
        r=subprocess.run(cmd+["admin",action,"--user",name,"--role",role],input=(password+"\n"+password+"\n").encode(),
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=tmp,env=env,creationflags=flags,timeout=20)
        if r.returncode:raise AssertionError(r.stderr.decode("utf-8",errors="replace"))
    def waitline(pipe):
        q=queue.Queue()
        threading.Thread(target=lambda:q.put(pipe.readline()),daemon=True).start()
        return q.get(timeout=20)
    s=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,cwd=tmp,creationflags=flags)
    errors=[]; clients=[]; report={}
    try:
        launched=json.loads(waitline(s.stdout))
        assert launched["type"]=="server_started"
        base=f"http://127.0.0.1:{launched['admin_port']}"
        endpoint=f"127.0.0.1:{launched['udp_port']}"
        def client():
            i=len(clients); profile=tmp/f"c{i}"; profile.mkdir()
            c=Bridge(app/("flyback-bridge"+suffix),app,profile);clients.append(c);c.get("catalog")
            c.cmd("connect",address=endpoint,name=f"QA-{i}",fingerprint=launched["fingerprint"])
            c.get("connected");c.get("directory");return c
        a_pass,b_pass=secrets.token_urlsafe(24),secrets.token_urlsafe(24)
        with sync_playwright() as pw:
            opts={"headless":True}
            if args.browser:opts["executable_path"]=str(args.browser)
            with pw.chromium.launch(**opts) as browser:
                ac=browser.new_context(viewport={"width":1440,"height":900})
                bc=browser.new_context(viewport={"width":1280,"height":800})
                a,b=ac.new_page(),bc.new_page()
                for page in [a,b]:
                    page.set_default_timeout(10000)
                    page.on("pageerror",lambda error: errors.append(str(error)))
                def login(p,username,pwd):
                    p.locator("#login-username").fill(username);p.locator("#login-password").fill(pwd)
                    p.locator("#btn-login-submit").click();p.locator("#console-view").wait_for()
                def state(p):
                    res=p.request.get(base+"/api/v1/state");assert res.status==200
                    return res.json()
                def session(p):return p.request.get(base+"/api/v1/session").json()
                a.goto(base);a.locator("#bootstrap-box").wait_for()
                set_user("alpha",a_pass);set_user("beta",b_pass)
                a.locator("#btn-recheck-session").click();login(a,"alpha",a_pass)
                b.goto(base);login(b,"beta",b_pass)
                report["hot_bootstrap"]=True
                def until(fn):
                    last=None;end=time.monotonic()+10
                    while time.monotonic()<end:
                        last=fn()
                        if last:return last
                        time.sleep(.12)
                    raise AssertionError("real state did not converge")
                if not args.auth_only:
                    host=client();host.cmd("create",settings={"title":"观战容量实测","seat_count":2,
                        "slots":["human","human"],"maps":["twinlakes"],"map_policy":"fixed"})
                    rid=host.get("room")["id"]
                    for i in range(32):
                        c=client();c.cmd("join",room_id=rid,role="spectator");r=c.get("room")
                        assert r["id"]==rid and r["self_seat"]==-1
                    def room(p):
                        return next(r for r in state(p)["rooms"] if r["room_id"]==rid)
                    until(lambda:room(a)["spectator_count"]==32)
                    for p in [a,b]:p.locator('[data-tab="capacity"]').click()
                    a.locator("#cap-max-specs").fill("24")
                    b.locator("#cap-max-specs").fill("16")
                    b.locator("#btn-cap-submit").click()
                    until(lambda:room(b)["effective_spectator_limit"]==16)
                    a.locator("#cap-rev-badge").filter(has_text="草稿基于").wait_for()
                    assert a.locator("#cap-max-specs").input_value()=="24"
                    with a.expect_response(lambda r:r.url.endswith("/api/v1/commands")) as result:
                        a.locator("#btn-cap-submit").click()
                    assert result.value.status==409
                    a.locator("#conflict-modal").wait_for()
                    assert "24" in a.locator("#conflict-diff-tbody").inner_text()
                    if args.screenshots:a.screenshot(path=args.screenshots/"conflict.png")
                    assert room(b)["spectator_count"]==32 and room(b)["effective_spectator_limit"]==16
                    a.locator("#btn-conflict-discard").click();assert a.locator("#cap-max-specs").input_value()=="16"
                    c=client();c.cmd("join",room_id=rid,role="spectator")
                    until_time=time.monotonic()+12
                    while time.monotonic()<until_time:
                        ev=c.events.get(timeout=12)
                        if ev.get("type")=="error":
                            assert ev["code"]=="spectator_full";break
                    else:raise AssertionError("new spectator not refused")
                    report["capacity"]={"initial":32,"lowered_to":16,"still_connected":room(b)["spectator_count"],
                                        "new_spectator_refused":True,"separate_admins":2,"conflict_http":409,"retained_draft":24}
                    b.set_viewport_size({"width":390,"height":844});b.locator('[data-tab="rooms"]').click()
                    card=b.locator("#rooms-list .room-card").filter(has_text="观战容量实测")
                    card.click();detail=b.locator("#room-detail.mobile-open");detail.wait_for()
                    box=detail.bounding_box();assert box and 0<=box["x"] and box["x"]+box["width"]<=391
                    bw=b.evaluate("({body:document.documentElement.scrollWidth, viewport:innerWidth})")
                    if args.screenshots:b.screenshot(path=args.screenshots/"narrow-room.png")
                    b.locator("#btn-mobile-detail-close").click();detail.wait_for(state="hidden")
                    report["mobile"]={"viewport":390,"document_width":bw["body"],"detail_x":box["x"],
                                      "detail_width":box["width"],"detail_open_close":True}
                # Extra stream solely to observe server-side termination, without relying on SPA reconnection timing.
                a.evaluate("""() => {window.qaSse={opened:false,ended:false}; const es=new EventSource('/api/v1/events');
                           es.onopen=()=>{window.qaSse.opened=true}; es.onerror=()=>{window.qaSse.ended=true;es.close()};}""")
                until(lambda:a.evaluate("() => window.qaSse.opened"))
                replacement=secrets.token_urlsafe(24);set_user("alpha",replacement,role="viewer",action="reset")
                revoked_at=time.monotonic();until(lambda:a.evaluate("() => window.qaSse.ended"))
                assert time.monotonic()-revoked_at<4.2
                assert not session(a)["authenticated"] and session(b)["authenticated"]
                if args.auth_only:
                    a.locator("#login-form").wait_for(timeout=9000)
                gate_after_revoke=a.locator("#gate-view").is_visible()
                assert a.request.get(base+"/api/v1/state").status==401
                assert a.request.post(base+"/api/v1/auth/login",data={"username":"alpha","password":a_pass}).status==401
                a.reload();login(a,"alpha",replacement)
                assert session(a)["role"]=="viewer"
                a.locator('[data-tab="capacity"]').click();assert a.locator("#btn-cap-submit").is_disabled()
                report["credential_reset"]={"revoked_session":True,"ended_open_sse":True,"other_admin_kept":True,
                                           "old_password_rejected":True,"new_role":"viewer","revoked_page_at_login":gate_after_revoke}
                assert not errors,errors
                ac.close();bc.close()
        if "mobile" in report:assert report["mobile"]["document_width"]==390,report["mobile"]
        return report
    finally:
        # Stop the private server only; no preexisting process or port is touched.
        if s.poll() is None:
            s.stdin.write(b"stop\n");s.stdin.flush()
        for c in reversed(clients):c.close()
        try:s.wait(10)
        except subprocess.TimeoutExpired:s.terminate();s.wait(5)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("bin",type=Path)
    parser.add_argument("--server",type=Path);parser.add_argument("--web-dir",type=Path)
    parser.add_argument("--browser",type=Path);parser.add_argument("--screenshots",type=Path)
    parser.add_argument("--auth-only",action="store_true",help="verify live revocation also returns the open page to the login form; skip admission/capacity tests")
    args=parser.parse_args()
    if args.screenshots:args.screenshots=args.screenshots.resolve();args.screenshots.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="flyback-browser-") as tmp:report=check(args,Path(tmp))
    text=json.dumps(report,ensure_ascii=False,indent=2)
    if args.screenshots:(args.screenshots/"report.json").write_text(text+"\n",encoding="utf-8")
    print(text)
if __name__=="__main__":main()
