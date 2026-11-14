// Depth-only rendering into shadow maps (directional cascades, spot and point lights).

struct ShadowConstants
{
	float4x4 ViewProj;
	float4 WorldRow0;
	float4 WorldRow1;
	float4 WorldRow2;
};

// Push constants: root constants on D3D12, a push-constant block on Vulkan (SPIR-V).
#ifdef __spirv__
	#define PN_PUSH_CONSTANT [[vk::push_constant]]
#else
	#define PN_PUSH_CONSTANT
#endif
PN_PUSH_CONSTANT ConstantBuffer<ShadowConstants> g_Shadow : register(b0, space0);

float4 VSMain(float3 position : POSITION) : SV_Position
{
	float4x4 world = float4x4(g_Shadow.WorldRow0, g_Shadow.WorldRow1, g_Shadow.WorldRow2, float4(0, 0, 0, 1));
	return mul(g_Shadow.ViewProj, mul(world, float4(position, 1.0)));
}
