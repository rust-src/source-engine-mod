package com.srceng.launcher.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.srceng.launcher.LauncherViewModel
import com.srceng.launcher.ui.screens.AboutScreen
import com.srceng.launcher.ui.screens.CVarScreen
import com.srceng.launcher.ui.screens.HomeScreen
import com.srceng.launcher.ui.screens.SettingsScreen

sealed class Screen(val route: String, val label: String, val icon: ImageVector) {
    data object Home : Screen("home", "主页", Icons.Default.SportsEsports)
    data object Settings : Screen("settings", "设置", Icons.Default.Settings)
    data object CVars : Screen("cvars", "CVars", Icons.Default.Tune)
    data object About : Screen("about", "关于", Icons.Default.Info)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LauncherApp(viewModel: LauncherViewModel) {
    val navController = rememberNavController()
    val snackbarHostState = remember { SnackbarHostState() }

    Scaffold(
        bottomBar = { LauncherBottomNav(navController) },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Home.route,
            modifier = Modifier.padding(padding)
        ) {
            composable(Screen.Home.route) { HomeScreen(viewModel, navController, snackbarHostState) }
            composable(Screen.Settings.route) { SettingsScreen(viewModel) }
            composable(Screen.CVars.route) { CVarScreen(viewModel) }
            composable(Screen.About.route) { AboutScreen() }
        }
    }
}

@Composable
fun LauncherBottomNav(navController: NavHostController) {
    val screens = listOf(
        Screen.Home,
        Screen.CVars,
        Screen.Settings,
        Screen.About
    )

    NavigationBar {
        val navBackStackEntry by navController.currentBackStackEntryAsState()
        val currentDestination = navBackStackEntry?.destination

        screens.forEach { screen ->
            NavigationBarItem(
                icon = {
                    Icon(
                        imageVector = screen.icon,
                        contentDescription = screen.label
                    )
                },
                label = {
                    Text(
                        text = screen.label,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                selected = currentDestination?.hierarchy?.any { it.route == screen.route } == true,
                onClick = {
                    navController.navigate(screen.route) {
                        popUpTo(navController.graph.findStartDestination().id) {
                            saveState = true
                        }
                        launchSingleTop = true
                        restoreState = true
                    }
                }
            )
        }
    }
}
