package com.srceng.launcher.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.srceng.launcher.LauncherViewModel
import com.srceng.launcher.ui.Screen

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(vm: LauncherViewModel, navController: NavHostController) {
    val config by vm.config.collectAsState()
    var showLaunchOptions by remember { mutableStateOf(false) }
    var showArgsDialog by remember { mutableStateOf(false) }
    var customArgs by remember(config.customLaunchArgs) { mutableStateOf(config.customLaunchArgs) }

    val scrollState = rememberScrollState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp)
    ) {
        // === Hero 卡片 ===
        HeroCard(onClickSettings = { navController.navigate(Screen.Settings.route) })

        // === 游戏目录 / 模组信息 ===
        InfoCard(config, vm, navController)

        // === 快速设置 ===
        QuickLaunchOptions(
            config = config,
            vm = vm,
            expanded = showLaunchOptions,
            onToggleExpand = { showLaunchOptions = !showLaunchOptions }
        )

        // === 自定义启动参数 ===
        OutlinedCard(
            onClick = { showArgsDialog = true },
            modifier = Modifier.fillMaxWidth()
        ) {
            ListItem(
                colors = ListItemDefaults.colors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
                ),
                headlineContent = { Text("启动参数", fontWeight = FontWeight.Bold) },
                supportingContent = {
                    Text(
                        text = config.customLaunchArgs.ifBlank { "（无自定义参数）" },
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2
                    )
                },
                trailingContent = { Icon(Icons.Default.ArrowForwardIos, null) },
                leadingContent = { Icon(Icons.Default.Code, null) }
            )
        }

        // === 启动按钮组 ===
        Spacer(modifier = Modifier.weight(1f))
        ActionButtons(
            config = config,
            vm = vm,
            customArgsPreview = vm.buildLaunchArgs()
        )
    }

    // 自定义启动参数对话框
    if (showArgsDialog) {
        AlertDialog(
            onDismissRequest = { showArgsDialog = false },
            confirmButton = {
                TextButton(onClick = {
                    vm.updateConfig { it.copy(customLaunchArgs = customArgs) }
                    showArgsDialog = false
                }) {
                    Text("保存")
                }
            },
            dismissButton = {
                TextButton(onClick = {
                    customArgs = config.customLaunchArgs
                    showArgsDialog = false
                }) {
                    Text("取消")
                }
            },
            title = { Text("自定义启动参数") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = "将附加到所有启动参数之后。例：-heapsize 524288 -novid",
                        style = MaterialTheme.typography.bodySmall
                    )
                    OutlinedTextField(
                        value = customArgs,
                        onValueChange = { customArgs = it },
                        modifier = Modifier.fillMaxWidth(),
                        minLines = 2,
                        placeholder = { Text("-novid -high") }
                    )
                }
            }
        )
    }
}

// ===== Hero =====
@Composable
private fun HeroCard(onClickSettings: () -> Unit) {
    val primary = MaterialTheme.colorScheme.primary
    val secondary = MaterialTheme.colorScheme.secondaryContainer

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(28.dp))
            .background(
                Brush.linearGradient(
                    listOf(primary, secondary)
                )
            )
            .padding(22.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = "\uD83C\uDFAE Source Engine",
                color = MaterialTheme.colorScheme.onPrimary,
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold)
            )
            Surface(
                onClick = onClickSettings,
                color = Color.Black.copy(alpha = 0.15f),
                shape = RoundedCornerShape(50),
                modifier = Modifier.size(42.dp)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        Icons.Default.Settings,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onPrimary,
                        modifier = Modifier.size(22.dp)
                    )
                }
            }
        }
        Text(
            text = "Half-Life 2 · 移动端启动器",
            color = MaterialTheme.colorScheme.onPrimary.copy(alpha = 0.88f),
            style = MaterialTheme.typography.titleMedium
        )
        Text(
            text = "为 ARM/ARM64 Windows-on-ARM 与 Android 优化",
            color = MaterialTheme.colorScheme.onPrimary.copy(alpha = 0.75f),
            style = MaterialTheme.typography.bodySmall
        )
    }
}

