# 运行实机检查

使用已按 [BUILD.md](../../BUILD.md) 构建并安装的应用目录：其下应有 `flyback-bridge`、`arena-server`、`data/maps`、`data/bots` 和管理台。下例从仓库根目录运行，Windows 安装前缀为 `out/run-win`（Linux 换成自己的前缀）。

```sh
python flyback/tests/opening_clock.py out/run-win
python flyback/tests/rounds.py out/run-win
python flyback/tests/live.py out/run-win
```

三个检查分别验证：真人节奏与加载就绪后的三秒倒数；原档回放、系列赛换图和归档；真实选手进程与局域网连接、投票、观战及管理端。运行带 AI 的 Linux 检查需要和专服相同的 cgroup 委派与进程隔离条件。测试使用临时目录和账号，不要将正在开服的配置目录传作参数。

Godot 侧的实机测试入口见[客户端说明](../client/README.md)。浏览器管理台检查另需 Playwright 和已安装的 Edge 或 Chromium；用 `python flyback/tests/admin_browser.py --help` 查看参数。
