#pragma once

#include <cstdint>
#include <string>

class SM4ZArchiver {
public:
    SM4ZArchiver();
    ~SM4ZArchiver();

    // Non-copyable, movable
    SM4ZArchiver(const SM4ZArchiver&) = delete;
    SM4ZArchiver& operator=(const SM4ZArchiver&) = delete;
    SM4ZArchiver(SM4ZArchiver&&) = default;
    SM4ZArchiver& operator=(SM4ZArchiver&&) = default;

    // Set worker thread count for parallel pack (0 or 1 = single-threaded).
    // Must be called before pack_archive. unpack_archive is always single-threaded.
    void set_num_threads(int n);

    // Pack a file into .sm4z archive
    // Layout: GlobalHeader(80B) + ChunkIndexTable(N*16B) + DataArea
    bool pack_archive(const std::string& src_file_path,
                      const std::string& out_sm4z_path,
                      const std::string& password);

    // Unpack a .sm4z archive back to original file
    // Verifies Header MAC before parsing index, then verifies each Chunk MAC
    // before decryption. Returns false on any integrity failure.
    bool unpack_archive(const std::string& src_sm4z_path,
                        const std::string& out_dest_path,
                        const std::string& password);

private:
    int num_threads_ = 0;
};
