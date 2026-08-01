package com.srceng.launcher.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.*
import androidx.datastore.preferences.preferencesDataStore
import com.srceng.launcher.ui.theme.ThemeMode
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map
import java.io.IOException

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "launcher_prefs")

/**
 * 默认的命令行快速勾选开关：根据 CmdOption.defaultEnabled 生成
 */
fun defaultQuickFlags(): Map<String, Boolean> =
    PredefinedCmdOptions.androidSafe()
        .filter { it.defaultEnabled }
        .associate { it.id to true }

/**
 * 默认的命令行参数值：对需要数值的参数使用 defaultValue
 */
fun defaultQuickFlagValues(): Map<String, String> =
    PredefinedCmdOptions.androidSafe()
        .filter { it.requiresValue && it.defaultValue.isNotBlank() }
        .associate { it.id to it.defaultValue }

/**
 * 游戏配置数据类
 */
data class LauncherConfig(
    // 外观
    val themeMode: ThemeMode = ThemeMode.SYSTEM,
    val useDynamicColor: Boolean = true,

    // 游戏路径
    val gameDirectory: String = "",
    val selectedMod: String = "hl2",
    val customLaunchArgs: String = "",

    // 图形
    val renderApi: String = "gles3",
    val fullscreen: Boolean = true,
    val vsync: Boolean = true,
    val msaaLevel: Int = 0,

    // 性能
    val threads: Int = -1, // -1 = 自动
    val memoryLimitMb: Int = 0, // 0 = 自动/无限制

    // 音频
    val masterVolume: Float = 1.0f,
    val sfxVolume: Float = 1.0f,
    val musicVolume: Float = 1.0f,

    // 控制
    val showOnscreenControls: Boolean = true,
    val controllerSupport: Boolean = true,

    // 其他
    val debugMode: Boolean = false,
    val showConsole: Boolean = false,

    // 服务端
    val serverMap: String = "d1_trainstation_01",
    val serverMaxPlayers: Int = 16,
    val startListenServer: Boolean = false,

    // ===== 快速勾选的命令行参数 (key = CmdOption.id, value = 勾选状态/自定义值) =====
    val quickFlags: Map<String, Boolean> = defaultQuickFlags(),
    // 对需要数值的参数保存值 (key = CmdOption.id)
    val quickFlagValues: Map<String, String> = defaultQuickFlagValues()
) {
    /**
     * 根据设置生成 +exec cfg 文件内容 (用于在游戏内自动执行)
     *
     * 规则：默认值不写入，只有用户显式修改/启用过的才写入，避免 cfg 被一大堆默认值污染
     *  - 音量: masterVolume / sfxVolume / musicVolume != 1.0f 才写
     *  - VSync: vsync == false 才写 (引擎默认开)
     *  - 线程数: threads > 0 才写
     *  - 内存限制: memoryLimitMb > 0 才写
     *  - CVar: isModified == true 才写
     */
    fun toAutoExecCfg(cvars: List<CVar>): String {
        val lines = mutableListOf<String>()
        lines.add("// 由 Source Engine Launcher 自动生成 - 请勿手动编辑")
        lines.add("// 生成于: ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date())}")
        lines.add("// 本文件仅包含用户自定义项，默认值不会写入。")
        lines.add("")

        val body = mutableListOf<String>()

        // ===== 音量：默认 1.0 不写 =====
        if (masterVolume != 1.0f) body += "volume ${fmtFloat(masterVolume)}"
        if (sfxVolume != 1.0f) body += "snd_musicvolume ${fmtFloat(sfxVolume)}"   // SFX 对应 snd_volume? 这里保留与原逻辑一致
        if (musicVolume != 1.0f) body += "snd_musicvolume ${fmtFloat(musicVolume)}"

        // ===== VSync：引擎默认开启，仅当用户关闭时写入 =====
        if (!vsync) {
            body += "mat_vsync 0"
            body += "r_shadows 1"   // r_shadows 与 vsync 不再强绑定；保留默认值不写的原则，这里只在 vsync 关时额外保证 1 还是不写？
            // 由于 r_shadows 的默认值不是由 vsync 决定，默认不动它；只有 mat_vsync 覆盖
        }

        // ===== 线程 / 内存：只有显式设置才写 =====
        if (threads > 0) body += "threads $threads"
        if (memoryLimitMb > 0) body += "mem_max_heapsize $memoryLimitMb"

        // ===== CVar：只有 isModified 的才写 =====
        cvars.filter { it.isModified }.forEach { cvar ->
            body += "${cvar.name} ${cvar.currentValue}"
        }

        if (body.isEmpty()) {
            lines.add("// (无用户自定义项)")
        } else {
            lines.addAll(body)
        }
        lines.add("")
        return lines.joinToString("\n")
    }

    private fun fmtFloat(v: Float): String {
        // 去掉无意义的 0 尾巴，如 0.50000 -> 0.5、1.0 -> 1
        val s = "%.6f".format(v)
        return s.trimEnd('0').trimEnd('.')
    }

    /**
     * 生成命令行启动参数
     *
     * 规则：默认值（如 fullscreen=true、renderApi=gles3、未勾选的 quickFlag）不写入，避免参数过长
     * 仅当用户显式修改/启用时才写入；-game 和自定义参数始终保留
     */
    fun toLaunchArgs(): String {
        val args = mutableListOf<String>()

        // 核心：-game 始终写
        args.add("-game $selectedMod")

        // 游戏根目录：只有用户配置了才写
        if (gameDirectory.isNotEmpty()) {
            args.add("-basedir \"$gameDirectory\"")
        }

        // 窗口模式：fullscreen=true 是默认，只在非默认时写
        if (!fullscreen) args.add("-windowed")

        // 启动监听服务器：只有勾选时写
        if (startListenServer) {
            args.add("+map $serverMap +maxplayers $serverMaxPlayers")
        }

        // 开发者模式：只有开启时写
        if (debugMode) args.add("-dev -condebug")
        if (showConsole) args.add("-console")

        // 渲染 API：gles3 是默认值，不写入；非默认才写
        when (renderApi) {
            "gles2" -> args.add("-gles2")
            "vulkan" -> args.add("-vulkan")
            // gles3 默认 → 不写
        }

        // ===== 快速勾选：只在用户显式勾选 (quickFlags[id]=true) 时写；值为空的必填参数跳过 =====
        PredefinedCmdOptions.androidSafe().forEach { opt ->
            val enabled = quickFlags[opt.id]
            if (enabled != true) return@forEach   // 未显式启用 → 不写（包含 defaultEnabled=false 的全部）
            val value = if (opt.requiresValue) {
                val userSet = quickFlagValues[opt.id]
                // 用户没设置值：仅当 defaultValue 非空时用 defaultValue，否则跳过此参数
                if (userSet.isNullOrBlank()) {
                    if (opt.defaultValue.isBlank()) return@forEach
                    opt.defaultValue
                } else userSet
            } else ""
            if (opt.requiresValue && value.isBlank()) return@forEach
            args.add(opt.format(value))
        }

        // 自定义参数：用户填了才写
        if (customLaunchArgs.isNotBlank()) {
            args.add(customLaunchArgs.trim())
        }

        return args.joinToString(" ")
    }
}

