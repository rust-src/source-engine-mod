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
// DX9AsmToSPV.h
//
// Translator that consumes DX9 shader bytecode (the uint32 token stream that
// D3DX produces) and emits a SPIR-V binary suitable for consumption by Vulkan.
//
// The class is modelled after D3DToGL in dx9asmtogl2.h but writes a SPIR-V
// binary (a uint32_t array) instead of GLSL source.
//------------------------------------------------------------------------------
#ifndef DX9_ASM_TO_SPV_H
#define DX9_ASM_TO_SPV_H

#include <cstdint>
#include <cstddef>
#include <vector>

//------------------------------------------------------------------------------
// Result codes returned from Translate()
//------------------------------------------------------------------------------
#define DX9ToVK_OK      0
#define DX9ToVK_ERROR   1

//------------------------------------------------------------------------------
// Capacity limits (mirrors the layout of D3DToGL where reasonable)
//------------------------------------------------------------------------------
#define MAX_SPV_CONSTANTS       512
#define MAX_SPV_TEMPS           32
#define MAX_SPV_SAMPLERS        16
#define MAX_SPV_VS_INPUTS       16
#define MAX_SPV_VS_TEXCOORD_OUT 8
#define MAX_SPV_PS_INPUTS       16
#define MAX_SPV_PS_COLOR_OUT    4

// Sampler texture type tags (matches SAMPLER_TYPE_* in dx9asmtogl2)
#define SPV_SAMPLER_TYPE_UNUSED    0
#define SPV_SAMPLER_TYPE_2D        2
#define SPV_SAMPLER_TYPE_3D        3
#define SPV_SAMPLER_TYPE_CUBE      4

//------------------------------------------------------------------------------
// Option bits (subset of the D3DToGL option bits that still make sense when
// generating SPIR-V for Vulkan)
//------------------------------------------------------------------------------
#define DX9ToVK_OptionDoFixupZ              0x0002
#define DX9ToVK_OptionDoFixupY              0x0004
#define DX9ToVK_OptionDoUserClipPlanes      0x0008
#define DX9ToVK_OptionSRGBWriteSuffix       0x0400
#define DX9ToVK_OptionGenerateBoneUniformBuffer 0x0800
#define DX9ToVK_OptionSpew                  0x80000000

//------------------------------------------------------------------------------
// DX9 shader token helpers (mirrors d3d9types.h so this header is self
// contained and does not require the DX SDK at include time).
//------------------------------------------------------------------------------
#define SPV_D3DSI_OPCODE_MASK           0x0000FFFF
#define SPV_D3DSI_INSTLENGTH_MASK       0x0F000000
#define SPV_D3DSI_INSTLENGTH_SHIFT      24

#define SPV_D3DSP_REGNUM_MASK           0x000007FF
#define SPV_D3DSP_WRITEMASK_0           0x00010000
#define SPV_D3DSP_WRITEMASK_1           0x00020000
#define SPV_D3DSP_WRITEMASK_2           0x00040000
#define SPV_D3DSP_WRITEMASK_3           0x00080000
#define SPV_D3DSP_WRITEMASK_ALL         0x000F0000

#define SPV_D3DSP_REGTYPE_SHIFT         28
#define SPV_D3DSP_REGTYPE_MASK          0x70000000
#define SPV_D3DSP_REGTYPE_SHIFT2        8
#define SPV_D3DSP_REGTYPE_MASK2         0x00001800

#define SPV_D3DSP_DSTMOD_SHIFT          20
#define SPV_D3DSP_DSTMOD_MASK           0x00F00000
#define SPV_D3DSPDM_SATURATE            (1<<SPV_D3DSP_DSTMOD_SHIFT)

#define SPV_D3DSP_SWIZZLE_SHIFT         16
#define SPV_D3DSP_SWIZZLE_MASK          0x00FF0000

#define SPV_D3DSP_SRCMOD_SHIFT          24
#define SPV_D3DSP_SRCMOD_MASK           0x0F000000
#define SPV_D3DSPSM_NONE                (0<<SPV_D3DSP_SRCMOD_SHIFT)
#define SPV_D3DSPSM_NEG                 (1<<SPV_D3DSP_SRCMOD_SHIFT)

