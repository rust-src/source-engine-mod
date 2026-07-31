//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Pipeline - Graphics/compute pipeline wrapper
//          Manages VkPipeline and pipeline state compilation/caching
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Vertex input attribute description
//-----------------------------------------------------------------------------
struct DxvkVertexInputAttribute_t
{
	uint32_t nLocation;
	uint32_t nBinding;
	uint32_t nFormat;
	uint32_t nOffset;
};

//-----------------------------------------------------------------------------
// DXVK Vertex input binding description
//-----------------------------------------------------------------------------
struct DxvkVertexInputBinding_t
{
	uint32_t nBinding;
	uint32_t nStride;
	uint32_t nInputRate;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - vertex input
//-----------------------------------------------------------------------------
struct DxvkPipelineVertexInputState_t
{
	uint32_t nBindingCount;
	const DxvkVertexInputBinding_t* pBindings;
	uint32_t nAttributeCount;
	const DxvkVertexInputAttribute_t* pAttributes;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - input assembly
//-----------------------------------------------------------------------------
struct DxvkPipelineInputAssemblyState_t
{
	uint32_t nTopology;
	uint32_t bPrimitiveRestartEnable;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - rasterization
//-----------------------------------------------------------------------------
struct DxvkPipelineRasterizationState_t
{
	uint32_t bDepthClampEnable;
	uint32_t bRasterizerDiscardEnable;
	uint32_t nPolygonMode;
	uint32_t nCullMode;
	uint32_t nFrontFace;
	uint32_t bDepthBiasEnable;
	float fDepthBiasConstantFactor;
	float fDepthBiasClamp;
	float fDepthBiasSlopeFactor;
	float fLineWidth;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - multisample
//-----------------------------------------------------------------------------
struct DxvkPipelineMultisampleState_t
{
	uint32_t nRasterizationSamples;
	uint32_t bSampleShadingEnable;
	float fMinSampleShading;
	const uint32_t* pSampleMask;
	uint32_t bAlphaToCoverageEnable;
	uint32_t bAlphaToOneEnable;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - depth/stencil
//-----------------------------------------------------------------------------
struct DxvkPipelineDepthStencilState_t
{
	uint32_t bDepthTestEnable;
	uint32_t bDepthWriteEnable;
	uint32_t nDepthCompareOp;
	uint32_t bDepthBoundsTestEnable;
	uint32_t bStencilTestEnable;
	uint32_t nFrontFailOp;
	uint32_t nFrontPassOp;
	uint32_t nFrontDepthFailOp;
	uint32_t nFrontCompareOp;
	uint32_t nFrontCompareMask;
	uint32_t nFrontWriteMask;
	uint32_t nFrontReference;
	uint32_t nBackFailOp;
	uint32_t nBackPassOp;
	uint32_t nBackDepthFailOp;
	uint32_t nBackCompareOp;
	uint32_t nBackCompareMask;
	uint32_t nBackWriteMask;
	uint32_t nBackReference;
	float fMinDepthBounds;
	float fMaxDepthBounds;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline color blend attachment
//-----------------------------------------------------------------------------
struct DxvkPipelineColorBlendAttachment_t
{
	uint32_t bBlendEnable;
	uint32_t nSrcColorBlendFactor;
	uint32_t nDstColorBlendFactor;
	uint32_t nColorBlendOp;
	uint32_t nSrcAlphaBlendFactor;
	uint32_t nDstAlphaBlendFactor;
	uint32_t nAlphaBlendOp;
	uint32_t nColorWriteMask;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - color blend
//-----------------------------------------------------------------------------
struct DxvkPipelineColorBlendState_t
{
	uint32_t bLogicOpEnable;
	uint32_t nLogicOp;
	uint32_t nAttachmentCount;
	const DxvkPipelineColorBlendAttachment_t* pAttachments;
	float fBlendConstants[ 4 ];
};

//-----------------------------------------------------------------------------
// DXVK Pipeline shader stage
//-----------------------------------------------------------------------------
struct DxvkPipelineShaderStage_t
{
	uint32_t nStageFlags;
	void* pShaderModule;
	const char* pEntryPoint;
	const void* pSpecializationInfo;
};

//-----------------------------------------------------------------------------
// DXVK Pipeline state subset - viewport
//-----------------------------------------------------------------------------
struct DxvkPipelineViewportState_t
{
	uint32_t nViewportCount;
	const float* pViewports;
	uint32_t nScissorCount;
	const int32_t* pScissors;
};

//-----------------------------------------------------------------------------
// DXVK Graphics Pipeline class
//-----------------------------------------------------------------------------
class CDxvkGraphicsPipeline
{
public:
	CDxvkGraphicsPipeline();
	~CDxvkGraphicsPipeline();

	bool Create( CDxvkAdapter* pAdapter,
				 void* pPipelineCache,
				 void* pPipelineLayout,
				 void* pRenderPass,
				 uint32_t nSubpass,
				 const DxvkPipelineShaderStage_t* pStages,
				 uint32_t nStageCount,
				 const DxvkPipelineVertexInputState_t& vertexInput,
				 const DxvkPipelineInputAssemblyState_t& inputAssembly,
				 const DxvkPipelineViewportState_t& viewportState,
				 const DxvkPipelineRasterizationState_t& rasterization,
				 const DxvkPipelineMultisampleState_t& multisample,
				 const DxvkPipelineDepthStencilState_t& depthStencil,
				 const DxvkPipelineColorBlendState_t& colorBlend,
				 const void* pDynamicStates = nullptr,
				 uint32_t nDynamicStateCount = 0,
				 const char* pDebugName = nullptr );
	void Destroy();

