#include "pnpch.h"
#include "Penumbra/Asset/Importers.h"

#include "Penumbra/Math/Math.h"

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

		std::shared_ptr<const TextureData> Shared(std::optional<TextureData> texture)
		{
			return texture ? std::make_shared<const TextureData>(std::move(*texture)) : nullptr;
		}

		std::shared_ptr<const TextureData> LoadImage(const cgltf_image* image, const std::filesystem::path& baseDirectory,
													 const GltfImporter::ExternalImageLoader& loadExternal, std::string& error)
		{
			if (image->buffer_view)
			{
				const cgltf_buffer_view* view = image->buffer_view;
				if (!view->buffer || !view->buffer->data)
				{
					error = "image buffer is not loaded";
					return nullptr;
				}
				const uint8_t* bytes = static_cast<const uint8_t*>(view->buffer->data) + view->offset;
				return Shared(TextureImporter::LoadFromMemory(bytes, view->size, error));
			}
			if (!image->uri)
			{
				error = "image has neither a URI nor a buffer view";
				return nullptr;
			}

			const std::string_view uri(image->uri);
			if (uri.starts_with("data:"))
			{
				const size_t comma = uri.find(',');
				if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos)
				{
					error = "unsupported data URI";
					return nullptr;
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
					return nullptr;
				}
				std::optional<TextureData> texture = TextureImporter::LoadFromMemory(static_cast<const uint8_t*>(decoded), size, error);
				// cgltf allocates with the default allocator (malloc) when options carry no custom one.
				std::free(decoded);
				return Shared(std::move(texture));
			}

			std::string decodedUri(uri);
			cgltf_decode_uri(decodedUri.data());
			decodedUri.resize(std::strlen(decodedUri.c_str()));
			const std::filesystem::path file = baseDirectory / std::filesystem::path(std::u8string(decodedUri.begin(), decodedUri.end()));
			if (loadExternal)
				return loadExternal(file, error);
			return Shared(TextureImporter::LoadFromFile(file, error));
		}

		TextureIndex TextureOf(const cgltf_data* data, const cgltf_texture_view& view)
		{
			if (!view.texture || !view.texture->image)
				return -1;
			return static_cast<TextureIndex>(cgltf_image_index(data, view.texture->image));
		}

		// How the vertices of a primitive are bound to the skeleton (skeletal files only).
		struct PrimitiveSkin
		{
			bool UseJointAttributes = false; // skinned primitive: JOINTS_0/WEIGHTS_0 index the skin's joints
			uint32_t JointOffset = 0;        // palette index of the skin's first joint
			uint32_t JointCount = 0;
			uint32_t RigidJoint = 0;         // palette entry used by rigid (non-skinned) primitives
		};

		void AppendPrimitive(MeshData& mesh, const cgltf_primitive& primitive, const glm::mat4& world, uint32_t materialIndex,
			const std::string& nodeName, const PrimitiveSkin* skin)
		{
			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* tangents = nullptr;
			const cgltf_accessor* texcoords = nullptr;
			const cgltf_accessor* joints = nullptr;
			const cgltf_accessor* weights = nullptr;
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
				else if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0)
					joints = attribute.data;
				else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0)
					weights = attribute.data;
			}
			if (!positions || positions->count == 0)
				return;

			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
			const bool flipWinding = glm::determinant(glm::mat3(world)) < 0.0f;
			const uint32_t baseVertex = static_cast<uint32_t>(mesh.Vertices.size());
			const bool useJoints = skin && skin->UseJointAttributes && skin->JointCount > 0 && joints && weights &&
								   joints->count >= positions->count && weights->count >= positions->count;

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

				if (!skin)
					continue;
				SkinVertex skinVertex;
				skinVertex.Joints = glm::uvec4(skin->RigidJoint);
				skinVertex.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
				if (useJoints)
				{
					cgltf_uint jointValues[4] = { 0, 0, 0, 0 };
					float weightValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					cgltf_accessor_read_uint(joints, i, jointValues, 4);
					cgltf_accessor_read_float(weights, i, weightValues, 4);
					float sum = 0.0f;
					for (int k = 0; k < 4; k++)
					{
						// Out-of-range joints (broken files) lose their influence instead of reading garbage.
						if (jointValues[k] >= skin->JointCount || !(weightValues[k] > 0.0f))
							weightValues[k] = 0.0f;
						skinVertex.Joints[k] = skin->JointOffset + std::min<uint32_t>(jointValues[k], skin->JointCount - 1);
						skinVertex.Weights[k] = weightValues[k];
						sum += weightValues[k];
					}
					if (sum > 1e-6f)
						skinVertex.Weights /= sum;
					else
						skinVertex.Weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
				}
				mesh.SkinVertices.push_back(skinVertex);
			}

			Submesh submesh;
			submesh.IndexOffset = static_cast<uint32_t>(mesh.Indices.size());
			submesh.MaterialIndex = materialIndex;
			submesh.Node = nodeName;
			if (primitive.indices)
			{
				for (cgltf_size i = 0; i + 2 < primitive.indices->count; i += 3)
				{
					uint32_t a = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
					uint32_t b = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 1));
					uint32_t c = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i + 2));
					if (a >= positions->count || b >= positions->count || c >= positions->count)
						continue;
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

		std::string NodeName(const cgltf_data* data, const cgltf_node* node)
		{
			return node->name ? std::string(node->name) : std::format("Node{}", cgltf_node_index(data, node));
		}

		uint32_t MaterialOf(const cgltf_data* data, const cgltf_primitive& primitive, uint32_t defaultMaterial)
		{
			return primitive.material ? static_cast<uint32_t>(cgltf_material_index(data, primitive.material)) : defaultMaterial;
		}

		glm::mat4 WorldTransform(const cgltf_node* node)
		{
			float matrix[16];
			cgltf_node_transform_world(node, matrix);
			return glm::make_mat4(matrix);
		}

		NodeTransform LocalTransform(const cgltf_node* node)
		{
			NodeTransform transform;
			if (node->has_matrix)
			{
				Math::DecomposeTransform(glm::make_mat4(node->matrix), transform.Translation, transform.Rotation, transform.Scale);
				return transform;
			}
			if (node->has_translation)
				transform.Translation = glm::make_vec3(node->translation);
			if (node->has_rotation)
				transform.Rotation = glm::normalize(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
			if (node->has_scale)
				transform.Scale = glm::make_vec3(node->scale);
			return transform;
		}

		// Static files: every node's mesh is baked into one rigid mesh.
		void VisitNode(MeshData& mesh, const cgltf_data* data, const cgltf_node* node, uint32_t defaultMaterial)
		{
			if (node->mesh)
			{
				const glm::mat4 world = WorldTransform(node);
				const std::string name = NodeName(data, node);
				for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
				{
					const cgltf_primitive& primitive = node->mesh->primitives[p];
					if (primitive.type == cgltf_primitive_type_triangles)
						AppendPrimitive(mesh, primitive, world, MaterialOf(data, primitive, defaultMaterial), name, nullptr);
				}
			}
			for (cgltf_size i = 0; i < node->children_count; i++)
				VisitNode(mesh, data, node->children[i], defaultMaterial);
		}

		// Files with skins or animations: the node hierarchy becomes the skeleton, skinned primitives
		// keep their joint weights and every other primitive is rigidly bound to its own node.
		class SkeletonBuilder
		{
		public:
			SkeletonBuilder(const cgltf_data* data, MeshData& mesh, uint32_t defaultMaterial)
				: m_Data(data), m_Mesh(mesh), m_DefaultMaterial(defaultMaterial), m_NodeToSkeleton(data->nodes_count, -1)
			{
			}

			void Build(const std::vector<const cgltf_node*>& roots)
			{
				for (const cgltf_node* root : roots)
					AddNode(root, -1);
				AddSkins();
				for (const cgltf_node* root : roots)
					AddMeshes(root);
				AddAnimations();
				ComputeJointBounds();
			}

		private:
			int32_t SkeletonIndex(const cgltf_node* node) const { return m_NodeToSkeleton[cgltf_node_index(m_Data, node)]; }

			// Depth-first pre-order keeps parents before their children.
			void AddNode(const cgltf_node* node, int32_t parent)
			{
				const size_t index = cgltf_node_index(m_Data, node);
				if (m_NodeToSkeleton[index] >= 0)
					return;
				SkeletonNode skeletonNode;
				skeletonNode.Name = NodeName(m_Data, node);
				skeletonNode.Parent = parent;
				skeletonNode.Rest = LocalTransform(node);
				const int32_t self = static_cast<int32_t>(m_Mesh.Skeleton.Nodes.size());
				m_NodeToSkeleton[index] = self;
				m_Mesh.Skeleton.Nodes.push_back(std::move(skeletonNode));
				for (cgltf_size i = 0; i < node->children_count; i++)
					AddNode(node->children[i], self);
			}

			void AddSkins()
			{
				for (cgltf_size s = 0; s < m_Data->skins_count; s++)
				{
					const cgltf_skin& skin = m_Data->skins[s];
					m_SkinJointOffset.push_back(static_cast<uint32_t>(m_Mesh.Joints.size()));
					for (cgltf_size j = 0; j < skin.joints_count; j++)
					{
						SkinJoint joint;
						const int32_t node = SkeletonIndex(skin.joints[j]);
						if (node < 0)
							PN_CORE_WARN("glTF skin {}: joint {} is not part of the scene", s, j);
						joint.Node = static_cast<uint32_t>(std::max(node, 0));
						float matrix[16];
						if (skin.inverse_bind_matrices && cgltf_accessor_read_float(skin.inverse_bind_matrices, j, matrix, 16))
							joint.InverseBind = glm::make_mat4(matrix);
						m_Mesh.Joints.push_back(joint);
					}
				}
			}

			uint32_t RigidJoint(const cgltf_node* node)
			{
				const int32_t skeletonNode = SkeletonIndex(node);
				if (const auto it = m_RigidJoints.find(skeletonNode); it != m_RigidJoints.end())
					return it->second;
				// Vertices are baked with the node's rest transform, so the palette entry undoes it.
				SkinJoint joint;
				joint.Node = static_cast<uint32_t>(std::max(skeletonNode, 0));
				joint.InverseBind = glm::inverse(WorldTransform(node));
				const uint32_t index = static_cast<uint32_t>(m_Mesh.Joints.size());
				m_Mesh.Joints.push_back(joint);
				m_RigidJoints.emplace(skeletonNode, index);
				return index;
			}

			void AddMeshes(const cgltf_node* node)
			{
				if (node->mesh)
				{
					const std::string name = NodeName(m_Data, node);
					for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
					{
						const cgltf_primitive& primitive = node->mesh->primitives[p];
						if (primitive.type != cgltf_primitive_type_triangles)
							continue;
						bool hasJoints = false;
						for (cgltf_size a = 0; a < primitive.attributes_count; a++)
							hasJoints |= primitive.attributes[a].type == cgltf_attribute_type_joints;

						PrimitiveSkin binding;
						glm::mat4 transform(1.0f);
						if (node->skin && hasJoints)
						{
							// Skinned meshes ignore their node transform: vertices are in bind space (glTF 2.0).
							binding.UseJointAttributes = true;
							binding.JointOffset = m_SkinJointOffset[cgltf_skin_index(m_Data, node->skin)];
							binding.JointCount = static_cast<uint32_t>(node->skin->joints_count);
						}
						else
						{
							// Rigid parts (capes, weapons in a hand slot) follow their node, which follows its parent bone.
							transform = WorldTransform(node);
							binding.RigidJoint = RigidJoint(node);
						}
						AppendPrimitive(m_Mesh, primitive, transform, MaterialOf(m_Data, primitive, m_DefaultMaterial), name, &binding);
					}
				}
				for (cgltf_size i = 0; i < node->children_count; i++)
					AddMeshes(node->children[i]);
			}

			void AddAnimations()
			{
				for (cgltf_size a = 0; a < m_Data->animations_count; a++)
				{
					const cgltf_animation& source = m_Data->animations[a];
					AnimationClip clip;
					clip.Name = source.name ? source.name : std::format("Animation{}", a);
					for (cgltf_size c = 0; c < source.channels_count; c++)
					{
						std::optional<AnimationChannel> channel = ReadChannel(source.channels[c]);
						if (!channel)
						{
							PN_CORE_WARN("glTF animation '{}': channel {} skipped (unsupported target or bad keyframes)", clip.Name, c);
							continue;
						}
						clip.Duration = std::max(clip.Duration, channel->Times.back());
						clip.Channels.push_back(std::move(*channel));
					}
					m_Mesh.Animations.push_back(std::move(clip));
				}
			}

			std::optional<AnimationChannel> ReadChannel(const cgltf_animation_channel& source) const
			{
				if (!source.target_node || !source.sampler || !source.sampler->input || !source.sampler->output)
					return std::nullopt;
				const int32_t node = SkeletonIndex(source.target_node);
				if (node < 0)
					return std::nullopt;

				AnimationChannel channel;
				channel.Node = static_cast<uint32_t>(node);
				size_t components = 3;
				switch (source.target_path)
				{
					case cgltf_animation_path_type_translation: channel.Path = AnimationPath::Translation; break;
					case cgltf_animation_path_type_rotation:    channel.Path = AnimationPath::Rotation; components = 4; break;
					case cgltf_animation_path_type_scale:       channel.Path = AnimationPath::Scale; break;
					default:                                    return std::nullopt; // morph target weights are not supported
				}
				switch (source.sampler->interpolation)
				{
					case cgltf_interpolation_type_step:         channel.Interpolation = AnimationInterpolation::Step; break;
					case cgltf_interpolation_type_cubic_spline: channel.Interpolation = AnimationInterpolation::CubicSpline; break;
					default:                                    channel.Interpolation = AnimationInterpolation::Linear; break;
				}

				const cgltf_accessor* input = source.sampler->input;
				const cgltf_accessor* output = source.sampler->output;
				channel.Times.resize(input->count);
				for (cgltf_size k = 0; k < input->count; k++)
				{
					if (!cgltf_accessor_read_float(input, k, &channel.Times[k], 1) || !std::isfinite(channel.Times[k]) ||
						(k > 0 && channel.Times[k] < channel.Times[k - 1]))
						return std::nullopt;
				}
				channel.Values.resize(output->count, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
				for (cgltf_size k = 0; k < output->count; k++)
				{
					if (!cgltf_accessor_read_float(output, k, &channel.Values[k][0], components))
						return std::nullopt;
				}
				const size_t stride = channel.Interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
				if (channel.Times.empty() || channel.Values.size() != channel.Times.size() * stride)
					return std::nullopt;
				return channel;
			}

			void ComputeJointBounds()
			{
				for (size_t v = 0; v < m_Mesh.SkinVertices.size(); v++)
				{
					const SkinVertex& skin = m_Mesh.SkinVertices[v];
					for (int k = 0; k < 4; k++)
					{
						if (skin.Weights[k] <= 0.0f || skin.Joints[k] >= m_Mesh.Joints.size())
							continue;
						SkinJoint& joint = m_Mesh.Joints[skin.Joints[k]];
						const glm::vec3& position = m_Mesh.Vertices[v].Position;
						joint.BoundsMin = joint.HasBounds ? glm::min(joint.BoundsMin, position) : position;
						joint.BoundsMax = joint.HasBounds ? glm::max(joint.BoundsMax, position) : position;
						joint.HasBounds = true;
					}
				}
			}

		private:
			const cgltf_data* m_Data;
			MeshData& m_Mesh;
			uint32_t m_DefaultMaterial;
			std::vector<int32_t> m_NodeToSkeleton;   // cgltf node index -> skeleton node (-1: not in the scene)
			std::vector<uint32_t> m_SkinJointOffset; // cgltf skin index -> its first palette entry
			std::unordered_map<int32_t, uint32_t> m_RigidJoints; // skeleton node -> palette entry
		};

	}

	std::optional<MeshData> GltfImporter::Import(const std::filesystem::path& path, std::string& error, const ExternalImageLoader& loadExternal)
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
			std::shared_ptr<const TextureData> texture = LoadImage(&data->images[i], baseDirectory, loadExternal, imageError);
			if (!texture)
			{
				PN_CORE_WARN("glTF '{}': image {} skipped: {}", pathString, i, imageError);
				texture = std::make_shared<const TextureData>();
			}
			mesh.Textures.push_back(std::move(texture));
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

		std::vector<const cgltf_node*> roots;
		if (const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr))
		{
			roots.assign(scene->nodes, scene->nodes + scene->nodes_count);
		}
		else
		{
			for (cgltf_size i = 0; i < data->nodes_count; i++)
			{
				if (!data->nodes[i].parent)
					roots.push_back(&data->nodes[i]);
			}
		}

		if (data->skins_count > 0 || data->animations_count > 0)
		{
			SkeletonBuilder(data.get(), mesh, defaultMaterial).Build(roots);
		}
		else
		{
			for (const cgltf_node* root : roots)
				VisitNode(mesh, data.get(), root, defaultMaterial);
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
