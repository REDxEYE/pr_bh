#include <optional>

#include "pr_module.hpp"

#include <filesystem>
#include <pragma/lua/luaapi.h>
#include <pragma/console/conout.h>
#include <luainterface.hpp>

#include <pragma/game/game.h>
#include <fsys/filesystem.h>
#include <pragma/model/modelmesh.h>
#include <pragma/model/model.h>
#include <fsys/ifile.hpp>
#include <pragma/logging.hpp>
#include <sharedutils/util_ifile.hpp>
#include <pragma/lua/converters/game_type_converters_t.hpp>

#include "pragma/asset/util_asset.hpp"
#include "pragma/asset_types/world.hpp"
#include <pragma/networkstate/networkstate.h>
#include <glm/gtx/matrix_decompose.hpp>

static spdlog::logger &LOGGER = pragma::register_logger("bionicle");

struct Header {
	char m_ident[4];
	int32_t m_size;
	uint32_t m_version;
	uint32_t m_unused;
};

template<typename T>
void read(ufile::IFile &f, T &t)
{
	f.Read(&t, sizeof(T));
}

template<typename T>
T read(ufile::IFile &f)
{
	T t;
	f.Read(&t, sizeof(T));
	return t;
}

std::string read_string(ufile::IFile &f, uint32_t len)
{
	char *tmp = (char *)malloc(len);
	f.Read(tmp, len);
	std::string tmp2(tmp, len);
	return std::move(tmp2);
}


struct ContainerHeader {
	uint32_t type;
	uint32_t unk[3];
	uint32_t flags;
};

struct Particle {
	Vector3 m_pos;
	Vector2 m_scale;
	Color m_color;

	Particle(ufile::IFile &file)
	{
		read(file, m_pos);
		read(file, m_scale);
		uint8_t color[4];
		file.Read(color, 4);
		m_color.r = color[2];
		m_color.g = color[1];
		m_color.b = color[0];
		m_color.a = color[3];
	}
};

struct ParticleEntry {
	uint32_t m_material_id;

	std::vector<Particle> m_particles;

	ParticleEntry(ufile::IFile &file)
	{
		file.Seek(8, ufile::IFile::Whence::Cur);
		read(file, m_material_id);
		file.Seek(8, ufile::IFile::Whence::Cur);
		uint32_t count = read<uint32_t>(file);
		file.Seek(4, ufile::IFile::Whence::Cur);
		m_particles.reserve(count);
		for(int i = 0; i < count; ++i) {
			m_particles.emplace_back(file);
		}
	}

};

struct Strip {
	uint32_t unk0;
	uint16_t indices_count;
	uint16_t indices_count_dup;
	uint8_t unk2[44];
	uint32_t unk3;
	uint32_t unk4;
	uint32_t unk5;
	uint32_t unk6;
	uint32_t indices_offset;
	uint32_t unk7;
};

struct ModelEntry {
	std::vector<Strip> m_strips;
	std::vector<uint32_t> m_vertex_blocks;

	uint32_t m_material_id;
	uint32_t m_vertex_count;
	uint32_t m_vertex_size;

	ModelEntry(ufile::IFile &file)
	{
		uint32_t start_offset = file.Tell();
		file.Seek(8, ufile::IFile::Whence::Cur);
		read(file, m_material_id);
		file.Seek(4, ufile::IFile::Whence::Cur);
		read(file, m_vertex_count);
		file.Seek(8, ufile::IFile::Whence::Cur);
		file.Seek(16, ufile::IFile::Whence::Cur);
		uint32_t strip_offset = file.Tell() + read<uint32_t>(file);
		file.Seek(4 * 5, ufile::IFile::Whence::Cur);
		uint32_t vertex_block_count = read<uint32_t>(file);
		m_vertex_blocks.reserve(vertex_block_count);
		for(int i = 0; i < 9; ++i) {
			if(i < vertex_block_count) {
				m_vertex_blocks.emplace_back(read<uint32_t>(file));
				continue;
			}
			file.Seek(4, ufile::IFile::Whence::Cur);

		}

		file.Seek(28, ufile::IFile::Whence::Cur);
		read(file, m_vertex_size);
		file.Seek(12, ufile::IFile::Whence::Cur);
		while(strip_offset > 0) {
			file.Seek(strip_offset);
			read(file, strip_offset);
			Strip &strip = m_strips.emplace_back();
			read(file, strip);
		}
	}
};

