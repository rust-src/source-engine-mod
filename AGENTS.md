# AGENTS.md — hl2sb (Source Engine 2013 + Lua) GMod 兼容

> 本文档供接手的 AI Agent（GLM / 其它模型）在没有对话上下文的情况下继续工作。
> 作用：把当前 hl2sb 项目状态、构建部署方法、已完成的改动、已知问题和下一步方向**一次写全**。
> 接手前请先读本文件，再读 `D:\srceng\hl2sb` 里实际的 lua 文件与 `game\shared\lua\` 的 C++ 绑定源码。

> **移植分支**：2026-09-05 为 GMod SWEP 完整移植开了 `gmod-swep-port` 分支（基于 hl2sb）。
> GMod SWEP 接口权威参考见 `GMOD_SWEP_REFERENCE.md`（抓自 wiki.facepunch.com/gmod/Structures/SWEP 与 /gmod/WEAPON）。
> **本次移植分两阶段**：阶段1 纯 Lua（weapon_base + gmod_compat，不重编）；阶段2 引擎侧 C++ 深度移植（重编 DLL）。

---

## 1. 项目是什么

- **hl2sb** = HL2MP（多人 + Lua SDK）的一个 fork（基于 nillerusr/source-engine 2013）。
- 目标：**直接放 GMod 原版插件就能用**。当前针对的插件是 “The Ultimate Admin Gun Fixed” 的 `pist_weagon` SWEP。
- 仓库源码：`D:\project\source-engine`，分支 `hl2sb`，HEAD `40f449c`（已提交推送）。
- 游戏安装目录（实际运行、扔 lua/模型/sound 的地方）：`D:\srceng\hl2sb`。
- 插件原始文件：`D:\project\gmod mod\The Ultimate Admin Gun Fixed\`（`lua\weapons\pist_weagon\shared.lua` 等）。

---

## 2. 构建与部署

```powershell
cd D:\project\source-engine
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
python ./waf build --build-games=hl2sb --target=client,server -j10
```

- 产物：`build\game\client\client.dll`、`build\game\server\server.dll`。
- 部署：把这两个 dll 复制到 `D:\srceng\hl2sb\bin\`。
- 回滚备份：旧的可运行版在 `D:\srceng\hl2sb\bin\_backup_40f449c\`（`client.dll.bak` / `server.dll.bak`）。

### VPC / 构建接线（重要）

- `game\client\wscript` 与 `game\server\wscript` 里的 `games` 表：
  ```python
  'hl2sb': ['client_base.vpc', 'client_hl2sb.vpc']
  'hl2sb': ['server_base.vpc', 'server_hl2sb.vpc']
  ```
- 新增的专用工程文件：`game\client\client_hl2sb.vpc`、`game\server\server_hl2sb.vpc`。二者是**自包含**的：把原先拼装的 `client_hl2mp.vpc + client_lua.vpc`（server 同理）完整合并，项目名改 `Client/Server (HL2SB)`，`GAMENAME=hl2sb`，并 `$Include` 各自 base 与 `nav_mesh.vpc`。
- **`$Include` 在 waf 的 `vpc_parser` 里是空操作**，所以 VPC 必须自己列全所有源文件，不能靠 include 聚合。
- **Include 目录顺序很关键，绝对不要 sort**：顺序决定同名头文件谁被命中（`hl2_gamerules.cpp` 里 `weapon_physcannon.h` 若先解析到 `game\shared\hl2mp\weapon_physcannon.h`（不含 `PhysCannonAccountableForObject`）会报 `C3861`。正确顺序是 `.\hl2`（server/hl2）排在 `$SRCDIR\game\shared\hl2mp` 之前）。
- 校验改动时用**完整有序比对**（sources/defines/includes 三者按出现顺序逐项相等），不要只比集合。

---

## 3. 已完成的改动（当前工作区 / 部署状态）

### 仓库内（未提交，工作区状态）
- `lua/src/llex.c`：给 Lua 词法器加了 `case '/':`，让 `//` 成为行注释；后又新增 `case '!'`（`!`→TK_NOT、`!=`→TK_NE）、`case '&'`（`&&`→TK_AND）、`case '|'`（`||`→TK_OR）。这是“原版插件一字不改能跑”的前提（pist_weagon 用了 `//` 和 `!`）。
- `game\shared\lua\weapon_hl2mpbase_scriptedweapon.cpp`：`InitScriptedWeapon` 读小写 `viewmodel`/`playermodel` 失败时回退读大写 `ViewModel`/`WorldModel`。
- `game\shared\lua\luamanager.cpp`：`luasrc_LoadWeapons` 在设全局 `SWEP` 前预置 `Primary`/`Secondary` 空表（修顶层 `SWEP.Primary.Sound` 报 nil）。
- `game\server\lua\lbaseentity.cpp`：新增 `CBaseEntity_Fire`（GMod `Entity:Fire(input,value,delay)`，value 转字符串 `AllocPooledString`→`variant_t`，delay>0 走 `g_EventQueue`），注册在 server meta 表 `{"AcceptInput", ...}` 旁；include 增 `eventqueue.h`、`gamestringpool.h`。该文件是 UTF-8 可直接编辑。
- `game\client\wscript` / `game\server\wscript`：hl2sb 改指向专用 VPC。
- `game\client\client_hl2sb.vpc` / `game\server\server_hl2sb.vpc`：新增专用工程文件。
- 用以上改动**全量编译成功**（2026-09-05），已部署到 `D:\srceng\hl2sb\bin\`。**用户要求：不要 git 提交这些改动。**

### 游戏目录 `D:\srceng\hl2sb`（兼容层，**不在仓库里**，是直接扔到安装目录的）
- `lua\weapons\weapon_base\shared.lua` 的 `SWEP.ViewModel/WorldModel` 已从硬编码 pist_weagon 模型改为**空字符串**，并同时设小写 `viewmodel`/`playermodel`（每个 SWEP 自己设模型，两个键名风格都设）。
- `lua\game\server\gmod_compat.lua` 和 `lua\game\client\gmod_compat.lua`（内容相同）：
  - GMod 全局：`SERVER`/`CLIENT`（由 `_CLIENT` 推导，hl2sb 不设 `_SERVER`）、`CurTime`、`Sound`、`Angle(=QAngle)`、`Color`、**`AddCSLuaFile`（空操作，已补）**。
  - `ents.Create/FindByClass`，`util.PrecacheSound/TraceLine`，ammo 名→索引 map。
  - `FireBullets`：GMod bullet 表 `{Num,Src,Dir,Spread,Tracer,Force,Damage,AmmoType,Attacker,Inflictor}` → hl2sb `FireBulletsInfo_t` 表的包装（挂在 `CBaseEntity` metatable）。
  - `weapon.get` 补丁：尊重 GMod `SWEP.Base`（把 base 的方法铺进 SWEP）。
  - `CBaseCombatWeapon.__index`：读 `Owner` 返回 `GetOwner()`。
  - player 的 `GetShootPos/GetAimVector/GetEyeTrace` Lua 回退；entity 的 `SetPos/SetKeyValue/SetOwner/Fire`（**Fire 目前是空桩，不引爆**）。
- `lua\weapons\weapon_base\shared.lua`：GMod 兼容的武器基类（`CanPrimaryAttack/SetNextPrimaryFire/TakePrimaryAmmo/ShootEffects/ShootBullet/PrimaryAttack/SecondaryAttack/Reload/Deploy/Holster/Initialize/SetWeaponHoldType/ItemPostFrame` 自动开火用 `bit.band(buttons,1)/(2048)/(8192)`）。
- `lua\weapons\pist_weagon\`：`shared.lua` = **逐字节原版 GMod**（5145 字节，含 `//`），`init.lua` 与 `cl_init.lua` 各 `include("shared.lua")`（hl2sb 要求这两个文件存在才能注册武器）。
- `lua\autorun\{server,client}\gmod_compat.lua`：薄 include（**autorun 在 hl2sb 无效**，实际加载走 `lua/game/*` 的自加载）。

