#include <dll/interface_mesh.h>
#include <nbascene>
#include <vector>

void* loadModelFile(const char* filePath, void** filePtr)
{
    /* Initialize CModelFile Address */
    INCLUDE_LODS = false;
    *filePtr = nullptr;

    /* Load file and scene-contents */
    try
    {
        CSceneFile* file = new CSceneFile(filePath);
        file->load();

        if (file->scene()->empty())
            return nullptr;

        auto& scene = file->scene();
        *filePtr = file;

        printf("\n[CNBAInterface] Found total models: %d\n", file->scene()->getNumModels());
        return scene.get();
    }
    catch (...) {}

    printf("[CNBAScene] Failed to read user scenefile.\n");
    return nullptr;
}

void release_model_file(void* filePtr)
{
    CSceneFile* file = static_cast<CSceneFile*>(filePtr);
    if (!file) return;

    delete file;
}

void release_model(void* pModel)
{
    CNBAModel* model = static_cast<CNBAModel*>(pModel);
    if (!model) return;

    delete model;
}

void release_scene(void* pScene)
{
    CNBAScene* scene = static_cast<CNBAScene*>(pScene);
    if (!scene) return;

    delete scene;
}

int getModelTotal(void* pNbaScene)
{
    CNBAScene* scene = static_cast<CNBAScene*>(pNbaScene);

    if (!scene)
        return 0;

    return scene->getNumModels();
}

int getMeshTotal(void* pNbaModel)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model)
    {
        printf("\n[Interface] Failed to load CNBAModel object.");
        return 0;
    }

    /* accumulate mesh total */
    return model->getNumMeshes();
}

const float* getVertexData(void* pNbaModel, const int index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || index >= model->getNumMeshes())
        return nullptr;

    /* Load mesh */
    auto mesh = model->getMesh(index);
    if (!mesh)
        return nullptr;

    // Different alignment for different vertex formats
    if (mesh->vertexComponents == 3) {
        // Characters: apply standard NBA to Blender transformation
        mesh->alignPosition(true, 3);
    }
    else if (mesh->vertexComponents == 4) {
        // Ball: skip alignment or use different transformation
        // The scale/offset transform already positions it correctly
        // Don't call alignPosition for 4-component meshes
    }

    // If 4-component mesh, strip W for Blender
    if (mesh->vertexComponents == 4) {
        static std::vector<float> vertices3D;
        vertices3D.clear();

        for (int i = 0; i < mesh->vertices.size(); i += 4) {
            vertices3D.push_back(mesh->vertices[i]);
            vertices3D.push_back(mesh->vertices[i + 1]);
            vertices3D.push_back(mesh->vertices[i + 2]);
        }
        return vertices3D.data();
    }

    return mesh->vertices.data();
}

int getNumVerts(void* pModel, const int index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pModel);
    if (!model || index >= model->getNumMeshes())
        return 0;

    /* Load mesh */
    auto mesh = model->getMesh(index);
    if (!mesh)
        return 0;

    // Use the stored component count
    return mesh->vertices.size() / mesh->vertexComponents;
}

int getVertexComponents(void* pModel, const int index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pModel);
    if (!model || index >= model->getNumMeshes())
        return 3;  // Default to 3

    auto mesh = model->getMesh(index);
    if (!mesh)
        return 3;

    // Return the stored component count
    return mesh->vertexComponents;
}


int getNumUvChannels(void* pNbaModel, const int index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || index >= model->getNumMeshes())
        return 0;

    /* Load mesh */
    auto mesh = model->getMesh(index);
    if (!mesh)
        return 0;

    return mesh->uvs.size();
}

const float* getMeshUvChannel(void* pNbaModel, const int meshIndex, const int channelIndex)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || meshIndex >= model->getNumMeshes())
        return nullptr;

    /* Load mesh */
    auto mesh = model->getMesh(meshIndex);
    if (!mesh)
        return nullptr;

    auto& uvs = mesh->uvs;

    if (channelIndex >= uvs.size())
        return nullptr;

    auto& channel = uvs.at(channelIndex).map;
    return channel.data();
}

