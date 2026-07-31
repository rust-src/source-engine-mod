//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Copyright 2011-2014 Valve Corporation
//  All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//------------------------------------------------------------------------------
// DX9AsmToSPV.cpp
//
// A first-cut DX9 shader bytecode -> SPIR-V translator.
//
// The translator consumes the same uint32 token stream that D3DToGL consumes
// (produced by D3DX / the abstracted shader pipeline) and emits a SPIR-V
// binary. It targets correctness for the common ALU opcodes (mov, add, sub,
// mul, mad, dp3, dp4, rcp, rsq, min, max, abs, frc, exp, log, pow, crs, lrp,
// cmp, slt, sge) and basic texture fetches (texld / texldl). More involved
// control flow (loops, subroutine calls, address-register indirection) and
// exotic opcodes are skipped as no-ops using the instruction length field so
// the resulting module stays well formed.
//------------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dx9asmvtospv.h"

// A couple of SPIR-V / GLSLstd.450 constants used only by this .cpp. They are
// kept out of the public header to avoid bloating it.
#define GLSLstd450FMix         46
#define GLSLstd450Pow          26

#define SPV_D3DSP_TEXTURETYPE_MASK   0x78000000      // bits 27..29 (D3DSAMPLER_TEXTURE_TYPE)
#define SPV_D3DSTT_2D        2
#define SPV_D3DSTT_CUBE      3
#define SPV_D3DSTT_3D        4

// Default number of vec4 constant registers exposed through the uniform block.
#define SPV_DEFAULT_CONST_COUNT  256

//------------------------------------------------------------------------------
// helpers
//------------------------------------------------------------------------------
static uint32_t SpvFloatBits( float f )
{
	uint32_t u;
	memcpy( &u, &f, sizeof(u) );
	return u;
}

static uint32_t SpvStringWordCount( const char* pStr )
{
	size_t len = strlen( pStr );
	return (uint32_t)( ( len + 1 + 3 ) / 4 );
}


//------------------------------------------------------------------------------
// CD3DToVK construction
//------------------------------------------------------------------------------
CD3DToVK::CD3DToVK()
{
	Reset();
}

CD3DToVK::~CD3DToVK()
{
}

void CD3DToVK::Reset()
{
	m_pdwBaseToken = nullptr;
	m_pdwNextToken = nullptr;
	m_pdwEndToken  = nullptr;

	m_bVertexShader = false;
	m_dwMajorVersion = 0;
	m_dwMinorVersion = 0;
	m_dwOptions = 0;
	m_bSpew = false;
	m_bDoFixupZ = false;
	m_bDoFixupY = false;
	m_bDoUserClipPlanes = false;
	m_bGenerateSRGBWriteSuffix = false;
	m_bGenerateBoneUniformBuffer = false;

	m_SPIRV.clear();
	m_NextId = 1;                 // SPIR-V ids start at 1
	m_BoundIndexInHeader = 0;

	m_IdTypeVoid = 0;
	m_IdTypeBool = 0;
	m_IdTypeBoolVec4 = 0;
	m_IdTypeFloat = 0;
	m_IdTypeVec2 = 0;
	m_IdTypeVec3 = 0;
	m_IdTypeVec4 = 0;
	m_IdTypeInt32 = 0;
	m_IdTypeVoidFunc = 0;
	m_IdPtrInputVec4 = 0;
	m_IdPtrOutputVec4 = 0;
	m_IdPtrFuncVec4 = 0;
	m_IdPtrUniformVec4 = 0;
	m_IdConstFloatZero = 0;
	m_IdConstFloatOne = 0;
	m_IdConstVec4Zero = 0;
	m_IdConstVec4One = 0;
	m_IdGLSLstd450 = 0;

	for ( int i = 0; i < MAX_SPV_TEMPS; ++i )            m_TempVarIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_VS_INPUTS; ++i )        m_VSInputIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_PS_INPUTS; ++i )        m_PSInputIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_PS_COLOR_OUT; ++i )     m_PSOutputColorIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_SAMPLERS; ++i )         m_SamplerVarIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_SAMPLERS; ++i )         m_SamplerImageTypeIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_SAMPLERS; ++i )         m_SamplerSampledTypeIds[i] = 0;
	for ( int i = 0; i < MAX_SPV_SAMPLERS; ++i )         m_dwSamplerTypes[i] = SPV_SAMPLER_TYPE_UNUSED;
	for ( int i = 0; i < MAX_SPV_VS_TEXCOORD_OUT; ++i )  m_VSOutputTexCoordIds[i] = 0;

	m_VSOutputPosId = 0;
	m_VSOutputColorIds[0] = m_VSOutputColorIds[1] = 0;
	m_VSOutputFogId = 0;
	m_PSFragCoordId = 0;
	m_PSOutputDepthId = 0;
	m_ConstUboStructId = 0;
	m_ConstUboVarId = 0;

	m_dwSamplerUsageMask = 0;
	m_dwConstUsageMask = 0;
	m_dwConstIntUsageMask = 0;
	m_dwConstBoolUsageMask = 0;
	m_dwTempUsageMask = 0;
	m_dwVertexAttribMask = 0;
	m_dwTexCoordOutMask = 0;
	m_dwVSOutputColorMask = 0;
	m_dwPSInputMask = 0;
	m_dwPSOutputColorMask = 0;
	m_bVSOutputPos = false;
	m_bPSOutputDepth = false;
	m_nCentroidMask = 0;

	for ( int i = 0; i < MAX_SPV_VS_INPUTS; ++i ) m_dwAttribMap[i] = 0xFFFFFFFFu;

	m_EntryFuncId = 0;
	m_EntryLabelId = 0;
	m_InterfaceVars.clear();
}


//------------------------------------------------------------------------------
// public accessors
//------------------------------------------------------------------------------
const uint32_t* CD3DToVK::GetSPIRV() const
{
	return m_SPIRV.empty() ? nullptr : m_SPIRV.data();
}

size_t CD3DToVK::GetSPIRVWordCount() const
{
	return m_SPIRV.size();
}

const std::vector<uint32_t>& CD3DToVK::GetSPIRVVector() const
{
	return m_SPIRV;
}

void CD3DToVK::ClearSPIRV()
{
	m_SPIRV.clear();
}

uint32_t CD3DToVK::GetSamplerType( uint32_t nSampler ) const
{
	if ( nSampler >= MAX_SPV_SAMPLERS )
		return SPV_SAMPLER_TYPE_UNUSED;
	return m_dwSamplerTypes[nSampler];
}


//------------------------------------------------------------------------------
// DX9 token stream
//------------------------------------------------------------------------------
uint32_t CD3DToVK::GetNextToken()
{
	if ( m_pdwNextToken == nullptr )
		return 0;
	if ( m_pdwEndToken != nullptr && m_pdwNextToken >= m_pdwEndToken )
		return SPV_D3DPS_END();
	uint32_t tok = *m_pdwNextToken;
	m_pdwNextToken++;
	return tok;
}

void CD3DToVK::SkipTokens( uint32_t numToSkip )
{
	if ( m_pdwNextToken == nullptr )
		return;
	m_pdwNextToken += numToSkip;
	if ( m_pdwEndToken != nullptr && m_pdwNextToken > m_pdwEndToken )
		m_pdwNextToken = const_cast<uint32_t*>( m_pdwEndToken );
}

bool CD3DToVK::AtEnd() const
{
	if ( m_pdwNextToken == nullptr )
		return true;
	if ( m_pdwEndToken != nullptr && m_pdwNextToken >= m_pdwEndToken )
		return true;
	return false;
}

uint32_t CD3DToVK::Opcode( uint32_t dwToken )
{
	return dwToken & SPV_D3DSI_OPCODE_MASK;
}

uint32_t CD3DToVK::OpcodeLength( uint32_t dwToken )
{
	// Total DWORDs in the instruction, including the opcode token.
	return ( dwToken & SPV_D3DSI_INSTLENGTH_MASK ) >> SPV_D3DSI_INSTLENGTH_SHIFT;
}

uint32_t CD3DToVK::GetRegType( uint32_t dwRegToken )
{
	uint32_t typeLow  = ( dwRegToken & SPV_D3DSP_REGTYPE_MASK )  >> SPV_D3DSP_REGTYPE_SHIFT;
	uint32_t typeHigh = ( dwRegToken & SPV_D3DSP_REGTYPE_MASK2 ) >> SPV_D3DSP_REGTYPE_SHIFT2;
	return typeLow | ( typeHigh << 3 );
}

