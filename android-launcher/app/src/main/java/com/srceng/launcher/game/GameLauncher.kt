package com.srceng.launcher.game

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
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
 * 参考 https://github.com/stephen-cusi/SourceEngineAndroid-Launcher
 *
 * 本类负责：
 *  1. checkPrerequisites() —— 启动前自检（so 是否存在、gameinfo.txt 是否可读、目录权限）
 *  2. writeAutoExec() —— 把 CVar + 配置写入 <mod>/cfg/autoexec.cfg
 *  3. writeLaunchArgs() —— 把命令行 args 写入 <filesDir>/launch-args.txt（引擎启动时读取）
 *  4. launchGame() —— 组装 Intent 启动 org.libsdl.app.SDLActivity
 *  5. prepareAndLaunch() —— 一键串起 1~4，返回检查失败/写入失败信息（UI 用）
 *
 * SDLActivity 是由编译好的 libSDL2.so + 引擎 so 提供的 Java 类。
 * 在 lib 还没编译出来的阶段，我们会在 UI 上给出明确的“引擎尚未编译”提示，不会直接崩溃。
 */
object GameLauncher {
    private const val TAG = "GameLauncher"

    /**
     * 引擎入口 Activity（与原型 SourceEngineAndroid-Launcher 保持一致）。
     * SDLActivity 本身由 SDL Java 层提供，真正的引擎逻辑在 native library 里加载。
     */
    const val SDL_ACTIVITY_CLASS = "org.libsdl.app.SDLActivity"

    /** 必须存在的 native 库（打包在 APK libs/<abi>/ 目录下，来自 CI build-android-aarch64/armv7a 产物） */
    private val REQUIRED_LIBS = listOf(
        "SDL2",              // libSDL2.so：SDL 入口/Java 层 JNI
        "tier0",             // libtier0.so：Source 基础库（内存、线程、platform）
        "vstdlib",           // libvstdlib.so：Source 标准库
        "launcher",          // liblauncher.so：引擎启动器（含 main/launcher）
        "engine"             // libengine.so：Source 引擎核心
    )

    /** launch-args.txt（写入 Context.filesDir，SDL 的 Java helper 启动时读本文件拼成 argv） */
    private const val LAUNCH_ARGS_FILENAME = "launch-args.txt"

    /** 快速自检的结果（供 UI 渲染提示条） */
    data class Diagnostic(
        val ok: Boolean,
        val title: String,
        val detail: String
    )

    data class PrepareResult(
        val success: Boolean,
        val message: String,
        val launchArgs: String,
        val autoexecPath: String? = null,
        val argsFile: String? = null,
        val diagnostics: List<Diagnostic> = emptyList()
    )

    // ============================================================
    //  启动前自检
    // ============================================================

    fun runDiagnostics(context: Context, gameDir: String, mod: String): List<Diagnostic> {
        val out = mutableListOf<Diagnostic>()

        // 1) 引擎 native libs 是否已打包进 APK
        val missingLibs = REQUIRED_LIBS.filter { !nativeLibExists(context, it) }
        if (missingLibs.isEmpty()) {
            out += Diagnostic(true, "引擎 Native 库", "已打包：lib${REQUIRED_LIBS.joinToString(".so、lib") { it }}.so")
        } else {
            out += Diagnostic(
                false,
                "引擎 Native 库尚未编译",
                "缺少 lib${missingLibs.joinToString(".so、lib")}.so。\n" +
                        "请先完成 Source Engine 安卓端的 NDK 编译并打包进 APK，之后即可正常启动。\n" +
                        "目前启动器已准备好所有配置（args/autoexec/启动 Intent），只要 lib 就位就能运行。"
            )
        }

        // 2) 游戏目录是否已选择
        val root = resolveGameRoot(gameDir, context)
        if (root == null) {
            out += Diagnostic(
                false,
                "尚未配置游戏目录",
                "请在「设置」中选择包含 hl2/、platform/、bin/ 等文件夹的 Source 资源根目录。"
            )
        } else {
            val modDir = File(root, mod)
            val gi = File(modDir, "gameinfo.txt")
            if (!modDir.isDirectory || !gi.isFile) {
                out += Diagnostic(
                    false,
                    "游戏/模组目录不完整",
                    "在 ${root.absolutePath}/$mod/ 下未找到 gameinfo.txt。\n请确认选择的目录以及模组名（当前：$mod）是否正确。"
                )
            } else {
                out += Diagnostic(true, "游戏资源", "已定位 ${gi.absolutePath}")
            }
        }

        // 3) SDL Activity 是否能在本 APK 中解析（Manifest 有声明 + Java 类存在）
        val sdlIntent = Intent().setClassName(context.packageName, SDL_ACTIVITY_CLASS)
        val resolved = context.packageManager.resolveActivity(sdlIntent, PackageManager.MATCH_DEFAULT_ONLY)
        if (resolved == null) {
            out += Diagnostic(
                false,
                "SDLActivity 入口不可用",
                "Manifest 未声明或无法解析 $SDL_ACTIVITY_CLASS。请重新构建引擎 APK。"
            )
        } else {
            // 确认类真的能加载（只有当 libSDL2 Java 类存在时才会通过）
            val classOk = try {
                Class.forName(SDL_ACTIVITY_CLASS)
                true
            } catch (_: Throwable) {
                false
            }
            if (classOk) {
                out += Diagnostic(true, "引擎入口", "$SDL_ACTIVITY_CLASS 可加载")
            } else {
                out += Diagnostic(
                    false,
                    "SDL Java 类尚未打进 APK",
                    "能找到 Manifest 声明，但 Class.forName(\"$SDL_ACTIVITY_CLASS\") 失败。\n" +
                            "这通常意味着 SDL2 Java 源尚未加入工程（在 engine 编译完成后会一起带进来）。\n" +
                            "不影响启动器的其它功能，补完 lib 即可。"
                )
            }
        }
        return out
    }

