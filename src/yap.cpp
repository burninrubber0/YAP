#include <yap.h>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

YAP::YAP(int argc, char* argv[])
{
	setupArgs();
	if (!readArgs(argc, argv) || !validateArgs())
	{
		result = 1;
		return;
	}

	if (mode == "e")
	{
		if (dirMode)
			result = extractDir();
		else
			result = extract();
	}
	else if (mode == "c")
	{
		if (dirMode)
			result = createDir();
		else
			result = create();
	}
}

YAP::~YAP()
{
	if (dc != nullptr)
		libdeflate_free_decompressor(dc);
	if (cmp != nullptr)
		libdeflate_free_compressor(cmp);
	delete args;
}

void YAP::setupArgs()
{
	args = new argparse::ArgumentParser("YAP", version, argparse::default_arguments::help);
	args->add_argument("mode")
		.choices("e", "c")
		.help("e=Extract the contents of a bundle to a folder\nc=Create a new bundle from a folder");
	args->add_argument("-e", "--extension")
		.nargs(argparse::nargs_pattern::at_least_one)
		.help("Specify the file extension to limit directory extraction/creation to.\n"
			"If used, this cannot be the last optional argument.");
	args->add_argument("-d", "--directory")
		.store_into(dirMode)
		.help("Extract all the files in a directory.");
	args->add_argument("input")
		.help("If extracting, the bundle to extract\nIf creating, the folder to generate a bundle from");
	args->add_argument("output")
		.help("If extracting, the folder to output to\nIf creating, the file to output");
	args->add_argument("-ns", "--nosort")
		.store_into(doNotSortByType)
		.flag()
		.help("(Extract only) Do not sort resources by type.");
	args->add_argument("-ci", "--combine-imports")
		.store_into(combineImports)
		.flag()
		.help("(Extract only) Consolidate the imports for every resource into a single file.");
	args->add_argument("-ap", "--primary-alignment")
		.help("(Create only) The alignment to be set on a resource's primary portion if no\nvalue is specified.\nMust be a power of 2 <=0x8000\nDefault: 0x10");
	args->add_argument("-as", "--secondary-alignment")
		.help("(Create only) The alignment to be set on a resource's secondary portion if no\nvalue is specified.\nMust be a power of 2 <=0x8000\nDefault: 0x80");
	args->add_description("A simple bundle extractor/creator.\nVersion " + version + ", built " + date);
	args->add_epilog("Examples:\n  YAP e AI.DAT ai_extracted\n  YAP c ai_extracted AI.DAT");
}

bool YAP::readArgs(int argc, char* argv[])
{
	try
	{
		args->parse_args(argc, argv);
	}
	catch (const std::exception& e)
	{
		if (argc == 1)
		{
			qInfo().noquote().nospace() << args->help().str();
			return false;
		}
		else
		{
			qCritical().noquote().nospace() << e.what() << "\n\n" << args->help().str();
			return false;
		}
	}

	mode = args->get("mode").c_str();
	inPath = args->get("input").c_str();
	outPath = args->get("output").c_str();
	inPath = QDir::cleanPath(inPath);
	outPath = QDir::cleanPath(outPath);

	if (auto arg = args->present<std::vector<std::string>>("-e"))
		if (arg.has_value())
			for (const std::string& ext : arg.value())
				fileExtensions.append(QString::fromStdString(ext.front() == '.' ? ext.substr(1) : ext));

	if (mode == "e" && !outPath.endsWith('/'))
		outPath += '/';
	else if (mode == "c" && !inPath.endsWith('/'))
		inPath += '/';

	if (args->is_used("--primary-alignment"))
	{
		if (!stringToUInt<uint16_t>(args->get("--primary-alignment").c_str(), defaultPrimaryAlignment, false, 0x10))
			return false;
	}
	if (args->is_used("--secondary-alignment"))
	{
		if (!stringToUInt<uint16_t>(args->get("--secondary-alignment").c_str(), defaultSecondaryAlignment, false, 0x80))
			return false;
	}

	return true;
}

