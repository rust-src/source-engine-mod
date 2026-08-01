# Keep JNI classes
-keep class com.valvesoftware.ValveActivity2 { *; }
-keep class com.valvesoftware.LauncherActivity { *; }
-keep class com.valvesoftware.SettingsActivity { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep SDL classes
-keep class org.libsdl.** { *; }