// DX9 register types (D3DSHADER_PARAM_REGISTER_TYPE)
#define SPV_D3DSPR_TEMP         0
#define SPV_D3DSPR_INPUT        1
#define SPV_D3DSPR_CONST        2
#define SPV_D3DSPR_ADDR         3
#define SPV_D3DSPR_TEXTURE      3
#define SPV_D3DSPR_RASTOUT      4
#define SPV_D3DSPR_ATTROUT      5
#define SPV_D3DSPR_TEXCRDOUT    6
#define SPV_D3DSPR_OUTPUT       6
#define SPV_D3DSPR_CONSTINT     7
#define SPV_D3DSPR_COLOROUT     8
#define SPV_D3DSPR_DEPTHOUT     9
#define SPV_D3DSPR_SAMPLER      10

// RASTOUT sub-register ids (D3DSHADER_PARAM_RASTOUT_OFFSETS)
#define SPV_D3DSRO_POSITION     0
#define SPV_D3DSRO_FOG          1
#define SPV_D3DSRO_POINT_SIZE   2

// Version token decode (D3DPS_VERSION / D3DVS_VERSION)
#define SPV_D3DPS_VERSION(_Major,_Minor) (0xFFFF0000|((_Major)<<8)|(_Minor))
#define SPV_D3DVS_VERSION(_Major,_Minor) (0xFFFE0000|((_Major)<<8)|(_Minor))
#define SPV_D3DSHADER_VERSION_MAJOR(_t)  ((((_t)>>8)&0xFF))
#define SPV_D3DSHADER_VERSION_MINOR(_t)  (((_t)&0xFF))
#define SPV_D3DPS_END()                  (0x0000FFFF)
#define SPV_D3DVS_END()                  (0x0000FFFF)

// DX9 instruction opcodes (D3DSHADER_INSTRUCTION_OPCODE_TYPE). Only the
// opcodes handled by this translator are listed; the full numeric values
// come from d3d9types.h so unknown opcodes can still be skipped by length.
#define SPV_D3DSIO_NOP          0
#define SPV_D3DSIO_MOV          1
#define SPV_D3DSIO_ADD          2
#define SPV_D3DSIO_SUB          3
#define SPV_D3DSIO_MAD          4
#define SPV_D3DSIO_MUL          5
#define SPV_D3DSIO_RCP          6
#define SPV_D3DSIO_RSQ          7
#define SPV_D3DSIO_DP3          8
#define SPV_D3DSIO_DP4          9
#define SPV_D3DSIO_MIN          10
#define SPV_D3DSIO_MAX          11
#define SPV_D3DSIO_SLT          12
#define SPV_D3DSIO_SGE          13
#define SPV_D3DSIO_EXP          14
#define SPV_D3DSIO_LOG          15
#define SPV_D3DSIO_LIT          16
#define SPV_D3DSIO_DST          17
#define SPV_D3DSIO_LRP          18
#define SPV_D3DSIO_FRC          19
#define SPV_D3DSIO_POW          32
#define SPV_D3DSIO_CRS          33
#define SPV_D3DSIO_SGN          34
#define SPV_D3DSIO_ABS          35
#define SPV_D3DSIO_NRM          36
#define SPV_D3DSIO_SINCOS       37
#define SPV_D3DSIO_DCL          31
#define SPV_D3DSIO_DEFB         47
#define SPV_D3DSIO_DEFI         48
#define SPV_D3DSIO_TEXCOORD     64
#define SPV_D3DSIO_TEXKILL      65
#define SPV_D3DSIO_TEX          66
#define SPV_D3DSIO_DEF          80
#define SPV_D3DSIO_TEXLDD       91
#define SPV_D3DSIO_TEXLDL       93
#define SPV_D3DSIO_CMP          88
#define SPV_D3DSIO_DP2ADD       89

//------------------------------------------------------------------------------
// SPIR-V opcode / enum constants.
//
// A full spirv_headers is not available in this module, so the subset of
// constants required by the translator is defined here. Values match the
// unified SPIR-V specification.
//------------------------------------------------------------------------------
#define SPV_MAGIC_NUMBER        0x07230203u
#define SPV_VERSION_1_0         0x00010000u
#define SPV_GENERATOR_ID        0u      // 0 = reserved for vendors

