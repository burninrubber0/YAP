#include <yap.h>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <iostream>

int YAP::extractDir()
{
	QDir dir(inPath);
	QDir outDir(outPath);

	// Create filter with specified extension
	QStringList filters;
	for (const QString& ext : fileExtensions)
		filters << "*." + ext;
	
	QStringList bundles;
	if (filters.empty())
		bundles = dir.entryList(QDir::Files);
	else
		bundles = dir.entryList(filters, QDir::Files);
	if (bundles.isEmpty())
	{
		qWarning() << "No files found in" << inPath;
		return 1;
	}

	for (const QString& bundle : bundles)
	{
		QString bundlePath = dir.filePath(bundle);
		QString extractPath = outDir.filePath(bundle);
		// Instead of removing extension, keep it in folder name
		extractPath.chop(bundle.length());
		QFileInfo bundleInfo(bundle);
		extractPath += bundleInfo.completeBaseName() + "_" + bundleInfo.suffix();
		
		QDir().mkpath(extractPath);
		
		qInfo() << "Extracting" << bundle << "...";
		
		// Store current paths
		QString savedInPath = inPath;
		QString savedOutPath = outPath;
		
		// Set paths for this bundle
		inPath = bundlePath;
		outPath = extractPath + "/";
		
		// Extract
		int result = extract();
		if (result != 0)
			qWarning() << "Failed to extract" << bundle;
		
		// Restore paths
		inPath = savedInPath;
		outPath = savedOutPath;
	}

	return 0;
}

int YAP::extract()
{
	QFile inFile(inPath);
	GameDataStream inStream(&inFile);
	inFile.open(QIODeviceBase::ReadOnly);
	if (!validateBundle(inStream)) // Also sets platform (and endianness by extension)
		return 2;
	setShaderTypeName(inStream);
	createDecompressor();
	Bundle bundle;
	readBundle(inStream, bundle); // Bundle header and resource entries
	if (!validateResourceEntries(bundle))
		return 3;
	for (uint32_t i = 0; i < bundle.resourceCount; ++i)
		extractResource(inStream, bundle, i);
	std::cout << '\n';
	if (bundle.flags & (uint32_t)Bundle::Flags::ContainsDebugData)
		outputDebugData(inStream, bundle);
	inFile.close();
	outputMetadata(bundle);
	std::cout << "Extraction complete\n";

	return 0;
}

bool YAP::validateBundle(GameDataStream& stream)
{
	// Validate bundle magic
	QString magic;
	stream.readString(magic, 4);
	if (magic != "bnd2")
	{
		qCritical() << "Invalid bundle magic. Extraction aborted.";
		return false;
	}

	// Validate bundle platform
	uint32_t platform = 0;
	stream.seek(8);
	stream >> platform;
	// PC is the default and doesn't need to be set
	if (platform == 0x02000000)
		stream.setPlatform(GameDataStream::Platform::X360);
	else if (platform == 0x03000000)
		stream.setPlatform(GameDataStream::Platform::PS3);
	else if (platform != 1)
	{
		qCritical() << "Invalid bundle platform. Extraction aborted.";
		return false;
	}

	// Validate bundle version
	uint32_t version = 0;
	stream.seek(4);
	stream >> version;
	if (version != 2)
	{
		qCritical() << "Bundle not built for Burnout Paradise. Extraction aborted.";
		return false;
	}

	stream.seek(0);
	return true;
}

void YAP::readBundle(GameDataStream& stream, Bundle& bundle)
{
	stream.readString(bundle.magic, 4);
	stream >> bundle.version;
	stream >> bundle.platform;
	stream >> bundle.debugData;
	stream >> bundle.resourceCount;
	stream >> bundle.resourceEntries;
	for (int i = 0; i < 3; ++i)
		stream >> bundle.resourceData[i];
	stream >> bundle.flags;

	// Read resource entries
	for (uint32_t i = 0; i < bundle.resourceCount; ++i)
		readResourceEntry(stream, bundle, i);

	std::cout << "Read bundle and resource info\n";
}

void YAP::readResourceEntry(GameDataStream& stream, Bundle& bundle, int index)
{
	stream.seek(bundle.resourceEntries + index * 0x40);
	ResourceEntry entry;
	stream >> entry.id;
	stream >> entry.importsHash;
	for (int i = 0; i < 3; ++i)
		stream >> entry.uncompressedInfo[i];
	for (int i = 0; i < 3; ++i)
		stream >> entry.compressedSize[i];
	for (int i = 0; i < 3; ++i)
		stream >> entry.offset[i];
	stream >> entry.importsOffset;
	stream >> entry.type;
	stream >> entry.importCount;
	stream >> entry.flags;
	stream >> entry.stream;
	bundle.entries.append(entry);
}

