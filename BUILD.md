# 从源码启动飞返竞技场

从仓库根目录运行命令。原生构建生成两个程序：`arena-server` 主持多房间专服，`flyback-bridge` 把 Godot 客户端接到裁判。默认还编译全部 13 名 AI，并准备 12 张地图。已提交的游戏美术可直接运行，无需安装 Blender 或检出原仓库。

## Windows x64：编译后开一局

使用 MSYS2 UCRT64 工具链。在 **UCRT64 终端**安装依赖：

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,openssl,zstd}
```

随后在 **PowerShell 7** 中将 MSYS2 的 `ucrt64\bin` 加入当前会话的 PATH（按你的安装位置调整），再构建：

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S flyback -B out/flyback-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build out/flyback-win --parallel 4
cmake --install out/flyback-win --prefix out/run-win
ctest --test-dir out/flyback-win --output-on-failure
```

`out/run-win` 是可运行的布局：程序、`msquic.dll` 与 `data/`、`admin-web/`、`server.toml` 同级。开发时保留工具链 PATH 以找到 OpenSSL、Zstd 和 MinGW 运行库。

安装 **Godot 4.6.1**，把其可执行文件加入 PATH 或用绝对路径替换下面的 `godot`：

```powershell
$env:FLYBACK_HOME = (Resolve-Path out/run-win).Path
godot --headless --path flyback/client --import
godot --path flyback/client
```

选择「快速挑战」，保留一个真人席位，进入准备厅后点「开始比赛」。要测试独立专服：

```powershell
Push-Location out/run-win
.\arena-server.exe --config .\server.toml check-config
.\arena-server.exe --config .\server.toml admin init
.\arena-server.exe --config .\server.toml
Pop-Location
```

首次部署才需要 `admin init`。启动后的游戏端点是 **UDP**，管理页是独立 **TCP** 端点。配置可用 `storage.data_dir` 指向安装树之外的持久目录；升级时保留其中的 `identity/` 和自己的配置。

## Ubuntu 24.04 x86-64：编译无头专服

```sh
sudo apt install build-essential cmake ninja-build curl libssl-dev libzstd-dev libargon2-dev
sh flyback/native/net/prepare-linux.sh "$PWD/out/linux-deps"
sudo apt install "$PWD/out/linux-deps/libmsquic.deb"
cmake -S flyback -B out/flyback-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLYBACK_MSQUIC_SDK="$PWD/out/linux-deps/sdk" \
  -DFLYBACK_MDNS_INCLUDE="$PWD/out/linux-deps/sdk/include"
cmake --build out/flyback-linux --parallel 4
ctest --test-dir out/flyback-linux --output-on-failure
cmake --install out/flyback-linux --prefix out/run-linux
```

专服不依赖 Godot。部署时按 [Linux 安装与升级指南](flyback/native/game/README-Linux.md) 设置服务账号与 systemd，将游戏批准使用的 cgroup 委托给服务。要让 AI 出场，请通过提供的 `run-delegated` 启动；不需要以 root 身份运行选手。

## 验证与制作发行包

- `ctest` 检查裁判、真实 QUIC 连接以及丢包恢复。Windows 还可以运行 `out/flyback-win/bin/flyback_game_test.exe out/flyback-win/data --new-map-bots`，让已编译的原选手实打六种新增地图/人数搭配。
- [Astra 对 Fable 的原始动作记录](docs/boomerang-fork/evidence/runtime/twinlakes-seed7.txt)可在客户端的回放界面打开。完整 117 局兼容性回归使用[上游同一修订的回放语料](https://github.com/chen-006/meow-ai-arena/tree/9a8a5cda412badb3ec8ba3cf86f29dfe7f5e3d47/boomerang/replays)；将那个目录作为 `flyback_game_test` 的第二个位置参数传入。
- Windows：安装与 Godot 4.6.1 匹配的导出模板后，运行下面的命令。服务器包不需要执行 Godot 导出。

```powershell
$export = (New-Item -ItemType Directory -Force flyback/client/build/windows).FullName
godot --headless --path flyback/client --export-release "Windows Desktop" "$export/Flyback.exe"
./flyback/packaging/package.ps1 -Kind client -NativeBin (Resolve-Path out/run-win).Path
./flyback/packaging/package.ps1 -Kind server -NativeBin (Resolve-Path out/run-win).Path
```

- Ubuntu 24.04：运行 `sh flyback/native/game/package-linux.sh out/flyback-linux out/linux-deps/libmsquic.deb out/flyback-linux/flyback-arena-ubuntu24.04-x86_64.tar.gz`。此脚本除运行库外还附上 AI 的可重链接源码和对应许可证。
- 将 Windows 和 Ubuntu 的无头产物按上述命令放在同一工作树的 `out/` 后，运行 `python flyback/packaging/bundle-servers.py`；双平台 ZIP 位于 `out/交付/`。

首次 CMake 配置会下载固定版本的头文件与 MsQuic。受限网络可通过 `FLYBACK_HEADERS`、`FLYBACK_MSQUIC_SDK`、`FLYBACK_MDNS_INCLUDE` 指向已有缓存；具体依赖版本和布局见 [CMake 配置](flyback/CMakeLists.txt) 和[传输模块](flyback/native/net/CMakeLists.txt)。
