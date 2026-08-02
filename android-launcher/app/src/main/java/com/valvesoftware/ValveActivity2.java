package com.valvesoftware;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.SharedPreferences;
import android.util.Log;

import java.util.Locale;

/**
 * Source Engine native bridge（参考两个原型：
 *   SourceEngineAndroid-Launcher  /  srceng-launcher_cn
 * ）
 *
 * liblauncher.so 会通过 JNI 注册本类里的两个 native 方法：
 *   public static native void setArgs(String args);
 *   public static native int  setenv(String name, String value, int overwrite);
 *
 * 调用顺序（见 SDLActivity.onCreate）：
 *   1. loadLibraries() 加载 libSDL2.so → libtier0.so → libvstdlib.so → liblauncher.so
 *   2. setupJNI() / SDL.initialize()
 *   3. ValveActivity2.initNatives(context, intent)   ← 你现在看到的入口
 */
public class ValveActivity2 {

    private static final String TAG = "ValveActivity2";
    private static final String PREF_NAME = "srceng_launcher";

    // JNI 入口（由 native 层在 System.loadLibrary("launcher") 时注册）
    public static native void setArgs(String args);
    public static native int setenv(String name, String value, int overwrite);

    /**
     * 给 SDLActivity.onCreate 调用：
     *   - 从 Intent extras / SharedPreferences 拿到 gamepath / gamedir / argv
     *   - setenv 写入引擎需要的环境变量
     *   - 拼接最终 argv（"-game " + gamedir + " " + user_argv）后 setArgs 交给 native
     */
    public static void initNatives(Context context, Intent intent) {
        try {
            ApplicationInfo appInfo = context.getApplicationInfo();
            SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);

            // ---- 1. gamepath（VALVE_GAME_PATH） ----
            String gamepath = null;
            if (intent != null) {
                gamepath = intent.getStringExtra("gamepath");
            }
            if (gamepath == null || gamepath.isEmpty()) {
                gamepath = prefs.getString("gamepath", "");
            }
            if (gamepath == null || gamepath.isEmpty()) {
                // 最后的兜底：用外部存储根目录（但大概率不对，仅防崩溃）
                gamepath = context.getExternalFilesDir(null) != null
                        ? context.getExternalFilesDir(null).getAbsolutePath()
                        : context.getFilesDir().getAbsolutePath();
            }

            // ---- 2. gamedir（mod 子目录，默认 hl2） ----
            String gamedir = (intent != null) ? intent.getStringExtra("gamedir") : null;
            if (gamedir == null || gamedir.isEmpty()) {
                gamedir = prefs.getString("gamedir", "hl2");
            }
            if (gamedir == null || gamedir.isEmpty()) {
                gamedir = "hl2";
            }

            // ---- 3. argv（用户自定义启动参数） ----
            String argv = (intent != null) ? intent.getStringExtra("argv") : null;
            if (argv == null || argv.isEmpty()) {
                argv = prefs.getString("argv", "");
            }
            if (argv == null) argv = "";

            // ---- 4. 写入环境变量（严格照原型 srceng-launcher_cn） ----
            String filesDir = context.getFilesDir().getAbsolutePath();
            // extras_dir.vpk：若启动器未内嵌此 vpk，设为空字符串也不会崩
            String vpkPath = filesDir + "/extras_dir.vpk";

            String gamelibdir = (intent != null) ? intent.getStringExtra("gamelibdir") : null;
            if (gamelibdir != null && !gamelibdir.isEmpty()) {
                safeSetenv("APP_MOD_LIB", gamelibdir);
            } else {
                safeSetenv("APP_MOD_LIB", appInfo.nativeLibraryDir);
            }
            safeSetenv("EXTRAS_VPK_PATH", vpkPath);
            safeSetenv("LANG", Locale.getDefault().toString());
            safeSetenv("APP_DATA_PATH", appInfo.dataDir);
            safeSetenv("APP_LIB_PATH", appInfo.nativeLibraryDir);
            safeSetenv("VALVE_GAME_PATH", gamepath);

            // ---- 5. 最终 argv：完全按用户给出的字符串透传
            // 参考 SourceEngineAndroid-Launcher 原型：setArgs(MainActivity.profile.getGameCmdVar())
            // 不自动 prepend "-game <gamedir>"，用户在自定义参数里自己需要时自己加
            String finalArgv = (argv != null) ? argv : "";
            Log.i(TAG, "setenv VALVE_GAME_PATH=" + gamepath);
            Log.i(TAG, "setArgs: [" + finalArgv + "]");
            try {
                setArgs(finalArgv);
            } catch (UnsatisfiedLinkError ule) {
                Log.e(TAG, "setArgs 未注册（liblauncher.so 未加载或 JNI 符号缺失）: " + ule.getMessage());
            }
        } catch (Throwable t) {
            Log.e(TAG, "initNatives 失败", t);
        }
    }

    private static void safeSetenv(String name, String value) {
        try {
            setenv(name, value, 1);
        } catch (UnsatisfiedLinkError ule) {
            Log.w(TAG, "setenv 未注册（liblauncher.so 的 JNI 未就绪）: " + name + "=" + value);
        } catch (Throwable t) {
            Log.w(TAG, "setenv 失败: " + name + "=" + value, t);
        }
    }
}