bool YAP::validateResourceEntries(Bundle& bundle)
{
	// Necessary for corrupt bundles recovered from HDDs. If the entries are
	// corrupt, extraction cannot proceed correctly, so validation must be
	// rigorous.
	// These bundles are liable to be overwritten as early as offset 0x800,
	// which means only the bundle header and any previous (validated) resource
	// entries can be trusted, but not the current or next entry.
	// Technically, it also means entries 0-30 can always be trusted, but it's
	// better to validate than to blindly trust.
	for (uint32_t i = 0; i < bundle.resourceCount; ++i)
	{
		ResourceEntry entry = bundle.entries[i];
		if ((entry.id & 0xFFFFFFFF) == 0)
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Null resource ID"
				<< ".\nExtraction aborted.";
			return false;
		}
		if ((entry.id & 0xFFFFFFFF00000000) != 0)
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Invalid resource ID 0x" << QString::number(entry.id, 16).toUpper()
				<< ".\nExtraction aborted.";
			return false;
		}
		if ((entry.importsHash & 0xFFFFFFFF00000000) != 0)
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Invalid imports hash 0x" << QString::number(entry.importsHash, 16).toUpper()
				<< ".\nExtraction aborted.";
			return false;
		}
		if (entry.compressedSize[0] == 0)
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Data size for main memory portion is 0"
				<< ".\nExtraction aborted.";
			return false;
		}
		if (entry.type > 0x11004)
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Invalid type 0x" << QString::number(entry.type, 16).toUpper()
				<< ".\nExtraction aborted.";
			return false;
		}
		if (entry.importsOffset > (entry.uncompressedInfo[0] & 0x0FFFFFFF))
		{
			qCritical().noquote().nospace() << "Resource entry " << i
				<< ": Imports offset 0x" << QString::number(entry.importsOffset, 16).toUpper()
				<< " is greater than resource size 0x" << QString::number(entry.uncompressedInfo[0] & 0x0FFFFFFF, 16).toUpper()
				<< ".\nExtraction aborted.";
			return false;
		}
		for (int j = 0; j < 2; ++j)
		{
			uint32_t resourceEnd = bundle.resourceData[j] + entry.offset[j] + entry.compressedSize[j];
			if (resourceEnd > bundle.resourceData[j + 1])
			{
				qCritical().noquote().nospace() << "Resource entry " << i << " memory type " << j
					<< ": End offset 0x" << QString::number(resourceEnd, 16).toUpper()
					<< " is greater than memory type " << j + 1 << " start offset 0x" << QString::number(bundle.resourceData[j + 1], 16).toUpper()
					<< ".\nExtraction aborted.";
				return false;
			}
		}
		if (i > 0)
		{
			for (int j = 0; j < 3; ++j)
			{
				// Skip if this is the first resource for this memory type,
				// or if there is no data for this memory type.
				if (entry.offset[j] == 0 || entry.compressedSize[j] == 0)
					continue;

				// Not all resources have secondary portions.
				// Check that there's resource data in the previous resource.
				// If not, find the last one that does.
				int resIndex = i - 1;
				ResourceEntry prev = bundle.entries[resIndex];
				if (prev.compressedSize[j] == 0)
				{
					while (prev.compressedSize[j] == 0 && resIndex >= 0)
						prev = bundle.entries[--resIndex];

					// Sanity check, should never be true
					if (prev.compressedSize[j] == 0)
					{
						qCritical().noquote().nospace() << "Resource entry " << i << " memory type " << j
							<< ": Offset is not 0, yet there is no previous resource with data.\n"
							<< "Open an issue on GitHub or contact burninrubber0 directly if this happens.\n"
							<< "Extraction aborted.";
						return false;
					}
				}
				
				int resourceOffset = bundle.resourceData[j] + entry.offset[j];
				int prevResourceEnd = bundle.resourceData[j] + prev.offset[j] + prev.compressedSize[j];
				if (resourceOffset < prevResourceEnd)
				{
					qCritical().noquote().nospace() << "Resource entry " << i << " memory type " << j
						<< ": Start offset 0x" << QString::number(resourceOffset, 16).toUpper()
						<< " is less than the previous resource end offset 0x" << QString::number(prevResourceEnd, 16).toUpper()
						<< ".\nExtraction aborted.";
					return false;
				}
			}
		}
	}
	return true;
}

