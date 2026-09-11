//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client-side mirror of CHunterFlechette, the hunter's explosive
//			flechette that HL2SB's ported weapon_flechettegun fires.
//
//			All of the rendering - the model, the skin and the physics state -
//			comes straight from C_PhysicsProp, so the only thing needed here is
//			an empty receive table that pairs up with DT_HunterFlechette on the
//			server. Without a client class for the server class there is nothing
//			for the client to instantiate and the projectile never draws.
//
//=============================================================================//

#include "cbase.h"
#include "c_physicsprop.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// The "BaseClass" typedef is not decoration: BEGIN_RECV_TABLE() in dt_recv.h
// expands to RecvPropDataTable( "baseclass", ..., className::BaseClass::m_pClassRecvTable, ... ),
// so it decides which data table sits underneath DT_HunterFlechette.  Without
// it the inherited typedef from C_PhysicsProp (C_BreakableProp) would be used,
// which no longer matches the server's DT_PhysicsProp base and makes
// RecvTable_BuildHierarchy drop the whole class.
//-----------------------------------------------------------------------------
class C_HunterFlechette : public C_PhysicsProp
{
public:
	typedef C_PhysicsProp BaseClass;

	DECLARE_CLIENTCLASS();
};

IMPLEMENT_CLIENTCLASS_DT( C_HunterFlechette, DT_HunterFlechette, CHunterFlechette )
END_RECV_TABLE()
