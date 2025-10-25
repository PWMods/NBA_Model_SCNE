#include <sceneupdate.h>
#include <common.h>
#include <scenereader.h>
#include <modelreader.h>
#include <nbamodel.h>
#include <sstream>
#include <bin_codec.h>
#include <fstream>
#include <filesystem>

CSceneUpdate::CSceneUpdate(const char* path, const bool fix_mesh)
	:
	CSceneFile(path),
	m_data(NULL),
	m_numVtxComponents(3),
	doMeshFix(fix_mesh)
{
}

CSceneUpdate::~CSceneUpdate()
{
}

void CSceneUpdate::clear()
{
	m_data = nullptr;
}

void CSceneUpdate::update(StUpdatePkg* data)
{
	/* Update scene source*/
	m_data = data;
	printf("\n\n========================================");
	printf("\n[CSceneUpdate] INJECTION STARTED!");
	printf("\n[CSceneUpdate] Inject Method: %d", m_data->search_method);
	printf("\n[CSceneUpdate] NumVerts: %d", m_data->numVerts);
	printf("\n[CSceneUpdate] NumFaces: %d", m_data->numFaces);
	printf("\n========================================\n");

	/* Perform update*/
	this->findTarget();
	this->updateTarget();

	/* Reset member data */
	this->clear();
}

void CSceneUpdate::findTarget()
{
	if (!m_data) {
		printf("\n[CSceneUpdate] ERROR: m_data is NULL!");
		return;
	}

	printf("\n[CSceneUpdate] Searching for mesh...");
	printf("\n  - Target NumVerts: %d", m_data->numVerts);
	printf("\n  - Target NumFaces: %d", m_data->numFaces);
	printf("\n  - Total models in scene: %d", m_scene->getNumModels());

	for (auto& model : m_scene->models())
	{
		printf("\n  - Checking model with %d meshes...", model->getNumMeshes());

		for (auto& mesh : model->getMeshes())
		{
			// FIXED: mesh->vertices is floats, divide by 3 for vertex count
			int meshVerts = mesh->vertices.size() / mesh->vertexComponents;  // NOT hardcoded /3
			int meshTris = mesh->triangles.size();

			printf("\n    - Mesh '%s': verts=%d, tris=%d",
				mesh->name.c_str(), meshVerts, meshTris);

			bool hasVtxMatch = (meshVerts == m_data->numVerts);
			bool hasTriMatch = (meshTris == m_data->numFaces);

			printf(" | vtx match=%s, tri match=%s",
				hasVtxMatch ? "YES" : "NO",
				hasTriMatch ? "YES" : "NO");

			if (hasVtxMatch && hasTriMatch)
			{
				printf("\n[CSceneUpdate] *** FOUND TARGET MESH: %s ***", mesh->name.c_str());
				m_targetMesh = mesh;
				return;
			}
		}
	}

	printf("\n[CSceneUpdate] ERROR: No matching mesh found!");
}