/**
 * 偏好设置管理器
 */
class LauncherPreferences(private val context: Context) {

    private object Keys {
        val THEME_MODE = stringPreferencesKey("theme_mode")
        val DYNAMIC_COLOR = booleanPreferencesKey("dynamic_color")
        val GAME_DIR = stringPreferencesKey("game_dir")
        val SELECTED_MOD = stringPreferencesKey("selected_mod")
        val CUSTOM_ARGS = stringPreferencesKey("custom_args")
        val RENDER_API = stringPreferencesKey("render_api")
        val FULLSCREEN = booleanPreferencesKey("fullscreen")
        val VSYNC = booleanPreferencesKey("vsync")
        val MSAA = intPreferencesKey("msaa")
        val THREADS = intPreferencesKey("threads")
        val MEMORY = intPreferencesKey("memory_mb")
        val MASTER_VOL = floatPreferencesKey("master_vol")
        val SFX_VOL = floatPreferencesKey("sfx_vol")
        val MUSIC_VOL = floatPreferencesKey("music_vol")
        val TOUCH_CTL = booleanPreferencesKey("touch_controls")
        val CTRLR = booleanPreferencesKey("controller")
        val DEBUG = booleanPreferencesKey("debug")
        val CONSOLE = booleanPreferencesKey("console")
        val SERVER_MAP = stringPreferencesKey("server_map")
        val SERVER_MAXPLAYERS = intPreferencesKey("server_maxplayers")
        val SERVER_START = booleanPreferencesKey("start_server")
        val QUICK_FLAGS_JSON = stringPreferencesKey("quick_flags_json")
        val QUICK_FLAG_VALUES_JSON = stringPreferencesKey("quick_flag_values_json")
    }

