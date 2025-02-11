#include <yap.h>
#include <QBuffer>
#include <QByteArray>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <cmath>

int YAP::createDir()
{
	QDir dir(inPath);
	QDir outDir(outPath);

	QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	if (entries.isEmpty())
	{
		qWarning() << "No folders found in" << inPath;
		return 1;
	}

	for (const QString& folder : entries)
	{
		QString folderPath = dir.filePath(folder);
		
		// Check if folder contains .meta.yaml
		if (!QFile::exists(folderPath + "/.meta.yaml"))
		{
			qInfo() << "Skipping" << folder << "(missing .meta.yaml)";
			continue;
		}

		// Extract extension from folder name
		QString outputName = folder;
		QString ext;
		int lastUnderscore = folder.lastIndexOf('_');
		if (lastUnderscore != -1)
		{
			ext = folder.mid(lastUnderscore + 1);
			outputName = folder.left(lastUnderscore);
		}

		// Skip if extension filter is set and doesn't match
		if (!fileExtensions.isEmpty() && !fileExtensions.contains(ext, Qt::CaseInsensitive))
		{
			qInfo() << "Skipping" << folder << "(extension mismatch)";
			continue;
		}

		if (!ext.startsWith("."))
			ext.prepend(".");

		QString outputBundle = outDir.filePath(outputName + ext);
		
		qInfo() << "Creating bundle for" << folder << "...";
		
		// Store current paths
		QString savedInPath = inPath;
		QString savedOutPath = outPath;
		
		// Set paths for this folder
		inPath = folderPath + "/";
		outPath = outputBundle;

		// Clear previous resource files before creating new bundle
		resourceFiles.clear();
		
		// Create bundle
		int result = create();
		if (result != 0)
			qWarning() << "Failed to create bundle for" << folder;
		
		// Restore paths
		inPath = savedInPath;
		outPath = savedOutPath;
	}

	return 0;
}

int YAP::create()
{
	QFile file(outPath);
	GameDataStream stream(&file);
	YAML::Node meta = YAML::LoadFile((inPath + metadataFilename).toStdString());
	Bundle bundle;
	createBundle(stream, meta, bundle);

	// Validate metadata first - this populates resourceFiles, and only do that if we're in directory mode, otherwise we get "Primary portion has a duplicate file"
	if (dirMode)
		if (!validateMetadata()) return 1;

	// Now we can safely create resource entries
	// We where getting an out of range when creating multiple bundles from folders before doing this
	int index = 0;
	for (YAML::const_iterator resource = meta["resources"].begin();
		resource != meta["resources"].end(); ++resource, ++index)
		createResourceEntry(resource, bundle, index);

	std::cout << '\n';
	std::sort(bundle.entries.begin(), bundle.entries.end(), compareResourceEntry);
	std::sort(resourceFiles.begin(), resourceFiles.end(), compareResourceFileList);
	createCompressor();
	QByteArray resourceData[3];
	for (int i = 0; i < 3; ++i)
	{
		for (uint32_t j = 0; j < bundle.resourceCount; ++j)
			createResource(resourceData[i], bundle, j, i, stream.platform());
		if (i == 0)
		{
			auto remainder = (bundle.resourceData[0] + resourceData[0].size()) % 0x80;
			if (remainder != 0)
				resourceData[0].resize(resourceData[0].size() - remainder + 0x80, '\0');
		}
		if (i == 1)
		{
			uint64_t total = 0;
			for (uint32_t j = 0; j < bundle.resourceCount; ++j)
				total += bundle.entries[j].uncompressedInfo[i + 1] & 0x0FFFFFFF;
			if (total > 0)
			{
				auto remainder = (bundle.resourceData[0] + resourceData[0].size() + resourceData[1].size()) % 0x80;
				if (remainder != 0)
					resourceData[1].resize(resourceData[1].size() - remainder + 0x80, '\0');
			}
		}
	}
	std::cout << '\n';
	outputBundle(stream, bundle, resourceData);
	return 0;
}

