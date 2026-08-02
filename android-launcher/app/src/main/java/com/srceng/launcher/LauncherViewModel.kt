package com.srceng.launcher

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.srceng.launcher.data.CVar
import com.srceng.launcher.data.CommandLineOption
import com.srceng.launcher.data.LauncherConfig
import com.srceng.launcher.data.LauncherPreferences
import com.srceng.launcher.data.PredefinedCmdOptions
import com.srceng.launcher.data.PredefinedCVars
import com.srceng.launcher.game.GameLauncher
import com.srceng.launcher.game.GameLauncher.Diagnostic
import com.srceng.launcher.game.GameLauncher.LaunchFlowResult
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

@OptIn(ExperimentalCoroutinesApi::class)
class LauncherViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = LauncherPreferences(app)

    val config: StateFlow<LauncherConfig> = prefs.configFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, LauncherConfig())

    // CVar 工作状态: 用户改动过但未保存的版本
    private val _workingCVars = MutableStateFlow<List<CVar>>(PredefinedCVars.all)
    val workingCVars: StateFlow<List<CVar>> = _workingCVars.asStateFlow()

    init {
        // 启动时：把持久化的 customCvars 应用到 workingCVars，让用户上一次保存的覆盖值
        // 再次显示为 "已修改"，避免用户以为 CVar "没保存"。
        viewModelScope.launch {
            prefs.configFlow
                .take(1)
                .collect { cfg ->
                    applyCustomCvarsToWorking(cfg.customCvars)
                }
        }
        // 当 customCvars 被保存/刷新时，也同步到工作副本（仅对未 isModified 的项覆盖）
        viewModelScope.launch {
            prefs.configFlow
                .drop(1) // 跳过启动时那一次（上面处理过了）
                .collect { cfg ->
                    mergeCustomCvarsToWorking(cfg.customCvars)
                }
        }
    }

    /** 首次加载时：把 customCvars 直接覆盖到 workingCVars，保留 isModified=true 状态 */
    private fun applyCustomCvarsToWorking(saved: Map<String, String>) {
        if (saved.isEmpty()) return
        _workingCVars.update { list ->
            list.map { cvar ->
                val override = saved[cvar.name] ?: return@map cvar
                cvar.withValue(override)
            }
        }
    }

    /** 后续同步：只有用户当前没有在 working 里显式改过的 CVar 才用 persisted 值覆盖，
     *  防止用户刚改的 working 值被一次保存动作冲掉 */
    private fun mergeCustomCvarsToWorking(saved: Map<String, String>) {
        _workingCVars.update { list ->
            list.map { cvar ->
                if (cvar.isModified) return@map cvar // 用户有未保存改动，保留用户值
                val override = saved[cvar.name] ?: return@map cvar
                cvar.withValue(override)
            }
        }
    }

    val hasUnsavedCVarChanges: StateFlow<Boolean> = _workingCVars
        .map { list -> list.any { it.isModified } }
        .stateIn(viewModelScope, SharingStarted.Eagerly, false)

    // 搜索关键字
    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    fun setSearchQuery(q: String) { _searchQuery.value = q }

    val filteredCVars: StateFlow<List<CVar>> = combine(
        _workingCVars,
        _searchQuery
    ) { list, q ->
        val query = q.trim().lowercase()
        if (query.isEmpty()) list
        else list.filter {
            it.name.lowercase().contains(query) ||
            it.displayName.lowercase().contains(query) ||
            it.description.lowercase().contains(query)
        }
    }.stateIn(viewModelScope, SharingStarted.Eagerly, PredefinedCVars.all)

    // === 更新 CVar 值 ===
    fun updateCVar(name: String, newValue: String) {
        _workingCVars.update { list ->
            list.map { cvar ->
                if (cvar.name == name) cvar.withValue(newValue) else cvar
            }
        }
    }

    fun resetCVar(name: String) {
        _workingCVars.update { list ->
            list.map { cvar ->
                if (cvar.name == name) cvar.resetToDefault() else cvar
            }
        }
    }

    fun resetAllCVars() {
        _workingCVars.value = PredefinedCVars.all
    }

    fun saveCVarsAsCustom() {
        viewModelScope.launch {
            val modified = _workingCVars.value.filter { it.isModified }
                .associate { it.name to it.currentValue }
            // 已重置为默认（=不在 modified）的项目应当从持久化中清掉：
            // 做法：从当前 customCvars 中先载入；然后把 isModified 的覆盖进去；把 reset 回默认的删掉
            // 由于 LauncherConfig.customCvars 本身存的就是 "非默认项"，逻辑就是直接用 workingCVars 中所有 isModified 重新覆盖写。
            // 这样：当用户点 "重置默认"（isModified=false）就会自然从保存结果中移除，下一次读入时默认值就会回来。
            prefs.update { cfg ->
                cfg.copy(customCvars = modified)
            }
        }
    }

    // === 更新配置 ===
    fun updateConfig(block: suspend (LauncherConfig) -> LauncherConfig) {
        viewModelScope.launch { prefs.update(block) }
    }

    // ===== 命令行快速勾选开关 =====
    fun toggleQuickFlag(opt: CommandLineOption, enabled: Boolean) {
        viewModelScope.launch {
            prefs.update { cfg ->
                val nextFlags = cfg.quickFlags.toMutableMap().apply {
                    if (enabled) put(opt.id, true) else remove(opt.id)
                }
                val nextValues = cfg.quickFlagValues.toMutableMap()
                // 首次启用时填充默认值
                if (enabled && opt.requiresValue && opt.defaultValue.isNotBlank()
                    && !nextValues.containsKey(opt.id)) {
                    nextValues[opt.id] = opt.defaultValue
                }
                cfg.copy(quickFlags = nextFlags, quickFlagValues = nextValues)
            }
        }
    }

    fun setQuickFlagValue(opt: CommandLineOption, value: String) {
        viewModelScope.launch {
            prefs.update { cfg ->
                val nextValues = cfg.quickFlagValues.toMutableMap()
                if (value.isBlank()) nextValues.remove(opt.id) else nextValues[opt.id] = value
                cfg.copy(quickFlagValues = nextValues)
            }
        }
    }

    // 当前选项是否勾选
    fun isQuickFlagEnabled(opt: CommandLineOption): Boolean {
        val cfg = config.value
        return cfg.quickFlags[opt.id] ?: opt.defaultEnabled
    }

    // 当前选项取值（未显式设置返回 defaultValue）
    fun quickFlagValue(opt: CommandLineOption): String {
        val cfg = config.value
        return cfg.quickFlagValues[opt.id].orEmpty().ifBlank { opt.defaultValue }
    }

    // === 游戏启动 ===
    fun buildAutoExecCfg(): String {
        return config.value.toAutoExecCfg(_workingCVars.value)
    }

    fun buildLaunchArgs(): String {
        return config.value.toLaunchArgs()
    }

    /** 当前启动前自检（UI 用于显示状态条） */
    fun diagnostics(): List<Diagnostic> {
        val cfg = config.value
        return GameLauncher.runDiagnostics(getApplication(), cfg.gameDirectory, cfg.selectedMod)
    }

    fun isReadyToLaunch(): Boolean = diagnostics().none { !it.ok }

    /**
     * 一键执行：写 autoexec.cfg → 写 launch-args.txt →（满足条件时）启动 SDLActivity
     * 返回完整流程结果，UI 端根据结果弹 Snackbar / 错误对话框。
     */
    fun launchNow(): LaunchFlowResult {
        val cfg = config.value
        val cvars = _workingCVars.value
        val autoexecBody = buildAutoExecCfg()
        val args = buildLaunchArgs()
        return GameLauncher.prepareAndLaunch(
            context = getApplication(),
            gameDir = cfg.gameDirectory,
            mod = cfg.selectedMod,
            cvars = cvars,
            autoexecBody = autoexecBody,
            launchArgs = args
        )
    }
}