bool YAP::validateArgs()
{
	if (dirMode)
	{
		QDir inDir(inPath);
		QDir outDir(outPath);

		if (!inDir.exists())
		{
			qCritical() << "Input directory does not exist:" << inPath;
			return false;
		}

		if (!outDir.exists() && !QDir().mkpath(outPath))
		{
			qCritical() << "Cannot create output directory:" << outPath;
			return false;
		}

		return true;
	}

	if (mode == "e" && !validateExtractArgs())
		return false;
	else if (mode == "c" && !validateCreateArgs())
		return false;
	return true;
}

bool YAP::validateExtractArgs()
{
	QFileInfo inInfo(inPath);
	if (!inInfo.exists() || !inInfo.isFile() || !inInfo.isReadable())
	{
		qCritical() << "Input file cannot be opened."
			<< "Ensure it exists and has the correct permissions set.";
		return false;
	}

	QFileInfo outInfo(outPath);
	if (!outInfo.exists())
	{
		if (!QDir().mkpath(outInfo.absoluteFilePath()))
		{
			qCritical() << "Invalid output folder. Check that the path is correct.";
			return false;
		}
	}
	else if (!outInfo.isDir() || !outInfo.isReadable() || !outInfo.isWritable())
	{
		qCritical() << "Output folder cannot be opened."
			<< "Ensure it has the correct permissions set.";
		return false;
	}

	if (combineImports)
	{
		QFile importsFile(outPath + importsFilename);
		if (!importsFile.open(QIODeviceBase::WriteOnly)) // Clear file
		{
			qCritical() << "Imports file cannot be opened."
				<< "Ensure it has the correct permissions set.";
			return false;
		}
	}

	return true;
}

bool YAP::validateCreateArgs()
{
	QFileInfo inInfo(inPath);
	if (!inInfo.exists() || !inInfo.isDir() || !inInfo.isReadable())
	{
		qCritical() << "Input folder cannot be opened."
			<< "Ensure is exists and has the correct permissions set.";
		return false;
	}

	QFileInfo outInfo(outPath);
	if (outInfo.exists() && !outInfo.isFile())
	{
		qCritical() << "Output file conflicts with an existing object."
			<< "Rename the object or choose a different output location.";
		return false;
	}
	if (!QFile(outInfo.absoluteFilePath()).open(QIODeviceBase::WriteOnly))
	{
		qCritical() << "Output file cannot be opened."
			<< "Ensure the path is correct and, if the file exists,"
			<< "that it has the correct permissions set.";
		return false;
	}

	// Ensure the alignments are powers of 2 and within bounds of 1 << 0 and 1 << 0xF
	if (defaultPrimaryAlignment < 1 || defaultPrimaryAlignment > 0x8000
		|| !std::has_single_bit(defaultPrimaryAlignment))
	{
		qWarning() << "Invalid custom primary alignment, defaulting to 0x10.";
		defaultPrimaryAlignment = 0x10;
	}
	if (defaultSecondaryAlignment < 1 || defaultSecondaryAlignment > 0x8000
		|| !std::has_single_bit(defaultSecondaryAlignment))
	{
		qWarning() << "Invalid custom secondary alignment, defaulting to 0x80.";
		defaultSecondaryAlignment = 0x80;
	}

	if (!validateMetadata())
		return false;

	return true;
}

bool YAP::validateMetadata()
{
	QFileInfo metaInfo(inPath + metadataFilename);
	if (!metaInfo.exists() || !metaInfo.isFile() || !metaInfo.isReadable())
	{
		qCritical().noquote() << "Metadata file could not be opened."
			<< "Ensure the file" << metadataFilename << "exists in the directory specified"
			<< "and that it has the correct permissions set.";
		return false;
	}

	YAML::Node meta = YAML::LoadFile((inPath + metadataFilename).toStdString());
	if (!meta.IsMap())
	{
		qCritical() << "Invalid metadata file: Expected root node type to be map.";
		return false;
	}

	if (!validateBundleMetadata(meta))
		return false;
	if (!validateResourceMetadata(meta))
		return false;
	if (!validateImports(meta))
		return false;

	return true;
}

