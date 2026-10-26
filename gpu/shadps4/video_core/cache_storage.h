// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/path_util.h"
#include "common/singleton.h"
#include "common/types.h"

#include <functional>
#include <thread>
#include <vector>

namespace Storage {

enum class BlobType : u32 {
    ShaderMeta,
    ShaderBinary,
    PipelineKey,
    ShaderProfile,
};

class DataBase {
public:
    static DataBase& Instance() {
        return *Common::Singleton<DataBase>::Instance();
    }

    void Open();
    void Close();
    [[nodiscard]] bool IsOpened() const {
        return opened;
    }
    void FinishPreload();
    /// osl: removes every cached blob (an incompatible cache is rebuilt, not ignored).
    void Clear();
    /// osl: puts the current cache aside (it belongs to another GPU) and moves back one put aside
    /// earlier whose stored profile `matches`. True when one came back; otherwise the cache
    /// starts empty.
    bool SwapCache(const std::function<bool(const std::vector<u8>&)>& matches);
    /// osl: deletes a blob, in order with the saves queued before it.
    void Remove(BlobType type, const std::string& name);

    bool Save(BlobType type, const std::string& name, std::vector<u8>&& data);
    bool Save(BlobType type, const std::string& name, std::vector<u32>&& data);

    void Load(BlobType type, const std::string& name, std::vector<u8>& data);
    void Load(BlobType type, const std::string& name, std::vector<u32>& data);

    using BlobVisitor = std::function<void(std::vector<u8>&& data, const std::string& name)>;
    /// osl: `func` gets each blob and its name (as Load takes it); `stop` returning true ends
    /// the walk early (background warm-up at exit).
    void ForEachBlob(BlobType type, const BlobVisitor& func,
                     const std::function<bool()>& stop = {});
    /// osl: the names of the blobs of a type, without reading them.
    void ForEachBlobName(BlobType type, const std::function<void(const std::string& name)>& func);
    /// osl: blobs are plain files written beside and renamed into place, so other threads
    /// may read them while the IO thread saves (the zip archive cannot).
    [[nodiscard]] bool ConcurrentReads() const;
    /// osl: number of blobs ForEachBlob visits (the loading screen's total).
    size_t CountBlobs(BlobType type);
    /// osl: the cache folder (the zip file with the archived cache; empty before Open).
    [[nodiscard]] std::filesystem::path Folder() const {
        return opened ? cache_path : std::filesystem::path{};
    }

private:
    std::jthread io_worker{};
    std::filesystem::path cache_path{};
    bool opened{};
};

} // namespace Storage