void YAP::extractResource(GameDataStream& stream, Bundle& bundle, int index)
{
	ResourceEntry& entry = bundle.entries[index];
	for (int i = 0; i < 3; ++i)
	{
		if (entry.compressedSize[i] == 0) // No data
			continue;

		// Get resource data
		char* resource = new char[entry.compressedSize[i]];
		stream.seek(entry.offset[i] + bundle.resourceData[i]);
		stream.device()->read(resource, entry.compressedSize[i]);

		// Decompress resource if compressed
		uint32_t uncompressedSize = entry.uncompressedInfo[i] & 0x0FFFFFFF;
		if (bundle.flags & 1)
		{
			char* uncompressedData = new char[uncompressedSize];
			auto r = libdeflate_zlib_decompress(dc, resource, entry.compressedSize[i], uncompressedData, uncompressedSize, nullptr);
			delete[] resource;
			if (r != LIBDEFLATE_SUCCESS)
			{
				qWarning().noquote().nospace()
					<< "Resource 0x" << QString::number(entry.id, 16).toUpper().rightJustified(8, '0')
					<< " memory type " << i << " failed to extract.";
				delete[] uncompressedData;
				continue;
			}
			resource = uncompressedData;
		}

		// Read imports and set resource data size
		uint32_t resourceDataLength = uncompressedSize;
		if (i == 0 && entry.importCount > 0)
		{
			uint32_t importsDataLength = entry.importCount * 0x10;
			resourceDataLength -= importsDataLength;
			QByteArray ba(resource + resourceDataLength, importsDataLength);
			GameDataStream importStream(ba, stream.platform());
			importStream.open(QIODeviceBase::ReadOnly);
			for (int j = 0; j < entry.importCount; ++j)
			{
				ImportEntry importEntry;
				importStream >> importEntry.id;
				importStream >> importEntry.offset;
				importStream.skip(4);
				entry.imports.append(importEntry);
			}
			importStream.close();
		}

		outputResource(resource, resourceDataLength, generateFilePath(entry, i));

		delete[] resource;
	}

	outputImports(bundle, index);

	std::cout << "\rExtracted resource " << index + 1 << "/" << bundle.resourceCount << std::flush;
}

// Returns the path + filename without extension
QString YAP::generateFilePath(ResourceEntry& entry, int memType)
{
	QString id = QString::number(entry.id, 16).rightJustified(8, '0');
	QString filename = id.toUpper();
	if (memType == 0 && (entry.compressedSize[1] != 0 || entry.compressedSize[2] != 0)) // Has secondary portion
		filename += primarySuffix;
	else if (memType > 0) // Is secondary portion
		filename += secondarySuffix;
	else
		filename += defaultSuffix;
	QString outPathFinal = outPath;
	if (!doNotSortByType) // Sort into type folders
	{
		if (resourceTypes.contains(entry.type))
			outPathFinal += resourceTypes[entry.type] + "/";
		else
			outPathFinal += "0x" + QString::number(entry.type, 16).toUpper() + "/";
		QDir().mkpath(outPathFinal);
	}
	return outPathFinal + filename;
}

void YAP::outputResource(char* resource, int length, QString path)
{
	QFile file(path);
	file.open(QIODeviceBase::WriteOnly);
	file.write(resource, length);
	file.flush();
	file.close();
}

