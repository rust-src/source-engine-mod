package com.srceng.launcher

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import com.srceng.launcher.ui.LauncherApp
import com.srceng.launcher.ui.theme.SourceEngineLauncherTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val vm: LauncherViewModel = viewModel()
            val cfg by vm.config.collectAsState()

            SourceEngineLauncherTheme(
                themeMode = cfg.themeMode,
                dynamicColor = cfg.useDynamicColor
            ) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    LauncherApp(vm)
                }
            }
        }
    }
}
