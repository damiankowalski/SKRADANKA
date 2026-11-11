#pragma once

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Scene/Components.h"

#include <optional>
#include <string>
#include <vector>

namespace Penumbra {

	class Scene;
	class Entity;

	// Advances AnimatorComponents (clip changes, crossfades, looping) and evaluates skinning
	// palettes from them. The animator's state lives entirely in the component, so the renderer
	// can evaluate any entity's pose (including in the editor, where nothing is updated).
	class AnimationSystem
	{
	public:
		// Runs once per runtime frame, after audio (see Scene::RunRuntimeFrame).
		static void Update(Scene& scene, float deltaTime);

		// Advances one animator by deltaTime. mesh may be null (the asset failed to load).
		static void UpdateAnimator(AnimatorComponent& animator, const MeshData* mesh, float deltaTime);

		// Starts a clip. fade < 0 uses the animator's BlendDuration. Replaying the clip that is
		// already playing does nothing unless it finished (non-looping), then it restarts.
		// Returns false with an error when the mesh has no clip with that name.
		static bool Play(AnimatorComponent& animator, const MeshData* mesh, const std::string& clip, float fade, std::optional<bool> loop,
			std::string& error);

		// True while the animator plays (optionally: plays this clip) and has not finished.
		static bool IsPlaying(const AnimatorComponent& animator, const std::string* clip = nullptr);

		// Pose of the animator (rest pose when animator is null or its clip is unknown). Before the
		// first update (editor) the authored Clip is shown at time 0.
		static void EvaluatePose(const MeshData& mesh, const AnimatorComponent* animator, Pose& pose);
		static void EvaluateSkinMatrices(const MeshData& mesh, const AnimatorComponent* animator, std::vector<glm::mat4>& skinMatrices);
	};

}