    val configFlow: Flow<LauncherConfig> = context.dataStore.data
        .catch { exception ->
            if (exception is IOException) emit(emptyPreferences()) else throw exception
        }
        .map { prefs ->
            LauncherConfig(
                themeMode = prefs[Keys.THEME_MODE]?.let(ThemeMode::valueOf) ?: ThemeMode.SYSTEM,
                useDynamicColor = prefs[Keys.DYNAMIC_COLOR] ?: true,
                gameDirectory = prefs[Keys.GAME_DIR] ?: "",
                selectedMod = prefs[Keys.SELECTED_MOD] ?: "hl2",
                customLaunchArgs = prefs[Keys.CUSTOM_ARGS] ?: "",
                renderApi = prefs[Keys.RENDER_API] ?: "gles3",
                fullscreen = prefs[Keys.FULLSCREEN] ?: true,
                vsync = prefs[Keys.VSYNC] ?: true,
                msaaLevel = prefs[Keys.MSAA] ?: 0,
                threads = prefs[Keys.THREADS] ?: -1,
                memoryLimitMb = prefs[Keys.MEMORY] ?: 0,
                masterVolume = prefs[Keys.MASTER_VOL] ?: 1f,
                sfxVolume = prefs[Keys.SFX_VOL] ?: 1f,
                musicVolume = prefs[Keys.MUSIC_VOL] ?: 1f,
                showOnscreenControls = prefs[Keys.TOUCH_CTL] ?: true,
                controllerSupport = prefs[Keys.CTRLR] ?: true,
                debugMode = prefs[Keys.DEBUG] ?: false,
                showConsole = prefs[Keys.CONSOLE] ?: false,
                serverMap = prefs[Keys.SERVER_MAP] ?: "d1_trainstation_01",
                serverMaxPlayers = prefs[Keys.SERVER_MAXPLAYERS] ?: 16,
                startListenServer = prefs[Keys.SERVER_START] ?: false,
                quickFlags = prefs[Keys.QUICK_FLAGS_JSON]?.parseFlagMap() ?: defaultQuickFlags(),
                quickFlagValues = prefs[Keys.QUICK_FLAG_VALUES_JSON]?.parseValueMap() ?: defaultQuickFlagValues()
            )
        }

    suspend fun update(transform: suspend (LauncherConfig) -> LauncherConfig) {
        context.dataStore.edit { prefs ->
            val current = prefs.toConfig()
            val next = transform(current)
            prefs[Keys.THEME_MODE] = next.themeMode.name
            prefs[Keys.DYNAMIC_COLOR] = next.useDynamicColor
            prefs[Keys.GAME_DIR] = next.gameDirectory
            prefs[Keys.SELECTED_MOD] = next.selectedMod
            prefs[Keys.CUSTOM_ARGS] = next.customLaunchArgs
            prefs[Keys.RENDER_API] = next.renderApi
            prefs[Keys.FULLSCREEN] = next.fullscreen
            prefs[Keys.VSYNC] = next.vsync
            prefs[Keys.MSAA] = next.msaaLevel
            prefs[Keys.THREADS] = next.threads
            prefs[Keys.MEMORY] = next.memoryLimitMb
            prefs[Keys.MASTER_VOL] = next.masterVolume
            prefs[Keys.SFX_VOL] = next.sfxVolume
            prefs[Keys.MUSIC_VOL] = next.musicVolume
            prefs[Keys.TOUCH_CTL] = next.showOnscreenControls
            prefs[Keys.CTRLR] = next.controllerSupport
            prefs[Keys.DEBUG] = next.debugMode
            prefs[Keys.CONSOLE] = next.showConsole
            prefs[Keys.SERVER_MAP] = next.serverMap
            prefs[Keys.SERVER_MAXPLAYERS] = next.serverMaxPlayers
            prefs[Keys.SERVER_START] = next.startListenServer
            prefs[Keys.QUICK_FLAGS_JSON] = next.quickFlags.flagsToJsonString()
            prefs[Keys.QUICK_FLAG_VALUES_JSON] = next.quickFlagValues.valuesToJsonString()
        }
    }

