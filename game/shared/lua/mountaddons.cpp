@@
 #include "cbase.h"
 #include "filesystem.h"
 #include "luamanager.h"
 
 // memdbgon must be the last include file in a .cpp file!!!
 #include "tier0/memdbgon.h"
+
+// Ensure struct _stat and filesystem function visibility on all platforms.
+#if defined(_WIN32)
+#include <io.h>
+#include <sys/stat.h>
+#else
+#include <sys/stat.h>
+#endif
@@
-    char const *fn = g_pFullFileSystem->FindFirstEx( LUA_PATH_ADDONS "/*", "MOD", &fh );
+    char const *fn = g_pFullFileSystem->FindFirstEx( LUA_PATH_ADDONS "/*", "MOD", &fh );
@@
-    g_pFullFileSystem->FindClose( fh );
+    g_pFullFileSystem->FindClose( fh );