uint32_t CD3DToVK::GetRegNum( uint32_t dwRegToken )
{
	return dwRegToken & SPV_D3DSP_REGNUM_MASK;
}

uint32_t CD3DToVK::GetWriteMask( uint32_t dwRegToken )
{
	uint32_t mask = ( dwRegToken & SPV_D3DSP_WRITEMASK_ALL ) >> 16;
	if ( mask == 0 )
		mask = 0xF;     // no explicit mask == write all
	return mask;
}

uint32_t CD3DToVK::GetSrcSwizzle( uint32_t dwRegToken )
{
	return ( dwRegToken & SPV_D3DSP_SWIZZLE_MASK ) >> SPV_D3DSP_SWIZZLE_SHIFT;
}

uint32_t CD3DToVK::GetSrcModifier( uint32_t dwRegToken )
{
	return ( dwRegToken & SPV_D3DSP_SRCMOD_MASK ) >> SPV_D3DSP_SRCMOD_SHIFT;
}

uint32_t CD3DToVK::GetDstModifier( uint32_t dwRegToken )
{
	return ( dwRegToken & SPV_D3DSP_DSTMOD_MASK ) >> SPV_D3DSP_DSTMOD_SHIFT;
}

uint32_t CD3DToVK::TextureType( uint32_t dwToken )
{
	return ( dwToken & SPV_D3DSP_TEXTURETYPE_MASK ) >> 27;
}


//------------------------------------------------------------------------------
// SPIR-V emit helpers
//------------------------------------------------------------------------------
uint32_t CD3DToVK::AllocId()
{
	return m_NextId++;
}

void CD3DToVK::EmitWord( uint32_t word )
{
	m_SPIRV.push_back( word );
}

void CD3DToVK::EmitOp( uint16_t opcode, const uint32_t* pOperands, uint32_t operandCount )
{
	uint32_t wordCount = 1u + operandCount;
	EmitWord( wordCount | ( (uint32_t)opcode << 16 ) );
	for ( uint32_t i = 0; i < operandCount; ++i )
		EmitWord( pOperands[i] );
}

void CD3DToVK::EmitOp( uint16_t opcode, const std::vector<uint32_t>& operands )
{
	EmitOp( opcode, operands.empty() ? nullptr : operands.data(), (uint32_t)operands.size() );
}

void CD3DToVK::EmitString( const char* pStr )
{
	// SPIR-V literal strings: little-endian, NUL terminated, padded with zeroes
	// to fill the final word. The NUL itself is part of the string, so when it
	// lands on a word boundary no extra padding word is required.
	uint32_t accum = 0;
	uint32_t shift = 0;
	size_t len = strlen( pStr );
	for ( size_t i = 0; i <= len; ++i )        // include the NUL terminator
	{
		char c = ( i < len ) ? pStr[i] : 0;
		accum |= ( (uint32_t)(uint8_t)c ) << shift;
		shift += 8;
		if ( shift == 32 )
		{
			EmitWord( accum );
			accum = 0;
			shift = 0;
		}
	}
	if ( shift != 0 )
		EmitWord( accum );
}

void CD3DToVK::EmitVectorShuffle( uint32_t resultType, uint32_t result,
                                  uint32_t v1, uint32_t v2,
                                  const uint32_t* pComponents, uint32_t nComponents )
{
	// OpVectorShuffle: resultType, result, vector1, vector2, then nComponents
	// component indices.
	uint32_t operandCount = 4u + nComponents;
	EmitWord( ( 1u + operandCount ) | ( (uint32_t)SpvOpVectorShuffle << 16 ) );
	EmitWord( resultType );
	EmitWord( result );
	EmitWord( v1 );
	EmitWord( v2 );
	for ( uint32_t i = 0; i < nComponents; ++i )
		EmitWord( pComponents[i] );
}


//------------------------------------------------------------------------------
// Constant emission
//------------------------------------------------------------------------------
uint32_t CD3DToVK::GetConstInt32( int32_t value )
{
	uint32_t id = AllocId();
	uint32_t operands[3] = { m_IdTypeInt32, id, (uint32_t)value };
	EmitOp( SpvOpConstant, operands, 3 );
	return id;
}

uint32_t CD3DToVK::GetConstFloat( float value )
{
	uint32_t id = AllocId();
	uint32_t operands[3] = { m_IdTypeFloat, id, SpvFloatBits( value ) };
	EmitOp( SpvOpConstant, operands, 3 );
	return id;
}

uint32_t CD3DToVK::GetConstVec4( float x, float y, float z, float w )
{
	uint32_t fx = GetConstFloat( x );
	uint32_t fy = GetConstFloat( y );
	uint32_t fz = GetConstFloat( z );
	uint32_t fw = GetConstFloat( w );
	uint32_t id = AllocId();
	uint32_t operands[6] = { m_IdTypeVec4, id, fx, fy, fz, fw };
	EmitOp( SpvOpConstantComposite, operands, 6 );
	return id;
}


//------------------------------------------------------------------------------
// Translate() - main entry point
//------------------------------------------------------------------------------
int CD3DToVK::Translate( const uint32_t* pCode, uint32_t nCodeSize, bool* pbVertexShader, uint32_t options )
{
	Reset();

	if ( pCode == nullptr )
		return DX9ToVK_ERROR;

	m_dwOptions = options;
	m_bSpew = ( options & DX9ToVK_OptionSpew ) != 0;
	m_bDoFixupZ = ( options & DX9ToVK_OptionDoFixupZ ) != 0;
	m_bDoFixupY = ( options & DX9ToVK_OptionDoFixupY ) != 0;
	m_bDoUserClipPlanes = ( options & DX9ToVK_OptionDoUserClipPlanes ) != 0;
	m_bGenerateSRGBWriteSuffix = ( options & DX9ToVK_OptionSRGBWriteSuffix ) != 0;
	m_bGenerateBoneUniformBuffer = ( options & DX9ToVK_OptionGenerateBoneUniformBuffer ) != 0;

	m_pdwBaseToken = pCode;
	m_pdwNextToken = pCode;
	m_pdwEndToken  = ( nCodeSize > 0 ) ? ( pCode + nCodeSize ) : nullptr;

	// First token is always the version token.
	uint32_t dwVersionToken = GetNextToken();
	if ( DecodeVersionToken( dwVersionToken ) != DX9ToVK_OK )
		return DX9ToVK_ERROR;

	if ( pbVertexShader )
		*pbVertexShader = m_bVertexShader;

	if ( m_bSpew )
		std::printf( "\n************* translating shader (vs=%d %d.%d)\n", m_bVertexShader ? 1 : 0, m_dwMajorVersion, m_dwMinorVersion );

	// Pass 1: walk the bytecode and record resource usage so we can emit
	// type/variable declarations before the function body.
	CollectUsage( pCode, nCodeSize );

	// Pass 2: emit the SPIR-V module.
	EmitPrologue();
	EmitTypesAndGlobals();
	EmitFunction( pCode, nCodeSize );
	FinalizeHeader();

	return DX9ToVK_OK;
}

int CD3DToVK::DecodeVersionToken( uint32_t dwToken )
{
	m_dwMajorVersion = SPV_D3DSHADER_VERSION_MAJOR( dwToken );
	m_dwMinorVersion = SPV_D3DSHADER_VERSION_MINOR( dwToken );

	if ( ( dwToken & 0xFFFF0000 ) == 0xFFFF0000 )
	{
		m_bVertexShader = false;     // pixel shader
	}
	else if ( ( dwToken & 0xFFFF0000 ) == 0xFFFE0000 )
	{
		m_bVertexShader = true;      // vertex shader
		m_bGenerateSRGBWriteSuffix = false;
	}
	else
	{
		// Unrecognised version token.
		return DX9ToVK_ERROR;
	}
	return DX9ToVK_OK;
}


