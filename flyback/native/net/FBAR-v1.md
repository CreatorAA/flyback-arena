# FBAR v1.0 原版互操作

**原版配置名** `flyback:vanilla`，规则集 `classic20`。传输为 QUIC v1、TLS 1.3、ALPN `fbar-1`，要求 DATAGRAM 扩展，不接收 0‑RTT 命令。客户端发起一条双向控制流；一条大清单/回放也可各用自己的单向可靠流。UDP 地址验证通过 MsQuic Stateless Retry，和房间密码无关。

### 每帧如何识别

| 偏移 | 字节 | 含义 |
|---|---:|---|
| 0 | 4 | ASCII `FBAR` |
| 4, 5 | 1 + 1 | `major, minor`，当前 `1,0` |
| 6 | 1 | `profile=0` 是原版；`1` 供协商扩展，当前客户端不能解扩展局 |
| 7 | 1 | 消息类型；原版 Hello 固定前缀为 `46 42 41 52 01 00 00 01` |
| 8 | 1 | 编码标志：0 为紧凑值；1 为原始大小（ULEB128）加独立 Zstd 帧 |
| 9 | 变长 | 负载 |

可靠流每帧前另有 **4 字节大端长度**（随后的完整 FBAR 帧长），可跨多次 receive 或一次 receive 含多帧。数据报恰好一帧，不拆 IP 分片，也不等待下一个 tick 凑包。常规帧负载上限 4 MiB；服务端接收客户端控制流单帧最多 64 KiB。Zstd 只对服务端公开清单、可靠基线或回放块尝试：原始体 ≥512 B，净收益至少 16 B，独立字典；认证、房密、输入与普通快照不走通用压缩。

客户端先发送 `_hello` (01)，服务端答 `_hello_ack` (02)，双方检查基础标识、主版本、profile、DATAGRAM 和必需扩展数组。原版只协商 minor 0，能接受同主版支持更多可选能力的 Hello，再用 1.0 应答。未支持的必需扩展不能入局；扩展实现应保留原版服务器目录握手，按房间公布扩展清单。指纹认证说明对端身份；FBAR 声明协议兼容，不能用它证明发布者是谁。当前不会下载 Mod。

| 类型（十六进制） | 消息 |
|---|---|
| 01/02/03/05 | `_hello` / `_hello_ack` / `identify` / `error` |
| 20–26 | `server,list,directory,create,join,room,seat` |
| 27–2F | `settings,ready,watch,leave,left,catalog,again,start,lobby` |
| 30 | `field_ready`（可选，可靠）：参赛客户端已绘出该局第 0 步 |
| 40–43 | `input,snapshot,delta,event` |
| 60/61/62 | `manifest` / `vote` 或 `pause` / `speed` |
| 63–67 | `_base_need` / `_spec_base` 或 `baseline` / `result` / `replay_clip` / `_base_ack` |

同号多语义时由负载的 `op` 或 `type` 区分；包号与内容必须匹配。私有前缀 `_` 的握手/基线操作由传输端处理。应用协议拒绝码对应 QUIC application close：`0x101` 主版不符、`0x102` 缺必需扩展、`0x103` profile 不符、`0x100` 其余畸形/越界帧。房密、房容量和权限错误由合法可靠房间消息返回，不应关整台服务器。

### 开赛时钟

规则仍为每游戏秒 20 步。房间的 `speed` 表示每现实秒推进的游戏秒数：有人参赛时默认 0.8（16 步/秒），纯 AI 局默认 1。联机局不能手动改速；本地局可以选 0.5、0.8、1、2 倍。回放维持原时间轴，不重播赛前倒数。

主线程发出 `room.state=loading`、清单和按席位过滤的可靠开场快照（`tick=0`）。新客户端可在 `create` 或 `join` 附 `field_ready:true`；当 `room.field_ready_required=true`，渲染完自己的开场帧后回复 `{op:"field_ready",room_id,round_epoch}`。仅该局仍在场的参赛席参与此屏障，最多等 10 个现实秒。未登记的客户端不必应答。观众的视野仍遵循原有延迟。

随后权威房间以可靠 `room` 消息逐秒发布 `state=countdown` 与 `countdown_seconds=3,2,1`，期间不执行任何游戏步。开赛消息 `state=playing,countdown_seconds=0` 切换到新的 `timeline_epoch`；只接收新时间线、本席在开赛后发出的输入。倒数不归档成游戏帧，不受倍速影响。

### 紧凑值与快照

[wire.cpp](wire.cpp) 是配套编码器/解码器（字典索引只能追加，不可重排）；整数的 ULEB128 均有限长、禁止溢出。类型标签字节 0–11 分别表示 null、false、true、非负整数、负整数（载荷是 `-1-n`）、64 位大端浮点、UTF‑8 长度+字节、数组、对象、同列对象行组、二进制、原版战斗快照。对象键为非零字典索引，或 0 后跟 UTF‑8 字符串；重复键拒绝。行组统一列头后按行给值。当全量快照严格符合原版字段组时使用战斗快照专码：布尔组合成位、数值按固定字段序列 ZigZag+ULEB128；不可见坐标根本不上线。额外合法字段改走可逆通用对象编码，不静默丢值。

本版比赛清单标明 `human_observation_delay_ticks=0`、`ai_observation_delay_ticks=4`。参赛真人席的快照给出当前步的可见状态；对手程序输入及观战选择 AI 本人视野时，敌情按原规则回看四步。草丛过滤和独立的观战保护仍生效。旧专服可能未提供这两个字段；不能仅凭客户端版本判断房主发的是哪种快照。

观战基线包 `{type:"_spec_base",base,sample}` 只含已向该视角释放的信息。接收方安装后用可靠 `_base_ack(base)` 应答。数据报 `delta(base,patch)` 始终相对于这一完整基线，不引用上一条数据报。patch 节点 0=完整替换；1=相同形状结构的字段变更掩码（按基线数组顺序或对象规范键序，最低位对应第一项），后跟置位子节点；2=有符号整数差值 ZigZag+ULEB128。字段/可见性形状不同必须整体替换该节点。丢包或切视角不允许从旧坐标推断新坐标；请求缺失基线用 `_base_need`。胜负与关键淘汰另走按接收视角释放的可靠流。

[vectors.json](vectors.json) 给出 Hello、输入、含隐藏对手的真实结构快照与基线差分的 JSON、完整十六进制或 patch 对照。向量不含私钥、房密或 QUIC 加密随机数；`flyback-net-test` 逐字节核对编码、还原字段，并验证拒绝未知 profile。
