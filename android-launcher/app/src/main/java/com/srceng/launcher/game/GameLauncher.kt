package com.srceng.launcher.game

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.util.Log
import com.srceng.launcher.data.CVar
import java.io.File
import java.io.IOException

/**
 * 游戏启动/配置管理器
 *
 * 本类负责：
 * - 写入 autoexec.cfg 到游戏目录
 * - 构造启动 Intent（根据不同的引擎入口 Activity）
 * - 校验资源目录（gameinfo.txt 等）
 */
object GameLauncher {
    private const val TAG = "GameLauncher"

    // 候选的入口 Activity 包名/类名（根据打包方式可调整）
    private val CANDIDATE_LAUNCHERS = listOf(
        // 原版 nillerusr 风格
        "org.nillerusr.srcds/.MainActivity",
        "org.nillerusr/.Launcher",
        "com.valvesoftware.source/.SourceActivity",
        // 默认 fallback (启动器自身预留占位)
        "com.srceng.engine/.EngineActivity"
    )

    data class PrepareResult(
        val success: Boolean,
        val message: String,
        val launchArgs: String,
        val autoexecPath: String? = null
    )

    /**
     * 写 autoexec.cfg 到指定 mod 目录
     */
    fun writeAutoExec(
        context: Context,
        gameDir: String,
        mod: String,
        cvars: List<CVar>,
        extraContent: String = ""
    ): PrepareResult {
        val dir = resolveModDir(gameDir, mod)
        if (dir == null) {
            return PrepareResult(
                false,
                "模组目录不存在，请先选择正确的游戏根目录。\n需要包含 hl2/, platform/, bin/ 等文件夹。",
                ""
            )
        }
        val cfgDir = File(dir, "cfg").apply { mkdirs() }
        if (!cfgDir.exists() || !cfgDir.isDirectory) {
            return PrepareResult(false, "无法写入到 ${cfgDir.absolutePath}", "")
        }
        val out = File(cfgDir, "autoexec.cfg")
        val header =
            "// ===== 由 Source Engine Launcher 生成 - 启动时自动执行 =====\n" +
                    "// 若要保留自定义内容，请修改 userconfig.cfg 而非本文件\n\n"
        val cvarLines = cvars.filter { it.isModified }.joinToString("\n") {
            "${it.name} ${it.currentValue}"
        }
        val content = listOf(header, cvarLines, "\n", extraContent, "\n").joinToString("")
        return try {
            out.writeText(content)
            PrepareResult(true, "已写入 ${out.absolutePath}", "", out.absolutePath)
        } catch (e: IOException) {
            PrepareResult(false, "写入失败: ${e.message}", "")
        }
    }

    /**
     * 解析模组目录
     */
    private fun resolveModDir(gameDir: String, mod: String): File? {
        val root = when {
            gameDir.isNotBlank() -> File(gameDir)
            else -> File(
                Environment.getExternalStorageDirectory(),
                "Android/data/com.srceng.launcher/files/source"
            )
        }
        val modDir = File(root, mod)
        return if (modDir.exists() && modDir.isDirectory) modDir else null
    }

    /**
     * 启动游戏（尝试多个候选入口）
     */
    fun launchGame(
        context: Context,
        args: String,
        extraFlags: Int = 0
    ): PrepareResult {
        val launchIntentArgs: ArrayList<String> = ArrayList(args.split(" ").filter { it.isNotBlank() })

        var lastError: Throwable? = null
        for (candidate in CANDIDATE_LAUNCHERS) {
            try {
                val (pkg, cls) = candidate.split("/")
                val intent = Intent().apply {
                    setClassName(pkg, cls)
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or extraFlags)
                    putStringArrayListExtra("args", launchIntentArgs)
                    data = Uri.parse("srceng://launch?args=${Uri.encode(args)}")
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    intent.addFlags(Intent.FLAG_ACTIVITY_LAUNCH_ADJACENT)
                }
                context.startActivity(intent)
                return PrepareResult(true, "已启动 $pkg/$cls", args)
            } catch (t: Throwable) {
                lastError = t
                Log.w(TAG, "启动失败 $candidate: ${t.message}")
            }
        }

        return PrepareResult(
            success = false,
            message = "未找到可启动的引擎 Activity。请确认：\n" +
                    "1) 已安装/编译 Source Engine 客户端 APK\n" +
                    "2) 包名/入口匹配以下任一项：\n  ${CANDIDATE_LAUNCHERS.joinToString("\n  ")}\n\n" +
                    "最后错误: ${lastError?.message ?: "未知"}",
            launchArgs = args
        )
    }
}