// Capabilities
#define SpvCapabilityShader     1
#define SpvCapabilityFloat64    10

// Addressing / memory models
#define SpvAddressingModelLogical    0
#define SpvMemoryModelGLSL450        1

// Execution models
#define SpvExecutionModelVertex      0
#define SpvExecutionModelFragment    4

// Execution modes
#define SpvExecutionModeOriginUpperLeft  7
#define SpvExecutionModeDepthReplacing   12

// Storage classes
#define SpvStorageClassUniformConstant   0
#define SpvStorageClassInput             1
#define SpvStorageClassUniform           2
#define SpvStorageClassOutput            3
#define SpvStorageClassFunction          7

// Decorations
#define SpvDecorationBlock            2
#define SpvDecorationBufferBlock      3
#define SpvDecorationRowMajor         4
#define SpvDecorationBuiltIn          11
#define SpvDecorationNoPerspective    13
#define SpvDecorationFlat             14
#define SpvDecorationCentroid         16
#define SpvDecorationLocation         30
#define SpvDecorationBinding          33
#define SpvDecorationDescriptorSet    34

// Builtins
#define SpvBuiltInPosition        0
#define SpvBuiltInPointSize       1
#define SpvBuiltInFragCoord       15
#define SpvBuiltInFragDepth       22

// Dim (used by OpTypeImage)
#define SpvDim1D     0
#define SpvDim2D     1
#define SpvDim3D     2
#define SpvDimCube   3

// Image operands
#define SpvImageOperandsMask       0x0001
#define SpvImageOperandsLod        0x0002

// SPIR-V opcodes (subset)
#define SpvOpNop                   0
#define SpvOpSource                3
#define SpvOpName                  5
#define SpvOpExtInstImport         11
#define SpvOpExtInst               12
#define SpvOpMemoryModel           14
#define SpvOpEntryPoint            15
#define SpvOpExecutionMode         16
#define SpvOpCapability            17
#define SpvOpTypeVoid              19
#define SpvOpTypeBool              20
#define SpvOpTypeInt               21
#define SpvOpTypeFloat             22
#define SpvOpTypeVector            23
#define SpvOpTypeMatrix            24
#define SpvOpTypeImage             25
#define SpvOpTypeSampledImage      27
#define SpvOpTypeArray             28
#define SpvOpTypeStruct            30
#define SpvOpTypePointer           32
#define SpvOpTypeFunction          33
#define SpvOpConstant              43
#define SpvOpConstantComposite     44
#define SpvOpFunction              54
#define SpvOpFunctionParameter     55
#define SpvOpFunctionEnd           56
#define SpvOpVariable              59
#define SpvOpLoad                  61
#define SpvOpStore                 62
#define SpvOpCopyObject            83
#define SpvOpAccessChain           65
#define SpvOpDecorate              71
#define SpvOpMemberDecorate        72
#define SpvOpVectorShuffle         79
#define SpvOpCompositeConstruct    80
#define SpvOpCompositeExtract      81
#define SpvOpFNegate               127
#define SpvOpFAdd                  129
#define SpvOpFSub                  131
#define SpvOpFMul                  133
#define SpvOpFDiv                  136
#define SpvOpDot                   148
#define SpvOpFOrdLessThan          184
#define SpvOpFOrdGreaterThanEqual  190
#define SpvOpSelect                169
#define SpvOpImageSampleImplicitLod 87
#define SpvOpImageSampleExplicitLod 88
#define SpvOpKill                  252
#define SpvOpReturn                253
#define SpvOpLabel                 248
#define SpvOpBranch                249

// GLSL.std.450 extended instruction enumeration (subset)
#define GLSLstd450FAbs           4
#define GLSLstd450FSign          6
#define GLSLstd450Floor          8
#define GLSLstd450Fract          10
#define GLSLstd450Sin            13
#define GLSLstd450Cos            14
#define GLSLstd450Exp            27
#define GLSLstd450Log            28
#define GLSLstd450Exp2           29
#define GLSLstd450Log2           30
#define GLSLstd450Sqrt           31
#define GLSLstd450InverseSqrt    32
#define GLSLstd450FMin           37
#define GLSLstd450FMax           42
#define GLSLstd450FClamp         43
#define GLSLstd450Cross          54
#define GLSLstd450SmoothStep     56


