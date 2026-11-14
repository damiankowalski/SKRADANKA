// Image-based lighting precomputation (compute shaders, run once per environment map).

#include "Common.hlsli"

struct IBLConstants
{
	uint FaceSize;     // destination face size in texels
	float Roughness;   // prefilter: roughness of this mip
	float SourceMip;   // mip of the source to sample (reduces aliasing)
	uint SampleCount;
};

PN_PUSH_CONSTANT ConstantBuffer<IBLConstants> g_IBL : register(b0);
Texture2D<float4> g_Equirect : register(t0);
TextureCube<float4> g_SourceCube : register(t1);
Texture2DArray<float4> g_SourceMip : register(t2);
RWTexture2DArray<float4> g_Output : register(u0);
RWTexture2D<float2> g_OutputLut : register(u1);
SamplerState g_LinearClamp : register(s0);

// Direction through texel (x, y) of a D3D cube face.
float3 CubeDirection(uint face, float2 uv)
{
	float2 p = uv * 2.0 - 1.0;
	switch (face)
	{
		case 0: return normalize(float3(1.0, -p.y, -p.x));
		case 1: return normalize(float3(-1.0, -p.y, p.x));
		case 2: return normalize(float3(p.x, 1.0, p.y));
		case 3: return normalize(float3(p.x, -1.0, -p.y));
		case 4: return normalize(float3(p.x, -p.y, 1.0));
		default: return normalize(float3(-p.x, -p.y, -1.0));
	}
}

float2 DirectionToEquirect(float3 direction)
{
	float u = atan2(direction.z, direction.x) / (2.0 * PI) + 0.5;
	float v = acos(clamp(direction.y, -1.0, 1.0)) / PI;
	return float2(u, v);
}

[numthreads(8, 8, 1)]
void CSEquirectToCube(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_IBL.FaceSize || id.y >= g_IBL.FaceSize)
		return;
	float2 uv = (float2(id.xy) + 0.5) / g_IBL.FaceSize;
	float3 direction = CubeDirection(id.z, uv);
	float3 color = g_Equirect.SampleLevel(g_LinearClamp, DirectionToEquirect(direction), 0).rgb;
	// Clamp extreme sun texels so they do not turn into fireflies in the filtered maps.
	g_Output[id] = float4(min(color, 2000.0), 1.0);
}

[numthreads(8, 8, 1)]
void CSDownsample(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_IBL.FaceSize || id.y >= g_IBL.FaceSize)
		return;
	uint2 source = id.xy * 2;
	float4 sum = g_SourceMip[uint3(source, id.z)] + g_SourceMip[uint3(source + uint2(1, 0), id.z)] +
				 g_SourceMip[uint3(source + uint2(0, 1), id.z)] + g_SourceMip[uint3(source + uint2(1, 1), id.z)];
	g_Output[id] = sum * 0.25;
}

void TangentBasis(float3 N, out float3 T, out float3 B)
{
	float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	T = normalize(cross(up, N));
	B = cross(N, T);
}

[numthreads(8, 8, 1)]
void CSIrradiance(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_IBL.FaceSize || id.y >= g_IBL.FaceSize)
		return;
	float3 N = CubeDirection(id.z, (float2(id.xy) + 0.5) / g_IBL.FaceSize);
	float3 T, B;
	TangentBasis(N, T, B);

	float3 irradiance = 0.0;
	float samples = 0.0;
	const float step = 0.05;
	for (float phi = 0.0; phi < 2.0 * PI; phi += step * 2.0)
	{
		for (float theta = 0.0; theta < 0.5 * PI; theta += step)
		{
			float3 local = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			float3 direction = local.x * T + local.y * B + local.z * N;
			irradiance += g_SourceCube.SampleLevel(g_LinearClamp, direction, g_IBL.SourceMip).rgb * cos(theta) * sin(theta);
			samples += 1.0;
		}
	}
	g_Output[id] = float4(PI * irradiance / samples, 1.0);
}

float RadicalInverse(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
	float a = roughness * roughness;
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	float3 T, B;
	TangentBasis(N, T, B);
	return normalize(T * H.x + B * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void CSPrefilter(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_IBL.FaceSize || id.y >= g_IBL.FaceSize)
		return;
	float3 N = CubeDirection(id.z, (float2(id.xy) + 0.5) / g_IBL.FaceSize);
	float3 V = N;
	float3 color = 0.0;
	float weight = 0.0;
	for (uint i = 0; i < g_IBL.SampleCount; i++)
	{
		float2 xi = float2(float(i) / g_IBL.SampleCount, RadicalInverse(i));
		float3 H = ImportanceSampleGGX(xi, N, max(g_IBL.Roughness, 0.02));
		float3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = dot(N, L);
		if (NdotL > 0.0)
		{
			color += g_SourceCube.SampleLevel(g_LinearClamp, L, g_IBL.SourceMip).rgb * NdotL;
			weight += NdotL;
		}
	}
	g_Output[id] = float4(color / max(weight, 1e-4), 1.0);
}

float GeometrySchlickIBL(float NdotV, float roughness)
{
	float k = (roughness * roughness) / 2.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

[numthreads(8, 8, 1)]
void CSBRDFLut(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_IBL.FaceSize || id.y >= g_IBL.FaceSize)
		return;
	float NdotV = max((id.x + 0.5) / g_IBL.FaceSize, 1e-3);
	float roughness = (id.y + 0.5) / g_IBL.FaceSize;
	float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
	float3 N = float3(0.0, 0.0, 1.0);
	float a = 0.0, b = 0.0;
	const uint samples = 256;
	for (uint i = 0; i < samples; i++)
	{
		float2 xi = float2(float(i) / samples, RadicalInverse(i));
		float3 H = ImportanceSampleGGX(xi, N, roughness);
		float3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = saturate(L.z);
		float NdotH = saturate(H.z);
		float VdotH = saturate(dot(V, H));
		if (NdotL > 0.0)
		{
			float G = GeometrySchlickIBL(NdotV, roughness) * GeometrySchlickIBL(NdotL, roughness);
			float visibility = (G * VdotH) / max(NdotH * NdotV, 1e-4);
			float fresnel = pow(1.0 - VdotH, 5.0);
			a += (1.0 - fresnel) * visibility;
			b += fresnel * visibility;
		}
	}
	g_OutputLut[id.xy] = float2(a, b) / samples;
}