bool YAP::validateBundleMetadata(YAML::Node& meta)
{
	if (!meta["bundle"])
	{
		qCritical().noquote() << "Could not find bundle node in metadata file."
			<< "Ensure the file" << metadataFilename << "is valid.";
		return false;
	}
	if (!meta["bundle"].IsMap())
	{
		qCritical() << "Invalid metadata file: Expected bundle node type to be map.";
		return false;
	}

	if (!meta["bundle"]["platform"])
	{
		qCritical() << "Could not find platform in metadata file. Aborting.";
		return false;
	}
	if (!meta["bundle"]["platform"].IsScalar())
	{
		qCritical() << "Invalid bundle platform: Expected scalar type.";
		return false;
	}
	uint32_t platform = meta["bundle"]["platform"].as<uint32_t>();
	if (platform < 1 || platform > 3)
	{
		qCritical() << "Invalid bundle platform: Must be 1, 2, or 3.";
		return false;
	}
	// Compressed and optimised flags checked during bundle creation
	return true;
}

bool YAP::validateResourceMetadata(YAML::Node& meta)
{
	if (!meta["resources"])
	{
		qCritical().noquote() << "Could not find resources node in metadata file."
			<< "Ensure the file" << metadataFilename << "is valid.";
		return false;
	}
	if (!meta["resources"].IsMap())
	{
		qCritical() << "Invalid metadata file: Expected resources node type to be map.";
		return false;
	}
	int i = 0;
	for (YAML::const_iterator resource = meta["resources"].begin();
		resource != meta["resources"].end(); ++resource, ++i)
	{
		std::cout << "\rValidating metadata for resource " << i + 1 << "/" << meta["resources"].size();
		if (!resource->second.IsMap())
		{
			qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
				<< ": Expected node type to be map.";
			return false;
		}
		uint64_t id = 0;
		if (!validateResourceIdKey(resource->first.as<std::string>(), id))
			return false;
		//if (resourceHasDuplicateKey(meta["resources"], id, resource->first.as<std::string>()))
		//	return false;
		if (!resource->second["type"] || !resource->second["type"].IsScalar())
		{
			qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
				<< " does not specify a type or specifies an invalid type. Aborting.";
			return false;
		}
		if (resource->second["secondaryMemoryType"])
		{
			if (!resource->second["secondaryMemoryType"].IsScalar())
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< ": Expected secondary memory type node type to be scalar.";
				return false;
			}
			uint32_t memType = resource->second["secondaryMemoryType"].as<uint32_t>();
			if (memType != 1 && memType != 2)
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< ": Invalid secondary memory type specified; must be 1 or 2.";
				return false;
			}
		}
		if (!resource->second["alignment"])
		{
			qWarning().noquote().nospace() << "Resource " << resource->first.as<std::string>()
				<< " does not specify alignment values. Defaults will be used.";
		}
		else if (!resource->second["alignment"].IsSequence())
		{
			qCritical().noquote().nospace() << "Resource" << resource->first.as<std::string>()
				<< ": Expected alignment node type to be sequence.";
			return false;
		}
		else
		{
			for (YAML::Node alignment : resource->second["alignment"])
			{
				if (!alignment.IsScalar())
				{
					qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
						<< ": Expected alignment value node type to be scalar.";
					return false;
				}
				if (!std::has_single_bit(alignment.as<uint16_t>()))
				{
					qWarning().nospace().noquote() << "Resource " << resource->first.as<std::string>()
						<< ": Invalid alignment value (must be a power of 2 <=0x8000). Defaults will be used.";
				}
			}
		}
		resourceFiles.append({ "", "", "" });
		QString idString = QString::number(id, 16).rightJustified(8, '0').toUpper();
		QDirIterator it(inPath, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			if (it.nextFileInfo().fileName() == idString + defaultSuffix
				|| it.fileInfo().fileName() == idString + primarySuffix)
			{
				if (!it.fileInfo().isFile() || !it.fileInfo().isReadable())
				{
					qCritical().noquote() << "Resource" << resource->first.as<std::string>()
						<< "primary portion cannot be opened. Ensure it has the correct permissions set.";
					return false;
				}
				if (it.fileInfo().size() == 0)
				{
					qCritical().noquote() << "Resource" << resource->first.as<std::string>()
						<< "primary portion is 0 bytes in size. Aborting.";
					return false;
				}
				if (!resourceFiles[i][0].isEmpty()) // Duplicate resource
				{
					qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
						<< ": Primary portion has a duplicate file. Aborting.";
					return false;
				}
				resourceFiles[i][0] = it.fileInfo().absoluteFilePath();
				// Do not break so duplicates may be found
			}
		}
		if (resourceFiles[i][0].isEmpty())
		{
			qCritical().noquote() << "Resource" << resource->first.as<std::string>()
				<< "is missing its primary data portion. Aborting.";
			return false;
		}
		if (resourceFiles[i][0].endsWith(primarySuffix))
		{
			resourceFiles[i][1] = resourceFiles[i][0].chopped(primarySuffix.size()) + secondarySuffix;
			QFileInfo secondaryInfo(resourceFiles[i][1]);
			if (!secondaryInfo.exists())
			{
				qCritical().noquote() << "Resource" << resource->first.as<std::string>()
					<< "is missing its secondary data portion. Aborting.";
				return false;
			}
			if (!secondaryInfo.isFile() || !secondaryInfo.isReadable())
			{
				qCritical().noquote() << "Resource" << resource->first.as<std::string>()
					<< "secondary portion cannot be opened. Ensure it has the correct permissions set.";
				return false;
			}
			if (secondaryInfo.size() == 0)
			{
				qCritical().noquote() << "Resource" << resource->first.as<std::string>()
					<< "secondary portion is 0 bytes in size. Aborting.";
				return false;
			}
		}
	}
	std::cout << "\nAll resource metadata validated successfully.\n";
	return true;
}

