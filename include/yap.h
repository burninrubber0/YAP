#include <argparse/argparse.hpp>
#include <gamedata-stream.h>
#include <libdeflate.h>
#include <yaml-cpp/yaml.h>
#include <QDateTime>
#include <QLocale>
#include <QDebug>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <string>

class YAP
{
public:
	YAP(int argc, char* argv[]);
	~YAP();
	int result = 0;

private:
	struct ImportEntry
	{
		uint64_t id = 0;
		uint32_t offset = 0;
	};

	struct ResourceEntry
	{
		uint64_t id = 0;
		uint64_t importsHash = 0;
		uint32_t uncompressedInfo[3] = { 0, 0, 0 }; // Size mask 0x0FFFFFFF, alignment mask 0xF0000000
		uint32_t compressedSize[3] = { 0, 0, 0 };
		uint32_t offset[3] = { 0, 0, 0 };
		uint32_t importsOffset = 0;
		uint32_t type = 0;
		uint16_t importCount = 0;
		uint8_t flags = 0;
		uint8_t stream = 0;

		QList<ImportEntry> imports;
	};

	struct Bundle
	{
		enum class Flags
		{
			IsCompressed = 0x1,
			IsMainMemOptimised = 0x2,
			IsGraphicsMemOptimised = 0x4,
			ContainsDebugData = 0x8
		};
		
		QString magic;
		uint32_t version = 0;
		uint32_t platform = 0;
		uint32_t debugData = 0;
		uint32_t resourceCount = 0;
		uint32_t resourceEntries = 0;
		uint32_t resourceData[3] = { 0, 0, 0 };
		uint32_t flags = 0;

		QList<ResourceEntry> entries;
	};

	const std::string version = "0.2-adriwin";
	const std::string date = __DATE__;
	QString mode;
	QString inPath;
	QString outPath;
	QStringList fileExtensions;
	bool dirMode = false;
	bool doNotSortByType = false;
	bool combineImports = false;
	uint16_t defaultPrimaryAlignment = 0x10;
	uint16_t defaultSecondaryAlignment = 0x80;
	argparse::ArgumentParser* args = nullptr;
	libdeflate_decompressor* dc = nullptr;
	libdeflate_compressor* cmp = nullptr;
	const QString debugDataFilename = ".debug.xml";
	const QString importsFilename = ".imports.yaml";
	const QString metadataFilename = ".meta.yaml";
	const QString defaultSuffix = ".dat";
	const QString primarySuffix = "_header.dat";
	const QString secondarySuffix = "_body.dat";
	const QString importsSuffix = "_imports.yaml";
	QList<QStringList> resourceFiles; // [0]=primary, [1]=secondary, [2]=imports
	YAML::Node combinedImports;

	void setupArgs();
	bool readArgs(int argc, char* argv[]);
	bool validateArgs();
	bool validateExtractArgs();
	bool validateCreateArgs();
	bool validateMetadata();
	bool validateBundleMetadata(YAML::Node& meta);
	bool validateResourceMetadata(YAML::Node& meta);
	bool validateImports(YAML::Node& meta);
	bool validateResourceIdKey(std::string resourceKey, uint64_t& id);
	//bool resourceHasDuplicateKey(YAML::Node list, uint64_t id, std::string resourceKey);
	//bool importHasDuplicateKey(YAML::Node list, uint32_t offset, std::string importKey);
	void setShaderTypeName(GameDataStream& stream);
	void createDecompressor();
	void createCompressor();

	int extractDir();
	int extract();
	bool validateBundle(GameDataStream& stream);
	void readBundle(GameDataStream& stream, Bundle& bundle);
	void readResourceEntry(GameDataStream& stream, Bundle& bundle, int index);
	bool validateResourceEntries(Bundle& bundle);
	void extractResource(GameDataStream& stream, Bundle& bundle, int index);
	QString generateFilePath(ResourceEntry& entry, int memType);
	void outputResource(char* resource, int length, QString path);
	void outputImports(Bundle& bundle, int resIndex);
	void outputDebugData(GameDataStream& stream, Bundle& bundle);
	void outputMetadata(Bundle& bundle);

	int createDir();
	int create();
	void createBundle(GameDataStream& stream, YAML::Node& meta, Bundle& bundle);
	void setPlatform(GameDataStream& stream, Bundle bundle);
	void createResourceEntry(YAML::const_iterator& resource, Bundle& bundle, int index);
	static bool compareResourceEntry(const ResourceEntry& a, const ResourceEntry& b);
	static bool compareResourceFileList(const QStringList& a, const QStringList& b);
	void createResource(QByteArray& data, Bundle& bundle, int index, int memType, GameDataStream::Platform platform);
	void outputBundle(GameDataStream& stream, Bundle& bundle, QByteArray data[]);

	// Don't particularly like this but it lets people use hex
	template<typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
	bool stringToUInt(QString in, T& out, bool critical, T defaultVal = 0)
	{
		try
		{
			if (in.startsWith("0x"))
				out = std::stoull(in.toStdString(), nullptr, 16);
			else
				out = std::stoull(in.toStdString());
		}
		catch (std::exception e)
		{
			if (!critical)
			{
				qWarning().noquote().nospace() << "Invalid value " << in
					<< ", setting default " << defaultVal << ".";
				out = defaultVal;
			}
			else
			{
				qCritical().noquote().nospace() << "Invalid value " << in
					<< ". Aborting.";
				return false;
			}
		}
		return true;
	}

