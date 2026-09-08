# AGENTS.md — HL2SB / source-engine 开发须知

本文件记录在这个仓库里干活时必须知道的硬事实，避免重复踩坑。
内容按「事实 + 证据」组织，凡是踩过的坑都标了根因。

---

## 1. 项目布局

| 路径 | 是什么 | 版本控制 |
|---|---|---|
| `D:\project\source-engine` | 引擎源码（nillerusr fork + HL2SB 改动） | ✅ git，分支 `c_hands_alignment_fix` |
| `D:\srceng\hl2sb` | 游戏内容目录（mod 本体：cfg / resource / lua / materials） | ❌ **不是 git 仓库** |
| `D:\srceng\bin` | 部分 DLL 的部署位置（见第 3 节） | ❌ |
| `D:\srceng\hl2` | 基础游戏内容（hl2 的 res / scripts） | ❌ |
| `D:\srceng\hl2mp vpkdir` | HL2MP 基础内容（含 `scripts/hudlayout.res`） | ❌ |
| `D:\games\garrysmod` | GMod 安装（内容来源，`garrysmod/garrysmod_*.vpk`） | ❌ |

⚠️ **游戏内容不在 git 里**。改 `cfg/`、`resource/`、`lua/` 下的文件没有历史记录，
需要备份时手动复制。曾多次因此丢失过改动。

---

## 2. 构建

```powershell
cd D:\project\source-engine
cmd /c ".\waf.bat build --targets=client"      # client.dll
cmd /c ".\waf.bat build --targets=server"      # server.dll
cmd /c ".\waf.bat build --targets=GameUI"      # GameUI.dll
```

### 编码坑（重要）

- **不要**用 `python .\waf build` 直接跑：MSVC 输出中文时 Python 会
  `UnicodeEncodeError: 'gbk' codec ...`。`waf.bat` 内部设了
  `PYTHONIOENCODING=UTF-8` + `chcp 1252`，能正常处理。
- **`PYTHONIOENCODING=latin1` 不可行**：编译器输出含 U+FFFD 替换字符，
  不在 Latin-1 范围（0-255），必然报错。
- 源码文件避免非 ASCII 字符（em dash `—` 之类），否则触发 MSVC **C4819** 警告。
  用 `-` 代替。

### 构建系统怎么找源文件

`game/client/wscript` → `vpc_parser.parse_vpcs(...)`，游戏配置在：

```python
'hl2sb': ['client_base.vpc', 'client_hl2mp.vpc', 'client_lua.vpc'],
```

**只有列在这三个 .vpc 里的文件才会被编译。** 源码树里存在但没列进去的文件是死代码
（例：`game/client/hl2mp/hud_deathnotice.cpp` 已删除）。

⚠️ `build/compile_commands.json` 会残留历史条目，**不能用它判断某文件是否参与编译**。
可靠做法：在产出的 DLL 里搜字符串，或查 `.vpc`。

---

## 3. 部署路径（最容易搞错的地方）

| 产物 | 部署到 |
|---|---|
| `build\game\client\client.dll` | `D:\srceng\hl2sb\bin\client.dll` |
| 同上（副本） | `D:\srceng\bin\client.dll` |
| `build\game\server\server.dll` | `D:\srceng\hl2sb\bin\server.dll` |
| `build\gameui\GameUI.dll` | **`D:\srceng\bin\GameUI.dll`** ← 注意不是 hl2sb\bin |

- 部署前先确认游戏没在运行（`Get-Process hl2,hl2sb`），否则文件被占用复制失败。
- **DLL 只在进程启动时加载**：改了 C++ 必须**完全重启游戏**，重载地图无效。
- Lua 脚本在地图加载时读取，所以改 Lua 只需重进地图，或控制台
  `lua_dofile_cl <path>` 热重载。

---

## 4. Git 约定

- 分支 `c_hands_alignment_fix`，推送 `origin`（`stephen-cusi/source-engine-mod`）。
- **绝不 `git add`**：`lib`（子模块）、`thirdparty`（未跟踪）。
- 提交信息用 conventional commits：`fix(client): ...` / `feat(lua): ...`。
- 多行提交信息写进临时文件用 `git commit -F <file>`，避免 PowerShell 引号问题。
- 仓库有**并发提交**（其他会话/人也在改），提交前先 `git log --oneline -5` 看有没有新提交。

---

## 5. 引擎/框架的坑（踩过的，都有根因）

### 5.1 vgui

