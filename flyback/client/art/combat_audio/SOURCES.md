# 战斗拟音：声源与混音

出镖是竹棍掠过空气；击中才出现拍击和短木响。格挡收在短促的金属闷音上，两镖相撞则留一段更亮的振铃。接镖用木扣与皮革的小动作，避免和击中混淆。

18 个文件均为 44.1 kHz、16 位 PCM 单声道，按声音实际发生的位置声像定位。`01/02` 是同一动作的两个剪辑，并非同一文件改名。所有声部都剪自已发布的外部采样；只调整速度、均衡、混音电平和淡入淡出。组间轻重已写入文件，建议以相同播放增益接入。

| 文件前缀 | 原始采样（两种变体使用的片段） | 时长 | 峰值 |
|---|---|---:|---:|
| `release` 出手 | `swosh-11/15` 竹棍＋`knifeSlice2/knifeSlice` 擦响＋`impactWood_light_000/001` 木接触 | 142–146 ms | −8.5 dBFS |
| `swing` 挥空 | `swosh-07/19`＋`cloth3/cloth1`＋`knifeSlice/knifeSlice2` 的短边缘，保留风声而不叠撞击 | 117–122 ms | −10.5 dBFS |
| `hit` 命中 | `wet_towel_on_body-04/33` 毛巾拍击＋`chop` 的瞬态＋`impactWood_light_002/001` 的短木响 | 148 ms | −3.5 dBFS |
| `kick` 踢中 | `wet_towel_on_body-05/04`＋`footstep_carpet_002/001`＋`dropLeather` | 166 ms | −4.5 dBFS |
| `block` 格挡 | `impactMetal_medium_001/002`＋`metal_interaction2/1`＋`impactWood_light_000/001` | 124 ms | −5.5 dBFS |
| `clash` 两镖相撞 | `impactMetal_light_000/001`＋`impactMetal_medium_000/002`＋`metal_interaction1/2`，保留短振铃 | 209 ms | −5.5 dBFS |
| `catch` 接回 | `impactWood_light_000/001`＋`handleSmallLeather/dropLeather`＋`metal_button_press1/2` | 74 ms | −11 dBFS |
| `dash` 冲刺 | `swosh-05/27`＋`cloth1/cloth3`＋`footstep_carpet_000/001`，削弱高频以避开出手声 | 166–169 ms | −12.5 dBFS |
| `warning_01` 缩圈预警 | `crack07.mp3.flac`＋`impactwood14.mp3.flac`＋`crack06.mp3.flac` 三个木材碎裂片段 | 270 ms | −10 dBFS |
| `fall_01` 坠落 | `impactwood18.mp3.flac`＋`crack10.mp3.flac`＋`swosh-05` 收尾 | 302 ms | −11 dBFS |

八组操作与接触音的开头在 0.11–0.59 ms 内越过 −40 dBFS；留尾最长的是两镖相撞的 209 ms。这是素材文件的起音时间，不含设备输出延迟。战斗声素材未加入音乐、倒计时蜂鸣或通用死亡提示。

## 来源及许可

- qubodup / Iwan Gabovitch：[竹棍挥动实录](https://opengameart.org/content/swish-bamboo-stick-weapon-swhoshes)、[湿毛巾拍击实录](https://opengameart.org/content/40-wet-towel-clubpoundhitattack-sounds)、[Metal Interactions](https://opengameart.org/content/metal-interactions)。前两组由作者在发布页说明录制动作；毛巾组的页面明确以 CC0 取代旧压缩包内的早期授权文本。
- Independent.nu（qubodup 发布）：[35 wooden cracks/hits/destructions](https://opengameart.org/content/35-wooden-crackshitsdestructions)。
- Kenney：[Impact Sounds](https://kenney.nl/assets/impact-sounds)、[RPG Audio](https://kenney.nl/assets/rpg-audio)。使用包内的木、金属、衣料与皮革拟音；不推定这组素材具体使用了哪种录音器材。

各来源均为 [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)，可随游戏分发与改编。发行包附 `licenses/combat-audio.txt`。