//------------------------------------------------------------------------------
// Pass 1: collect usage
//
// Walk the token stream once to record which temps / inputs / outputs /
// samplers / constants are referenced and what texture type each sampler
// uses. This drives the global declarations emitted later.
//------------------------------------------------------------------------------
void CD3DToVK::CollectUsage( const uint32_t* pCode, uint32_t nCodeSize )
{
	const uint32_t* pCur = pCode + 1;   // skip version token
	const uint32_t* pEnd = ( nCodeSize > 0 ) ? ( pCode + nCodeSize ) : nullptr;

	while ( pEnd == nullptr || pCur < pEnd )
	{
		uint32_t dwToken = *pCur;
		if ( dwToken == SPV_D3DPS_END() )
			break;

		uint32_t op = Opcode( dwToken );
		uint32_t len = OpcodeLength( dwToken );
		if ( len == 0 )
		{
			// Malformed / NOP; advance by 1 to avoid spinning.
			pCur++;
			continue;
		}

		const uint32_t* pArgs = pCur + 1;
		uint32_t nArgs = ( len > 1 ) ? ( len - 1 ) : 0;

		// DCL carries the texture type for samplers and the usage semantic for
		// vertex inputs, so handle it specially.
		if ( op == SPV_D3DSIO_DCL && nArgs >= 2 )
		{
			uint32_t dwInfo   = pArgs[0];
			uint32_t dwRegTok = pArgs[1];
			uint32_t regType  = GetRegType( dwRegTok );
			uint32_t regNum   = GetRegNum( dwRegTok );

			if ( regType == SPV_D3DSPR_SAMPLER )
			{
				uint32_t texType = TextureType( dwInfo );
				uint32_t samplerTag = SPV_SAMPLER_TYPE_UNUSED;
				if ( texType == SPV_D3DSTT_2D )       samplerTag = SPV_SAMPLER_TYPE_2D;
				else if ( texType == SPV_D3DSTT_CUBE ) samplerTag = SPV_SAMPLER_TYPE_CUBE;
				else if ( texType == SPV_D3DSTT_3D )   samplerTag = SPV_SAMPLER_TYPE_3D;
				if ( regNum < MAX_SPV_SAMPLERS )
					m_dwSamplerTypes[regNum] = samplerTag;
				m_dwSamplerUsageMask |= ( 1u << regNum );
			}
			else if ( regType == SPV_D3DSPR_INPUT && m_bVertexShader )
			{
				if ( regNum < MAX_SPV_VS_INPUTS )
				{
					m_dwVertexAttribMask |= ( 1u << regNum );
					// dwInfo upper bits carry (usage<<16)|index in the low
					// portion of the dcl usage token; stash a compact tag.
					m_dwAttribMap[regNum] = ( dwInfo >> 16 ) & 0xFFFF;
				}
			}
			else if ( regType == SPV_D3DSPR_INPUT && !m_bVertexShader )
			{
				if ( regNum < MAX_SPV_PS_INPUTS )
					m_dwPSInputMask |= ( 1u << regNum );
			}
			else if ( ( regType == SPV_D3DSPR_RASTOUT ) && m_bVertexShader )
			{
				if ( regNum == SPV_D3DSRO_POSITION ) m_bVSOutputPos = true;
			}
			else if ( ( regType == SPV_D3DSPR_ATTROUT ) && m_bVertexShader )
			{
				if ( regNum < 2 ) m_dwVSOutputColorMask |= ( 1u << regNum );
			}
			else if ( ( regType == SPV_D3DSPR_TEXCRDOUT ) && m_bVertexShader )
			{
				if ( regNum < MAX_SPV_VS_TEXCOORD_OUT )
					m_dwTexCoordOutMask |= ( 1u << regNum );
			}
			else if ( regType == SPV_D3DSPR_COLOROUT && !m_bVertexShader )
			{
				if ( regNum < MAX_SPV_PS_COLOR_OUT )
					m_dwPSOutputColorMask |= ( 1u << regNum );
			}
			else if ( regType == SPV_D3DSPR_DEPTHOUT && !m_bVertexShader )
			{
				m_bPSOutputDepth = true;
			}
		}

		// Walk the operand tokens and mark temp / const / sampler usage.
		for ( uint32_t i = 0; i < nArgs; ++i )
		{
			uint32_t regTok  = pArgs[i];
			uint32_t regType = GetRegType( regTok );
			uint32_t regNum  = GetRegNum( regTok );
			switch ( regType )
			{
			case SPV_D3DSPR_TEMP:
				if ( regNum < MAX_SPV_TEMPS )
					m_dwTempUsageMask |= ( 1u << regNum );
				break;
			case SPV_D3DSPR_CONST:
				if ( regNum < MAX_SPV_CONSTANTS )
					m_dwConstUsageMask |= ( 1u << ( regNum & 31 ) );
				break;
			case SPV_D3DSPR_CONSTINT:
				if ( regNum < 32 ) m_dwConstIntUsageMask |= ( 1u << regNum );
				break;
			case SPV_D3DSPR_SAMPLER:
				if ( regNum < MAX_SPV_SAMPLERS )
				{
					m_dwSamplerUsageMask |= ( 1u << regNum );
					if ( m_dwSamplerTypes[regNum] == SPV_SAMPLER_TYPE_UNUSED )
						m_dwSamplerTypes[regNum] = SPV_SAMPLER_TYPE_2D;  // default
				}
				break;
			case SPV_D3DSPR_INPUT:
				if ( m_bVertexShader )
				{
					if ( regNum < MAX_SPV_VS_INPUTS ) m_dwVertexAttribMask |= ( 1u << regNum );
				}
				else
				{
					if ( regNum < MAX_SPV_PS_INPUTS ) m_dwPSInputMask |= ( 1u << regNum );
				}
				break;
			case SPV_D3DSPR_RASTOUT:
				if ( m_bVertexShader && regNum == SPV_D3DSRO_POSITION ) m_bVSOutputPos = true;
				break;
			case SPV_D3DSPR_ATTROUT:
				if ( m_bVertexShader && regNum < 2 ) m_dwVSOutputColorMask |= ( 1u << regNum );
				break;
			case SPV_D3DSPR_TEXCRDOUT:   // SPV_D3DSPR_OUTPUT is the same value (6)
				if ( m_bVertexShader && regNum < MAX_SPV_VS_TEXCOORD_OUT )
					m_dwTexCoordOutMask |= ( 1u << regNum );
				break;
			case SPV_D3DSPR_COLOROUT:
				if ( !m_bVertexShader && regNum < MAX_SPV_PS_COLOR_OUT )
					m_dwPSOutputColorMask |= ( 1u << regNum );
				break;
			case SPV_D3DSPR_DEPTHOUT:
				if ( !m_bVertexShader ) m_bPSOutputDepth = true;
				break;
			default:
				break;
			}
		}

		pCur += len;
	}
}


//------------------------------------------------------------------------------
// Pass 2a: prologue (header, capabilities, memory model, entry point)
//
// Global variable ids are reserved up front so the entry point can list them
// before they are defined in the types/globals section (SPIR-V permits forward
// references to <id>s).
//------------------------------------------------------------------------------
void CD3DToVK::AddInterfaceVar( uint32_t id )
{
	if ( id != 0 )
		m_InterfaceVars.push_back( id );
}

