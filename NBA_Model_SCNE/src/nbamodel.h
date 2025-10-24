#pragma once
#include <meshprimitive.h>
#include <armature/armature.h>
#include <databuffer.h>

class CNBAModel
{
public:
	CNBAModel(const char* id);
	~CNBAModel();

public:
	// Existing mesh methods
	int getNumMeshes();
	Mesh* getMesh(int index = 0);
	std::vector<std::shared_ptr<Mesh>> getMeshes();
	std::string name();

public:
	// Skeleton methods
	bool hasSkeleton();
	void pushMesh(const Mesh& mesh);
	NSSkeleton& getSkeleton();

	// World position and bounding box methods
	void setWorldPosition(const Vec3& pos) { m_worldPosition = pos; }
	Vec3 getWorldPosition() const { return m_worldPosition; }
	void setBoundingBox(const Vec3& min, const Vec3& max) { m_boundingMin = min; m_boundingMax = max; }
	Vec3 getBoundingMin() const { return m_boundingMin; }
	Vec3 getBoundingMax() const { return m_boundingMax; }
	void setRadius(float r) { m_radius = r; }
	float getRadius() const { return m_radius; }

	// NEW: Vertex component detection and handling methods
	// These methods fix the issue where 4-component vertices (XYZW) were being
	// processed with 3-component stride, causing mesh explosions

	/**
	 * Get the number of vertices for a specific mesh
	 * Correctly handles both 3-component (XYZ) and 4-component (XYZW) vertex formats
	 * @param meshIndex Index of the mesh
	 * @return Number of vertices in the mesh
	 */
	int getNumVerts(int meshIndex) const;

	/**
	 * Detect the number of components per vertex for a specific mesh
	 * Uses multiple detection strategies:
	 * 1. Stored vertexComponents value if available
	 * 2. Triangle data validation
	 * 3. Heuristic W-component analysis
	 *
	 * @param meshIndex Index of the mesh
	 * @return Number of components (3 for XYZ, 4 for XYZW)
	 */
	int getVertexComponents(int meshIndex) const;

	/**
	 * Manually set the vertex component count for a mesh
	 * Use this when loading mesh data if you know the format from CDataBuffer
	 *
	 * Example usage:
	 *   std::string format = vertexBuffer->getFormat();
	 *   if (format.find("R32G32B32A32") != std::string::npos) {
	 *       model.setVertexComponents(meshIdx, 4);
	 *   } else {
	 *       model.setVertexComponents(meshIdx, 3);
	 *   }
	 *
	 * @param meshIndex Index of the mesh
	 * @param components Number of components (3 or 4)
	 */
	void setVertexComponents(int meshIndex, int components);

protected:
	std::string m_name;
	NSSkeleton m_skeleton;
	std::vector<std::shared_ptr<Mesh>> m_meshes;
	std::vector<StGeoPrim> m_primitives;
	std::vector<Array2D> g_uvDeriv;
	int m_weightBits;

	// Transform data
	Vec3 m_worldPosition = { 0.0f, 0.0f, 0.0f };
	Vec3 m_boundingMin = { 0.0f, 0.0f, 0.0f };
	Vec3 m_boundingMax = { 0.0f, 0.0f, 0.0f };
	float m_radius = 0.0f;
};