| 坑 | 根因 | 解法 |
|---|---|---|
| 控件文字完全不显示 | `clientscheme.res` 只定义了 `Default`、`DefaultSmall`、`DefaultVerySmall` 三个字体。`GetFont("DefaultBold")` 返回 `INVALID_FONT` | 字体名只用 `Default` |
| `OnThink()` / `OnTick()` 从不执行 | vgui 只对调用过 `ivgui()->AddTickSignal(GetVPanel())` 的面板发 Tick 消息 | 构造函数里注册 |
| `.res` 里 `"xpos" "c-600"` 导致窗口跑到左上角 | `c` 是相对父面板居中，构造时父面板还是 0×0 | 只从 res 读 `wide`/`tall`，位置用 `surface()->GetScreenSize()` 自己算 |
| 面板收不到 `RowSelected` | 消息走 `PostActionSignal` 发给父面板，跨层级会丢 | 用 `OnThink` 轮询 `GetSelectedItem()`，或让列表的父就是该面板 |

### 5.2 CModelPanel（3D 模型预览）

| 坑 | 根因 | 解法 |
|---|---|---|
| `SwapModel()` 什么都不做 | 开头 `if (!m_pModelInfo) return;`，而 `m_pModelInfo` **只**由 `ParseModelInfo()` 创建，它只在 `ApplySettings()` 遇到 `"model"` 子键时运行 | 手动构造 KeyValues 喂给 `ApplySettings()` |
| 预览相机跑进模型内部 | `GetModelRenderBounds()` 对移植/重打包模型返回错误包围盒 | 优先用 `CStudioHdr::hull_min/max()` + `view_bbmin/max()` |
| 打开菜单后世界材质变紫 / 满屏噪点 | `CModelPanel::Paint()` 是为主菜单写的，在游戏里泄漏状态：cubemap 恢复成 NULL 而非原值、`SetColorModulation`/`SetBlend` 不复位、**spotlight 时把栈上局部 `LightDesc_t` 的地址交给 `g_pStudioRender->SetLocalLights()`**（返回后指针悬空） | 子类覆写 `Paint()` 保存/还原状态，并在 `BaseClass::Paint()` 后 `g_pStudioRender->SetLocalLights(0, NULL)` |

### 5.3 渲染 / 武器

| 坑 | 根因 | 解法 |
|---|---|---|
| 开枪后出现**两个**枪口火（一个在枪口、一个在腰部） | `AE_MUZZLEFLASH` 由**实体自身的开火动画**派发，本地玩家的**世界武器模型**也带这个事件，且 `DispatchMuzzleEffect(options, true)` 永远传 `firstPerson=true` | `C_BaseAnimating::FireEvent` 里第一人称下跳过本地玩家的世界模型/世界武器（排除 viewmodel，第三人称保留） |
| 镜像里看不到本地玩家/武器 | 第一人称下本地玩家和武器被排除出渲染列表 | `g_bRenderingReflection` 全局标志，`CReflectiveGlassView::Draw()` 前后置位 |

### 5.4 Lua SDK（Team Sandbox 血统）

**加载路径**（`CHLClient::LevelInitPreEntity`，每次地图加载执行）：

```
lua/includes/extensions
lua/includes/modules
lua/game/shared
lua/game/client      ← 客户端脚本放这里
lua/game/server      ← 服务端（server 侧）
```

⚠️ **`lua/autorun/` 根本不会被加载**。放那儿的脚本是死的。

**可用的 GMod 风格 API**：
- `hook.add(name, id, fn)` / `hook.call` — 与 GMod 签名一致
- `surface.*` — 完整的 `vgui::ISurface` 绑定（DrawPrintText / GetTextSize / CreateFont / SetFontGlyphSet / DrawTexturedRect / GetScreenSize …）
- `concommand.Create(name, cb, help, flags)`
- `Color(r,g,b,a)`、`gpGlobals.curtime()`、`_CLIENT` / `_GAME`
- `print()` → 控制台 + 日志

**已修复的绑定 bug**（改在 `public/lua/vgui/LISurface.cpp`）：
- `surface.GetTextSize` 分配了 wchar 缓冲却**没调用 `ConvertANSIToUnicode`**，
  量的是未初始化栈内存 → 返回垃圾宽度。**所有 Lua 文本布局都受影响。**
- `surface.AddCustomFontFile` 在函数体和注册表两处都被注释掉，导致
  sandbox gamemode 的 `CreateDefaultPanels()` 报 nil。原注释版还只传了 1 个参数，
  而 `ISurface::AddCustomFontFile(name, file)` 需要 2 个。

**`HudViewportPaint` 钩子**（≈ GMod 的 `HUDPaint`）：
由 `CScriptedHudViewport::Paint()` 每帧触发。该面板**之前从未被挂载**
（`ClientModeShared::Enable()` 没给它设父面板，`Layout()` 里 `SetBounds` 被注释掉），
所以钩子从来没跑过。已修复。

---

## 6. 击杀播报（kill feed）

### 事件来源（服务端）

| 触发点 | 事件 | 覆盖 |
|---|---|---|
| `CHL2MPRules::DeathNotice()` | `player_death` | 玩家杀玩家、自杀、世界杀玩家 |
| `CBaseCombatCharacter::Event_Killed()` → `SendOnKilledGameEvent()` | `entity_killed` | 所有战斗角色（NPC）死亡 |

