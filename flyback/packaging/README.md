# 生成可分发的客户端和服务端

默认产物位于 `out/交付/`。玩家只需客户端；无头服务器不需要 Godot、Blender 或显卡。

### Windows

先按[根构建说明](../../BUILD.md)把 Windows Release 原生组件和所有选手构建到 `out/flyback-win`。使用 PowerShell 7、MinGW-w64 的 `g++.exe` 与 `objdump.exe`；后者用于收集实际依赖的 DLL。

```powershell
$g = (Get-Command godot).Source
./flyback/client/tools/export.ps1 -Godot $g -Format windows -Destination ./flyback/client/build/windows
./flyback/packaging/package.ps1 -Kind client
./flyback/packaging/package.ps1 -Kind server
```

`client` 写入可直接解压游玩的游戏 ZIP；`server` 写入 `out/Flyback-Server-Windows-x64.zip`。若先用 CMake 安装到单独的前缀，可用 `-NativeBin` 指向含二进制和 `data` 的安装目录。脚本从白名单复制程序、数据和许可，不从正在运行的专服复制账号、证书、私钥或存档。

### Ubuntu 24.04 x86-64

在能构建并安装项目的 Ubuntu 上，使用与构建对应的官方 MsQuic 2.6.4 amd64 软件包。举例：

```sh
mkdir -p out/flyback-linux
sh flyback/native/game/package-linux.sh out/flyback-linux /path/to/libmsquic_2.6.4_amd64.deb out/flyback-linux/flyback-arena-ubuntu24.04-x86_64.tar.gz
```

将该 tar.gz 放回 Windows ZIP 所在工作树的相同相对路径，然后合并：

```sh
python flyback/packaging/bundle-servers.py
```

结果为 `out/交付/飞返竞技场-服务端.zip`。其中 Windows 和 Ubuntu 分目录存放；Linux 进程与启动脚本保留可执行权限。打包后请从解压的产物实际开服，核对本机指纹再用客户端连接。
