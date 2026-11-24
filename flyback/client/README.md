# 飞返竞技场 · 3D 客户端

想亲自上场，选 **快速挑战**；想看 Astra 和 Fable 交手，选 **AI 表演赛**，将两个席位分别指定给这两名选手。进入准备厅后点「开始比赛」。比赛在本机运行，不调用模型 API。

- **赛制和地图**：可以选择二、三、四人混战。选「轮换」可调整勾选顺序，选「随机」会从兼容地图池中无放回抽取。「每图回合」和各图覆盖值决定何时换场，由裁判推进和累计积分。
- **操作**：WASD 移动，鼠标瞄准；左键出镖、右键近战、空格冲刺、R 召回。开局先在真实场地倒数三秒；真人参赛默认 0.8×，纯 AI 默认 1×。手柄用左右摇杆移动和瞄准，RT 出镖、RB 近战、LB 冲刺、LT 召回。F11 全屏，Esc 打开菜单。单机可暂停或切换 0.5× / 0.8× / 1× / 2×。联网由参赛真人公开表决，观众不能改变比赛时间。
- **邀请朋友**：「对战大厅 → 创建局域网房间」。房主在准备厅查看 UDP 端口，复制连接信息；跨网时使用能转发 UDP 的映射。托管专服则用「添加服务器」收藏地址，进入其目录加入或创建房间。首次连接时请和服主核对指纹。
- **观战与回放**：准备中可以选观战席，进行中的房间只接收观众。全景和跟随只改变镜头；菜单内的「席位观察」才会让桥切换为该席有权获得的信息。主菜单「查看回放」支持本机保存的 FBAR 和原赛事的 DART-REPLAY 2 文本。
- **赛场视野**：本版本真人看到裁判当前步允许观察的对手、飞镖和事件；草中隐蔽目标不会随快照传给真人。原版 AI 仍以四步前的可见敌情作决策。含真人的联机比赛另外对观众实行三秒保护；跟随镜头不会消除这段延迟。
- **战斗音效**：飞镖实际离手才有破风，碰镖是清亮的金属声，格挡更短、更闷，击中身体或一脚踢中另有撞击声。声音根据已知接触位置左右分布；同一碰撞只响一次。主菜单或局内菜单的「操作与设置 → 比赛音效」可调音量或静音。

## 从源码运行

先按[构建说明](../../BUILD.md)安装原生桥、地图与选手到同一个前缀。以下示例从仓库根目录运行，使用 4.6.1 稳定版 Godot 命令行和安装前缀 `out/run-win`：

```powershell
$g = (Get-Command godot).Source
$env:FLYBACK_HOME = (Resolve-Path out/run-win).Path
$env:FLYBACK_BRIDGE = Join-Path $env:FLYBACK_HOME 'flyback-bridge.exe'
& $g --headless --path flyback/client --import
& $g --path flyback/client
```

Godot 展示裁判授权给当前席位的快照，自身预测也通过原生桥计算。可用下列命令验证实际加载、开场和本地速度切换，另可验证地图选择器：

```powershell
& $g --headless --path flyback/client --script res://tools/check_opening.gd
& $g --headless --path flyback/client --script res://tools/check_map_picker.gd
```

安装与 Godot 版本对应的官方导出模板后，可生成 Windows 程序及 PCK：

```powershell
./flyback/client/tools/export.ps1 -Godot $g -Format windows -Destination ./flyback/client/build/windows
```

发行版要同时提供原生桥、运行库、裁判数据和选手程序，见[打包说明](../packaging/README.md)。

## 美术与声音

可直接使用本仓库的 3D 模型、地形、头像、声音；运行、导出都不需要重做素材。13 名 AI 的模型基于[上游体素角色](https://github.com/chen-006/meow-ai-arena/tree/9a8a5cda412badb3ec8ba3cf86f29dfe7f5e3d47/characters/boomerang)。角色形象设计参考小叶子XIYZ（B 站），美术及制作为喵了个猫了个咪了个猫；豆包席位的中性图标由本作替代，上游另行保留权利的头像 PNG 不随库分发。[录制拟音及许可](art/combat_audio/SOURCES.md) · [发行组件署名](../packaging/licenses/组件与署名.md)

若要重做地形、角色或远景，另检出原版渲染器并设置其位置；只学习、运行、导出本游戏不需要这个额外检出：

```powershell
git clone https://github.com/chen-006/meow-ai-arena.git ../flyback-upstream
git -C ../flyback-upstream checkout --detach 9a8a5cda412badb3ec8ba3cf86f29dfe7f5e3d47
$env:FLYBACK_UPSTREAM = (Resolve-Path ../flyback-upstream).Path
# 供离线烘焙的 Python 环境需已安装 numpy、scipy、scikit-image、Pillow。
python flyback/client/tools/bake_land.py moonwell
python flyback/client/tools/bake_art.py moonwell
```

这两个命令分别重做月井庭的塌陷阶段地形与地表纹理。地图和出生点预览只需 `python flyback/client/tools/bake_map_atlas.py`，直接使用随库裁判地图。背景、草丛和飞镖的 Blender 入口在 [packaging/art](../packaging/art/)。
