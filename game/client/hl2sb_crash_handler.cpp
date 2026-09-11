// hl2sb_crash_handler.cpp
// Crash handler for HL2SB - captures exceptions and writes to log

#include "cbase.h"
#include "hl2sb_crash_handler.h"

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include <exception>
#endif

#include "tier0/minidump.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32

//-----------------------------------------------------------------------------
// The process aborting through the CRT (std::terminate, an invalid parameter,
// a pure virtual call) does NOT go through SetUnhandledExceptionFilter: on
// x64 the UCRT ends in __fastfail(), which the kernel turns into
// STATUS_STACK_BUFFER_OVERRUN (0xC0000409) *without* consulting SEH.  That is
// why a crash of that kind left no minidump behind -- only
// "client.dll, exception 0xc0000409" in the Windows event log.
//
// So intercept the CRT entries themselves, while the process is still healthy
// enough to walk its own stack and write a dump.
//-----------------------------------------------------------------------------

// MINIDUMP_TYPE: DataSegs | IndirectlyReferencedMemory | ProcessThreadData.
// Same shape as the engine's own dumps (~29 MB, enough for a usable stack).
#define HL2SB_MINIDUMP_TYPE ( 0x00000001 | 0x00000040 | 0x00000100 )

static void HL2SB_LogLine( FILE *fp, const char *pszLine )
{
	if ( fp )
	{
		fprintf( fp, "%s\n", pszLine );
		fflush( fp );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Write a crash log entry plus a minidump for a CRT abort path.
//          Called while the process is still alive, so the stack is real.
//-----------------------------------------------------------------------------
static void HL2SB_WriteAbortDump( const char *pszReason, const char *pszDetail )
{
	FILE *fp = fopen( "hl2sb_crash.log", "a" );
	if ( fp )
	{
		fprintf( fp, "\n=== HL2SB Crash (CRT abort) ===\n" );
		fprintf( fp, "Reason: %s\n", pszReason );
		if ( pszDetail )
			fprintf( fp, "Detail: %s\n", pszDetail );

		void *stack[ 96 ];
		unsigned short frames = RtlCaptureStackBackTrace( 1, 96, stack, NULL );
		fprintf( fp, "\nStack Trace (%u frames):\n", frames );
		for ( unsigned int i = 0; i < frames; i++ )
			fprintf( fp, "  %02u: 0x%p\n", i, stack[ i ] );

		fprintf( fp, "\n=== End Crash ===\n" );
		fflush( fp );
		fclose( fp );
	}

	// Minidump: build a synthetic exception record around the current context so
	// the dump has a walkable stack (the fastfail path gives us none).
	CONTEXT ctx;
	RtlCaptureContext( &ctx );

	EXCEPTION_RECORD rec;
	ZeroMemory( &rec, sizeof( rec ) );
	rec.ExceptionCode = 0xC0000409;				// STATUS_STACK_BUFFER_OVERRUN
	rec.ExceptionAddress = (PVOID)ctx.Rip;
	rec.ExceptionFlags = EXCEPTION_NONCONTINUABLE;

	EXCEPTION_POINTERS info;
	info.ExceptionRecord = &rec;
	info.ContextRecord = &ctx;

	WriteMiniDumpUsingExceptionInfo( 0xC0000409, &info, HL2SB_MINIDUMP_TYPE, "abort" );

	Msg( "\n[HL2SB] CRASH (abort): %s%s%s\n", pszReason,
		pszDetail ? " - " : "", pszDetail ? pszDetail : "" );
	Msg( "[HL2SB] Crash log: hl2sb_crash.log, minidump: dumps/\n" );

	char szBox[ 512 ];
	Q_snprintf( szBox, sizeof( szBox ),
		"HL2SB crashed!\n\nReason: %s\n%s\n\nSee hl2sb_crash.log and the dumps/ folder.",
		pszReason, pszDetail ? pszDetail : "" );
	MessageBoxA( NULL, szBox, "HL2SB Crash", MB_OK | MB_ICONERROR );
}

//-----------------------------------------------------------------------------
// std::terminate -- an unhandled C++ exception (this is the usual route to
// abort() in a /EHsc build).
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_TerminateHandler( void )
{
	HL2SB_WriteAbortDump( "std::terminate (unhandled C++ exception)", NULL );
	abort();
}

//-----------------------------------------------------------------------------
// CRT invalid parameter -- sprintf_s/strcpy_s/vsnprintf with bad arguments.
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_InvalidParameterHandler(
	const wchar_t *pszExpression, const wchar_t *pszFunction,
	const wchar_t *pszFile, unsigned int uiLine, uintptr_t /*pReserved*/ )
{
	char szDetail[ 512 ];
	char szExpr[ 192 ] = { 0 };
	char szFunc[ 192 ] = { 0 };

	if ( pszExpression )
		WideCharToMultiByte( CP_ACP, 0, pszExpression, -1, szExpr, sizeof( szExpr ), NULL, NULL );
	if ( pszFunction )
		WideCharToMultiByte( CP_ACP, 0, pszFunction, -1, szFunc, sizeof( szFunc ), NULL, NULL );

	Q_snprintf( szDetail, sizeof( szDetail ), "expr=\"%s\" func=\"%s\" line=%u",
		szExpr, szFunc, uiLine );

	HL2SB_WriteAbortDump( "CRT invalid parameter", szDetail );

	// Do not return: the caller is in an unrecoverable state.
	ExitProcess( 3 );
}

//-----------------------------------------------------------------------------
// Pure virtual call.
//-----------------------------------------------------------------------------
static void __cdecl HL2SB_PurecallHandler( void )
{
	HL2SB_WriteAbortDump( "pure virtual function call", NULL );
	abort();
}

static LONG WINAPI HL2SB_ExceptionFilter( LPEXCEPTION_POINTERS lpExceptionInfo )
{
	char szLogPath[MAX_PATH];
	Q_snprintf( szLogPath, sizeof(szLogPath), "hl2sb_crash.log" );

	// Write crash info to log
	FILE *fp = fopen( szLogPath, "a" );
	if ( fp )
	{
		fprintf( fp, "\n=== HL2SB Crash ===\n" );
		fprintf( fp, "Exception Code: 0x%08X\n", (unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode );
		fprintf( fp, "Exception Address: 0x%p\n", lpExceptionInfo->ExceptionRecord->ExceptionAddress );
		fprintf( fp, "Exception Flags: %u\n", lpExceptionInfo->ExceptionRecord->ExceptionFlags );
		fprintf( fp, "Number Parameters: %u\n", lpExceptionInfo->ExceptionRecord->NumberParameters );
		
		// Capture stack
		void *stack[64];
		unsigned short frames = RtlCaptureStackBackTrace( 1, 64, stack, NULL );
		
		fprintf( fp, "\nStack Trace (%u frames):\n", frames );
		for ( unsigned int i = 0; i < frames; i++ )
		{
			fprintf( fp, "  %02u: 0x%p\n", i, stack[i] );
		}
		
		fprintf( fp, "\n=== End Crash ===\n" );
		fclose( fp );
	}

	// A real minidump for SEH-reachable exceptions too.
	WriteMiniDumpUsingExceptionInfo(
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo, HL2SB_MINIDUMP_TYPE, "hl2sb" );

	// Also write to console
	Msg( "\n[HL2SB] CRASH DETECTED! Exception 0x%08X at 0x%p\n", 
		(unsigned int)lpExceptionInfo->ExceptionRecord->ExceptionCode,
		lpExceptionInfo->ExceptionRecord->ExceptionAddress );
	Warning( "[HL2SB] Crash log written to hl2sb_crash.log\n" );
	
	// Show message box
#ifdef _WIN32
	MessageBoxA( NULL, 
		"HL2SB crashed!\nCheck hl2sb_crash.log for details.", 
		"HL2SB Crash", 
		MB_OK | MB_ICONERROR );
#endif
	
	// Return EXCEPTION_EXECUTE_HANDLER to terminate gracefully
	return EXCEPTION_EXECUTE_HANDLER;
}

void HL2SB_InstallCrashHandler( void )
{
	SetUnhandledExceptionFilter( HL2SB_ExceptionFilter );

	// The abort paths that never reach the filter above.
	std::set_terminate( HL2SB_TerminateHandler );
	_set_invalid_parameter_handler( HL2SB_InvalidParameterHandler );
	_set_purecall_handler( HL2SB_PurecallHandler );

	Msg( "[HL2SB] Crash handler installed (SEH + terminate/invalid-parameter/purecall)\n" );
}

#else

void HL2SB_InstallCrashHandler( void )
{
	// Non-Windows: no op
}

#endif
