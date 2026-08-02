package com.srceng.launcher.data

/**
 * Source Engine 常用命令行参数（基于 Valve Developer Community 文档）
 * https://developer.valvesoftware.com/wiki/Command_line_options
 */
data class CommandLineOption(
    val id: String,
    val flag: String,                 // 命令行参数本身，如 -novid
    val displayName: String,          // 中文名
    val category: CmdCategory,
    val description: String,
    val valueHint: CmdValueType = CmdValueType.NONE,
    val defaultValue: String = "",
    val requiresValue: Boolean = false,
    val androidSafe: Boolean = true,  // Android 上适用？
    val defaultEnabled: Boolean = false
) {
    fun format(value: String): String =
        if (valueHint == CmdValueType.NONE || !requiresValue) flag else "$flag $value".trimEnd()
}

enum class CmdCategory(val displayName: String) {
    VIDEO("显示与图形"),
    PERFORMANCE("性能与内存"),
    GAMEPLAY("游戏与启动"),
    NETWORK("网络"),
    DEVELOPER("调试与开发者"),
    WINDOW("窗口模式"),
    INPUT("输入与外设"),
    SERVER("服务端")
}

enum class CmdValueType {
    NONE,       // 纯开关
    INTEGER,    // -heapsize <KB>
    STRING,     // -language <lang>
    ENUM        // 枚举选项
}

