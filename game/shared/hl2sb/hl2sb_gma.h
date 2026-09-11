//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
// $NoKeywords: $
//===========================================================================//

#ifndef HL2SB_GMA_H
#define HL2SB_GMA_H
#ifdef _WIN32
#pragma once
#endif

// Scans addons/*.gma (pathID "MOD") and makes every archive usable exactly as
// if the user had unzipped it into addons/<name>/:
//
//   * the archive is unpacked once into addons/<sanitized header name>/
//     (a marker file .hl2sb_gma skips the work on later runs),
//   * the resulting folder is added to the MOD and GAME search paths, so Lua,
//     materials, models and everything else resolve in the same session,
//   * file CRCs are verified against the archive index when present.
//
// Everything is defensive: a truncated, malformed or hostile archive produces a
// warning and is skipped.  Nothing is ever written outside its own
// addons/<sanitized>/ folder.
//
// Safe (and harmless) to call from both the client and the server DLL; the
// marker file makes the second call a no-op.  Both realms must call it before
// their Lua passes so an addon's Lua is picked up as early as possible.
void HL2SB_MountGMAAddons();

#endif // HL2SB_GMA_H