bool YAP::validateImports(YAML::Node& meta)
{
	// Imports are NOT guaranteed to exist, even if they should.
	// Due to changes in development builds, they can't be fully validated.
	// Leave that to the game and only check basic things here.
	bool usingCombinedFile = true;
	QFileInfo importsFileInfo(inPath + importsFilename);
	YAML::Node importsFile;
	if (!importsFileInfo.exists())
		usingCombinedFile = false;
	else
	{
		if (!importsFileInfo.isFile() || !importsFileInfo.isReadable())
		{
			qCritical() << "Imports file cannot be opened."
				<< "Ensure it has the correct permissions set.";
			return false;
		}
		combinedImports = YAML::LoadFile(importsFileInfo.absoluteFilePath().toStdString());
		importsFile = combinedImports;
		if (!importsFile.IsMap())
		{
			qCritical() << "Expected imports node type to be map. Aborting.";
			return false;
		}
	}
	int i = 0;
	for (YAML::const_iterator resource = meta["resources"].begin();
		resource != meta["resources"].end(); ++resource, ++i)
	{
		std::cout << "\rValidating imports for resource " << i + 1 << "/" << meta["resources"].size();
		YAML::Node resourceImports;
		uint64_t id = 0;
		stringToUInt(QString::fromStdString(resource->first.as<std::string>()), id, true); // ID already validated, don't check result
		if (!usingCombinedFile)
		{
			// Find imports file and determine whether it exists, then set it as per-resource imports
			QString importsLocation;
			if (resourceFiles[i][0].endsWith(primarySuffix))
				importsLocation = resourceFiles[i][0].chopped(primarySuffix.size()) + importsSuffix;
			else // <ID>.dat
				importsLocation = resourceFiles[i][0].chopped(defaultSuffix.size()) + importsSuffix;
			importsFileInfo.setFile(importsLocation);
			if (!importsFileInfo.exists())
				continue;
			if (!importsFileInfo.isFile() || !importsFileInfo.isReadable())
			{
				qCritical().noquote() << "Imports for resource" << resource->first.as<std::string>()
					<< "cannot be opened. Ensure it has the correct permissions set.";
				return false;
			}
			resourceFiles[i][2] = importsFileInfo.absoluteFilePath();
			importsFile = YAML::LoadFile(importsFileInfo.absoluteFilePath().toStdString());
			resourceImports = importsFile;
		}
		else
		{
			bool foundImportsList = false;
			for (YAML::const_iterator importsList = importsFile.begin();
				importsList != importsFile.end(); ++importsList)
			{
				uint64_t resId = 0;
				if (!validateResourceIdKey(importsList->first.as<std::string>(), resId))
					return false;
				//if (resourceHasDuplicateKey(importsFile, resId, resource->first.as<std::string>()))
				//	return false;
				if (resId == id)
				{
					resourceImports = importsList->second;
					foundImportsList = true;
					break;
				}
			}
			if (!foundImportsList)
				continue;
		}
		if (!resourceImports.IsSequence())
		{
			qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
				<< ": Expected imports node type to be sequence.";
			return false;
		}
		for (YAML::const_iterator import = resourceImports.begin();
			import != resourceImports.end(); ++import)
		{
			if (!import->IsMap())
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< ": Expected import node type to be map.";
				return false;
			}
			if (import->size() != 1)
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< ": Only one import per offset is allowed.";
				return false;
			}
			// Data existence has been verified at this point
			QFileInfo dataFileInfo(resourceFiles[i][0]);
			uint32_t importOffset = 0;
			if (!stringToUInt<uint32_t>(QString::fromStdString(import->begin()->first.as<std::string>()), importOffset, true))
				return false;
			if (importOffset > dataFileInfo.size())
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< ": Import offset " << import->begin()->first.as<std::string>() << " out of range. Aborting.";
				return false;
			}
			//if (importHasDuplicateKey(resourceImports, importOffset, import->begin()->first.as<std::string>()))
			//	return false;
			if (!import->begin()->second.IsScalar())
			{
				qCritical().noquote().nospace() << "Resource " << resource->first.as<std::string>()
					<< " import " << import->begin()->first.as<std::string>() << ": Expected node type to be scalar. Aborting.";
				return false;
			}
			uint64_t importedResourceId = import->begin()->second.as<uint64_t>();
			if (importedResourceId == 0 || importedResourceId > 0xFFFFFFFF)
			{
				qCritical().noquote().nospace() << "Invalid imported resource ID " << QString::number(importedResourceId, 16)
					<< " for resource " << resource->first.as<std::string>() << ". Aborting.";
				return false;
			}
		}
	}
	std::cout << "\nAll imports validated successfully.\n";
	return true;
}

