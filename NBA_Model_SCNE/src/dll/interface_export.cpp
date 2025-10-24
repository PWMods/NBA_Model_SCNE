#include <dll/interface_export.h>
#include <cereal/sceneserializer.h>
#include <material/material.h>

void freeMesh(void* pMesh)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	delete mesh;
}

void setMeshVertexComponents(void* pMesh, int numComponents)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	mesh->vertexComponents = numComponents;
	printf("\n[setMeshVertexComponents] Set mesh '%s' to %d-component vertices",
		mesh->name.c_str(), numComponents);
}

void* getNewSkinMesh(const char* name)
{
	return new Mesh{ name };
}

void* getNewSkinModel(const char* name)
{
	return new CNBAModel(name);
}

void* getNewSceneObj(const char* name)
{
	return new CNBAScene(name);
}

void setMeshNameInfo(void* pMesh, const char* meshName, const char* mtlName)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	mesh->name = meshName;
	mesh->material.setName(mtlName);
	return;
}

void setMeshData(void* pMesh, float* position, int* indexList, int numVerts, int numFaces)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	// ⭐ CRITICAL FIX: Check if mesh needs 4-component vertices
	// Blender always sends 3-component (XYZ), but some meshes need 4-component (XYZW)
	bool needs4Components = (mesh->vertexComponents == 4);

	if (needs4Components)
	{
		printf("\n[setMeshData] Converting 3-component input to 4-component for mesh '%s'", mesh->name.c_str());

		// Blender sends XYZ, we need XYZW
		int numVertices = numVerts / 3;  // numVerts is total floats (e.g., 999 = 333 verts * 3)
		mesh->vertices.resize(numVertices * 4);  // Allocate for XYZW

		for (int i = 0; i < numVertices; i++)
		{
			int srcIdx = i * 3;  // Source: XYZ
			int dstIdx = i * 4;  // Destination: XYZW

			mesh->vertices[dstIdx + 0] = position[srcIdx + 0];  // X
			mesh->vertices[dstIdx + 1] = position[srcIdx + 1];  // Y
			mesh->vertices[dstIdx + 2] = position[srcIdx + 2];  // Z
			mesh->vertices[dstIdx + 3] = 1.0f;                  // W = 1.0
		}

		printf("\n[setMeshData] Converted %d vertices from 3-component to 4-component", numVertices);
	}
	else
	{
		// Standard 3-component vertices (XYZ only)
		mesh->vertices.resize(numVerts);
		for (int i = 0; i < numVerts; i++)
		{
			mesh->vertices[i] = position[i];
		}
	}

	/* Set triangle indices */
	numFaces /= 3;
	mesh->triangles.resize(numFaces);
	for (int i = 0; i < numFaces; i++)
	{
		Triangle& face = mesh->triangles.at(i);
		size_t index = (i * 3);

		face[0] = indexList[index];
		face[1] = indexList[index + 1];
		face[2] = indexList[index + 2];
	}

	/* Update counts - use correct component count for alignment */
	mesh->alignPosition(true, mesh->vertexComponents);
	mesh->generateAABBs();

	printf("\n[setMeshData] Final mesh has %d vertices (%d components each)",
		mesh->vertices.size() / mesh->vertexComponents, mesh->vertexComponents);
}

void setMeshNormals(void* pMesh, float* normals, int size)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	// Calculate expected normal count based on vertex components
	int numVertices = mesh->vertices.size() / mesh->vertexComponents;
	int expectedNormalFloats = numVertices * 3;  // Normals are always 3-component (XYZ)

	if (expectedNormalFloats != size)
	{
		printf("\n[setMeshNormals] WARNING: Normal count mismatch. Expected %d, got %d",
			expectedNormalFloats, size);
		printf("\n[setMeshNormals] Mesh has %d vertices (%d components each)",
			numVertices, mesh->vertexComponents);
	}

	/* Populate vertex normals */
	mesh->normals.resize(size);
	for (int i = 0; i < size; i++)
		mesh->normals[i] = normals[i];

	mesh->alignNormals(true, 3);  // Normals are always 3-component
}

