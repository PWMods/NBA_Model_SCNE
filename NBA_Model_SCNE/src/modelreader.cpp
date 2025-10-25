#include <modelreader.h>
#include <scenefile.h>
#include <common.h>
#include <armature/bone_reader.h>
#include <cmath>

CModelReader::CModelReader(const char* id, JSON& data)
	:
	CNBAModel(id),
	m_json(data),
	m_parent(NULL)
{
}

CModelReader::~CModelReader()
{
}

void CModelReader::parse()
{
	printf("\n[CModelReader::parse] Starting parse...");

	for (JSON::iterator it = m_json.begin(); it != m_json.end(); ++it)
	{
		auto key = common::chash(it.key());
		std::string keyStr = it.key();

		printf("\n[parse] Processing key: %s", keyStr.c_str());

		// Check for split index buffers first (using string comparison)
		if (keyStr == "NormalIndexBuffer") {
			printf("\n[parse] Found NormalIndexBuffer");
			readNormalIndexBuffer(it.value());
			continue;
		}
		if (keyStr == "TangentIndexBuffer") {
			printf("\n[parse] Found TangentIndexBuffer");
			readTangentIndexBuffer(it.value());
			continue;
		}

		// Use switch for the rest
		switch (key)
		{
		case enModelData::MORPH:
			readMorphs(it.value());
			break;
		case enModelData::WEIGHTBITS:
			m_weightBits = it.value();
			break;
		case enModelData::TRANSFORM:
			readTfms(it.value());
			break;
		case enModelData::PRIM:
			printf("\n[parse] Processing PRIM");
			readPrim(it.value());
			break;
		case enModelData::INDEXBUFFER:
			printf("\n[parse] Processing IndexBuffer");
			readIndexBuffer(it.value());
			break;
		case enModelData::MATRIXWEIGHTBUFFER:
			readMtxWeightBuffer(it.value());
			break;
		case enModelData::VERTEXFORMAT:
			printf("\n[parse] Processing VertexFormat");
			readVertexFmt(it.value());
			break;
		case enModelData::VERTEXSTREAM:
			printf("\n[parse] Processing VertexStream");
			readVertexStream(it.value());
			printf("\n[parse] VertexStream complete");
			break;
		case enPrimTag::PM_DUV_0:
			g_uvDeriv.push_back(it.value());
			break;
		case enPrimTag::PM_DUV_1:
			g_uvDeriv.push_back(it.value());
			break;
		case enPrimTag::PM_DUV_2:
			g_uvDeriv.push_back(it.value());
			break;
		default:
			printf("\n[parse] Unknown key: %s (hash: %llu)", keyStr.c_str(), key);
			break;
		};
	}

	printf("\n[parse] All keys processed, calling loadMeshData...");
	this->loadMeshData();
	printf("\n[parse] Parse complete");
}

inline static void trisFromMeshGroup(std::shared_ptr<Mesh>& fullMesh, std::shared_ptr<Mesh>& splitMsh, const FaceGroup& group)
{
	splitMsh->triangles.clear();

	// update index list
	const int numTris = fullMesh->triangles.size();
	size_t indexBegin = (group.begin) / 3;
	size_t indexEnd = (group.begin + group.count) / 3;

	for (size_t i = indexBegin; i < indexEnd; i++)
		if (i <= numTris)
		{
			Triangle& tri = fullMesh->triangles[i];
			splitMsh->triangles.push_back(tri);
		}
};

void CModelReader::splitMeshGroups()
{
	if (m_meshes.empty())
		return;

	// split mesh using groups ...
	auto fullMesh = m_meshes.front();
	for (auto& group : fullMesh->groups)
	{
		auto splitMsh = std::make_shared<Mesh>(*fullMesh);
		::trisFromMeshGroup(fullMesh, splitMsh, group);

		splitMsh->name = group.name;
		m_meshes.push_back(splitMsh);
	}

	m_meshes.erase(m_meshes.begin());
}

void CModelReader::loadMeshData()
{
	if (m_dataBfs.empty() || m_vtxBfs.empty() || m_primitives.empty())
		return;

	this->loadMesh();
	//this->splitMeshGroups();

	m_primitives.clear();
}

