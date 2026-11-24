"""Real QUIC clients. Deliberately small budget to test admission and recovery, not throughput."""
import http.cookiejar,json,pathlib,subprocess,tempfile,time,urllib.request
from live import Proc,ROOT,EXE,settings
def verify(tmp):
    config=tmp/'server.toml'
    config.write_text('[server]\nlisten="127.0.0.1:0"\negress_budget_kbps=1\n[admin.web]\nenabled=true\nlisten="127.0.0.1:0"\n')
    args=['--config',str(config)]
    pw='bandwidth-test-only'
    init=subprocess.run([str(ROOT/('arena-server'+EXE)),*args,'admin','init'],input=f'{pw}\n{pw}\n',text=True,cwd=tmp,capture_output=True)
    assert init.returncode==0,init.stderr
    ps=[]
    def make(exe,argv,tag):
        x=Proc(exe,argv,tmp,tmp/tag);ps.append(x);return x
    try:
        server=make('arena-server',args,'server')
        boot=server.wait('server_started')
        def connect(tag):
            c=make('flyback-bridge',['client'],tag);c.wait('catalog')
            c.send('connect',name=tag,address=f"127.0.0.1:{boot['udp_port']}");c.wait('trust');c.send('trust',accept=True);c.wait('directory');return c
        a,b,k=map(connect,['driver-A','driver-B','spectator'])
        a.send('create',settings=settings(2,['human','human'],maps=['twinlakes']));room=a.wait('room');rid=room['id']
        b.send('join',room_id=rid,role='player');b.wait('room')
        a.send('ready',ready=True);b.send('ready',ready=True)
        ready=a.wait('room',lambda r:all(s['ready'] for s in r['slots']))
        a.send('start',room_rev=ready['room_rev']);a.wait('manifest');a.wait('snapshot',lambda s:s['tick']>=10)
        base=f"http://127.0.0.1:{boot['admin_port']}"
        session=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        def get(path,data=None,token=None):
            headers={'Content-Type':'application/json'}
            if token:headers['X-CSRF-Token']=token
            with session.open(urllib.request.Request(base+path,data=None if data is None else json.dumps(data).encode(),headers=headers),timeout=4) as r:return json.load(r)
        token=get('/api/v1/auth/login',{'username':'admin','password':pw})['csrf_token']
        def wait_for_pressure(value):
            until=time.monotonic()+12
            while time.monotonic()<until:
                st=get('/api/v1/state')
                if st['server']['bandwidth_pressure']==value and st['rooms']:return st
                time.sleep(.15)
            raise AssertionError(f"expected pressure {value}, got {st['server']}")
        st=wait_for_pressure(2);r=next(i for i in st['rooms'] if i['room_id']==rid)
        assert r['effective_spectator_hz']==5 and r['spectator_join_block_reason']=='egress_budget'
        k.send('join',room_id=rid,role='spectator');k.wait('error',lambda e:e['code']=='egress_budget')
        a.send('input',seq=77,mx=0,my=0,ax=1000,ay=0,act=0)
        a.wait('snapshot',lambda f:f.get('last_applied_input_seq')==77)
        assert 15.1 < r['tick_tps'] < 17 and st['capacity']['player_snapshot_hz']==20  # 0.8x humans; budget does not reduce their updates
        get('/api/v1/commands',{'action':'set_capacity','target':'capacity','expected_revision':st['config_revision'],'payload':{'egress_budget_kbps':0}},token)
        st=wait_for_pressure(0);r=next(i for i in st['rooms'] if i['room_id']==rid);assert r['effective_spectator_hz']==10
        k.send('join',room_id=rid,role='spectator');k.wait('room',lambda v:v['self_seat']==-1)
        server.write('stop');server.p.wait(8)
        print('PASS real 1 kbit/s budget: spectators 10→5 Hz, reject new spectator, player ACK/ticks continue, unlimited restores 10 Hz + admission')
    finally:
        for p in reversed(ps):p.close()
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='flyback-egress-') as d:verify(pathlib.Path(d))
