//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style raw image textures.
//
//          GMod's material system reads .png / .jpg / .tga / .bmp files
//          directly, so GMod content routinely writes
//
//              Material( "gwenskin/GModDefault.png" )
//              "$basetexture" "hud/killicons/default.png"
//
//          Stock Source can only read .vtf, so those names used to fall
//          through to the error material.  These helpers decode the image
//          into a procedurally generated texture instead.
//
//=======================================================================================//

#ifndef HL2SB_PNGTEXTURE_H
#define HL2SB_PNGTEXTURE_H
#ifdef _WIN32
#pragma once
#endif

class ITextureRegenerator;

// True if the name ends in an extension stb_image can decode.
bool HL2SB_IsImageFileName( const char *pFileName );

// Resolves a texture name to the logical texture name of an image file that
// actually exists ( "<dir>/<name>.png" relative to materials/ ), accepting the
// name with or without its extension, and with or without a leading
// "materials/".  Returns false when no image file exists for the name.
bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize );

// Decodes the logical texture name into a new regenerator.  The caller owns the
// returned regenerator (hand it to ITextureInternal::CreateProceduralTexture).
ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, int *pOutWidth, int *pOutHeight );

#endif // HL2SB_PNGTEXTURE_H