void CModelReader::loadMesh()
{
	// build full model mesh
	uintptr_t dataOffset = NULL;
	int  beginIdx = 0;
	auto mesh = std::make_shared<Mesh>();

	for (auto& prim : m_primitives)
	{
		FaceGroup group;

		dataOffset = (prim.data_begin < 0) ? dataOffset : prim.data_begin;
		group.name = prim.name;
		group.begin = beginIdx;
		group.count = prim.count;
		group.material.setName(prim.material_name.c_str());
		mesh->groups.push_back(group);

		loadIndices(*mesh, prim.count, dataOffset);
		beginIdx += prim.count;
	}

	loadVertices(*mesh);
	loadWeights(*mesh);
	m_meshes.push_back(mesh);
}

void CModelReader::loadVertices(Mesh& mesh)
{
	printf("\n[loadVertices] Starting...");

	// Debug: Print all available buffers
	printf("\n[loadVertices] === ALL AVAILABLE BUFFERS ===");
	printf("\n[loadVertices] Data buffers: %zu", m_dataBfs.size());
	for (auto& buf : m_dataBfs) {
		printf("\n  - %s", buf.id.c_str());
	}
	printf("\n[loadVertices] Vertex buffers: %zu", m_vtxBfs.size());
	for (auto& buf : m_vtxBfs) {
		printf("\n  - %s (stream %d)", buf.id.c_str(), buf.getStreamIdx());
	}
	printf("\n[loadVertices] =====================================");

	auto posBf = findDataBuffer("POSITION0");
	auto tanBf = findDataBuffer("TANGENTFRAME0");
	auto texBf = findDataBuffer("TEXCOORD0");

	if (!posBf) {
		printf("\n[loadVertices] ERROR: No POSITION0 buffer found!");
		return;
	}

	printf("\n[loadVertices] Found buffers: posBf=%p, tanBf=%p, texBf=%p", posBf, tanBf, texBf);

	// Build mesh geometry
	mesh.name = (mesh.name.empty()) ? m_name : mesh.name;
	GeomDef::setMeshVtxs(posBf, mesh);

	// Determine vertex component count
	if (posBf->getFormat() == "R16G16B16A16_SNORM") {
		mesh.vertexComponents = 4;
	}
	else {
		mesh.vertexComponents = 3;
	}

	printf("\n[loadVertices] Vertex components: %d", mesh.vertexComponents);

	// Check for split index buffers (jerseys/cloth)
	auto normalIdxBf = findDataBuffer("NormalIndexBuffer");
	auto tangentIdxBf = findDataBuffer("TangentIndexBuffer");

	printf("\n[loadVertices] Split index buffers: normalIdxBf=%p, tangentIdxBf=%p", normalIdxBf, tangentIdxBf);

	// ✓ For jerseys: Find where the unique normal/tangent data is stored
	CDataBuffer* normalDataBf = nullptr;

	if (normalIdxBf && tangentIdxBf) {
		// Jersey uses split indices - find the actual tangent/normal data
		// Priority: BINORMAL0 > TANGENT0 > TANGENTFRAME0 > NORMAL0

		normalDataBf = findDataBuffer("BINORMAL0");
		if (normalDataBf) {
			printf("\n[loadVertices] Found normal data in BINORMAL0");
		}

		if (!normalDataBf) {
			normalDataBf = findDataBuffer("TANGENT0");
			if (normalDataBf) {
				printf("\n[loadVertices] Found normal data in TANGENT0");
			}
		}

		if (!normalDataBf) {
			normalDataBf = findDataBuffer("TANGENTFRAME0");
			if (normalDataBf) {
				printf("\n[loadVertices] Found normal data in TANGENTFRAME0");
			}
		}

		if (!normalDataBf) {
			normalDataBf = findDataBuffer("NORMAL0");
			if (normalDataBf) {
				printf("\n[loadVertices] Found normal data in NORMAL0");
			}
		}
	}

	// Process normals based on what we found
	if (normalIdxBf && tangentIdxBf && normalDataBf) {
		// Split index format - expand to per-vertex for Blender
		printf("\n[loadVertices] Using SPLIT INDEX format");
		mesh.hasSplitIndices = true;
		mesh.normalIndexRef = normalIdxBf;
		mesh.tangentIndexRef = tangentIdxBf;
		mesh.normals_ref = normalDataBf;

		printf("\n[loadVertices] Calling expandSplitAttributes...");
		expandSplitAttributes(mesh);
		printf("\n[loadVertices] expandSplitAttributes completed");
	}
	else if (tanBf) {
		// Standard per-vertex format (characters)
		printf("\n[loadVertices] Using STANDARD per-vertex format");
		GeomDef::calculateVtxNormals(tanBf, mesh);
		mesh.normals_ref = tanBf;
	}
	else {
		printf("\n[loadVertices] WARNING: No normal data found!");
	}

	if (texBf && !texBf->data.empty()) {
		printf("\n[loadVertices] Adding UV map...");
		GeomDef::addMeshUVMap(texBf, mesh);
	}

	// Update mesh refs
	mesh.vertex_ref = posBf;
	mesh.texcoord_ref = texBf;

	// System logs
	if (USE_DEBUG_LOGS) {
		int numVerts = mesh.vertices.size() / mesh.vertexComponents;
		printf("\n[CModelReader] Built 3D Mesh: \"%s\" | Points: %d | Tris: %d | Components: %d | Split: %s",
			mesh.name.c_str(),
			numVerts,
			mesh.triangles.size(),
			mesh.vertexComponents,
			mesh.hasSplitIndices ? "YES" : "NO"
		);
	}

	printf("\n[loadVertices] Completed successfully");
}

