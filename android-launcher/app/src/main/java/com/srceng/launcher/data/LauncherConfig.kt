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
    val startListenServer: Boolean = false
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
                startListenServer = prefs[Keys.SERVER_START] ?: false
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
        startListenServer = this[Keys.SERVER_START] ?: false
    )
}
