package com.srceng.launcher.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.srceng.launcher.LauncherViewModel
import com.srceng.launcher.data.CVar
import com.srceng.launcher.data.CVarCategory
import com.srceng.launcher.data.CVarType

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun CVarScreen(vm: LauncherViewModel) {
    val workingCVars by vm.workingCVars.collectAsState()
    val filteredCVars by vm.filteredCVars.collectAsState()
    val searchQ by vm.searchQuery.collectAsState()
    val hasChanges by vm.hasUnsavedCVarChanges.collectAsState()

    // 当前选中的分类 Tab
    var currentCategory by remember { mutableStateOf<CVarCategory?>(null) }

    // 要编辑的 CVar 对话框
    var editingCVar by remember { mutableStateOf<CVar?>(null) }

    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = {
                        Text("CVar 管理", fontWeight = FontWeight.Bold)
                    },
                    actions = {
                        IconButton(onClick = { vm.resetAllCVars() }) {
                            Icon(Icons.Default.Restore, "全部重置")
                        }
                    }
                )
                // Search bar
                SearchBar(
                    query = searchQ,
                    onQueryChange = { vm.setSearchQuery(it) },
                    onSearch = {},
                    active = false,
                    onActiveChange = {},
                    placeholder = { Text("搜索 CVar 名称或描述…") },
                    leadingIcon = { Icon(Icons.Default.Search, null) },
                    trailingIcon = {
                        if (searchQ.isNotBlank()) {
                            IconButton(onClick = { vm.setSearchQuery("") }) {
                                Icon(Icons.Default.Clear, null)
                            }
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp)
                ) {}
                // Category tabs
                ScrollableTabRow(
                    selectedTabIndex = CVarCategory.entries.indexOfFirst { it == currentCategory } + 1,
                    modifier = Modifier.padding(top = 10.dp),
                    edgePadding = 8.dp,
                    containerColor = MaterialTheme.colorScheme.surface
                ) {
                    Tab(
                        selected = currentCategory == null,
                        onClick = { currentCategory = null },
                        text = {
                            Text(
                                "全部 (${workingCVars.size})",
                                fontWeight = if (currentCategory == null) FontWeight.Bold else FontWeight.Normal
                            )
                        }
                    )
                    CVarCategory.entries.forEach { cat ->
                        val count = workingCVars.count { it.category == cat }
                        Tab(
                            selected = currentCategory == cat,
                            onClick = { currentCategory = cat },
                            text = {
                                Text(
                                    "${cat.displayName} ($count)",
                                    fontWeight = if (currentCategory == cat) FontWeight.Bold else FontWeight.Normal
                                )
                            }
                        )
                    }
                }
            }
        },
        bottomBar = {
            if (hasChanges) {
                BottomAppBar(containerColor = MaterialTheme.colorScheme.secondaryContainer) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        TextButton(
                            onClick = { vm.resetAllCVars() },
                            modifier = Modifier.padding(start = 4.dp)
                        ) {
                            Icon(Icons.Default.Undo, null)
                            Spacer(Modifier.width(4.dp))
                            Text("全部重置")
                        }
                        Spacer(Modifier.weight(1f))
                        val modified = workingCVars.count { it.isModified }
                        Text(
                            "已修改 $modified 项",
                            modifier = Modifier.padding(end = 12.dp),
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                        Button(
                            onClick = { vm.saveCVarsAsCustom() }
                        ) {
                            Icon(Icons.Default.Save, null)
                            Spacer(Modifier.width(6.dp))
                            Text("应用到启动配置")
                        }
                        Spacer(Modifier.width(6.dp))
                    }
                }
            }
        }
    ) { innerPadding ->
        val displayed = filteredCVars
            .let { list -> if (currentCategory == null) list else list.filter { it.category == currentCategory } }

        LazyColumn(
            modifier = Modifier.padding(innerPadding),
            contentPadding = PaddingValues(12.dp, 8.dp, 12.dp, 100.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (displayed.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(top = 80.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                Icons.Default.SearchOff, null,
                                modifier = Modifier.size(56.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Spacer(Modifier.height(8.dp))
                            Text(
                                "没有找到匹配的 CVar",
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            items(displayed, key = { it.name }) { cvar ->
                CVarCard(
                    cvar = cvar,
                    onClick = { editingCVar = cvar },
                    onToggle = { bool ->
                        vm.updateCVar(cvar.name, if (bool) "1" else "0")
                    },
                    onReset = { vm.resetCVar(cvar.name) }
                )
            }
        }
    }

    // Edit dialog
    val edit = editingCVar
    if (edit != null) {
        CVarEditDialog(
            cvar = edit,
            onDismiss = { editingCVar = null },
            onApply = { newValue ->
                vm.updateCVar(edit.name, newValue)
                editingCVar = null
            }
        )
    }
}

// ===== CVarCard =====
@Composable
private fun CVarCard(
    cvar: CVar,
    onClick: () -> Unit,
    onToggle: (Boolean) -> Unit,
    onReset: () -> Unit
) {
    ElevatedCard(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(14.dp, 12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            cvar.displayName,
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.titleSmall
                        )
                        Spacer(Modifier.width(8.dp))
                        // 作弊/修改 Badge
                        if (cvar.requiresCheats) {
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(MaterialTheme.colorScheme.errorContainer)
                                    .padding(6.dp, 2.dp)
                            ) {
                                Text(
                                    "作弊",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onErrorContainer,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                            Spacer(Modifier.width(6.dp))
                        }
                        if (cvar.isModified) {
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(MaterialTheme.colorScheme.tertiaryContainer)
                                    .padding(6.dp, 2.dp)
                            ) {
                                Text(
                                    "已修改",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onTertiaryContainer,
                                    fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }
                    Text(
                        text = cvar.name,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Medium
                    )
                }
                // Boolean: 直接显示开关
                if (cvar.type == CVarType.BOOLEAN) {
                    Switch(
                        checked = cvar.currentValue == "1",
                        onCheckedChange = onToggle
                    )
                } else {
                    Text(
                        cvar.currentValue.ifBlank { "（空）" },
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.padding(end = 6.dp)
                    )
                }
            }
            if (cvar.description.isNotBlank()) {
                Spacer(Modifier.height(6.dp))
                Text(
                    cvar.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Spacer(Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                SuggestionChip(
                    onClick = {},
                    label = { Text("默认: ${cvar.defaultValue.ifBlank { "空" }}") },
                    enabled = !cvar.isModified
                )
                Spacer(Modifier.width(8.dp))
                if (cvar.isModified) {
                    AssistChip(
                        onClick = onReset,
                        label = { Text("重置默认") },
                        leadingIcon = { Icon(Icons.Default.Restore, null, Modifier.size(16.dp)) }
                    )
                }
                Spacer(Modifier.weight(1f))
                Text(
                    text = "点击编辑",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

// ===== Edit dialog =====
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CVarEditDialog(
    cvar: CVar,
    onDismiss: () -> Unit,
    onApply: (String) -> Unit
) {
    var current by remember(cvar) { mutableStateOf(cvar.currentValue) }

    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = { onApply(current) }) {
                Text("应用", fontWeight = FontWeight.Bold)
            }
        },
        dismissButton = {
            Row {
                TextButton(onClick = { current = cvar.defaultValue }) {
                    Text("默认")
                }
                TextButton(onClick = onDismiss) { Text("取消") }
            }
        },
        title = {
            Column {
                Text(cvar.displayName, fontWeight = FontWeight.Bold)
                Text(
                    cvar.name,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                if (cvar.description.isNotBlank()) {
                    Text(
                        cvar.description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                when (cvar.type) {
                    CVarType.BOOLEAN -> {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                                .padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("启用", modifier = Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                            Switch(
                                checked = current == "1",
                                onCheckedChange = { v -> current = if (v) "1" else "0" }
                            )
                        }
                    }
                    CVarType.ENUM -> {
                        cvar.allowedValues?.let { values ->
                            FlowRow(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                values.forEach { v ->
                                    FilterChip(
                                        selected = current == v,
                                        onClick = { current = v },
                                        label = { Text(v.ifBlank { "空" }) }
                                    )
                                }
                            }
                        }
                    }
                    CVarType.INTEGER, CVarType.FLOAT -> {
                        OutlinedTextField(
                            value = current,
                            onValueChange = { current = it },
                            keyboardOptions = KeyboardOptions(
                                keyboardType = if (cvar.type == CVarType.INTEGER) KeyboardType.Number
                                else KeyboardType.Decimal
                            ),
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            label = { Text("数值") },
                            supportingText = {
                                val min = cvar.minValue; val max = cvar.maxValue
                                if (min != null || max != null) {
                                    Text("范围: ${min ?: "-∞"} ~ ${max ?: "+∞"}")
                                }
                            }
                        )
                    }
                    CVarType.STRING -> {
                        OutlinedTextField(
                            value = current,
                            onValueChange = { current = it },
                            modifier = Modifier.fillMaxWidth(),
                            label = { Text("字符串值") },
                            placeholder = { Text("（空字符串）") }
                        )
                    }
                }
            }
        }
    )
}