void CModelReader::loadIndices(Mesh& mesh, const int count, uintptr_t& offset)
{
	auto triBf = findDataBuffer("IndexBuffer");
	int end = count + offset;

	if (!triBf || end > triBf->data.size() || count % 3 != 0)
		return;

	for (int i = offset; i < end; i += 3)
	{
		Triangle face
		{
			triBf->data[i],
			triBf->data[i + 1],
			triBf->data[i + 2]
		};

		mesh.triangles.push_back(face);
	}

	offset += count;
}

void CModelReader::readVertexFmt(JSON& obj)
{
	printf("\n[readVertexFmt] Processing vertex format with %zu entries", obj.size());

	for (JSON::iterator it = obj.begin(); it != obj.end(); ++it)
	{
		if (it.value().is_object())
		{
			CDataBuffer data;
			data.id = it.key();
			data.parse(it.value());
			m_vtxBfs.push_back(data);

			printf("\n[readVertexFmt] Added buffer: %s (stream %d)", data.id.c_str(), data.getStreamIdx());
		}
	}

	printf("\n[readVertexFmt] Total vertex buffers: %zu", m_vtxBfs.size());
}

CDataBuffer* CModelReader::getVtxBuffer(int index)
{
	if (index > m_vtxBfs.size())
		return nullptr;

	for (auto& vtxBf : m_vtxBfs)
	{
		if (vtxBf.getStreamIdx() == index)
			return &vtxBf;
	}

	return nullptr;
}

CDataBuffer* CModelReader::findDataBuffer(const char* target)
{
	printf("\n[findDataBuffer] Searching for: %s", target);

	for (auto& dataBf : m_dataBfs)
	{
		if (dataBf.id == target) {
			printf("\n[findDataBuffer] Found '%s' in m_dataBfs", target);
			return &dataBf;
		}
	}

	for (auto& vtxBf : m_vtxBfs)
	{
		if (vtxBf.id == target) {
			printf("\n[findDataBuffer] Found '%s' in m_vtxBfs", target);
			return &vtxBf;
		}
	}

	printf("\n[findDataBuffer] NOT FOUND: %s", target);
	return nullptr;
}

void CModelReader::readVertexStream(JSON& obj)
{
	int index = 0;

	printf("\n[readVertexStream] Processing %zu streams", obj.size());

	for (JSON::iterator it = obj.begin(); it != obj.end(); ++it)
	{
		printf("\n[readVertexStream] Stream %d", index);

		if (it.value().is_object())
		{
			for (auto& vtxBf : m_vtxBfs)
			{
				// Vertex buffers can sometimes share a stream binary
				if (vtxBf.getStreamIdx() == index)
				{
					printf("\n[readVertexStream] Loading stream %d for buffer %s", index, vtxBf.id.c_str());
					try {
						vtxBf.parse(it.value());
						vtxBf.loadBinary();
						printf("\n[readVertexStream] Stream %d loaded successfully", index);
					}
					catch (const std::exception& e) {
						printf("\n[readVertexStream] EXCEPTION loading stream %d: %s", index, e.what());
						throw;
					}
				}
			}

			index++;
		}
	}

	printf("\n[readVertexStream] All streams processed");
}

