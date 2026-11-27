#include "TestHelpers.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Asset/Primitives.h"

#include <nlohmann/json.hpp>

using namespace Penumbra;

namespace {

	std::string Base64(const std::vector<uint8_t>& data)
	{
		static const char* s_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		for (size_t i = 0; i < data.size(); i += 3)
		{
			const uint32_t b0 = data[i];
			const uint32_t b1 = i + 1 < data.size() ? data[i + 1] : 0;
			const uint32_t b2 = i + 2 < data.size() ? data[i + 2] : 0;
			const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
			out += s_Alphabet[(triple >> 18) & 63];
			out += s_Alphabet[(triple >> 12) & 63];
			out += i + 1 < data.size() ? s_Alphabet[(triple >> 6) & 63] : '=';
			out += i + 2 < data.size() ? s_Alphabet[triple & 63] : '=';
		}
		return out;
	}

	template<typename T>
	void Append(std::vector<uint8_t>& buffer, const T& value)
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
		buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
	}

	// A glTF with one triangle used by two nodes: one translated, one mirrored (negative scale).
	std::string MakeTriangleGltf(const std::string& textureUri)
	{
		std::vector<uint8_t> buffer;
		for (const glm::vec3& p : { glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0) })
			Append(buffer, p);
		for (const glm::vec2& uv : { glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0, 1) })
			Append(buffer, uv);
		for (uint16_t index : { uint16_t(0), uint16_t(1), uint16_t(2) })
			Append(buffer, index);
		buffer.push_back(0);
		buffer.push_back(0);

		nlohmann::json gltf = {
			{ "asset", { { "version", "2.0" } } },
			{ "scene", 0 },
			{ "scenes", { { { "nodes", { 0, 1 } } } } },
			{ "nodes", { { { "mesh", 0 }, { "translation", { 0, 0, 5 } } }, { { "mesh", 0 }, { "scale", { -1, 1, 1 } } } } },
			{ "meshes", { { { "primitives", { { { "attributes", { { "POSITION", 0 }, { "TEXCOORD_0", 1 } } }, { "indices", 2 }, { "material", 0 } } } } } } },
			{ "materials", { { { "name", "Brick" }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.5, 0.25, 1.0, 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 0.8 }, { "baseColorTexture", { { "index", 0 } } } } }, { "emissiveFactor", { 1.0, 0.5, 0.0 } } } } },
			{ "textures", { { { "source", 0 } } } },
			{ "images", { { { "uri", textureUri } } } },
			{ "buffers", { { { "byteLength", buffer.size() }, { "uri", "data:application/octet-stream;base64," + Base64(buffer) } } } },
			{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 } }, { { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 24 } }, { { "buffer", 0 }, { "byteOffset", 60 }, { "byteLength", 6 } } } },
			{ "accessors", {
				{ { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" }, { "min", { 0, 0, 0 } }, { "max", { 1, 1, 0 } } },
				{ { "bufferView", 1 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC2" } },
				{ { "bufferView", 2 }, { "componentType", 5123 }, { "count", 3 }, { "type", "SCALAR" } } } },
		};
		return gltf.dump();
	}

	void CheckOutwardCounterClockwise(const MeshData& mesh, bool closed)
	{
		REQUIRE(mesh.Indices.size() % 3 == 0);
		for (size_t i = 0; i < mesh.Indices.size(); i += 3)
		{
			const Vertex& a = mesh.Vertices[mesh.Indices[i]];
			const Vertex& b = mesh.Vertices[mesh.Indices[i + 1]];
			const Vertex& c = mesh.Vertices[mesh.Indices[i + 2]];
			const glm::vec3 faceNormal = glm::cross(b.Position - a.Position, c.Position - a.Position);
			if (glm::length(faceNormal) < 1e-7f)
				continue; // degenerate pole triangles
			const glm::vec3 averageNormal = a.Normal + b.Normal + c.Normal;
			CHECK(glm::dot(faceNormal, averageNormal) > 0.0f);
			if (closed)
			{
				const glm::vec3 centroid = (a.Position + b.Position + c.Position) / 3.0f;
				CHECK(glm::dot(faceNormal, centroid) > -1e-5f);
			}
		}
	}

}

TEST_SUITE("Assets")
{
	TEST_CASE("Built-in primitives are well-formed, CCW and correctly sized")
	{
		for (const char* name : { "cube", "sphere", "plane", "capsule", "cylinder" })
		{
			CAPTURE(name);
			std::shared_ptr<const MeshData> mesh = Primitives::GetBuiltin(std::string("builtin:") + name);
			REQUIRE(mesh);
			CHECK(!mesh->Vertices.empty());
			CHECK(mesh->Submeshes.size() == 1);
			CHECK(mesh->Submeshes[0].IndexCount == mesh->Indices.size());
			for (uint32_t index : mesh->Indices)
				CHECK(index < mesh->Vertices.size());
			for (const Vertex& vertex : mesh->Vertices)
			{
				CHECK(Test::Near(glm::length(vertex.Normal), 1.0f, 1e-3f));
				CHECK(Test::Near(glm::length(glm::vec3(vertex.Tangent)), 1.0f, 1e-3f));
				CHECK(std::abs(glm::dot(vertex.Normal, glm::vec3(vertex.Tangent))) < 1e-3f);
			}
			CheckOutwardCounterClockwise(*mesh, std::string(name) != "plane");
		}
		CHECK(Test::Near(Primitives::GetBuiltin("builtin:cube")->Bounds.Max, glm::vec3(0.5f)));
		CHECK(Test::Near(Primitives::GetBuiltin("builtin:capsule")->Bounds.Max.y, 1.0f));
		CHECK(Test::Near(Primitives::GetBuiltin("builtin:sphere")->Bounds.Max.x, 0.5f));
		CHECK(Primitives::GetBuiltin("builtin:cube") == Primitives::GetBuiltin("builtin:cube"));
		CHECK(Primitives::GetBuiltin("builtin:teapot") == nullptr);
	}

	TEST_CASE("The built-in cube's four sides map textures upright: u to the right, v downwards")
	{
		std::shared_ptr<const MeshData> cube = Primitives::GetBuiltin("builtin:cube");
		REQUIRE(cube);
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		int sides = 0;
		for (size_t face = 0; face + 3 < cube->Vertices.size(); face += 4)
		{
			const glm::vec3 normal = cube->Vertices[face].Normal;
			if (std::abs(normal.y) > 0.5f)
				continue;
			++sides;
			CAPTURE(face);
			const glm::vec3 right = glm::cross(up, normal); // to the right, seen from outside
			for (size_t a = face; a < face + 4; ++a)
				for (size_t b = face; b < face + 4; ++b)
				{
					const Vertex& va = cube->Vertices[a];
					const Vertex& vb = cube->Vertices[b];
					if (glm::dot(vb.Position - va.Position, right) > 0.5f)
						CHECK(vb.TexCoord.x > va.TexCoord.x);
					if (vb.Position.y - va.Position.y > 0.5f)
						CHECK(vb.TexCoord.y < va.TexCoord.y);
				}
		}
		CHECK(sides == 4);
	}

	TEST_CASE("Textures load from PNG and HDR data")
	{
		Test::TemporaryProject project;
		const std::vector<uint8_t> pixels = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128 };
		const std::filesystem::path pngPath = project.GetAssetDirectory() / "Textures" / "Test.png";
		REQUIRE(TextureImporter::WritePNG(pngPath, 2, 2, pixels.data(), 8));

		std::string error;
		std::optional<TextureData> texture = TextureImporter::LoadFromFile(pngPath, error);
		REQUIRE_MESSAGE(texture, error);
		CHECK(texture->Width == 2);
		CHECK(texture->Height == 2);
		CHECK(texture->Format == TextureFormat::RGBA8);
		CHECK(texture->Pixels == pixels);

		// Minimal Radiance .hdr (RGBE, uncompressed scanlines): 1x1 pixel of value ~1.0.
		std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
		const unsigned char rgbe[] = { 0x80, 0x80, 0x80, 0x81 }; // mantissa 128/256, exponent 1 -> 1.0
		hdr.append(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
		std::optional<TextureData> hdrTexture = TextureImporter::LoadFromMemory(reinterpret_cast<const uint8_t*>(hdr.data()), hdr.size(), error);
		REQUIRE_MESSAGE(hdrTexture, error);
		CHECK(hdrTexture->Format == TextureFormat::RGBA32F);
		float rgba[4];
		std::memcpy(rgba, hdrTexture->Pixels.data(), sizeof(rgba));
		CHECK(Test::Near(rgba[0], 1.0f, 0.01f));

		CHECK_FALSE(TextureImporter::LoadFromFile(project.GetAssetDirectory() / "missing.png", error).has_value());
		CHECK(AssetManager::GetTexture("Textures/Test.png") != nullptr);
		CHECK(AssetManager::GetTexture("Textures/Test.png") == AssetManager::GetTexture("Textures/Test.png"));
		AssetManager::Clear();
	}

	TEST_CASE("glTF import bakes node transforms, materials and textures")
	{
		Test::TemporaryProject project;
		const std::vector<uint8_t> pixels(4 * 4 * 4, 200);
		REQUIRE(TextureImporter::WritePNG(project.GetAssetDirectory() / "Meshes" / "albedo.png", 4, 4, pixels.data(), 16));
		project.WriteAsset("Meshes/Triangle.gltf", MakeTriangleGltf("albedo.png"));

		std::string error;
		std::optional<MeshData> mesh = GltfImporter::Import(project.GetAssetDirectory() / "Meshes" / "Triangle.gltf", error);
		REQUIRE_MESSAGE(mesh, error);
		CHECK(mesh->Vertices.size() == 6);
		CHECK(mesh->Indices.size() == 6);
		CHECK(mesh->Submeshes.size() == 2);
		REQUIRE(mesh->Materials.size() == 2); // Brick + default
		CHECK(mesh->Materials[0].Name == "Brick");
		CHECK(Test::Near(glm::vec3(mesh->Materials[0].BaseColor), { 0.5f, 0.25f, 1.0f }));
		CHECK(Test::Near(mesh->Materials[0].Roughness, 0.8f));
		CHECK(Test::Near(mesh->Materials[0].Emissive, { 1.0f, 0.5f, 0.0f }));
		REQUIRE(mesh->Materials[0].AlbedoTexture == 0);
		REQUIRE(mesh->Textures.size() == 1);
		CHECK(mesh->Textures[0]->Width == 4);

		// Node 0 translated along +Z.
		CHECK(Test::Near(mesh->Vertices[0].Position, { 0.0f, 0.0f, 5.0f }));
		// Generated normals are missing in the file, generated tangents must still be finite.
		for (const Vertex& vertex : mesh->Vertices)
			CHECK(std::isfinite(vertex.Tangent.x));

		// The mirrored node flips winding so both triangles face +Z after the transform.
		for (size_t i = 0; i < mesh->Indices.size(); i += 3)
		{
			const glm::vec3 a = mesh->Vertices[mesh->Indices[i]].Position;
			const glm::vec3 b = mesh->Vertices[mesh->Indices[i + 1]].Position;
			const glm::vec3 c = mesh->Vertices[mesh->Indices[i + 2]].Position;
			CHECK(glm::cross(b - a, c - a).z > 0.0f);
		}

		CHECK(AssetManager::GetMesh("Meshes/Triangle.gltf") != nullptr);
		CHECK(AssetManager::GetMesh("Meshes/Missing.gltf") == nullptr);
		CHECK(AssetManager::GetMesh("Meshes/albedo.png") == nullptr);
		AssetManager::Clear();
	}

	TEST_CASE("glTF files sharing an image file share one copy of it, the texture cache's")
	{
		// Props built from the same material sheets (Tools/PropGen) must not load a sheet once per prop.
		Test::TemporaryProject project;
		const std::vector<uint8_t> pixels(4 * 4 * 4, 200);
		REQUIRE(TextureImporter::WritePNG(project.GetAssetDirectory() / "Textures" / "sheet.png", 4, 4, pixels.data(), 16));
		project.WriteAsset("Meshes/A.gltf", MakeTriangleGltf("../Textures/sheet.png"));
		project.WriteAsset("Meshes/B/B.gltf", MakeTriangleGltf("../../Textures/sheet.png"));

		const std::shared_ptr<const MeshData> a = AssetManager::GetMesh("Meshes/A.gltf");
		const std::shared_ptr<const MeshData> b = AssetManager::GetMesh("Meshes/B/B.gltf");
		REQUIRE(a);
		REQUIRE(b);
		REQUIRE(a->Textures.size() == 1);
		REQUIRE(b->Textures.size() == 1);
		CHECK(a->Textures[0]->Width == 4);
		CHECK(a->Textures[0] == b->Textures[0]);
		CHECK(a->Textures[0] == AssetManager::GetTexture("Textures/sheet.png"));

		// Imported without the cache (no loader), each file reads its own copy, as before.
		std::string error;
		std::optional<MeshData> direct = GltfImporter::Import(project.GetAssetDirectory() / "Meshes" / "A.gltf", error);
		REQUIRE_MESSAGE(direct, error);
		REQUIRE(direct->Textures.size() == 1);
		CHECK(direct->Textures[0] != a->Textures[0]);
		CHECK(direct->Textures[0]->Width == 4);
		AssetManager::Clear();
	}

	TEST_CASE("Malformed glTF is reported, not crashed on")
	{
		Test::TemporaryProject project;
		project.WriteAsset("Broken.gltf", "{ this is not json");
		std::string error;
		CHECK_FALSE(GltfImporter::Import(project.GetAssetDirectory() / "Broken.gltf", error).has_value());
		CHECK(!error.empty());
	}

	TEST_CASE("Asset hot reload drops modified entries")
	{
		Test::TemporaryProject project;
		const std::vector<uint8_t> pixels(4, 255);
		const std::filesystem::path path = project.GetAssetDirectory() / "a.png";
		REQUIRE(TextureImporter::WritePNG(path, 1, 1, pixels.data(), 4));
		std::shared_ptr<const TextureData> first = AssetManager::GetTexture("a.png");
		REQUIRE(first);
		const uint64_t generation = AssetManager::GetGeneration();
		CHECK(AssetManager::ReloadModified() == 0);

		std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(5));
		CHECK(AssetManager::ReloadModified() == 1);
		CHECK(AssetManager::GetGeneration() > generation);
		CHECK(AssetManager::GetTexture("a.png") != first);
		AssetManager::Clear();
	}
}
