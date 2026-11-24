# 飞返专服 · Ubuntu 24.04 x86-64

这个包可以直接开服。专服和 13 名原赛 AI 均已编译；客户端、显示器、Python 和 C++ 工具链都不是运行依赖。

## 安装并启动

在下载目录运行以下命令。首次安装会创建独立的服务账号，并把程序、配置、存档放在三个不同的目录。升级已有服务器时保留 `/etc/flyback` 和 `/var/lib/flyback`，不要再覆盖现有配置。

```sh
mkdir -p flyback-release
tar -xzf flyback-arena-ubuntu24.04-x86_64.tar.gz -C flyback-release
cd flyback-release
sudo apt install ./runtime/libmsquic_2.6.4_amd64.deb libargon2-1 libzstd1
getent passwd flyback >/dev/null || sudo useradd --system --user-group --home-dir /var/lib/flyback --shell /usr/sbin/nologin flyback
sudo install -d -m 755 /opt/flyback
sudo cp -a arena-server flyback-bridge run-delegated admin-web data examples /opt/flyback/
sudo chown -R root:root /opt/flyback
sudo install -d -m 750 -o flyback -g flyback /etc/flyback /var/lib/flyback
sed 's#data_dir = "./data"#data_dir = "/var/lib/flyback"#' server.toml | sudo -u flyback tee /etc/flyback/server.toml >/dev/null
sudo -u flyback /opt/flyback/arena-server --config /etc/flyback/server.toml check-config
sudo -u flyback /opt/flyback/arena-server --config /etc/flyback/server.toml admin init --user admin
sudo install -m 644 examples/flyback.service /etc/systemd/system/flyback.service
sudo systemctl daemon-reload
sudo systemctl enable --now flyback
sudo journalctl -u flyback -n 30 --no-pager
```

在启动日志的 `server_started` 中查看游戏 UDP 端口、管理端口和游戏证书指纹。示例地址为 **UDP 27860** 和 **HTTP 127.0.0.1:27861**。玩家在联机大厅添加服务器的游戏地址；服主可直接使用主机的可达地址或映射该 UDP 端口。管理页面与游戏用的是不同端口：例如在管理员电脑上运行 `ssh -L 27861:127.0.0.1:27861 用户@服务器`，再打开 <http://127.0.0.1:27861>。也可按实际部署配置 HTTPS 或反向代理及 `admin.web.public_origin`。

## 开服后

- 编辑 `/etc/flyback/server.toml` 调整可创建的房间数、同时对战数、各房观战上限、回放保留期和磁盘上限。WebUI 保存配置时需要在配置目录创建临时文件和备份，因此该专属目录由服务账号持有。地图和已经审核的 Bot 位于只读的 `/opt/flyback/data`。
- 备份配置及 `/var/lib/flyback`，特别是其中的 `identity/`。保留身份目录，玩家下次连接时仍可认出同一服务器。用 `sudo systemctl stop flyback` 正常停止；替换程序后 `sudo systemctl start flyback`。
- 含 Bot 的对局需要 cgroup v2、Landlock 和 seccomp；随包的 systemd 单元和 `run-delegated` 在启动时仅设置本服务自己的 cgroup。若受限容器没有委托 CPU、memory、pids 控制器，用支持委托的 VM 或宿主运行；不要试图放开整棵 cgroup 的权限。无须运行 root 游戏进程。

**给开发者**：发行树 `bot-source/` 提供每个正式选手的 C++ 源码和本版 `dart.hpp`。例如 `g++ -std=c++17 -O2 -static -Ibot-source bot-source/gpt-6.2-astra/player.cpp -o gpt-6.2-astra` 即可重新链接这个选手。构建环境的 Ubuntu 库版本及许可证见 `licenses/`；本版 Bot 使用 [Ubuntu glibc 2.39-0ubuntu8.12 源包](https://launchpad.net/ubuntu/+source/glibc/2.39-0ubuntu8.12)。完整仓库的 `flyback/native/game/package-linux.sh` 会从 CMake 安装树制作同样布局的发行包。