void CD3DToVK::EmitPrologue()
{
	// Reserve ids for global variables and the entry function/label up front.
	m_IdGLSLstd450 = AllocId();
	m_EntryFuncId  = AllocId();
	m_EntryLabelId = AllocId();

	if ( m_bVertexShader )
	{
		for ( uint32_t i = 0; i < MAX_SPV_VS_INPUTS; ++i )
			if ( m_dwVertexAttribMask & ( 1u << i ) )
			{
				m_VSInputIds[i] = AllocId();
				AddInterfaceVar( m_VSInputIds[i] );
			}

		if ( m_bVSOutputPos )            { m_VSOutputPosId = AllocId(); AddInterfaceVar( m_VSOutputPosId ); }
		for ( uint32_t i = 0; i < 2; ++i )
			if ( m_dwVSOutputColorMask & ( 1u << i ) )
			{
				m_VSOutputColorIds[i] = AllocId();
				AddInterfaceVar( m_VSOutputColorIds[i] );
			}
		for ( uint32_t i = 0; i < MAX_SPV_VS_TEXCOORD_OUT; ++i )
			if ( m_dwTexCoordOutMask & ( 1u << i ) )
			{
				m_VSOutputTexCoordIds[i] = AllocId();
				AddInterfaceVar( m_VSOutputTexCoordIds[i] );
			}
	}
	else
	{
		for ( uint32_t i = 0; i < MAX_SPV_PS_INPUTS; ++i )
			if ( m_dwPSInputMask & ( 1u << i ) )
			{
				m_PSInputIds[i] = AllocId();
				AddInterfaceVar( m_PSInputIds[i] );
			}
		for ( uint32_t i = 0; i < MAX_SPV_PS_COLOR_OUT; ++i )
			if ( m_dwPSOutputColorMask & ( 1u << i ) )
			{
				m_PSOutputColorIds[i] = AllocId();
				AddInterfaceVar( m_PSOutputColorIds[i] );
			}
		if ( m_bPSOutputDepth ) { m_PSOutputDepthId = AllocId(); AddInterfaceVar( m_PSOutputDepthId ); }
	}

	// Constant UBO and samplers.
	m_ConstUboVarId = AllocId();
	for ( uint32_t i = 0; i < MAX_SPV_SAMPLERS; ++i )
		if ( m_dwSamplerUsageMask & ( 1u << i ) )
			m_SamplerVarIds[i] = AllocId();

	// --- SPIR-V header (5 words). Bound is patched up in FinalizeHeader. ---
	EmitWord( SPV_MAGIC_NUMBER );
	EmitWord( SPV_VERSION_1_0 );
	EmitWord( SPV_GENERATOR_ID );
	m_BoundIndexInHeader = (uint32_t)m_SPIRV.size();
	EmitWord( 0 );                 // Bound (placeholder)
	EmitWord( 0 );                 // Schema

	// Capabilities
	{
		uint32_t cap = SpvCapabilityShader;
		EmitOp( SpvOpCapability, &cap, 1 );
	}

	// GLSL.std.450 import (used for transcendental helpers)
	{
		const char* pName = "GLSL.std.450";
		uint32_t strWords = SpvStringWordCount( pName );
		uint32_t wordCount = 2u + strWords;
		EmitWord( wordCount | ( (uint32_t)SpvOpExtInstImport << 16 ) );
		EmitWord( m_IdGLSLstd450 );
		EmitString( pName );
	}

	// Memory model
	{
		uint32_t ops[2] = { SpvAddressingModelLogical, SpvMemoryModelGLSL450 };
		EmitOp( SpvOpMemoryModel, ops, 2 );
	}

	// Entry point
	{
		const char* pName = "main";
		uint32_t strWords = SpvStringWordCount( pName );
		uint32_t wordCount = 3u + strWords + (uint32_t)m_InterfaceVars.size();
		uint32_t model = m_bVertexShader ? SpvExecutionModelVertex : SpvExecutionModelFragment;
		EmitWord( wordCount | ( (uint32_t)SpvOpEntryPoint << 16 ) );
		EmitWord( model );
		EmitWord( m_EntryFuncId );
		EmitString( pName );
		for ( uint32_t id : m_InterfaceVars )
			EmitWord( id );
	}

	// Execution mode
	{
		if ( !m_bVertexShader )
		{
			uint32_t ops[2] = { m_EntryFuncId, SpvExecutionModeOriginUpperLeft };
			EmitOp( SpvOpExecutionMode, ops, 2 );
			if ( m_bPSOutputDepth )
			{
				uint32_t ops2[2] = { m_EntryFuncId, SpvExecutionModeDepthReplacing };
				EmitOp( SpvOpExecutionMode, ops2, 2 );
			}
		}
	}
}