void* getSceneModel(void* pNbaScene, const int index)
{
    CNBAScene* scene = static_cast<CNBAScene*>(pNbaScene);
    if (!scene || index >= scene->getNumModels())
        return nullptr;

    auto model = scene->model(index);
    return model.get();
}

const char* getMeshName(void* pNbaModel, int mesh_index)
{
    // Convert void pointer back to CNbaModel pointer
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || mesh_index >= model->getNumMeshes())
        return "";

    auto mesh = model->getMesh(mesh_index);
    if (!mesh)
        return "";

    return mesh->name.c_str();
}

int getNumTriangles(void* pNbaModel, const int index)
{
    // Convert void pointer back to CNBAModel pointer
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || index >= model->getNumMeshes())
        return 0;

    /* Load mesh */
    auto mesh = model->getMesh(index);
    if (!mesh)
        return 0;

    // Count valid triangles only
    int numVerts = mesh->vertices.size() / mesh->vertexComponents;
    int validTriangles = 0;

    for (const auto& tri : mesh->triangles)
    {
        if (tri[0] < numVerts && tri[1] < numVerts && tri[2] < numVerts)
        {
            validTriangles++;
        }
    }

    return validTriangles;
}

const uint32_t* getMeshTriangleList(void* pNbaModel, const int index) {
    // Convert void pointer back to CNBAModel pointer
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || index >= model->getNumMeshes())
        return nullptr;

    /* Load mesh */
    auto mesh = model->getMesh(index);
    if (!mesh)
        return nullptr;

    // Get actual vertex count
    int numVerts = mesh->vertices.size() / mesh->vertexComponents;

    // Sanitize triangle indices
    static std::vector<uint32_t> sanitized;
    sanitized.clear();
    sanitized.reserve(mesh->triangles.size() * 3);

    int skipped = 0;
    for (const auto& tri : mesh->triangles)
    {
        // Check if all three vertices are valid
        if (tri[0] < numVerts && tri[1] < numVerts && tri[2] < numVerts)
        {
            sanitized.push_back(tri[0]);
            sanitized.push_back(tri[1]);
            sanitized.push_back(tri[2]);
        }
        else
        {
            skipped++;
        }
    }

    if (skipped > 0)
    {
        printf("\n[Interface] WARNING: Skipped %d triangles with invalid vertex indices (max vertex: %d)",
            skipped, numVerts - 1);
    }

    return sanitized.data();
}

const float* getMeshNormals(void* pNbaModel, const int index) {
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || index >= model->getNumMeshes())
        return nullptr;

    auto mesh = model->getMesh(index);
    if (!mesh || mesh->normals.empty())
        return nullptr;

    // ✓ Only align 3-component meshes, skip 4-component entirely
    if (mesh->vertexComponents == 3) {
        mesh->alignNormals(true, 3);  // NBA→Blender
        printf("\n[getMeshNormals] Aligned normals for 3-component mesh");
    }
    else {
        printf("\n[getMeshNormals] Skipped alignment for 4-component mesh");
    }

    return mesh->normals.data();
}

void freeMemory_intArr(int* data)
{
    if (!data) return;
    delete[] data;
}

void freeMemory_float32(float* set)
{
    if (!set) return;
    delete[] set;
}

void freeMemory_charArrPtr(const char** set)
{
    if (!set) return;
    delete[] set;
}

void freeMemory_skinData(void* pSkinData)
{
    // ...
}

int getNumBones(void* pNbaModel)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model) return 0;

    auto skel = model->getSkeleton();
    return skel.joints.size();
}

inline static int findParent(const std::vector<std::shared_ptr<NSJoint>>& joints, const std::string& id)
{
    for (auto& joint : joints)
    {
        for (auto& child : joint->children)
            if (child->name == id)
                return joint->index;
    }

    return -1;
}

int getBoneParentIndex(void* pNbaModel, int joint_index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model) return -1;

    auto& skel = model->getSkeleton();
    if (joint_index >= skel.joints.size())
        return -1;

    auto& joint = skel.joints[joint_index];

    return (joint->parent) ? joint->parent->index : -1;
}

