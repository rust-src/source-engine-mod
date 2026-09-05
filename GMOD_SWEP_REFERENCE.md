# GMod SWEP (Scripted Weapon) Reference — 移植参考

> 来源：Facepunch wiki `gmod/Structures/SWEP`（2026-09-05 抓取，原始 HTML 1.49MB 已解析）。
> 目的：为 hl2sb 兼容/移植 GMod SWEP 提供权威接口清单。

## 1. SWEP 表字段（顶层）

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `ClassName` | string | 自动设置 | 实体类名，引擎自动填 |
| `ClassNameOverride` | string | — | 覆盖类名 |
| `Category` | string | `#spawnmenu.category.other` | 生成菜单分类 |
| `Spawnable` | bool | false | 是否可从生成菜单获取 |
| `AdminOnly` | bool | false | 生成菜单按钮是否仅管理员 |
| `PrintName` | string | `Scripted Weapon` | 显示名 |
| `Base` | string | `weapon_base` | 基类脚本，相对 `lua/weapons` |
| `m_WeaponDeploySpeed` | number | cvar | 部署速度倍率 |
| `Author` / `Contact` / `Purpose` / `Instructions` | string | "" | 元信息 |
| `ViewModel` | string | `models/weapons/v_pistol.mdl` | 视角模型路径 |
| `ViewModelFlip`/`ViewModelFlip1`/`ViewModelFlip2` | bool | false | 翻转 CS:S 风格视角模型 |
| `ViewModelFOV` | number | 62 | 持枪时视场角 |
| `WorldModel` | string | `models/weapons/w_357.mdl` | 世界模型路径 |
| `AutoSwitchFrom`/`AutoSwitchTo` | bool | true | 是否可自动切换走/到 |
| `Weight` | number | 5 | 自动切换权重 |
| `BobScale`/`SwayScale` | number | 1 | 视角模型摆动/摇摆缩放 |
| `BounceWeaponIcon` | bool | true | 武器选择图标弹跳 |
| `DrawWeaponInfoBox`/`DrawAmmo`/`DrawCrosshair` | bool | true | HUD 绘制开关 |
| `RenderGroup` | enum | — | 渲染组 |
| `WantsTranslucency` | bool | false | RenderGroup 未设时切到 `RENDERGROUP_BOTH` |
| `Slot` | number | 0 | 武器选择槽（从 0 起） |
| `SlotPos` | number | 10 | 槽内位置（0–128） |
| `SpeechBubbleLid` | number | — | 信息框绘制变量 |
| `WepSelectIcon` | number | — | 选择图标纹理 ID（.vmt） |
| `CSMuzzleFlashes`/`CSMuzzleX` | bool | false | CS 枪口闪光 |
| `Primary` | table | — | 主攻击设置（见 §2） |
| `Secondary` | table | — | 副攻击设置（与 Primary 同字段） |
| `UseHands` | bool | false | 骨合并玩家手到视角模型 |
| `Folder` | string | 自动 | `weapons/weapon_myweapon` 格式，加载自动设 |
| `AccurateCrosshair` | bool | false | 3D 准星位于真实瞄准点 |
| `DisableDuplicator` | bool | false | 禁止复制 |
| `ScriptedEntityType` | string | — | 脚本实体类型 |

## 2. Primary / Secondary 攻击设置子字段

`Primary` 与 `Secondary` 是相同结构的两张表。每个字段分别是：

| 字段 | 类型 | 说明 |
|---|---|---|
| `Ammo` | string | 弹药类型（`Pistol`、`SMG1` 等） |
| `ClipSize` | number | 弹夹容量；`-1` 表示无弹夹（如榴弹/火箭） |
| `DefaultClip` | number | 出生时弹夹弹药数；高于 ClipSize 则出生额外给弹药 |
| `Automatic` | bool | **true = 按住主攻击键自动开火**，false = 需每次点击 |

