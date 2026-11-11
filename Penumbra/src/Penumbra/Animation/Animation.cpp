#include "pnpch.h"
#include "Penumbra/Animation/Animation.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Penumbra::Animation {

	namespace {

		glm::quat ToQuat(const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); }
		glm::vec4 FromQuat(const glm::quat& q) { return glm::vec4(q.x, q.y, q.z, q.w); }

		glm::quat NormalizedLerp(const glm::quat& a, glm::quat b, float t)
		{
			if (glm::dot(a, b) < 0.0f)
				b = -b;
			const glm::quat q(a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
			const float length = glm::length(q);
			return length > 1e-8f ? q / length : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

	}

	Pose RestPose(const Skeleton& skeleton)
	{
		Pose pose(skeleton.Nodes.size());
		for (size_t i = 0; i < skeleton.Nodes.size(); i++)
			pose[i] = skeleton.Nodes[i].Rest;
		return pose;
	}

	float WrapTime(float time, float duration, bool loop)
	{
		if (duration <= 0.0f || !std::isfinite(time))
			return 0.0f;
		if (!loop)
			return std::clamp(time, 0.0f, duration);
		const float wrapped = std::fmod(time, duration);
		return wrapped < 0.0f ? wrapped + duration : wrapped;
	}

	glm::vec4 SampleChannel(const AnimationChannel& channel, float time)
	{
		const std::vector<float>& times = channel.Times;
		const bool cubic = channel.Interpolation == AnimationInterpolation::CubicSpline;
		const size_t stride = cubic ? 3 : 1;
		if (times.empty() || channel.Values.size() < times.size() * stride)
			return channel.Path == AnimationPath::Rotation ? glm::vec4(0, 0, 0, 1) : (channel.Path == AnimationPath::Scale ? glm::vec4(1) : glm::vec4(0));

		auto value = [&](size_t key) { return channel.Values[key * stride + (cubic ? 1 : 0)]; };
		auto finish = [&](glm::vec4 v) {
			if (channel.Path == AnimationPath::Rotation)
			{
				const float length = glm::length(v);
				v = length > 1e-8f ? v / length : glm::vec4(0, 0, 0, 1);
			}
			return v;
		};

		if (time <= times.front() || times.size() == 1)
			return finish(value(0));
		if (time >= times.back())
			return finish(value(times.size() - 1));

		// First key strictly greater than time; the segment is [next - 1, next].
		const size_t next = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
		const size_t previous = next - 1;
		const float t0 = times[previous], t1 = times[next];
		const float span = t1 - t0;
		const float t = span > 0.0f ? (time - t0) / span : 0.0f;

		switch (channel.Interpolation)
		{
			case AnimationInterpolation::Step:
				return finish(value(previous));
			case AnimationInterpolation::Linear:
			{
				if (channel.Path == AnimationPath::Rotation)
					return FromQuat(NormalizedLerp(ToQuat(value(previous)), ToQuat(value(next)), t));
				return glm::mix(value(previous), value(next), t);
			}
			case AnimationInterpolation::CubicSpline:
			{
				// glTF cubic Hermite spline: stored tangents are per second, so they are scaled by the key interval.
				const glm::vec4 p0 = value(previous);
				const glm::vec4 m0 = channel.Values[previous * 3 + 2] * span; // out-tangent of the previous key
				const glm::vec4 p1 = value(next);
				const glm::vec4 m1 = channel.Values[next * 3 + 0] * span; // in-tangent of the next key
				const float t2 = t * t, t3 = t2 * t;
				const glm::vec4 result = (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0 + (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
				return finish(result);
			}
		}
		return finish(value(previous));
	}

	void SampleClip(const AnimationClip& clip, float time, Pose& pose)
	{
		for (const AnimationChannel& channel : clip.Channels)
		{
			if (channel.Node >= pose.size())
				continue;
			const glm::vec4 value = SampleChannel(channel, time);
			NodeTransform& node = pose[channel.Node];
			switch (channel.Path)
			{
				case AnimationPath::Translation: node.Translation = glm::vec3(value); break;
				case AnimationPath::Rotation:    node.Rotation = ToQuat(value); break;
				case AnimationPath::Scale:       node.Scale = glm::vec3(value); break;
			}
		}
	}

	void BlendPoses(const Pose& from, const Pose& to, float weight, Pose& out)
	{
		const size_t count = std::min(from.size(), to.size());
		out.resize(count);
		weight = std::clamp(weight, 0.0f, 1.0f);
		for (size_t i = 0; i < count; i++)
		{
			NodeTransform blended;
			blended.Translation = glm::mix(from[i].Translation, to[i].Translation, weight);
			blended.Rotation = NormalizedLerp(from[i].Rotation, to[i].Rotation, weight);
			blended.Scale = glm::mix(from[i].Scale, to[i].Scale, weight);
			out[i] = blended;
		}
	}

	glm::mat4 ToMatrix(const NodeTransform& transform)
	{
		glm::mat4 matrix = glm::mat4_cast(transform.Rotation);
		matrix[0] *= transform.Scale.x;
		matrix[1] *= transform.Scale.y;
		matrix[2] *= transform.Scale.z;
		matrix[3] = glm::vec4(transform.Translation, 1.0f);
		return matrix;
	}

	void ComputeGlobalTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& globals)
	{
		const size_t count = std::min(skeleton.Nodes.size(), pose.size());
		globals.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			const glm::mat4 local = ToMatrix(pose[i]);
			const int32_t parent = skeleton.Nodes[i].Parent;
			globals[i] = parent >= 0 && static_cast<size_t>(parent) < i ? globals[static_cast<size_t>(parent)] * local : local;
		}
	}

	void ComputeSkinMatrices(const MeshData& mesh, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& skinMatrices)
	{
		skinMatrices.resize(mesh.Joints.size());
		for (size_t i = 0; i < mesh.Joints.size(); i++)
		{
			const SkinJoint& joint = mesh.Joints[i];
			skinMatrices[i] = joint.Node < globals.size() ? globals[joint.Node] * joint.InverseBind : glm::mat4(1.0f);
		}
	}

	BoundingBox ComputeSkinnedBounds(const MeshData& mesh, const std::vector<glm::mat4>& skinMatrices)
	{
		BoundingBox bounds;
		for (size_t i = 0; i < mesh.Joints.size() && i < skinMatrices.size(); i++)
		{
			const SkinJoint& joint = mesh.Joints[i];
			if (!joint.HasBounds)
				continue;
			for (int corner = 0; corner < 8; corner++)
			{
				const glm::vec3 point((corner & 1) ? joint.BoundsMax.x : joint.BoundsMin.x, (corner & 2) ? joint.BoundsMax.y : joint.BoundsMin.y,
					(corner & 4) ? joint.BoundsMax.z : joint.BoundsMin.z);
				bounds.Expand(glm::vec3(skinMatrices[i] * glm::vec4(point, 1.0f)));
			}
		}
		return bounds.IsValid() ? bounds : mesh.Bounds;
	}

	Vertex SkinVertexCPU(const Vertex& vertex, const SkinVertex& skin, const std::vector<glm::mat4>& skinMatrices)
	{
		glm::mat4 matrix(0.0f);
		for (int i = 0; i < 4; i++)
		{
			if (skin.Weights[i] != 0.0f && skin.Joints[i] < skinMatrices.size())
				matrix += skinMatrices[skin.Joints[i]] * skin.Weights[i];
		}
		Vertex result = vertex;
		result.Position = glm::vec3(matrix * glm::vec4(vertex.Position, 1.0f));
		const glm::mat3 linear(matrix);
		const glm::vec3 normal = linear * vertex.Normal;
		const glm::vec3 tangent = linear * glm::vec3(vertex.Tangent);
		result.Normal = glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal) : vertex.Normal;
		result.Tangent = glm::vec4(glm::dot(tangent, tangent) > 1e-12f ? glm::normalize(tangent) : glm::vec3(vertex.Tangent), vertex.Tangent.w);
		return result;
	}

}