---

## 4. 已知问题（当前未解决）

### 4.1 “还是一把左轮” —— ✅ 根因已找到并修复（2026-09-05，待实机验证）
- **真正根因不是模型映射，而是 SWEP 脚本解析失败**：`ds_debug.log` 里 `lua\weapons\pist_weagon/shared.lua:96: unexpected symbol near '!'`。原版插件第 96 行用了 GMod 的 `!`（逻辑非），此前 llex.c 只补了 `//` 注释，`!` 仍是非法字符 → SWEP 未注册 → `give` 回落成 357 左轮。
- 修复：`lua\src\llex.c` 新增 GMod C 风格操作符：`!`→`TK_NOT`、`!=`→`TK_NE`、`&&`→`TK_AND`、`||`→`TK_OR`（lparser.c 原生支持这些 token，无需改）。
- 模型映射次因也已兜底：`game\shared\lua\weapon_hl2mpbase_scriptedweapon.cpp` 的 `InitScriptedWeapon` 读小写 `viewmodel`/`playermodel` 失败时回退读大写 `ViewModel`/`WorldModel`（GMod SWEP 只写大写）。
- `D:\srceng\hl2sb\lua\weapons\weapon_base\shared.lua` 里遗留的 pist_weagon 模型硬编码已清除（否则所有继承 SWEP 都回落到该模型），改为空默认并同设大小写两套键。

