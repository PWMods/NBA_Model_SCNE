/* Stores special model attributes and child Meshes for model object.
   Extrapolates mesh data from given JSON container. */

#include <nbamodel.h>
#include <fstream>
#include <istream>
#pragma once

class CSceneFile;
using JSON = nlohmann::ordered_json;

class CModelReader : public CNBAModel
{
public:
	CModelReader(const char* id, JSON& data);
	~CModelReader();

	void parse();

private:
	inline void splitMeshGroups();

private:
	void loadMeshData();
	void loadWeights(Mesh& mesh);
	void loadVertices(Mesh& mesh);
	void loadIndices(Mesh& mesh, const int count, uintptr_t& offset);
	void loadMesh();
	void readMorphs(JSON& obj);
	void readTfms(JSON& obj);
	void readPrim(JSON& obj);
	void readVertexFmt(JSON& obj);
	void readVertexStream(JSON& obj);
	void readIndexBuffer(JSON& obj);
	void readMtxWeightBuffer(JSON& obj);
	void decodeOctahedralNormals(CDataBuffer* tanBf, Mesh& mesh);

	// ✓ NEW: Split index buffer methods
	void readNormalIndexBuffer(JSON& obj);
	void readTangentIndexBuffer(JSON& obj);
	void expandSplitAttributes(Mesh& mesh);

private:
	CDataBuffer* findDataBuffer(const char* id);
	CDataBuffer* getVtxBuffer(int index);

private:
	JSON m_json;
	std::vector<CDataBuffer> m_vtxBfs;
	std::vector<CDataBuffer> m_dataBfs;
	CSceneFile* m_parent;
};