void YAP::createBundle(GameDataStream& stream, YAML::Node& meta, Bundle& bundle)
{
	bundle.magic = "bnd2";
	bundle.version = 2;
	bundle.platform = meta["bundle"]["platform"].as<uint32_t>();
	setPlatform(stream, bundle);
	bundle.debugData = 0x30;
	QFileInfo debugDataInfo(inPath + debugDataFilename);
	if (debugDataInfo.exists() && debugDataInfo.size() > 0)
	{
		uint32_t entriesOffset = bundle.debugData + debugDataInfo.size() + 1;
		if (entriesOffset % 0x10 != 0)
			bundle.resourceEntries = (entriesOffset & 0xFFFFFFF0) + 0x10;
		else
			bundle.resourceEntries = entriesOffset;
		bundle.flags |= (uint32_t)Bundle::Flags::ContainsDebugData;
	}
	else
	{
		bundle.resourceEntries = bundle.debugData;
	}
	bundle.resourceCount = meta["resources"].size();
	if (bundle.resourceCount == 0)
		qWarning() << "Metadata file contains no resources.";
	bundle.resourceData[0] = bundle.resourceEntries + bundle.resourceCount * 0x40;
	// resourceData[1] and [2] set after writing resources

	// Set flags other than debug data flag
	if (!meta["bundle"]["compressed"] || !meta["bundle"]["compressed"].IsScalar())
	{
		qWarning() << "Flag \"compressed\" is unspecified or invalid. Defaulting to true.";
		bundle.flags |= (uint32_t)Bundle::Flags::IsCompressed;
	}
	else if (meta["bundle"]["compressed"].as<bool>())
	{
		bundle.flags |= (uint32_t)Bundle::Flags::IsCompressed;
	}
	if (!meta["bundle"]["mainMemOptimised"] || !meta["bundle"]["mainMemOptimised"].IsScalar())
	{
		qWarning() << "Flag \"mainMemOptimised\" is unspecified or invalid. Defaulting to true.";
		bundle.flags |= (uint32_t)Bundle::Flags::IsMainMemOptimised;
	}
	else if (meta["bundle"]["mainMemOptimised"].as<bool>())
	{
		bundle.flags |= (uint32_t)Bundle::Flags::IsMainMemOptimised;
	}
	if (!meta["bundle"]["graphicsMemOptimised"] || !meta["bundle"]["graphicsMemOptimised"].IsScalar())
	{
		qWarning() << "Flag \"graphicsMemOptimised\" is unspecified or invalid. Defaulting to true.";
		bundle.flags |= (uint32_t)Bundle::Flags::IsGraphicsMemOptimised;
	}
	else if (meta["bundle"]["graphicsMemOptimised"].as<bool>())
	{
		bundle.flags |= (uint32_t)Bundle::Flags::IsGraphicsMemOptimised;
	}
	std::cout << "Created bundle header\n";
}

void YAP::setPlatform(GameDataStream& stream, Bundle bundle)
{
	if (bundle.platform == 1)
		stream.setPlatform(GameDataStream::Platform::PC);
	else if (bundle.platform == 2)
		stream.setPlatform(GameDataStream::Platform::X360);
	else if (bundle.platform == 3)
		stream.setPlatform(GameDataStream::Platform::PS3);
}