// ===== Info Card =====
@Composable
private fun InfoCard(
    config: com.srceng.launcher.data.LauncherConfig,
    vm: LauncherViewModel,
    navController: NavHostController
) {
    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = "运行信息",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(bottom = 10.dp)
            )
            RowInfo(Icons.Default.Folder, "游戏目录",
                config.gameDirectory.ifBlank { "未配置（点击选择）" }) {
                // TODO: 目录选择
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))
            RowInfo(Icons.Default.Extension, "当前模组",
                mapOf(
                    "hl2" to "Half-Life 2",
                    "hl2mp" to "Half-Life 2: Deathmatch",
                    "ep1" to "Half-Life 2: Episode One",
                    "ep2" to "Half-Life 2: Episode Two"
                ).getOrDefault(config.selectedMod, config.selectedMod)
            ) {
                vm.updateConfig {
                    it.copy(selectedMod = if (it.selectedMod == "hl2") "hl2mp" else "hl2")
                }
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))
            RowInfo(Icons.Default.GraphicsBag, "渲染 API",
                when (config.renderApi) {
                    "gles2" -> "OpenGL ES 2.0"
                    "gles3" -> "OpenGL ES 3.1"
                    "vulkan" -> "Vulkan"
                    else -> config.renderApi
                }
            ) {
                navController.navigate(Screen.Settings.route)
            }
        }
    }
}

@Composable
private fun RowInfo(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    value: String,
    onClick: () -> Unit = {}
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(24.dp)
        )
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.labelMedium)
            Text(
                value,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
        Icon(Icons.Default.ArrowDropDown, null,
            modifier = Modifier.size(20.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

// ===== Quick options =====
@Composable
private fun QuickLaunchOptions(
    config: com.srceng.launcher.data.LauncherConfig,
    vm: LauncherViewModel,
    expanded: Boolean,
    onToggleExpand: () -> Unit
) {
    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column {
            ListItem(
                headlineContent = { Text("快速设置", fontWeight = FontWeight.Bold) },
                trailingContent = {
                    IconButton(onClick = onToggleExpand) {
                        Icon(
                            if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                            null
                        )
                    }
                },
                leadingContent = { Icon(Icons.Default.Tune, null) }
            )
            if (expanded) {
                Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)) {
                    SwitchRow(
                        title = "全屏模式",
                        checked = config.fullscreen,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(fullscreen = it) }
                        }
                    )
                    SwitchRow(
                        title = "垂直同步 (VSync)",
                        checked = config.vsync,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(vsync = it) }
                        }
                    )
                    SwitchRow(
                        title = "同时启动服务端",
                        subtitle = "启动监听服务器（主机）",
                        checked = config.startListenServer,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(startListenServer = it) }
                        }
                    )
                    SwitchRow(
                        title = "显示控制台",
                        subtitle = "调试模式可见",
                        checked = config.showConsole,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(showConsole = it) }
                        }
                    )
                    SwitchRow(
                        title = "调试模式 (-dev)",
                        checked = config.debugMode,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(debugMode = it) }
                        }
                    )
                }
                Spacer(Modifier.height(6.dp))
            }
        }
    }
}

@Composable
private fun SwitchRow(
    title: String,
    subtitle: String? = null,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            if (subtitle != null) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

// ===== Action buttons =====
@Composable
private fun ActionButtons(
    config: com.srceng.launcher.data.LauncherConfig,
    vm: LauncherViewModel,
    customArgsPreview: String
) {
    var showArgs by remember { mutableStateOf(false) }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        // Args preview
        OutlinedCard(
            onClick = { showArgs = !showArgs },
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text(
                    text = "启动参数预览 (点击展开/收起)",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary
                )
                if (showArgs) {
                    Spacer(Modifier.height(6.dp))
                    Text(
                        text = customArgsPreview,
                        style = MaterialTheme.typography.bodySmall.copy(lineHeight = 18.sp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            FilledTonalButton(
                onClick = { /* TODO: 启动服务端 */ },
                modifier = Modifier
                    .weight(1f)
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp)
            ) {
                Icon(Icons.Default.Dns, null)
                Spacer(Modifier.width(6.dp))
                Text("服务端", fontWeight = FontWeight.Bold)
            }
            Button(
                onClick = { /* TODO: 启动游戏 */ },
                modifier = Modifier
                    .weight(1.6f)
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = MaterialTheme.colorScheme.onPrimary
                )
            ) {
                Icon(Icons.Default.PlayArrow, null, modifier = Modifier.size(28.dp))
                Spacer(Modifier.width(8.dp))
                Text(
                    if (config.startListenServer) "启动主机" else "启动游戏",
                    fontWeight = FontWeight.Bold,
                    fontSize = 17.sp
                )
            }
        }
    }
}