//------------------------------------------------------------------------------
// CD3DToVK
//
// Translates a DX9 shader token stream into a SPIR-V binary.
//------------------------------------------------------------------------------
class CD3DToVK
{
public:
    CD3DToVK();
    ~CD3DToVK();

    //--------------------------------------------------------------------------
    // Primary entry point.
    //
    // pCode           : DX9 bytecode token stream (as returned by D3DX).
    // nCodeSize       : Number of uint32 tokens in pCode (may be 0 if the
    //                   stream is terminated by an END token, in which case a
    //                   reasonable upper bound is walked until END is hit).
    // pbVertexShader  : Optional out-param set to true for a vertex shader.
    // options         : Bitmask of DX9ToVK_Option* flags.
    //
    // Returns DX9ToVK_OK on success. On success the SPIR-V binary is available
    // via GetSPIRV() / GetSPIRVWordCount().
    //--------------------------------------------------------------------------
    int Translate( const uint32_t* pCode, uint32_t nCodeSize, bool* pbVertexShader, uint32_t options );

    //--------------------------------------------------------------------------
    // Access to the produced SPIR-V binary.
    //--------------------------------------------------------------------------
    const uint32_t*       GetSPIRV() const;
    size_t                GetSPIRVWordCount() const;
    const std::vector<uint32_t>& GetSPIRVVector() const;
    void                  ClearSPIRV();

    //--------------------------------------------------------------------------
    // Access to tracked translation state (mirrors the masks D3DToGL exposes).
    //--------------------------------------------------------------------------
    bool    IsVertexShader() const        { return m_bVertexShader; }
    uint32_t GetMajorVersion() const      { return m_dwMajorVersion; }
    uint32_t GetMinorVersion() const      { return m_dwMinorVersion; }

    uint32_t GetSamplerUsageMask() const  { return m_dwSamplerUsageMask; }
    uint32_t GetSamplerType( uint32_t nSampler ) const;
    uint32_t GetConstUsageMask() const    { return m_dwConstUsageMask; }
    uint32_t GetConstIntUsageMask() const { return m_dwConstIntUsageMask; }
    uint32_t GetConstBoolUsageMask() const{ return m_dwConstBoolUsageMask; }
    uint32_t GetTempUsageMask() const     { return m_dwTempUsageMask; }
    uint32_t GetVertexAttributeMask() const { return m_dwVertexAttribMask; }
    uint32_t GetAttribMap( uint32_t nIndex ) const { return m_dwAttribMap[nIndex & 15]; }

private:
    //--------------------------------------------------------------------------
    // DX9 token stream management
    //--------------------------------------------------------------------------
    const uint32_t* m_pdwBaseToken;
    const uint32_t* m_pdwNextToken;
    const uint32_t* m_pdwEndToken;

    uint32_t GetNextToken();
    void     SkipTokens( uint32_t numToSkip );
    bool     AtEnd() const;

    static uint32_t Opcode( uint32_t dwToken );
    static uint32_t OpcodeLength( uint32_t dwToken );
    static uint32_t GetRegType( uint32_t dwRegToken );
    static uint32_t GetRegNum( uint32_t dwRegToken );
    static uint32_t GetWriteMask( uint32_t dwRegToken );    // 4-bit xyzw
    static uint32_t GetSrcSwizzle( uint32_t dwRegToken );   // 8-bit, 2 bits per comp
    static uint32_t GetSrcModifier( uint32_t dwRegToken );
    static uint32_t GetDstModifier( uint32_t dwRegToken );
    static uint32_t TextureType( uint32_t dwToken );

    //--------------------------------------------------------------------------
    // Shader identity
    //--------------------------------------------------------------------------
    bool    m_bVertexShader;
    uint32_t m_dwMajorVersion;
    uint32_t m_dwMinorVersion;
    uint32_t m_dwOptions;
    bool    m_bSpew;
    bool    m_bDoFixupZ;
    bool    m_bDoFixupY;
    bool    m_bDoUserClipPlanes;
    bool    m_bGenerateSRGBWriteSuffix;
    bool    m_bGenerateBoneUniformBuffer;