//------------------------------------------------------------------------------
// Pass 2b: types, constants and global variables
//------------------------------------------------------------------------------
void CD3DToVK::EmitTypesAndGlobals()
{
	// --- Annotations (decorations) ---
	// Constant UBO: struct decorated as Block, member 0 Offset 0, array stride 16.
	// We need the struct/array type ids to decorate them, so allocate them now.
	m_IdTypeVoid   = AllocId();
	m_IdTypeBool   = AllocId();
	m_IdTypeBoolVec4 = AllocId();
	m_IdTypeFloat  = AllocId();
	m_IdTypeInt32  = AllocId();
	m_IdTypeVec2   = AllocId();
	m_IdTypeVec3   = AllocId();
	m_IdTypeVec4   = AllocId();

	uint32_t constArrayLenId = 0;   // created after OpTypeInt is emitted below
	m_ConstUboStructId = AllocId();
	uint32_t constArrayTypeId = AllocId();

	// Decorate array stride.
	{
		uint32_t ops[3] = { constArrayTypeId, 6 /*ArrayStride*/, 16 };
		EmitOp( SpvOpDecorate, ops, 3 );
	}
	// Decorate struct as Block.
	{
		uint32_t ops[2] = { m_ConstUboStructId, SpvDecorationBlock };
		EmitOp( SpvOpDecorate, ops, 2 );
	}
	// Member 0 Offset 0.
	{
		uint32_t ops[4] = { m_ConstUboStructId, 0, 35 /*Offset*/, 0 };
		EmitOp( SpvOpMemberDecorate, ops, 4 );
	}
	// UBO binding/descriptor set.
	{
		uint32_t opsDS[3] = { m_ConstUboVarId, SpvDecorationDescriptorSet, 0 };
		EmitOp( SpvOpDecorate, opsDS, 3 );
		uint32_t opsB[3]  = { m_ConstUboVarId, SpvDecorationBinding, 0 };
		EmitOp( SpvOpDecorate, opsB, 3 );
	}

	// Interface variable locations / builtins.
	if ( m_bVertexShader )
	{
		for ( uint32_t i = 0; i < MAX_SPV_VS_INPUTS; ++i )
			if ( m_VSInputIds[i] != 0 )
			{
				uint32_t ops[3] = { m_VSInputIds[i], SpvDecorationLocation, i };
				EmitOp( SpvOpDecorate, ops, 3 );
			}
		if ( m_VSOutputPosId != 0 )
		{
			uint32_t ops[3] = { m_VSOutputPosId, SpvDecorationBuiltIn, SpvBuiltInPosition };
			EmitOp( SpvOpDecorate, ops, 3 );
		}
		uint32_t outLoc = 0;
		for ( uint32_t i = 0; i < 2; ++i )
			if ( m_VSOutputColorIds[i] != 0 )
			{
				uint32_t ops[3] = { m_VSOutputColorIds[i], SpvDecorationLocation, outLoc++ };
				EmitOp( SpvOpDecorate, ops, 3 );
			}
		for ( uint32_t i = 0; i < MAX_SPV_VS_TEXCOORD_OUT; ++i )
			if ( m_VSOutputTexCoordIds[i] != 0 )
			{
				uint32_t ops[3] = { m_VSOutputTexCoordIds[i], SpvDecorationLocation, outLoc++ };
				EmitOp( SpvOpDecorate, ops, 3 );
			}
	}
	else
	{
		for ( uint32_t i = 0; i < MAX_SPV_PS_INPUTS; ++i )
			if ( m_PSInputIds[i] != 0 )
			{
				uint32_t ops[3] = { m_PSInputIds[i], SpvDecorationLocation, i };
				EmitOp( SpvOpDecorate, ops, 3 );
			}
		for ( uint32_t i = 0; i < MAX_SPV_PS_COLOR_OUT; ++i )
			if ( m_PSOutputColorIds[i] != 0 )
			{
				uint32_t ops[3] = { m_PSOutputColorIds[i], SpvDecorationLocation, i };
				EmitOp( SpvOpDecorate, ops, 3 );
			}
		if ( m_PSOutputDepthId != 0 )
		{
			uint32_t ops[3] = { m_PSOutputDepthId, SpvDecorationBuiltIn, SpvBuiltInFragDepth };
			EmitOp( SpvOpDecorate, ops, 3 );
		}
	}

	// Sampler bindings (descriptor set 0, binding = sampler index).
	for ( uint32_t i = 0; i < MAX_SPV_SAMPLERS; ++i )
		if ( m_SamplerVarIds[i] != 0 )
		{
			uint32_t opsDS[3] = { m_SamplerVarIds[i], SpvDecorationDescriptorSet, 0 };
			EmitOp( SpvOpDecorate, opsDS, 3 );
			uint32_t opsB[3]  = { m_SamplerVarIds[i], SpvDecorationBinding, i };
			EmitOp( SpvOpDecorate, opsB, 3 );
		}

	// --- Type declarations ---
	{
		uint32_t ops[1] = { m_IdTypeVoid };
		EmitOp( SpvOpTypeVoid, ops, 1 );
	}
	{
		uint32_t ops[1] = { m_IdTypeBool };
		EmitOp( SpvOpTypeBool, ops, 1 );
	}
	{
		uint32_t ops[3] = { m_IdTypeBoolVec4, m_IdTypeBool, 4 };
		EmitOp( SpvOpTypeVector, ops, 3 );
	}
	{
		uint32_t ops[2] = { m_IdTypeFloat, 32 };
		EmitOp( SpvOpTypeFloat, ops, 2 );
	}
	{
		uint32_t ops[3] = { m_IdTypeInt32, 32, 1 };
		EmitOp( SpvOpTypeInt, ops, 3 );
	}

	// Array length constant must come after OpTypeInt but before OpTypeArray.
	constArrayLenId = GetConstInt32( SPV_DEFAULT_CONST_COUNT );
	{
		uint32_t ops[3] = { m_IdTypeVec2, m_IdTypeFloat, 2 };
		EmitOp( SpvOpTypeVector, ops, 3 );
	}
	{
		uint32_t ops[3] = { m_IdTypeVec3, m_IdTypeFloat, 3 };
		EmitOp( SpvOpTypeVector, ops, 3 );
	}
	{
		uint32_t ops[3] = { m_IdTypeVec4, m_IdTypeFloat, 4 };
		EmitOp( SpvOpTypeVector, ops, 3 );
	}

	// void function type ()
	m_IdTypeVoidFunc = AllocId();
	{
		uint32_t ops[2] = { m_IdTypeVoidFunc, m_IdTypeVoid };
		EmitOp( SpvOpTypeFunction, ops, 2 );
	}

	// Constant array of vec4 (the uniform constant pool)
	{
		uint32_t ops[3] = { constArrayTypeId, m_IdTypeVec4, constArrayLenId };
		EmitOp( SpvOpTypeArray, ops, 3 );
	}
	// struct { vec4 c[N]; }  (single member, no count operand)
	{
		uint32_t ops[2] = { m_ConstUboStructId, constArrayTypeId };
		EmitOp( SpvOpTypeStruct, ops, 2 );
	}

	// Pointer types
	uint32_t ptrUboStruct = AllocId();
	{
		uint32_t ops[3] = { ptrUboStruct, SpvStorageClassUniform, m_ConstUboStructId };
		EmitOp( SpvOpTypePointer, ops, 3 );
	}
	m_IdPtrInputVec4   = AllocId();
	m_IdPtrOutputVec4  = AllocId();
	m_IdPtrFuncVec4    = AllocId();
	m_IdPtrUniformVec4 = AllocId();
	{
		uint32_t ops[3] = { m_IdPtrInputVec4,   SpvStorageClassInput,   m_IdTypeVec4 };
		EmitOp( SpvOpTypePointer, ops, 3 );
	}
	{
		uint32_t ops[3] = { m_IdPtrOutputVec4,  SpvStorageClassOutput,  m_IdTypeVec4 };
		EmitOp( SpvOpTypePointer, ops, 3 );
	}
	{
		uint32_t ops[3] = { m_IdPtrFuncVec4,    SpvStorageClassFunction,m_IdTypeVec4 };
		EmitOp( SpvOpTypePointer, ops, 3 );
	}
	{
		uint32_t ops[3] = { m_IdPtrUniformVec4, SpvStorageClassUniform, m_IdTypeVec4 };
		EmitOp( SpvOpTypePointer, ops, 3 );
	}

	// --- Constants ---
	m_IdConstFloatZero = GetConstFloat( 0.0f );
	m_IdConstFloatOne  = GetConstFloat( 1.0f );
	{
		uint32_t id = AllocId();
		uint32_t ops[6] = { m_IdTypeVec4, id, m_IdConstFloatZero, m_IdConstFloatZero, m_IdConstFloatZero, m_IdConstFloatZero };
		EmitOp( SpvOpConstantComposite, ops, 6 );
		m_IdConstVec4Zero = id;
	}
	{
		uint32_t id = AllocId();
		uint32_t ops[6] = { m_IdTypeVec4, id, m_IdConstFloatOne, m_IdConstFloatOne, m_IdConstFloatOne, m_IdConstFloatOne };
		EmitOp( SpvOpConstantComposite, ops, 6 );
		m_IdConstVec4One = id;
	}

	// Sampler image types + sampled image types + pointer types.
	for ( uint32_t i = 0; i < MAX_SPV_SAMPLERS; ++i )
	{
		if ( m_SamplerVarIds[i] == 0 )
			continue;

		uint32_t dim = SpvDim2D;
		uint32_t tag = m_dwSamplerTypes[i];
		if ( tag == SPV_SAMPLER_TYPE_CUBE )      dim = SpvDimCube;
		else if ( tag == SPV_SAMPLER_TYPE_3D )   dim = SpvDim3D;

		uint32_t imageType = AllocId();
		m_SamplerImageTypeIds[i] = imageType;
		{
			// OpTypeImage %float Dim Depth=0 Arrayed=0 MS=0 Sampled=1 Format=Unknown
			uint32_t ops[8] = { imageType, m_IdTypeFloat, dim, 0, 0, 0, 1, 0 };
			EmitOp( SpvOpTypeImage, ops, 8 );     // 8 operands => 9 word instruction
		}
		uint32_t sampledType = AllocId();
		m_SamplerSampledTypeIds[i] = sampledType;
		{
			uint32_t ops[2] = { sampledType, imageType };
			EmitOp( SpvOpTypeSampledImage, ops, 2 );
		}
		uint32_t ptrType = AllocId();
		{
			uint32_t ops[3] = { ptrType, SpvStorageClassUniformConstant, sampledType };
			EmitOp( SpvOpTypePointer, ops, 3 );
		}
		// Variable for the sampler.
		{
			uint32_t ops[3] = { ptrType, m_SamplerVarIds[i], SpvStorageClassUniformConstant };
			EmitOp( SpvOpVariable, ops, 3 );
		}
	}

	// --- Global variables ---
	// UBO
	{
		uint32_t ops[3] = { ptrUboStruct, m_ConstUboVarId, SpvStorageClassUniform };
		EmitOp( SpvOpVariable, ops, 3 );
	}
	// VS inputs / outputs
	if ( m_bVertexShader )
	{
		for ( uint32_t i = 0; i < MAX_SPV_VS_INPUTS; ++i )
			if ( m_VSInputIds[i] != 0 )
			{
				uint32_t ops[3] = { m_IdPtrInputVec4, m_VSInputIds[i], SpvStorageClassInput };
				EmitOp( SpvOpVariable, ops, 3 );
			}
		if ( m_VSOutputPosId != 0 )
		{
			uint32_t ops[3] = { m_IdPtrOutputVec4, m_VSOutputPosId, SpvStorageClassOutput };
			EmitOp( SpvOpVariable, ops, 3 );
		}
		for ( uint32_t i = 0; i < 2; ++i )
			if ( m_VSOutputColorIds[i] != 0 )
			{
				uint32_t ops[3] = { m_IdPtrOutputVec4, m_VSOutputColorIds[i], SpvStorageClassOutput };
				EmitOp( SpvOpVariable, ops, 3 );
			}
		for ( uint32_t i = 0; i < MAX_SPV_VS_TEXCOORD_OUT; ++i )
			if ( m_VSOutputTexCoordIds[i] != 0 )
			{
				uint32_t ops[3] = { m_IdPtrOutputVec4, m_VSOutputTexCoordIds[i], SpvStorageClassOutput };
				EmitOp( SpvOpVariable, ops, 3 );
			}
	}
	else
	{
		for ( uint32_t i = 0; i < MAX_SPV_PS_INPUTS; ++i )
			if ( m_PSInputIds[i] != 0 )
			{
				uint32_t ops[3] = { m_IdPtrInputVec4, m_PSInputIds[i], SpvStorageClassInput };
				EmitOp( SpvOpVariable, ops, 3 );
			}
		for ( uint32_t i = 0; i < MAX_SPV_PS_COLOR_OUT; ++i )
			if ( m_PSOutputColorIds[i] != 0 )
			{
				uint32_t ops[3] = { m_IdPtrOutputVec4, m_PSOutputColorIds[i], SpvStorageClassOutput };
				EmitOp( SpvOpVariable, ops, 3 );
			}
		if ( m_PSOutputDepthId != 0 )
		{
			uint32_t ops[3] = { m_IdPtrOutputVec4, m_PSOutputDepthId, SpvStorageClassOutput };
			EmitOp( SpvOpVariable, ops, 3 );
		}
	}
}


//------------------------------------------------------------------------------
// Pass 2c: function body
//------------------------------------------------------------------------------
void CD3DToVK::DeclareTemp( uint32_t nTemp )
{
	if ( nTemp >= MAX_SPV_TEMPS )
		return;
	if ( m_TempVarIds[nTemp] != 0 )
		return;
	uint32_t id = AllocId();
	m_TempVarIds[nTemp] = id;
	uint32_t ops[3] = { m_IdPtrFuncVec4, id, SpvStorageClassFunction };
	EmitOp( SpvOpVariable, ops, 3 );
}

