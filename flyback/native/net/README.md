# 飞返联机传输

个人房和多房专服都只监听一个游戏 UDP 端口。客户端先核对房主身份，再读目录、加入房间；攻击和快照不会排在回放下载后面。服务端、客户端桥共用 `flyback_net`，原比赛的 Bot 协议与回放文件不经过这个模块。

## 接到房间循环

`net.hpp` 的 `Endpoint` 由主循环线程持有；MsQuic 工作线程负责收发。监听填写实际网卡或 `"*"`，端口传 0 表示让系统分配。绑定成功后显示 `port()`，不要把配置里的 0 当作邀请端口。房主出示的指纹是证书公钥（DER SubjectPublicKeyInfo）的 SHA‑256，换证但保留密钥不改变此指纹。

```cpp
flyback::net::Endpoint host;
host.listen("*", 0, data_directory / "identity");
host.advertise("周末服");  // 可选：同链路 DNS-SD，UDP 5353 不是游戏端口
```

`connect(host, port, saved_pin)` 随即返回本地 peer ID；直到 `poll()` 读到 `connected` 才能发房密或命令。`trust_required` 含 `host/fingerprint`：显示给玩家，与服主核对后 `trust(peer,true)`；收藏按端点保存已确认的指纹。已保存的指纹不符会收到 `disconnected: certificate_pin_changed`，不会自动降级成首次信任。公信证书另验链、域名或 IP SAN；过期证书拒绝。私钥 `server.key` 首次生成时使用当前用户/SYSTEM ACL（Windows）或 0600（Linux）；自带 PEM 证书时 `server.crt` 可以是叶证书后接中间证书的全链。已有身份文件不被替换。

`send(peer,json,true)` 用有序控制流；输入和快照按 `false` 走不可靠数据报。消息有界，自包含玩家帧按步立即提交；`false` 返回值要求调用者识别断连、背压或超出当前 MTU，不偷偷改走可靠流。结算、重要淘汰通知走可靠流；本地表现不能把一发即逝的数据报作必达通知。每次循环排完 `poll()` 后再退出；断开由同一循环回收。

观众按 `room_id,round_epoch,timeline_epoch,pov` 分组：同一观察/同一批事件才复用编码，不复用密文。每 20 个模拟步为一个基线窗口；大致每现实秒 1 次（1×）。服务器经可靠流发完整观察基线并等待应用 ACK，等待时仍发自包含快照。随后仅在差分更小时使用相对于该**已确认基线**的差分；丢一个差分不会损坏后续差分。换视角或恢复比赛另取基线，不继承旧视角的隐藏坐标。没有 `last_applied_input_seq` 且带四个视角键的服务端快照才走这一优化。房间层先合并观测时间之间的事件，网络层不改变谁能看见什么。

## 依赖与发行