### 4.2 副武器不爆炸 —— ✅ 已加 C++ Fire 绑定（2026-09-05，待实机验证）
- **server 侧本来就有 `AcceptInput` 绑定**（`game\server\lua\lbaseentity.cpp` 原 154 行，注册于原 674 行 meta 表），只是没有 GMod 风格的 `Fire`。
- 已在该文件新增 `CBaseEntity_Fire`（紧挨 `CBaseEntity_AcceptInput`）：`Entity:Fire(input, value, delay)`。value 一律按 GMod 语义转成字符串经 `AllocPooledString` 塞进 `variant_t`；`delay > 0` 走 `g_EventQueue.AddEvent`（含 variant 的重载，`eventqueue.h:45`），`delay <= 0` 直接 `AcceptInput(input, self, self, value, 0)`。注册项 `{"Fire", CBaseEntity_Fire}` 在 `{"AcceptInput", ...}` 旁。
- 该文件是 **UTF-8**，可直接编辑（与 shared 下 GBK 文件不同）；新 include 了 `eventqueue.h`、`gamestringpool.h`。
- `gmod_compat.lua` 的 Lua Fire 空桩保留（client 无 AcceptInput）；server 侧因 C++ 注册了 `Fire`，`emt.Fire == nil` 不成立，桩自动跳过。

### 4.3 兼容层未经实机完整验证
- 主武器（左键连发）是否已真正开火，用户只反馈 “还是一把左轮”，未明确确认开火成功。需实机测试 `give pist_weagon` + 左键。

### 4.4 顶层 `SWEP.Primary.Sound` 报 nil → 右键失效（2026-09-05，已改 loader，待实机验证）
- **根因**：`game\shared\lua\luamanager.cpp` 的 `luasrc_LoadWeapons`（约 471–505 行）在跑武器顶层 `init.lua`→`include("shared.lua")` 前，只给全局 `SWEP` 表填 `__folder`/`__base`，**没预置 `Primary`/`Secondary`**。GMod 风格顶层 `SWEP.Primary.Sound = ...`（pist_weagon/shared.lua:59）此时 `SWEP.Primary` 为 nil → 报 `attempt to index field 'Primary' (a nil value)` → 脚本中断、`weapon.register` 未调用 → 方法/表不完整 → 右键 `SecondaryAttack`（用 `self.Secondary.Recoil`、`ents.Create`）失效。
- 修复：在 `luasrc_LoadWeapons` 的 `lua_setglobal(L,"SWEP")`（原 478 行）**前**插入 `lua_newtable`+`lua_setfield(L,-2,"Primary"/"Secondary")`（现 478–484 行），让顶层 `SWEP.Primary.X=...` 不再报 nil；真实的 base 由之后 `weapon.get`→`table.inherit` 铺入/覆盖（`includes\modules\weapon.lua` 用 `table.copy`+`table.inherit`，`table.inherit` 只在 `t[k]==nil` 时才拷入，故顶层先写入的键会保留，未写的用 base 值——GMod 语义）。
- 注意：`luamanager.cpp` 实为 **ISO-8859-1/单字节**（非 GBK），Read/Edit 工具读不了，必须用 `python` 以 `latin-1` 无损读改写（锚点带 `\r\n`）。