void YAP::createResourceEntry(YAML::const_iterator& resource, Bundle& bundle, int index)
{
	ResourceEntry entry;
	stringToUInt(QString::fromStdString(resource->first.as<std::string>()), entry.id, true); // Already validated
	entry.type = resource->second["type"].as<uint32_t>();

	// Set up files and imports
	int secondaryMemType = resource->second["secondaryMemoryType"].as<int>(-1);
	bool usingCombinedImports = true;
	bool noImports = false;
	QFileInfo dataFileInfo(resourceFiles[index][0]);
	QFileInfo secondaryFileInfo;
	if (!resourceFiles[index][1].isEmpty())
		secondaryFileInfo.setFile(resourceFiles[index][1]);
	QFileInfo importsFileInfo(inPath + importsFilename);
	if (!resourceFiles[index][2].isEmpty())
	{
		importsFileInfo.setFile(resourceFiles[index][2]);
		usingCombinedImports = false;
	}
	YAML::Node imports;
	YAML::Node resourceImports; // Imports for this specific resource
	if (!importsFileInfo.exists())
		noImports = true;
	if (usingCombinedImports)
	{
		if (!combinedImports[resource->first.as<std::string>()])
			noImports = true;
		else
			resourceImports = combinedImports[resource->first.as<std::string>()];
	}
	else if (!noImports && !usingCombinedImports)
	{
		resourceImports = YAML::LoadFile(importsFileInfo.absoluteFilePath().toStdString());
	}

	// Create import entries and set import hash
	if (!noImports)
	{
		for (YAML::const_iterator impEntryIt = resourceImports.begin();
			impEntryIt != resourceImports.end(); ++impEntryIt)
		{
			ImportEntry impEntry;
			stringToUInt<uint32_t>(QString::fromStdString(impEntryIt->begin()->first.as<std::string>()), impEntry.offset, true);
			impEntry.id = impEntryIt->begin()->second.as<uint64_t>();
			entry.imports.append(impEntry);
			entry.importCount++;
			entry.importsHash |= impEntry.id;
		}
	}

	// Uncompressed size and alignment
	uint32_t primaryAlignment = (uint32_t)log2(resource->second["alignment"][0].as<uint16_t>(defaultPrimaryAlignment)) << 28;
	uint32_t primarySize = dataFileInfo.size();
	uint32_t importsSize = entry.importCount * 0x10;
	entry.uncompressedInfo[0] = primarySize + importsSize + primaryAlignment;
	if (secondaryMemType != -1)
	{
		uint32_t secondaryAlignment = (uint32_t)log2(resource->second["alignment"][1].as<uint16_t>(defaultSecondaryAlignment)) << 28;
		uint32_t secondarySize = secondaryFileInfo.size();
		entry.uncompressedInfo[secondaryMemType] = secondarySize + secondaryAlignment;
	}

	// Import offset
	if (entry.importCount > 0)
		entry.importsOffset = primarySize;

	// Compressed size and disk offset will be set during resource creation
	
	bundle.entries.append(entry);
	std::cout << "\rCreated resource entry " << index + 1 << "/" << bundle.resourceCount;
}

bool YAP::compareResourceEntry(const ResourceEntry& a, const ResourceEntry& b)
{
	return a.id < b.id;
}

bool YAP::compareResourceFileList(const QStringList& a, const QStringList& b)
{
	QString aStr = a[0].sliced(a[0].lastIndexOf('/') + 1, 8);
	QString bStr = b[0].sliced(b[0].lastIndexOf('/') + 1, 8);
	return std::stoull(aStr.toStdString(), nullptr, 16) < std::stoull(bStr.toStdString(), nullptr, 16);
}

