# 飞返竞技场 · Windows x64 无头服务端

首次开服，在本目录打开 PowerShell：

```powershell
.\arena-server.exe --config .\server.toml check-config
.\arena-server.exe --config .\server.toml admin init
.\arena-server.exe --config .\server.toml
```

按提示输入新管理员密码。控制台会输出游戏 UDP 地址、游戏证书指纹和另一个供浏览器使用的管理地址。请把 **游戏 UDP 地址**提供给玩家；公网端口映射必须支持 UDP。首次连接时，玩家可和服主核对指纹。

升级时先在控制台输入 `stop` 停服，保留原有 `server.toml` 与 `storage.data_dir`（默认 `data`）里已生成的账号、回放和 `identity`。只更新发行文件，不要整目录清空再解压。运行中的服务器会使用旧身份：原客户端可以直接重连。
