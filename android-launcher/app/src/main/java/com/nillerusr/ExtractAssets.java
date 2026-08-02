package com.nillerusr;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.reflect.Method;

/**
 * 从 APK assets 中提取 extras_dir.vpk 和字体文件到 filesDir。
 *
 * 实现严格对齐两个原型：
 *   SourceEngineAndroid-Launcher / app/src/main/java/com/nillerusr/ExtractAssets.java
 *   srceng-launcher_cn        / src/me/nillerusr/ExtractAssets.java
 *
 * 关键点：
 *  1. SharedPreferences 名固定为 "mod"（与 native 侧 UpdateSystem 约定）
 *  2. 先 chmod(dataDir, 0777) + chmod(filesDir, 0777)，否则引擎读不到
 *  3. 提取 extras_dir.vpk 之外，还要提取 7 个字体文件（缺少字体导致引擎 UI 文字缺字/崩溃）
 *  4. 每个提取出的文件再单独 chmod 0777 / 511
 *  5. chmod 走 Runtime.exec("chmod OCTAL path") + 反射 android.os.FileUtils.setPermissions 双通道
 */
public class ExtractAssets {
    public static String TAG = "ExtractAssets";
    static SharedPreferences mPref;

    public static final String VPK_NAME = "extras_dir.vpk";
    public static final int PAK_VERSION = 9;

    // ===== 必须提取的字体文件列表（SourceEngineAndroid-Launcher 原型 extractAssets 方法） =====
    private static final String[] FONT_ASSETS = new String[] {
            "DroidSansFallback.ttf",
            "LiberationMono-Regular.ttf",
            "dejavusans-boldoblique.ttf",
            "dejavusans-bold.ttf",
            "dejavusans-oblique.ttf",
            "dejavusans.ttf",
            "Itim-Regular.otf",
            "jf-openhuninn-2.0.ttf"
    };

    /**
     * 双通道 chmod：先用 Runtime.exec，失败再走 android.os.FileUtils 反射。
     * 与两个原型完全相同的实现（SE 的 chmod 多了一层 try 顺序，但核心一样）。
     */
    private static int chmod(String path, int mode) {
        int ret = -1;
        try {
            ret = Runtime.getRuntime().exec("chmod " + Integer.toOctalString(mode) + " " + path).waitFor();
            Log.d(TAG, "chmod " + Integer.toOctalString(mode) + " " + path + ": " + ret);
        } catch (Exception e) {
            ret = -1;
            Log.d(TAG, "chmod: Runtime not worked: " + e.toString());
        }
        try {
            Class<?> fileUtils = Class.forName("android.os.FileUtils");
            Method setPermissions = fileUtils.getMethod("setPermissions", String.class, int.class, int.class, int.class);
            ret = (Integer) setPermissions.invoke(null, path, mode, -1, -1);
        } catch (Exception e) {
            Log.d(TAG, "chmod: FileUtils not worked: " + e.toString());
        }
        return ret;
    }

    /**
     * 与 SourceEngineAndroid-Launcher 原型对齐的 extractVPK(Context, Boolean force)。
     * 额外会 chmod dataDir、filesDir 为 0777。
     */
    public static void extractVPK(Context context, Boolean force) {
        ApplicationInfo appinf = context.getApplicationInfo();
        FileOutputStream os = null;
        try {
            if (mPref == null)
                mPref = context.getSharedPreferences("mod", 0);

            File file = new File(context.getFilesDir().getPath() + "/" + VPK_NAME);
            if (!file.exists())
                force = true;

            if (mPref.getInt("pakversion", 0) == PAK_VERSION && !force)
                return;

            InputStream is = context.getAssets().open(VPK_NAME);
            os = new FileOutputStream(context.getFilesDir().getPath() + "/" + VPK_NAME);
            byte[] buffer = new byte[8192];
            while (true) {
                int length = is.read(buffer);
                if (length <= 0)
                    break;
                os.write(buffer, 0, length);
            }
            os.close();
            os = null;

            SharedPreferences.Editor editor = mPref.edit();
            editor.putInt("pakversion", PAK_VERSION);
            editor.commit();

            // 三个路径全部 0777（srceng-launcher_cn 精确顺序）
            chmod(appinf.dataDir, 0777);
            chmod(context.getFilesDir().getPath(), 0777);
            chmod(context.getFilesDir().getPath() + "/" + VPK_NAME, 0777);
        } catch (Exception e) {
            Log.e("SRCAPK", "Failed to extract vpk:" + e.toString());
        } finally {
            if (os != null) {
                try { os.close(); } catch (Exception ignored) {}
            }
        }
    }

    /**
     * 提取单个资产文件（写 tmp + rename 的原子写法，与 SE 原型一致），
     * 写完 chmod 为 511 (0777)。
     */
    public static void extractAsset(Context context, String str, Boolean bool) {
        AssetManager am = context.getAssets();
        try {
            File file = new File(context.getFilesDir().getPath() + "/" + str);
            Boolean valueOf = file.exists();
            if (bool || !valueOf) {
                InputStream open = am.open(str);
                FileOutputStream file2 = new FileOutputStream(context.getFilesDir().getPath() + "/tmp");
                byte[] bArr = new byte[8192];
                while (true) {
                    int read = open.read(bArr);
                    if (read <= 0) break;
                    file2.write(bArr, 0, read);
                }
                file2.close();
                File file3 = new File(context.getFilesDir().getPath() + "/tmp");
                if (valueOf) file.delete();
                file3.renameTo(new File(context.getFilesDir().getPath() + "/" + str));
                chmod(context.getFilesDir().getPath() + "/" + str, 511);
            }
        } catch (Exception e) {
            Log.e("SRCAPK", "Failed to extract asset " + str + ": " + e.toString());
        }
    }

    /**
     * SourceEngineAndroid-Launcher 的单参数无参版 extractVPK。
     * 读取 SharedPreferences("mod") 中的 pakversion 判断是否 force 为 true。
     */
    public static void extractVPK(Context context) {
        boolean z = false;
        if (mPref == null) {
            mPref = context.getSharedPreferences("mod", 0);
        }
        if (mPref.getInt("pakversion", 0) != PAK_VERSION) {
            z = true;
        }
        extractAsset(context, VPK_NAME, z);
        SharedPreferences.Editor editor = mPref.edit();
        editor.putInt("pakversion", PAK_VERSION);
        editor.commit();
    }

    /**
     * SourceEngineAndroid-Launcher 主入口：
     *   1) chmod dataDir / filesDir 511 (0777)
     *   2) extractVPK() —— 只在版本变了时重提 VPK
     *   3) 逐字体 extractAsset(force=false) —— 不存在才提
     */
    public static void extractAssets(Context context) {
        chmod(context.getApplicationInfo().dataDir, 511);
        chmod(context.getFilesDir().getPath(), 511);
        extractVPK(context);
        for (String font : FONT_ASSETS) {
            extractAsset(context, font, false);
        }
        // 最后确认 files 目录权限（提完文件再 chmod 一次以防万一）
        chmod(context.getFilesDir().getPath(), 0777);
    }
}
