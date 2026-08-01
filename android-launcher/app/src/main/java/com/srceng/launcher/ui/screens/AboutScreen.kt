package com.srceng.launcher.ui.screens

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen() {
    val ctx = LocalContext.current
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
    ) {
        LargeTopAppBar(
            title = { Text("关于", fontWeight = FontWeight.Bold) }
        )
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // App banner
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(20.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Surface(
                            shape = Shapes().extraLarge,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(64.dp)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    Icons.Default.SportsEsports, null,
                                    tint = MaterialTheme.colorScheme.onPrimary,
                                    modifier = Modifier.size(32.dp)
                                )
                            }
                        }
                        Spacer(Modifier.width(16.dp))
                        Column {
                            Text(
                                "Source Engine Launcher",
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                "版本 1.0.0 (1)",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    Spacer(Modifier.height(14.dp))
                    Text(
                        "现代、开源的 Source Engine 移动端启动器，为 ARM64 / WoA 优化。" +
                            "支持自定义启动参数、CVar 管理、Material Design 3 动态主题与深浅色切换。",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }

            // Links cards
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(8.dp)) {
                    AboutRow(
                        icon = Icons.Default.Code,
                        title = "源代码仓库",
                        subtitle = "GitHub / stephen-cusi/source-engine-mod",
                        onClick = {
                            openUrl(ctx, "https://github.com/stephen-cusi/source-engine-mod")
                        }
                    )
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 8.dp))
                    AboutRow(
                        icon = Icons.Default.Smartphone,
                        title = "Android 启动器参考",
                        subtitle = "SourceEngineAndroid-Launcher",
                        onClick = {
                            openUrl(ctx, "https://github.com/stephen-cusi/SourceEngineAndroid-Launcher")
                        }
                    )
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 8.dp))
                    AboutRow(
                        icon = Icons.Default.Description,
                        title = "Valve 开发者社区 (VDC)",
                        subtitle = "Source Engine 官方文档",
                        onClick = {
                            openUrl(ctx, "https://developer.valvesoftware.com/wiki/Main_Page")
                        }
                    )
                }
            }

            // Credits
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("致谢", fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "• Source Engine © Valve Corporation\n" +
                                "• nillerusr 及其 Source SDK 开源移植\n" +
                                "• ItzVladik / LostGamer 等 Android 移植作者\n" +
                                "• PojavLauncher 设计灵感 (UI/UX)\n" +
                                "• Jetpack Compose · Material Design 3\n" +
                                "• 所有开源贡献者 ❤",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }

            // License
            ElevatedCard(modifier = Modifier.fillMaxWidth()) {
                ListItem(
                    headlineContent = { Text("开源协议", fontWeight = FontWeight.Bold) },
                    supportingContent = { Text("本项目遵循上游 Source Engine 相关协议，启动器代码遵循 MIT。") },
                    leadingContent = { Icon(Icons.Default.Gavel, null) }
                )
            }

            Text(
                "Made with ❤ using Jetpack Compose & Kotlin",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.align(Alignment.CenterHorizontally)
            )
        }
    }
}

@Composable
private fun AboutRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    subtitle: String? = null,
    onClick: () -> Unit
) {
    ListItem(
        headlineContent = { Text(title, fontWeight = FontWeight.Medium) },
        supportingContent = { subtitle?.let { Text(it) } },
        leadingContent = { Icon(icon, null, tint = MaterialTheme.colorScheme.primary) },
        trailingContent = { Icon(Icons.Default.OpenInNew, null) },
        modifier = Modifier.clickable { onClick() }
    )
}

private fun openUrl(ctx: android.content.Context, url: String) {
    runCatching {
        ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
    }
}