struct Container {
	ContainerHeader m_header;
	std::vector<ParticleEntry> m_particles;
	std::vector<ModelEntry> m_models;

	Vector3 bbox_min;
	Vector3 bbox_max;

	Container(ufile::IFile &file)
	{
		read(file, m_header);
		if(m_header.flags) {
			if(m_header.flags > 0 && m_header.flags <= 2) {
				uint32_t particle_count = read<uint32_t>(file);
				file.Seek(8, ufile::IFile::Whence::Cur);
				m_particles.reserve(particle_count);
				for(int i = 0; i < particle_count; ++i) {
					m_particles.emplace_back(file);
				}
			}
		}
		else {
			uint32_t model_count = read<uint32_t>(file);
			file.Seek(24, ufile::IFile::Whence::Cur);
			m_models.reserve(model_count);
			for(int i = 0; i < model_count; ++i) {
				m_models.emplace_back(file);
			}
		}
		read(file, bbox_min);
		read(file, bbox_max);
		file.Seek(8, ufile::IFile::Whence::Cur);
	}
};

struct BHMaterial {
	uint32_t unk0[16];
	uint32_t flags;
	uint32_t unk1[4];
	uint8_t color[4];
	uint32_t unk2[24];
	uint32_t unk_flags;
	uint32_t texture_ids[4];
	uint32_t unk3[59];
	uint32_t vertex_format;
	uint32_t unk4[22];
};

static_assert(sizeof(BHMaterial) == 532);

struct BufferHeader {
	uint32_t size, id, offset;
};

struct VBuffer {
	BufferHeader header;
	std::vector<uint8_t> data;
};

struct IBuffer {
	BufferHeader header;
	std::vector<uint16_t> indices;
};

struct VertexIndexHeader {
	uint32_t vertex_buffer_count;
	uint32_t index_buffer_count;
	uint32_t total_buffer_size;
	uint32_t vertex_blocks_offset, vertex_buffer_offset, vertex_buffer_size;
	uint32_t index_blocks_offset, index_buffer_offset, index_buffer_size;
	uint32_t unk;
};

struct Instance {
	Mat4x4 matrix;
	uint32_t mesh_id;
	uint32_t flags;
	uint32_t unk0, unk1;
};

class NupFile {
	std::vector<Container> m_containers;
	std::vector<BHMaterial> m_materials;
	std::vector<VBuffer> m_vbuffers;
	std::vector<IBuffer> m_ibuffers;
	std::vector<Instance> m_instances;
	std::vector<char> m_string_table;

public:
#define INVALID_IF_NO_VALUE(opt)\
	if(!(opt).has_value()) {\
	  m_valid = false;\
	  return;\
	}

