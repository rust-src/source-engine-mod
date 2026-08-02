package com.srceng.launcher.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.srceng.launcher.LauncherViewModel
import com.srceng.launcher.data.CmdCategory
import com.srceng.launcher.data.CommandLineOption
import com.srceng.launcher.data.PredefinedCmdOptions
import com.srceng.launcher.game.GameLauncher
import com.srceng.launcher.game.StorageHelper
import com.srceng.launcher.ui.Screen
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun HomeScreen(
    vm: LauncherViewModel,
    navController: NavHostController,
    snackbarHostState: SnackbarHostState
) {
    val context = LocalContext.current
    val config by vm.config.collectAsState()
    var showLaunchOptions by remember { mutableStateOf(false) }
    var showArgsDialog by remember { mutableStateOf(false) }
    var customArgs by remember(config.customLaunchArgs) { mutableStateOf(config.customLaunchArgs) }
    var showDiagnostics by remember { mutableStateOf(true) }
    var failureDialog by remember { mutableStateOf<GameLauncher.LaunchFlowResult?>(null) }
    // 命令行预览点击后弹出的可编辑对话框
    var showFullArgsEditor by remember { mutableStateOf(false) }
    var fullArgsCustomDraft by remember(config.customLaunchArgs) { mutableStateOf(config.customLaunchArgs) }
    // 游戏目录对话框：手动编辑
    var showGameDirDialog by remember { mutableStateOf(false) }
    var gameDirDraft by remember(config.gameDirectory) { mutableStateOf(config.gameDirectory) }

    val scope = rememberCoroutineScope()

    val scrollState = rememberScrollState()
    // 触发 recompute diagnostics 每当 config 或 gameDir 变化（每次重组拉一次即可）
    val diagnostics = remember(config) { vm.diagnostics() }

    // ===== 目录选择 (SAF OpenDocumentTree) =====
    val dirPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        // 持久化读写权限，下次打开不用重新授权
        try {
            val takeFlags =
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(uri, takeFlags)
        } catch (_: SecurityException) { /* 某些 ROM 不支持持久化忽略 */ }
        // 转换成真实文件路径（引擎 native 侧只能读真正的文件路径）
        val path = StorageHelper.safTreeToPath(uri)
        // 验证该路径的文档树看起来是否像 Source 根目录
        val looksSource = StorageHelper.looksLikeSourceRoot(context, uri)
        if (!looksSource) {
            scope.launch {
                snackbarHostState.showSnackbar(
                    message = "所选目录不像 Source 资源目录（没找到 hl2/platform/bin）",
                    duration = SnackbarDuration.Short
                )
            }
        }
        // 无论像不像都写入配置（路径 + URI 都要存，URI 用于 Android 11+ 作用域存储检测）
        vm.updateConfig { it.copy(gameDirectory = path, gameDirectoryUri = uri.toString()) }
        scope.launch {
            snackbarHostState.showSnackbar(
                message = "已保存游戏目录: $path",
                duration = SnackbarDuration.Short
            )
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp)
    ) {
        // === Hero 卡片 ===
        HeroCard(onClickSettings = { navController.navigate(Screen.Settings.route) })

        // === 启动前自检：状态面板 ===
        LauncherDiagnosticsPanel(
            diagnostics = diagnostics,
            expanded = showDiagnostics,
            onToggle = { showDiagnostics = !showDiagnostics }
        )

        // === 游戏目录 / 模组信息 ===
        InfoCard(config, vm, navController,
            onPickDir = { dirPicker.launch(null) },
            onManualEditDir = {
                gameDirDraft = config.gameDirectory
                showGameDirDialog = true
            }
        )

        // === 快速设置 ===
        QuickLaunchOptions(
            config = config,
            vm = vm,
            expanded = showLaunchOptions,
            onToggleExpand = { showLaunchOptions = !showLaunchOptions }
        )

        // === 常用命令行参数快速勾选 (基于 VDC 官方文档) ===
        QuickCmdLineOptionsCard(vm = vm)

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
            customArgsPreview = vm.buildLaunchArgs(),
            diagnostics = diagnostics,
            onOpenArgsEditor = {
                fullArgsCustomDraft = config.customLaunchArgs
                showFullArgsEditor = true
            },
            onClickLaunch = {
                val result = vm.launchNow()
                when {
                    result.canLaunch && result.launch?.success == true -> {
                        scope.launch {
                            snackbarHostState.showSnackbar(
                                message = "已启动引擎 · 已写入 autoexec.cfg + launch-args.txt",
                                duration = SnackbarDuration.Short
                            )
                        }
                    }
                    !result.canLaunch -> {
                        // 前置条件未满足：弹窗 + Snackbar 提示
                        failureDialog = result
                        val wrote = buildList {
                            if (result.autoexec?.success == true) add("autoexec.cfg")
                            if (result.argsFile?.success == true) add("launch-args.txt")
                        }
                        scope.launch {
                            snackbarHostState.showSnackbar(
                                if (wrote.isNotEmpty()) {
                                    "启动条件未满足，但已写入 ${wrote.joinToString(" / ")}。点击弹窗查看原因。"
                                } else {
                                    "启动条件未满足（详见弹出的诊断对话框）"
                                },
                                duration = SnackbarDuration.Short
                        )
                    }
                    else -> {
                        // canLaunch=true 但 launch!=success：给出 launch 失败提示
                        val msg = result.launch?.message?.takeIf { it.isNotBlank() }
                            ?: "启动失败（原因未知）"
                        failureDialog = result
                        scope.launch {
                            snackbarHostState.showSnackbar(msg, duration = SnackbarDuration.Short)
                        }
                    }
                }
            }
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

    // ===== 启动失败 / 条件未满足 弹窗 =====
    val fail = failureDialog
    if (fail != null) {
        LaunchFailureDialog(
            result = fail,
            onDismiss = { failureDialog = null },
            onRetry = {
                failureDialog = null
                // 重新点一下 launch（条件满足的话）
                if (fail.canLaunch && fail.launch?.success != true) {
                    // canLaunch=true 只是 launch 失败，再尝试一次；否则保持用户手动按启动按钮
                }
            }
        )
    }

    // ===== 完整启动参数编辑器（点击「启动参数预览」时弹出） =====
    if (showFullArgsEditor) {
        val assembled by remember(vm.config.value, fullArgsCustomDraft) {
            derivedStateOf {
                val tmp = vm.config.value.copy(customLaunchArgs = fullArgsCustomDraft)
                tmp.toLaunchArgs()
            }
        }
        AlertDialog(
            onDismissRequest = { showFullArgsEditor = false },
            confirmButton = {
                TextButton(onClick = {
                    vm.updateConfig { it.copy(customLaunchArgs = fullArgsCustomDraft.trim()) }
                    showFullArgsEditor = false
                }) {
                    Text("保存")
                }
            },
            dismissButton = {
                Row {
                    TextButton(onClick = {
                        fullArgsCustomDraft = config.customLaunchArgs
                        showFullArgsEditor = false
                    }) { Text("取消") }
                }
            },
            title = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Code, null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.width(8.dp))
                    Text("启动参数（含预览 + 编辑）", fontWeight = FontWeight.Bold)
                }
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        "以下为当前配置拼出的完整启动命令（只读预览）；下方文本框可追加自定义参数。",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    // 只读预览
                    Surface(
                        shape = RoundedCornerShape(12.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = assembled.ifBlank { "（无）" },
                            style = MaterialTheme.typography.bodySmall.copy(lineHeight = 18.sp),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(10.dp)
                        )
                    }
                    OutlinedTextField(
                        value = fullArgsCustomDraft,
                        onValueChange = { fullArgsCustomDraft = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("自定义启动参数（附加在末尾）") },
                        placeholder = { Text("-novid -heapsize 524288") },
                        minLines = 2,
                        supportingText = {
                            Text("提示：修改保存后，上方预览会实时重算。")
                        }
                    )
                }
            }
        )
    }

    // ===== 游戏目录手动输入 / 粘贴对话框 =====
    if (showGameDirDialog) {
        AlertDialog(
            onDismissRequest = { showGameDirDialog = false },
            confirmButton = {
                TextButton(onClick = {
                    vm.updateConfig { it.copy(gameDirectory = gameDirDraft.trim()) }
                    showGameDirDialog = false
                }) {
                    Text("保存")
                }
            },
            dismissButton = {
                TextButton(onClick = {
                    gameDirDraft = config.gameDirectory
                    showGameDirDialog = false
                }) { Text("取消") }
            },
            title = { Text("手动设置游戏目录", fontWeight = FontWeight.Bold) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        "直接粘贴一个文件系统路径（如 /sdcard/Sources/…）。如果不确定路径，使用上方的「从文件管理器选择」。",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    OutlinedTextField(
                        value = gameDirDraft,
                        onValueChange = { gameDirDraft = it },
                        modifier = Modifier.fillMaxWidth(),
                        label = { Text("游戏根目录绝对路径") },
                        placeholder = { Text("/storage/emulated/0/Sources/hl2-assets") },
                        singleLine = false,
                        minLines = 2
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
    navController: NavHostController,
    onPickDir: () -> Unit,
    onManualEditDir: () -> Unit
) {
    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = "运行信息",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(bottom = 10.dp)
            )
            // 游戏目录：点击行 → 弹 SAF 目录选择器；右下角提供手动粘贴路径
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .clickable { onPickDir() }
                    .padding(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    Icons.Default.Folder,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(24.dp)
                )
                Spacer(Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text("游戏目录", style = MaterialTheme.typography.labelMedium)
                    Text(
                        config.gameDirectory.ifBlank { "未配置（点击选择 Source 资源目录）" },
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
                IconButton(onClick = onManualEditDir) {
                    Icon(Icons.Default.Edit, "手动输入路径")
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 8.dp, end = 8.dp, top = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedButton(
                    onClick = onPickDir,
                    modifier = Modifier.height(36.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp)
                ) {
                    Icon(Icons.Default.FolderOpen, null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("从文件管理器选择", fontSize = 12.sp)
                }
                OutlinedButton(
                    onClick = onManualEditDir,
                    modifier = Modifier.height(36.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp)
                ) {
                    Icon(Icons.Default.Edit, null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("手动粘贴路径", fontSize = 12.sp)
                }
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
            RowInfo(Icons.Default.Preview, "渲染 API",
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
    customArgsPreview: String,
    diagnostics: List<GameLauncher.Diagnostic>,
    onOpenArgsEditor: () -> Unit,
    onClickLaunch: () -> Unit
) {
    val ready = diagnostics.none { !it.ok }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        // Args preview (点击直接弹编辑器而不是只展开预览)
        OutlinedCard(
            onClick = onOpenArgsEditor,
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "启动参数（点击编辑，包含预览）",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold
                    )
                    Icon(Icons.Default.Edit, null, modifier = Modifier.size(14.dp),
                        tint = MaterialTheme.colorScheme.primary)
                }
                Spacer(Modifier.height(6.dp))
                Text(
                    text = customArgsPreview,
                    style = MaterialTheme.typography.bodySmall.copy(lineHeight = 18.sp),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 4
                )
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            FilledTonalButton(
                onClick = { onClickLaunch() }, // 复用同一流程（args 中已包含 +map 等服务器参数
                modifier = Modifier
                    .weight(1f)
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp),
                enabled = true
            ) {
                Icon(Icons.Default.Dns, null)
                Spacer(Modifier.width(6.dp))
                Text("服务端", fontWeight = FontWeight.Bold)
            }
            val (btnLabel, btnIcon) = when {
                config.startListenServer -> "启动主机" to Icons.Default.Router
                ready -> "启动游戏" to Icons.Default.PlayArrow
                else -> "启动（未就绪）" to Icons.Default.PlayArrow
            }
            Button(
                onClick = onClickLaunch,
                modifier = Modifier
                    .weight(1.6f)
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (ready) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.tertiaryContainer,
                    contentColor = if (ready) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onTertiaryContainer
                ),
                enabled = true  // 始终启用；不满足条件时点击后仍会写配置 + 清晰的诊断弹窗，避免“按钮灰掉用户以为坏掉
            ) {
                Icon(btnIcon, null, modifier = Modifier.size(28.dp))
                Spacer(Modifier.width(8.dp))
                Text(
                    text = if (config.startListenServer) "启动主机" else if (ready) "启动游戏" else "启动（未就绪）",
                    fontWeight = FontWeight.Bold,
                    fontSize = 17.sp
                )
            }
        }
    }
}

// ============================================================
// 常用命令行参数快速勾选面板 (基于 Valve Developer Community)
// ============================================================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun QuickCmdLineOptionsCard(vm: LauncherViewModel) {
    var expanded by remember { mutableStateOf(false) }
    var tab by remember { mutableStateOf(CmdCategory.GAMEPLAY) }
    val config by vm.config.collectAsState()
    val editingValueFor = remember { mutableStateOf<CommandLineOption?>(null) }

    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column {
            ListItem(
                headlineContent = {
                    Text("命令行启动参数", fontWeight = FontWeight.Bold)
                },
                supportingContent = {
                    Text(
                        "基于 VDC 官方文档整理，常用开关一键勾选",
                        style = MaterialTheme.typography.bodySmall
                    )
                },
                leadingContent = { Icon(Icons.Default.Launch, null, tint = MaterialTheme.colorScheme.primary) },
                trailingContent = {
                    IconButton(onClick = { expanded = !expanded }) {
                        Icon(
                            if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                            null
                        )
                    }
                }
            )
            if (expanded) {
                // 分类 Tab
                val categories = listOf(
                    CmdCategory.GAMEPLAY to "游戏",
                    CmdCategory.VIDEO to "图形",
                    CmdCategory.PERFORMANCE to "性能",
                    CmdCategory.NETWORK to "网络",
                    CmdCategory.WINDOW to "窗口",
                    CmdCategory.DEVELOPER to "调试",
                    CmdCategory.INPUT to "输入"
                )
                ScrollableTabRow(
                    selectedTabIndex = categories.indexOfFirst { it.first == tab }.coerceAtLeast(0),
                    containerColor = Color.Transparent,
                    edgePadding = 8.dp,
                    divider = {}
                ) {
                    categories.forEach { (cat, name) ->
                        Tab(
                            selected = tab == cat,
                            onClick = { tab = cat },
                            text = { Text(name, maxLines = 1) }
                        )
                    }
                }
                Divider()
                val options = PredefinedCmdOptions.androidSafe()
                    .filter { it.category == tab }
                if (options.isEmpty()) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 24.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            "当前分类下暂无推荐参数",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        options.forEach { opt ->
                            val enabled = config.quickFlags[opt.id] ?: opt.defaultEnabled
                            CmdOptionRow(
                                opt = opt,
                                enabled = enabled,
                                value = config.quickFlagValues[opt.id]
                                    .orEmpty()
                                    .ifBlank { opt.defaultValue },
                                onToggle = { on -> vm.toggleQuickFlag(opt, on) },
                                onEditValue = { editingValueFor.value = opt }
                            )
                        }
                    }
                }
            }
        }
    }

    // 数值编辑对话框
    val editing = editingValueFor.value
    if (editing != null) {
        CmdValueDialog(
            opt = editing,
            currentValue = config.quickFlagValues[editing.id]
                .orEmpty()
                .ifBlank { editing.defaultValue },
            onDismiss = { editingValueFor.value = null },
            onApply = { newVal ->
                vm.setQuickFlagValue(editing, newVal)
                // 自动勾选
                if (!config.quickFlags.getOrDefault(editing.id, editing.defaultEnabled) && newVal.isNotBlank()) {
                    vm.toggleQuickFlag(editing, true)
                }
                editingValueFor.value = null
            }
        )
    }
}

