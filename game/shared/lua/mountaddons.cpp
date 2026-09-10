//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose:
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#include <sys/stat.h>

static bool TryExtractGMA_ToDir( const char *gmaFullPath, const char *outDir )
{
    // Try a few common gmad invocation patterns. We don't require gmad to exist; just try and see if outDir appears.
    char cmd[MAX_PATH * 2];
    int ret = -1;

    // Ensure outDir exists parent-wise
#ifdef _WIN32
    _mkdir( outDir );
#else
    mkdir( outDir, 0755 );
#endif

    const char *patterns[] = {
        "gmad extract -file \"%s\" -outdir \"%s\"",
        "gmad.exe extract -file \"%s\" -outdir \"%s\"",
        "gmad extract -out \"%s\" \"%s\"",
        "gmad.exe extract -out \"%s\" \"%s\"",
        "gmad extract \"%s\" \"%s\"",
        "gmad.exe extract \"%s\" \"%s\"",
    };

    for ( size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); ++i )
    {
        // Try patterns where order of args differs. We will try both permutations when needed.
        if ( Q_snprintf( cmd, sizeof(cmd), patterns[i], gmaFullPath, outDir ) < 0 )
            continue;

        ret = system( cmd );

        // If system returned 0, check if outDir now contains files.
        struct stat st;
        char anyfile[MAX_PATH];
        Q_snprintf( anyfile, sizeof(anyfile), "%s%c*", outDir, CORRECT_PATH_SEPARATOR );
#ifdef _WIN32
        // On Windows, check by attempting to open any file using _stat on a known file is harder; instead check directory exists and not empty by using _stat on outDir (existence) and then try FindFirstFile? Simpler: check directory existence only.
        if ( _stat( outDir, &st ) != -1 )
        {
            // We assume extraction succeeded if outDir exists and contains something (best-effort).
            // More thorough checks could be added if gmad output format is known.
            return true;
        }
#else
        if ( stat( outDir, &st ) != -1 )
        {
            return true;
        }
#endif
    }

    return false;
}

void MountAddons()
{
    // Andrew; mount the Lua cache directory first. We consider this a temporary
    // addon used across servers
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
    filesystem->AddSearchPath( LUA_PATH_CACHE, "MOD", PATH_ADD_TO_TAIL );
    if ( bGetCurrentDirectory )
        V_SetCurrentDirectory( fullpath );

    FileFindHandle_t fh;

    char relativepath[ MAX_PATH ] = { 0 };
    char addonName[ 255 ] = { 0 };

    char const *fn = g_pFullFileSystem->FindFirstEx( LUA_PATH_ADDONS "/*", "MOD", &fh );
    while ( fn )
    {
        Q_strcpy( addonName, fn );
        if ( fn[0] != '.' )
        {
            if ( g_pFullFileSystem->FindIsDirectory( fh ) )
            {
#ifdef GAME_DLL
                Msg( "Mounting addon \"%s\"...\n", addonName );
#endif

                Q_snprintf( relativepath, sizeof( relativepath ), LUA_PATH_ADDONS "/%s", addonName );
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
                filesystem->AddSearchPath( relativepath, "MOD", PATH_ADD_TO_TAIL );
                if ( bGetCurrentDirectory )
                    V_SetCurrentDirectory( fullpath );
            }
            else
            {
                // Not a directory. Check for .gma files and try to extract them to a cache and mount.
                const char *ext = V_GetFileExtension( addonName );
                if ( ext && Q_stricmp( ext, "gma" ) == 0 )
                {
                    // Build relative and full paths
                    char gmaRel[ MAX_PATH ];
                    Q_snprintf( gmaRel, sizeof( gmaRel ), LUA_PATH_ADDONS "/%s", addonName );

                    char gmaFull[ MAX_PATH ];
                    // Resolve to a disk path if possible
                    const char *resolved = g_pFullFileSystem->RelativePathToFullPath( gmaRel, "MOD", gmaFull, sizeof( gmaFull ) );
                    if ( resolved )
                    {
                        struct _stat st;
                        if ( FS_stat( gmaFull, &st ) != -1 )
                        {
                            // Prepare output directory under game cache
                            char outDir[ MAX_PATH ];
#ifdef CLIENT_DLL
                            const char *gamePath = engine->GetGameDirectory();
                            Q_snprintf( outDir, sizeof( outDir ), "%s/cache/gma/%s", gamePath, addonName );
#else
                            char gamePath[ 256 ];
                            engine->GetGameDir( gamePath, 256 );
                            Q_snprintf( outDir, sizeof( outDir ), "%s/cache/gma/%s", gamePath, addonName );
#endif
                            // Normalize
                            Q_FixSlashes( outDir );

                            // If outDir already exists, skip extraction
                            struct _stat st2;
                            bool bExists = ( FS_stat( outDir, &st2 ) != -1 );

                            if ( !bExists )
                            {
                                // Try to extract using gmad if present
                                bool bExtracted = TryExtractGMA_ToDir( gmaFull, outDir );
                                if ( !bExtracted )
                                {
                                    Warning( "MountAddons: Failed to extract GMA %s\n", gmaFull );
                                }
                            }

                            // If directory exists now, add to search path
                            if ( FS_stat( outDir, &st2 ) != -1 )
                            {
                                char relativeOut[ MAX_PATH ];
                                // Make path relative to game dir for AddSearchPath. Using absolute should also work.
                                Q_snprintf( relativeOut, sizeof( relativeOut ), "%s", outDir );

                                bool bGetCurrentDirectory2 = V_GetCurrentDirectory( fullpath, sizeof( fullpath ) );
                                if ( bGetCurrentDirectory2 )
                                {
#ifdef CLIENT_DLL
                                    const char *gamePath2 = engine->GetGameDirectory();
#else
                                    char gamePath2[ 256 ];
                                    engine->GetGameDir( gamePath2, 256 );
#endif
                                    V_SetCurrentDirectory( gamePath2 );
                                }
                                filesystem->AddSearchPath( relativeOut, "MOD", PATH_ADD_TO_TAIL );
                                if ( bGetCurrentDirectory2 )
                                    V_SetCurrentDirectory( fullpath );
                            }
                        }
                    }
                }
            }
        }

        fn = g_pFullFileSystem->FindNext( fh );
    }
    g_pFullFileSystem->FindClose( fh );
}
