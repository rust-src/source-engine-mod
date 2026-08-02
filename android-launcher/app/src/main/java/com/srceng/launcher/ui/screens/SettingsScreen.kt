package com.srceng.launcher.ui.screens

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.srceng.launcher.LauncherViewModel
import com.srceng.launcher.data.LauncherConfig
import com.srceng.launcher.ui.theme.ThemeMode

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(vm: LauncherViewModel) {
    val config by vm.config.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(bottom = 24.dp)
    ) {
        // Header
        LargeTopAppBar(
            title = { Text("设置", fontWeight = FontWeight.Bold) },
            colors = TopAppBarDefaults.largeTopAppBarColors(
                containerColor = Color.Transparent
            )
        )

        Column(
            modifier = Modifier.padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(18.dp)
        ) {
            // === 外观 ===
            SectionTitle("外观", Icons.Default.Palette)
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    // Theme mode
                    Text("主题模式", fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(8.dp))
                    val modes = listOf(
                        ThemeMode.SYSTEM to "跟随系统",
                        ThemeMode.LIGHT to "浅色",
                        ThemeMode.DARK to "深色"
                    )
                    ModeSegmentedButton(
                        selected = config.themeMode,
                        options = modes,
                        onSelect = { mode ->
                            vm.updateConfig { it.copy(themeMode = mode) }
                        }
                    )

                    Spacer(Modifier.height(16.dp))
                    Divider()
                    Spacer(Modifier.height(10.dp))

                    SwitchRow(
                        title = "使用动态取色 (Material You)",
                        subtitle = "基于壁纸和系统色板生成主题",
                        checked = config.useDynamicColor,
                        onCheckedChange = {
                            vm.updateConfig { c -> c.copy(useDynamicColor = it) }
                        }
                    )
                }
            }

            // === 其他 ===
            SectionTitle("其他", Icons.Default.SettingsApplications)
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    SwitchRow(
                        "调试模式 (-dev)",
                        subtitle = "启用调试输出与日志",
                        checked = config.debugMode,
                        onCheckedChange = { vm.updateConfig { c -> c.copy(debugMode = it) } }
                    )
                    SwitchRow(
                        "显示控制台",
                        subtitle = "运行时显示引擎控制台",
                        checked = config.showConsole,
                        onCheckedChange = { vm.updateConfig { c -> c.copy(showConsole = it) } }
                    )
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = { /* TODO: 重置 */ },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Default.Restore, null)
                        Spacer(Modifier.width(8.dp))
                        Text("恢复默认设置")
                    }
                }
            }

            Spacer(Modifier.height(16.dp))
            Text(
                text = "💡 命令行参数在首页设置。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

// ====== 通用组件 ======

@Composable
private fun SectionTitle(text: String, icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.padding(start = 4.dp, top = 4.dp, bottom = 2.dp)
    ) {
        Icon(
            icon, null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(22.dp)
        )
        Spacer(Modifier.width(8.dp))
        Text(
            text,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.primary
        )
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
            .padding(vertical = 4.dp),
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

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun <T> ModeSegmentedButton(
    selected: T,
    options: List<Pair<T, String>>,
    onSelect: (T) -> Unit
) {
    // 旧实现 SingleChoiceSegmentedButtonRow + SegmentedButton 在窄屏/选项较多(如线程数 0..8)时，
    // Material3 会在选中项绘制一个 "对勾 leadingIcon"，与标签文本叠加导致绘制错乱，
    // 出现截图中「一个 Chip 被画两次」的视觉错位。改用横向可滚动的 FilterChip，
    // 选中时不会额外插入 leading icon，且选项多时仍可左右滑动正常浏览。
    val scrollState = rememberScrollState()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(scrollState),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        options.forEach { (value, label) ->
            val sel = value == selected
            FilterChip(
                selected = sel,
                onClick = { if (!sel) onSelect(value) },
                label = {
                    Text(
                        text = label,
                        fontWeight = if (sel) FontWeight.SemiBold else FontWeight.Normal
                    )
                }
            )
        }
    }
}

@Composable
private fun LabeledSlider(
    label: String,
    value: Float,
    min: Float = 0f,
    max: Float = 1f,
    steps: Int = 0,
    format: (Float) -> String = { String.format("%.2f", it) },
    onValueChange: (Float) -> Unit
) {
    Column(modifier = Modifier.padding(vertical = 6.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, style = MaterialTheme.typography.titleSmall)
            Text(
                format(value),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = min..max,
            steps = steps
        )
    }
}