void addUvMap(void* pMesh, float* texcoords, int size)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	/* Create a new uv channel */
	UVMap channel;
	channel.map.resize(size);
	for (int i = 0; i < size; i += 2) {
		channel.map.at(i) = texcoords[i];
		channel.map.at(i + 1) = -(-1.0 + texcoords[i + 1]);
	}

	mesh->uvs.push_back(channel);

	// Pack tangent frames
	MeshCalc::calculateTangentsBinormals(*mesh);
	MeshCalc::buildTangentFrameVec(*mesh, mesh->tangent_frames);
}

void saveModelToFile(void* pModel, const char* savePath)
{
	// todo: add cereal implementation ...
	auto model = static_cast<CNBAModel*>(pModel);
	if (!model || (model->getNumMeshes() == 0)) return;

	// System logs ...
	printf("\n[CSkinModel] Saving model to file: %s\n", savePath);

	auto scene_id = model->getMesh()->name;
	auto scene = std::make_shared<CNBAScene>(scene_id.c_str());
	scene->pushModel(*model);

	CSceneSerializer serializer(scene);
	serializer.save(savePath);
}

void linkMeshToModel(void* pModel, void* pMesh)
{
	// Convert void pointer back to CNBAModel pointer
	CNBAModel* model = static_cast<CNBAModel*>(pModel);
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	model->pushMesh(*mesh);

	// release mesh
	::freeMesh(mesh);
	return;
}

void setMeshMaterial(void* pMesh, const char* name)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	mesh->material.setName(name);
}

void setMaterialTexture(void* pMesh, const char* name, const char* type, const int width, const int height, const int size, float* pixmap)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	// Create a new texture map
	auto& material = mesh->material;
	auto  texture = std::make_shared<CNSTexture>(name, width, height);

	// Set texture pixmap
	texture->setPixmap(pixmap, size);
	texture->setType(type);
	material.addTexture(texture);
}

void setNewModelBone(
	void* pModel,
	const char* name,
	float* matrices,
	const int index,
	const char* parent)
{
	// Convert void pointer back to CNBAModel pointer
	CNBAModel* model = static_cast<CNBAModel*>(pModel);
	if (!model) return;

	// Create a new bone
	std::string parent_id(parent);
	auto& skeleton = model->getSkeleton();
	auto bone = std::make_shared<NSJoint>(NSJoint{ index, name });
	bone->translate = Vec3{ matrices[0], matrices[2], -matrices[1] };

	if (!parent_id.empty())
	{
		auto joint = skeleton.findJoint(parent);
		if (joint)
		{
			bone->parent = joint;
			joint->children.push_back(bone);
		}
	}

	skeleton.addJoint(bone);
};

inline static void normalize_data_16_bits(float* data, const int size)
{
	for (int i = 0; i < size; i++)
	{
		// normalize weights to 16 bit scalar
		data[i] = (uint16_t(data[i] * 65535.0f)) / 65535.0f;
	}
}

void setMeshSkinData(void* pMesh, int* indices, float* weights, int size, int num_weights)
{
	Mesh* mesh = static_cast<Mesh*>(pMesh);
	if (!mesh) return;

	int num_verts = size / num_weights;
	int arr_index;
	mesh->skin.blendverts.resize(num_verts);

	::normalize_data_16_bits(weights, size);

	for (int i = 0; i < num_verts; i++)
	{
		auto& vertex = mesh->skin.blendverts[i];

		for (int j = 0; j < num_weights; j++)
		{
			arr_index = (i * num_weights) + j;
			auto& index = indices[arr_index];
			auto& weight = weights[arr_index];

			if (weight > 0.0f)
			{
				vertex.indices.push_back(index);
				vertex.weights.push_back(weight);
			}
		}
	};
};