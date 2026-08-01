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
            // TODO: 持久化用户修改过的 CVars
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
}