	QMap<uint32_t, QString> resourceTypes = {
		{ 0x0, "Texture" },
		{ 0x1, "Material" },
		{ 0x2, "RenderableMesh" },
		{ 0x3, "TextFile" },
		{ 0x4, "DrawIndexParams" },
		{ 0x5, "IndexBuffer" },
		{ 0x6, "MeshState" },
		{ 0x7, "TextureAuxInfo" },
		{ 0x8, "VertexBufferItem" },
		{ 0x9, "VertexBuffer" },
		{ 0xA, "VertexDescriptor" },
		{ 0xB, "MaterialCRC32" },
		{ 0xC, "Renderable" },
		{ 0xD, "MaterialTechnique" },
		{ 0xE, "TextureState" },
		{ 0xF, "MaterialState" },
		{ 0x10, "DepthStencilState" },
		{ 0x11, "RasterizerState" },
		{ 0x12, "ShaderProgramBuffer" },
		{ 0x13, "RenderTargetState" },
		{ 0x14, "ShaderParameter" },
		{ 0x15, "RenderableAssembly" },
		{ 0x16, "Debug" },
		{ 0x17, "KdTree" },
		{ 0x18, "VoiceHierarchy" },
		{ 0x19, "Snr" },
		{ 0x1A, "InterpreterData" },
		{ 0x1B, "AttribSysSchema" },
		{ 0x1C, "AttribSysVault" },
		{ 0x1D, "EntryList" },
		{ 0x1E, "AptData" },
		{ 0x1F, "GuiPopup" },
		{ 0x21, "Font" },
		{ 0x22, "LuaCode" },
		{ 0x23, "InstanceList" },
		{ 0x24, "ClusteredMesh" },
		{ 0x25, "IdList" },
		{ 0x26, "InstanceCollisionList" },
		{ 0x27, "Language" },
		{ 0x28, "SatNavTile" },
		{ 0x29, "SatNavTileDirectory" },
		{ 0x2A, "Model" },
		{ 0x2B, "ColourCube" },
		{ 0x2C, "HudMessage" },
		{ 0x2D, "HudMessageList" },
		{ 0x2E, "HudMessageSequence" },
		{ 0x2F, "HudMessageSequenceDictionary" },
		{ 0x30, "WorldPainter2D" },
		{ 0x31, "PFXHookBundle" },
		//{ 0x32, "ShaderTechnique" },
		{ 0x32, "Shader" },
		{ 0x40, "RawFile" },
		{ 0x41, "ICETakeDictionary" },
		{ 0x42, "VideoData" },
		{ 0x43, "PolygonSoupList" },
		{ 0x44, "DeveloperList" },
		{ 0x45, "CommsToolListDefinition" },
		{ 0x46, "CommsToolList" },
		{ 0x50, "BinaryFile" },
		{ 0x51, "AnimationCollection" },
		{ 0x2710, "CharAnimBankFile" },
		{ 0x2711, "WeaponFile" },
		{ 0x343E, "VFXFile" },
		{ 0x343F, "BearFile" },
		{ 0x3A98, "BkPropInstanceList" },
		{ 0xA000, "Registry" },
		{ 0xA010, "GenericRwacFactoryConfiguration" },
		{ 0xA020, "GenericRwacWaveContent" },
		{ 0xA021, "GinsuWaveContent" },
		{ 0xA022, "AemsBank" },
		{ 0xA023, "Csis" },
		{ 0xA024, "Nicotine" },
		{ 0xA025, "Splicer" },
		{ 0xA026, "FreqContent" },
		{ 0xA027, "VoiceHierarchyCollection" },
		{ 0xA028, "GenericRwacReverbIRContent" },
		{ 0xA029, "SnapshotData" },
		{ 0xB000, "ZoneList" },
		{ 0xC001, "VFX" },
		{ 0x10000, "LoopModel" },
		{ 0x10001, "AISections" },
		{ 0x10002, "TrafficData" },
		{ 0x10003, "TriggerData" },
		{ 0x10004, "DeformationModel" },
		{ 0x10005, "VehicleList" },
		{ 0x10006, "GraphicsSpec" },
		{ 0x10007, "PhysicsSpec" },
		{ 0x10008, "ParticleDescriptionCollection" },
		{ 0x10009, "WheelList" },
		{ 0x1000A, "WheelGraphicsSpec" },
		{ 0x1000B, "TextureNameMap" },
		{ 0x1000C, "ICEList" },
		{ 0x1000D, "ICEData" },
		{ 0x1000E, "ProgressionData" },
		{ 0x1000F, "PropPhysics" },
		{ 0x10010, "PropGraphicsList" },
		{ 0x10011, "PropInstanceData" },
		{ 0x10012, "EnvironmentKeyframe" },
		{ 0x10013, "EnvironmentTimeLine" },
		{ 0x10014, "EnvironmentDictionary" },
		{ 0x10015, "GraphicsStub" },
		{ 0x10016, "StaticSoundMap" },
		{ 0x10017, "PFXHookBundle" },
		{ 0x10018, "StreetData" },
		{ 0x10019, "VFXMeshCollection" },
		{ 0x1001A, "MassiveLookupTable" },
		{ 0x1001B, "VFXPropCollection" },
		{ 0x1001C, "StreamedDeformationSpec" },
		{ 0x1001D, "ParticleDescription" },
		{ 0x1001E, "PlayerCarColours" },
		{ 0x1001F, "ChallengeList" },
		{ 0x10020, "FlaptFile" },
		{ 0x10021, "ProfileUpgrade" },
		{ 0x10022, "OfflineChallengeList" },
		{ 0x10023, "VehicleAnimation" },
		{ 0x10024, "BodypartRemapData" },
		{ 0x10025, "LUAList" },
		{ 0x10026, "LUAScript" },
		{ 0x11000, "BkSoundWeapon" },
		{ 0x11001, "BkSoundGunsu" },
		{ 0x11002, "BkSoundBulletImpact" },
		{ 0x11003, "BkSoundBulletImpactList" },
		{ 0x11004, "BkSoundBulletImpactStream" }
	};
};
