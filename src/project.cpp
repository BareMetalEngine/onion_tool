#include "common.h"
#include "project.h"
#include "projectManifest.h"
#include "projectCollection.h"
#include "moduleManifest.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"
#include "utils.h"

//--

ProjectFileInfo::ProjectFileInfo()
{}

//--

static ProjectFileType FileTypeForExtension(std::string_view ext)
{
	if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".inl")
		return ProjectFileType::CppHeader;
	if (ext == ".c" || ext == ".cpp" || ext == ".cxx" || ext == ".crt" || ext == ".cc")
		return ProjectFileType::CppSource;
	if (ext == ".s" || ext == ".S")
		return ProjectFileType::NasmAssembly;
	if (ext == ".bison")
		return ProjectFileType::Bison;
	if (ext == ".natvis")
		return ProjectFileType::NatVis;
	if (ext == ".lib")
		return ProjectFileType::LocalStaticLibrary;
	if (ext == ".rc")
		return ProjectFileType::WindowsResources;

	return ProjectFileType::Unknown;
}

//--

ProjectInfo::ProjectInfo()
{
}

ProjectInfo::~ProjectInfo()
{
    for (auto* file : files)
        delete file;
}

bool ProjectInfo::scanContent()
{
    bool valid = true;

    if (manifest->optionThirdParty)
    {
        for (const auto& file : manifest->thirdPartySourceFiles)
            valid &= internalTryAddFileFromPath(manifest->rootPath, file, ScanType::PrivateFiles);

        const auto buildFilePath = (manifest->loadPath / "build.xml").make_preferred();
        if (fs::is_regular_file(buildFilePath))
        {
			LogInfo() << "Found build.xml in third part library at " << buildFilePath;
            valid &= internalTryAddFileFromPath(manifest->loadPath, buildFilePath, ScanType::PrivateFiles);
        }

        for (const auto& path : manifest->exportedIncludePaths)
			valid &= scanFilesAtDir(manifest->loadPath, path, ScanType::PublicFiles, true);

		for (const auto& path : manifest->localIncludePaths)
			valid &= scanFilesAtDir(manifest->loadPath, path, ScanType::PublicFiles, true);
    }
    else if (manifest->optionLegacy)
    {
        if (manifest->legacySourceDirectories.empty())
        {
			const auto publicFilePath = manifest->rootPath;
			valid &= scanFilesAtDir(publicFilePath, publicFilePath, ScanType::PublicFiles, true);
        }
        else
        {
            for (const auto& localPath : manifest->legacySourceDirectories)
            {
				const auto publicFilePath = manifest->rootPath / localPath;
				valid &= scanFilesAtDir(publicFilePath, publicFilePath, ScanType::PublicFiles, false);
            }
        }

		if (fs::is_regular_file(manifest->loadPath))
		{
			valid &= internalTryAddFileFromPath(manifest->rootPath.parent_path(), manifest->loadPath, ScanType::PrivateFiles);
		}
    }
    else
    {
        {
            const auto publicFilePath = manifest->rootPath / "include";
            valid &= scanFilesAtDir(publicFilePath, publicFilePath, ScanType::PublicFiles, true);
        }

        {
            const auto privateFilePath = manifest->rootPath / "src";
            valid &= scanFilesAtDir(privateFilePath, privateFilePath, ScanType::PrivateFiles, true);
        }

        {
            const auto privateFilePath = manifest->rootPath / "natvis";
            valid &= scanFilesAtDir(privateFilePath, privateFilePath, ScanType::PrivateFiles, true);
        }

        {
            const auto mediaFilePath = manifest->rootPath / "media";
            valid &= scanFilesAtDir(mediaFilePath, mediaFilePath, ScanType::MediaFiles, true);
        }

        {
            const auto mediaFilePath = manifest->rootPath / "res";
            valid &= scanFilesAtDir(mediaFilePath, mediaFilePath, ScanType::ResourceFiles, true);
        }

        {
            const auto mediaFilePath = manifest->rootPath;
            valid &= scanFilesAtDir(mediaFilePath, mediaFilePath, ScanType::PrivateFiles, false);
        }
    }

    return valid;
}