void CSceneUpdate::updateTarget()
{
	if (!m_targetMesh) return;

	printf("\n[CSceneUpdate] Updating mesh: %s\n", m_targetMesh->name.c_str());

	printf("\n[CSceneUpdate] STEP 1: buildVertexMap()...");
	this->buildVertexMap();
	printf(" DONE");

	printf("\n[CSceneUpdate] STEP 2: Creating update mesh...");
	m_updateMesh = std::make_shared<Mesh>();
	m_updateMesh->hasSplitIndices = m_targetMesh->hasSplitIndices;
	m_updateMesh->normalIndexRef = m_targetMesh->normalIndexRef;
	m_updateMesh->tangentIndexRef = m_targetMesh->tangentIndexRef;
	printf(" DONE");

	printf("\n[CSceneUpdate] STEP 3: getUpdatedVertices()...");
	this->getUpdatedVertices();
	printf(" DONE");

	// ✓ Skip normals entirely for split-index meshes
	if (m_targetMesh->hasSplitIndices) {
		printf("\n[CSceneUpdate] STEP 4: Split-index mesh - SKIPPING all normal/tangent updates");
	}
	else if (m_targetMesh->normals_ref) {
		printf("\n[CSceneUpdate] STEP 4: Mesh has tangent frames - updating normals...");
		this->getUpdatedNormals();
		printf(" DONE");
	}
	else {
		printf("\n[CSceneUpdate] STEP 4: Skipping normals - mesh has no tangent frames");
	}

	// ✓ Simplified: Only update vertex buffer for split-index meshes
	if (m_targetMesh->hasSplitIndices) {
		printf("\n[CSceneUpdate] Split-index mesh detected - updating ONLY vertex buffer");

		printf("\n[CSceneUpdate] STEP 5: updateVertexBuffer()...");
		this->updateVertexBuffer();
		printf(" DONE");

		printf("\n[CSceneUpdate] Skipping all normal/tangent buffer updates");
	}
	else {
		// Standard path for non-split meshes (characters, balls)
		printf("\n[CSceneUpdate] STEP 5: updateVertexBuffer()...");
		this->updateVertexBuffer();
		printf(" DONE");

		if (m_targetMesh->normals_ref) {
			printf("\n[CSceneUpdate] STEP 6: updateTangentBuffer()...");
			this->updateTangentBuffer();
			printf(" DONE");
		}
	}

	// Update .scne file after all buffers saved
	printf("\n[CSceneUpdate] STEP 7: Updating .scne file...");
	this->updateSceneFile();

	printf("\n======================================== DONE");
	printf("\n[CSceneUpdate] ALL STEPS COMPLETE!");

	// Log status
	common::ShowMessageBox("Updated Scene Mesh: " + m_targetMesh->name);
}

void CSceneUpdate::buildVertexMap()
{
	m_vertexMap.clear();
	auto& scan_type = m_data->search_method;
	auto& numVerts = m_data->numVerts;

	// Use Vertex ID Scan
	for (int i = 0; i < numVerts; i++) {
		this->m_vertexMap.push_back(i);
	};
};

