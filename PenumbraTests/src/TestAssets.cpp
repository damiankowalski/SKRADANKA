#include "TestHelpers.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Penumbra::Test {

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

		// Accumulates binary data and the matching bufferViews/accessors of a glTF document.
		class GltfBuffer
		{
		public:
			template<typename T>
			int Add(const std::vector<T>& values, int componentType, const char* type, size_t count, bool minMax = false)
			{
				while (m_Data.size() % 4 != 0)
					m_Data.push_back(0);
				const size_t offset = m_Data.size();
				const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
				m_Data.insert(m_Data.end(), bytes, bytes + values.size() * sizeof(T));
				m_Views.push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", values.size() * sizeof(T) } });
				nlohmann::json accessor = { { "bufferView", m_Views.size() - 1 }, { "componentType", componentType }, { "count", count }, { "type", type } };
				if (minMax)
				{
					// Scalar float input accessors (keyframe times) need min/max.
					const auto* floats = reinterpret_cast<const float*>(values.data());
					accessor["min"] = { *std::min_element(floats, floats + count) };
					accessor["max"] = { *std::max_element(floats, floats + count) };
				}
				m_Accessors.push_back(accessor);
				return static_cast<int>(m_Accessors.size() - 1);
			}

			void Finish(nlohmann::json& gltf) const
			{
				gltf["buffers"] = { { { "byteLength", m_Data.size() }, { "uri", "data:application/octet-stream;base64," + Base64(m_Data) } } };
				gltf["bufferViews"] = m_Views;
				gltf["accessors"] = m_Accessors;
			}

		private:
			std::vector<uint8_t> m_Data;
			nlohmann::json m_Views = nlohmann::json::array();
			nlohmann::json m_Accessors = nlohmann::json::array();
		};

	}

	std::string MakeSkinnedGltf()
	{
		constexpr int Float = 5126, UnsignedByte = 5121, UnsignedShort = 5123;
		GltfBuffer buffer;

		// A square bar 0.5 wide and 2 tall, as three rings of four vertices (y = 0, 1, 2).
		std::vector<glm::vec3> positions, normals;
		std::vector<uint8_t> joints;
		std::vector<glm::vec4> weights;
		const glm::vec2 corners[4] = { { -0.25f, -0.25f }, { 0.25f, -0.25f }, { 0.25f, 0.25f }, { -0.25f, 0.25f } };
		for (int ring = 0; ring < 3; ring++)
		{
			for (const glm::vec2& corner : corners)
			{
				positions.emplace_back(corner.x, static_cast<float>(ring), corner.y);
				normals.push_back(glm::normalize(glm::vec3(corner.x, 0.0f, corner.y)));
				// Bottom ring: Root, middle: half/half, top: Upper. Joint 3 carries zero weight on purpose.
				joints.insert(joints.end(), { 0, 1, 3, 0 });
				weights.push_back(ring == 0 ? glm::vec4(1, 0, 0, 0) : ring == 1 ? glm::vec4(0.5f, 0.5f, 0, 0) : glm::vec4(0, 1, 0, 0));
			}
		}
		std::vector<uint16_t> indices;
		for (uint16_t segment = 0; segment < 2; segment++)
		{
			for (uint16_t side = 0; side < 4; side++)
			{
				const uint16_t a = static_cast<uint16_t>(segment * 4 + side), b = static_cast<uint16_t>(segment * 4 + (side + 1) % 4);
				const uint16_t c = static_cast<uint16_t>(b + 4), d = static_cast<uint16_t>(a + 4);
				// Counter-clockwise seen from outside.
				indices.insert(indices.end(), { a, d, c, a, c, b });
			}
		}
		buffer.Add(positions, Float, "VEC3", positions.size()); // accessor 0
		buffer.Add(normals, Float, "VEC3", normals.size()); // accessor 1
		buffer.Add(joints, UnsignedByte, "VEC4", positions.size()); // accessor 2
		buffer.Add(weights, Float, "VEC4", weights.size()); // accessor 3
		buffer.Add(indices, UnsignedShort, "SCALAR", indices.size()); // accessor 4

		// Joints: Root at the origin, Upper one unit up. Inverse bind matrices undo the rest pose.
		std::vector<glm::mat4> inverseBind = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)) };
		buffer.Add(inverseBind, Float, "MAT4", 2); // accessor 5

		// A rigid "Hat" triangle parented to Upper (no skin).
		std::vector<glm::vec3> hat = { { -0.3f, 0.0f, 0.0f }, { 0.3f, 0.0f, 0.0f }, { 0.0f, 0.3f, 0.0f } };
		buffer.Add(hat, Float, "VEC3", 3); // accessor 6

		// Animations.
		const glm::quat bent = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		std::vector<float> bendTimes = { 0.0f, 1.0f };
		std::vector<glm::vec4> bendValues = { { 0, 0, 0, 1 }, { bent.x, bent.y, bent.z, bent.w } };
		std::vector<float> slideTimes = { 0.0f, 0.5f };
		std::vector<glm::vec3> slideValues = { { 0, 0, 0 }, { 1, 0, 0 } };
		std::vector<float> growTimes = { 0.0f, 1.0f };
		// Cubic spline: in-tangent, value, out-tangent per key (flat tangents).
		std::vector<glm::vec3> growValues = { { 0, 0, 0 }, { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, { 2, 2, 2 }, { 0, 0, 0 } };
		const int bendIn = buffer.Add(bendTimes, Float, "SCALAR", 2, true);
		const int bendOut = buffer.Add(bendValues, Float, "VEC4", 2);
		const int slideIn = buffer.Add(slideTimes, Float, "SCALAR", 2, true);
		const int slideOut = buffer.Add(slideValues, Float, "VEC3", 2);
		const int growIn = buffer.Add(growTimes, Float, "SCALAR", 2, true);
		const int growOut = buffer.Add(growValues, Float, "VEC3", 6);

		nlohmann::json gltf = {
			{ "asset", { { "version", "2.0" } } },
			{ "scene", 0 },
			// The skinned bar comes first in the scene, so its vertices come first in the mesh.
			{ "scenes", { { { "nodes", { 3, 0 } } } } },
			{ "nodes", {
				{ { "name", "Root" }, { "children", { 1 } } },
				{ { "name", "Upper" }, { "translation", { 0, 1, 0 } }, { "children", { 2 } } },
				{ { "name", "Hat" }, { "translation", { 0, 1, 0 } }, { "mesh", 1 } },
				// The skinned mesh node sits away from the origin: its transform must be ignored.
				{ { "name", "Bar" }, { "mesh", 0 }, { "skin", 0 }, { "translation", { 5, 5, 5 } } } } },
			{ "skins", { { { "joints", { 0, 1 } }, { "inverseBindMatrices", 5 } } } },
			{ "meshes", {
				{ { "primitives", { { { "attributes", { { "POSITION", 0 }, { "NORMAL", 1 }, { "JOINTS_0", 2 }, { "WEIGHTS_0", 3 } } }, { "indices", 4 } } } } },
				{ { "primitives", { { { "attributes", { { "POSITION", 6 } } } } } } } } },
			{ "animations", {
				{ { "name", "Bend" }, { "samplers", { { { "input", bendIn }, { "output", bendOut }, { "interpolation", "LINEAR" } } } },
					{ "channels", { { { "sampler", 0 }, { "target", { { "node", 1 }, { "path", "rotation" } } } } } } },
				{ { "name", "Slide" }, { "samplers", { { { "input", slideIn }, { "output", slideOut }, { "interpolation", "STEP" } } } },
					{ "channels", { { { "sampler", 0 }, { "target", { { "node", 0 }, { "path", "translation" } } } } } } },
				{ { "name", "Grow" }, { "samplers", { { { "input", growIn }, { "output", growOut }, { "interpolation", "CUBICSPLINE" } } } },
					{ "channels", { { { "sampler", 0 }, { "target", { { "node", 0 }, { "path", "scale" } } } } } } } } },
		};
		buffer.Finish(gltf);
		return gltf.dump();
	}

}