    //--------------------------------------------------------------------------
    // SPIR-V emit state
    //--------------------------------------------------------------------------
    std::vector<uint32_t> m_SPIRV;
    uint32_t m_NextId;
    uint32_t m_BoundIndexInHeader;   // byte/word offset of the Bound word

    uint32_t AllocId();
    void     EmitWord( uint32_t word );
    void     EmitOp( uint16_t opcode, const uint32_t* pOperands, uint32_t operandCount );
    void     EmitOp( uint16_t opcode, const std::vector<uint32_t>& operands );
    void     EmitString( const char* pStr );   // emits NUL-padded words

    // Emit OpVectorShuffle with the given component indices. The result vector
    // width is implied by nComponents and must match resultType.
    void     EmitVectorShuffle( uint32_t resultType, uint32_t result,
                                uint32_t v1, uint32_t v2,
                                const uint32_t* pComponents, uint32_t nComponents );

    //--------------------------------------------------------------------------
    // Cached SPIR-V type / constant ids
    //--------------------------------------------------------------------------
    uint32_t m_IdTypeVoid;
    uint32_t m_IdTypeBool;
    uint32_t m_IdTypeBoolVec4;
    uint32_t m_IdTypeFloat;
    uint32_t m_IdTypeVec2;
    uint32_t m_IdTypeVec3;
    uint32_t m_IdTypeVec4;
    uint32_t m_IdTypeInt32;
    uint32_t m_IdTypeVoidFunc;
    uint32_t m_IdPtrInputVec4;       // pointer-to-vec4 in Input storage
    uint32_t m_IdPtrOutputVec4;      // pointer-to-vec4 in Output storage
    uint32_t m_IdPtrFuncVec4;        // pointer-to-vec4 in Function storage
    uint32_t m_IdPtrUniformVec4;     // pointer-to-vec4 in Uniform storage
    uint32_t m_IdConstFloatZero;
    uint32_t m_IdConstFloatOne;
    uint32_t m_IdConstVec4Zero;
    uint32_t m_IdConstVec4One;
    uint32_t m_IdGLSLstd450;         // OpExtInstImport result id

    uint32_t GetConstInt32( int32_t value );
    uint32_t GetConstFloat( float value );
    uint32_t GetConstVec4( float x, float y, float z, float w );

    //--------------------------------------------------------------------------
    // Resource / register variable ids
    //--------------------------------------------------------------------------
    uint32_t m_TempVarIds[MAX_SPV_TEMPS];

    uint32_t m_VSInputIds[MAX_SPV_VS_INPUTS];          // v0..vN
    uint32_t m_VSOutputPosId;                          // oPos (BuiltIn Position)
    uint32_t m_VSOutputColorIds[2];                    // oD0/oD1
    uint32_t m_VSOutputTexCoordIds[MAX_SPV_VS_TEXCOORD_OUT]; // oT0..oT7
    uint32_t m_VSOutputFogId;

    uint32_t m_PSInputIds[MAX_SPV_PS_INPUTS];          // v0..vN (color/texcoord)
    uint32_t m_PSFragCoordId;                          // BuiltIn FragCoord
    uint32_t m_PSOutputColorIds[MAX_SPV_PS_COLOR_OUT]; // oC0..oC3
    uint32_t m_PSOutputDepthId;                        // oDepth (BuiltIn FragDepth)

    uint32_t m_ConstUboStructId;                       // struct { vec4 c[N]; }
    uint32_t m_ConstUboVarId;                          // uniform block variable
    uint32_t m_SamplerImageTypeIds[MAX_SPV_SAMPLERS];  // OpTypeImage per sampler
    uint32_t m_SamplerSampledTypeIds[MAX_SPV_SAMPLERS];// OpTypeSampledImage per sampler
    uint32_t m_SamplerVarIds[MAX_SPV_SAMPLERS];

