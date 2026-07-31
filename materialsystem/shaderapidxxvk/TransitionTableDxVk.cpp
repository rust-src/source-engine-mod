//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Transition Table - Minimal stub (caches state transitions)
//
// The full shaderapidx9 version uses TransitionTable.cpp to minimize redundant
// state changes. For DXVK we start with a minimal stub that tracks state so
// the material system's snapshot API works correctly.
//
//===========================================================================//

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "shaderapi/shareddefs.h"
#include "shaderapi/ishadershadow.h"
#include "materialsystem/imaterial.h"

//-----------------------------------------------------------------------------
// State slot enum (indexes into transition table)
//-----------------------------------------------------------------------------
enum EDxvkStateSlot
{
	DXVK_STATE_DEPTH_FUNC = 0,
	DXVK_STATE_DEPTH_WRITE,
	DXVK_STATE_DEPTH_TEST,
	DXVK_STATE_CULL_ENABLE,
	DXVK_STATE_BLEND_ENABLE,
	DXVK_STATE_BLEND_SRC,
	DXVK_STATE_BLEND_DST,
	DXVK_STATE_ALPHATEST_ENABLE,
	DXVK_STATE_ALPHA_FUNC,
	DXVK_STATE_ALPHA_REF,
	DXVK_STATE_FILL_MODE_FRONT,
	DXVK_STATE_FILL_MODE_BACK,
	DXVK_STATE_COLOR_WRITE,
	DXVK_STATE_ALPHA_WRITE,
	DXVK_STATE_SRGB_WRITE,
	DXVK_STATE_STENCIL_ENABLE,
	DXVK_STATE_STENCIL_FUNC,
	DXVK_STATE_STENCIL_PASS,
	DXVK_STATE_STENCIL_FAIL,
	DXVK_STATE_STENCIL_ZFAIL,
	DXVK_STATE_STENCIL_REF,
	DXVK_STATE_STENCIL_MASK,
	DXVK_STATE_STENCIL_WRITEMASK,
	DXVK_STATE_ALPHA_TO_COVERAGE,

	DXVK_STATE_SLOT_COUNT
};

//-----------------------------------------------------------------------------
// CDxvkTransitionTable
//-----------------------------------------------------------------------------
class CDxvkTransitionTable
{
public:
	CDxvkTransitionTable() { Reset(); }

	void Reset()
	{
		m_nGeneration = 1;
		for ( int i = 0; i < DXVK_STATE_SLOT_COUNT; ++i )
		{
			m_aSlotValue[ i ] = ~0u;
			m_aSlotGeneration[ i ] = 0;
		}
		m_nBoundVShader = 0;
		m_nBoundPShader = 0;
		m_nBoundGShader = 0;
	}

	// Returns true if the state actually changed
	bool SetState( EDxvkStateSlot slot, uint32_t value )
	{
		Assert( slot >= 0 && slot < DXVK_STATE_SLOT_COUNT );
		if ( m_aSlotGeneration[ slot ] != m_nGeneration ||
			 m_aSlotValue[ slot ] != value )
		{
			m_aSlotValue[ slot ] = value;
			m_aSlotGeneration[ slot ] = m_nGeneration;
			return true;
		}
		return false;
	}

	uint32_t GetState( EDxvkStateSlot slot ) const
	{
		if ( m_aSlotGeneration[ slot ] != m_nGeneration )
			return ~0u;
		return m_aSlotValue[ slot ];
	}

	// Shader bindings
	bool SetVertexShader( uintptr_t handle )
	{
		bool changed = ( m_nBoundVShader != handle );
		m_nBoundVShader = handle;
		return changed;
	}
	bool SetPixelShader( uintptr_t handle )
	{
		bool changed = ( m_nBoundPShader != handle );
		m_nBoundPShader = handle;
		return changed;
	}
	bool SetGeometryShader( uintptr_t handle )
	{
		bool changed = ( m_nBoundGShader != handle );
		m_nBoundGShader = handle;
		return changed;
	}

	uintptr_t GetVertexShader()   const { return m_nBoundVShader; }
	uintptr_t GetPixelShader()    const { return m_nBoundPShader; }
	uintptr_t GetGeometryShader() const { return m_nBoundGShader; }

	// Statistics
	void GetStats( uint32_t& nTotalSets, uint32_t& nRedundantSets ) const
	{
		nTotalSets = m_nTotalSets;
		nRedundantSets = m_nRedundantSets;
	}

private:
	uint32_t m_aSlotValue[ DXVK_STATE_SLOT_COUNT ];
	uint32_t m_aSlotGeneration[ DXVK_STATE_SLOT_COUNT ];
	uint32_t m_nGeneration;
	uintptr_t m_nBoundVShader;
	uintptr_t m_nBoundPShader;
	uintptr_t m_nBoundGShader;

	// Counters
	uint32_t m_nTotalSets;
	uint32_t m_nRedundantSets;
};

// Global instance (used by the API wrapper to filter redundant calls)
static CDxvkTransitionTable s_TransitionTable;
CDxvkTransitionTable* DxvkGetTransitionTable() { return &s_TransitionTable; }

//-----------------------------------------------------------------------------
// External helpers
//-----------------------------------------------------------------------------
void DxvkTransitionTable_Reset()
{
	s_TransitionTable.Reset();
}

// Convenience: convert ShaderDepthFunc_t -> uint32 slot value
uint32_t DxvkPackDepthFunc( ShaderDepthFunc_t d ) { return (uint32_t)d; }

uint32_t DxvkPackBlendFactor( ShaderBlendFactor_t f ) { return (uint32_t)f; }

uint32_t DxvkPackStencilOp( ShaderStencilOp_t op ) { return (uint32_t)op; }

uint32_t DxvkPackStencilFunc( ShaderStencilFunc_t f ) { return (uint32_t)f; }
