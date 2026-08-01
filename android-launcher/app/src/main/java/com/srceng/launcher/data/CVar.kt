package com.srceng.launcher.data

import com.squareup.moshi.JsonClass

/**
 * CVar 数据类，对应 Source Engine 的控制台变量
 */
@JsonClass(generateAdapter = true)
data class CVar(
    val name: String,
    val displayName: String,
    val description: String = "",
    val category: CVarCategory = CVarCategory.GENERAL,
    val type: CVarType,
    val defaultValue: String,
    val currentValue: String = defaultValue,
    val allowedValues: List<String>? = null,
    val minValue: Float? = null,
    val maxValue: Float? = null,
    val requiresCheats: Boolean = false,
    val helpUrl: String? = null
) {
    val isModified: Boolean get() = currentValue != defaultValue

    fun withValue(newValue: String): CVar = copy(currentValue = newValue)
    fun resetToDefault(): CVar = copy(currentValue = defaultValue)
}

enum class CVarCategory(val displayName: String) {
    GENERAL("通用"),
    GRAPHICS("图形"),
    NETWORK("网络"),
    PERFORMANCE("性能"),
    CHEATS("作弊")
}

enum class CVarType {
    BOOLEAN,    // 0/1 switch
    INTEGER,    // integer number
    FLOAT,      // float number
    STRING,     // arbitrary string
    ENUM        // from allowedValues list
}

/**
 * 预设的 CVar 列表
 */
object PredefinedCVars {
    val all: List<CVar> by lazy {
        listOf(
            // === 通用 ===
            CVar(
                name = "sv_cheats",
                displayName = "允许作弊",
                description = "允许使用作弊指令。修改后需重新加载地图生效。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "developer",
                displayName = "开发者模式",
                description = "启用调试输出与更多控制台信息。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "con_enable",
                displayName = "启用控制台",
                description = "允许通过 ~ 键打开控制台。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "fps_max",
                displayName = "最大帧率",
                description = "限制最大渲染帧率。0 = 不限制。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.INTEGER,
                defaultValue = "0",
                minValue = 0f,
                maxValue = 1000f
            ),

            // === 图形 ===
            CVar(
                name = "mat_fullbright",
                displayName = "全亮度渲染",
                description = "跳过所有光照计算，常用于调试地图。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "mat_wireframe",
                displayName = "线框模式",
                description = "以线框方式渲染所有几何体。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "r_drawdetailprops",
                displayName = "绘制细节道具",
                description = "绘制地图中细小的装饰性道具（如石块、杂草）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_shadows",
                displayName = "渲染阴影",
                description = "启用阴影渲染。关闭可提升性能。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_propsmaxdist",
                displayName = "道具最大距离",
                description = "道具（Props）的最大可见距离。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.INTEGER,
                defaultValue = "1500",
                minValue = 100f,
                maxValue = 5000f
            ),
            CVar(
                name = "mat_picmip",
                displayName = "纹理分辨率",
                description = "纹理降采样等级。0 = 最高，2 = 低，3 = 最低。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "1", "2", "3")
            ),
            CVar(
                name = "r_eyes",
                displayName = "绘制模型眼睛",
                description = "渲染 NPC/玩家模型的眼睛。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_flex",
                displayName = "面部表情",
                description = "启用模型面部表情（flex）动画。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),

            // === 性能 ===
            CVar(
                name = "r_threaded_renderables",
                displayName = "多线程渲染",
                description = "在独立线程中处理可渲染对象。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_threaded_particles",
                displayName = "多线程粒子",
                description = "在独立线程中更新粒子系统。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "host_thread_mode",
                displayName = "多线程模式",
                description = "0 = 单线程, 1 = 多线程, 2 = 超线程（实验性）。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.ENUM,
                defaultValue = "1",
                allowedValues = listOf("0", "1", "2")
            ),
            CVar(
                name = "mem_max_heapsize",
                displayName = "最大堆内存 (MB)",
                description = "引擎可使用的最大内存。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.INTEGER,
                defaultValue = "1024",
                minValue = 256f,
                maxValue = 8192f
            ),
            CVar(
                name = "snd_mixahead",
                displayName = "音频缓冲",
                description = "音频预读缓冲大小（秒），更大更稳定但延迟高。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.FLOAT,
                defaultValue = "0.1",
                minValue = 0.01f,
                maxValue = 1.0f
            ),

            // === 网络 ===
            CVar(
                name = "rate",
                displayName = "网络速率",
                description = "客户端最大网络带宽 (bytes/sec)。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "80000",
                minValue = 10000f,
                maxValue = 2000000f
            ),
            CVar(
                name = "cl_cmdrate",
                displayName = "指令发送率",
                description = "每秒发送给服务器的指令包数量。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "66",
                minValue = 10f,
                maxValue = 300f
            ),
            CVar(
                name = "cl_updaterate",
                displayName = "更新频率",
                description = "每秒从服务器请求的更新包数量。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "66",
                minValue = 10f,
                maxValue = 300f
            ),
            CVar(
                name = "cl_interp",
                displayName = "插值比例",
                description = "用于减少网络抖动的时间插值。",
                category = CVarCategory.NETWORK,
                type = CVarType.FLOAT,
                defaultValue = "0.1",
                minValue = 0f,
                maxValue = 0.5f
            ),
            CVar(
                name = "sv_lan",
                displayName = "局域网模式",
                description = "只允许局域网连接。",
                category = CVarCategory.NETWORK,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "sv_password",
                displayName = "服务器密码",
                description = "加入服务器所需的密码，留空表示无密码。",
                category = CVarCategory.NETWORK,
                type = CVarType.STRING,
                defaultValue = ""
            ),

            // === 作弊类 (需要 sv_cheats 1) ===
            CVar(
                name = "god",
                displayName = "无敌模式",
                description = "玩家不会受到伤害。",
                category = CVarCategory.CHEATS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "noclip",
                displayName = "穿墙模式",
                description = "玩家可飞行并穿墙。",
                category = CVarCategory.CHEATS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "notarget",
                displayName = "隐身模式",
                description = "NPC 不会把玩家当作目标。",
                category = CVarCategory.CHEATS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "ai_disabled",
                displayName = "禁用 AI",
                description = "停止所有 NPC 的 AI 活动。",
                category = CVarCategory.CHEATS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "impulse",
                displayName = "脉冲指令",
                description = "调试/作弊脉冲指令，常用 101 = 所有武器。",
                category = CVarCategory.CHEATS,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "101", "82", "102", "103", "195", "200"),
                requiresCheats = true
            ),
            CVar(
                name = "thirdperson",
                displayName = "第三人称",
                description = "切换第三人称视角。",
                category = CVarCategory.CHEATS,
                type = CVarType.BOOLEAN,
                defaultValue = "0",
                requiresCheats = true
            ),
            CVar(
                name = "host_timescale",
                displayName = "时间倍速",
                description = "游戏时间倍速。1 = 正常, 0.5 = 慢动作, 2 = 快进。",
                category = CVarCategory.CHEATS,
                type = CVarType.FLOAT,
                defaultValue = "1.0",
                minValue = 0.1f,
                maxValue = 10f,
                requiresCheats = true
            )
        )
    }
}