    //--------------------------------------------------------------------------
    // Tracked translation masks
    //--------------------------------------------------------------------------
    uint32_t m_dwSamplerUsageMask;
    uint32_t m_dwSamplerTypes[MAX_SPV_SAMPLERS];
    uint32_t m_dwConstUsageMask;
    uint32_t m_dwConstIntUsageMask;
    uint32_t m_dwConstBoolUsageMask;
    uint32_t m_dwTempUsageMask;
    uint32_t m_dwVertexAttribMask;
    uint32_t m_dwAttribMap[MAX_SPV_VS_INPUTS];   // (usage<<4)|usageindex per vs input
    uint32_t m_dwTexCoordOutMask;
    uint32_t m_dwVSOutputColorMask;
    uint32_t m_dwPSInputMask;
    uint32_t m_dwPSOutputColorMask;
    bool     m_bVSOutputPos;
    bool     m_bPSOutputDepth;
    uint32_t m_nCentroidMask;

    //--------------------------------------------------------------------------
    // Entry point bookkeeping
    //--------------------------------------------------------------------------
    uint32_t m_EntryFuncId;
    uint32_t m_EntryLabelId;
    std::vector<uint32_t> m_InterfaceVars;   // ids listed in OpEntryPoint

    //--------------------------------------------------------------------------
    // Translation phases
    //--------------------------------------------------------------------------
    void Reset();
    int  DecodeVersionToken( uint32_t dwToken );
    void CollectUsage( const uint32_t* pCode, uint32_t nCodeSize );
    void EmitPrologue();
    void EmitTypesAndGlobals();
    void EmitFunction( const uint32_t* pCode, uint32_t nCodeSize );
    void FinalizeHeader();

    void DeclareTemp( uint32_t nTemp );
    void AddInterfaceVar( uint32_t id );

    //--------------------------------------------------------------------------
    // Register resolution
    //
    // LoadSrc  returns an SSA <id> of type vec4 holding the (swizzled and
    //          source-modifier-adjusted) value of a source register token.
    // GetDstPtr returns an SSA <id> of a pointer to the destination register.
    // StoreDst writes a vec4 value into the destination honouring the write
    //          mask and the saturate destination modifier.
    //--------------------------------------------------------------------------
    uint32_t LoadSrc( uint32_t dwRegToken );
    uint32_t GetDstPtr( uint32_t dwRegToken );
    void     StoreDst( uint32_t dwRegToken, uint32_t valueId );

    uint32_t ApplySwizzle( uint32_t swizzle, uint32_t vec4Id );
    uint32_t ApplySrcModifier( uint32_t mod, uint32_t vec4Id );
    uint32_t BroadcastScalar( uint32_t scalarId );    // scalar -> vec4

    //--------------------------------------------------------------------------
    // Per-opcode translation helpers. Each consumes its operand tokens from
    // the stream and emits SPIR-V into the function body.
    //--------------------------------------------------------------------------
    void TranslateInstruction( uint32_t dwOpcodeToken );
    void Handle_MOV( uint32_t dwOpcodeToken );
    void Handle_Binary( uint32_t dwOpcodeToken, uint16_t spvOp );
    void Handle_MAD( uint32_t dwOpcodeToken );
    void Handle_Dot( uint32_t dwOpcodeToken, uint32_t nComps );
    void Handle_RcpRsq( uint32_t dwOpcodeToken, bool bRsq );
    void Handle_UnaryExtInst( uint32_t dwOpcodeToken, uint32_t extOp );
    void Handle_LRP( uint32_t dwOpcodeToken );
    void Handle_POW( uint32_t dwOpcodeToken );
    void Handle_CRS( uint32_t dwOpcodeToken );
    void Handle_CMP( uint32_t dwOpcodeToken );
    void Handle_SltSge( uint32_t dwOpcodeToken, uint16_t spvOp );
    void Handle_TEX( uint32_t dwOpcodeToken, bool bLod );
    void Handle_TEXKILL( uint32_t dwOpcodeToken );
    void Handle_DCL( uint32_t dwOpcodeToken );
    void Handle_DEF( uint32_t dwOpcodeToken );
    void Handle_NOP( uint32_t dwOpcodeToken );
    void Handle_Unsupported( uint32_t dwOpcodeToken, const char* pName );
};


#endif // DX9_ASM_TO_SPV_H
