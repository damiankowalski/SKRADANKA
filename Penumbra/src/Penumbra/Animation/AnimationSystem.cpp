#include "pnpch.h"
#include "Penumbra/Animation/AnimationSystem.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Scene/Scene.h"

namespace Penumbra {

	namespace {

		float ClipDuration(const MeshData* mesh, const std::string& clip)
		{
			const AnimationClip* found = mesh ? mesh->FindAnimation(clip) : nullptr;
			return found ? found->Duration : 0.0f;
		}

	}

	void AnimationSystem::Update(Scene& scene, float deltaTime)
	{
		for (auto [handle, animator, meshComponent] : scene.GetRegistry().view<AnimatorComponent, MeshComponent>().each())
		{
			const std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(meshComponent.Mesh);
			UpdateAnimator(animator, mesh.get(), deltaTime);
		}
	}

	void AnimationSystem::UpdateAnimator(AnimatorComponent& animator, const MeshData* mesh, float deltaTime)
	{
		// A new target clip: crossfade from whatever is playing.
		if (animator.Clip != animator.RuntimeClip)
		{
			if (mesh && !animator.Clip.empty() && !mesh->FindAnimation(animator.Clip))
				PN_CORE_WARN("Animator: mesh '{}' has no clip '{}'", mesh->Name, animator.Clip);
			const float fade = animator.RuntimeRequestedFade >= 0.0f ? animator.RuntimeRequestedFade : animator.BlendDuration;
			if (animator.RuntimeClip.empty() || fade <= 0.0f)
			{
				animator.RuntimePreviousClip.clear();
				animator.RuntimeBlend = 1.0f;
				animator.RuntimeFadeDuration = 0.0f;
			}
			else
			{
				animator.RuntimePreviousClip = animator.RuntimeClip;
				animator.RuntimePreviousTime = animator.RuntimeTime;
				animator.RuntimeBlend = 0.0f;
				animator.RuntimeFadeDuration = fade;
			}
			animator.RuntimeClip = animator.Clip;
			animator.RuntimeTime = animator.Speed < 0.0f ? ClipDuration(mesh, animator.Clip) : 0.0f;
			animator.RuntimeFinished = false;
			// The first frame of the new clip is shown as is; time starts advancing next update.
			animator.RuntimeRequestedFade = -1.0f;
			return;
		}
		animator.RuntimeRequestedFade = -1.0f;

		if (animator.RuntimeBlend < 1.0f)
		{
			animator.RuntimeBlend = animator.RuntimeFadeDuration > 0.0f ? animator.RuntimeBlend + deltaTime / animator.RuntimeFadeDuration : 1.0f;
			if (animator.RuntimeBlend >= 1.0f)
			{
				animator.RuntimeBlend = 1.0f;
				animator.RuntimePreviousClip.clear();
			}
		}

		if (!animator.Playing)
			return;
		const float step = deltaTime * animator.Speed;
		if (!animator.RuntimePreviousClip.empty())
			animator.RuntimePreviousTime = Animation::WrapTime(animator.RuntimePreviousTime + step, ClipDuration(mesh, animator.RuntimePreviousClip), true);

		const float duration = ClipDuration(mesh, animator.RuntimeClip);
		if (duration <= 0.0f || animator.RuntimeFinished)
			return;
		const float time = animator.RuntimeTime + step;
		if (animator.Loop)
		{
			animator.RuntimeTime = Animation::WrapTime(time, duration, true);
		}
		else
		{
			animator.RuntimeTime = std::clamp(time, 0.0f, duration);
			animator.RuntimeFinished = step >= 0.0f ? time >= duration : time <= 0.0f;
		}
	}

	bool AnimationSystem::Play(AnimatorComponent& animator, const MeshData* mesh, const std::string& clip, float fade, std::optional<bool> loop,
		std::string& error)
	{
		if (mesh && !clip.empty() && !mesh->FindAnimation(clip))
		{
			error = std::format("mesh '{}' has no animation clip '{}'", mesh->Name, clip);
			return false;
		}
		if (loop)
			animator.Loop = *loop;
		animator.Playing = true;
		if (clip == animator.Clip && clip == animator.RuntimeClip)
		{
			if (animator.RuntimeFinished)
			{
				animator.RuntimeTime = animator.Speed < 0.0f ? ClipDuration(mesh, clip) : 0.0f;
				animator.RuntimeFinished = false;
			}
			return true;
		}
		animator.Clip = clip;
		animator.RuntimeRequestedFade = fade;
		return true;
	}

	bool AnimationSystem::IsPlaying(const AnimatorComponent& animator, const std::string* clip)
	{
		const std::string& current = animator.RuntimeClip == animator.Clip ? animator.RuntimeClip : animator.Clip;
		if (clip && *clip != current)
			return false;
		return animator.Playing && !current.empty() && !(animator.RuntimeFinished && animator.RuntimeClip == animator.Clip);
	}

	void AnimationSystem::EvaluatePose(const MeshData& mesh, const AnimatorComponent* animator, Pose& pose)
	{
		pose = Animation::RestPose(mesh.Skeleton);
		if (!animator)
			return;
		// A ragdoll (or anything else) has taken over the pose.
		if (animator->RuntimePoseOverride && animator->RuntimePoseOverride->size() == mesh.Skeleton.Nodes.size())
		{
			pose = *animator->RuntimePoseOverride;
			return;
		}

		// Not updated yet (editor, first frame): show the authored clip's first frame.
		const bool started = !animator->RuntimeClip.empty();
		const AnimationClip* current = mesh.FindAnimation(started ? animator->RuntimeClip : animator->Clip);
		if (current)
			Animation::SampleClip(*current, started ? animator->RuntimeTime : 0.0f, pose);

		const AnimationClip* previous = animator->RuntimeBlend < 1.0f ? mesh.FindAnimation(animator->RuntimePreviousClip) : nullptr;
		if (previous)
		{
			Pose from = Animation::RestPose(mesh.Skeleton);
			Animation::SampleClip(*previous, animator->RuntimePreviousTime, from);
			// Smoothstep weight: eases in and out of the transition.
			const float t = std::clamp(animator->RuntimeBlend, 0.0f, 1.0f);
			Animation::BlendPoses(from, pose, t * t * (3.0f - 2.0f * t), pose);
		}
	}

	void AnimationSystem::EvaluateSkinMatrices(const MeshData& mesh, const AnimatorComponent* animator, std::vector<glm::mat4>& skinMatrices)
	{
		Pose pose;
		EvaluatePose(mesh, animator, pose);
		std::vector<glm::mat4> globals;
		Animation::ComputeGlobalTransforms(mesh.Skeleton, pose, globals);
		Animation::ComputeSkinMatrices(mesh, globals, skinMatrices);
	}

}