void CModelReader::readMtxWeightBuffer(JSON& obj)
{
	// find weight data stream
	CDataBuffer data;
	data.parse(obj);
	data.loadBinary();
	data.id = "MatrixWeightBuffer";

	m_dataBfs.push_back(data);
};

void CModelReader::readIndexBuffer(JSON& obj)
{
	CDataBuffer data;
	data.parse(obj);
	data.loadBinary();
	data.id = "IndexBuffer";

	m_dataBfs.push_back(data);
}

void CModelReader::readPrim(JSON& obj)
{
	for (JSON::iterator it = obj.begin(); it != obj.end(); ++it)
	{
		if (it.value().is_object())
		{
			StGeoPrim grp{ m_name };
			grp.load(it.value());

			// default to global transform if non specified
			grp.uv_deriv = (grp.uv_deriv.empty()) ? g_uvDeriv : grp.uv_deriv;

			// push lods
			GeomDef::pushPrimLods(grp, m_primitives);
		}
	}
}

void CModelReader::readTfms(JSON& obj)
{
	// Load bone data
	CBoneReader::fromJSON(obj, m_skeleton);
}

inline static float unpackWeight(uint16_t packedWeight) {
	return static_cast<float>(packedWeight) / 65535.0f;
}

inline static void loadPackedWeights(const uint32_t& index, const std::vector<float>& mtxData, const int num_weights, BlendVertex& skinVtx)
{
	uint32_t encodedValue, blendIdx, skinVal;
	skinVtx.weights.resize(num_weights);
	skinVtx.indices.resize(num_weights);

	// ✓ Add bounds check
	if (index >= mtxData.size()) {
		printf("\n[loadPackedWeights] ERROR: index %d out of range (size: %zu)", index, mtxData.size());
		// Fill with default weights
		for (int i = 0; i < num_weights; i++) {
			skinVtx.indices[i] = 0;
			skinVtx.weights[i] = 0.0f;
		}
		return;
	}

	for (int i = 0; i < num_weights; i++)
	{
		// ✓ Check each access
		if (index + i >= mtxData.size()) {
			printf("\n[loadPackedWeights] ERROR: index+i (%d) out of range (size: %zu)", index + i, mtxData.size());
			skinVtx.indices[i] = 0;
			skinVtx.weights[i] = 0.0f;
			continue;
		}

		encodedValue = mtxData[index + i];
		blendIdx = encodedValue >> 0x10;
		skinVal = encodedValue & 0xFFFF;
		skinVtx.indices[i] = blendIdx;
		skinVtx.weights[i] = ::unpackWeight(skinVal);
	}
}

inline static void loadMatrixBufferWeights(
	Mesh& mesh, const size_t& numVerts, const size_t& numElems, const CDataBuffer* weightBf, const CDataBuffer* matrixBf)
{
	printf("\n[loadMatrixBufferWeights] numVerts: %zu, matrixBf size: %zu, weightBf size: %zu",
		numVerts, matrixBf->data.size(), weightBf->data.size());

	uint32_t packedVtxSkin, numWeights, index;
	mesh.skin.blendverts.resize(numVerts);

	for (size_t i = 0; i < numVerts; i++)
	{
		auto& skinVtx = mesh.skin.blendverts[i];
		packedVtxSkin = weightBf->data[i];
		numWeights = packedVtxSkin & 0xFF;
		index = packedVtxSkin >> 0x8;

		if (numWeights == 0) {
			skinVtx.weights.push_back(1.0f);
			skinVtx.indices.push_back(index);
		}
		else {
			::loadPackedWeights(index, matrixBf->data, numWeights + 1, skinVtx);
		}
	}

	printf("\n[loadMatrixBufferWeights] Completed successfully");
}