    private fun Preferences.toConfig(): LauncherConfig = LauncherConfig(
        themeMode = this[Keys.THEME_MODE]?.let(ThemeMode::valueOf) ?: ThemeMode.SYSTEM,
        useDynamicColor = this[Keys.DYNAMIC_COLOR] ?: true,
        gameDirectory = this[Keys.GAME_DIR] ?: "",
        selectedMod = this[Keys.SELECTED_MOD] ?: "hl2",
        customLaunchArgs = this[Keys.CUSTOM_ARGS] ?: "",
        renderApi = this[Keys.RENDER_API] ?: "gles3",
        fullscreen = this[Keys.FULLSCREEN] ?: true,
        vsync = this[Keys.VSYNC] ?: true,
        msaaLevel = this[Keys.MSAA] ?: 0,
        threads = this[Keys.THREADS] ?: -1,
        memoryLimitMb = this[Keys.MEMORY] ?: 0,
        masterVolume = this[Keys.MASTER_VOL] ?: 1f,
        sfxVolume = this[Keys.SFX_VOL] ?: 1f,
        musicVolume = this[Keys.MUSIC_VOL] ?: 1f,
        showOnscreenControls = this[Keys.TOUCH_CTL] ?: true,
        controllerSupport = this[Keys.CTRLR] ?: true,
        debugMode = this[Keys.DEBUG] ?: false,
        showConsole = this[Keys.CONSOLE] ?: false,
        serverMap = this[Keys.SERVER_MAP] ?: "d1_trainstation_01",
        serverMaxPlayers = this[Keys.SERVER_MAXPLAYERS] ?: 16,
        startListenServer = this[Keys.SERVER_START] ?: false,
        quickFlags = this[Keys.QUICK_FLAGS_JSON]?.parseFlagMap() ?: defaultQuickFlags(),
        quickFlagValues = this[Keys.QUICK_FLAG_VALUES_JSON]?.parseValueMap() ?: defaultQuickFlagValues()
    )
}

// ====== 简易 Map<String, T> 的 JSON 序列化（不依赖 Moshi，避免反射成本） ======

private fun Map<String, Boolean>.flagsToJsonString(): String {
    if (isEmpty()) return "{}"
    val sb = StringBuilder("{")
    var first = true
    for ((k, v) in this) {
        if (!first) sb.append(",")
        first = false
        sb.append("\"").append(k.jsonEscape()).append("\":").append(if (v) "true" else "false")
    }
    sb.append("}")
    return sb.toString()
}

private fun Map<String, String>.valuesToJsonString(): String {
    if (isEmpty()) return "{}"
    val sb = StringBuilder("{")
    var first = true
    for ((k, v) in this) {
        if (!first) sb.append(",")
        first = false
        sb.append("\"").append(k.jsonEscape()).append("\":\"").append(v.jsonEscape()).append("\"")
    }
    sb.append("}")
    return sb.toString()
}

private fun String.jsonEscape(): String =
    replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r")

private fun String.parseFlagMap(): Map<String, Boolean> {
    val m = mutableMapOf<String, Boolean>()
    val s = this.trim()
    if (s.length < 2 || !s.startsWith("{") || !s.endsWith("}")) return m
    val inner = s.substring(1, s.length - 1).trim()
    if (inner.isEmpty()) return m
    splitTopLevel(inner, ',').forEach { pair ->
        val (k, v) = splitTopLevel(pair, ':').let { parts ->
            if (parts.size < 2) return@forEach
            parts[0].trim().unquote() to parts[1].trim()
        }
        m[k] = v.equals("true", ignoreCase = true)
    }
    return m
}

private fun String.parseValueMap(): Map<String, String> {
    val m = mutableMapOf<String, String>()
    val s = this.trim()
    if (s.length < 2 || !s.startsWith("{") || !s.endsWith("}")) return m
    val inner = s.substring(1, s.length - 1).trim()
    if (inner.isEmpty()) return m
    splitTopLevel(inner, ',').forEach { pair ->
        val (k, v) = splitTopLevel(pair, ':').let { parts ->
            if (parts.size < 2) return@forEach
            parts[0].trim().unquote() to parts[1].trim().unquote()
        }
        m[k] = v
    }
    return m
}

private fun splitTopLevel(s: String, delim: Char): List<String> {
    val out = mutableListOf<String>()
    var inStr = false
    var esc = false
    var depth = 0
    var start = 0
    for (i in s.indices) {
        val c = s[i]
        if (esc) { esc = false; continue }
        when (c) {
            '\\' -> { esc = true }
            '"' -> inStr = !inStr
            '{', '[' -> if (!inStr) depth++
            '}', ']' -> if (!inStr) depth--
            delim -> if (!inStr && depth == 0) {
                out.add(s.substring(start, i))
                start = i + 1
            }
        }
    }
    if (start <= s.length) out.add(s.substring(start))
    return out
}

private fun String.unquote(): String =
    if (startsWith("\"") && endsWith("\"") && length >= 2) {
        substring(1, length - 1)
            .replace("\\\"", "\"")
            .replace("\\\\", "\\")
            .replace("\\n", "\n")
            .replace("\\r", "\r")
    } else this

