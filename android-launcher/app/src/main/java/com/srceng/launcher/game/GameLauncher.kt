package com.srceng.launcher.game

import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
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

    fun runDiagnostics(context: Context, gameDir: String, gameDirUri: String, mod: String): List<Diagnostic> {
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

        // 2) 游戏目录检测：优先用 SAF DocumentFile（Android 11+ 作用域存储），回退到 File
        val gameRoot = resolveGameRoot(gameDir, context)
        val gameRootDoc = resolveGameRootDoc(context, gameDirUri)

        if (gameDir.isBlank() && gameDirUri.isBlank()) {
            out += Diagnostic(
                false,
                "尚未配置游戏目录",
                "请在主页点击「从文件管理器选择」或手动输入路径，选择包含 hl2/、platform/、bin/ 的 Source 资源根目录。"
            )
        } else {
            // 尝试用 DocumentFile 检测（SAF 路径优先）
            val (rootFound, rootDesc) = if (gameRootDoc != null) {
                true to "SAF URI: $gameDirUri"
            } else if (gameRoot != null) {
                true to "文件路径: ${gameRoot.absolutePath}"
            } else {
                // 路径指定了但都可访问不到 → 提示用户
                false to (gameDir.ifBlank { "（未设置）" })
            }

            if (!rootFound) {
                out += Diagnostic(
                    false,
                    "游戏目录不可访问",
                    "已配置路径为: $rootDesc\n" +
                            "原因：Android 11+ 作用域存储导致 APP 无法直接通过文件路径访问。\n" +
                            "解决方案：请重新点击「从文件管理器选择」选择游戏目录，确保系统文件选择器弹窗后正常授权。\n" +
                            "提示：如果仍然无效，请尝试通过「手动粘贴路径」输入 /storage/emulated/0/ 下的完整路径。"
                )
            } else {
                // 在根目录下找 gameinfo.txt（支持三种常见结构）
                val gameInfoCheck = findGameInfo(gameRootDoc, gameRoot, mod)
                if (gameInfoCheck == null) {
                    // 详细列出用户目录下有什么
                    val listing = listDirContents(gameRootDoc, gameRoot)
                    out += Diagnostic(
                        false,
                        "未找到 gameinfo.txt",
                        "在游戏根目录下未找到 $mod/gameinfo.txt。\n\n" +
                                "当前目录内容：\n${listing.ifEmpty { "（空目录或无法列出）" }}\n\n" +
                                "请确认：\n" +
                                "1) 选择的目录是 Source 引擎根目录（包含 hl2/、platform/、bin/）\n" +
                                "2) 当前模组选择正确（当前：$mod，可在「运行信息」中切换）\n" +
                                "3) 游戏资源文件已正确解压到该目录"
                    )
                } else {
                    out += Diagnostic(true, "游戏资源", "已定位 ${gameInfoCheck.absolutePath}")
                }
            }
        }

        // 3) SDL Activity 是否能在本 APK 中解析
        val sdlIntent = Intent().setClassName(context.packageName, SDL_ACTIVITY_CLASS)
        val resolved = context.packageManager.resolveActivity(sdlIntent, PackageManager.MATCH_DEFAULT_ONLY)
        if (resolved == null) {
            out += Diagnostic(
                false,
                "SDLActivity 入口不可用",
                "Manifest 未声明或无法解析 $SDL_ACTIVITY_CLASS。请重新构建引擎 APK。"
            )
        } else {
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
    fun isReadyToLaunch(context: Context, gameDir: String, gameDirUri: String, mod: String): Boolean =
        runDiagnostics(context, gameDir, gameDirUri, mod).none { !it.ok }

    // ===== 游戏目录解析辅助 =====

    /** 用 File API 解析目录（传统路径，可能受作用域存储限制） */
    private fun resolveGameRoot(gameDir: String, context: Context): File? {
        val candidates = mutableListOf<File>()
        if (gameDir.isNotBlank()) candidates += File(gameDir)
        val ext = Environment.getExternalStorageDirectory()
        candidates += File(ext, "Android/data/${context.packageName}/files/source")
        candidates += File(ext, "source")
        candidates += File(context.getExternalFilesDir(null), "source")
        candidates += context.filesDir
        return candidates.firstOrNull { it.isDirectory }
    }

    /** 用 SAF DocumentFile 解析目录（Android 11+ 推荐方式） */
    private fun resolveGameRootDoc(context: Context, gameDirUri: String): DocumentFile? {
        if (gameDirUri.isBlank()) return null
        return try {
            val uri = Uri.parse(gameDirUri)
            val doc = DocumentFile.fromTreeUri(context, uri)
            if (doc != null && doc.exists() && doc.isDirectory) doc else null
        } catch (_: Exception) {
            null
        }
    }

    /**
     * 在游戏根目录下找 gameinfo.txt，支持多种目录结构：
     * 1) $root/$mod/gameinfo.txt  （标准 Source 结构）
     * 2) $root/gameinfo.txt       （用户直接选了 mod 目录）
     * 3) $root/game/$mod/gameinfo.txt（备选结构）
     */
    private fun findGameInfo(
        rootDoc: DocumentFile?,
        rootFile: File?,
        mod: String
    ): File? {
        // 优先用 SAF DocumentFile（返回路径）
        if (rootDoc != null) {
            val ctx = rootDoc.uri
            // 尝试 $root/$mod/gameinfo.txt
            for (sub in listOf(mod, "")) {
                val dir = if (sub.isNotBlank()) rootDoc.findFile(sub) else rootDoc
                if (dir != null && dir.isDirectory) {
                    val gi = dir.findFile("gameinfo.txt")
                    if (gi != null && gi.isFile) {
                        // 转成真实路径（如果可能）
                        return try {
                            val docId = DocumentsContract.getDocumentId(gi.uri)
                            val path = if (docId.contains(":")) {
                                val parts = docId.split(":")
                                "/storage/emulated/0/${parts.getOrElse(1) { parts[0] }}"
                            } else null
                            path?.let { File(it) } ?: File(gi.uri.toString())
                        } catch (_: Exception) {
                            File(gi.uri.toString())
                        }
                    }
                }
            }
        }

        // 回退到 File API
        if (rootFile != null) {
            // 标准结构：$root/$mod/gameinfo.txt
            val modDir = File(rootFile, mod)
            val gi1 = File(modDir, "gameinfo.txt")
            if (gi1.isFile) return gi1
            // 用户直接选了 mod 目录
            val gi2 = File(rootFile, "gameinfo.txt")
            if (gi2.isFile) return gi2
            // $root/game/$mod/gameinfo.txt
            val gameDir = File(rootFile, "game")
            if (gameDir.isDirectory) {
                val gi3 = File(File(gameDir, mod), "gameinfo.txt")
                if (gi3.isFile) return gi3
            }
        }
        return null
    }

    /** 列出目录内容（用于诊断提示） */
    private fun listDirContents(rootDoc: DocumentFile?, rootFile: File?): String {
        val items = mutableListOf<String>()
        if (rootDoc != null) {
            try {
                rootDoc.listFiles().take(20).forEach { f ->
                    items += "  ${if (f.isDirectory) "📁" else "📄"} ${f.name ?: "?"}"
                }
            } catch (_: Exception) {}
        }
        if (items.isEmpty() && rootFile != null) {
            try {
                rootFile.listFiles()?.take(20)?.forEach { f ->
                    items += "  ${if (f.isDirectory) "📁" else "📄"} ${f.name}"
                }
            } catch (_: Exception) {}
        }
        return items.joinToString("\n")
    }

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
        gameDirUri: String,
        mod: String,
        cvars: List<CVar>,
        autoexecBody: String,
        launchArgs: String
    ): LaunchFlowResult {
        val diags = runDiagnostics(context, gameDir, gameDirUri, mod)
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

    }