	bool IsValid() const { return m_pVkPipeline != nullptr; }
	void* GetPipelineHandle() const { return m_pVkPipeline; }
	void* GetPipelineLayout() const { return m_pPipelineLayout; }
	void* GetRenderPass() const { return m_pRenderPass; }
	uint32_t GetSubpass() const { return m_nSubpass; }

	uint32_t GetStageCount() const { return m_stageCount; }
	uint32_t GetColorAttachmentCount() const { return m_nColorAttachments; }
	uint32_t GetSampleCount() const { return m_nSamples; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	bool ValidateState();
	void CacheCreationParameters(
		const DxvkPipelineShaderStage_t* pStages, uint32_t nStageCount,
		const DxvkPipelineVertexInputState_t& vertexInput,
		const DxvkPipelineInputAssemblyState_t& inputAssembly,
		const DxvkPipelineRasterizationState_t& rasterization,
		const DxvkPipelineMultisampleState_t& multisample,
		const DxvkPipelineDepthStencilState_t& depthStencil,
		const DxvkPipelineColorBlendState_t& colorBlend );

	CDxvkAdapter* m_pAdapter;
	void* m_pVkPipeline;
	void* m_pPipelineLayout;
	void* m_pRenderPass;
	uint32_t m_nSubpass;

	uint32_t m_stageCount;
	uint32_t m_nColorAttachments;
	uint32_t m_nSamples;
	uint32_t m_nVertexBindingCount;
	uint32_t m_nVertexAttributeCount;
	uint32_t m_nTopology;

	char m_szDebugName[ 128 ];
};

static CDxvkGraphicsPipeline s_DxvkGraphicsPipeline;

//-----------------------------------------------------------------------------
// CDxvkGraphicsPipeline Implementation
//-----------------------------------------------------------------------------
CDxvkGraphicsPipeline::CDxvkGraphicsPipeline() :
	m_pAdapter( nullptr ),
	m_pVkPipeline( nullptr ),
	m_pPipelineLayout( nullptr ),
	m_pRenderPass( nullptr ),
	m_nSubpass( 0 ),
	m_stageCount( 0 ),
	m_nColorAttachments( 0 ),
	m_nSamples( 1 ),
	m_nVertexBindingCount( 0 ),
	m_nVertexAttributeCount( 0 ),
	m_nTopology( 0 )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkGraphicsPipeline::~CDxvkGraphicsPipeline()
{
	Destroy();
}

bool CDxvkGraphicsPipeline::Create( CDxvkAdapter* pAdapter,
									void* pPipelineCache,
									void* pPipelineLayout,
									void* pRenderPass,
									uint32_t nSubpass,
									const DxvkPipelineShaderStage_t* pStages,
									uint32_t nStageCount,
									const DxvkPipelineVertexInputState_t& vertexInput,
									const DxvkPipelineInputAssemblyState_t& inputAssembly,
									const DxvkPipelineViewportState_t& viewportState,
									const DxvkPipelineRasterizationState_t& rasterization,
									const DxvkPipelineMultisampleState_t& multisample,
									const DxvkPipelineDepthStencilState_t& depthStencil,
									const DxvkPipelineColorBlendState_t& colorBlend,
									const void* pDynamicStates,
									uint32_t nDynamicStateCount,
									const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter ) return false;
	if ( !pPipelineLayout || !pRenderPass ) return false;
	if ( !pStages || nStageCount == 0 ) return false;

	m_pAdapter = pAdapter;
	m_pPipelineLayout = pPipelineLayout;
	m_pRenderPass = pRenderPass;
	m_nSubpass = nSubpass;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	CacheCreationParameters(
		pStages, nStageCount,
		vertexInput, inputAssembly,
		rasterization, multisample,
		depthStencil, colorBlend );

	if ( !ValidateState() )
	{
		Destroy();
		return false;
	}

	return true;
}

void CDxvkGraphicsPipeline::Destroy()
{
	m_pVkPipeline = nullptr;
	m_pPipelineLayout = nullptr;
	m_pRenderPass = nullptr;
	m_nSubpass = 0;
	m_stageCount = 0;
	m_nColorAttachments = 0;
	m_nSamples = 1;
	m_nVertexBindingCount = 0;
	m_nVertexAttributeCount = 0;
	m_nTopology = 0;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkGraphicsPipeline::ValidateState()
{
	if ( m_stageCount == 0 ) return false;
	return true;
}

void CDxvkGraphicsPipeline::CacheCreationParameters(
	const DxvkPipelineShaderStage_t* pStages, uint32_t nStageCount,
	const DxvkPipelineVertexInputState_t& vertexInput,
	const DxvkPipelineInputAssemblyState_t& inputAssembly,
	const DxvkPipelineRasterizationState_t& rasterization,
	const DxvkPipelineMultisampleState_t& multisample,
	const DxvkPipelineDepthStencilState_t& depthStencil,
	const DxvkPipelineColorBlendState_t& colorBlend )
{
	m_stageCount = nStageCount;
	m_nVertexBindingCount = vertexInput.nBindingCount;
	m_nVertexAttributeCount = vertexInput.nAttributeCount;
	m_nTopology = inputAssembly.nTopology;
	m_nSamples = multisample.nRasterizationSamples ? multisample.nRasterizationSamples : 1;
	m_nColorAttachments = colorBlend.nAttachmentCount;
}
