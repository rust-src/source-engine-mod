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
fun defaultQuickFlags(): Map<String, Boolean> = emptyMap()

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
    val gameDirectoryUri: String = "",   // SAF tree URI，用于 DocumentFile 检测（Android 11+ 作用域存储需要）
    val selectedMod: String = "hl2",
    val customLaunchArgs: String = "",

    // 其他
    val debugMode: Boolean = false,
    val showConsole: Boolean = false,

    // ===== 快速勾选的命令行参数 (key = CmdOption.id, value = 勾选状态/自定义值) =====
    val quickFlags: Map<String, Boolean> = defaultQuickFlags(),
    // 对需要数值的参数保存值 (key = CmdOption.id)
    val quickFlagValues: Map<String, String> = defaultQuickFlagValues(),

    // ===== 用户手动修改过的 CVar 值 (key = cvar.name, value = 用户覆盖值) =====
    val customCvars: Map<String, String> = emptyMap()
) {
    /**
     * 根据设置生成 +exec cfg 文件内容 (用于在游戏内自动执行)
     *
     * 规则：默认值不写入，只有用户显式修改/启用过的才写入，避免 cfg 被一大堆默认值污染
     *  - CVar: isModified == true 才写
     */
    fun toAutoExecCfg(cvars: List<CVar>): String {
        val lines = mutableListOf<String>()
        lines.add("// 由 Source Engine Launcher 自动生成 - 请勿手动编辑")
        lines.add("// 生成于: ${java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date())}")
        lines.add("// 本文件仅包含用户自定义项，默认值不会写入。")
        lines.add("")

        val body = mutableListOf<String>()

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

    /**
     * 生成命令行启动参数
     *
     * 规则：未勾选的 quickFlag 不写入，避免参数过长
     * 仅当用户显式修改/启用时才写入；自定义参数始终保留
     */
    fun toLaunchArgs(): String {
        val args = mutableListOf<String>()

        // 注意：-game <mod> 由 ValveActivity2.initNatives 根据 Intent 的 gamedir extra 拼接
        // （参考 srceng-launcher_cn 的 setArgs 调用：finalArgv = "-game "+gamedir+" "+argv）
        // 这里**不能**再重复写 -game，否则会在最终命令行出现两次。

        // 开发者模式：只有开启时写
        if (debugMode) args.add("-dev -condebug")
        if (showConsole) args.add("-console")

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
        val GAME_DIR_URI = stringPreferencesKey("game_dir_uri")
        val SELECTED_MOD = stringPreferencesKey("selected_mod")
        val CUSTOM_ARGS = stringPreferencesKey("custom_args")
        val DEBUG = booleanPreferencesKey("debug")
        val CONSOLE = booleanPreferencesKey("console")
        val QUICK_FLAGS_JSON = stringPreferencesKey("quick_flags_json")
        val QUICK_FLAG_VALUES_JSON = stringPreferencesKey("quick_flag_values_json")
        val CUSTOM_CVARS_JSON = stringPreferencesKey("custom_cvars_json")
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
                gameDirectoryUri = prefs[Keys.GAME_DIR_URI] ?: "",
                selectedMod = prefs[Keys.SELECTED_MOD] ?: "hl2",
                customLaunchArgs = prefs[Keys.CUSTOM_ARGS] ?: "",
                debugMode = prefs[Keys.DEBUG] ?: false,
                showConsole = prefs[Keys.CONSOLE] ?: false,
                quickFlags = prefs[Keys.QUICK_FLAGS_JSON]?.parseFlagMap() ?: defaultQuickFlags(),
                quickFlagValues = prefs[Keys.QUICK_FLAG_VALUES_JSON]?.parseValueMap() ?: defaultQuickFlagValues(),
                customCvars = prefs[Keys.CUSTOM_CVARS_JSON]?.parseValueMap() ?: emptyMap()
            )
        }

    suspend fun update(transform: suspend (LauncherConfig) -> LauncherConfig) {
        context.dataStore.edit { prefs ->
            val current = prefs.toConfig()
            val next = transform(current)
            prefs[Keys.THEME_MODE] = next.themeMode.name
            prefs[Keys.DYNAMIC_COLOR] = next.useDynamicColor
            prefs[Keys.GAME_DIR] = next.gameDirectory
            prefs[Keys.GAME_DIR_URI] = next.gameDirectoryUri
            prefs[Keys.SELECTED_MOD] = next.selectedMod
            prefs[Keys.CUSTOM_ARGS] = next.customLaunchArgs
            prefs[Keys.DEBUG] = next.debugMode
            prefs[Keys.CONSOLE] = next.showConsole
            prefs[Keys.QUICK_FLAGS_JSON] = next.quickFlags.flagsToJsonString()
            prefs[Keys.QUICK_FLAG_VALUES_JSON] = next.quickFlagValues.valuesToJsonString()
            prefs[Keys.CUSTOM_CVARS_JSON] = next.customCvars.valuesToJsonString()
        }
    }

    private fun Preferences.toConfig(): LauncherConfig = LauncherConfig(
        themeMode = this[Keys.THEME_MODE]?.let(ThemeMode::valueOf) ?: ThemeMode.SYSTEM,
        useDynamicColor = this[Keys.DYNAMIC_COLOR] ?: true,
        gameDirectory = this[Keys.GAME_DIR] ?: "",
        gameDirectoryUri = this[Keys.GAME_DIR_URI] ?: "",
        selectedMod = this[Keys.SELECTED_MOD] ?: "hl2",
        customLaunchArgs = this[Keys.CUSTOM_ARGS] ?: "",
        debugMode = this[Keys.DEBUG] ?: false,
        showConsole = this[Keys.CONSOLE] ?: false,
        quickFlags = this[Keys.QUICK_FLAGS_JSON]?.parseFlagMap() ?: defaultQuickFlags(),
        quickFlagValues = this[Keys.QUICK_FLAG_VALUES_JSON]?.parseValueMap() ?: defaultQuickFlagValues(),
        customCvars = this[Keys.CUSTOM_CVARS_JSON]?.parseValueMap() ?: emptyMap()
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