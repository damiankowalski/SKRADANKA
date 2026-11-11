#include "pnpch.h"
#include "Penumbra/Physics/PhysicsWorld.h"

#include "Penumbra/Asset/AssetManager.h"

#include <thread>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Penumbra {

	namespace {

		// --- Jolt process-wide setup (reference counted) ------------------------------

		std::mutex s_JoltMutex;
		int s_JoltUsers = 0;

		void AcquireJolt()
		{
			std::scoped_lock lock(s_JoltMutex);
			if (s_JoltUsers++ == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}
		}

		void ReleaseJolt()
		{
			std::scoped_lock lock(s_JoltMutex);
			if (--s_JoltUsers == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		// --- Layers --------------------------------------------------------------------

		namespace Layers {
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers {
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr uint32_t Count = 2;
		}

		class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				if (layer == Layers::NonMoving)
					return broadPhaseLayer == BroadPhaseLayers::Moving;
				return true;
			}
		};

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return !(a == Layers::NonMoving && b == Layers::NonMoving);
			}
		};

		// --- Conversions -------------------------------------------------------------------

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 FromJolt(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		class EntityBodyFilter final : public JPH::BodyFilter
		{
		public:
			explicit EntityBodyFilter(const PhysicsEntityFilter& filter) : m_Filter(filter) {}

			bool ShouldCollideLocked(const JPH::Body& body) const override
			{
				if (body.IsSensor())
					return false;
				return !m_Filter || m_Filter(UUID(body.GetUserData()));
			}

		private:
			const PhysicsEntityFilter& m_Filter;
		};

		class ContactCollector final : public JPH::ContactListener
		{
		public:
			void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
			{
				const uint64_t a = body1.GetUserData();
				const uint64_t b = body2.GetUserData();
				if (a == 0 || b == 0)
					return;
				const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
				// Called before the solver: the velocities are those of the moment of impact.
				const JPH::Vec3 relative = body1.GetPointVelocity(point) - body2.GetPointVelocity(point);
				const float speed = std::abs(relative.Dot(manifold.mWorldSpaceNormal));
				std::scoped_lock lock(m_Mutex);
				m_Events.push_back({ UUID(a), UUID(b), glm::vec3(point.GetX(), point.GetY(), point.GetZ()), speed });
			}

			std::vector<ContactEvent> Consume()
			{
				std::scoped_lock lock(m_Mutex);
				return std::exchange(m_Events, {});
			}

		private:
			std::mutex m_Mutex;
			std::vector<ContactEvent> m_Events;
		};

		glm::vec3 AbsScale(const glm::vec3& scale) { return glm::abs(scale); }

	}

	struct PhysicsWorld::Impl
	{
		Scene& SceneRef;
		std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
		std::unique_ptr<JPH::JobSystemThreadPool> JobSystem;
		BroadPhaseLayerInterface BroadPhaseLayers;
		ObjectVsBroadPhaseLayerFilter ObjectVsBroadPhase;
		ObjectLayerPairFilter ObjectPairs;
		ContactCollector Contacts;
		std::unique_ptr<JPH::PhysicsSystem> System;
		JPH::CharacterVsCharacterCollisionSimple CharacterCollisions;

		std::unordered_map<UUID, JPH::BodyID> Bodies;
		std::unordered_map<UUID, JPH::Ref<JPH::CharacterVirtual>> Characters;
		std::unordered_map<UUID, float> CharacterHeights; // height of the current character shape
		float Accumulator = 0.0f;
		bool Started = false;

		explicit Impl(Scene& scene) : SceneRef(scene) {}

		JPH::BodyInterface& Bodies_() { return System->GetBodyInterface(); }

		JPH::Ref<JPH::ShapeSettings> BuildShape(Entity entity, const glm::vec3& scale, bool allowMesh)
		{
			struct Part
			{
				JPH::Ref<JPH::ShapeSettings> Shape;
				glm::vec3 Offset{ 0.0f };
			};
			std::vector<Part> parts;
			const glm::vec3 s = AbsScale(scale);
			const float maxScale = std::max({ s.x, s.y, s.z });

			if (const auto* box = entity.TryGetComponent<BoxColliderComponent>())
			{
				const glm::vec3 halfExtents = glm::max(box->HalfExtents * s, glm::vec3(0.01f));
				const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ halfExtents.x, halfExtents.y, halfExtents.z }) * 0.5f);
				parts.push_back({ new JPH::BoxShapeSettings(ToJolt(halfExtents), convexRadius), box->Offset * scale });
			}
			if (const auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
				parts.push_back({ new JPH::SphereShapeSettings(std::max(sphere->Radius * maxScale, 0.01f)), sphere->Offset * scale });
			if (const auto* capsule = entity.TryGetComponent<CapsuleColliderComponent>())
			{
				const float radius = std::max(capsule->Radius * std::max(s.x, s.z), 0.01f);
				const float halfHeight = std::max(capsule->HalfHeight * s.y, 0.01f);
				parts.push_back({ new JPH::CapsuleShapeSettings(halfHeight, radius), capsule->Offset * scale });
			}
			if (entity.HasComponent<MeshColliderComponent>())
			{
				const MeshComponent* meshComponent = entity.TryGetComponent<MeshComponent>();
				std::shared_ptr<const MeshData> mesh = meshComponent ? AssetManager::GetMesh(meshComponent->Mesh) : nullptr;
				if (!allowMesh)
				{
					PN_CORE_WARN("'{}': mesh colliders are only supported on static and kinematic bodies", entity.GetName());
				}
				else if (mesh && mesh->Indices.size() >= 3)
				{
					JPH::VertexList vertices;
					vertices.reserve(mesh->Vertices.size());
					for (const Vertex& vertex : mesh->Vertices)
					{
						const glm::vec3 p = vertex.Position * scale;
						vertices.push_back(JPH::Float3(p.x, p.y, p.z));
					}
					JPH::IndexedTriangleList triangles;
					triangles.reserve(mesh->Indices.size() / 3);
					for (size_t i = 0; i + 2 < mesh->Indices.size(); i += 3)
						triangles.push_back(JPH::IndexedTriangle(mesh->Indices[i], mesh->Indices[i + 1], mesh->Indices[i + 2], 0));
					parts.push_back({ new JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)), glm::vec3(0.0f) });
				}
				else
				{
					PN_CORE_WARN("'{}': mesh collider has no usable mesh", entity.GetName());
				}
			}

			if (parts.empty())
				return nullptr;
			if (parts.size() == 1)
			{
				if (glm::dot(parts[0].Offset, parts[0].Offset) < 1e-12f)
					return parts[0].Shape;
				return new JPH::RotatedTranslatedShapeSettings(ToJolt(parts[0].Offset), JPH::Quat::sIdentity(), parts[0].Shape);
			}
			JPH::Ref<JPH::StaticCompoundShapeSettings> compound = new JPH::StaticCompoundShapeSettings();
			for (const Part& part : parts)
				compound->AddShape(ToJolt(part.Offset), JPH::Quat::sIdentity(), part.Shape);
			return compound.GetPtr();
		}

		void CreateBody(Entity entity)
		{
			const UUID uuid = entity.GetUUID();
			if (Bodies.contains(uuid) || entity.HasComponent<CharacterControllerComponent>())
				return;

			RigidBodyComponent defaults;
			RigidBodyComponent* rigidBody = entity.TryGetComponent<RigidBodyComponent>();
			const RigidBodyComponent& settings = rigidBody ? *rigidBody : defaults;

			glm::vec3 translation, scale;
			glm::quat rotation;
			Math::DecomposeTransform(SceneRef.GetWorldTransform(entity), translation, rotation, scale);

			JPH::Ref<JPH::ShapeSettings> shapeSettings = BuildShape(entity, scale, settings.Type != RigidBodyType::Dynamic);
			if (!shapeSettings)
				return;
			JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings->Create();
			if (shapeResult.HasError())
			{
				PN_CORE_ERROR("'{}': cannot create collision shape: {}", entity.GetName(), shapeResult.GetError().c_str());
				return;
			}

			JPH::EMotionType motionType = JPH::EMotionType::Static;
			if (settings.Type == RigidBodyType::Dynamic)
				motionType = JPH::EMotionType::Dynamic;
			else if (settings.Type == RigidBodyType::Kinematic)
				motionType = JPH::EMotionType::Kinematic;

			JPH::BodyCreationSettings creation(shapeResult.Get(), JPH::RVec3(ToJolt(translation)), ToJolt(rotation), motionType,
				motionType == JPH::EMotionType::Static ? Layers::NonMoving : Layers::Moving);
			creation.mUserData = static_cast<uint64_t>(uuid);
			creation.mFriction = settings.Friction;
			creation.mRestitution = settings.Restitution;
			creation.mLinearDamping = settings.LinearDamping;
			creation.mAngularDamping = settings.AngularDamping;
			creation.mGravityFactor = settings.GravityFactor;
			creation.mIsSensor = settings.IsTrigger;
			if (settings.IsTrigger)
				creation.mCollideKinematicVsNonDynamic = true;
			if (settings.ContinuousCollision)
				creation.mMotionQuality = JPH::EMotionQuality::LinearCast;
			if (motionType == JPH::EMotionType::Dynamic)
			{
				creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				creation.mMassPropertiesOverride.mMass = std::max(settings.Mass, 0.001f);
			}

			const JPH::BodyID bodyID = Bodies_().CreateAndAddBody(creation,
				motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			if (bodyID.IsInvalid())
			{
				PN_CORE_ERROR("'{}': physics body limit reached", entity.GetName());
				return;
			}
			Bodies[uuid] = bodyID;
			if (rigidBody)
				rigidBody->RuntimeBodyID = bodyID.GetIndexAndSequenceNumber();
		}

		// Capsule standing on the entity origin (the character's feet).
		static JPH::Ref<JPH::Shape> MakeCharacterShape(float radius, float height)
		{
			const float halfHeight = std::max(height * 0.5f - radius, 0.05f);
			return JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, halfHeight + radius, 0.0f), JPH::Quat::sIdentity(),
				new JPH::CapsuleShape(halfHeight, radius))
				.Create()
				.Get();
		}

		void CreateCharacter(Entity entity)
		{
			const UUID uuid = entity.GetUUID();
			if (Characters.contains(uuid))
				return;
			const auto& controller = entity.GetComponent<CharacterControllerComponent>();
			const float radius = std::max(controller.Radius, 0.05f);
			JPH::Ref<JPH::Shape> capsule = MakeCharacterShape(radius, controller.Height);
			CharacterHeights[uuid] = controller.Height;

			JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
			settings->mShape = capsule;
			settings->mInnerBodyShape = capsule;
			settings->mInnerBodyLayer = Layers::Moving;
			settings->mMaxSlopeAngle = glm::radians(controller.MaxSlopeDegrees);
			settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
			settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

			const glm::vec3 position = SceneRef.GetWorldPosition(entity);
			const glm::quat rotation = SceneRef.GetWorldRotation(entity);
			JPH::Ref<JPH::CharacterVirtual> character =
				new JPH::CharacterVirtual(settings, JPH::RVec3(ToJolt(position)), ToJolt(rotation), static_cast<uint64_t>(uuid), System.get());
			character->SetCharacterVsCharacterCollision(&CharacterCollisions);
			CharacterCollisions.Add(character);
			Characters[uuid] = character;
		}

		void CreateMissing()
		{
			auto& registry = SceneRef.GetRegistry();
			auto tryCreate = [&](entt::entity handle) {
				Entity entity(handle, &SceneRef);
				if (!SceneRef.IsEntityPendingDestroy(entity))
					CreateBody(entity);
			};
			for (entt::entity handle : registry.view<BoxColliderComponent>())
				tryCreate(handle);
			for (entt::entity handle : registry.view<SphereColliderComponent>())
				tryCreate(handle);
			for (entt::entity handle : registry.view<CapsuleColliderComponent>())
				tryCreate(handle);
			for (entt::entity handle : registry.view<MeshColliderComponent>())
				tryCreate(handle);
			for (entt::entity handle : registry.view<CharacterControllerComponent>())
				CreateCharacter(Entity(handle, &SceneRef));
		}

		void RemoveEntity(UUID uuid)
		{
			if (const auto it = Bodies.find(uuid); it != Bodies.end())
			{
				Bodies_().RemoveBody(it->second);
				Bodies_().DestroyBody(it->second);
				Bodies.erase(it);
			}
			if (const auto it = Characters.find(uuid); it != Characters.end())
			{
				CharacterCollisions.Remove(it->second);
				Characters.erase(it);
				CharacterHeights.erase(uuid);
			}
		}

		void UpdateCharacters(float dt)
		{
			const JPH::Vec3 gravity = System->GetGravity();
			for (auto& [uuid, character] : Characters)
			{
				std::optional<Entity> entity = SceneRef.TryGetEntityByUUID(uuid);
				if (!entity)
					continue;
				auto& controller = entity->GetComponent<CharacterControllerComponent>();

				// Crouching/standing: a changed Height swaps the capsule. Standing up under a low
				// ceiling fails, and the component keeps the height the character actually has.
				float& currentHeight = CharacterHeights[uuid];
				if (std::abs(controller.Height - currentHeight) > 1e-3f)
				{
					const float radius = std::max(controller.Radius, 0.05f);
					controller.Height = std::max(controller.Height, radius * 2.0f + 0.1f);
					JPH::Ref<JPH::Shape> shape = MakeCharacterShape(radius, controller.Height);
					if (character->SetShape(shape, 0.05f, System->GetDefaultBroadPhaseLayerFilter(Layers::Moving),
							System->GetDefaultLayerFilter(Layers::Moving), {}, {}, *TempAllocator))
					{
						character->SetInnerBodyShape(shape);
						currentHeight = controller.Height;
					}
					else
					{
						controller.Height = currentHeight;
					}
				}

				character->UpdateGroundVelocity();
				const JPH::Vec3 groundVelocity = character->GetGroundVelocity();
				const JPH::Vec3 linearVelocity = character->GetLinearVelocity();
				const bool onGround = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

				JPH::Vec3 velocity;
				if (onGround && (linearVelocity.GetY() - groundVelocity.GetY()) < 0.1f)
				{
					velocity = groundVelocity;
					if (controller.RuntimeJumpRequested)
						velocity += JPH::Vec3(0.0f, controller.RuntimeJumpSpeed, 0.0f);
				}
				else
				{
					velocity = JPH::Vec3(0.0f, linearVelocity.GetY(), 0.0f);
				}
				controller.RuntimeJumpRequested = false;
				velocity += gravity * controller.GravityFactor * dt;
				velocity += JPH::Vec3(controller.RuntimeDesiredVelocity.x, 0.0f, controller.RuntimeDesiredVelocity.z);
				character->SetLinearVelocity(velocity);

				JPH::CharacterVirtual::ExtendedUpdateSettings update;
				update.mWalkStairsStepUp = JPH::Vec3(0.0f, controller.StepHeight, 0.0f);
				character->ExtendedUpdate(dt, gravity * controller.GravityFactor, update,
					System->GetDefaultBroadPhaseLayerFilter(Layers::Moving), System->GetDefaultLayerFilter(Layers::Moving), {}, {},
					*TempAllocator);

				controller.RuntimeVelocity = FromJolt(character->GetLinearVelocity());
				controller.RuntimeGrounded = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

				const JPH::RVec3 position = character->GetPosition();
				glm::vec3 translation, scale;
				glm::quat rotation;
				Math::DecomposeTransform(SceneRef.GetWorldTransform(*entity), translation, rotation, scale);
				SceneRef.SetWorldTransform(*entity,
					Math::ComposeTransform(glm::vec3(position.GetX(), position.GetY(), position.GetZ()), rotation, scale));
			}
		}

		void PushKinematicTargets(float dt)
		{
			for (auto [handle, rigidBody] : SceneRef.GetRegistry().view<RigidBodyComponent>().each())
			{
				if (rigidBody.Type != RigidBodyType::Kinematic)
					continue;
				Entity entity(handle, &SceneRef);
				const auto it = Bodies.find(entity.GetUUID());
				if (it == Bodies.end())
					continue;
				glm::vec3 translation, scale;
				glm::quat rotation;
				Math::DecomposeTransform(SceneRef.GetWorldTransform(entity), translation, rotation, scale);
				Bodies_().MoveKinematic(it->second, JPH::RVec3(ToJolt(translation)), ToJolt(rotation), dt);
			}
		}

		void PullDynamicTransforms()
		{
			for (auto [handle, rigidBody] : SceneRef.GetRegistry().view<RigidBodyComponent>().each())
			{
				if (rigidBody.Type != RigidBodyType::Dynamic)
					continue;
				Entity entity(handle, &SceneRef);
				const auto it = Bodies.find(entity.GetUUID());
				if (it == Bodies.end() || !Bodies_().IsActive(it->second))
					continue;
				JPH::RVec3 position;
				JPH::Quat rotation;
				Bodies_().GetPositionAndRotation(it->second, position, rotation);
				const glm::vec3 scale = entity.GetComponent<TransformComponent>().Scale;
				SceneRef.SetWorldTransform(entity,
					Math::ComposeTransform(glm::vec3(position.GetX(), position.GetY(), position.GetZ()), FromJolt(rotation), scale));
			}
		}

		std::optional<JPH::BodyID> BodyOf(Entity entity) const
		{
			if (!entity.IsValid())
				return std::nullopt;
			const auto it = Bodies.find(entity.GetUUID());
			if (it == Bodies.end())
				return std::nullopt;
			return it->second;
		}
	};

	PhysicsWorld::PhysicsWorld(Scene& scene)
		: m_Impl(CreateScope<Impl>(scene))
	{
		AcquireJolt();

		constexpr uint32_t MaxBodies = 65536;
		constexpr uint32_t BodyMutexes = 0;
		constexpr uint32_t MaxBodyPairs = 65536;
		constexpr uint32_t MaxContactConstraints = 16384;

		m_Impl->TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
		const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
		m_Impl->JobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
		m_Impl->System = std::make_unique<JPH::PhysicsSystem>();
		m_Impl->System->Init(MaxBodies, BodyMutexes, MaxBodyPairs, MaxContactConstraints, m_Impl->BroadPhaseLayers, m_Impl->ObjectVsBroadPhase,
			m_Impl->ObjectPairs);
		m_Impl->System->SetContactListener(&m_Impl->Contacts);
	}

	PhysicsWorld::~PhysicsWorld()
	{
		Stop();
		m_Impl->System.reset();
		m_Impl->JobSystem.reset();
		m_Impl->TempAllocator.reset();
		m_Impl.reset();
		ReleaseJolt();
	}

	void PhysicsWorld::Start()
	{
		m_Impl->Started = true;
		m_Impl->CreateMissing();
		m_Impl->System->OptimizeBroadPhase();
	}

	void PhysicsWorld::Stop()
	{
		if (!m_Impl || !m_Impl->Started)
			return;
		std::vector<UUID> all;
		for (const auto& [uuid, body] : m_Impl->Bodies)
			all.push_back(uuid);
		for (const auto& [uuid, character] : m_Impl->Characters)
			all.push_back(uuid);
		for (UUID uuid : all)
			m_Impl->RemoveEntity(uuid);
		m_Impl->Started = false;
	}

	void PhysicsWorld::Step(Timestep ts)
	{
		m_Impl->CreateMissing();
		m_Impl->Accumulator = std::min(m_Impl->Accumulator + ts.GetSeconds(), FixedTimestep * MaxStepsPerFrame);
		while (m_Impl->Accumulator >= FixedTimestep)
		{
			m_Impl->PushKinematicTargets(FixedTimestep);
			m_Impl->UpdateCharacters(FixedTimestep);
			m_Impl->System->Update(FixedTimestep, 1, m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
			m_Impl->Accumulator -= FixedTimestep;
		}
		m_Impl->PullDynamicTransforms();
	}

	void PhysicsWorld::OnEntityDestroyed(Entity entity) { m_Impl->RemoveEntity(entity.GetUUID()); }

	void PhysicsWorld::RemoveCharacter(Entity entity)
	{
		const UUID uuid = entity.GetUUID();
		if (const auto it = m_Impl->Characters.find(uuid); it != m_Impl->Characters.end())
		{
			m_Impl->CharacterCollisions.Remove(it->second);
			m_Impl->Characters.erase(it);
			m_Impl->CharacterHeights.erase(uuid);
		}
	}

	bool PhysicsWorld::HasCharacter(UUID entity) const { return m_Impl->Characters.contains(entity); }

	std::optional<RaycastHit> PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
		const PhysicsEntityFilter& filter) const
	{
		const float length = glm::length(direction);
		if (length < Math::Epsilon || maxDistance <= 0.0f)
			return std::nullopt;
		const glm::vec3 unit = direction / length;

		const JPH::RRayCast ray(JPH::RVec3(ToJolt(origin)), ToJolt(unit * maxDistance));
		JPH::RayCastSettings settings;
		settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
		JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
		const EntityBodyFilter bodyFilter(filter);
		m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, {}, bodyFilter);
		if (!collector.HadHit())
			return std::nullopt;

		RaycastHit hit;
		hit.Distance = collector.mHit.mFraction * maxDistance;
		hit.Point = origin + unit * hit.Distance;
		JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), collector.mHit.mBodyID);
		if (lock.Succeeded())
		{
			const JPH::Body& body = lock.GetBody();
			hit.Entity = UUID(body.GetUserData());
			hit.Normal = FromJolt(body.GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, JPH::RVec3(ToJolt(hit.Point))));
		}
		return hit;
	}

	std::vector<UUID> PhysicsWorld::OverlapSphere(const glm::vec3& center, float radius) const
	{
		std::vector<UUID> result;
		if (radius <= 0.0f)
			return result;
		JPH::SphereShape sphere(radius);
		sphere.SetEmbedded();
		JPH::CollideShapeSettings settings;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		m_Impl->System->GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(JPH::RVec3(ToJolt(center))),
			settings, JPH::RVec3::sZero(), collector);

		std::unordered_set<uint64_t> seen;
		for (const JPH::CollideShapeResult& hit : collector.mHits)
		{
			const uint64_t userData = m_Impl->System->GetBodyInterface().GetUserData(hit.mBodyID2);
			if (userData != 0 && seen.insert(userData).second)
				result.emplace_back(userData);
		}
		return result;
	}

	void PhysicsWorld::CollectStaticTriangles(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices) const
	{
		constexpr int BatchSize = 64; // >= JPH::Shape::cGetTrianglesMinTrianglesRequested
		std::array<JPH::Float3, BatchSize * 3> batch;
		for (const auto& [uuid, bodyID] : m_Impl->Bodies)
		{
			JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), bodyID);
			if (!lock.Succeeded())
				continue;
			const JPH::Body& body = lock.GetBody();
			if (!body.IsStatic() || body.IsSensor())
				continue;
			// Decorated (offset) and compound shapes cannot list triangles themselves: split the body
			// into its leaf shapes (boxes, spheres, capsules, meshes) with their world transforms.
			JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
			body.GetTransformedShape().CollectTransformedShapes(JPH::AABox::sBiggest(), leaves);
			for (const JPH::TransformedShape& shape : leaves.mHits)
			{
				JPH::Shape::GetTrianglesContext context;
				shape.GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::RVec3::sZero());
				for (;;)
				{
					const int count = shape.GetTrianglesNext(context, BatchSize, batch.data());
					if (count <= 0)
						break;
					for (int i = 0; i < count * 3; ++i)
					{
						const JPH::Float3& vertex = batch[static_cast<size_t>(i)];
						indices.push_back(static_cast<uint32_t>(vertices.size()));
						vertices.emplace_back(vertex.x, vertex.y, vertex.z);
					}
				}
			}
		}
	}

	void PhysicsWorld::AddImpulse(Entity entity, const glm::vec3& impulse)
	{
		if (const auto body = m_Impl->BodyOf(entity))
			m_Impl->Bodies_().AddImpulse(*body, ToJolt(impulse));
	}

	void PhysicsWorld::AddForce(Entity entity, const glm::vec3& force)
	{
		if (const auto body = m_Impl->BodyOf(entity))
			m_Impl->Bodies_().AddForce(*body, ToJolt(force));
	}

	void PhysicsWorld::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
	{
		if (const auto body = m_Impl->BodyOf(entity))
		{
			m_Impl->Bodies_().SetLinearVelocity(*body, ToJolt(velocity));
			return;
		}
		if (entity.IsValid())
		{
			if (const auto it = m_Impl->Characters.find(entity.GetUUID()); it != m_Impl->Characters.end())
				it->second->SetLinearVelocity(ToJolt(velocity));
		}
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
	{
		if (const auto body = m_Impl->BodyOf(entity))
			return FromJolt(m_Impl->Bodies_().GetLinearVelocity(*body));
		if (entity.IsValid())
		{
			if (const auto it = m_Impl->Characters.find(entity.GetUUID()); it != m_Impl->Characters.end())
				return FromJolt(it->second->GetLinearVelocity());
		}
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::Teleport(Entity entity, const glm::vec3& position, const glm::quat& rotation)
	{
		if (const auto body = m_Impl->BodyOf(entity))
			m_Impl->Bodies_().SetPositionAndRotation(*body, JPH::RVec3(ToJolt(position)), ToJolt(rotation), JPH::EActivation::Activate);
		if (entity.IsValid())
		{
			if (const auto it = m_Impl->Characters.find(entity.GetUUID()); it != m_Impl->Characters.end())
			{
				it->second->SetPosition(JPH::RVec3(ToJolt(position)));
				it->second->SetRotation(ToJolt(rotation));
			}
		}
	}

	void PhysicsWorld::AddRadialImpulse(const glm::vec3& center, float radius, float strength)
	{
		for (UUID uuid : OverlapSphere(center, radius))
		{
			const auto it = m_Impl->Bodies.find(uuid);
			if (it == m_Impl->Bodies.end() || m_Impl->Bodies_().GetMotionType(it->second) != JPH::EMotionType::Dynamic)
				continue;
			const glm::vec3 position = FromJolt(m_Impl->Bodies_().GetCenterOfMassPosition(it->second));
			glm::vec3 offset = position - center;
			const float distance = glm::length(offset);
			offset = distance > Math::Epsilon ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f);
			const float falloff = 1.0f - std::clamp(distance / radius, 0.0f, 1.0f);
			m_Impl->Bodies_().AddImpulse(it->second, ToJolt(offset * strength * falloff));
		}
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity) { m_Impl->System->SetGravity(ToJolt(gravity)); }
	glm::vec3 PhysicsWorld::GetGravity() const { return FromJolt(m_Impl->System->GetGravity()); }
	bool PhysicsWorld::HasBody(UUID entity) const { return m_Impl->Bodies.contains(entity) || m_Impl->Characters.contains(entity); }
	size_t PhysicsWorld::GetBodyCount() const { return m_Impl->Bodies.size() + m_Impl->Characters.size(); }
	std::vector<ContactEvent> PhysicsWorld::ConsumeContactEvents() { return m_Impl->Contacts.Consume(); }

}