bool YAP::validateResourceIdKey(std::string resourceKey, uint64_t& id)
{
	QString key = QString::fromStdString(resourceKey);
	if (!stringToUInt<uint64_t>(key, id, true))
		return false;
	if (id > 0xFFFFFFFF || id == 0)
	{
		qCritical().noquote().nospace() << "Resource ID " << key
			<< " is invalid. Aborting.";
		return false;
	}
	return true;
}

//bool YAP::resourceHasDuplicateKey(YAML::Node list, uint64_t id, std::string resourceKey)
//{
//	bool foundFirst = false;
//	for (YAML::const_iterator next = list.begin(); next != list.end(); ++next)
//	{
//		uint64_t nextId = 0;
//		if (!stringToUInt<uint64_t>(QString::fromStdString(next->first.as<std::string>()), nextId, true))
//			return true;
//		if (id == nextId)
//		{
//			if (!foundFirst)
//			{
//				foundFirst = true;
//				continue;
//			}
//			qCritical().noquote() << resourceKey << "has a duplicate entry. Aborting.";
//			return true;
//		}
//	}
//	return false;
//}

//bool YAP::importHasDuplicateKey(YAML::Node list, uint32_t offset, std::string importKey)
//{
//	bool foundFirst = false;
//	for (YAML::const_iterator next = list.begin(); next != list.end(); ++next)
//	{
//		uint64_t nextOffset = 0;
//		if (!stringToUInt<uint64_t>(QString::fromStdString(next->begin()->first.as<std::string>()), nextOffset, true))
//			return true;
//		if (offset == nextOffset)
//		{
//			if (!foundFirst)
//			{
//				foundFirst = true;
//				continue;
//			}
//			qCritical().noquote() << importKey << "has a duplicate entry. Aborting.";
//			return true;
//		}
//	}
//	return false;
//}

void YAP::setShaderTypeName(GameDataStream& stream)
{
	// Already set to "Shader", only change if console version
	if (stream.platform() != GameDataStream::Platform::PC)
		resourceTypes[0x32] = "ShaderTechnique";
}

void YAP::createDecompressor()
{
	dc = libdeflate_alloc_decompressor();
}

void YAP::createCompressor()
{
	cmp = libdeflate_alloc_compressor(9);
}