void CD3DToVK::EmitFunction( const uint32_t* pCode, uint32_t nCodeSize )
{
	// OpFunction %void %main None %voidFunc
	{
		uint32_t ops[4] = { m_IdTypeVoid, m_EntryFuncId, 0 /*FunctionControl None*/, m_IdTypeVoidFunc };
		EmitOp( SpvOpFunction, ops, 4 );
	}
	// Entry block label.
	{
		uint32_t ops[1] = { m_EntryLabelId };
		EmitOp( SpvOpLabel, ops, 1 );
	}

	// Declare every temp that the shader references as a function-scope vec4.
	for ( uint32_t i = 0; i < MAX_SPV_TEMPS; ++i )
		if ( m_dwTempUsageMask & ( 1u << i ) )
			DeclareTemp( i );

	// Walk the token stream again, translating each instruction.
	m_pdwBaseToken = pCode;
	m_pdwNextToken = pCode + 1;   // skip version token
	m_pdwEndToken  = ( nCodeSize > 0 ) ? ( pCode + nCodeSize ) : nullptr;

	while ( !AtEnd() )
	{
		uint32_t dwToken = GetNextToken();
		if ( dwToken == SPV_D3DPS_END() )
			break;
		TranslateInstruction( dwToken );
	}

	// OpReturn + OpFunctionEnd
	{
		EmitOp( SpvOpReturn, nullptr, 0 );
		EmitOp( SpvOpFunctionEnd, nullptr, 0 );
	}
}

void CD3DToVK::FinalizeHeader()
{
	// Patch the Bound word (max id + 1).
	if ( m_BoundIndexInHeader < m_SPIRV.size() )
		m_SPIRV[m_BoundIndexInHeader] = m_NextId;
}


//------------------------------------------------------------------------------
// Register resolution
//------------------------------------------------------------------------------
uint32_t CD3DToVK::ApplySwizzle( uint32_t swizzle, uint32_t vec4Id )
{
	// Identity swizzle is 0xE4 (x,y,z,w).
	if ( swizzle == 0xE4 || vec4Id == 0 )
		return vec4Id;

	uint32_t comps[4] = {
		swizzle & 0x3,
		( swizzle >> 2 ) & 0x3,
		( swizzle >> 4 ) & 0x3,
		( swizzle >> 6 ) & 0x3
	};

	uint32_t id = AllocId();
	EmitVectorShuffle( m_IdTypeVec4, id, vec4Id, vec4Id, comps, 4 );
	return id;
}

uint32_t CD3DToVK::ApplySrcModifier( uint32_t mod, uint32_t vec4Id )
{
	if ( vec4Id == 0 )
		return vec4Id;
	if ( mod == 1 )     // D3DSPSM_NEG
	{
		uint32_t id = AllocId();
		uint32_t ops[3] = { m_IdTypeVec4, id, vec4Id };
		EmitOp( SpvOpFNegate, ops, 3 );
		return id;
	}
	// Other source modifiers (bias, sign, x2, complement) not handled here.
	return vec4Id;
}

uint32_t CD3DToVK::BroadcastScalar( uint32_t scalarId )
{
	uint32_t id = AllocId();
	uint32_t ops[6] = { m_IdTypeVec4, id, scalarId, scalarId, scalarId, scalarId };
	EmitOp( SpvOpCompositeConstruct, ops, 6 );
	return id;
}

uint32_t CD3DToVK::LoadSrc( uint32_t dwRegToken )
{
	uint32_t regType = GetRegType( dwRegToken );
	uint32_t regNum  = GetRegNum( dwRegToken );
	uint32_t swizzle = GetSrcSwizzle( dwRegToken );
	uint32_t mod     = GetSrcModifier( dwRegToken );

	uint32_t ptrId = 0;

	switch ( regType )
	{
	case SPV_D3DSPR_TEMP:
		if ( regNum < MAX_SPV_TEMPS )
		{
			if ( m_TempVarIds[regNum] == 0 )
				DeclareTemp( regNum );   // shouldn't normally happen post-collect
			ptrId = m_TempVarIds[regNum];
		}
		break;
	case SPV_D3DSPR_INPUT:
		if ( m_bVertexShader )
		{
			if ( regNum < MAX_SPV_VS_INPUTS ) ptrId = m_VSInputIds[regNum];
		}
		else
		{
			if ( regNum < MAX_SPV_PS_INPUTS ) ptrId = m_PSInputIds[regNum];
		}
		break;
	case SPV_D3DSPR_CONST:
		{
			// AccessChain into UBO: indices [0 (member), regNum (array idx)]
			uint32_t idx0 = GetConstInt32( 0 );
			uint32_t idx1 = GetConstInt32( (int32_t)regNum );
			uint32_t id = AllocId();
			uint32_t ops[5] = { m_IdPtrUniformVec4, id, m_ConstUboVarId, idx0, idx1 };
			EmitOp( SpvOpAccessChain, ops, 5 );
			ptrId = id;
		}
		break;
	case SPV_D3DSPR_RASTOUT:
		if ( m_bVertexShader && regNum == SPV_D3DSRO_POSITION && m_VSOutputPosId != 0 )
			ptrId = m_VSOutputPosId;
		break;
	case SPV_D3DSPR_ATTROUT:
		if ( m_bVertexShader && regNum < 2 ) ptrId = m_VSOutputColorIds[regNum];
		break;
	case SPV_D3DSPR_TEXCRDOUT:   // SPV_D3DSPR_OUTPUT is the same value (6)
		if ( m_bVertexShader && regNum < MAX_SPV_VS_TEXCOORD_OUT ) ptrId = m_VSOutputTexCoordIds[regNum];
		break;
	case SPV_D3DSPR_COLOROUT:
		if ( !m_bVertexShader && regNum < MAX_SPV_PS_COLOR_OUT ) ptrId = m_PSOutputColorIds[regNum];
		break;
	default:
		break;
	}

	if ( ptrId == 0 )
	{
		// Unknown / unsupported source: feed zeros.
		return m_IdConstVec4Zero;
	}

	uint32_t loaded = AllocId();
	uint32_t ops[3] = { m_IdTypeVec4, loaded, ptrId };
	EmitOp( SpvOpLoad, ops, 3 );

	loaded = ApplySwizzle( swizzle, loaded );
	loaded = ApplySrcModifier( mod, loaded );
	return loaded;
}

uint32_t CD3DToVK::GetDstPtr( uint32_t dwRegToken )
{
	uint32_t regType = GetRegType( dwRegToken );
	uint32_t regNum  = GetRegNum( dwRegToken );

	switch ( regType )
	{
	case SPV_D3DSPR_TEMP:
		if ( regNum < MAX_SPV_TEMPS )
		{
			if ( m_TempVarIds[regNum] == 0 )
				DeclareTemp( regNum );
			return m_TempVarIds[regNum];
		}
		break;
	case SPV_D3DSPR_RASTOUT:
		if ( m_bVertexShader && regNum == SPV_D3DSRO_POSITION ) return m_VSOutputPosId;
		break;
	case SPV_D3DSPR_ATTROUT:
		if ( m_bVertexShader && regNum < 2 ) return m_VSOutputColorIds[regNum];
		break;
	case SPV_D3DSPR_TEXCRDOUT:   // SPV_D3DSPR_OUTPUT is the same value (6)
		if ( m_bVertexShader && regNum < MAX_SPV_VS_TEXCOORD_OUT ) return m_VSOutputTexCoordIds[regNum];
		break;
	case SPV_D3DSPR_COLOROUT:
		if ( !m_bVertexShader && regNum < MAX_SPV_PS_COLOR_OUT ) return m_PSOutputColorIds[regNum];
		break;
	case SPV_D3DSPR_DEPTHOUT:
		if ( !m_bVertexShader ) return m_PSOutputDepthId;
		break;
	default:
		break;
	}
	return 0;
}

void CD3DToVK::StoreDst( uint32_t dwRegToken, uint32_t valueId )
{
	uint32_t ptrId = GetDstPtr( dwRegToken );
	if ( ptrId == 0 || valueId == 0 )
		return;

	uint32_t writeMask = GetWriteMask( dwRegToken );
	uint32_t dstMod    = GetDstModifier( dwRegToken );

	uint32_t value = valueId;

	// Saturate destination modifier (_sat): clamp to [0,1].
	if ( dstMod & 1 )     // D3DSPDM_SATURATE
	{
		uint32_t id = AllocId();
		// OpExtInst FClamp(x, min, max): resultType, result, set, instr, x, min, max
		uint32_t ops[7] = { m_IdTypeVec4, id, m_IdGLSLstd450, GLSLstd450FClamp, value, m_IdConstVec4Zero, m_IdConstVec4One };
		EmitOp( SpvOpExtInst, ops, 7 );
		value = id;
	}

	if ( writeMask == 0xF )
	{
		uint32_t ops[2] = { ptrId, value };
		EmitOp( SpvOpStore, ops, 2 );
		return;
	}

	// Partial write: load existing, merge via OpVectorShuffle, store.
	uint32_t cur = AllocId();
	{
		uint32_t ops[3] = { m_IdTypeVec4, cur, ptrId };
		EmitOp( SpvOpLoad, ops, 3 );
	}

	// Build component selection. For each output component i (0..3), if the
	// write mask has bit i set, take from `value` (index i), else from `cur`
	// (index i). OpVectorShuffle indexes into [value, cur] => value comps are
	// 0..3, cur comps are 4..7.
	uint32_t sel[4];
	for ( uint32_t i = 0; i < 4; ++i )
		sel[i] = ( writeMask & ( 1u << i ) ) ? i : ( 4 + i );

	uint32_t merged = AllocId();
	EmitVectorShuffle( m_IdTypeVec4, merged, value, cur, sel, 4 );
	uint32_t ops[2] = { ptrId, merged };
	EmitOp( SpvOpStore, ops, 2 );
}


