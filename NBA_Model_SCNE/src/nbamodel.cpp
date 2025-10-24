#include <nbamodel.h>

CNBAModel::CNBAModel(const char* id)
	:
	m_name(id),
	m_weightBits(16),
	m_worldPosition({ 0.0f, 0.0f, 0.0f }),
	m_boundingMin({ 0.0f, 0.0f, 0.0f }),
	m_boundingMax({ 0.0f, 0.0f, 0.0f }),
	m_radius(0.0f)
{
}

CNBAModel::~CNBAModel()
{
	m_meshes.clear();
}

int CNBAModel::getNumMeshes()
{
	return m_meshes.size();
}

Mesh* CNBAModel::getMesh(int index)
{
	if (index >= m_meshes.size())
		return nullptr;
	return m_meshes[index].get();
}

std::vector<std::shared_ptr<Mesh>> CNBAModel::getMeshes()
{
	return m_meshes;
}

std::string CNBAModel::name()
{
	return m_name;
}

bool CNBAModel::hasSkeleton()
{
	return !m_skeleton.joints.empty();
}

void CNBAModel::pushMesh(const Mesh& mesh)
{
	m_meshes.push_back(std::make_shared<Mesh>(mesh));
}

NSSkeleton& CNBAModel::getSkeleton()
{
	return m_skeleton;
}

int CNBAModel::getNumVerts(int meshIndex) const
{
	if (meshIndex >= m_meshes.size())
		return 0;

	auto mesh = m_meshes[meshIndex];
	if (!mesh)
		return 0;

	int numComponents = getVertexComponents(meshIndex);
	return mesh->vertices.size() / numComponents;
}

int CNBAModel::getVertexComponents(int meshIndex) const
{
	if (meshIndex >= m_meshes.size())
		return 3; // default

	auto mesh = m_meshes[meshIndex];
	if (!mesh)
		return 3;

	// If the mesh has stored its vertex component count, use that
	if (mesh->vertexComponents > 0)
	{
		return mesh->vertexComponents;
	}

	// Otherwise, try to detect from the data
	int totalFloats = mesh->vertices.size();

	// If we have triangle data, we can verify the component count
	if (!mesh->triangles.empty())
	{
		// Find the maximum vertex index referenced in triangles
		int maxIndex = 0;
		for (const auto& tri : mesh->triangles)
		{
			// Check each index in the triangle
			int idx0 = static_cast<int>(tri[0]);
			int idx1 = static_cast<int>(tri[1]);
			int idx2 = static_cast<int>(tri[2]);

			if (idx0 > maxIndex) maxIndex = idx0;
			if (idx1 > maxIndex) maxIndex = idx1;
			if (idx2 > maxIndex) maxIndex = idx2;
		}
		int expectedVerts = maxIndex + 1;

		// Check if dividing by 4 gives us the expected vertex count
		if (totalFloats % 4 == 0 && totalFloats / 4 == expectedVerts)
		{
			printf("\n[CNBAModel] Detected 4-component vertices for mesh '%s' (totalFloats=%d, expectedVerts=%d)",
				mesh->name.c_str(), totalFloats, expectedVerts);
			return 4;
		}

		// Check if dividing by 3 gives us the expected vertex count
		if (totalFloats % 3 == 0 && totalFloats / 3 == expectedVerts)
		{
			printf("\n[CNBAModel] Detected 3-component vertices for mesh '%s' (totalFloats=%d, expectedVerts=%d)",
				mesh->name.c_str(), totalFloats, expectedVerts);
			return 3;
		}
	}

	// Fallback: check if divisible by 4 and seems reasonable
	if (totalFloats % 4 == 0 && totalFloats >= 4)
	{
		// Additional heuristic: 4-component vertices are more common in certain mesh types
		// Check if the 4th component looks like a W coordinate (should be close to 1.0 or 0.0)
		bool looks_like_w_component = true;
		int numPotentialVerts = totalFloats / 4;
		int sampleCount = (numPotentialVerts < 10) ? numPotentialVerts : 10; // Sample first 10 vertices

		for (int i = 0; i < sampleCount; i++)
		{
			float w = mesh->vertices[i * 4 + 3];
			// W component is typically 1.0 for positions, or close to 0.0
			if (std::abs(w) > 2.0f || (std::abs(w) < 0.5f && std::abs(w) > 0.1f))
			{
				looks_like_w_component = false;
				break;
			}
		}

		if (looks_like_w_component)
		{
			printf("\n[CNBAModel] Heuristic detected 4-component vertices for mesh '%s'", mesh->name.c_str());
			return 4;
		}
	}

	// Default to 3-component
	printf("\n[CNBAModel] Defaulting to 3-component vertices for mesh '%s'", mesh->name.c_str());
	return 3;
}

void CNBAModel::setVertexComponents(int meshIndex, int components)
{
	if (meshIndex >= m_meshes.size())
		return;

	auto mesh = m_meshes[meshIndex];
	if (mesh)
	{
		mesh->vertexComponents = components;
		printf("\n[CNBAModel] Set mesh '%s' to use %d-component vertices", mesh->name.c_str(), components);
	}
}