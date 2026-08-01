package com.srceng.launcher.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

enum class ThemeMode {
    SYSTEM, LIGHT, DARK
}

private val LightColorScheme = lightColorScheme(
    primary = Color(0xFFFF6D00),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFDCC2),
    onPrimaryContainer = Color(0xFF3B1300),
    secondary = Color(0xFF775647),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFFFDCC9),
    onSecondaryContainer = Color(0xFF2C1509),
    tertiary = Color(0xFF665E2F),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFEEE3A7),
    onTertiaryContainer = Color(0xFF1F1B00),
    error = Color(0xFFBA1A1A),
    errorContainer = Color(0xFFFFDAD6),
    onError = Color(0xFFFFFFFF),
    onErrorContainer = Color(0xFF410002),
    background = Color(0xFFFFFBFF),
    onBackground = Color(0xFF201A17),
    surface = Color(0xFFFFFBFF),
    onSurface = Color(0xFF201A17),
    surfaceVariant = Color(0xFFF4DED3),
    onSurfaceVariant = Color(0xFF53433B),
    outline = Color(0xFF85736A),
    inverseOnSurface = Color(0xFFFBEEDE),
    inverseSurface = Color(0xFF362F2B),
    inversePrimary = Color(0xFFFFB785),
    surfaceTint = Color(0xFFFF6D00),
    outlineVariant = Color(0xFFD7C2B8),
    scrim = Color(0xFF000000),
)

private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFFFFB785),
    onPrimary = Color(0xFF5F2300),
    primaryContainer = Color(0xFF853400),
    onPrimaryContainer = Color(0xFFFFDCC2),
    secondary = Color(0xFFE7BFA9),
    onSecondary = Color(0xFF452A1C),
    secondaryContainer = Color(0xFF5E3F31),
    onSecondaryContainer = Color(0xFFFFDCC9),
    tertiary = Color(0xFFD1C78D),
    onTertiary = Color(0xFF373007),
    tertiaryContainer = Color(0xFF4E461B),
    onTertiaryContainer = Color(0xFFEEE3A7),
    error = Color(0xFFFFB4AB),
    errorContainer = Color(0xFF93000A),
    onError = Color(0xFF690005),
    onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF201A17),
    onBackground = Color(0xFFEDE0D4),
    surface = Color(0xFF201A17),
    onSurface = Color(0xFFEDE0D4),
    surfaceVariant = Color(0xFF53433B),
    onSurfaceVariant = Color(0xFFD7C2B8),
    outline = Color(0xFFA08C82),
    inverseOnSurface = Color(0xFF201A17),
    inverseSurface = Color(0xFFEDE0D4),
    inversePrimary = Color(0xFFFF6D00),
    surfaceTint = Color(0xFFFFB785),
    outlineVariant = Color(0xFF53433B),
    scrim = Color(0xFF000000),
)

@Composable
fun SourceEngineLauncherTheme(
    themeMode: ThemeMode = ThemeMode.SYSTEM,
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit
) {
    val darkTheme = when (themeMode) {
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
    }

    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (darkTheme) dynamicDarkColorScheme(context)
            else dynamicLightColorScheme(context)
        }
        darkTheme -> DarkColorScheme
        else -> LightColorScheme
    }

    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as android.app.Activity).window
            window.statusBarColor = colorScheme.primary.toArgb()
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = !darkTheme
        }
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography(),
        shapes = Shapes(),
        content = content
    )
}
