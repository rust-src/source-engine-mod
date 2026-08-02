package com.nillerusr;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * 从 APK assets 中提取 extras_dir.vpk 和字体文件到 filesDir。
 * 参考原型 SourceEngineAndroid-Launcher / srceng-launcher_cn。
 *
 * ValveActivity2.initNatives() 会在启动前调用 extractVPK()，
 * 确保引擎运行时能通过 EXTRAS_VPK_PATH 环境变量找到 VPK。
 */
public class ExtractAssets {
    private static final String TAG = "ExtractAssets";
    private static final String PREF_NAME = "srceng_launcher";
    public static final String VPK_NAME = "extras_dir.vpk";
    public static final int PAK_VERSION = 9;

    public static void extractVPK(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        FileOutputStream os = null;
        try {
            File file = new File(context.getFilesDir(), VPK_NAME);
            if (prefs.getInt("pakversion", 0) == PAK_VERSION && file.exists())
                return;

            InputStream is = context.getAssets().open(VPK_NAME);
            os = new FileOutputStream(file);
            byte[] buffer = new byte[8192];
            while (true) {
                int length = is.read(buffer);
                if (length <= 0) break;
                os.write(buffer, 0, length);
            }
            os.close();
            os = null;

            prefs.edit().putInt("pakversion", PAK_VERSION).apply();

            // chmod 确保引擎可读
            file.setReadable(true, false);
            file.setWritable(true, false);
            context.getFilesDir().setReadable(true, false);
            context.getFilesDir().setWritable(true, false);
        } catch (Exception e) {
            Log.e(TAG, "extractVPK 失败: " + e.getMessage());
        } finally {
            if (os != null) {
                try { os.close(); } catch (Exception ignored) {}
            }
        }
    }
}