- **Windows x64**：CMake 获取 [微软的 MsQuic 2.6.4 OpenSSL 原生 NuGet](https://www.nuget.org/packages/Microsoft.Native.Quic.MsQuic.OpenSSL/2.6.4)。已有离线包时解压，传 `-DFLYBACK_MSQUIC_SDK=<展开目录>/build/native`；`msquic.dll` 与可执行文件放同目录，连同 NuGet 的 LICENSE / THIRD-PARTY-NOTICES 发行。普通玩家不需要安装 NuGet 或 OpenSSL 命令行工具。
- **Linux x86-64**：在 Ubuntu 24.04/22.04 或 Debian 12 构建机执行 `sh native/net/prepare-linux.sh /tmp/fbar-quic`，再按输出安装对应发行版 `.deb`。例如 [Ubuntu 24.04 微软官方包](https://packages.microsoft.com/ubuntu/24.04/prod/pool/main/libm/libmsquic/libmsquic_2.6.4_amd64.deb) 依赖 `libssl3t64` 和 `libnuma1`，提供 `libmsquic.so.2`。只传这套运行库而非编译工具给服主；不要把不同 glibc 发行版的共享库混装。
- 两端编译还需 OpenSSL 3 Crypto、Zstandard、nlohmann/json；发现使用固定版本的 [mjansson/mdns](https://github.com/mjansson/mdns/tree/a569c4759bd47e0f2a7bfc4d4c19620445782806)。`-DFLYBACK_MDNS_INCLUDE=<目录>` 可指定已有头文件。安装包应随发行附用到的 Crypto、Zstd、编译器运行库及许可证；平台系统库走系统。
- 技术依据：[MsQuic 凭证格式](https://github.com/microsoft/msquic/blob/v2.6.4/docs/api/QUIC_CREDENTIAL_CONFIG.md)、[不可靠 QUIC DATAGRAM](https://www.rfc-editor.org/rfc/rfc9221.html#section-5.2)、[DNS-SD](https://www.rfc-editor.org/rfc/rfc6763.html)。跨公网映射游戏端口须支持 UDP；mDNS 仅用于同链路发现，收藏与地址直连不依赖组播。

## 验收入口

`flyback-net-test` 通过真实 IPv4/IPv6 UDP 监听验证：首次核验与拒绝、记住密钥与阻止变更、加密控制流与数据报、90 kB 大回放独立流、观众基线 ACK、视角切换与固定协议向量。加参数 `--lan` 会在本机可组播网卡上**实际**查询并接收 DNS-SD，应核对返回地址和实际游戏端口。

`flyback-net-traffic boomerang/replays/g23457.txt` 将原四人局按 20 Hz 推给两条真实回环连接，观众读 10 Hz。Windows 测得 62 个全可见玩家帧平均 **178.05 B**、最大 **244 B**；3.1 秒内收到 62 个玩家帧和 31 个观众帧。该次 MsQuic 记录服务端出站 UDP **负载**分别约 **40.83 / 19.13 kbit/s**（扣除建连前计数，含 QUIC/加密开销，不含 IP/UDP 头）。这是单局无人工丢包的本机样本；端点位、证书和握手时间会使短样本计数略有变化。命令把归档的全可见状态用作玩家大小上界样本，不是游戏对真人开放全图。

`flyback-net-test --weak` 把两条本机 UDP 套接字放入路径，模拟单程 40±15 ms、2% 随机丢包，以及从第 35 到第 54 步连续 1 s 的双向中断。Windows 样本中，100 条输入收到 77、100 个快照收到 79；恢复后两边均收到末段新帧。10 条可靠控制全部按序到达、无重复，中断时发出的结果也送达。已送达输入从发送到服务端收到的 P50 为 40.21 ms，P95 为 57.04 ms；不含裁判等下一步的时间。夹具使用线程自己的高精度等待，不改操作系统网络配置或全局计时器。

若要压测真正的多房专服，而非回放编码器，Windows 上用 PowerShell 7 执行 `measure-host.ps1`，传入构建所得 `arena-server.exe`、地图/选手数据目录、`flyback-net-load.exe` 和全新的输出目录。脚本用独立配置、系统分配的回环端口和独立身份，结束时发控制台 `stop`；不会停已有服务器。模式如下：

- `-Mode match`：106 条独立 TLS/QUIC 连接。A 房四名真人协议客户端 + 100 名观众，B 房另两名真人。先只暂停 B 房，再比较两房十秒推进量、各观众收帧数、实际下行及服务端 CPU/工作集。客户端同机、多人输入保持中立，因此它检验联网/房间容量，并非活动 AI 的 CPU 压测。
- `-Mode idle -Seconds 1800`：用两个同时运行的无头服对照。两者均由一条连接预热；其中一服建好 100 间空房，然后客户端退出。每分钟记录两 PID 从采样开始至今的单核 CPU 占比、工作集、私有内存及线程数。诊断只分享 JSON 等数值，勿分发这些临时实例的身份私钥。

跨系统互连已用发行入口双向验证：两个 Windows bridge 钉住 Ubuntu 专服公钥，实际建房、占满两个人类席、开赛并消费双方输入；Ubuntu bridge 也从 Windows 服取得目录并创建了真人房。执行 `interop_clients.py BRIDGE GAME_HOME ADDRESS SPKI` 使用隔离临时配置，不改玩家收藏。固定测试观察、并发实测和适用范围收于 [measurements.json](measurements.json)。

协议号、帧编码和下游互测：见 [FBAR-v1.md](FBAR-v1.md)、[固定向量](vectors.json)。



