package com.srceng.launcher.game

import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * 文件与目录工具：SAF 路径解析、游戏资源校验等
 */
object StorageHelper {

    /**
     * 申请的目录是否包含基本的 Source 资源
     */
    fun looksLikeSourceRoot(context: Context, uri: Uri): Boolean {
        val root = DocumentFile.fromTreeUri(context, uri) ?: return false
        val required = listOf("hl2", "platform", "bin")
        return required.all { name ->
            root.findFile(name)?.isDirectory == true
        }
    }

    /**
     * 将 SAF tree URI 转换为可读文件路径（仅用于展示 / 调试）
     */
    fun safTreeToPath(uri: Uri): String {
        val docId = try {
            DocumentsContract.getTreeDocumentId(uri)
        } catch (_: Throwable) {
            return uri.toString()
        }
        val (vol, rel) = docId.split(":", limit = 2).let {
            if (it.size == 1) "primary" to it[0] else it[0] to it[1]
        }
        val base = when {
            "primary".equals(vol, ignoreCase = true) ->
                Environment.getExternalStorageDirectory().absolutePath
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R ->
                "/storage/$vol"
            else -> "/storage/$vol"
        }
        return File(base, rel).absolutePath
    }

    /**
     * 列举 mod 目录（一级子目录中带 gameinfo.txt 的）
     */
    fun scanMods(context: Context, rootUri: Uri): List<String> {
        val root = DocumentFile.fromTreeUri(context, rootUri) ?: return emptyList()
        val mods = mutableListOf<String>()
        root.listFiles().forEach { dir ->
            if (!dir.isDirectory) return@forEach
            val gi = dir.findFile("gameinfo.txt")
            if (gi != null && gi.isFile) mods += dir.name ?: return@forEach
        }
        return mods
    }
}
