#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <memory>
#include <string_view>

namespace Penumbra::Primitives {

	// Unit-sized primitives centred on the origin:
	//   cube      1 x 1 x 1
	//   sphere    diameter 1
	//   plane     1 x 1 in XZ, facing +Y
	//   capsule   radius 0.5, total height 2 (Y axis)
	//   cylinder  radius 0.5, height 1 (Y axis)
	MeshData CreateCube();
	MeshData CreateSphere(uint32_t segments = 32, uint32_t rings = 16);
	MeshData CreatePlane();
	MeshData CreateCapsule(uint32_t segments = 24, uint32_t rings = 8);
	MeshData CreateCylinder(uint32_t segments = 32);

	constexpr std::string_view BuiltinPrefix = "builtin:";

	// Returns nullptr for unknown names. Instances are created once and shared.
	std::shared_ptr<const MeshData> GetBuiltin(std::string_view name);

}