`entity_killed` 被 HL2SB 额外填充：`attackername`（攻击者类名）、
`attacker_uid`（仅玩家非 0）、`victimclass`、武器类名。

客户端对 `victimclass` 以 `player` 开头的直接 return，避免和 `player_death` 重复。

### 绘制归属

- C++ `CHudKillFeed` 收事件；`cl_killfeed_lua 1`（默认）时转发给 Lua 钩子
  `AddDeathNotice(attacker, attackerTeam, inflictor, victim, victimTeam, suicide, victimIsNPC, killerIsPlayer)` 并**不自己绘制**。
- Lua 实现：`D:\srceng\hl2sb\lua\game\client\hl2sb_deathnotice.lua`
- `cl_killfeed_lua 0` 时回退到 C++ 绘制。

### 相关 cvar

| cvar | 默认 | 说明 |
|---|---|---|
| `hud_deathnotice_time` | 6 | 每条存活秒数 |
| `cl_drawdeathnotice` | 1 | 总开关 |
| `hud_killfeed_max` | 4 | 最多行数。`0` = 不限（GMod 行为），`-1` = 用 res 的 `MaxDeathNotices` |
| `cl_killfeed_lua` | 1 | 是否由 Lua 绘制 |
| `hl2sb_killfeed_icon_y` | 6 | 图标垂直微调（正数下移），实时生效 |

### res 文件

| 文件 | 块 | 谁读 |
|---|---|---|
| `hl2sb/scripts/hudlayout.res` | `HudKillFeed` | C++ 版（Lua 版不读 res） |
| `hl2mp vpkdir/scripts/hudlayout.res` | `HudDeathNotice` | 无（旧面板已删） |
| `hl2/scripts/hudlayout.res` | `HudDeathNotice` | 无 |

GMod 也有 `HudDeathNotice` 块（`D:\games\garrysmod\sourceengine\scripts\hudlayout.res:316`），
但只有 `visible/enabled/wide/tall`——因为 GMod 的死亡提示是 Lua 画的，那块是引擎基础脚本残留。

### 图标材质

世界/自杀击杀走纹理路径，材质是 GMod 的 `hud/killicons/default`，
**引擎自带包里没有**。缺失时 `DrawSetTextureFile()`（返回 void）静默绑定
ERROR 材质 → 粉色棋盘格。已补文件 + 代码里用 `filesystem->FileExists()` 兜底：

```
D:\srceng\hl2sb\materials\hud\killicons\default.vmt
D:\srceng\hl2sb\materials\hud\killicons\default.vtf
```

武器图标是**字体字形**（`scripts/mod_textures.txt` 里映射到 `HL2MP`/`csd` 字体），
Lua 里复刻了这张映射表（`KILLICON_GLYPH`）。

---

## 7. 调试手段

### 日志
`D:\srceng\hl2sb\ds_debug.log`（`autoexec.cfg` 里 `con_logfile` 打开）
`D:\srceng\hl2sb\ds_debug.prev.log`（上一份）

### Lua 加载诊断
`luasrc_dofolder()` 已加逐文件日志：
```
[Lua]   lua/game/client/hl2sb_deathnotice.lua
[Lua] lua/game/client -> 2 file(s)
```

### Lua 热重载
```
lua_dofile_cl game/client/hl2sb_deathnotice.lua
```
注意：命令内部会加 `lua/` 前缀，所以路径**不要**写 `lua/`。

### 验证脚本是否真的加载
看日志里的加载标记（脚本自己 `print` 的）。**没有输出不等于没加载**——
曾经因为「脚本版本还没有 print 标记」而误判过，务必确认时间线。

### 离线验证 Lua 脚本
引擎源码自带 Lua 5.1.5，可编出独立解释器做语法检查 + mock 功能测试：
```
D:\project\luacheck\lua_syntax.exe
D:\project\luacheck\test_deathnotice.lua
```
（用 `vcvars64.bat` + `cl.exe` 编译 `lua/src/*.c`，需带上 `bit.c`。）

### 排查 DLL 里有没有某个面板
在二进制里搜字符串（最可靠）：
```powershell
$t = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dll))
([regex]::Matches($t, "HudKillFeed")).Count
```

---

## 8. 本地化

- 客户端 token 文件：`hl2sb/resource/hl2sb_english.txt`、`hl2sb_schinese.txt`
- **必须是 UTF-16LE + BOM**（vgui 的要求），且 token 要放在 `"Tokens"` 块**内部**
  （即倒数第二个 `}` 之前，不是最后一个）。
- 用 PowerShell `Set-Content` 改会破坏编码，用 Python 按字节操作。
- 面板里的 `#Token` 由 vgui 自动解析；带参数的用
  `g_pVGuiLocalize->ConstructString()`。
