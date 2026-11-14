// Dear ImGui rendering (editor only).

struct ImGuiConstants
{
	float4x4 Projection;
};

// Push constants: root constants on D3D12, a push-constant block on Vulkan (SPIR-V).
#ifdef __spirv__
	#define PN_PUSH_CONSTANT [[vk::push_constant]]
#else
	#define PN_PUSH_CONSTANT
#endif
PN_PUSH_CONSTANT ConstantBuffer<ImGuiConstants> g_ImGui : register(b0);
Texture2D<float4> g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct VertexInput
{
	float2 Position : POSITION;
	float2 TexCoord : TEXCOORD0;
	float4 Color : COLOR0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
	float4 Color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = mul(g_ImGui.Projection, float4(input.Position, 0.0, 1.0));
	output.TexCoord = input.TexCoord;
	output.Color = input.Color;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	return input.Color * g_Texture.Sample(g_Sampler, input.TexCoord);
}