> 注：`Sound/Damage/TakeAmmo/Spread/NumberofShots/Recoil/Delay/Force` 这些字段是 **GMod 社区约定**，wiki 的 `Structures/SWEP` 页并未列出——它们由具体 SWEP 的 `PrimaryAttack`/`SecondaryAttack` 自己读取使用，不是引擎强制字段。`Automatic` 是引擎真正用来判断连发的字段。
> 判定：`Automatic=true` 时 `CBaseCombatWeapon` 帧循环在 `m_flNextPrimaryAttack <= now` 且按钮按住时持续调 `PrimaryAttack`；`Automatic=false` 需下一次点击（按钮按下沿）。

## 3. SWEP 可覆盖的方法（引擎会调用）

按字母序（来自 wiki 方法列表，非全部但含核心）：

- `Initialize` — 生成时
- `Deploy` / `Holster` — 部署/收起
- `CanPrimaryAttack` / `CanSecondaryAttack` — 能否攻击
- `PrimaryAttack` / `SecondaryAttack` — **主/副攻击执行体**
- `Think` — 玩家思考帧
- `Reload` — 换弹
- `ShootEffects` / `ShootBullet` — 射击特效/子弹
- `TakePrimaryAmmo` / `TakeSecondaryAmmo` — 扣弹药
- `SetNextPrimaryFire` / `SetNextSecondaryFire` — 设定下次可开火时间
- `ItemPostFrame` / `ItemBusyFrame` — **每帧驱动入口**（按钮判断+调度攻击）
- `OnRemove` / `OwnerChanged` — 移除/换主人
- `GetOwner` — 取持有者
- `DrawHUD`/`DrawWeaponSelection`/`PreDrawViewModel`/`PostDrawViewModel` — 客户端绘制
- `CalcView`/`GetViewModelPosition`/`TranslateFOV` — 客户端视角
- `SetupDataTables` — 网络数据表
- `Equip`/`OnDrop`/`OnReloaded`/`OnRestore` — 生命周期

### 每帧驱动逻辑（关键）

GMod 的 `CBaseCombatWeapon::ItemPostFrame`（玩家 PostThink 每帧调用活动武器）：
1. 读持有者按钮：`pOwner->m_nButtons & IN_ATTACK`（1）、`IN_ATTACK2`（2048）、`IN_RELOAD`（8192）。
2. `weapon_base`（Lua）的 `ItemPostFrame` 里对**主攻击**：`if bit.band(buttons,1)~=0 then` → 若 `Primary.Automatic` 且 `m_flNextPrimaryAttack<=now` → 调 `self:PrimaryAttack()`；若非自动且按钮按下沿（`m_afButtonPressed`）且到时间 → 调 `PrimaryAttack`。
3. **副攻击**同理用 `IN_ATTACK2`(2048) + `Secondary.Automatic` → `self:SecondaryAttack()`。
4. `IN_RELOAD`(8192) 触发 `Reload`（仅用弹夹且非换弹中）。

## 4. `SWEP.Base = "weapon_base"` 的解析

- `Base` 默认是 `"weapon_base"`，意为继承 `lua/weapons/weapon_base` 下的脚本（GMod 内置提供）。
- 引擎在加载 SWEP 时走 `weapon.get(classname)` → 找 `Base` → 递归 `table.inherit` 把基类字段/方法铺进 SWEP 表（仅在子类缺省键时铺入）。

## 5. 对 hl2sb 移植的要点

hl2sb 的 `CBaseCombatWeapon::ItemPostFrame`（`basecombatweapon_shared.cpp`）已有按钮判断 + 调 `PrimaryAttack()`/`SecondaryAttack()`，但 `CHL2MPScriptedWeapon::ItemPostFrame` 会**优先调用 Lua 的 `ItemPostFrame`**，若 Lua 返回 false 则 `RETURN_LUA_NONE()` 提前 return，旁路掉 C++ 自动开火——所以 hl2sb 里"每帧自动开火"实际由 **Lua `weapon_base:ItemPostFrame` 的 `bit.band(buttons, 1/2048/8192)` 实现**。
兼容 GMod SWEP 必须保证：`weapon_base` 被注册、`weapon.get` 把 base 铺进 SWEP、`m_flNextPrimaryAttack/m_iClip1/self.Primary.Automatic` 已正确初始化。