//------------------------------------------------------------------------------
// Opcode handlers
//------------------------------------------------------------------------------
void CD3DToVK::Handle_NOP( uint32_t /*dwOpcodeToken*/ )
{
	EmitOp( SpvOpNop, nullptr, 0 );
}

void CD3DToVK::Handle_Unsupported( uint32_t dwOpcodeToken, const char* pName )
{
	(void)pName;
	// Emit a NOP marker and let the dispatcher skip the rest of the
	// instruction by length.
	EmitOp( SpvOpNop, nullptr, 0 );
	if ( m_bSpew && pName )
		std::printf( "  [dx9asmvtospv] unsupported opcode 0x%x (%s) - skipped\n", Opcode( dwOpcodeToken ), pName );
}

void CD3DToVK::Handle_MOV( uint32_t /*dwOpcodeToken*/ )
{
	uint32_t dst = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t v = LoadSrc( src0 );
	StoreDst( dst, v );
}

void CD3DToVK::Handle_Binary( uint32_t /*dwOpcodeToken*/, uint16_t spvOp )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );
	uint32_t id = AllocId();
	uint32_t ops[4] = { m_IdTypeVec4, id, a, b };
	EmitOp( spvOp, ops, 4 );
	StoreDst( dst, id );
}

void CD3DToVK::Handle_MAD( uint32_t /*dwOpcodeToken*/ )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t src2 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );
	uint32_t c = LoadSrc( src2 );
	uint32_t mul = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeVec4, mul, a, b };
		EmitOp( SpvOpFMul, ops, 4 );
	}
	uint32_t add = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeVec4, add, mul, c };
		EmitOp( SpvOpFAdd, ops, 4 );
	}
	StoreDst( dst, add );
}

void CD3DToVK::Handle_Dot( uint32_t /*dwOpcodeToken*/, uint32_t nComps )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );

	uint32_t ax = a, bx = b;
	if ( nComps == 3 )
	{
		uint32_t comps[3] = { 0, 1, 2 };
		uint32_t a3 = AllocId();
		EmitVectorShuffle( m_IdTypeVec3, a3, a, a, comps, 3 );
		uint32_t b3 = AllocId();
		EmitVectorShuffle( m_IdTypeVec3, b3, b, b, comps, 3 );
		ax = a3; bx = b3;
	}

	uint32_t scalar = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeFloat, scalar, ax, bx };
		EmitOp( SpvOpDot, ops, 4 );
	}
	uint32_t broad = BroadcastScalar( scalar );
	StoreDst( dst, broad );
}

void CD3DToVK::Handle_RcpRsq( uint32_t /*dwOpcodeToken*/, bool bRsq )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t v = LoadSrc( src0 );

	// Extract .x
	uint32_t x = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeFloat, x, v, 0 };
		EmitOp( SpvOpCompositeExtract, ops, 4 );
	}

	uint32_t res;
	if ( bRsq )
	{
		res = AllocId();
		uint32_t ops[5] = { m_IdTypeFloat, res, m_IdGLSLstd450, GLSLstd450InverseSqrt, x };
		EmitOp( SpvOpExtInst, ops, 5 );
	}
	else
	{
		res = AllocId();
		uint32_t ops[4] = { m_IdTypeFloat, res, m_IdConstFloatOne, x };
		EmitOp( SpvOpFDiv, ops, 4 );
	}
	StoreDst( dst, BroadcastScalar( res ) );
}

void CD3DToVK::Handle_UnaryExtInst( uint32_t /*dwOpcodeToken*/, uint32_t extOp )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t v = LoadSrc( src0 );

	// Most DX9 unary ops (abs, frc, exp, log) operate per-component on a vec4.
	// GLSLstd450 scalar extended instructions are applied via OpExtInst on the
	// vec4 type where supported (FAbs, Fract, Exp2, Log2, Floor, Sqrt, etc.).
	uint32_t res = AllocId();
	uint32_t ops[5] = { m_IdTypeVec4, res, m_IdGLSLstd450, extOp, v };
	EmitOp( SpvOpExtInst, ops, 5 );
	StoreDst( dst, res );
}

void CD3DToVK::Handle_LRP( uint32_t /*dwOpcodeToken*/ )
{
	// dst = mix(src2, src1, src0) = src0*src1 + (1-src0)*src2
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t src2 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );
	uint32_t c = LoadSrc( src2 );

	uint32_t res = AllocId();
	// OpExtInst FClamp-style: resultType, result, set, instr, x(=src2), y(=src1), a(=src0)
	uint32_t ops[7] = { m_IdTypeVec4, res, m_IdGLSLstd450, GLSLstd450FMix, c, b, a };
	EmitOp( SpvOpExtInst, ops, 7 );
	StoreDst( dst, res );
}

void CD3DToVK::Handle_POW( uint32_t /*dwOpcodeToken*/ )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );

	uint32_t ax = AllocId(), bx = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeFloat, ax, a, 0 };
		EmitOp( SpvOpCompositeExtract, ops, 4 );
	}
	{
		uint32_t ops[4] = { m_IdTypeFloat, bx, b, 0 };
		EmitOp( SpvOpCompositeExtract, ops, 4 );
	}
	uint32_t res = AllocId();
	{
		uint32_t ops[6] = { m_IdTypeFloat, res, m_IdGLSLstd450, GLSLstd450Pow, ax, bx };
		EmitOp( SpvOpExtInst, ops, 6 );
	}
	StoreDst( dst, BroadcastScalar( res ) );
}

void CD3DToVK::Handle_CRS( uint32_t /*dwOpcodeToken*/ )
{
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );

	// vec3 parts
	uint32_t comps[3] = { 0, 1, 2 };
	uint32_t a3 = AllocId();
	EmitVectorShuffle( m_IdTypeVec3, a3, a, a, comps, 3 );
	uint32_t b3 = AllocId();
	EmitVectorShuffle( m_IdTypeVec3, b3, b, b, comps, 3 );

	uint32_t cross = AllocId();
	{
		// OpExtInst Cross(x, y): resultType, result, set, instr, x, y
		uint32_t ops[6] = { m_IdTypeVec3, cross, m_IdGLSLstd450, GLSLstd450Cross, a3, b3 };
		EmitOp( SpvOpExtInst, ops, 6 );
	}
	// Promote to vec4 with w = 0.
	uint32_t v4 = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeVec4, v4, cross, m_IdConstFloatZero };
		EmitOp( SpvOpCompositeConstruct, ops, 4 );
	}
	StoreDst( dst, v4 );
}

void CD3DToVK::Handle_CMP( uint32_t /*dwOpcodeToken*/ )
{
	// dst = (src0 >= 0) ? src1 : src2
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t src2 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );
	uint32_t c = LoadSrc( src2 );

	uint32_t cond = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeBoolVec4, cond, a, m_IdConstVec4Zero };
		EmitOp( SpvOpFOrdGreaterThanEqual, ops, 4 );
	}
	uint32_t sel = AllocId();
	{
		uint32_t ops[5] = { m_IdTypeVec4, sel, cond, b, c };
		EmitOp( SpvOpSelect, ops, 5 );
	}
	StoreDst( dst, sel );
}

void CD3DToVK::Handle_SltSge( uint32_t /*dwOpcodeToken*/, uint16_t spvOp )
{
	// dst = (src0 < src1) ? 1 : 0   (slt)  or  >= (sge)
	uint32_t dst  = GetNextToken();
	uint32_t src0 = GetNextToken();
	uint32_t src1 = GetNextToken();
	uint32_t a = LoadSrc( src0 );
	uint32_t b = LoadSrc( src1 );

	uint32_t cond = AllocId();
	{
		uint32_t ops[4] = { m_IdTypeBoolVec4, cond, a, b };
		EmitOp( spvOp, ops, 4 );
	}
	uint32_t sel = AllocId();
	{
		uint32_t ops[5] = { m_IdTypeVec4, sel, cond, m_IdConstVec4One, m_IdConstVec4Zero };
		EmitOp( SpvOpSelect, ops, 5 );
	}
	StoreDst( dst, sel );
}

