#include "DirStorage.h"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

std::vector<std::string> DirStorage::pending_installs()
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(_root / "install", ec))
        if (entry.is_regular_file())
            names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

success_is_true DirStorage::read_install(const std::string &file_name, ByteBuffer &out)
{
    std::ifstream in(_root / "install" / file_name, std::ios::binary);
    if (!in)
        RETURN_ERROR_AS_FALSE();
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true DirStorage::mark_installed(const std::string &file_name)
{
    std::error_code ec;
    fs::create_directories(_root / "installed", ec);
    fs::rename(_root / "install" / file_name, _root / "installed" / file_name, ec);
    RETURN_SUCCESS_IF(!ec);
}

success_is_true DirStorage::write_backup(const std::string &user, const std::string &file_name,
                                         const ByteBuffer &data)
{
    std::error_code ec;
    fs::path dir = _root / "backup" / user;
    fs::create_directories(dir, ec);
    std::ofstream out(dir / file_name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    RETURN_SUCCESS_IF(out.good());
}
