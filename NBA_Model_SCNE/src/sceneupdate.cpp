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

	// Assign vertex ids
	printf("\n[CSceneUpdate] Updating mesh: %s\n", m_targetMesh->name.c_str());
	printf("\n[CSceneUpdate] STEP 1: buildVertexMap()...");
	this->buildVertexMap();
	printf(" DONE");

	// Build Update Mesh
	printf("\n[CSceneUpdate] STEP 2: Creating update mesh...");
	m_updateMesh = std::make_shared<Mesh>();
	printf(" DONE");

	printf("\n[CSceneUpdate] STEP 3: getUpdatedVertices()...");
	this->getUpdatedVertices();
	printf(" DONE");

	// Only update normals if mesh has tangent frames (character models)
	if (m_targetMesh->normals_ref) {
		printf("\n[CSceneUpdate] STEP 4: Mesh has tangent frames - updating normals...");
		this->getUpdatedNormals();
		printf(" DONE");
	}
	else {
		printf("\n[CSceneUpdate] STEP 4: Skipping normals - mesh has no tangent frames (environment mesh)");
	}

	// Update streams
	printf("\n[CSceneUpdate] STEP 5: updateVertexBuffer()...");
	this->updateVertexBuffer();
	printf(" DONE");

	// Only update tangent buffer if it exists
	if (m_targetMesh->normals_ref) {
		printf("\n[CSceneUpdate] STEP 6: updateTangentBuffer()...");
		this->updateTangentBuffer();
		printf(" DONE");
	}
	else {
		printf("\n[CSceneUpdate] STEP 6: Skipping tangent buffer update (environment mesh)");
	}

	// Update .scne file after all buffers saved
	printf("\n[CSceneUpdate] STEP 7: Updating .scne file...");
	this->updateSceneFile();
	printf(" DONE");

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

	// Build XYZ data
	for (int i = 0; i < m_vertexMap.size(); i++)
	{
		auto& index = m_vertexMap[i];
		verts.push_back(m_data->position[index * 3 + 0]);
		verts.push_back(m_data->position[index * 3 + 1]);
		verts.push_back(m_data->position[index * 3 + 2]);
	}

	// Transform XYZ (inverse scale/offset only)
	MeshCalc::transformVertices(posBf, verts, 3);

	// ✓ CRITICAL FIX: Only apply coordinate swap for 3-component meshes
	// 4-component meshes import WITHOUT alignPosition, so inject WITHOUT it too!
	if (doMeshFix && m_numVtxComponents == 3) {
		m_updateMesh->alignPosition(false, 3);
		printf("\n[getUpdatedVertices] Applied Blender→NBA coordinate swap (3-component)");
	}
	else if (m_numVtxComponents == 4) {
		printf("\n[getUpdatedVertices] Skipped coordinate swap for 4-component mesh (matches import)");
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

	// Initialize vertex buffer
	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;
	printf("\n[getUpdatedNormals] Got tanBf pointer");

	if (!tanBf || tanBf->getFormat() != "R10G10B10A2_UINT") {
		printf("\n[getUpdatedNormals] ERROR: tanBf=%p, format=%s",
			tanBf, tanBf ? tanBf->getFormat().c_str() : "NULL");
		throw std::runtime_error("Unsupported tangent frame encoding. Failed to update.");
	}

	printf("\n[getUpdatedNormals] Format check passed");

	// Get stream attributes & update mesh normals
	auto& normals = m_updateMesh->normals;
	auto& tanFrames = m_targetMesh->tangent_frames;

	printf("\n[getUpdatedNormals] vertexMap size: %zu", m_vertexMap.size());

	for (int i = 0; i < m_vertexMap.size(); i++)
	{
		auto& index = m_vertexMap[i];

		// normals is a flat float array: [x,y,z, x,y,z, ...]
		normals.push_back(m_data->normals[index * 3 + 0]);
		normals.push_back(m_data->normals[index * 3 + 1]);
		normals.push_back(m_data->normals[index * 3 + 2]);
	}

	printf("\n[getUpdatedNormals] Loaded %zu normals", normals.size());

	// ✓ FIXED: Only align 3-component meshes, skip 4-component
	if (doMeshFix && m_numVtxComponents == 3) {
		printf("\n[getUpdatedNormals] Aligning normals for 3-component mesh (Blender→NBA)...");
		m_updateMesh->alignNormals(false, 3);  // false = Blender to NBA
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
	catch (...) {
		printf("\n[getUpdatedNormals] UNKNOWN EXCEPTION in updateTangentFrameVec!");
		throw;
	}

	printf("\n[getUpdatedNormals] Complete!");
}

void CSceneUpdate::updateVertexBuffer()
{
	auto posBf = (CDataBuffer*)m_targetMesh->vertex_ref;
	auto& mesh_data = m_updateMesh->vertices;
	auto  buffer = posBf->getBinary();
	char* src = (char*)buffer.data();

	printf("\n[updateVertexBuffer] CRITICAL STRIDE CHECK:");
	printf("\n  - Vertex components: %d", m_numVtxComponents);
	printf("\n  - Format: %s", posBf->getFormat().c_str());
	printf("\n  - DataBuffer stride: %d", posBf->getStride());
	printf("\n  - Expected stride for R16×4: 8 bytes");
	printf("\n  - Buffer size: %zu", buffer.size());
	printf("\n  - Mesh data size: %zu floats", mesh_data.size());
	printf("\n  - Calculated vertices: %zu", mesh_data.size() / m_numVtxComponents);

	BinaryCodec codec(posBf->getEncoding(), posBf->getType());
	codec.update(src, mesh_data.size(), mesh_data, posBf->getDataOffset(), posBf->getStride());
	posBf->saveBinary(src, buffer.size());
}

void CSceneUpdate::updateTangentBuffer()
{
	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;

	if (!tanBf) {
		printf("\n[CSceneUpdate] Failed to locate mesh data.");
		return;
	}

	// Get updated mesh data vector
	auto& mesh_data = m_updateMesh->tangent_frames;
	auto  buffer = tanBf->getBinary();
	char* src = (char*)buffer.data();

	// update/overwrite binary data
	BinaryCodec codec(tanBf->getEncoding(), tanBf->getType());
	codec.update(src, mesh_data.size(), mesh_data, tanBf->getDataOffset(), tanBf->getStride());

	// save buffer to source file
	tanBf->saveBinary(src, buffer.size());
}

void CSceneUpdate::updateSceneFile()
{
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

	// Track which buffers we updated (basename only!)
	std::vector<std::string> updatedBuffers;

	// Get buffer paths - extract basename only
	auto posBf = (CDataBuffer*)m_targetMesh->vertex_ref;
	if (posBf) {
		std::string oldName = posBf->getPath();
		if (!oldName.empty() && common::containsSubstring(oldName, ".gz")) {
			std::string newName = oldName;
			common::replaceSubString(newName, ".gz", ".bin");
			updatedBuffers.push_back(oldName);
			updatedBuffers.push_back(newName);
			printf("\n[CSceneUpdate] Will update position buffer: %s -> %s", oldName.c_str(), newName.c_str());
		}
	}

	auto tanBf = (CDataBuffer*)m_targetMesh->normals_ref;
	if (tanBf) {
		std::string oldName = tanBf->getPath();
		if (!oldName.empty() && common::containsSubstring(oldName, ".gz")) {
			std::string newName = oldName;
			common::replaceSubString(newName, ".gz", ".bin");
			updatedBuffers.push_back(oldName);
			updatedBuffers.push_back(newName);
			printf("\n[CSceneUpdate] Will update tangent buffer: %s -> %s", oldName.c_str(), newName.c_str());
		}
	}

	if (updatedBuffers.empty()) {
		printf("\n[CSceneUpdate] No compressed buffers to update in JSON");
		return;
	}

	printf("\n[CSceneUpdate] Updating JSON for %d buffer(s)...", updatedBuffers.size() / 2);

	// Read JSON file
	std::ifstream file(scenePath, std::ios::binary);
	if (!file.is_open()) {
		printf("\n[ERROR] Could not open .scne file!");
		return;
	}

	// Read entire file to string
	std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	// Wrap with braces for parsing
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

	// Helper function to update a node's binary references
	auto updateBinaryNode = [&](JSON& node, const std::string& path) {
		if (!node.is_object() || !node.contains("Binary")) return false;

		std::string currentBinary = node["Binary"];

		// Check if this buffer was updated
		for (size_t i = 0; i < updatedBuffers.size(); i += 2) {
			if (currentBinary == updatedBuffers[i]) {
				node["Binary"] = updatedBuffers[i + 1];

				// CRITICAL: Remove CompressionMethod key entirely (don't set to 0)
				if (node.contains("CompressionMethod")) {
					node.erase("CompressionMethod");
				}

				printf("\n  ✓ Updated %s: %s -> %s",
					path.c_str(), updatedBuffers[i].c_str(), updatedBuffers[i + 1].c_str());
				return true;
			}
		}
		return false;
		};

	// Recursively search for Model section
	std::function<void(JSON&, const std::string&)> searchAndUpdate;
	searchAndUpdate = [&](JSON& node, const std::string& currentPath) {
		if (!node.is_object()) return;

		// Check if this node has VertexStream array
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

		// Check IndexBuffer, MatrixWeightsBuffer, etc.
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

		// Recurse into child objects
		for (auto& [key, value] : node.items()) {
			if (value.is_object()) {
				searchAndUpdate(value, currentPath.empty() ? key : currentPath + "." + key);
			}
		}
		};

	// Start search from root
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

	// Dump and unwrap
	std::string output = json.dump(1, '\t');

	// Remove outer braces
	if (output.size() >= 2 && output[0] == '{' && output[output.size() - 1] == '}') {
		output = output.substr(1, output.size() - 2);
	}

	// Trim ALL leading whitespace (tabs and newlines)
	while (!output.empty() && (output[0] == '\n' || output[0] == '\t' || output[0] == ' ')) {
		output.erase(0, 1);
	}

	outFile << output;
	outFile.close();

	printf("\n[CSceneUpdate] Successfully updated .scne file!");
	printf("\n========================================");
}