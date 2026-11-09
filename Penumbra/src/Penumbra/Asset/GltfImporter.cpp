#include "pnpch.h"
#include "Penumbra/Asset/Importers.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

namespace Penumbra {

	namespace {

		struct CgltfDeleter
		{
			void operator()(cgltf_data* data) const { cgltf_free(data); }
		};

		const char* ResultToString(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_success:         return "success";
				case cgltf_result_data_too_short:  return "data too short";
				case cgltf_result_unknown_format:  return "unknown format";
				case cgltf_result_invalid_json:    return "invalid JSON";
				case cgltf_result_invalid_gltf:    return "invalid glTF";
				case cgltf_result_invalid_options: return "invalid options";
				case cgltf_result_file_not_found:  return "file not found";
				case cgltf_result_io_error:        return "I/O error";
				case cgltf_result_out_of_memory:   return "out of memory";
				case cgltf_result_legacy_gltf:     return "legacy glTF (1.0) is not supported";
				default:                           return "unknown error";
			}
		}

		std::optional<TextureData> LoadImage(const cgltf_image* image, const std::filesystem::path& baseDirectory, std::string& error)
		{
			if (image->buffer_view)
			{
				const cgltf_buffer_view* view = image->buffer_view;
				if (!view->buffer || !view->buffer->data)
				{
					error = "image buffer is not loaded";
					return std::nullopt;
				}
				const uint8_t* bytes = static_cast<const uint8_t*>(view->buffer->data) + view->offset;
				return TextureImporter::LoadFromMemory(bytes, view->size, error);
			}
			if (!image->uri)
			{
				error = "image has neither a URI nor a buffer view";
				return std::nullopt;
			}

			const std::string_view uri(image->uri);
			if (uri.starts_with("data:"))
			{
				const size_t comma = uri.find(',');
				if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos)
				{
					error = "unsupported data URI";
					return std::nullopt;
				}
				const std::string_view payload = uri.substr(comma + 1);
				size_t padding = 0;
				while (padding < 2 && padding < payload.size() && payload[payload.size() - 1 - padding] == '=')
					padding++;
				const cgltf_size size = (payload.size() / 4) * 3 - padding;
				void* decoded = nullptr;
				cgltf_options options{};
				if (payload.size() % 4 != 0 || cgltf_load_buffer_base64(&options, size, payload.data(), &decoded) != cgltf_result_success)
				{
					error = "invalid base64 image data";
					return std::nullopt;
				}
				std::optional<TextureData> texture = TextureImporter::LoadFromMemory(static_cast<const uint8_t*>(decoded), size, error);
				// cgltf allocates with the default allocator (malloc) when options carry no custom one.
				std::free(decoded);
				return texture;
			}