    /** 是否“完全可以启动”（没有失败级别的诊断项） */
    fun isReadyToLaunch(context: Context, gameDir: String, mod: String): Boolean =
        runDiagnostics(context, gameDir, mod).none { !it.ok }

    // ============================================================
    //  写入 autoexec.cfg
    // ============================================================

    fun writeAutoExec(
        context: Context,
        gameDir: String,
        mod: String,
        cvars: List<CVar>,
        extraContent: String = ""
    ): PrepareResult {
        val root = resolveGameRoot(gameDir, context)
            ?: return PrepareResult(false, "游戏目录未配置，无法写入 autoexec.cfg", "")
        val modDir = File(root, mod)
        if (!modDir.isDirectory) {
            return PrepareResult(false, "模组目录不存在：${modDir.absolutePath}", "")
        }
        val cfgDir = File(modDir, "cfg").apply { mkdirs() }
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
            PrepareResult(false, "写入 autoexec.cfg 失败: ${e.message}", "")
        }
    }

    // ============================================================
    //  写入 launch-args.txt
    // ============================================================

    fun writeLaunchArgs(context: Context, args: String): PrepareResult {
        val f = File(context.filesDir, LAUNCH_ARGS_FILENAME)
        return try {
            // 与 nillerusr/SourceEngineAndroid 的 SDL 入口约定兼容：
            //   每行一个参数，支持带引号的 token（简单情况下每行一个即可）。
            // 这里我们写原始拼接字符串，再同时写 args.json 便于后续扩展。
            f.writeText(args)
            // 同时写 JSON 数组版本（更安全的 argv 切分方式）
            val json = splitArgsAsJson(args)
            File(context.filesDir, "launch-args.json").writeText(json)
            PrepareResult(true, "启动参数已写入 ${f.absolutePath}", args, argsFile = f.absolutePath)
        } catch (e: IOException) {
            PrepareResult(false, "写入 launch-args.txt 失败: ${e.message}", "")
        }
    }

    private fun splitArgsAsJson(args: String): String {
        // 简单的 argv-like 切分：支持 "..." 与 '...'
        val tokens = mutableListOf<String>()
        var cur = StringBuilder()
        var inQuote: Char? = null
        var i = 0
        val s = args
        while (i < s.length) {
            val c = s[i]
            when {
                inQuote != null -> {
                    if (c == inQuote) inQuote = null else cur.append(c)
                }
                c == '"' || c == '\'' -> inQuote = c
                c.isWhitespace() -> {
                    if (cur.isNotEmpty()) {
                        tokens += cur.toString(); cur = StringBuilder()
                    }
                }
                else -> cur.append(c)
            }
            i++
        }
        if (cur.isNotEmpty()) tokens += cur.toString()
        return tokens.joinToString(
            prefix = "[", postfix = "]", separator = ","
        ) { "\"${it.jsonEscapeArg()}\"" }
    }

    private fun String.jsonEscapeArg() =
        replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")

    // ============================================================
    //  启动引擎 Activity
    // ============================================================

    fun launchGame(
        context: Context,
        args: String,
        extraFlags: Int = 0
    ): PrepareResult {
        val intent = Intent().apply {
            setClassName(context.packageName, SDL_ACTIVITY_CLASS)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or extraFlags)
            // 额外信息：URI + String-ArrayList extra，便于 SDL Java 侧/自定义入口读取
            val list = ArrayList(args.splitQuotedArgs().filter { it.isNotBlank() })
            putStringArrayListExtra("args", list)
            putExtra("cmdline", args)
            data = Uri.parse("srceng://launch?pkg=${context.packageName}")
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            intent.addFlags(Intent.FLAG_ACTIVITY_LAUNCH_ADJACENT)
        }
        return try {
            context.startActivity(intent)
            PrepareResult(true, "启动 $SDL_ACTIVITY_CLASS，参数数量=${args.splitQuotedArgs().size}", args)
        } catch (t: Throwable) {
            Log.e(TAG, "启动失败: $SDL_ACTIVITY_CLASS", t)
            PrepareResult(
                false,
                "未能启动引擎入口。\n\n" +
                        "目标: $SDL_ACTIVITY_CLASS\n" +
                        "原因: ${t.message ?: t.javaClass.simpleName}\n\n" +
                        "提示：若提示 ClassNotFound，则引擎 Native/Java 库尚未编译，先完成 lib 构建即可。",
                args
            )
        }
    }

    // ============================================================
    //  一键执行：自检 + 写 autoexec + 写 launch-args + 启动
    //  返回最终结果（供 UI 显示 Snackbar / 错误对话框）
    // ============================================================

    data class LaunchFlowResult(
        val canLaunch: Boolean,
        val diagnostics: List<Diagnostic>,
        val autoexec: PrepareResult? = null,
        val argsFile: PrepareResult? = null,
        val launch: PrepareResult? = null
    )

    fun prepareAndLaunch(
        context: Context,
        gameDir: String,
        mod: String,
        cvars: List<CVar>,
        autoexecBody: String,
        launchArgs: String
    ): LaunchFlowResult {
        val diags = runDiagnostics(context, gameDir, mod)
        val canLaunch = diags.none { !it.ok }

        // 总是尝试写配置（哪怕缺少 lib 也先写好，让用户下次带 lib 时就能用）
        val wCfg = writeAutoExec(context, gameDir, mod, cvars, autoexecBody)
        val wArgs = writeLaunchArgs(context, launchArgs)

        if (!canLaunch) {
            return LaunchFlowResult(
                canLaunch = false,
                diagnostics = diags,
                autoexec = wCfg,
                argsFile = wArgs,
                launch = null
            )
        }

        val launchResult = launchGame(context, launchArgs)
        return LaunchFlowResult(true, diags, wCfg, wArgs, launchResult)
    }

    // ============================================================
    //  辅助
    // ============================================================

    private fun String.splitQuotedArgs(): List<String> {
        val out = mutableListOf<String>()
        var cur = StringBuilder()
        var inQuote: Char? = null
        for (c in this) {
            when {
                inQuote != null -> {
                    if (c == inQuote) inQuote = null else cur.append(c)
                }
                c == '"' || c == '\'' -> inQuote = c
                c.isWhitespace() -> {
                    if (cur.isNotEmpty()) { out += cur.toString(); cur = StringBuilder() }
                }
                else -> cur.append(c)
            }
        }
        if (cur.isNotEmpty()) out += cur.toString()
        return out
    }

    private fun nativeLibExists(context: Context, shortName: String): Boolean {
        try {
            val appInfo = context.packageManager.getApplicationInfo(context.packageName, 0)
            val libDirs = buildList {
                appInfo.nativeLibraryDir?.also { add(it) }
                // split APK / extractNativeLibs=false 的情况
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    appInfo.sourceDir?.also { src ->
                        add(File(src).parentFile?.resolve("lib")?.absolutePath ?: return@also)
                    }
                }
            }
            val candidates = libDirs.flatMap { base ->
                arrayOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64").map { File(File(base), it) }
            } + libDirs.map { File(it) }

            for (dir in candidates.distinct()) {
                val f = File(dir, "lib${shortName}.so")
                if (f.isFile && f.length() > 0) return true
            }
            // 最后兜底：尝试 System.loadLibrary（若它能成功，就存在）
            try {
                System.loadLibrary(shortName)
                return true
            } catch (_: UnsatisfiedLinkError) { /* fall through */ }
            return false
        } catch (_: Throwable) {
            return false
        }
    }

    private fun resolveGameRoot(gameDir: String, context: Context): File? {
        val candidates = mutableListOf<File>()
        if (gameDir.isNotBlank()) candidates += File(gameDir)
        // 经典默认路径
        val ext = Environment.getExternalStorageDirectory()
        candidates += File(ext, "Android/data/${context.packageName}/files/source")
        candidates += File(ext, "source")
        candidates += File(context.getExternalFilesDir(null), "source")
        candidates += context.filesDir
        return candidates.firstOrNull { it.isDirectory }
    }
}
