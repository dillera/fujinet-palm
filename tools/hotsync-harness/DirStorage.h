#ifndef DIR_STORAGE_H
#define DIR_STORAGE_H

#include "HotSyncStorage.h"

#include <filesystem>

// HotSyncStorage over a host directory, for tests and the harness.
class DirStorage : public HotSyncStorage
{
public:
    explicit DirStorage(std::filesystem::path root) : _root(std::move(root)) {}

    std::vector<std::string> pending_installs() override;
    success_is_true read_install(const std::string &file_name, ByteBuffer &out) override;
    success_is_true mark_installed(const std::string &file_name) override;
    success_is_true write_backup(const std::string &user, const std::string &file_name,
                                 const ByteBuffer &data) override;

private:
    std::filesystem::path _root;
};

#endif
