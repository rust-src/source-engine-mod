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
     */
    fun toAutoExecCfg(cvars: List<CVar>): String {
        val lines = mutableListOf<String>()
        lines.add("// 由 Source Engine Launcher 自动生成 - 请勿手动编辑")
        lines.add("// 生成于: ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date())}")
        lines.add("")

        // 音量
        lines.add("volume $masterVolume")
        lines.add("snd_musicvolume $musicVolume")
        lines.add("")

        // 图形
        lines.add("r_shadows ${if (vsync) "1" else "0"}")
        lines.add("mat_vsync ${if (vsync) "1" else "0"}")
        lines.add("")

        // 线程 / 内存
        if (threads > 0) {
            lines.add("threads $threads")
        }
        if (memoryLimitMb > 0) {
            lines.add("mem_max_heapsize $memoryLimitMb")
        }
        lines.add("")

        // 所有修改过的 CVars
        cvars.filter { it.isModified }.forEach { cvar ->
            lines.add("${cvar.name} ${cvar.currentValue}")
        }

        lines.add("")
        return lines.joinToString("\n")
    }

    /**
     * 生成命令行启动参数
     */
    fun toLaunchArgs(): String {
        val args = mutableListOf<String>()

        args.add("-game $selectedMod")

        if (gameDirectory.isNotEmpty()) {
            args.add("-basedir \"$gameDirectory\"")
        }

        if (!fullscreen) {
            args.add("-windowed")
        } else {
            args.add("-fullscreen")
        }

        if (startListenServer) {
            args.add("+map $serverMap +maxplayers $serverMaxPlayers")
        }

        if (debugMode) {
            args.add("-dev -condebug")
        }

        if (showConsole) {
            args.add("-console")
        }

        when (renderApi) {
            "gles2" -> args.add("-gles2")
            "gles3" -> args.add("-gles3")
            "vulkan" -> args.add("-vulkan")
        }

        // ===== 快速勾选的常用命令行参数 =====
        PredefinedCmdOptions.androidSafe().forEach { opt ->
            val enabled = quickFlags[opt.id] ?: opt.defaultEnabled
            if (!enabled) return@forEach
            val value = if (opt.requiresValue) quickFlagValues[opt.id].orEmpty().ifBlank { opt.defaultValue } else ""
            if (opt.requiresValue && value.isBlank()) return@forEach
            args.add(opt.format(value))
        }

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