	NupFile(ufile::IFile &file)
	{
		file.Seek(0);
		m_valid = true;
		std::optional<uint32_t> chunk_offset;

		chunk_offset = FindChunkOffset(file, "NTBL");
		INVALID_IF_NO_VALUE(chunk_offset)

		file.Seek(chunk_offset.value());
		uint32_t string_table_size = read<uint32_t>(file);
		m_string_table.resize(string_table_size);
		file.Read(m_string_table.data(), string_table_size);

		chunk_offset = FindChunkOffset(file, "OBJ0");
		INVALID_IF_NO_VALUE(chunk_offset)
		file.Seek(chunk_offset.value());
		uint32_t container_count = read<uint32_t>(file);
		file.Seek(4, ufile::IFile::Whence::Cur);
		m_containers.reserve(container_count);
		for(uint32_t i = 0; i < container_count; i++) {
			m_containers.emplace_back(file);
		}
		LOGGER.info("Loaded {} containers", m_containers.size());

		chunk_offset = FindChunkOffset(file, "MS00");
		INVALID_IF_NO_VALUE(chunk_offset)
		file.Seek(chunk_offset.value());
		uint32_t material_count = read<uint32_t>(file);
		file.Seek(4, ufile::IFile::Whence::Cur);
		for(uint32_t i = 0; i < material_count; i++) {
			BHMaterial &material = m_materials.emplace_back();
			read(file, material);
		}
		LOGGER.info("Loaded {} materials", m_materials.size());

		chunk_offset = FindChunkOffset(file, "VBIB");
		INVALID_IF_NO_VALUE(chunk_offset)
		file.Seek(chunk_offset.value());
		VertexIndexHeader vertex_index_header;
		read(file, vertex_index_header);
		for(int i = 0; i < vertex_index_header.vertex_buffer_count; ++i) {
			file.Seek(chunk_offset.value() + vertex_index_header.vertex_blocks_offset + sizeof(BufferHeader) * i);
			VBuffer &buffer = m_vbuffers.emplace_back();
			read(file, buffer.header);
			file.Seek(chunk_offset.value() + buffer.header.offset + vertex_index_header.vertex_buffer_offset);
			buffer.data.resize(buffer.header.size);
			file.Read(buffer.data.data(), buffer.header.size);
		}
		for(int i = 0; i < vertex_index_header.index_buffer_count; ++i) {
			file.Seek(chunk_offset.value() + vertex_index_header.index_blocks_offset + sizeof(BufferHeader) * i);
			IBuffer &buffer = m_ibuffers.emplace_back();
			read(file, buffer.header);
			file.Seek(chunk_offset.value() + buffer.header.offset + vertex_index_header.index_buffer_offset);
			buffer.indices.resize(buffer.header.size / 2);
			file.Read(buffer.indices.data(), buffer.header.size);
		}
		LOGGER.info("Loaded {} buffers", m_vbuffers.size() + m_ibuffers.size());

		chunk_offset = FindChunkOffset(file, "INST");
		INVALID_IF_NO_VALUE(chunk_offset)
		file.Seek(chunk_offset.value());
		uint32_t instance_count = read<uint32_t>(file);
		file.Seek(4, ufile::IFile::Whence::Cur);
		m_instances.reserve(instance_count);
		for(int i = 0; i < instance_count; ++i) {
			Instance &instance = m_instances.emplace_back();
			read(file, instance);
		}
		LOGGER.info("Loaded {} instances", m_instances.size());
	}

	const std::vector<Container> &GetContainers() const { return m_containers; }
	const std::vector<Instance> &GetInstances() const { return m_instances; }

	const VBuffer &GetVertexBuffer(uint32_t id) const { return m_vbuffers[id]; }
	const IBuffer &GetIndexBuffer(uint32_t id) const { return m_ibuffers[id]; }
	const Instance &GetInstance(uint32_t id) const { return m_instances[id]; }

	bool m_valid = false;
private:
	std::optional<uint32_t> FindChunkOffset(ufile::IFile &file, std::string name)
	{
		file.Seek(sizeof(Header));
		while(file.Tell() < file.GetSize()) {

			std::string chunk_name = read_string(file, 4);
			uint32_t size = read<uint32_t>(file);
			if(chunk_name == name) {
				return file.Tell();
			}
			file.Seek(size - 8, ufile::IFile::Whence::Cur);
		}
		LOGGER.warn("Failed to find {} chunk", name);
		return std::nullopt;
	}


};

struct Tri {
	uint16_t a, b, c;
};

static std::vector<Tri> unstripify(const std::vector<uint16_t> &tristrip, uint32_t offset, uint32_t count)
{
	std::vector<Tri> trilist;
	for(int i = offset + 2; i < offset + count; ++i) {
		uint16_t v0, v1, v2;
		if(i % 2 == 0) {
			v0 = tristrip[i - 2];
			v1 = tristrip[i - 1];
			v2 = tristrip[i];
		}
		else {
			v0 = tristrip[i];
			v1 = tristrip[i - 1];
			v2 = tristrip[i - 2];
		}
		if(v0 == v1 or v1 == v2)
			continue;

		trilist.emplace_back(v0, v1, v2);
	}

	return std::move(trilist);
}

