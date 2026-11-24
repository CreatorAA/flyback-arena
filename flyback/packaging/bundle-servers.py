"""Assemble separate platform roots with unchanged native binaries and executable Unix mode."""
from pathlib import Path, PurePosixPath
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
from tarfile import open as taropen
from os import replace
import stat,datetime,sys
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
dest=root/'out/交付/飞返竞技场-服务端.zip';candidate=dest.with_suffix('.zip.part')
windows=root/'out/Flyback-Server-Windows-x64.zip';linux=root/'out/flyback-linux/flyback-arena-ubuntu24.04-x86_64.tar.gz'
fork_maps = list((root / 'flyback/native/game/maps').glob('*.txt'))
map_names = {p.name for p in fork_maps} | {n+'.txt' for n in 'twinlakes yinyang icefloes ruins skybridge stonecircle'.split()}
now=datetime.datetime.now().timetuple()[:6]
def add(z,path,data,mode=0o644):
    entry=ZipInfo(path,now);entry.create_system=3;entry.external_attr=((stat.S_IFREG|mode)<<16);z.writestr(entry,data,compress_type=ZIP_DEFLATED,compresslevel=6)
def extension(p):return PurePosixPath(p).suffix
candidate.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(candidate,'w') as out:
 with ZipFile(windows) as win:
    inventory=[]
    for x in win.infolist():
        if x.is_dir():continue
        prefix='Flyback-Server-Windows-x64/';assert x.filename.startswith(prefix);rel=x.filename[len(prefix):]
        assert not PurePosixPath(rel).is_absolute() and '..' not in PurePosixPath(rel).parts
        if rel=='distribution-files.txt':continue
        assert extension(rel).lower() not in {'.wav','.ogg','.mp3','.pck'}
        assert PurePosixPath(rel).name.lower() not in {'combat-audio.txt','flyback.exe','flyback-bridge.exe'},rel
        data=win.read(x)
        add(out,'Windows-x64/'+rel,data);inventory.append(rel)
    add(out,'Windows-x64/distribution-files.txt',('\n'.join(sorted(inventory))+'\n').encode())
 with taropen(linux,'r:gz') as tar:
    for x in tar:
        if x.isdir():continue
        p=PurePosixPath(x.name);assert x.isfile() and not p.is_absolute() and '..' not in p.parts
        rel=p.as_posix();data=tar.extractfile(x).read()
        assert extension(rel).lower() not in {'.wav','.ogg','.mp3','.pck'} and p.name != 'combat-audio.txt'
        if rel=='README-Linux.md':
            s=data.decode('utf-8').replace('\r\n','\n').replace('在下载目录运行以下命令。','在解压后的 `Ubuntu-24.04-x86_64` 目录运行以下命令。')
            s=s[:s.index('mkdir -p flyback-release')]+s[s.index('sudo apt install'):]
            data=s.encode()
        add(out,'Ubuntu-24.04-x86_64/'+rel,data,x.mode & 0o777)
 add(out,'开始使用.txt',('Windows x64：打开 Windows-x64，按其中的「开始使用.txt」启动专服。\r\n'
     'Ubuntu 24.04 x86-64：在 Linux 上用 unzip 解压本 ZIP，进入 Ubuntu-24.04-x86_64，直接从 README-Linux.md 的 sudo apt install 开始；无需再创建 tar 解压目录或解开 tar 包。\r\n'
     '本衍生版的游戏开发、联机服务和发行由 Codex 和 ChatGPT 完成；专服 Web 管理台前端由 Gemini 贡献。\r\n'
     '升级已有专服：先停服，保留原 server.toml 及其 storage.data_dir 指向的目录，尤其是其中的 identity/server.crt 和 identity/server.key（Ubuntu 按随包指南安装为服务时，身份目录是 /var/lib/flyback/identity）。把新程序和随包地图、AI 装入原位置后再启动，无需更换客户端信任记录。\r\n').encode('utf-8-sig'))
with ZipFile(candidate) as bundle:
 names=set(bundle.namelist());assert {n.split('/')[0] for n in names}=={'Windows-x64','Ubuntu-24.04-x86_64','开始使用.txt'}
 for system in ('Windows-x64','Ubuntu-24.04-x86_64'):
    assert {n.rsplit('/',1)[1] for n in names if n.startswith(system+'/data/maps/')}==map_names
    for src in fork_maps:
        assert bundle.read(system+'/data/maps/'+src.name).decode().splitlines()==src.read_text(encoding='utf-8').splitlines()
    assert {system+'/'+n for n in ['server.toml','admin-web/index.html','admin-web/app.js']}<=names
 for rel in ('arena-server','flyback-bridge','run-delegated','data/bots/arena-bot-guard','data/bots/claude-fable-5.2'):
    assert (bundle.getinfo('Ubuntu-24.04-x86_64/'+rel).external_attr>>16)&0o111
 assert 'tar -xzf' not in bundle.read('Ubuntu-24.04-x86_64/README-Linux.md').decode()
replace(candidate,dest)
print(dest,dest.stat().st_size)