void CModelReader::loadWeights(Mesh& mesh)
{
	// Retrieve weight buffers
	auto weightBf = findDataBuffer("WEIGHTDATA0");
	auto matrixBf = findDataBuffer("MatrixWeightBuffer");
	if (!weightBf || !matrixBf)
		return;

	// Get num total elements
	size_t numBfElems = matrixBf->data.size();
	size_t numVerts = mesh.vertices.size() / 3;
	if (numVerts > weightBf->data.size())
		return;

	// Load vertex skin
	::loadMatrixBufferWeights(mesh, numVerts, numBfElems, weightBf, matrixBf);

	// ✓ Skip updateIndices for now - causes crashes on some meshes
	printf("\n[loadWeights] Skipping updateIndices (known issue with static jerseys)");
	// mesh.skin.updateIndices(&m_skeleton);
}

void CModelReader::readMorphs(JSON& obj)
{
	// debug morph id's - collects and prints all debug morphs
	return;

	std::vector<std::string> debugMorphs;
	for (JSON::iterator it = obj.begin(); it != obj.end(); ++it)
	{
		debugMorphs.push_back(it.key());
	}

	printf("\ndebugMorphs = {");
	for (auto& morph : debugMorphs)
	{
		printf("\n\"%s\",", morph.c_str());
	}

	printf("};");
}

// ========================================
// NEW METHODS FOR SPLIT INDEX BUFFER SUPPORT
// ========================================

void CModelReader::readNormalIndexBuffer(JSON& obj)
{
	CDataBuffer data;
	data.parse(obj);
	data.loadBinary();
	data.id = "NormalIndexBuffer";
	m_dataBfs.push_back(data);

	if (USE_DEBUG_LOGS) {
		printf("\n[CModelReader] Loaded NormalIndexBuffer: %zu indices", data.data.size());
	}
}

void CModelReader::readTangentIndexBuffer(JSON& obj)
{
	CDataBuffer data;
	data.parse(obj);
	data.loadBinary();
	data.id = "TangentIndexBuffer";
	m_dataBfs.push_back(data);

	if (USE_DEBUG_LOGS) {
		printf("\n[CModelReader] Loaded TangentIndexBuffer: %zu indices", data.data.size());
	}
}

void CModelReader::expandSplitAttributes(Mesh& mesh)
{
	auto normalIdxBf = findDataBuffer("NormalIndexBuffer");
	auto tangentIdxBf = findDataBuffer("TangentIndexBuffer");
	auto tanBf = (CDataBuffer*)mesh.normals_ref;

	if (!normalIdxBf || !tangentIdxBf || !tanBf) {
		printf("\n[expandSplitAttributes] Missing required buffers, skipping");
		return;
	}

	printf("\n[expandSplitAttributes] Processing split index format");
	printf("\n  - Unique tangent frames: %zu", tanBf->data.size() / 3);
	printf("\n  - Normal indices: %zu (per-vertex)", normalIdxBf->data.size());
	printf("\n  - Vertices: %zu", mesh.vertices.size() / mesh.vertexComponents);

	mesh.uniqueTangents = tanBf->data;

	// ✓ Use the ORIGINAL function that worked!
	Mesh tempMesh;
	size_t numUniqueTangents = tanBf->data.size() / 3;

	tempMesh.vertices.resize(numUniqueTangents * 3);
	for (size_t i = 0; i < numUniqueTangents; i++) {
		tempMesh.vertices[i * 3 + 0] = 0.0f;
		tempMesh.vertices[i * 3 + 1] = 0.0f;
		tempMesh.vertices[i * 3 + 2] = 0.0f;
	}

	// Use original function instead of octahedral
	GeomDef::calculateVtxNormals(tanBf, tempMesh);
	mesh.uniqueNormals = tempMesh.normals;

	printf("\n[expandSplitAttributes] Decoded %zu unique normals", mesh.uniqueNormals.size() / 3);

	// Store indices
	mesh.normalIndices.clear();
	for (size_t i = 0; i < normalIdxBf->data.size(); i++) {
		mesh.normalIndices.push_back(static_cast<uint16_t>(normalIdxBf->data[i]));
	}

	mesh.tangentIndices.clear();
	for (size_t i = 0; i < tangentIdxBf->data.size(); i++) {
		mesh.tangentIndices.push_back(static_cast<uint16_t>(tangentIdxBf->data[i]));
	}

	// Assign per-vertex normals
	int numVerts = mesh.vertices.size() / mesh.vertexComponents;
	mesh.normals.clear();
	mesh.normals.resize(numVerts * 3, 0.0f);

	for (int vertIdx = 0; vertIdx < numVerts; vertIdx++)
	{
		if (vertIdx >= mesh.normalIndices.size()) continue;
		uint16_t normalIdx = mesh.normalIndices[vertIdx];
		if (normalIdx * 3 + 2 >= mesh.uniqueNormals.size()) continue;

		mesh.normals[vertIdx * 3 + 0] = mesh.uniqueNormals[normalIdx * 3 + 0];
		mesh.normals[vertIdx * 3 + 1] = mesh.uniqueNormals[normalIdx * 3 + 1];
		mesh.normals[vertIdx * 3 + 2] = mesh.uniqueNormals[normalIdx * 3 + 2];
	}

	printf("\n[expandSplitAttributes] Assigned normals to %d vertices", numVerts);
}