### 4.5 左键 `FireBullets` 的 Spread 误判 + GetEyeTrace 依赖缺失（2026-09-05，已改兼容层）
- **修复**：`gmod_firebullets` 新增 `is_vector(v)`（识别 metatable `__type=="vector"` 的 userdata，lvector.cpp 确认设了 `__type="vector"`），`is_vector(sp)` 直接透传 `info.m_vecSpread = sp`；table 分支用 `.x/.y/.z`。这样 `pist_weagon` 顶部 `Vector(...)` 不再被误包。
- **右键 GetEyeTrace 依赖缺失（必修）**：`gmod_compat.lua` 声称依赖的全局 `MAX_TRACE_LENGTH` 实为 **C++ 宏**（worldsize.h:32），在 Lua VM 是 **nil**；`_E` 是**空表**，`_E.MASK.SHOT` 走不通（真正枚举在独立全局 `MASK`，lbspflags.cpp）。导致 `GetEyeTrace` 回退（`self.Owner:GetEyeTrace()` → `start + fwd*MAX_TRACE_LENGTH`、`_E.MASK.SHOT`）必崩 → `eyetrace.HitPos` 拿不到 → 副武器 `env_explosion` 放置失败。
- **修复**：在 `gmod_compat.lua` 顶部前置定义：
  ```lua
  MAX_TRACE_LENGTH = MAX_TRACE_LENGTH or 32768
  if _E == nil then _E = {} end
  if _E.MASK == nil and _G.MASK then _E.MASK = _G.MASK end
  ```
  （`trace_t`、`util.TraceLine`、`EyeVectors`、`EyePosition` 均存在，签名已核对。）
- **已知确认存在的库**：`trace_t`（public/lua/lgametrace.cpp `luaopen_CGameTrace`）、`util.TraceLine`（lutil_shared.cpp，返回 0 值、填充第 6 参 trace）、`EyeVectors`/`EyePosition`（lbaseplayer_shared.cpp:126/115）。`Vector`/`QAngle` metatable `__type` 分别为 `"vector"`/`"angle"`（lvector.cpp:315/455）。
- 两份兼容层 `lua\game\server\gmod_compat.lua`、`lua\game\client\gmod_compat.lua` 均已同步修复；server 版额外保留异常 Spread 打点（`[gmod_fb] ANOMALOUS ...`）与 `GetEyeTrace` 打点（`[gmod_eYetrace] ...`），正常左键不刷屏。
- 注意：无 `lua_cache` 目录，`gmod_compat.lua` 每次 LevelInit 从盘直读，改文件即生效（但**需重启游戏**）。
- **剩余待查**：`attempt to index a number value`（sandbox gamemode 报的，与武器链路无关，见 §4.x 之前记录）。

### 4.6 【关键】`Msg` 不是 hl2sb 的 Lua 全局 → 之前"补丁无效"的真凶（2026-09-05）
- **现象**：日志刷 `gmod_compat.lua:169: attempt to call global 'Msg' (a nil value)`。
- **根因**：hl2sb 的 Lua VM 里全局消息函数是 **`print`**（`game\shared\lua\luamanager.cpp:59` 定义 `luasrc_print` 并注册为全局 `print`，重定向到引擎日志），**没有 `Msg`**。`Msg` 只在 C++ 侧是宏，不进 Lua。我在兼容层加的所有 `Msg(...)` 打点 → 每次 `gmod_firebullets`/`GetEyeTrace` 被调用时第一行就崩 → **整个开火链被我自己打断**，导致前面所有修复看似无效。
- **教训**：给 hl2sb Lua 加诊断输出**一律用 `print`**，不要用 `Msg`。服务端 `print` 会写进 `ds_debug.log`（luasrc_print 重定向）。
- 已把 server 版 `gmod_compat.lua` 里 3 处 `Msg(` 改成 `print(`，尾部 `if Msg then end` 改成 `if print then end`。client 版无打点、无 `Msg(`。
- **后续所有补丁若"无效"，先查是否用了 `Msg` 或其它不存在全局**——这正是本轮反复踩坑的根因。

