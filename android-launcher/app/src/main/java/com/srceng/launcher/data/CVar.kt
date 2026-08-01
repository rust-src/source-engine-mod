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

            // ===== 新增: 常用 HL2 图形 CVar =====
            CVar(
                name = "mat_bloomscale",
                displayName = "Bloom 强度",
                description = "HDR 泛光强度缩放。1 = 默认，0 = 关闭。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.FLOAT,
                defaultValue = "1.0",
                minValue = 0f,
                maxValue = 5f
            ),
            CVar(
                name = "mat_motion_blur_enabled",
                displayName = "动态模糊",
                description = "启用摄像机运动造成的动态模糊。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_parallaxmap",
                displayName = "视差贴图",
                description = "使用视差映射（Relief mapping）模拟凹凸深度。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_specular",
                displayName = "高光渲染",
                description = "渲染材质高光（镜面）效果。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_normalmaps",
                displayName = "法线贴图",
                description = "使用法线贴图模拟表面光照细节。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_hdr_enabled",
                displayName = "HDR 高动态范围",
                description = "启用 HDR 渲染（如地图支持）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_hdr_level",
                displayName = "HDR 曝光等级",
                description = "HDR 曝光/色调映射等级。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.ENUM,
                defaultValue = "2",
                allowedValues = listOf("0", "1", "2", "3")
            ),
            CVar(
                name = "mat_antialias",
                displayName = "全屏抗锯齿 (MSAA)",
                description = "全屏多重采样抗锯齿倍数。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "1", "2", "4", "6", "8")
            ),
            CVar(
                name = "mat_forceaniso",
                displayName = "各向异性过滤",
                description = "强制 AF 倍数 (0-16)。0 = 自动。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "2", "4", "8", "16")
            ),
            CVar(
                name = "mat_picmip",
                displayName = "纹理降采样 (Picmip)",
                description = "纹理降采样等级，0 最高，3 最低。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "1", "2", "3")
            ),
            CVar(
                name = "mat_mipmaptextures",
                displayName = "Mipmap 纹理",
                description = "对纹理使用 Mipmap，远处纹理更平滑。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_compressedtextures",
                displayName = "压缩纹理",
                description = "使用 GPU 压缩格式节省显存（S3TC/DXT/ETC）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_3dsky",
                displayName = "3D 天空盒",
                description = "启用 3D 天空盒渲染。关闭可能会显示 2D 替代。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_farz",
                displayName = "远景裁剪距离",
                description = "距离超过该值的物体会被裁剪 (需要 sv_cheats 1)。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.INTEGER,
                defaultValue = "-1",
                minValue = -1f,
                maxValue = 10000f,
                requiresCheats = true
            ),
            CVar(
                name = "r_lod",
                displayName = "模型 LOD 偏移",
                description = "模型细节等级偏移。0 = 默认，-2/更高 = 更高质量，+数字 = 更低质量更流畅。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.INTEGER,
                defaultValue = "0",
                minValue = -5f,
                maxValue = 10f
            ),
            CVar(
                name = "r_drawmodeldecals",
                displayName = "模型 Decals",
                description = "在模型上绘制贴花（弹孔、血渍等）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_dynamic",
                displayName = "动态光照",
                description = "启用动态（即时）光照。关闭会显著影响画面但提升性能。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_dispatchrefractor",
                displayName = "折射效果",
                description = "渲染折射/半透明材质（如水、玻璃）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_waterforceexpensive",
                displayName = "高质量水面",
                description = "启用高质量水面反射/折射。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_WaterDrawReflection",
                displayName = "水面倒影",
                description = "水面绘制真实反射（性能开销高）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_WaterDrawRefraction",
                displayName = "水面折射",
                description = "水面上进行折射渲染。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_rainradius",
                displayName = "雨效半径",
                description = "雨粒子渲染半径，调小可提升雨天性能。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.INTEGER,
                defaultValue = "1000",
                minValue = 0f,
                maxValue = 3000f
            ),
            CVar(
                name = "mat_bufferprimitives",
                displayName = "缓冲几何体 (VBO)",
                description = "使用 GPU VBO 缓冲顶点数据，提升渲染性能。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "mat_viewportscale",
                displayName = "渲染分辨率缩放",
                description = "将 3D 场景缩放到 <1.0 的分辨率再放大渲染，显著提升 FPS (0.5 = 一半)。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.FLOAT,
                defaultValue = "1.0",
                minValue = 0.25f,
                maxValue = 1.0f
            ),

            // ===== 新增: 常用性能 CVar =====
            CVar(
                name = "ai_optimizations",
                displayName = "AI 优化",
                description = "启用 AI 性能优化（跳过远距离 NPC think）。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_threaded_renderables",
                displayName = "多线程渲染列表",
                description = "在非主线程构建渲染列表（主要性能 CVar）。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_threaded_particles",
                displayName = "多线程粒子",
                description = "独立线程更新粒子系统。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "g_ragdollmaxanimate",
                displayName = "布娃娃最大动画数",
                description = "同一时刻可动画的布娃娃最大数量，调低可减少 CPU 占用。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.INTEGER,
                defaultValue = "8",
                minValue = 0f,
                maxValue = 64f
            ),
            CVar(
                name = "g_ragdollmaxfade",
                displayName = "布娃娃最大可见数",
                description = "同时可见的布娃娃数量上限，超过会淡出最早的。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.INTEGER,
                defaultValue = "8",
                minValue = 0f,
                maxValue = 64f
            ),
            CVar(
                name = "fov_desired",
                displayName = "玩家 FOV (度)",
                description = "第一人称视野角度，HL2 默认 75，常见 90-110。",
                category = CVarCategory.GENERAL,
                type = CVarType.INTEGER,
                defaultValue = "75",
                minValue = 50f,
                maxValue = 130f
            ),
            CVar(
                name = "viewmodel_fov",
                displayName = "武器视角 FOV",
                description = "手上武器模型的 FOV，调整手臂远近。",
                category = CVarCategory.GENERAL,
                type = CVarType.INTEGER,
                defaultValue = "54",
                minValue = 30f,
                maxValue = 120f
            ),
            CVar(
                name = "cl_showfps",
                displayName = "显示 FPS",
                description = "在屏幕角落显示 FPS 计数。",
                category = CVarCategory.GENERAL,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "1", "2")
            ),
            CVar(
                name = "cl_showpos",
                displayName = "显示位置/速度",
                description = "显示玩家的坐标与移动速度。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "net_graph",
                displayName = "显示网络图表",
                description = "网络调试图表：ping / loss / choke / tick。",
                category = CVarCategory.NETWORK,
                type = CVarType.ENUM,
                defaultValue = "0",
                allowedValues = listOf("0", "1", "2", "3", "4")
            ),
            CVar(
                name = "net_graphpos",
                displayName = "网络图表位置",
                description = "1 = 左下，2 = 中下，3 = 右下。",
                category = CVarCategory.NETWORK,
                type = CVarType.ENUM,
                defaultValue = "3",
                allowedValues = listOf("1", "2", "3")
            ),
            CVar(
                name = "cl_ejectbrass",
                displayName = "弹壳抛出效果",
                description = "射击时显示抛出的弹壳。关闭可减轻 CPU 负担。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "snd_ducktovolume",
                displayName = "语音衰减量",
                description = "玩家说话时自动压低其他音效的比例。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.FLOAT,
                defaultValue = "0.5",
                minValue = 0f,
                maxValue = 1f
            ),
            CVar(
                name = "snd_mixahead",
                displayName = "音频预读缓冲 (秒)",
                description = "越大越稳定但延迟越高。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.FLOAT,
                defaultValue = "0.1",
                minValue = 0.01f,
                maxValue = 1.0f
            ),
            CVar(
                name = "snd_noextraupdate",
                displayName = "关闭额外音频更新",
                description = "关闭可能不必要的额外音频线程更新。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "r_PhysPropStaticLighting",
                displayName = "物理体静态光照",
                description = "对物理对象启用预计算静态光照贴图。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_drawflecks",
                displayName = "碎片粒子 (Flecks)",
                description = "显示受击时碎屑、火花等细小粒子。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_drawparticles",
                displayName = "粒子系统",
                description = "绘制特效粒子（爆炸、烟雾、水花等）。关闭后画面干净但缺失特效。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_drawtrans",
                displayName = "半透明表面",
                description = "绘制半透明/可穿透面。关闭可能提升低端设备性能。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_drawentities",
                displayName = "绘制实体",
                description = "绘制所有非世界几何实体（NPC、物品、道具）。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1",
                requiresCheats = true
            ),
            CVar(
                name = "r_drawviewmodel",
                displayName = "显示手中武器",
                description = "绘制武器/手臂 viewmodel。",
                category = CVarCategory.GRAPHICS,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "r_dopixelvisibility",
                displayName = "像素可见性剔除",
                description = "以屏幕像素尺寸做可见性裁剪，优化性能。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "cl_forcepreload",
                displayName = "强制地图资源预加载",
                description = "进入新地图时预加载所有资源，避免加载抖动。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "cl_updaterate",
                displayName = "更新频率 (updaterate)",
                description = "每秒从服务器接收的快照数。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "66",
                minValue = 10f,
                maxValue = 300f
            ),
            CVar(
                name = "cl_cmdrate",
                displayName = "指令发送率 (cmdrate)",
                description = "每秒向服务器发送的用户指令数。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "66",
                minValue = 10f,
                maxValue = 300f
            ),
            CVar(
                name = "rate",
                displayName = "网络速率 (bytes/s)",
                description = "客户端最大网络带宽。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "80000",
                minValue = 10000f,
                maxValue = 2000000f
            ),
            CVar(
                name = "cl_interp",
                displayName = "客户端插值 (秒)",
                description = "用于抵消网络抖动的时间插值。一般 = 1/cl_updaterate。",
                category = CVarCategory.NETWORK,
                type = CVarType.FLOAT,
                defaultValue = "0.1",
                minValue = 0f,
                maxValue = 0.5f
            ),
            CVar(
                name = "cl_interp_ratio",
                displayName = "插值倍率",
                description = "插值时间 = cl_interp_ratio / cl_updaterate。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "2",
                minValue = 1f,
                maxValue = 5f
            ),
            CVar(
                name = "sv_client_cmdrate_difference",
                displayName = "客户端 cmdrate 偏差容限",
                description = "允许 cmdrate 与服务器目标值的最大偏差。",
                category = CVarCategory.NETWORK,
                type = CVarType.INTEGER,
                defaultValue = "30",
                minValue = 0f,
                maxValue = 100f
            ),
            CVar(
                name = "dsp_off",
                displayName = "禁用 DSP 音频效果",
                description = "禁用回声等 DSP 音频后期处理，降低 CPU。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "snd_wave_mixahead",
                displayName = "波形混音预读 (页)",
                description = "音频预读页数，更多更稳但延迟更高。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.INTEGER,
                defaultValue = "8",
                minValue = 2f,
                maxValue = 64f
            ),

            // ===== 新增: 玩家体验 =====
            CVar(
                name = "hud_draw",
                displayName = "显示 HUD",
                description = "整体 HUD（准星、血量、弹药等）。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "hud_quickinfo",
                displayName = "快捷信息栏",
                description = "屏幕右下角的快速目标/提示信息。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "crosshair",
                displayName = "显示准星",
                description = "显示/隐藏武器准星。",
                category = CVarCategory.GENERAL,
                type = CVarType.BOOLEAN,
                defaultValue = "1"
            ),
            CVar(
                name = "vguitest_solidbg",
                displayName = "VGUI 纯色背景",
                description = "加载/UI 面板使用纯色而非背景图，加快加载。",
                category = CVarCategory.PERFORMANCE,
                type = CVarType.BOOLEAN,
                defaultValue = "0"
            ),
            CVar(
                name = "r_screenshotaspectratio",
                displayName = "截图宽高比",
                description = "截图时强制的宽高比 (宽/高)。",
                category = CVarCategory.GENERAL,
                type = CVarType.FLOAT,
                defaultValue = "1.777",
                minValue = 1f,
                maxValue = 2.5f
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