// Decode octahedral normals from R10G10B10 format
void CModelReader::decodeOctahedralNormals(CDataBuffer* tanBf, Mesh& mesh)
{
	if (!tanBf || tanBf->data.empty()) {
		printf("\n[decodeOctahedralNormals] No tangent data");
		return;
	}

	size_t numNormals = tanBf->data.size() / 3;
	mesh.normals.clear();
	mesh.normals.reserve(numNormals * 3);

	printf("\n[decodeOctahedralNormals] Decoding %zu normals from R10G10B10", numNormals);

	std::string format = tanBf->getFormat();
	bool isNormalized = (format.find("SNORM") != std::string::npos) ||
		(format.find("snorm") != std::string::npos);

	printf("\n[decodeOctahedralNormals] Format: %s, IsNormalized: %s",
		format.c_str(), isNormalized ? "YES" : "NO");

	// Debug: Print first few raw values
	printf("\n[decodeOctahedralNormals] Sample raw values:");
	for (size_t i = 0; i < std::min(size_t(5), numNormals); i++) {
		printf("\n  Normal %zu: x=%.6f, y=%.6f, z=%.6f",
			i, tanBf->data[i * 3 + 0], tanBf->data[i * 3 + 1], tanBf->data[i * 3 + 2]);
	}

	int wrongNormals = 0;

	for (size_t i = 0; i < numNormals; i++)
	{
		float x_raw = tanBf->data[i * 3 + 0];
		float y_raw = tanBf->data[i * 3 + 1];
		float z_raw = tanBf->data[i * 3 + 2];  // This might contain extra data (the A2 part)

		float x, y;

		if (isNormalized) {
			x = x_raw;
			y = y_raw;
			// Note: z_raw contains octahedral encoding + the A2 part, ignore it
		}
		else {
			x = (x_raw / 1023.0f) * 2.0f - 1.0f;
			y = (y_raw / 1023.0f) * 2.0f - 1.0f;
		}

		// Decode octahedral normal
		float z = 1.0f - fabs(x) - fabs(y);
		float t = fmax(-z, 0.0f);
		x += (x >= 0.0f) ? -t : t;
		y += (y >= 0.0f) ? -t : t;

		// Normalize to unit vector
		float len = sqrt(x * x + y * y + z * z);
		if (len > 0.0001f) {
			x /= len;
			y /= len;
			z /= len;
		}

		// Check if this looks like a reasonable normal
		if (fabs(y) > 0.95f) {
			wrongNormals++;
		}

		mesh.normals.push_back(x);
		mesh.normals.push_back(y);
		mesh.normals.push_back(z);
	}

	// Debug: Print first few decoded normals
	printf("\n[decodeOctahedralNormals] Sample decoded normals:");
	for (size_t i = 0; i < std::min(size_t(5), numNormals); i++) {
		printf("\n  Normal %zu: x=%.6f, y=%.6f, z=%.6f",
			i, mesh.normals[i * 3 + 0], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]);
	}

	printf("\n[decodeOctahedralNormals] Decoded %zu normals (%d seem mostly vertical)",
		mesh.normals.size() / 3, wrongNormals);
}