void CSceneUpdate::getUpdatedVertices()
{
	auto posBf = (CDataBuffer*)m_targetMesh->vertex_ref;
	if (!posBf) throw std::runtime_error("Cannot load empty vertex buffer.");

	BinaryCodec codec(posBf->getEncoding(), posBf->getType());
	auto& verts = m_updateMesh->vertices;
	m_numVtxComponents = codec.num_channels();
	bool hasCoordW = m_numVtxComponents == 4;

	std::string format = posBf->getFormat();
	printf("\n[getUpdatedVertices] Position buffer format: %s", format.c_str());
	printf("\n[getUpdatedVertices] Vertex components: %d", m_numVtxComponents);

	// Build XYZ data
	for (int i = 0; i < m_vertexMap.size(); i++)
	{
		auto& index = m_vertexMap[i];
		verts.push_back(m_data->position[index * 3 + 0]);
		verts.push_back(m_data->position[index * 3 + 1]);
		verts.push_back(m_data->position[index * 3 + 2]);
	}

	printf("\n[getUpdatedVertices] Sample positions BEFORE transform:");
	for (int i = 0; i < std::min(3, (int)(verts.size() / 3)); i++) {
		printf("\n  Vert %d: [%.6f, %.6f, %.6f]", i, verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
	}

	// ✓ Apply coordinate swap BEFORE transform for R21G21B22
	if (doMeshFix && m_numVtxComponents == 3) {
		m_updateMesh->alignPosition(false, 3);
		printf("\n[getUpdatedVertices] Applied Blender→NBA coordinate swap");

		printf("\n[getUpdatedVertices] Sample positions AFTER swap:");
		for (int i = 0; i < std::min(3, (int)(verts.size() / 3)); i++) {
			printf("\n  Vert %d: [%.6f, %.6f, %.6f]", i, verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
		}
	}

	// ✓ SKIP the scale/offset transform for R21G21B22 (will be applied during encoding)
	if (format != "R21G21B22_UINT") {
		MeshCalc::transformVertices(posBf, verts, 3);
		printf("\n[getUpdatedVertices] Applied scale/offset transform");
	}
	else {
		printf("\n[getUpdatedVertices] SKIPPED transform for R21G21B22_UINT (will apply during encoding)");
	}

	// Add W for 4-component meshes
	if (hasCoordW) {
		std::vector<float> verts4D;
		verts4D.reserve((verts.size() / 3) * 4);
		for (int i = 0; i < verts.size() / 3; i++) {
			verts4D.push_back(verts[i * 3 + 0]);
			verts4D.push_back(verts[i * 3 + 1]);
			verts4D.push_back(verts[i * 3 + 2]);
			verts4D.push_back(1.0f);
		}
		m_updateMesh->vertices = verts4D;
		m_numVtxComponents = 4;
		printf("\n[getUpdatedVertices] Built 4-component vertices with W=1.0");
	}
}

void CSceneUpdate::getUpdatedNormals()
{
	printf("\n[getUpdatedNormals] Starting...");

	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;
	printf("\n[getUpdatedNormals] Got tanBf pointer");

	if (!tanBf) {
		printf("\n[getUpdatedNormals] ERROR: tanBf is NULL");
		throw std::runtime_error("No tangent frame buffer. Failed to update.");
	}

	std::string format = tanBf->getFormat();
	printf("\n[getUpdatedNormals] Format: %s", format.c_str());

	bool isSplitIndex = m_targetMesh->hasSplitIndices;
	bool isR10G10B10A2 = (format == "R10G10B10A2_UINT");
	bool isR10G10B10 = (format == "R10G10B10_SNORM_A2_UNORM");

	printf("\n[getUpdatedNormals] Split indices: %s", isSplitIndex ? "YES" : "NO");

	// ✓ Skip normal updates for split-index meshes for now
	if (isSplitIndex && isR10G10B10) {
		printf("\n[getUpdatedNormals] SKIPPING normal updates for split-index mesh");
		printf("\n[getUpdatedNormals] (Normals will be preserved from original)");
		return;
	}

	if (!isR10G10B10A2 && !isR10G10B10) {
		printf("\n[getUpdatedNormals] ERROR: Unsupported format: %s", format.c_str());
		throw std::runtime_error("Unsupported tangent frame encoding. Failed to update.");
	}

	printf("\n[getUpdatedNormals] Format check passed");

	// Standard per-vertex format
	printf("\n[getUpdatedNormals] Processing STANDARD per-vertex mesh");

	auto& normals = m_updateMesh->normals;
	auto& tanFrames = m_targetMesh->tangent_frames;
	printf("\n[getUpdatedNormals] vertexMap size: %zu", m_vertexMap.size());

	for (int i = 0; i < m_vertexMap.size(); i++)
	{
		auto& index = m_vertexMap[i];
		normals.push_back(m_data->normals[index * 3 + 0]);
		normals.push_back(m_data->normals[index * 3 + 1]);
		normals.push_back(m_data->normals[index * 3 + 2]);
	}
	printf("\n[getUpdatedNormals] Loaded %zu normals", normals.size());

	if (doMeshFix && m_numVtxComponents == 3) {
		printf("\n[getUpdatedNormals] Aligning normals for 3-component mesh (Blender→NBA)...");
		m_updateMesh->alignNormals(false, 3);
		printf("\n[getUpdatedNormals] Align done");
	}
	else if (m_numVtxComponents == 4) {
		printf("\n[getUpdatedNormals] Skipping normal alignment for 4-component mesh");
	}

	// Update tangent frame vectors 
	printf("\n[getUpdatedNormals] Calling updateTangentFrameVec...");
	printf("\n  - tanFrames size: %zu", tanFrames.size());
	printf("\n  - normals size: %zu", normals.size());
	try {
		MeshCalc::updateTangentFrameVec(tanFrames, normals, m_updateMesh->tangent_frames);
		printf("\n[getUpdatedNormals] updateTangentFrameVec SUCCESS!");
	}
	catch (const std::exception& e) {
		printf("\n[getUpdatedNormals] EXCEPTION in updateTangentFrameVec: %s", e.what());
		throw;
	}

	printf("\n[getUpdatedNormals] Complete!");
}

void CSceneUpdate::updateVertexBuffer()
{
	auto posBf = (CDataBuffer*)m_targetMesh->vertex_ref;
	if (!posBf) {
		printf("\n[updateVertexBuffer] Failed to locate mesh data.");
		return;
	}

	auto& mesh_data = m_updateMesh->vertices;
	auto buffer = posBf->getBinary();
	char* src = (char*)buffer.data();

	std::string format = posBf->getFormat();
	printf("\n[updateVertexBuffer] Position format: %s", format.c_str());

	// For R21G21B22, apply transform to encode back to quantized space
	std::vector<float> transformedVerts;
	if (format == "R21G21B22_UINT") {
		printf("\n[updateVertexBuffer] Applying encode transform for R21G21B22...");

		printf("\n[updateVertexBuffer] Sample positions BEFORE encoding:");
		for (int i = 0; i < std::min(3, (int)(mesh_data.size() / 3)); i++) {
			printf("\n  Vert %d: [%.6f, %.6f, %.6f]", i,
				mesh_data[i * 3], mesh_data[i * 3 + 1], mesh_data[i * 3 + 2]);
		}

		transformedVerts = mesh_data;
		MeshCalc::transformVertices(posBf, transformedVerts, 3);

		printf("\n[updateVertexBuffer] Sample positions AFTER encoding:");
		for (int i = 0; i < std::min(3, (int)(transformedVerts.size() / 3)); i++) {
			printf("\n  Vert %d: [%.6f, %.6f, %.6f]", i,
				transformedVerts[i * 3], transformedVerts[i * 3 + 1], transformedVerts[i * 3 + 2]);
		}

		BinaryCodec codec(posBf->getEncoding(), posBf->getType());
		codec.update(src, transformedVerts.size(), transformedVerts, posBf->getDataOffset(), posBf->getStride());
	}
	else {
		BinaryCodec codec(posBf->getEncoding(), posBf->getType());
		codec.update(src, mesh_data.size(), mesh_data, posBf->getDataOffset(), posBf->getStride());
	}

	// ✓ Skip hash increment for split-index meshes
	if (!m_targetMesh->hasSplitIndices) {
		std::string oldPath = posBf->getPath();
		if (!common::containsSubstring(oldPath, ".gz")) {
			std::string newPath = incrementHash(oldPath);
			m_updatedBuffers[oldPath] = newPath;
			posBf->setPath(newPath);
			printf("\n[updateVertexBuffer] Changed filename: %s -> %s", oldPath.c_str(), newPath.c_str());
		}
	}
	else {
		printf("\n[updateVertexBuffer] Split-index mesh - keeping original filename");
	}

	posBf->saveBinary(src, buffer.size());
	printf("\n[updateVertexBuffer] Buffer saved successfully");
}

void CSceneUpdate::updateTangentBuffer()
{
	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;
	if (!tanBf) {
		printf("\n[updateTangentBuffer] Failed to locate tangent buffer.");
		return;
	}

	std::string format = tanBf->getFormat();
	printf("\n[updateTangentBuffer] Format: %s", format.c_str());
	printf("\n[updateTangentBuffer] Split indices: %s", m_targetMesh->hasSplitIndices ? "YES" : "NO");

	auto buffer = tanBf->getBinary();
	char* src = (char*)buffer.data();

	// Handle split index meshes differently
	if (m_targetMesh->hasSplitIndices && format == "R10G10B10_SNORM_A2_UNORM")
	{
		printf("\n[updateTangentBuffer] Updating SPLIT INDEX mesh with unique normals");

		// Encode unique normals back to octahedral R10G10B10
		std::vector<float> encodedNormals;
		encodeOctahedralNormals(m_updateMesh->uniqueNormals, encodedNormals);

		printf("\n[updateTangentBuffer] Encoded %zu unique normals", encodedNormals.size() / 3);

		BinaryCodec codec(tanBf->getEncoding(), tanBf->getType());
		codec.update(src, encodedNormals.size(), encodedNormals, tanBf->getDataOffset(), tanBf->getStride());

		printf("\n[updateTangentBuffer] Updated tangent buffer with encoded normals");
	}
	else
	{
		printf("\n[updateTangentBuffer] Updating STANDARD per-vertex mesh");

		auto& mesh_data = m_updateMesh->tangent_frames;

		if (mesh_data.empty()) {
			printf("\n[updateTangentBuffer] WARNING: No tangent frame data to update!");
			return;
		}

		BinaryCodec codec(tanBf->getEncoding(), tanBf->getType());
		codec.update(src, mesh_data.size(), mesh_data, tanBf->getDataOffset(), tanBf->getStride());
	}

	// ✓ Skip hash increment for split-index meshes
	if (!m_targetMesh->hasSplitIndices) {
		std::string oldPath = tanBf->getPath();
		if (!common::containsSubstring(oldPath, ".gz")) {
			std::string newPath = incrementHash(oldPath);
			m_updatedBuffers[oldPath] = newPath;
			tanBf->setPath(newPath);
			printf("\n[updateTangentBuffer] Changed filename: %s -> %s", oldPath.c_str(), newPath.c_str());
		}
	}
	else {
		printf("\n[updateTangentBuffer] Split-index mesh - keeping original filename");
	}

	tanBf->saveBinary(src, buffer.size());
	printf("\n[updateTangentBuffer] Buffer saved successfully");
}

std::string CSceneUpdate::incrementHash(const std::string& filename)
{
	std::string newFilename = filename;

	// Find the hash portion: between last '.' and ".bin"
	size_t binPos = newFilename.find(".bin");
	size_t lastDotPos = newFilename.find_last_of('.', binPos - 1);

	if (binPos == std::string::npos || lastDotPos == std::string::npos) {
		return filename; // Can't parse, return original
	}

	// Increment the last character of the hash (before .bin)
	char& lastChar = newFilename[binPos - 1];

	// Increment through hex chars: 0-9, a-f
	if (lastChar >= '0' && lastChar < '9') {
		lastChar++;
	}
	else if (lastChar == '9') {
		lastChar = 'a';
	}
	else if (lastChar >= 'a' && lastChar < 'f') {
		lastChar++;
	}
	else if (lastChar == 'f') {
		lastChar = '0'; // Wrap around
	}

	return newFilename;
}

void CSceneUpdate::rebuildSplitIndexBuffers()
{
	// ✓ Skip for split-index meshes - they already have correct unique normals
	// and we must preserve the original index structure
	if (m_targetMesh->hasSplitIndices) {
		printf("\n[rebuildSplitIndexBuffers] Split-index mesh detected - skipping rebuild");
		printf("\n[rebuildSplitIndexBuffers] Using original index structure with updated unique normals");
		printf("\n[rebuildSplitIndexBuffers] Original normal indices: %zu", m_targetMesh->normalIndices.size());
		printf("\n[rebuildSplitIndexBuffers] Updated unique normals: %zu", m_updateMesh->uniqueNormals.size() / 3);

		// Keep original indices unchanged
		m_updateMesh->normalIndices = m_targetMesh->normalIndices;
		m_updateMesh->tangentIndices = m_targetMesh->tangentIndices;

		// Unique normals were already built in getUpdatedNormals()
		// Just confirm they exist
		if (m_updateMesh->uniqueNormals.empty()) {
			printf("\n[rebuildSplitIndexBuffers] WARNING: No unique normals found!");
		}

		return;
	}

	// ✓ Original code for NON-split meshes
	printf("\n[rebuildSplitIndexBuffers] Re-optimizing per-vertex normals to split indices");

	auto& normals = m_updateMesh->normals;

	// Build unique normal list with tolerance
	constexpr float TOLERANCE = 0.0001f;
	std::vector<float> uniqueNormals;
	std::vector<uint16_t> newNormalIndices;

	uniqueNormals.reserve(normals.size() / 2);
	newNormalIndices.reserve(m_targetMesh->triangles.size() * 3);

	// For each triangle corner
	for (size_t triIdx = 0; triIdx < m_targetMesh->triangles.size(); triIdx++)
	{
		auto& tri = m_targetMesh->triangles[triIdx];

		for (int corner = 0; corner < 3; corner++)
		{
			uint32_t vertIdx = tri[corner];
			if (vertIdx * 3 + 2 >= normals.size()) {
				printf("\n[rebuildSplitIndexBuffers] ERROR: Vertex %d out of range", vertIdx);
				newNormalIndices.push_back(0);
				continue;
			}

			Vec3 normal{
				normals[vertIdx * 3 + 0],
				normals[vertIdx * 3 + 1],
				normals[vertIdx * 3 + 2]
			};

			// Search for matching normal in unique list
			int matchIdx = -1;
			for (size_t i = 0; i < uniqueNormals.size() / 3; i++)
			{
				Vec3 existing{
					uniqueNormals[i * 3 + 0],
					uniqueNormals[i * 3 + 1],
					uniqueNormals[i * 3 + 2]
				};

				float diff = fabsf(normal.x - existing.x) +
					fabsf(normal.y - existing.y) +
					fabsf(normal.z - existing.z);

				if (diff < TOLERANCE) {
					matchIdx = i;
					break;
				}
			}

			// Add to unique list if not found
			if (matchIdx == -1) {
				matchIdx = uniqueNormals.size() / 3;
				uniqueNormals.push_back(normal.x);
				uniqueNormals.push_back(normal.y);
				uniqueNormals.push_back(normal.z);
			}

			newNormalIndices.push_back(static_cast<uint16_t>(matchIdx));
		}
	}

	// Update mesh with new unique normals and indices
	m_updateMesh->uniqueNormals = uniqueNormals;
	m_updateMesh->normalIndices = newNormalIndices;

	// Build tangent frames from unique normals
	m_updateMesh->uniqueTangents.clear();
	m_updateMesh->tangentIndices = newNormalIndices;  // Same indices

	for (size_t i = 0; i < uniqueNormals.size() / 3; i++)
	{
		Vec3 normal{
			uniqueNormals[i * 3 + 0],
			uniqueNormals[i * 3 + 1],
			uniqueNormals[i * 3 + 2]
		};

		Vec4 encoded{ 0, 0, 0, 0 };
		MeshCalc::EncodeOctahedralNormal(&encoded, normal);

		m_updateMesh->uniqueTangents.push_back(encoded.x);
		m_updateMesh->uniqueTangents.push_back(encoded.y);
		m_updateMesh->uniqueTangents.push_back(encoded.z);
		m_updateMesh->uniqueTangents.push_back(encoded.w);
	}

	printf("\n[rebuildSplitIndexBuffers] Optimized %zu normals to %zu unique normals",
		normals.size() / 3, uniqueNormals.size() / 3);
}

void CSceneUpdate::updateNormalIndexBuffer()
{
	auto normalIdxBf = (CDataBuffer*)m_targetMesh->normalIndexRef;
	if (!normalIdxBf) {
		printf("\n[updateNormalIndexBuffer] No NormalIndexBuffer found");
		return;
	}

	auto buffer = normalIdxBf->getBinary();
	char* src = (char*)buffer.data();

	uint16_t* indices = reinterpret_cast<uint16_t*>(src);
	size_t numIndices = std::min(m_updateMesh->normalIndices.size(), buffer.size() / sizeof(uint16_t));

	for (size_t i = 0; i < numIndices; i++) {
		indices[i] = m_updateMesh->normalIndices[i];
	}

	// ✓ Skip hash increment for split-index meshes
	if (!m_targetMesh->hasSplitIndices) {
		std::string oldPath = normalIdxBf->getPath();
		if (!common::containsSubstring(oldPath, ".gz")) {
			std::string newPath = incrementHash(oldPath);
			m_updatedBuffers[oldPath] = newPath;
			normalIdxBf->setPath(newPath);
			printf("\n[updateNormalIndexBuffer] Changed filename: %s -> %s", oldPath.c_str(), newPath.c_str());
		}
	}
	else {
		printf("\n[updateNormalIndexBuffer] Split-index mesh - keeping original filename");
	}

	normalIdxBf->saveBinary(src, buffer.size());
	printf("\n[updateNormalIndexBuffer] Updated %zu indices", numIndices);
}

void CSceneUpdate::updateTangentIndexBuffer()
{
	auto tangentIdxBf = (CDataBuffer*)m_targetMesh->tangentIndexRef;
	if (!tangentIdxBf) {
		printf("\n[updateTangentIndexBuffer] No TangentIndexBuffer found");
		return;
	}

	auto buffer = tangentIdxBf->getBinary();
	char* src = (char*)buffer.data();

	uint16_t* indices = reinterpret_cast<uint16_t*>(src);
	size_t numIndices = std::min(m_updateMesh->tangentIndices.size(), buffer.size() / sizeof(uint16_t));

	for (size_t i = 0; i < numIndices; i++) {
		indices[i] = m_updateMesh->tangentIndices[i];
	}

	// ✓ Skip hash increment for split-index meshes
	if (!m_targetMesh->hasSplitIndices) {
		std::string oldPath = tangentIdxBf->getPath();
		if (!common::containsSubstring(oldPath, ".gz")) {
			std::string newPath = incrementHash(oldPath);
			m_updatedBuffers[oldPath] = newPath;
			tangentIdxBf->setPath(newPath);
			printf("\n[updateTangentIndexBuffer] Changed filename: %s -> %s", oldPath.c_str(), newPath.c_str());
		}
	}
	else {
		printf("\n[updateTangentIndexBuffer] Split-index mesh - keeping original filename");
	}

	tangentIdxBf->saveBinary(src, buffer.size());
	printf("\n[updateTangentIndexBuffer] Updated %zu indices", numIndices);
}

void CSceneUpdate::updateSceneFile()
{
	// ✓ Skip JSON update for split-index meshes
	if (m_targetMesh->hasSplitIndices) {
		printf("\n[CSceneUpdate] Split-index mesh detected - skipping JSON update");
		printf("\n[CSceneUpdate] Files updated in-place with same names");
		return;
	}

	// Find .scne file in working directory
	namespace fs = std::filesystem;
	std::string scenePath;

	for (const auto& entry : fs::directory_iterator(WORKING_DIR)) {
		if (entry.path().extension() == ".scne" || entry.path().extension() == ".SCNE") {
			scenePath = entry.path().string();
			break;
		}
	}

	if (scenePath.empty()) {
		printf("\n[ERROR] No .scne file found in: %s", WORKING_DIR.c_str());
		return;
	}

	printf("\n[CSceneUpdate] Found .scne file: %s", scenePath.c_str());

	// Use map to prevent duplicates
	std::map<std::string, std::string> bufferUpdates;

	// Get buffer paths
	auto posBf = (CDataBuffer*)m_targetMesh->vertex_ref;
	if (posBf) {
		std::string oldName = posBf->getPath();
		if (!oldName.empty() && common::containsSubstring(oldName, ".gz")) {
			std::string newName = oldName;
			common::replaceSubString(newName, ".gz", ".bin");
			bufferUpdates[oldName] = newName;
			printf("\n[CSceneUpdate] Will update position buffer: %s -> %s", oldName.c_str(), newName.c_str());
		}
	}

	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;
	if (tanBf) {
		std::string oldName = tanBf->getPath();
		if (!oldName.empty() && common::containsSubstring(oldName, ".gz")) {
			std::string newName = oldName;
			common::replaceSubString(newName, ".gz", ".bin");

			if (bufferUpdates.find(oldName) == bufferUpdates.end()) {
				bufferUpdates[oldName] = newName;
				printf("\n[CSceneUpdate] Will update tangent buffer: %s -> %s", oldName.c_str(), newName.c_str());
			}
			else {
				printf("\n[CSceneUpdate] Tangent buffer already in update list (same as position)");
			}
		}
	}

	if (bufferUpdates.empty()) {
		printf("\n[CSceneUpdate] No compressed buffers to update in JSON");
		return;
	}

	printf("\n[CSceneUpdate] Updating JSON for %d unique buffer(s)...", bufferUpdates.size());

	// Read JSON file
	std::ifstream file(scenePath, std::ios::binary);
	if (!file.is_open()) {
		printf("\n[ERROR] Could not open .scne file!");
		return;
	}

	std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	std::string wrappedContent = "{" + fileContent + "}";

	// Parse JSON
	JSON json;
	try {
		json = JSON::parse(wrappedContent);
		printf("\n[DEBUG] JSON parsed successfully");
	}
	catch (const std::exception& e) {
		printf("\n[ERROR] Failed to parse JSON: %s", e.what());
		return;
	}

	bool modified = false;

	auto updateBinaryNode = [&](JSON& node, const std::string& path) {
		if (!node.is_object() || !node.contains("Binary")) return false;

		std::string currentBinary = node["Binary"];

		auto it = bufferUpdates.find(currentBinary);
		if (it != bufferUpdates.end()) {
			node["Binary"] = it->second;

			if (node.contains("CompressionMethod")) {
				node.erase("CompressionMethod");
			}

			printf("\n  ✓ Updated %s: %s -> %s",
				path.c_str(), it->first.c_str(), it->second.c_str());
			return true;
		}
		return false;
		};

	std::function<void(JSON&, const std::string&)> searchAndUpdate;
	searchAndUpdate = [&](JSON& node, const std::string& currentPath) {
		if (!node.is_object()) return;

		if (node.contains("VertexStream") && node["VertexStream"].is_array()) {
			printf("\n[DEBUG] Found VertexStream at %s", currentPath.c_str());
			int streamIndex = 0;
			for (auto& stream : node["VertexStream"]) {
				if (!stream.is_null()) {
					if (updateBinaryNode(stream, currentPath + ".VertexStream[" + std::to_string(streamIndex) + "]")) {
						modified = true;
					}
				}
				streamIndex++;
			}
		}

		if (node.contains("IndexBuffer")) {
			if (updateBinaryNode(node["IndexBuffer"], currentPath + ".IndexBuffer")) {
				modified = true;
			}
		}
		if (node.contains("MatrixWeightsBuffer")) {
			if (updateBinaryNode(node["MatrixWeightsBuffer"], currentPath + ".MatrixWeightsBuffer")) {
				modified = true;
			}
		}

		for (auto& [key, value] : node.items()) {
			if (value.is_object()) {
				searchAndUpdate(value, currentPath.empty() ? key : currentPath + "." + key);
			}
		}
		};

	searchAndUpdate(json, "");

	if (!modified) {
		printf("\n[WARNING] No matching entries found to update in .scne file!");
		return;
	}

	// Create backup
	std::string backupPath = scenePath + ".bak";
	try {
		fs::copy_file(scenePath, backupPath, fs::copy_options::overwrite_existing);
		printf("\n[CSceneUpdate] Created backup: %s", backupPath.c_str());
	}
	catch (const std::exception& e) {
		printf("\n[WARNING] Could not create backup: %s", e.what());
	}

	// Save updated JSON
	std::ofstream outFile(scenePath);
	if (!outFile.is_open()) {
		printf("\n[ERROR] Could not write .scne file!");
		return;
	}

	std::string output = json.dump(1, '\t');

	if (output.size() >= 2 && output[0] == '{' && output[output.size() - 1] == '}') {
		output = output.substr(1, output.size() - 2);
	}

	while (!output.empty() && (output[0] == '\n' || output[0] == '\t' || output[0] == ' ')) {
		output.erase(0, 1);
	}

	outFile << output;
	outFile.close();

	printf("\n[CSceneUpdate] Successfully updated .scne file!");
}

void CSceneUpdate::encodeOctahedralNormals(const std::vector<float>& normals, std::vector<float>& encoded)
{
	printf("\n[encodeOctahedralNormals] Encoding %zu normals", normals.size() / 3);

	size_t numNormals = normals.size() / 3;
	encoded.clear();
	encoded.reserve(numNormals * 3);

	for (size_t i = 0; i < numNormals; i++)
	{
		float nx = normals[i * 3 + 0];
		float ny = normals[i * 3 + 1];
		float nz = normals[i * 3 + 2];

		// Normalize input (just in case)
		float len = sqrt(nx * nx + ny * ny + nz * nz);
		if (len > 0.0001f) {
			nx /= len;
			ny /= len;
			nz /= len;
		}

		// Octahedral projection
		float s = 1.0f / (fabs(nx) + fabs(ny) + fabs(nz));
		float ox = nx * s;
		float oy = ny * s;

		// ✓ CRITICAL FIX: Wrap when z < 0, not z >= 0
		if (nz < 0.0f) {
			float octWrapX = (1.0f - fabs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
			float octWrapY = (1.0f - fabs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
			ox = octWrapX;
			oy = octWrapY;
		}

		// Store as SNORM [-1, 1] range for BinaryCodec
		// R10G10B10_SNORM expects values in [-1, 1]
		encoded.push_back(ox);   // X component [-1, 1]
		encoded.push_back(oy);   // Y component [-1, 1]
		encoded.push_back(0.0f); // Z unused (A2 component)
	}

	printf("\n[encodeOctahedralNormals] Encoded to %zu values", encoded.size());
	printf("\n[encodeOctahedralNormals] Sample encoded normals:");
	for (size_t i = 0; i < std::min(size_t(3), encoded.size() / 3); i++) {
		printf("\n  Encoded %zu: [%.6f, %.6f, %.6f]",
			i, encoded[i * 3 + 0], encoded[i * 3 + 1], encoded[i * 3 + 2]);
	}
}