void export_instances(Game &game, const NupFile &nup, const std::string &mapName)
{
	auto &containers = nup.GetContainers();
	for(int i = 0; i < containers.size(); ++i) {
		const auto &container = containers[i];
		auto mdl = game.CreateModel();
		auto meshGroup = mdl->GetMeshGroup(0);
		if(container.m_models.empty()) {
			continue;
		}
		for(auto model : container.m_models) {
			assert(model.m_strips.size() == 1);
			assert(model.m_vertex_blocks.size() == 1);

			Strip strip = model.m_strips.front();
			uint32_t vertex_block_id = model.m_vertex_blocks.front();

			const VBuffer &vertex_buffer = nup.GetVertexBuffer(vertex_block_id);
			const IBuffer &index_buffer = nup.GetIndexBuffer(0);

			auto trilist = unstripify(index_buffer.indices, strip.indices_offset, strip.indices_count);
			auto subMesh = game.CreateModelSubMesh();
			subMesh->ReserveVertices(model.m_vertex_count);
			uint8_t *vertex_data = (uint8_t *)vertex_buffer.data.data();
			for(int i = 0; i < model.m_vertex_count; ++i) {
				Vector3 pos = *(Vector3 *)vertex_data;
				subMesh->AddVertex(umath::Vertex{{pos.x, -pos.y, pos.z}, {0.0, 0.0}, {}, {}});
				vertex_data += model.m_vertex_size;
			}
			subMesh->ReserveIndices(trilist.size() * 3);
			for(auto tri : trilist) {
				subMesh->AddTriangle(tri.a, tri.b, tri.c);
			}

			subMesh->GenerateNormals();
			auto mesh = game.CreateModelMesh();
			mesh->AddSubMesh(subMesh);
			meshGroup->AddMesh(mesh);
		}
		mdl->AddTexturePath("");
		mdl->GetMetaInfo().textures.push_back("white");
		mdl->GetTextureGroup(0)->textures.push_back(0);
		mdl->Update();
		std::string mdlName = std::format("{}/instance_{:04}", mapName, i);
		std::string errMsg;
		auto result = mdl->Save(game, std::string{"addons/bionicle/"} + pragma::asset::get_asset_root_directory(pragma::asset::Type::Model) + "/" + mdlName, errMsg);
		if(!errMsg.empty())
			LOGGER.error(errMsg);
	}
}