### 4.7 【已解决】左键 Spread 误包：hl2sb 重写了 `type()`（2026-09-05，日志验证）
- **真相**：hl2sb 在 `game\shared\lua\luamanager.cpp:81` 定义了 `luasrc_type` 并注册为全局 `type`（`luamanager.cpp:116`）。它逻辑是：凡带 `__type` metatable 的对象，`type()` **直接返回 `__type` 的值**。所以 `type(Vector) == "vector"`，`type(QAngle) == "angle"`，而非标准 Lua 的 `"userdata"`。
- **之前判断错误**：我在 `is_vector` 里用 `type(v)=="userdata" and getmetatable(v).__type=="vector"` —— 但 `type(vector)` 压根返回 `"vector"`，`type(v)=="userdata"` 恒为 false，`is_vector` 永远拦不住 → 左键 `bullet.Spread` 落入 `elseif sp` 分支 → `Vector(sp,sp,0)` 把整个 Vector 当数字 → `bad argument #2 to 'Vector' (number expected, got userdata)`。
- **修复**：`is_vector` 改成了 `return type(v) == "vector"`。两份 `gmod_compat.lua` 已改，并清理了所有诊断打点（`trace_firebullets` 函数、`GetEyeTrace` 里的 `Msg`/`print` 打点全删）。服务器/客户端安装目录的 4 份副本仅 `game/{server,client}/gmod_compat.lua` 生效，autorun 两版是 stub。
- **验证（ds_debug.log 会话 9831 起）**：`bad argument #2 to 'Vector'` 完全消失。
- **遗留**：`attempt to index a number value` 仍出现（约 54 次/会话），但与 `CBaseAnimatingOverlay::AddGesture ... ACT_HL2MP_GESTURE_RANGE_ATTACK/RELOAD`、`SV_StartSound ... deagle-1.wav not precached` 成对——是**玩家模型（combine_soldier）缺 gesture 活动**的动画噪音，与武器 Lua 链路无关。`v_pist_weagon.mdl` 缺 `v_hands` 材质同理（只影响渲染）。
- **右键状态**：`CBaseEntity_Fire`（AcceptInput）已绑定（`game\server\lua\lbaseentity.cpp:164/704`），server.dll 21:37 已含该绑定。`GetEyeTrace` 回退依赖（`MAX_TRACE_LENGTH`/`_E.MASK.SHOT`/`trace_t`/`util.TraceLine`）已补齐。右键是否真正走通 `ents.Create("env_explosion")`+`Fire("Explode")` 尚未实机确认（日志未见到爆炸痕迹）。

### 4.8 【阶段2 C++】爆炸特效/弹孔/模型闪左轮 三修复（2026-09-06，已重编部署，待实机验证）
分支 `gmod-swep-port`，5 个 C++ 文件改动：

- **爆炸无特效**：`game\server\explode.cpp` 的 `CEnvExplosion` 构造函数加 `m_nRenderMode = kRenderTransAdd`。原因：SWEP 经 `ents.Create("env_explosion")` 创建时 `m_nRenderMode` 默认 `kRenderNormal`，`InputExplode` 里 explode.cpp:289 置起 `TE_EXPLFLAG_NOADDITIVE` 抑制 additive 火球；原生 `ExplosionCreate`(explode.cpp:410) 显式设 `kRenderTransAdd` 故不置。另把 pistol 的 `SetKeyValue("iMagnitude")` 移到 `Spawn()` 前（原版插件顺序 bug，因 `m_spriteScale=(iMagnitude-50)*0.6` 在 Spawn 时算一次，Spawn 后设不重算）。
- **无弹孔**：`game\server\hl2mp\te_hl2mp_shotgun_shot.cpp:65` 的 `SendPropInt(m_iShots, 5)` 改 **8**（5-bit 上限 31，75 发被截成 11 发、客户端重放错位→无 decal）；`game\server\hl2mp\hl2mp_player.cpp:649` 加 `>0` 守卫（避免把 GMod SWEP 的 `info.m_flDamage=99999999999` 覆盖成武器数据的 0）。client 端 `c_te_hl2mp_shotgun_shot.cpp` 的 `RecvPropInt(m_iShots)` 自适应 server 位宽，无需改。
- **开火/切枪闪左轮**：`game\shared\lua\weapon_hl2mpbase_scriptedweapon.cpp:131` 的 `ActivityList()` 加 `memset(m_acttable,0,sizeof)`（否则未初始化数组是垃圾值，`ActivityOverride` 乱映射）；`game\shared\basecombatweapon_shared.cpp:2355` 的 `SetIdealActivity()` 在 `SelectWeightedSequence==-1` 时回退 `ACT_VM_IDLE`（自定义 SWEP 模型常缺 `ACT_VM_*` 序列，缺序列时不再停留在旧序列/闪成 357 姿态）。

