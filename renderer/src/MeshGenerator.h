#pragma once

#include "Mesh.h"

namespace MeshGenerator {

// 24 vertices (4 per face), 36 indices. One distinct color per face:
// +X=red, -X=green, +Y=blue, -Y=yellow, +Z=magenta, -Z=cyan.
MeshAsset GenerateCube();

// UV sphere. Vertex normals = normalised position (smooth shading).
// Face color alternates by (stack+slice) % 2: orange / off-white checkerboard.
MeshAsset GenerateSphere(int stacks = 12, int slices = 16);

// Square-base pyramid. Apex duplicated per side face for flat normals.
// 4 side triangles (distinct colors) + 1 quad base (gray).
MeshAsset GeneratePyramid();

// Capped cylinder. 3 color regions: red top cap, green sides, blue bottom cap.
MeshAsset GenerateCylinder(int segments = 16);

// Capped cone. Apex duplicated per side triangle. Orange sides, blue base cap.
MeshAsset GenerateCone(int segments = 16);

} // namespace MeshGenerator
