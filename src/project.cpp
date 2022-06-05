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
	if (ext == ".c" || ext == ".cpp" || ext == ".cxx" || ext == ".crt")
		return ProjectFileType::CppSource;
	if (ext == ".bison")
		return ProjectFileType::Bison;
	if (ext == ".natvis")
		return ProjectFileType::NatVis;
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

    {
        const auto publicFilePath = manifest->rootPath / "include";
        valid &= scanFilesAtDir(publicFilePath, publicFilePath, ScanType::PublicFiles, true);
    }

    {
        const auto privateFilePath = manifest->rootPath / "src";
        valid &= scanFilesAtDir(privateFilePath, privateFilePath, ScanType::PrivateFiles, true);
    }

    {
        const auto mediaFilePath = manifest->rootPath / "media";
        valid &= scanFilesAtDir(mediaFilePath, mediaFilePath, ScanType::MediaFiles, true);
    }	

    return valid;
}

bool ProjectInfo::internalTryAddFileFromPath(const fs::path& scanRootPath, const fs::path& absolutePath, ScanType scanType)
{
    const auto ext = absolutePath.extension().u8string();

    auto type = (scanType == ProjectInfo::ScanType::MediaFiles) ? ProjectFileType::MediaFile : FileTypeForExtension(ext);

    if (absolutePath.filename() == PROJECT_MANIFEST_NAME)
        type = ProjectFileType::BuildScript;

    if (type == ProjectFileType::Unknown)
        return false;

    if (scanType == ScanType::PublicFiles && type != ProjectFileType::CppHeader)
        return false;

    const auto shortName = absolutePath.filename().u8string();

    auto* file = new ProjectFileInfo;
    file->type = type;
    file->absolutePath = absolutePath;
    file->name = shortName;
    file->projectRelativePath = MakeGenericPathEx(fs::relative(absolutePath, rootPath));
    file->rootRelativePath = MakeGenericPathEx(fs::relative(absolutePath, parentModule->projectsRootPath));
    file->scanRelativePath = MakeGenericPathEx(fs::relative(absolutePath, scanRootPath));
    file->originalProject = this;
    files.push_back(file);

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
        std::cout << "Filesystem Error: " << e.what() << "\n";
        valid = false;
    }

    return valid;
}

//--

bool ProjectInfo::resolveDependencies(const ProjectCollection& projects)
{
    bool valid = true;

    if (manifest)
    {
        // resolve required dependencies
        for (const auto& dep : manifest->dependencies)
            valid &= projects.resolveDependency(dep, resolvedDependencies, false);

        // resolve optional dependencies
        for (const auto& dep : manifest->optionalDependencies)
            projects.resolveDependency(dep, resolvedDependencies, true);
    }

    return valid;
}

bool ProjectInfo::resolveLibraries(ExternalLibraryReposistory& libs)
{
    bool valid = true;

	for (const auto& dep : manifest->libraryDependencies)
	{
		if (auto* lib = libs.installLibrary(dep))
		{
			PushBackUnique(resolvedLibraryDependencies, lib);
		}
		else
		{
			std::cerr << KRED << "[BREAKING] Missing library '" << dep << "' referenced in project '" << name << "'\n" << RST;
			valid = false;
		}
	}

    return valid;
}

//--