**重编**：`build\game\{client,server}\*.dll` 已重编（27.6s）并部署到 `D:\srceng\hl2sb\bin`（00:06）。备份 `_backup_40f449c` 仍在。
**待验证**：爆炸是否有 fireball/声音、墙上是否有弹孔、开火/切枪是否不再闪 357。

---

## 5. 关键技术事实（务必记住）

- **脚本武器类名 = 文件夹名**（无 `weapon_` 前缀）：`give pist_weagon`，不是 `weapon_pist_weagon`。
- **`IN` 不是全局变量**（server Lua VM 里 `IN` 为 nil）。硬编码按钮位：`IN_ATTACK=1`，`IN_ATTACK2=2048`，`IN_RELOAD=8192`，用 `bit.band`。
- **Lua = 原生 5.1.5**，现已补 `//` 注释（见 `llex.c`）。未补前 GMod 的 `//` 注释是非法语法。
- **hl2sb 没有 GMod 的 autorun loader**。它会**在加载武器之前**自动加载 `lua\game\shared`、`lua\game\server`、`lua\game\client` 下的**顶层** `.lua`。所以 `lua\autorun\...` 是无效的，兼容层必须放在 `lua/game/*`。
- **`SWEP.__base` 默认是 `weapon_hl2mpbase_scriptedweapon`**；hl2sb 的 loader 要求每个武器目录有 `init.lua`(server) + `cl_init.lua`(client)，各 `include("shared.lua")`。
- **必须同时设置**（否则模型错）：小写 `viewmodel`/`playermodel` 与大写 `ViewModel`/`WorldModel`。
- 脚本武器的 `Think` 不会被调用；每帧走的是 `SWEP:ItemPostFrame` / `ItemBusyFrame`。
- **不要轻易改动全局 `ItemPostFrame`**（之前一次全局改动导致所有 lua 武器崩溃/重置地图，已回退到 40f449c）。改动尽量限制在武器本身 + 兼容层。

---

## 6. 实机调试方法

- `D:\srceng\hl2sb\cfg\autoexec.cfg` 里已开：`con_logfile "ds_debug.log"` + `developer 1`。
- 客户端 Lua 报错会写到 `D:\srceng\hl2sb\ds_debug.log`。
- 测试流程：重启游戏 → `give pist_weagon` → 左键（连发）观察 open fire，右键（副武器）看是否爆炸 → 看 `ds_debug.log` 的红字报错。
- 可在兼容层/playattack 里加 `Msg("[gmod_compat] ...")` 打点定位。

---

## 7. 下一步（给接手的 Agent）

1. **实机验证（最重要）**：重启游戏 → `give pist_weagon` → 确认手中是 `v_pist_weagon` 模型（不再是 357）→ 左键连发 → 右键副武器扔出后看是否爆炸 → 查 `ds_debug.log` 红字。**特别注意**：`shared.lua:59` 的 `Primary is nil` 报错应已消失（本次 luamanager 改动）。
2. 若右键仍无反应：查 `PlayerPlayStepSound` 的 "attempt to index a number value"（这是 gamemode 报的，不属于副武器链路）；查 `v_pist_weagon.mdl` 缺 `v_hands` 材质（只影响渲染不影响开火）；必要时在 `gmod_compat` 的 `weapon.get` wrap 里把"覆盖"改成"合并"（`table.inherit` 只在 `t[k]==nil` 时拷入，若顶层已写入部分 Primary 键而 base 同名键没盖住，视情况调整）。
2. 若模型仍不对：查 `GiveNamedItem`/`give` 路径是否真的拿到 `pist_weagon` 类；看 `m_iViewModelIndex` 是否在 Precache 里赋上。
3. 已知小问题：sandbox gamemode 客户端 `AddCustomFontFile` 报错已用空桩修；`PlayerPlayStepSound` 的 "attempt to index a number value" 尚未查。
4. **不要 git 提交**（用户明确要求）；改完必须实机验证后再下结论。
5. 始终用有序比对校验 VPC 改动，include 目录顺序不要排序。