@Composable
private fun CmdOptionRow(
    opt: CommandLineOption,
    enabled: Boolean,
    value: String,
    onToggle: (Boolean) -> Unit,
    onEditValue: () -> Unit
) {
    Surface(
        onClick = { onToggle(!enabled) },
        color = if (enabled)
            MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.45f)
        else Color.Transparent,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = opt.displayName,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(Modifier.width(8.dp))
                    AssistChip(
                        onClick = {},
                        label = {
                            Text(
                                opt.flag,
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Medium
                            )
                        },
                        colors = AssistChipDefaults.assistChipColors(
                            containerColor = MaterialTheme.colorScheme.surfaceVariant,
                            labelColor = MaterialTheme.colorScheme.primary
                        )
                    )
                }
                Text(
                    opt.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (opt.requiresValue && enabled) {
                    Spacer(Modifier.height(4.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        OutlinedButton(
                            onClick = onEditValue,
                            modifier = Modifier.height(32.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp)
                        ) {
                            Icon(Icons.Default.Edit, null, modifier = Modifier.size(14.dp))
                            Spacer(Modifier.width(4.dp))
                            Text(
                                if (value.isNotBlank()) value else "设置值…",
                                fontSize = 12.sp
                            )
                        }
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "默认: ${if (opt.defaultValue.isNotBlank()) opt.defaultValue else "空"}",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            Spacer(Modifier.width(6.dp))
            Switch(checked = enabled, onCheckedChange = onToggle)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CmdValueDialog(
    opt: CommandLineOption,
    currentValue: String,
    onDismiss: () -> Unit,
    onApply: (String) -> Unit
) {
    var value by remember { mutableStateOf(currentValue) }

    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = { onApply(value.trim()) }) {
                Text("应用", fontWeight = FontWeight.Bold)
            }
        },
        dismissButton = {
            Row {
                if (opt.defaultValue.isNotBlank()) {
                    TextButton(onClick = { value = opt.defaultValue }) { Text("默认") }
                }
                TextButton(onClick = onDismiss) { Text("取消") }
            }
        },
        title = {
            Column {
                Text(opt.displayName, fontWeight = FontWeight.Bold)
                Text(
                    opt.flag,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    opt.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                when (opt.valueHint) {
                    com.srceng.launcher.data.CmdValueType.ENUM -> {
                        val presets = when (opt.id) {
                            "language" -> listOf("english", "schinese", "tchinese", "japanese", "korean", "german", "french", "italian", "spanish", "russian")
                            "mat_antialias" -> listOf("0", "1", "2", "4", "6", "8")
                            "mat_aaquality" -> listOf("0", "1", "2", "3")
                            else -> opt.defaultValue.split(",", " ").map { it.trim() }.filter { it.isNotBlank() }.ifEmpty { listOf(opt.defaultValue) }
                        }
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            presets.forEach { v ->
                                FilterChip(
                                    selected = value == v,
                                    onClick = { value = v },
                                    label = { Text(v.ifBlank { "空" }) }
                                )
                            }
                        }
                        OutlinedTextField(
                            value = value,
                            onValueChange = { value = it },
                            modifier = Modifier.fillMaxWidth(),
                            label = { Text("自定义值 (可选)") },
                            singleLine = true
                        )
                    }
                    com.srceng.launcher.data.CmdValueType.INTEGER -> OutlinedTextField(
                        value = value,
                        onValueChange = { s -> if (s.isEmpty() || s.matches(Regex("^-?\\d*$"))) value = s },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        label = { Text("整数数值") },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        supportingText = { Text("单位说明: ${opt.flag} <int>") }
                    )
                    com.srceng.launcher.data.CmdValueType.STRING -> OutlinedTextField(
                        value = value,
                        onValueChange = { value = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        label = { Text("字符串") }
                    )
                    else -> OutlinedTextField(
                        value = value,
                        onValueChange = { value = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                        label = { Text("数值") }
                    )
                }
            }
        }
    )
}

// ============================================================
// 启动前自检状态面板（HeroCard 下方显示）
// ============================================================

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LauncherDiagnosticsPanel(
    diagnostics: List<GameLauncher.Diagnostic>,
    expanded: Boolean,
    onToggle: () -> Unit
) {
    val okCount = diagnostics.count { it.ok }
    val failCount = diagnostics.size - okCount
    val ok = failCount == 0

    val bg = when {
        ok -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.45f)
        else -> MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.55f)
    }
    val fg = when {
        ok -> MaterialTheme.colorScheme.onPrimaryContainer
        else -> MaterialTheme.colorScheme.onErrorContainer
    }

    ElevatedCard(modifier = Modifier.fillMaxWidth()) {
        Column {
            ListItem(
                colors = ListItemDefaults.colors(containerColor = bg),
                headlineContent = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            if (ok) "启动条件已就绪" else "启动条件未满足",
                            color = fg, fontWeight = FontWeight.Bold
                        )
                        Spacer(Modifier.width(10.dp))
                        AssistChip(
                            onClick = onToggle,
                            label = {
                                Text(
                                    "✅ $okCount   ❌ $failCount",
                                    fontSize = 12.sp,
                                    fontWeight = FontWeight.SemiBold
                                )
                            }
                        )
                    }
                },
                supportingContent = {
                    Text(
                        if (ok) "引擎 native 库、游戏资源目录、入口 Activity 均已就绪" else "点击展开查看具体缺失项，修正后即可启动",
                        color = fg.copy(alpha = 0.85f),
                        style = MaterialTheme.typography.bodySmall
                    )
                },
                leadingContent = {
                    Icon(
                        if (ok) Icons.Default.CheckCircle else Icons.Default.ErrorOutline,
                        null,
                        tint = fg
                    )
                },
                trailingContent = {
                    IconButton(onClick = onToggle) {
                        Icon(
                            if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                            null,
                            tint = fg
                        )
                    }
                }
            )
            if (expanded) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 14.dp, end = 14.dp, bottom = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    diagnostics.forEach { d ->
                        DiagnosticRow(d)
                    }
                }
            }
        }
    }
}