void CD3DToVK::Handle_TEXKILL( uint32_t /*dwOpcodeToken*/ )
{
	// texkill would discard the fragment if any of src.xyz < 0. Implementing
	// that requires control flow; as a first-cut we emit a NOP and consume the
	// operand token so the stream stays aligned. (Documented limitation.)
	uint32_t src0 = GetNextToken();
	(void)src0;
	EmitOp( SpvOpNop, nullptr, 0 );
}

void CD3DToVK::Handle_TEX( uint32_t /*dwOpcodeToken*/, bool bLod )
{
	// texld dst, srcCoord, sampler
	uint32_t dst     = GetNextToken();
	uint32_t srcCoord = GetNextToken();
	uint32_t srcSamp = GetNextToken();

	uint32_t samplerNum = GetRegNum( srcSamp );
	if ( samplerNum >= MAX_SPV_SAMPLERS || m_SamplerVarIds[samplerNum] == 0 )
	{
		StoreDst( dst, m_IdConstVec4Zero );
		return;
	}

	uint32_t coord4 = LoadSrc( srcCoord );
	uint32_t samplerTag = m_dwSamplerTypes[samplerNum];

	// Build the coordinate vector of the right dimensionality.
	uint32_t coord = coord4;
	uint32_t coordType = m_IdTypeVec4;
	if ( samplerTag == SPV_SAMPLER_TYPE_2D )
	{
		coordType = m_IdTypeVec2;
		uint32_t comps[2] = { 0, 1 };
		coord = AllocId();
		EmitVectorShuffle( m_IdTypeVec2, coord, coord4, coord4, comps, 2 );
	}
	else // 3D or Cube -> vec3
	{
		coordType = m_IdTypeVec3;
		uint32_t comps[3] = { 0, 1, 2 };
		coord = AllocId();
		EmitVectorShuffle( m_IdTypeVec3, coord, coord4, coord4, comps, 3 );
	}

	// Load the combined image+sampler.
	uint32_t sampledImg = AllocId();
	{
		uint32_t ops[3] = { m_SamplerSampledTypeIds[samplerNum], sampledImg, m_SamplerVarIds[samplerNum] };
		EmitOp( SpvOpLoad, ops, 3 );
	}

	uint32_t result = AllocId();
	if ( !bLod )
	{
		uint32_t ops[4] = { m_IdTypeVec4, result, sampledImg, coord };
		EmitOp( SpvOpImageSampleImplicitLod, ops, 4 );
	}
	else
	{
		// texldl: srcCoord.w carries the explicit LOD.
		uint32_t lod = AllocId();
		{
			uint32_t ops[4] = { m_IdTypeFloat, lod, coord4, 3 };
			EmitOp( SpvOpCompositeExtract, ops, 4 );
		}
		// OpImageSampleExplicitLod %vec4 %result %img %coord Lod %lod
		EmitWord( (1u + 4u + 2u) | ( (uint32_t)SpvOpImageSampleExplicitLod << 16 ) );
		EmitWord( m_IdTypeVec4 );
		EmitWord( result );
		EmitWord( sampledImg );
		EmitWord( coord );
		EmitWord( SpvImageOperandsLod );
		EmitWord( lod );
	}
	(void)coordType;
	StoreDst( dst, result );
}

void CD3DToVK::Handle_DCL( uint32_t /*dwOpcodeToken*/ )
{
	// Declarations are handled by the CollectUsage pass; here we just consume
	// the operand tokens so the stream stays aligned. The instruction length
	// tells us how many tokens to skip.
	uint32_t len = OpcodeLength( *( m_pdwNextToken - 1 ) );
	// We already consumed the opcode token; skip the rest.
	if ( len > 1 )
		SkipTokens( len - 1 );
}

void CD3DToVK::Handle_DEF( uint32_t /*dwOpcodeToken*/ )
{
	// def cN, x, y, z, w  -- the constant is provided at runtime through the
	// uniform block, so just consume the dst token + 4 float words.
	GetNextToken();   // dst
	SkipTokens( 4 );  // four float words
}


//------------------------------------------------------------------------------
// Instruction dispatch
//
// For each opcode we either fully consume its operand tokens (handled cases)
// or, for unknown/unimplemented opcodes, skip the entire instruction by its
// length field so the token stream stays aligned.
//------------------------------------------------------------------------------
void CD3DToVK::TranslateInstruction( uint32_t dwOpcodeToken )
{
	uint32_t op  = Opcode( dwOpcodeToken );
	uint32_t len = OpcodeLength( dwOpcodeToken );

	switch ( op )
	{
	case SPV_D3DSIO_NOP:         Handle_NOP( dwOpcodeToken ); return;
	case SPV_D3DSIO_MOV:         Handle_MOV( dwOpcodeToken ); return;
	case SPV_D3DSIO_ADD:         Handle_Binary( dwOpcodeToken, SpvOpFAdd ); return;
	case SPV_D3DSIO_SUB:         Handle_Binary( dwOpcodeToken, SpvOpFSub ); return;
	case SPV_D3DSIO_MUL:         Handle_Binary( dwOpcodeToken, SpvOpFMul ); return;
	case SPV_D3DSIO_MAD:         Handle_MAD( dwOpcodeToken ); return;
	case SPV_D3DSIO_DP3:         Handle_Dot( dwOpcodeToken, 3 ); return;
	case SPV_D3DSIO_DP4:         Handle_Dot( dwOpcodeToken, 4 ); return;
	case SPV_D3DSIO_RCP:         Handle_RcpRsq( dwOpcodeToken, false ); return;
	case SPV_D3DSIO_RSQ:         Handle_RcpRsq( dwOpcodeToken, true ); return;
	case SPV_D3DSIO_MIN:         Handle_Binary( dwOpcodeToken, SpvOpFMin ); return;
	case SPV_D3DSIO_MAX:         Handle_Binary( dwOpcodeToken, SpvOpFMax ); return;
	case SPV_D3DSIO_ABS:         Handle_UnaryExtInst( dwOpcodeToken, GLSLstd450FAbs ); return;
	case SPV_D3DSIO_FRC:         Handle_UnaryExtInst( dwOpcodeToken, GLSLstd450Fract ); return;
	case SPV_D3DSIO_EXP:         Handle_UnaryExtInst( dwOpcodeToken, GLSLstd450Exp2 ); return;
	case SPV_D3DSIO_LOG:         Handle_UnaryExtInst( dwOpcodeToken, GLSLstd450Log2 ); return;
	case SPV_D3DSIO_LRP:         Handle_LRP( dwOpcodeToken ); return;
	case SPV_D3DSIO_POW:         Handle_POW( dwOpcodeToken ); return;
	case SPV_D3DSIO_CRS:         Handle_CRS( dwOpcodeToken ); return;
	case SPV_D3DSIO_CMP:         Handle_CMP( dwOpcodeToken ); return;
	case SPV_D3DSIO_SLT:         Handle_SltSge( dwOpcodeToken, SpvOpFOrdLessThan ); return;
	case SPV_D3DSIO_SGE:         Handle_SltSge( dwOpcodeToken, SpvOpFOrdGreaterThanEqual ); return;
	case SPV_D3DSIO_TEX:         Handle_TEX( dwOpcodeToken, false ); return;
	case SPV_D3DSIO_TEXLDL:      Handle_TEX( dwOpcodeToken, true ); return;
	case SPV_D3DSIO_TEXKILL:     Handle_TEXKILL( dwOpcodeToken ); return;
	case SPV_D3DSIO_DCL:         Handle_DCL( dwOpcodeToken ); return;
	case SPV_D3DSIO_DEF:         Handle_DEF( dwOpcodeToken ); return;
	case SPV_D3DSIO_DEFB:        SkipTokens( len ? ( len - 1 ) : 0 ); return;  // defb dst, bool
	case SPV_D3DSIO_DEFI:        SkipTokens( len ? ( len - 1 ) : 0 ); return;  // defi dst, i,x,y,z
	default:
		break;
	}

	// Everything else (control flow, matrix multiplies, sine/cosine, tex*,
	// m4x4, etc.) is skipped as a no-op for this first implementation.
	Handle_Unsupported( dwOpcodeToken, "unimplemented" );
	if ( len > 1 )
		SkipTokens( len - 1 );
}