void YAP::outputImports(Bundle& bundle, int resIndex)
{
	QFile importsFile;
	if (combineImports)
		importsFile.setFileName(outPath + importsFilename);
	ResourceEntry& resEntry = bundle.entries[resIndex];
	if (resEntry.importCount == 0)
		return;
	YAML::Emitter out;
	out.SetIntBase(YAML::Hex);
	if (combineImports)
	{
		QString resId = QString::number(resEntry.id, 16).rightJustified(8, '0').prepend("0x");
		out << YAML::BeginMap // Resources
			<< YAML::Key << resId.toStdString()
			<< YAML::Value;
	}
	out << YAML::BeginSeq; // Imports
	for (uint16_t j = 0; j < resEntry.importCount; ++j)
	{
		ImportEntry impEntry = resEntry.imports[j];
		QString impId = QString::number(impEntry.id, 16).rightJustified(8, '0').prepend("0x");
		QString impOffset = QString::number(impEntry.offset, 16).rightJustified(8, '0').prepend("0x");
		out << YAML::BeginMap // offset: id
			<< YAML::Key << impOffset.toStdString()
			<< YAML::Value << impId.toStdString()
			<< YAML::EndMap; // offset: id
	}
	out << YAML::EndSeq; // Imports
	if (!combineImports)
	{
		QString path = generateFilePath(resEntry, 0);
		if (path.endsWith("_primary"))
			path.chop(8);
		importsFile.setFileName(path + importsSuffix);
		if (!importsFile.open(QIODeviceBase::WriteOnly))
		{
			qWarning() << "Could not open file" << importsFile.fileName() << "for writing.";
			return;
		}
	}
	else
	{
		out << YAML::EndMap // Resources
			<< YAML::Newline;
		importsFile.open(QIODeviceBase::WriteOnly | QIODeviceBase::Append);
	}
	importsFile.write(out.c_str());
	importsFile.flush();
	importsFile.close();
}

void YAP::outputDebugData(GameDataStream& stream, Bundle& bundle)
{
	stream.seek(bundle.debugData);
	QString debugData;
	stream.readString(debugData);

	QFile file(outPath + debugDataFilename);
	file.open(QIODeviceBase::WriteOnly);
	file.write(debugData.toStdString().c_str());
	file.flush();
	file.close();

	std::cout << "Wrote debug data XML\n";
}

void YAP::outputMetadata(Bundle& bundle)
{
	YAML::Emitter out;
	out.SetIntBase(YAML::Hex);
	out << YAML::BeginMap; // overarching structure

	// Write bundle metadata
	out << YAML::Key << "bundle"
		<< YAML::Value
		<< YAML::BeginMap // bundle
		<< YAML::Key << "platform"
		<< YAML::Value << YAML::Dec << bundle.platform // 1=pc, 2=x360, 3=ps3
		<< YAML::Key << "compressed"
		<< YAML::Value << (bool)((bundle.flags & (uint32_t)Bundle::Flags::IsCompressed))
		<< YAML::Key << "mainMemOptimised"
		<< YAML::Value << (bool)((bundle.flags & (uint32_t)Bundle::Flags::IsMainMemOptimised))
		<< YAML::Key << "graphicsMemOptimised"
		<< YAML::Value << (bool)((bundle.flags & (uint32_t)Bundle::Flags::IsGraphicsMemOptimised))
		// Debug data flag excluded, determined by presence of .debug.xml
		<< YAML::EndMap; // bundle

	// Write resource metadata
	out << YAML::Key << "resources"
		<< YAML::Value
		<< YAML::BeginMap; // resources
	for (uint32_t i = 0; i < bundle.resourceCount; ++i)
	{
		ResourceEntry entry = bundle.entries[i];

		// Determine whether the resource has a secondary portion and, if so, which memory type it resides in
		int secondaryMemoryType = -1;
		if (entry.compressedSize[1] != 0)
			secondaryMemoryType = 1;
		else if (entry.compressedSize[2] != 0)
			secondaryMemoryType = 2;

		// Resource ID, type
		out << YAML::Key << QString::number(entry.id, 16).rightJustified(8, '0').prepend("0x").toStdString()
			<< YAML::Value
			<< YAML::BeginMap // resource details
			<< YAML::Key << "type"
			<< YAML::Value << entry.type;

		if (secondaryMemoryType != -1)
		{
			// Secondary portion's memory type
			out << YAML::Key << "secondaryMemoryType"
				<< YAML::Value << YAML::Dec << secondaryMemoryType;
		}

		// Per memory type alignment
		out << YAML::Key << "alignment"
			<< YAML::Value
			<< YAML::BeginSeq // alignment
			<< (1 << ((entry.uncompressedInfo[0] & 0xF0000000) >> 28));
		if (secondaryMemoryType != -1)
			out << (1 << ((entry.uncompressedInfo[secondaryMemoryType] & 0xF0000000) >> 28));
		out << YAML::EndSeq // alignment
			<< YAML::EndMap; // resource details
	}
	out << YAML::EndMap; // resources
	out << YAML::EndMap; // overarching structure

	QFile file(outPath + metadataFilename);
	file.open(QIODeviceBase::WriteOnly);
	file.write(out.c_str());
	file.flush();
	file.close();

	std::cout << "Wrote metadata file\n";
}