void YAP::createResource(QByteArray& data, Bundle& bundle, int index, int memType, GameDataStream::Platform platform)
{
	if ((bundle.entries[index].uncompressedInfo[memType] & 0x0FFFFFFF) == 0)
		return;

	// Align start
	uint16_t align = memType == 0 ? 0x10 : 0x80;
	uint32_t alignedSize = data.size();
	if (alignedSize % align != 0)
		alignedSize = align * ((alignedSize + (align - 1)) / align);
	data.resize(alignedSize, '\0');

	QFile file;
	if (memType == 0)
		file.setFileName(resourceFiles[index][0]);
	else
		file.setFileName(resourceFiles[index][1]);

	// Get data
	file.open(QIODeviceBase::ReadOnly);
	QByteArray resourceData = file.readAll();
	file.close();

	// Append imports, if they exist
	if (memType == 0)
	{
		QBuffer importData;
		GameDataStream stream(&importData, platform);
		importData.open(QIODeviceBase::WriteOnly);
		for (int i = 0; i < bundle.entries[index].importCount; ++i)
		{
			stream << bundle.entries[index].imports[i].id;
			stream << bundle.entries[index].imports[i].offset;
			stream << (uint32_t)0;
		}
		importData.close();
		resourceData.append(importData.buffer());
	}

	// Compress data if specified
	if (bundle.flags & (uint32_t)Bundle::Flags::IsCompressed)
	{
		// No idea how much bigger zlib can be than uncompressed but +1024 should be ok
		char* compressedData = new char[resourceData.size() + 1024];
		size_t cmpSize = libdeflate_zlib_compress(cmp, resourceData.constData(), resourceData.size(), compressedData, resourceData.size() + 1024);
		resourceData.setRawData(compressedData, cmpSize);
		bundle.entries[index].compressedSize[memType] = cmpSize; // Compressed size
	}
	else
	{
		bundle.entries[index].compressedSize[memType] = bundle.entries[index].uncompressedInfo[memType] & 0x0FFFFFFF;
	}
	
	// Add to the existing array
	bundle.entries[index].offset[memType] = data.size(); // Disk offset
	data.append(resourceData);
	if (index == 0 && memType != 0)
		std::cout << '\n';
	if (memType == 0)
		std::cout << "\rAdded primary portion for resource " << index + 1 << "/" << bundle.resourceCount;
	else
		std::cout << "\rAdded secondary portion for resource " << index + 1;
}

void YAP::outputBundle(GameDataStream& stream, Bundle& bundle, QByteArray data[])
{
	stream.device()->open(QIODeviceBase::WriteOnly);
	
	// Update resource data offsets
	bundle.resourceData[1] = bundle.resourceData[0] + data[0].size();
	if (bundle.resourceData[1] % 0x80 != 0)
		bundle.resourceData[1] = 0x80 * ((bundle.resourceData[1] + 0x7F) / 0x80);
	bundle.resourceData[2] = bundle.resourceData[1] + data[1].size();
	if (bundle.resourceData[2] % 0x80 != 0)
		bundle.resourceData[2] = 0x80 * ((bundle.resourceData[2] + 0x7F) / 0x80);
	
	// Write bundle header
	stream.writeString(bundle.magic);
	stream << bundle.version;
	stream << bundle.platform;
	stream << bundle.debugData;
	stream << bundle.resourceCount;
	stream << bundle.resourceEntries;
	for (int i = 0; i < 3; ++i)
		stream << bundle.resourceData[i];
	stream << bundle.flags;

	// Write debug data
	if (bundle.flags & (uint32_t)Bundle::Flags::ContainsDebugData)
	{
		stream.seek(bundle.debugData);
		QFile debugDataFile(inPath + debugDataFilename);
		debugDataFile.open(QIODeviceBase::ReadOnly);
		QByteArray debugData = debugDataFile.readAll();
		debugDataFile.close();
		stream.writeString(debugData);
	}

	// Write resource entries
	stream.seek(bundle.resourceEntries);
	for (uint32_t i = 0; i < bundle.resourceCount; ++i)
	{
		stream << bundle.entries[i].id;
		stream << bundle.entries[i].importsHash;
		for (int j = 0; j < 3; ++j)
			stream << bundle.entries[i].uncompressedInfo[j];
		for (int j = 0; j < 3; ++j)
			stream << bundle.entries[i].compressedSize[j];
		for (int j = 0; j < 3; ++j)
			stream << bundle.entries[i].offset[j];
		stream << bundle.entries[i].importsOffset;
		stream << bundle.entries[i].type;
		stream << bundle.entries[i].importCount;
		stream << bundle.entries[i].flags;
		stream << bundle.entries[i].stream;
	}

	// Write resource data
	stream.writeRawData(data[0].constData(), bundle.resourceData[1] - bundle.resourceData[0]);
	stream.writeRawData(data[1].constData(), bundle.resourceData[2] - bundle.resourceData[1]);
	stream.writeRawData(data[2].constData(), data[2].size());

	// Save
	stream.close();
	std::cout << "Bundle created.\n";
}
