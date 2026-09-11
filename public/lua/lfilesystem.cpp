//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define lfilesystem_cpp

#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lfilesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



/*
** access functions (stack -> C)
*/


LUA_API lua_FileHandle_t &lua_tofilehandle (lua_State *L, int idx) {
  lua_FileHandle_t *phFile = (lua_FileHandle_t *)luaL_checkudata(L, idx, "FileHandle_t");
  return *phFile;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushfilehandle (lua_State *L, lua_FileHandle_t hFile) {
  lua_FileHandle_t *phFile = (lua_FileHandle_t *)lua_newuserdata(L, sizeof(lua_FileHandle_t));
  *phFile = hFile;
  luaL_getmetatable(L, "FileHandle_t");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_FileHandle_t &luaL_checkfilehandle (lua_State *L, int narg) {
  lua_FileHandle_t *d = (lua_FileHandle_t *)luaL_checkudata(L, narg, "FileHandle_t");
  if (*d == FILESYSTEM_INVALID_HANDLE)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "FileHandle_t expected, got FILESYSTEM_INVALID_HANDLE");
  return *d;
}


static int filesystem_AddPackFile (lua_State *L) {
  lua_pushboolean(L, filesystem->AddPackFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
  return 1;
}

static int filesystem_AddSearchPath (lua_State *L) {
  char fullpath[ 512 ] = { 0 };
  bool bGetCurrentDirectory = V_GetCurrentDirectory( fullpath, sizeof( fullpath ) );
  if ( bGetCurrentDirectory )
  {
#ifdef CLIENT_DLL
    const char *gamePath = engine->GetGameDirectory();
#else
    char gamePath[ 256 ];
    engine->GetGameDir( gamePath, 256 );
#endif
    V_SetCurrentDirectory( gamePath );
  }
  filesystem->AddSearchPath(luaL_checkstring(L, 1), luaL_checkstring(L, 2), (SearchPathAdd_t)luaL_optint(L, 3, PATH_ADD_TO_TAIL));
  if ( bGetCurrentDirectory )
	  V_SetCurrentDirectory( fullpath );
  return 0;
}

static int filesystem_BeginMapAccess (lua_State *L) {
  filesystem->BeginMapAccess();
  return 0;
}

static int filesystem_CancelWaitForResources (lua_State *L) {
  filesystem->CancelWaitForResources(luaL_checkint(L, 1));
  return 0;
}

static int filesystem_Close (lua_State *L) {
  filesystem->Close(luaL_checkfilehandle(L, 1));
  // Andrew; this isn't standard behavior or usage, but we do this for the sake
  // of things being safe in Lua
  luaL_checkfilehandle(L, 1) = FILESYSTEM_INVALID_HANDLE;
  return 0;
}

static int filesystem_CreateDirHierarchy (lua_State *L) {
  filesystem->CreateDirHierarchy(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0));
  return 0;
}

static int filesystem_DiscardPreloadData (lua_State *L) {
  filesystem->DiscardPreloadData();
  return 0;
}

static int filesystem_Disconnect (lua_State *L) {
  filesystem->Disconnect();
  return 0;
}

static int filesystem_EnableWhitelistFileTracking (lua_State *L) {
  filesystem->EnableWhitelistFileTracking(luaL_checkboolean(L, 1), luaL_checkboolean(L, 2), luaL_checkboolean(L, 3));
  return 0;
}

static int filesystem_EndMapAccess (lua_State *L) {
  filesystem->EndMapAccess();
  return 0;
}

static int filesystem_EndOfFile (lua_State *L) {
  lua_pushboolean(L, filesystem->EndOfFile(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int filesystem_FileExists (lua_State *L) {
  lua_pushboolean(L, filesystem->FileExists(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_Flush (lua_State *L) {
  filesystem->Flush(luaL_checkfilehandle(L, 1));
  return 0;
}

static int filesystem_GetDVDMode (lua_State *L) {
  lua_pushinteger(L, filesystem->GetDVDMode());
  return 1;
}

static int filesystem_GetLocalCopy (lua_State *L) {
  filesystem->GetLocalCopy(luaL_checkstring(L, 1));
  return 0;
}

static int filesystem_GetWhitelistSpewFlags (lua_State *L) {
  lua_pushinteger(L, filesystem->GetWhitelistSpewFlags());
  return 1;
}

static int filesystem_HintResourceNeed (lua_State *L) {
  lua_pushinteger(L, filesystem->HintResourceNeed(luaL_checkstring(L, 1), luaL_checkint(L, 2)));
  return 1;
}

static int filesystem_IsDirectory (lua_State *L) {
  lua_pushboolean(L, filesystem->IsDirectory(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_IsFileImmediatelyAvailable (lua_State *L) {
  lua_pushboolean(L, filesystem->IsFileImmediatelyAvailable(luaL_checkstring(L, 1)));
  return 1;
}

static int filesystem_IsFileWritable (lua_State *L) {
  lua_pushboolean(L, filesystem->IsFileWritable(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_IsOk (lua_State *L) {
  lua_pushboolean(L, filesystem->IsOk(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int filesystem_IsSteam (lua_State *L) {
  lua_pushboolean(L, filesystem->IsSteam());
  return 1;
}

static int filesystem_LoadCompiledKeyValues (lua_State *L) {
  filesystem->LoadCompiledKeyValues((IFileSystem::KeyValuesPreloadType_t)luaL_checkint(L, 1), luaL_checkstring(L, 2));
  return 0;
}

static int filesystem_MarkAllCRCsUnverified (lua_State *L) {
  filesystem->MarkAllCRCsUnverified();
  return 0;
}

static int filesystem_MarkPathIDByRequestOnly (lua_State *L) {
  filesystem->MarkPathIDByRequestOnly(luaL_checkstring(L, 1), luaL_checkboolean(L, 2));
  return 0;
}

static int filesystem_MountSteamContent (lua_State *L) {
  lua_pushinteger(L, filesystem->MountSteamContent(luaL_optint(L, 1, -1)));
  return 1;
}

static int filesystem_Open (lua_State *L) {
  lua_pushfilehandle(L, filesystem->Open(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_Precache (lua_State *L) {
  lua_pushboolean(L, filesystem->Precache(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_PrintOpenedFiles (lua_State *L) {
  filesystem->PrintOpenedFiles();
  return 0;
}

static int filesystem_PrintSearchPaths (lua_State *L) {
  filesystem->PrintSearchPaths();
  return 0;
}

static int filesystem_Read (lua_State *L) {
  byte *buffer;

  FileHandle_t file;
  file = luaL_checkfilehandle(L, 2);

  int size = luaL_checkint(L, 1);
  buffer = new byte[ size + 1 ];
  if ( !buffer )
  {
  	Warning( "filesystem.Read:  Couldn't allocate buffer of size %i for file\n", size + 1 );
  	lua_pushinteger(L, -1);
  	lua_pushstring(L, NULL);
  	return 2;
  }
  int bytesRead = filesystem->Read( buffer, size, file );

  if ( bytesRead )
  {
	  // Ensure null terminator
	  buffer[ bytesRead ] =0;
  }
  else
  {
	  *buffer = 0;
  }

  lua_pushinteger(L, size);
  lua_pushstring(L, (const char *)buffer);
  delete buffer;

  return 2;
}

static int filesystem_RemoveAllSearchPaths (lua_State *L) {
  filesystem->RemoveAllSearchPaths();
  return 0;
}

static int filesystem_RemoveFile (lua_State *L) {
  filesystem->RemoveFile(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0));
  return 0;
}

static int filesystem_RemoveSearchPath (lua_State *L) {
  lua_pushboolean(L, filesystem->RemoveSearchPath(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_RemoveSearchPaths (lua_State *L) {
  filesystem->RemoveSearchPaths(luaL_checkstring(L, 1));
  return 0;
}

static int filesystem_RenameFile (lua_State *L) {
  lua_pushboolean(L, filesystem->RenameFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_SetFileWritable (lua_State *L) {
  lua_pushboolean(L, filesystem->SetFileWritable(luaL_checkstring(L, 1), luaL_checkboolean(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_SetupPreloadData (lua_State *L) {
  filesystem->SetupPreloadData();
  return 0;
}

static int filesystem_SetWarningLevel (lua_State *L) {
  filesystem->SetWarningLevel((FileWarningLevel_t)luaL_checkint(L, 1));
  return 0;
}

static int filesystem_SetWhitelistSpewFlags (lua_State *L) {
  filesystem->SetWhitelistSpewFlags(luaL_checkint(L, 1));
  return 0;
}

static int filesystem_Shutdown (lua_State *L) {
  filesystem->Shutdown();
  return 0;
}

static int filesystem_Size (lua_State *L) {
  switch(lua_type(L, 1)) {
    case LUA_TSTRING:
      lua_pushinteger(L, filesystem->Size(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
      break;
    case LUA_TUSERDATA:
    default:
      lua_pushinteger(L, filesystem->Size(luaL_checkfilehandle(L, 1)));
      break;
  }
  return 1;
}

static int filesystem_UnzipFile (lua_State *L) {
  lua_pushboolean(L, filesystem->UnzipFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_checkstring(L, 3)));
  return 1;
}

static int filesystem_WaitForResources (lua_State *L) {
  lua_pushinteger(L, filesystem->WaitForResources(luaL_checkstring(L, 1)));
  return 1;
}

static int filesystem_Write (lua_State *L) {
  size_t l;
  const char *pInput = luaL_checklstring(L, 1, &l);
  lua_pushinteger(L, filesystem->Write(pInput, l, luaL_checkfilehandle(L, 2)));
  return 1;
}


static const luaL_Reg filesystemlib[] = {
  {"AddPackFile",   filesystem_AddPackFile},
  {"AddSearchPath",   filesystem_AddSearchPath},
  {"BeginMapAccess",   filesystem_BeginMapAccess},
  {"CancelWaitForResources",   filesystem_CancelWaitForResources},
  {"Close",   filesystem_Close},
  {"CreateDirHierarchy",   filesystem_CreateDirHierarchy},
  {"DiscardPreloadData",   filesystem_DiscardPreloadData},
  {"Disconnect",   filesystem_Disconnect},
  {"EnableWhitelistFileTracking",   filesystem_EnableWhitelistFileTracking},
  {"EndMapAccess",   filesystem_EndMapAccess},
  {"EndOfFile",   filesystem_EndOfFile},
  {"FileExists",   filesystem_FileExists},
  {"Flush",   filesystem_Flush},
  {"GetDVDMode",   filesystem_GetDVDMode},
  {"GetLocalCopy",   filesystem_GetLocalCopy},
  {"GetWhitelistSpewFlags",   filesystem_GetWhitelistSpewFlags},
  {"HintResourceNeed",   filesystem_HintResourceNeed},
  {"IsDirectory",   filesystem_IsDirectory},
  {"IsFileImmediatelyAvailable",   filesystem_IsFileImmediatelyAvailable},
  {"IsFileWritable",   filesystem_IsFileWritable},
  {"IsOk",   filesystem_IsOk},
  {"IsSteam",   filesystem_IsSteam},
  {"LoadCompiledKeyValues",   filesystem_LoadCompiledKeyValues},
  {"MarkAllCRCsUnverified",   filesystem_MarkAllCRCsUnverified},
  {"MarkPathIDByRequestOnly",   filesystem_MarkPathIDByRequestOnly},
  {"MountSteamContent",   filesystem_MountSteamContent},
  {"Open",   filesystem_Open},
  {"Precache",   filesystem_Precache},
  {"PrintOpenedFiles",   filesystem_PrintOpenedFiles},
  {"PrintSearchPaths",   filesystem_PrintSearchPaths},
  {"Read",   filesystem_Read},
  {"RemoveAllSearchPaths",   filesystem_RemoveAllSearchPaths},
  {"RemoveFile",   filesystem_RemoveFile},
  {"RemoveSearchPath",   filesystem_RemoveSearchPath},
  {"RemoveSearchPaths",   filesystem_RemoveSearchPaths},
  {"RenameFile",   filesystem_RenameFile},
  {"SetFileWritable",   filesystem_SetFileWritable},
  {"SetupPreloadData",   filesystem_SetupPreloadData},
  {"SetWarningLevel",   filesystem_SetWarningLevel},
  {"SetWhitelistSpewFlags",   filesystem_SetWhitelistSpewFlags},
  {"Size",   filesystem_Size},
  {"UnzipFile",   filesystem_UnzipFile},
  {"WaitForResources",   filesystem_WaitForResources},
  {"Write",   filesystem_Write},
  {NULL, NULL}
};


static int FileHandle_t___gc (lua_State *L) {
  FileHandle_t hFile = lua_tofilehandle(L, 1);
  if (hFile != FILESYSTEM_INVALID_HANDLE)
    filesystem->Close(hFile);
  return 0;
}

static int FileHandle_t___tostring (lua_State *L) {
  FileHandle_t hFile = lua_tofilehandle(L, 1);
  if (hFile == FILESYSTEM_INVALID_HANDLE)
    lua_pushstring(L, "FILESYSTEM_INVALID_HANDLE");
  else
    lua_pushfstring(L, "FileHandle_t: %p", lua_tofilehandle(L, 1));
  return 1;
}


/*
** HL2SB: GMod's File object methods.
**
** lua/includes/extensions/file.lua (Garry's Mod's own file extension, shipped
** here verbatim) rebuilds file.Read / file.Write on top of file.Open, and then
** does `local str = f:Read( f:Size() )` / `f:Close()`.  The FileHandle_t
** metatable used to carry nothing but __gc and __tostring, so loading that
** extension failed on both realms:
**
**   [Lua] FAILED lua/includes/extensions/player_auth.lua:
**         file.lua:10: attempt to call a nil value (method 'Size')
*/
static int FileHandle_Close (lua_State *L) {
  FileHandle_t &hFile = luaL_checkfilehandle(L, 1);

  if (hFile != FILESYSTEM_INVALID_HANDLE) {
    filesystem->Close(hFile);
    hFile = FILESYSTEM_INVALID_HANDLE;
  }

  return 0;
}

static int FileHandle_EndOfFile (lua_State *L) {
  lua_pushboolean(L, filesystem->EndOfFile(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Flush (lua_State *L) {
  filesystem->Flush(luaL_checkfilehandle(L, 1));
  return 0;
}

// f:Read( [bytes] ) -> string  (the rest of the file by default)
static int FileHandle_Read (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  int nBytes = luaL_optint(L, 2, filesystem->Size(hFile));

  if (nBytes <= 0) {
    lua_pushstring(L, "");
    return 1;
  }

  byte *pBuffer = new byte[nBytes + 1];
  int nRead = filesystem->Read(pBuffer, nBytes, hFile);

  if (nRead <= 0) {
    delete[] pBuffer;
    lua_pushstring(L, "");
    return 1;
  }

  pBuffer[nRead] = 0;
  lua_pushlstring(L, (const char *)pBuffer, nRead);
  delete[] pBuffer;
  return 1;
}

// f:Seek( offset, whence ) -> number.  GMod's whence is a string ("set"/"cur"/
// "end"); the plain 0/1/2 numbers C uses are accepted too.
static int FileHandle_Seek (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  int nOffset = luaL_checkint(L, 2);
  FileSystemSeek_t nWhence = FILESYSTEM_SEEK_HEAD;

  if (lua_type(L, 3) == LUA_TSTRING) {
    const char *pWhence = lua_tostring(L, 3);

    if (!V_stricmp(pWhence, "cur") || !V_stricmp(pWhence, "current"))
      nWhence = FILESYSTEM_SEEK_CURRENT;
    else if (!V_stricmp(pWhence, "end") || !V_stricmp(pWhence, "tail"))
      nWhence = FILESYSTEM_SEEK_TAIL;
  } else if (lua_isnumber(L, 3)) {
    int nWhence = lua_tointeger(L, 3);
    nWhence = (nWhence == 1) ? FILESYSTEM_SEEK_CURRENT : (nWhence == 2) ? FILESYSTEM_SEEK_TAIL : FILESYSTEM_SEEK_HEAD;
  }

  // IFileSystem::Seek returns void in this fork, so report the new position the
  // way GMod's File:Seek does.
  filesystem->Seek(hFile, nOffset, nWhence);
  lua_pushinteger(L, filesystem->Tell(hFile));
  return 1;
}

static int FileHandle_Size (lua_State *L) {
  lua_pushinteger(L, filesystem->Size(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Tell (lua_State *L) {
  lua_pushinteger(L, filesystem->Tell(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Write (lua_State *L) {
  size_t nLength = 0;
  const char *pData = luaL_checklstring(L, 2, &nLength);

  // IFileSystem::Write returns void in this fork; report what we handed it.
  filesystem->Write(pData, (int)nLength, luaL_checkfilehandle(L, 1));
  lua_pushinteger(L, (lua_Integer)nLength);
  return 1;
}

static const luaL_Reg FileHandle_tmeta[] = {
  {"Close", FileHandle_Close},
  {"EndOfFile", FileHandle_EndOfFile},
  {"Flush", FileHandle_Flush},
  {"Read", FileHandle_Read},
  {"Seek", FileHandle_Seek},
  {"Size", FileHandle_Size},
  {"Tell", FileHandle_Tell},
  {"Write", FileHandle_Write},
  {"__gc", FileHandle_t___gc},
  {"__tostring", FileHandle_t___tostring},
  {NULL, NULL}
};


/*
** Open filesystem library
*/
LUALIB_API int luaopen_filesystem (lua_State *L) {
  luaL_newmetatable(L, "FileHandle_t");
  luaL_register(L, NULL, FileHandle_tmeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "filehandle");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "filehandle" */
  lua_pop(L, 1);
  lua_pushfilehandle(L, FILESYSTEM_INVALID_HANDLE);
  lua_setglobal(L, "FILESYSTEM_INVALID_HANDLE");
  luaL_register(L, LUA_FILESYSTEMLIBNAME, filesystemlib);
  return 1;
}


//=============================================================================
// HL2SB: GMod's file library -- luaopen_Files
//
// luaopen_Files was DECLARED in luasrclib.h:217 and LUA_FILESLIBNAME is "Files",
// but the registration in lsrcinit.cpp was commented out and there was no
// implementation anywhere, so the lib alias at lsrcinit.cpp:520
// ("file" -> LUA_FILESLIBNAME) silently skipped and every GMod file that uses it
// failed:
//
//     [Lua] FAILED lua/includes/extensions/file.lua:2: attempt to index a nil value (global 'file')
//     [Lua] FAILED lua/includes/extensions/player_auth.lua:77: ... (global 'file')
//
// Lives in lfilesystem.cpp on purpose: that file is already listed in BOTH
// client_lua.vpc and server_lua.vpc, so neither .vpc needs touching and both
// realms get it.  Paths are relative to the game ("MOD") directory, which is what
// GMod's file.* use against garrysmod/.
//=============================================================================

static IFileSystem *HL2SB_FileSystem( void )
{
	return g_pFullFileSystem ? g_pFullFileSystem : filesystem;
}

// file.Read( path [, gamePath] ) -> string | nil
static int file_Read (lua_State *L) {
  const char *pszPath = luaL_checkstring(L, 1);
  IFileSystem *pFS = HL2SB_FileSystem();

  FileHandle_t fh = pFS->Open(pszPath, "rb", "MOD");
  if (!fh) { lua_pushnil(L); return 1; }

  int nSize = pFS->Size(fh);
  if (nSize < 0) { pFS->Close(fh); lua_pushnil(L); return 1; }

  char *pBuf = (char *)malloc(nSize + 1);
  int nRead = (nSize > 0) ? pFS->Read(pBuf, nSize, fh) : 0;
  pFS->Close(fh);

  pBuf[nRead > 0 ? nRead : 0] = '\0';
  lua_pushlstring(L, pBuf, (nRead > 0) ? nRead : 0);
  free(pBuf);
  return 1;
}

static int file_WriteInternal (lua_State *L, const char *pszMode) {
  const char *pszPath = luaL_checkstring(L, 1);
  size_t nLen = 0;
  const char *pszData = luaL_checklstring(L, 2, &nLen);

  FileHandle_t fh = HL2SB_FileSystem()->Open(pszPath, pszMode, "MOD");
  if (!fh) { lua_pushboolean(L, false); return 1; }

  int nWritten = (nLen > 0) ? HL2SB_FileSystem()->Write(pszData, (int)nLen, fh) : 0;
  HL2SB_FileSystem()->Close(fh);

  lua_pushboolean(L, (int)nLen == nWritten);
  return 1;
}

static int file_Write  (lua_State *L) { return file_WriteInternal(L, "wb"); }
static int file_Append (lua_State *L) { return file_WriteInternal(L, "ab"); }

static int file_Exists (lua_State *L) {
  lua_pushboolean(L, HL2SB_FileSystem()->FileExists(luaL_checkstring(L, 1), "MOD"));
  return 1;
}

static int file_Delete (lua_State *L) {
  HL2SB_FileSystem()->RemoveFile(luaL_checkstring(L, 1), "MOD");
  return 0;
}

static int file_Time (lua_State *L) {
  lua_pushnumber(L, (double)HL2SB_FileSystem()->GetFileTime(luaL_checkstring(L, 1), "MOD"));
  return 1;
}

static int file_Size (lua_State *L) {
  FileHandle_t fh = HL2SB_FileSystem()->Open(luaL_checkstring(L, 1), "rb", "MOD");
  if (!fh) { lua_pushnumber(L, 0); return 1; }
  lua_pushnumber(L, HL2SB_FileSystem()->Size(fh));
  HL2SB_FileSystem()->Close(fh);
  return 1;
}

static int file_IsDir (lua_State *L) {
  lua_pushboolean(L, HL2SB_FileSystem()->IsDirectory(luaL_checkstring(L, 1), "MOD"));
  return 1;
}

static int file_CreateDir (lua_State *L) {
  HL2SB_FileSystem()->CreateDirHierarchy(luaL_checkstring(L, 1), "MOD");
  return 0;
}

// file.Find( path ) -> files, dirs   (GMod returns two tables)
static int file_Find (lua_State *L) {
  char szPattern[512];
  Q_snprintf(szPattern, sizeof(szPattern), "%s/*", luaL_checkstring(L, 1));

  FileFindHandle_t fh;
  const char *pszFound = HL2SB_FileSystem()->FindFirstEx(szPattern, "MOD", &fh);

  lua_newtable(L);                       // files
  int iFiles = 0;
  lua_newtable(L);                       // dirs
  int iDirs = 0;

  while (pszFound) {
    if (pszFound[0] != '.') {
      if (HL2SB_FileSystem()->FindIsDirectory(fh)) {
        lua_pushstring(L, pszFound); lua_rawseti(L, -2, ++iDirs);
      } else {
        lua_pushstring(L, pszFound); lua_rawseti(L, -3, ++iFiles);
      }
    }
    pszFound = HL2SB_FileSystem()->FindNext(fh);
  }
  HL2SB_FileSystem()->FindClose(fh);

  return 2;
}

// file.Open( path [, mode] ) -> FileHandle
//
// lua/includes/extensions/file.lua:7 is implemented ON TOP of this (GMod's file
// extension wraps file.Open), so leaving it out made that whole file fail and
// took extensions/player_auth.lua down with it:
//
//     extensions/player_auth.lua -> lua/includes/extensions/file.lua:7:
//         attempt to call a nil value (field 'Open')
//
// lfilesystem.cpp already binds the FileHandle_t metatable (Close / Read / Write /
// Size / Seek / EndOfFile / Flush / IsOk) plus lua_pushfilehandle, so the handle
// GMod's file.lua expects is exactly the one this engine already has.  Opened
// through the same `filesystem` pointer those methods use, so open and close
// cannot end up on different interfaces.  GMod's mode strings ("r", "w", "a",
// "rb", "wb", "ab", "r+", ...) are Source's own, so they pass straight through.
static int file_Open (lua_State *L) {
  const char *pszPath = luaL_checkstring(L, 1);
  const char *pszMode = luaL_optstring(L, 2, "r");

  lua_pushfilehandle(L, filesystem->Open(pszPath, pszMode, "MOD"));
  return 1;
}


static const luaL_Reg file_funcs[] = {
  {"Open",      file_Open},
  {"Read",      file_Read},
  {"Write",     file_Write},
  {"Append",    file_Append},
  {"Exists",    file_Exists},
  {"Delete",    file_Delete},
  {"Time",      file_Time},
  {"Size",      file_Size},
  {"IsDir",     file_IsDir},
  {"CreateDir", file_CreateDir},
  {"Find",      file_Find},
  {NULL, NULL}
};

LUALIB_API int luaopen_Files (lua_State *L) {
  luaL_register(L, LUA_FILESLIBNAME, file_funcs);
  return 1;
}
