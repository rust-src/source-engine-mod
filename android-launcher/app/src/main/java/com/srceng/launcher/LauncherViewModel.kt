package com.srceng.launcher

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.srceng.launcher.data.CVar
import com.srceng.launcher.data.LauncherConfig
import com.srceng.launcher.data.LauncherPreferences
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

    // === 游戏启动 ===
    fun buildAutoExecCfg(): String {
        return config.value.toAutoExecCfg(_workingCVars.value)
    }

    fun buildLaunchArgs(): String {
        return config.value.toLaunchArgs()
    }
}