			std::string decodedUri(uri);
			cgltf_decode_uri(decodedUri.data());
			decodedUri.resize(std::strlen(decodedUri.c_str()));
			return TextureImporter::LoadFromFile(baseDirectory / std::filesystem::path(std::u8string(decodedUri.begin(), decodedUri.end())), error);
		}

		TextureIndex TextureOf(const cgltf_data* data, const cgltf_texture_view& view)
		{
			if (!view.texture || !view.texture->image)
				return -1;
			return static_cast<TextureIndex>(cgltf_image_index(data, view.texture->image));
		}

		void AppendPrimitive(MeshData& mesh, const cgltf_primitive& primitive, const glm::mat4& world, uint32_t materialIndex)
		{
			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* tangents = nullptr;
			const cgltf_accessor* texcoords = nullptr;
			for (cgltf_size i = 0; i < primitive.attributes_count; i++)
			{
				const cgltf_attribute& attribute = primitive.attributes[i];
				if (attribute.type == cgltf_attribute_type_position)
					positions = attribute.data;
				else if (attribute.type == cgltf_attribute_type_normal)
					normals = attribute.data;
				else if (attribute.type == cgltf_attribute_type_tangent)
					tangents = attribute.data;
				else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
					texcoords = attribute.data;
			}
			if (!positions || positions->count == 0)
				return;

			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
			const bool flipWinding = glm::determinant(glm::mat3(world)) < 0.0f;
			const uint32_t baseVertex = static_cast<uint32_t>(mesh.Vertices.size());

			for (cgltf_size i = 0; i < positions->count; i++)
			{
				Vertex vertex;
				float value[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				cgltf_accessor_read_float(positions, i, value, 3);
				vertex.Position = glm::vec3(world * glm::vec4(value[0], value[1], value[2], 1.0f));
				if (normals && cgltf_accessor_read_float(normals, i, value, 3))
					vertex.Normal = glm::normalize(normalMatrix * glm::vec3(value[0], value[1], value[2]));
				if (tangents && cgltf_accessor_read_float(tangents, i, value, 4))
					vertex.Tangent = glm::vec4(glm::normalize(glm::mat3(world) * glm::vec3(value[0], value[1], value[2])), value[3]);
				if (texcoords && cgltf_accessor_read_float(texcoords, i, value, 2))
					vertex.TexCoord = glm::vec2(value[0], value[1]);
				mesh.Vertices.push_back(vertex);
			}

			Submesh submesh;
			submesh.IndexOffset = static_cast<uint32_t>(mesh.Indices.size());
			submesh.MaterialIndex = materialIndex;
			if (primitive.indices)
			{
				for (cgltf_size i = 0; i + 2 < primitive.indices->count; i += 3)
				{
					uint32_t a = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
					uint32_t b = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 1));
					uint32_t c = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 2));
					if (flipWinding)
						std::swap(b, c);
					mesh.Indices.insert(mesh.Indices.end(), { baseVertex + a, baseVertex + b, baseVertex + c });
				}
			}
			else
			{
				for (uint32_t i = 0; i + 2 < positions->count; i += 3)
				{
					if (flipWinding)
						mesh.Indices.insert(mesh.Indices.end(), { baseVertex + i, baseVertex + i + 2, baseVertex + i + 1 });
					else
						mesh.Indices.insert(mesh.Indices.end(), { baseVertex + i, baseVertex + i + 1, baseVertex + i + 2 });
				}
			}
			submesh.IndexCount = static_cast<uint32_t>(mesh.Indices.size()) - submesh.IndexOffset;
			if (submesh.IndexCount > 0)
				mesh.Submeshes.push_back(submesh);
		}

		void VisitNode(MeshData& mesh, const cgltf_data* data, const cgltf_node* node, uint32_t defaultMaterial)
		{
			if (node->mesh)
			{
				float matrix[16];
				cgltf_node_transform_world(node, matrix);
				const glm::mat4 world = glm::make_mat4(matrix);
				for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
				{
					const cgltf_primitive& primitive = node->mesh->primitives[p];
					if (primitive.type != cgltf_primitive_type_triangles)
						continue;
					const uint32_t material = primitive.material ? static_cast<uint32_t>(cgltf_material_index(data, primitive.material)) : defaultMaterial;
					AppendPrimitive(mesh, primitive, world, material);
				}
			}
			for (cgltf_size i = 0; i < node->children_count; i++)
				VisitNode(mesh, data, node->children[i], defaultMaterial);
		}

	}

	std::optional<MeshData> GltfImporter::Import(const std::filesystem::path& path, std::string& error)
	{
		cgltf_options options{};
		cgltf_data* rawData = nullptr;
		const std::string pathString = path.string();
		cgltf_result result = cgltf_parse_file(&options, pathString.c_str(), &rawData);
		std::unique_ptr<cgltf_data, CgltfDeleter> data(rawData);
		if (result != cgltf_result_success)
		{
			error = std::format("'{}': {}", pathString, ResultToString(result));
			return std::nullopt;
		}
		result = cgltf_load_buffers(&options, data.get(), pathString.c_str());
		if (result != cgltf_result_success)
		{
			error = std::format("'{}': failed to load buffers ({})", pathString, ResultToString(result));
			return std::nullopt;
		}
		result = cgltf_validate(data.get());
		if (result != cgltf_result_success)
		{
			error = std::format("'{}': validation failed ({})", pathString, ResultToString(result));
			return std::nullopt;
		}

		MeshData mesh;
		mesh.Name = path.stem().string();
		const std::filesystem::path baseDirectory = path.parent_path();

		for (cgltf_size i = 0; i < data->images_count; i++)
		{
			std::string imageError;
			std::optional<TextureData> texture = LoadImage(&data->images[i], baseDirectory, imageError);
			if (!texture)
			{
				PN_CORE_WARN("glTF '{}': image {} skipped: {}", pathString, i, imageError);
				texture = TextureData{};
			}
			mesh.Textures.push_back(std::move(*texture));
		}

		for (cgltf_size i = 0; i < data->materials_count; i++)
		{
			const cgltf_material& source = data->materials[i];
			MaterialData material;
			material.Name = source.name ? source.name : std::format("Material{}", i);
			if (source.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
				material.BaseColor = glm::make_vec4(pbr.base_color_factor);
				material.Metallic = pbr.metallic_factor;
				material.Roughness = pbr.roughness_factor;
				material.AlbedoTexture = TextureOf(data.get(), pbr.base_color_texture);
				material.MetallicRoughnessTexture = TextureOf(data.get(), pbr.metallic_roughness_texture);
			}
			material.NormalTexture = TextureOf(data.get(), source.normal_texture);
			material.EmissiveTexture = TextureOf(data.get(), source.emissive_texture);
			material.Emissive = glm::make_vec3(source.emissive_factor);
			if (source.has_emissive_strength)
				material.Emissive *= source.emissive_strength.emissive_strength;
			material.AlphaBlend = source.alpha_mode == cgltf_alpha_mode_blend;
			mesh.Materials.push_back(material);
		}

		// Primitives without a material use an appended default material.
		const uint32_t defaultMaterial = static_cast<uint32_t>(mesh.Materials.size());
		mesh.Materials.push_back(MaterialData{ .Name = "Default" });

		const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene)
		{
			for (cgltf_size i = 0; i < scene->nodes_count; i++)
				VisitNode(mesh, data.get(), scene->nodes[i], defaultMaterial);
		}
		else
		{
			for (cgltf_size i = 0; i < data->nodes_count; i++)
			{
				if (!data->nodes[i].parent)
					VisitNode(mesh, data.get(), &data->nodes[i], defaultMaterial);
			}
		}

		if (mesh.Indices.empty())
		{
			error = std::format("'{}' contains no triangle geometry", pathString);
			return std::nullopt;
		}

		bool hasTangents = false;
		for (cgltf_size i = 0; i < data->meshes_count && !hasTangents; i++)
		{
			for (cgltf_size p = 0; p < data->meshes[i].primitives_count && !hasTangents; p++)
			{
				const cgltf_primitive& primitive = data->meshes[i].primitives[p];
				for (cgltf_size a = 0; a < primitive.attributes_count; a++)
					hasTangents |= primitive.attributes[a].type == cgltf_attribute_type_tangent;
			}
		}
		if (!hasTangents)
			mesh.ComputeTangents();
		mesh.ComputeBounds();
		return mesh;
	}

}