void load_nup(Game &game, NetworkState &nw, const std::string &pathToNup)
{
	auto fp = filemanager::open_system_file(pathToNup, filemanager::FileMode::Read | filemanager::FileMode::Binary);
	if(!fp) {
		Con::cerr << "Failed to load model" << Con::endl;
		return; // TODO: Error handling
	}
	fsys::File f{fp};

	Header header;
	read(f, header);
	LOGGER.info("Header(ident: {}, size:{}, version:{})", std::string(header.m_ident, 4), -header.m_size, header.m_version);
	if(std::string(header.m_ident, 4) != "NU20") {
		return;
	}
	NupFile nup_file(f);
	std::string map_name = std::filesystem::path(pathToNup).stem().string();

	if(!nup_file.m_valid) {
		LOGGER.error("Loaded NUP file is invalid");
		return;
	}

	export_instances(game, nup_file, map_name);

	auto worldData = pragma::asset::WorldData::Create(nw);
	worldData->GetMaterialTable().push_back("white");

	// auto world_ent = pragma::asset::EntityData::Create();
	// world_ent->SetClassName("world");
	// world_ent->SetKeyValue("skyname", "sky_day01_01");
	// auto baseHash = std::hash<std::string>{}(map_name + "_world");
	// world_ent->SetKeyValue("uuid", util::uuid_to_string(util::generate_uuid_v4(baseHash)));
	// worldData->AddEntity(*world_ent);

	auto ent = pragma::asset::EntityData::Create();
	ent->SetClassName("prop_dynamic");
	ent->SetKeyValue("uuid", "553e839c-868f-40d8-944a-82d08114bf90");
	ent->SetKeyValue("model", "maps/stage/world_1");
	worldData->AddEntity(*ent);

	auto spawn_ent = pragma::asset::EntityData::Create();
	spawn_ent->SetClassName("info_player_start");
	auto baseHash = std::hash<std::string> {}(map_name + "_world");
	spawn_ent->SetKeyValue("uuid", util::uuid_to_string(util::generate_uuid_v4(baseHash)));
	worldData->AddEntity(*spawn_ent);

	const auto &instances = nup_file.GetInstances();
	for(int i = 0; i < instances.size(); ++i) {
		const auto &instance = instances[i];

		umath::Transform t{instance.matrix};
		// Vector3 pos = {t.GetOrigin().x, t.GetOrigin().z, t.GetOrigin().y};
		// Quat rot = {-t.GetRotation().x, -t.GetRotation().z, -t.GetRotation().y, t.GetRotation().w};
		Vector3 pos = {t.GetOrigin().x, t.GetOrigin().y, -t.GetOrigin().z};
		Quat rot = {-t.GetRotation().x, -t.GetRotation().y, t.GetRotation().z, t.GetRotation().w};

		auto ent = pragma::asset::EntityData::Create();
		ent->SetOrigin(pos);
		ent->SetRotation(rot);
		ent->SetClassName("prop_dynamic");
		auto baseHash = std::hash<std::string>{}(map_name + "_" + std::to_string(i++));
		ent->SetKeyValue("uuid", util::uuid_to_string(util::generate_uuid_v4(baseHash)));
		uint32_t mesh_id = instance.mesh_id;
		if(mesh_id > nup_file.GetContainers().size()) {
			mesh_id &= 0x000FFFFF;
		}
		std::string mdlName = std::format("{}/instance_{:04}", map_name, mesh_id);
		std::string errMsg;

		ent->SetKeyValue("model", mdlName); // Without extension
		worldData->AddEntity(*ent);
	}

	std::string mapFilePath = std::string{"addons/bionicle/"} + pragma::asset::get_asset_root_directory(pragma::asset::Type::Map) + "/" + map_name;

	auto udmData = udm::Data::Create();
	auto assetData = udmData->GetAssetData();
	std::string errMsg;
	auto res = worldData->Save(assetData, ufile::get_file_from_filename(mapFilePath), errMsg);
	if(!res)
		return;
	bool binary = false;
	auto ext = pragma::asset::get_udm_format_extension(pragma::asset::Type::Map, binary);
	assert(ext.has_value());
	mapFilePath += "." + *ext;
	filemanager::create_path(ufile::get_path_from_filename(mapFilePath));
	try {
		if(binary) {
			res = udmData->Save(mapFilePath);
		}
		else {
			res = udmData->SaveAscii(mapFilePath, udm::AsciiSaveFlags::IncludeHeader);
		}
	}
	catch(const udm::Exception &e) {
		errMsg = "Failed to save map file '" + mapFilePath + "': " + std::string{e.what()};
	}
	if(!res)
		return;
}

extern "C" {
// Called after the module has been loaded
DLLEXPORT bool pragma_attach(std::string &outErr)
{
	// Return true to indicate that the module has been loaded successfully.
	// If the module could not be loaded properly, return false and populate 'outErr' with a descriptive error message.
	Con::cout << "Custom module \"pr_bh\" has been loaded!" << Con::endl;
	return true;
}

// Called when the module is about to be unloaded
DLLEXPORT void pragma_detach()
{
	Con::cout << "Custom module \"pr_bh\" is about to be unloaded!" << Con::endl;
}

// Lua bindings can be initialized here
DLLEXPORT void pragma_initialize_lua(Lua::Interface &lua)
{
	auto &libDemo = lua.RegisterLibrary("pr_bh");
	libDemo[luabind::def("load_nup", &load_nup)];

	struct DemoClass {
		DemoClass()
		{
		}

		void PrintWarning(const std::string &msg)
		{
			Con::cwar << msg << Con::endl;
		}
	};

	auto classDef = luabind::class_<DemoClass>("DemoClass");
	classDef.def(luabind::constructor<>());
	classDef.def("PrintWarning", &DemoClass::PrintWarning);
	libDemo[classDef];
}

// Called when the Lua state is about to be closed.
DLLEXPORT void pragma_terminate_lua(Lua::Interface &lua)
{
}
};