float* getBoneMatrix(void* pNbaModel, int joint_index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model)
        return nullptr;

    auto& skel = model->getSkeleton();
    if (joint_index >= skel.joints.size())
        return nullptr;

    auto& joint = skel.joints[joint_index];
    float* matrix = new float[16];

    matrix[0] = joint->translate.x;
    matrix[1] = joint->translate.y;
    matrix[2] = joint->translate.z;
    return matrix;
}

const char* getBoneName(void* pNbaModel, int joint_index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model)
        return "";

    auto& skel = model->getSkeleton();
    if (joint_index >= skel.joints.size())
        return "";

    auto& joint = skel.joints[joint_index];
    return joint->name.c_str();
}

void* getSkinData(void* pNbaModel, int mesh_index)
{
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model)
        return nullptr;

    if (mesh_index >= model->getNumMeshes())
        return nullptr;

    auto mesh = model->getMesh(mesh_index);
    if (!mesh)
        return nullptr;

    return &mesh->skin;
}

inline bool hasGroup(const std::vector<std::string*>& vec, const std::string* target)
{
    for (auto& string : vec)
        if (*string == *target)
            return true;

    return false;
}

const char** getAllSkinGroups(void* pSkin, int* num_groups)
{
    Skin* skin = static_cast<Skin*>(pSkin);
    if (!pSkin)
        return nullptr;

    // Get all bones from skin
    std::vector<std::string*> groups;
    for (auto& vertex : skin->blendverts)
    {
        for (auto& bone : vertex.bones)
            if (!hasGroup(groups, &bone))
                groups.push_back(&bone);
    }

    // Convert std::vector<std::string> to array of char pointers
    *num_groups = static_cast<int>(groups.size());
    const char** arr = new const char* [*num_groups];
    for (size_t i = 0; i < *num_groups; ++i)
    {
        arr[i] = groups[i]->c_str();
    }

    return arr;
}

float* getAllJointWeights(void* pSkin, const char* joint_name, int* size)
{
    Skin* skin = static_cast<Skin*>(pSkin);
    if (!pSkin)
        return nullptr;

    int numVerts = skin->blendverts.size();
    float* vtxWeights = new float[numVerts];

    // Iterate through skin data and get a weight list for all verts of specified bone..
    for (int i = 0; i < numVerts; i++)
    {
        auto& skinVtx = skin->blendverts.at(i);
        int numBones = skinVtx.bones.size();
        vtxWeights[i] = 0.0f;

        for (int j = 0; j < numBones; j++)
            if (skinVtx.bones.at(j) == joint_name)
            {
                vtxWeights[i] = skinVtx.weights.at(j);
                break;
            }
    }

    *size = numVerts;
    return vtxWeights;
}

const char** getAllFaceGroups(void* pNbaModel, const int meshIndex, int* size)
{
    // Convert void pointer back to CSkinModel pointer
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || meshIndex >= model->getNumMeshes())
        return nullptr;

    /* Load mesh */
    auto mesh = model->getMeshes().at(meshIndex);

    // Collect all group names
    std::vector<std::string*> groups;
    for (auto& faceGrp : mesh->groups)
    {
        groups.push_back(&faceGrp.name);
    }

    // Convert std::vector<std::string> to array of char pointers
    *size = static_cast<int>(groups.size());
    const char** arr = new const char* [*size];
    for (size_t i = 0; i < *size; ++i) {
        arr[i] = groups[i]->c_str();
    }

    return arr;
}

void getMaterialFaceGroup(void* pNbaModel, const int meshIndex, const int groupIndex, int* faceBegin, int* faceSize)
{
    /* Define default values*/
    *faceBegin = -1;
    *faceSize = -1;

    // Convert void pointer back to CSkinModel pointer
    CNBAModel* model = static_cast<CNBAModel*>(pNbaModel);
    if (!model || meshIndex >= model->getNumMeshes())
        return;

    /* Load mesh */
    auto mesh = model->getMeshes().at(meshIndex);
    if (groupIndex >= mesh->groups.size())
        return;

    FaceGroup& group = mesh->groups.at(groupIndex);
    *faceBegin = group.begin / 3;
    *faceSize = group.count / 3;
}