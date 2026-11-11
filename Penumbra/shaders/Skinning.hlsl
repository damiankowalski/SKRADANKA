// GPU skinning: transforms a skinned mesh's bind-pose vertices by its joint palette into a
// per-instance vertex buffer. The prepass, forward and shadow passes then draw that buffer with
// the regular mesh pipelines, so each instance is skinned once per frame however many views see it.

// Mirrors Penumbra::Vertex (48 bytes).
struct VertexData
{
	float3 Position;
	float3 Normal;
	float4 Tangent;
	float2 TexCoord;
};

// Mirrors Penumbra::SkinVertex (32 bytes).
struct SkinData
{
	uint4 Joints;
	float4 Weights;
};

struct SkinningConstants
{
	uint VertexCount;
	uint JointCount;
	uint2 Padding;
};

ConstantBuffer<SkinningConstants> g_Skinning : register(b0);
StructuredBuffer<VertexData> g_SourceVertices : register(t0);
StructuredBuffer<SkinData> g_SkinVertices : register(t1);
StructuredBuffer<float4x4> g_JointMatrices : register(t2); // column-major glm matrices: skin = global * inverse bind
RWStructuredBuffer<VertexData> g_OutputVertices : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint index = id.x;
	if (index >= g_Skinning.VertexCount)
		return;

	VertexData vertex = g_SourceVertices[index];
	const SkinData skin = g_SkinVertices[index];

	float4x4 skinMatrix = 0;
	[unroll]
	for (uint i = 0; i < 4; i++)
	{
		const uint joint = min(skin.Joints[i], g_Skinning.JointCount - 1);
		skinMatrix += g_JointMatrices[joint] * skin.Weights[i];
	}

	vertex.Position = mul(skinMatrix, float4(vertex.Position, 1.0)).xyz;
	// Joint matrices are rigid (plus uniform scale) in practice, so the linear part also transforms normals.
	const float3x3 linearPart = (float3x3)skinMatrix;
	const float3 normal = mul(linearPart, vertex.Normal);
	const float3 tangent = mul(linearPart, vertex.Tangent.xyz);
	vertex.Normal = dot(normal, normal) > 1e-12 ? normalize(normal) : vertex.Normal;
	vertex.Tangent.xyz = dot(tangent, tangent) > 1e-12 ? normalize(tangent) : vertex.Tangent.xyz;
	g_OutputVertices[index] = vertex;
}