@Composable
private fun DiagnosticRow(d: GameLauncher.Diagnostic) {
    val container = if (d.ok)
        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    else
        MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.22f)
    val onContainer = if (d.ok)
        MaterialTheme.colorScheme.onSurfaceVariant
    else
        MaterialTheme.colorScheme.onErrorContainer

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(container)
            .padding(10.dp),
        verticalAlignment = Alignment.Top
    ) {
        Icon(
            imageVector = if (d.ok) Icons.Default.TaskAlt else Icons.Default.WarningAmber,
            contentDescription = null,
            tint = if (d.ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
            modifier = Modifier
                .padding(top = 1.dp)
                .size(18.dp)
        )
        Spacer(Modifier.width(10.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                d.title,
                color = onContainer,
                fontWeight = FontWeight.SemiBold,
                style = MaterialTheme.typography.titleSmall
            )
            Spacer(Modifier.height(2.dp))
            Text(
                d.detail,
                color = onContainer.copy(alpha = 0.85f),
                style = MaterialTheme.typography.bodySmall.copy(lineHeight = 16.sp)
            )
        }
    }
}

// ============================================================
// 启动失败/条件未满足 —— 详细对话框
// ============================================================

@Composable
private fun LaunchFailureDialog(
    result: GameLauncher.LaunchFlowResult,
    onDismiss: () -> Unit,
    onRetry: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Default.Info,
                    null,
                    tint = MaterialTheme.colorScheme.primary
                )
                Spacer(Modifier.width(10.dp))
                Text(
                    if (!result.canLaunch) "启动条件未满足" else "启动失败",
                    fontWeight = FontWeight.Bold
                )
            }
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                if (result.diagnostics.isNotEmpty()) {
                    Text("自检结果：", fontWeight = FontWeight.SemiBold)
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        result.diagnostics.forEach { DiagnosticRow(it) }
                    }
                } else {
                    Text("无诊断细节。", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                HorizontalDivider()
                Text("写入结果：", fontWeight = FontWeight.SemiBold)
                Column {
                    Text(
                        text = when {
                            result.autoexec?.success == true -> "✅ autoexec.cfg → ${result.autoexec.autoexecPath}"
                            result.autoexec != null -> "❌ ${result.autoexec.message}"
                            else -> "— autoexec.cfg（未写入）"
                        },
                        style = MaterialTheme.typography.bodySmall
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = when {
                            result.argsFile?.success == true -> "✅ launch-args.txt → ${result.argsFile.argsFile}"
                            result.argsFile != null -> "❌ ${result.argsFile.message}"
                            else -> "— launch-args.txt（未写入）"
                        },
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                result.launch?.let { l ->
                    HorizontalDivider()
                    Text("启动结果：", fontWeight = FontWeight.SemiBold)
                    Text(
                        l.message.ifBlank { "(无详细信息)" },
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        },
        confirmButton = {
            Row {
                if (result.canLaunch) {
                    TextButton(onClick = onRetry) {
                        Text("重试", fontWeight = FontWeight.Bold)
                    }
                }
                TextButton(onClick = onDismiss) {
                    Text(
                        if (result.canLaunch) "关闭" else "知道了",
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        }
    )
}