bool ProjectInfo::internalTryAddFileFromPath(const fs::path& scanRootPath, const fs::path& absolutePath, ScanType scanType)
{
    const auto ext = absolutePath.extension().u8string();

	if (filesPaths.find(absolutePath) != filesPaths.end())
		return true; // already added

    auto type = (scanType == ProjectInfo::ScanType::MediaFiles) ? ProjectFileType::MediaFile : FileTypeForExtension(ext);

	if (manifest->optionThirdParty && scanType == ScanType::PublicFiles && type != ProjectFileType::CppHeader)
		return true; // silently ignore

    if (absolutePath.filename() == "build.xml")
        type = ProjectFileType::BuildScript;

    if (type == ProjectFileType::Unknown)
    {
        if (scanType == ScanType::ResourceFiles)
            return true; // just ignore it

        LogWarning() << "Unknown file type for " << absolutePath;
        return true;
    }

    if (scanType == ScanType::PublicFiles && type != ProjectFileType::CppHeader && !manifest->optionLegacy)
    {
        LogError() << "Public files directory (include/) can only host header files, file " << absolutePath << " is not a header";
        return false;
    }

    const auto shortName = absolutePath.filename().u8string();

    auto* file = new ProjectFileInfo;
    file->type = type;
    file->absolutePath = absolutePath;
    file->name = shortName;
    file->projectRelativePath = MakeGenericPathEx(fs::relative(absolutePath, rootPath));
    file->scanRelativePath = MakeGenericPathEx(fs::relative(absolutePath, scanRootPath));
    file->originalProject = this;
    files.push_back(file);

    filesPaths.insert(absolutePath);

    return true;    
}

bool ProjectInfo::scanFilesAtDir(const fs::path& scanRootPath, const fs::path& directoryPath, ScanType type, bool recursive)
{
    bool valid = true;

    try
    {
        if (fs::is_directory(directoryPath))
        {
            for (const auto& entry : fs::directory_iterator(directoryPath))
            {
                const auto name = entry.path().filename().u8string();

                if (entry.is_directory() && recursive)
                    valid &= scanFilesAtDir(scanRootPath, entry.path(), type, recursive);
                else if (entry.is_regular_file())
                    valid &= internalTryAddFileFromPath(scanRootPath, entry.path(), type);
            }
        }
    }
    catch (fs::filesystem_error& e)
    {
        LogError() << "Filesystem Error: " << e.what();
        valid = false;
    }

    return valid;
}

//--

bool ProjectInfo::resolveDependencies(const ProjectCollection& projects, std::vector<std::string>* outMissingProjectDependencies)
{
    bool valid = true;

    if (manifest)
    {
        // resolve required dependencies
        for (const auto& dep : manifest->dependencies)
            valid &= projects.resolveDependency(dep, resolvedDependencies, false, outMissingProjectDependencies);

        // resolve optional dependencies
        for (const auto& dep : manifest->optionalDependencies)
            projects.resolveDependency(dep, resolvedDependencies, true, outMissingProjectDependencies);

        // remove self from dependency list
        Remove(resolvedDependencies, this);
    }

    return valid;
}

bool ProjectInfo::resolveLibraries(ExternalLibraryReposistory& libs)
{
    bool valid = true;

	for (const auto& dep : manifest->libraryDependencies)
	{
		if (auto* lib = libs.findLibrary(dep))
		{
			PushBackUnique(resolvedLibraryDependencies, lib);
		}
		else
		{
			LogError() << "Missing library '" << dep << "' referenced in project '" << name << "'";
			valid = false;
		}
	}

    return valid;
}

//--

