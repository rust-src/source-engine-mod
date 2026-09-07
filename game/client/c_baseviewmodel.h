//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side view model implementation. Responsible for drawing
//			the view model.
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_BASEVIEWMODEL_H
#define C_BASEVIEWMODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"
#include "utlvector.h"
#include "baseviewmodel_shared.h"

#if defined( CLIENT_DLL )

// Forward declaration
class C_ViewmodelAttachment;

// The hands attachment members live on CBaseViewModel (shared header) inside a
// CLIENT_DLL block - that is where UpdateHandsAttachment() uses them. The one
// extra piece of state, m_bHandsHeldForVehicle, is deliberately NOT stored in a
// class member: adding fields to this shared class changes its layout and the
// incremental build has produced mixed-offset binaries from that before (heap
// corruption). It lives in a file-static slot array indexed by entindex below.
#endif // CLIENT_DLL

#endif // C_BASEVIEWMODEL_H
