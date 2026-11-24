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

请在编译 13 个 Bot 的同一系统打包，期间不要升级工具链或 libc6-dev。打包使用 Ubuntu 24.04 的 Python 3，并用 APT 的 `deb-src` 索引获取与本机 libc6-dev 完全匹配的 glibc 源包。离线时，设 `FLYBACK_GLIBC_SOURCE_DIR` 指向已保存的 Ubuntu 官方 `.dsc` 及其清单中的所有文件；脚本会校验并随包附上这些文件，而不是只保留下载链接。

```sh
mkdir -p out/flyback-linux
sh flyback/native/game/package-linux.sh out/flyback-linux /path/to/libmsquic_2.6.4_amd64.deb out/flyback-linux/flyback-arena-ubuntu24.04-x86_64.tar.gz
```

将该 tar.gz 放回 Windows ZIP 所在工作树的相同相对路径，然后合并：

```sh
python flyback/packaging/bundle-servers.py
```

结果为 `out/交付/飞返竞技场-服务端.zip`。其中 Windows 和 Ubuntu 分目录存放；Linux 进程与启动脚本保留可执行权限。打包后请从解压的产物实际开服，核对本机指纹再用客户端连接。