object PredefinedCmdOptions {
    val all: List<CommandLineOption> by lazy {
        listOf(
            // ========== 游戏与启动 ==========
            CommandLineOption(
                id = "novid",
                flag = "-novid",
                displayName = "跳过开场视频",
                category = CmdCategory.GAMEPLAY,
                description = "不播放启动时的 Valve/Source 片头动画，更快进入主菜单。",
                defaultEnabled = false,
                androidSafe = true
            ),
            CommandLineOption(
                id = "nojoy",
                flag = "-nojoy",
                displayName = "禁用手柄输入",
                category = CmdCategory.INPUT,
                description = "禁用所有摇杆/手柄支持，仅使用触控或键鼠。"
            ),
            CommandLineOption(
                id = "high",
                flag = "-high",
                displayName = "高优先级进程",
                category = CmdCategory.PERFORMANCE,
                description = "以高优先级运行游戏进程，可能改善卡顿。Android/Linux 上可能无效。"
            ),
            CommandLineOption(
                id = "noaafonts",
                flag = "-noaafonts",
                displayName = "禁用字体抗锯齿",
                category = CmdCategory.VIDEO,
                description = "取消字体抗锯齿，可改善低端设备的渲染性能。"
            ),
            CommandLineOption(
                id = "low",
                flag = "-low",
                displayName = "低优先级进程",
                category = CmdCategory.PERFORMANCE,
                description = "以低优先级运行游戏进程，更多 CPU 资源留给系统。"
            ),
            CommandLineOption(
                id = "multirun",
                flag = "-multirun",
                displayName = "允许多开",
                category = CmdCategory.GAMEPLAY,
                description = "允许同时启动多个 Source 实例（Source 2013 起支持）。"
            ),
            CommandLineOption(
                id = "insecure",
                flag = "-insecure",
                displayName = "关闭 VAC",
                category = CmdCategory.DEVELOPER,
                description = "禁用 Valve Anti-Cheat，单机/局域网用。"
            ),
            CommandLineOption(
                id = "autoconfig",
                flag = "-autoconfig",
                displayName = "自动检测硬件",
                category = CmdCategory.VIDEO,
                description = "根据检测到的硬件自动设置视频/性能参数，忽略 cfg 中的保存值。"
            ),

            // ========== 窗口/显示 ==========
            CommandLineOption(
                id = "noborder",
                flag = "-noborder",
                displayName = "无边框窗口",
                category = CmdCategory.WINDOW,
                description = "窗口模式下移除窗口边框（Borderless Windowed，需配合 -windowed）。"
            ),
            CommandLineOption(
                id = "sw",
                flag = "-sw",
                displayName = "窗口化启动",
                category = CmdCategory.WINDOW,
                description = "同 -windowed，强制窗口模式。"
            ),
            CommandLineOption(
                id = "refresh",
                flag = "-refresh",
                displayName = "强制刷新率 (Hz)",
                category = CmdCategory.VIDEO,
                description = "指定屏幕刷新率（如 60/90/120）。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "60"
            ),
            CommandLineOption(
                id = "w",
                flag = "-w",
                displayName = "分辨率宽度 (px)",
                category = CmdCategory.VIDEO,
                description = "强制分辨率宽度，例如 1920。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true
            ),
            CommandLineOption(
                id = "h",
                flag = "-h",
                displayName = "分辨率高度 (px)",
                category = CmdCategory.VIDEO,
                description = "强制分辨率高度，例如 1080。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true
            ),

            // ========== 图形 ==========
            CommandLineOption(
                id = "mat_vsync",
                flag = "-mat_vsync",
                displayName = "启动时启用 VSync",
                category = CmdCategory.VIDEO,
                description = "强制开启垂直同步（另可用 mat_vsync CVar）。"
            ),
            CommandLineOption(
                id = "forcenovsync",
                flag = "-forcenovsync",
                displayName = "强制关闭 VSync",
                category = CmdCategory.VIDEO,
                description = "无论设置如何都关闭垂直同步，可能减少输入延迟。",
                defaultEnabled = false
            ),
            CommandLineOption(
                id = "mat_antialias",
                flag = "-mat_antialias",
                displayName = "MSAA 倍数",
                category = CmdCategory.VIDEO,
                description = "全屏抗锯齿。支持 0/2/4/8。",
                valueHint = CmdValueType.ENUM,
                requiresValue = true,
                defaultValue = "0"
            ),
            CommandLineOption(
                id = "mat_aaquality",
                flag = "-mat_aaquality",
                displayName = "MSAA 质量",
                category = CmdCategory.VIDEO,
                description = "设置 MSAA 抗锯齿质量等级（0 - 最高）。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true
            ),
            CommandLineOption(
                id = "mat_softwaretl",
                flag = "-mat_softwaretl",
                displayName = "软件顶点处理",
                category = CmdCategory.VIDEO,
                description = "用 CPU 做顶点处理（调试/兼容用，性能较差）。"
            ),
            CommandLineOption(
                id = "g15",
                flag = "-g15",
                displayName = "Logitech G15 支持",
                category = CmdCategory.INPUT,
                description = "启用 Logitech G15 键盘 LCD 支持（仅部分分支）。",
                androidSafe = false
            ),

            // ========== 语言 ==========
            CommandLineOption(
                id = "language",
                flag = "-language",
                displayName = "游戏语言",
                category = CmdCategory.GAMEPLAY,
                description = "强制指定游戏语言：english / schinese / tchinese / german / french / japanese 等。",
                valueHint = CmdValueType.ENUM,
                requiresValue = true,
                defaultValue = "schinese"
            ),
            CommandLineOption(
                id = "all_languages",
                flag = "-all_languages",
                displayName = "加载全部语言",
                category = CmdCategory.GAMEPLAY,
                description = "加载所有可用本地化文件（占用更多内存）。"
            ),

            // ========== 性能/内存 ==========
            CommandLineOption(
                id = "heapsize",
                flag = "-heapsize",
                displayName = "堆内存大小 (KB)",
                category = CmdCategory.PERFORMANCE,
                description = "指定引擎堆内存（KB），如 524288 = 512MB，1048576 = 1GB。老引擎有效。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "524288"
            ),
            CommandLineOption(
                id = "maxmemory",
                flag = "-maxmemory",
                displayName = "最大内存 (MB)",
                category = CmdCategory.PERFORMANCE,
                description = "限制引擎使用的最大系统内存（MB）。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "0"
            ),
            CommandLineOption(
                id = "threads",
                flag = "-threads",
                displayName = "渲染线程数",
                category = CmdCategory.PERFORMANCE,
                description = "强制指定渲染/逻辑线程数。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true
            ),
            CommandLineOption(
                id = "noasync",
                flag = "-noasync",
                displayName = "禁用异步文件系统",
                category = CmdCategory.PERFORMANCE,
                description = "关闭异步 I/O，调试文件加载问题时使用。"
            ),
            CommandLineOption(
                id = "limitvsconst",
                flag = "-limitvsconst",
                displayName = "限制 VS常量数",
                category = CmdCategory.PERFORMANCE,
                description = "把顶点着色器常量限制在 256 个，兼容老 GPU。"
            ),

            // ========== 网络 ==========
            CommandLineOption(
                id = "clientport",
                flag = "-clientport",
                displayName = "客户端端口",
                category = CmdCategory.NETWORK,
                description = "指定客户端 UDP 端口，默认 27002。多开时每个实例需要不同。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "27002"
            ),
            CommandLineOption(
                id = "ip",
                flag = "-ip",
                displayName = "绑定 IP 地址",
                category = CmdCategory.NETWORK,
                description = "绑定到指定网卡/IP（多网卡/主机用）。",
                valueHint = CmdValueType.STRING,
                requiresValue = true
            ),
            CommandLineOption(
                id = "netspike",
                flag = "-netspike",
                displayName = "网络追踪阈值 (KB)",
                category = CmdCategory.NETWORK,
                description = "设置网络 spike 记录阈值，写入 netspike.txt 供调试。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true
            ),

            // ========== 开发者/调试 ==========
            CommandLineOption(
                id = "condebug",
                flag = "-condebug",
                displayName = "控制台写入日志",
                category = CmdCategory.DEVELOPER,
                description = "把所有控制台输出写入 console.log。"
            ),
            CommandLineOption(
                id = "conclearlog",
                flag = "-conclearlog",
                displayName = "清空控制台日志",
                category = CmdCategory.DEVELOPER,
                description = "启动时清空 console.log（需配合 -condebug）。"
            ),
            CommandLineOption(
                id = "flushlog",
                flag = "-flushlog",
                displayName = "日志实时落盘",
                category = CmdCategory.DEVELOPER,
                description = "日志每秒落盘而非每 4KB 一次，便于追踪崩溃前输出。"
            ),
            CommandLineOption(
                id = "allowdebug",
                flag = "-allowdebug",
                displayName = "允许调试器注入",
                category = CmdCategory.DEVELOPER,
                description = "伪造 phonehome 调试状态（等价 -dev，除非指定 -nodev）。"
            ),
            CommandLineOption(
                id = "dev",
                flag = "-dev",
                displayName = "开发者模式",
                category = CmdCategory.DEVELOPER,
                description = "启用 dev 模式：主菜单不加载背景地图、退出无确认、输出更多错误。"
            ),
            CommandLineOption(
                id = "console",
                flag = "-console",
                displayName = "启动显示控制台",
                category = CmdCategory.DEVELOPER,
                description = "启动后直接打开引擎控制台。"
            ),
            CommandLineOption(
                id = "hushasserts",
                flag = "-hushasserts",
                displayName = "静默 asserts",
                category = CmdCategory.DEVELOPER,
                description = "跳过一些核心库的 assert，减少错误弹窗。"
            ),
            CommandLineOption(
                id = "log_opened_files",
                flag = "-log_opened_files",
                displayName = "记录打开的文件",
                category = CmdCategory.DEVELOPER,
                description = "将所有读取的文件记录到 opened_files.txt，方便排查缺文件。"
            ),

            // ========== 服务端 ==========
            CommandLineOption(
                id = "maxplayers",
                flag = "-maxplayers",
                displayName = "最大玩家数",
                category = CmdCategory.SERVER,
                description = "设置监听服务器最大玩家数。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "16"
            ),
            CommandLineOption(
                id = "port",
                flag = "-port",
                displayName = "服务器端口",
                category = CmdCategory.SERVER,
                description = "服务器监听的 UDP 端口，默认 27015。",
                valueHint = CmdValueType.INTEGER,
                requiresValue = true,
                defaultValue = "27015"
            ),
            CommandLineOption(
                id = "strictportbind",
                flag = "-strictportbind",
                displayName = "严格端口绑定",
                category = CmdCategory.SERVER,
                description = "如果 -port 指定的端口不可用则退出，而不是自动递增。"
            ),

            // ========== 其他实用 ==========
            CommandLineOption(
                id = "exit",
                flag = "-exit",
                displayName = "加载完地图自动退出",
                category = CmdCategory.DEVELOPER,
                description = "加载完毕指定地图后立即退出，用于自动化测试/打包。"
            ),
            CommandLineOption(
                id = "buildcubemaps",
                flag = "-buildcubemaps",
                displayName = "自动构建 Cubemap",
                category = CmdCategory.VIDEO,
                description = "进入地图后自动执行 buildcubemaps，然后退出。"
            ),
            CommandLineOption(
                id = "makereslists",
                flag = "-makereslists",
                category = CmdCategory.DEVELOPER,
                displayName = "生成资源列表",
                description = "生成发布用的资源列表（需配合 -textmode）。",
                androidSafe = false
            ),
            CommandLineOption(
                id = "gamepadui",
                flag = "-gamepadui",
                displayName = "启用手柄 UI (Big Picture)",
                category = CmdCategory.INPUT,
                description = "启用 Steam Deck 风格的手柄优化大 UI，同时会强制启用 Vulkan。"
            ),
            CommandLineOption(
                id = "32bit",
                flag = "-32bit",
                displayName = "强制 32 位模式",
                category = CmdCategory.PERFORMANCE,
                description = "在 64 位系统/引擎上强制 32 位执行（一般 Android 不适用）。",
                androidSafe = false
            ),
            CommandLineOption(
                id = "disable_d3d9ex",
                flag = "-disable_d3d9ex",
                displayName = "禁用 Direct3D 9Ex",
                category = CmdCategory.VIDEO,
                description = "强制禁用 D3D9Ex（仅 Windows/D3D 分支，Android 不生效）。",
                androidSafe = false
            )
        )
    }

    /**
     * 按类别分组
     */
    fun byCategory(): Map<CmdCategory, List<CommandLineOption>> =
        all.groupBy { it.category }

    /**
     * 过滤出 Android 安全可用的
     */
    fun androidSafe(): List<CommandLineOption> = all.filter { it.androidSafe }
}
