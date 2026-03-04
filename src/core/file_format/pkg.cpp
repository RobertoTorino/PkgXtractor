// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <zlib.h>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <limits>
#include "common/io_file.h"
#include "common/logging/formatter.h"
#include "core/file_format/pkg.h"
#include "core/file_format/pkg_type.h"

static std::string SanitizeFileName(const std::string& filename) {
    std::string result = filename;
    static constexpr char invalid_chars[] = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};
    
    for (char& c : result) {
        if (static_cast<unsigned char>(c) < 0x20) {
            c = '_';
            continue;
        }
        for (char invalid : invalid_chars) {
            if (c == invalid) {
                c = '_';
                break;
            }
        }
    }
    
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    
    // More aggressive fallback - also handle empty input
    if (result.empty() || filename.empty()) {
        return "unnamed_file";
    }
    return result;
}

static bool DecompressPFSC(std::span<char> compressed_data, std::span<char> decompressed_data) {
    if (compressed_data.size() <= 2) {
        return false;
    }

    z_stream decompressStream{};
    decompressStream.zalloc = Z_NULL;
    decompressStream.zfree = Z_NULL;
    decompressStream.opaque = Z_NULL;

    // PFSC compressed sectors store a 2-byte prefix followed by a raw DEFLATE stream.
    if (inflateInit2(&decompressStream, -MAX_WBITS) != Z_OK) {
        return false;
    }

    decompressStream.avail_in = static_cast<uInt>(compressed_data.size() - 2);
    decompressStream.next_in =
        reinterpret_cast<unsigned char*>(compressed_data.data() + 2);
    decompressStream.avail_out = static_cast<uInt>(decompressed_data.size());
    decompressStream.next_out = reinterpret_cast<unsigned char*>(decompressed_data.data());

    const int inflate_result = inflate(&decompressStream, Z_FINISH);
    const bool ok = (inflate_result == Z_STREAM_END);
    inflateEnd(&decompressStream);
    return ok;
}

u32 GetPFSCOffset(std::span<const u8> pfs_image) {
    static constexpr u32 PfscMagic = 0x43534650;
    static constexpr u32 MinPFSCSize = 0x100;  // Minimum valid PFSC header size
    u32 value;

    if (pfs_image.size() < MinPFSCSize) {
        return -1;
    }

    // Fast path: original layout used by many PKGs.
    for (u32 i = 0x20000; i < pfs_image.size(); i += 0x10000) {
        if (i + sizeof(u32) > pfs_image.size()) break;
        std::memcpy(&value, &pfs_image[i], sizeof(u32));
        if (value == PfscMagic) {
            // Ensure we have enough data after this offset
            if (i + MinPFSCSize <= pfs_image.size()) {
                return i;
            }
        }
    }

    // Fallback: some PKGs place PFSC on 0x1000 boundaries instead.
    for (u32 i = 0; i + sizeof(u32) <= pfs_image.size(); i += 0x1000) {
        std::memcpy(&value, &pfs_image[i], sizeof(u32));
        if (value == PfscMagic) {
            // Ensure we have enough data after this offset
            if (i + MinPFSCSize <= pfs_image.size()) {
                return i;
            }
        }
    }

    // Last resort: byte-granular scan for uncommon layouts.
    // But limit the byte-granular search to reasonable locations (not the entire file)
    const u32 byte_scan_limit = std::min(static_cast<u32>(pfs_image.size()), 0x10000000U);  // Limit to 256MB
    for (u32 i = 0; i + sizeof(u32) <= byte_scan_limit; ++i) {
        std::memcpy(&value, &pfs_image[i], sizeof(u32));
        if (value == PfscMagic) {
            // Ensure we have enough data after this offset
            if (i + MinPFSCSize <= pfs_image.size()) {
                return i;
            }
        }
    }

    return -1;
}

PKG::PKG() = default;

PKG::~PKG() = default;

bool PKG::Open(const std::filesystem::path& filepath, std::string& failreason) {
    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }
    pkgpath = filepath;
    pkgSize = file.GetSize();

    file.Read(pkgheader);
    if (pkgheader.magic != 0x7F434E54)
        return false;

    for (const auto& flag : flagNames) {
        if (isFlagSet(pkgheader.pkg_content_flags, flag.first)) {
            if (!pkgFlags.empty())
                pkgFlags += (", ");
            pkgFlags += (flag.second);
        }
    }

    // Find title id it is part of pkg_content_id starting at offset 0x40
    file.Seek(0x47); // skip first 7 characters of content_id
    file.Read(pkgTitleID);

    u32 offset = pkgheader.pkg_table_entry_offset;
    u32 n_files = pkgheader.pkg_table_entry_count;

    if (!file.Seek(offset)) {
        failreason = "Failed to seek to PKG table entry offset";
        return false;
    }

    for (int i = 0; i < n_files; i++) {
        PKGEntry entry{};
        file.Read(entry.id);
        file.Read(entry.filename_offset);
        file.Read(entry.flags1);
        file.Read(entry.flags2);
        file.Read(entry.offset);
        file.Read(entry.size);
        file.Seek(8, Common::FS::SeekOrigin::CurrentPosition);

        // Try to figure out the name
        const auto name = GetEntryNameByType(entry.id);
        if (name == "param.sfo") {
            sfo.clear();
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to param.sfo offset";
                return false;
            }
            sfo.resize(entry.size);
            file.ReadRaw<u8>(sfo.data(), entry.size);
        }
    }
    file.Close();

    return true;
}

bool PKG::GetEntryDataById(u32 entry_id, std::vector<u8>& out_data, std::string& failreason) const {
    if (pkgpath.empty()) {
        failreason = "PKG path not initialized";
        return false;
    }

    Common::FS::IOFile file(pkgpath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        failreason = "Failed to open PKG file";
        return false;
    }

    PKGHeader header{};
    file.ReadRaw<u8>(&header, sizeof(PKGHeader));
    if (header.magic != 0x7F434E54) {
        failreason = "Invalid PKG magic";
        return false;
    }

    u32 offset = header.pkg_table_entry_offset;
    u32 n_files = header.pkg_table_entry_count;

    if (!file.Seek(offset)) {
        failreason = "Failed to seek to PKG table entry offset";
        return false;
    }

    for (u32 i = 0; i < n_files; i++) {
        PKGEntry entry{};
        file.Read(entry.id);
        file.Read(entry.filename_offset);
        file.Read(entry.flags1);
        file.Read(entry.flags2);
        file.Read(entry.offset);
        file.Read(entry.size);
        file.Seek(8, Common::FS::SeekOrigin::CurrentPosition);

        if (entry.id == entry_id) {
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to entry data";
                return false;
            }
            out_data.resize(entry.size);
            file.ReadRaw<u8>(out_data.data(), entry.size);
            return true;
        }
    }

    failreason = "Entry not found";
    return false;
}

bool PKG::Extract(const std::filesystem::path& filepath, const std::filesystem::path& extract,
                  std::string& failreason) {
    extract_path = extract;
    pkgpath = filepath;
    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }
    pkgSize = file.GetSize();
    file.ReadRaw<u8>(&pkgheader, sizeof(PKGHeader));

    if (pkgheader.magic != 0x7F434E54)
        return false;

    if (pkgheader.pkg_size > pkgSize) {
        failreason = "PKG file size is different";
        return false;
    }
    if ((pkgheader.pkg_content_size + pkgheader.pkg_content_offset) > pkgheader.pkg_size) {
        failreason = "Content size is bigger than pkg size";
        return false;
    }

    u32 offset = pkgheader.pkg_table_entry_offset;
    u32 n_files = pkgheader.pkg_table_entry_count;

    std::array<u8, 64> concatenated_ivkey_dk3;
    std::array<u8, 32> seed_digest;
    std::array<std::array<u8, 32>, 7> digest1;
    std::array<std::array<u8, 256>, 7> key1;
    std::array<u8, 256> imgkeydata;

    if (!file.Seek(offset)) {
        failreason = "Failed to seek to PKG table entry offset";
        return false;
    }

    for (int i = 0; i < n_files; i++) {
        PKGEntry entry{};
        file.Read(entry.id);
        file.Read(entry.filename_offset);
        file.Read(entry.flags1);
        file.Read(entry.flags2);
        file.Read(entry.offset);
        file.Read(entry.size);
        file.Seek(8, Common::FS::SeekOrigin::CurrentPosition);

        auto currentPos = file.Tell();

        // Try to figure out the name
        const auto name = GetEntryNameByType(entry.id);
        const auto filepath = extract_path / "sce_sys" / name;
        std::filesystem::create_directories(filepath.parent_path());

        // Populate sfo vector for param.sfo (entry ID 0x1000)
        if (entry.id == 0x1000 && name == "param.sfo") {
            sfo.clear();
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to param.sfo offset";
                return false;
            }
            sfo.resize(entry.size);
            file.ReadRaw<u8>(sfo.data(), entry.size);
            file.Seek(currentPos);
        }

        if (name.empty()) {
            // Just print with id
            Common::FS::IOFile out(extract_path / "sce_sys" / std::to_string(entry.id),
                                   Common::FS::FileAccessMode::Write);
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to PKG entry offset";
                return false;
            }

            std::vector<u8> data;
            data.resize(entry.size);
            file.ReadRaw<u8>(data.data(), entry.size);
            out.WriteRaw<u8>(data.data(), entry.size);
            out.Close();

            file.Seek(currentPos);
            continue;
        }

        if (entry.id == 0x1) {         // DIGESTS, seek;
                                       // file.Seek(entry.offset, fsSeekSet);
        } else if (entry.id == 0x10) { // ENTRY_KEYS, seek;
            file.Seek(entry.offset);
            file.Read(seed_digest);

            for (int i = 0; i < 7; i++) {
                file.Read(digest1[i]);
            }

            for (int i = 0; i < 7; i++) {
                file.Read(key1[i]);
            }

            PKG::crypto.RSA2048Decrypt(dk3_, key1[3], true); // decrypt DK3
        } else if (entry.id == 0x20) {                       // IMAGE_KEY, seek; IV_KEY
            file.Seek(entry.offset);
            file.Read(imgkeydata);

            // The Concatenated iv + dk3 imagekey for HASH256
            std::memcpy(concatenated_ivkey_dk3.data(), &entry, sizeof(entry));
            std::memcpy(concatenated_ivkey_dk3.data() + sizeof(entry), dk3_.data(), sizeof(dk3_));

            PKG::crypto.ivKeyHASH256(concatenated_ivkey_dk3, ivKey); // ivkey_
            // imgkey_ to use for last step to get ekpfs
            PKG::crypto.aesCbcCfb128Decrypt(ivKey, imgkeydata, imgKey);
            // ekpfs key to get data and tweak keys.
            PKG::crypto.RSA2048Decrypt(ekpfsKey, imgKey, false);
        } else if (entry.id == 0x80) {
            // GENERAL_DIGESTS, seek;
            // file.Seek(entry.offset, fsSeekSet);
        }

        Common::FS::IOFile out(extract_path / "sce_sys" / name, Common::FS::FileAccessMode::Write);
        if (!file.Seek(entry.offset)) {
            failreason = "Failed to seek to PKG entry offset";
            return false;
        }

        std::vector<u8> data;
        data.resize(entry.size);
        file.ReadRaw<u8>(data.data(), entry.size);
        out.WriteRaw<u8>(data.data(), entry.size);
        out.Close();

        // Decrypt Np stuff and overwrite.
        if (entry.id == 0x400 || entry.id == 0x401 || entry.id == 0x402 ||
            entry.id == 0x403) { // somehow 0x401 is not decrypting
            decNp.resize(entry.size);
            if (!file.Seek(entry.offset)) {
                failreason = "Failed to seek to PKG entry offset";
                return false;
            }

            std::vector<u8> data;
            data.resize(entry.size);
            file.ReadRaw<u8>(data.data(), entry.size);

            std::span<u8> cipherNp(data.data(), entry.size);
            std::array<u8, 64> concatenated_ivkey_dk3_;
            std::memcpy(concatenated_ivkey_dk3_.data(), &entry, sizeof(entry));
            std::memcpy(concatenated_ivkey_dk3_.data() + sizeof(entry), dk3_.data(), sizeof(dk3_));
            PKG::crypto.ivKeyHASH256(concatenated_ivkey_dk3_, ivKey);
            PKG::crypto.aesCbcCfb128DecryptEntry(ivKey, cipherNp, decNp);

            Common::FS::IOFile out(extract_path / "sce_sys" / name,
                                   Common::FS::FileAccessMode::Write);
            out.Write(decNp);
            out.Close();
        }

        file.Seek(currentPos);
    }

    // Read the seed
    std::array<u8, 16> seed;
    if (!file.Seek(pkgheader.pfs_image_offset + 0x370)) {
        failreason = "Failed to seek to PFS image offset";
        return false;
    }
    file.Read(seed);

    // Get data and tweak keys.
    PKG::crypto.PfsGenCryptoKey(ekpfsKey, seed, dataKey, tweakKey);

    const u64 max_pfs_read =
        (pkgheader.pfs_image_offset < pkgSize) ? (pkgSize - static_cast<u64>(pkgheader.pfs_image_offset)) : 0;
    const u64 cache_length = static_cast<u64>(pkgheader.pfs_cache_size) * 0x2ULL;
    u64 length = std::min(cache_length, max_pfs_read);
    if (length == 0 && pkgheader.pfs_image_size != 0) {
        length = std::min(static_cast<u64>(pkgheader.pfs_image_size), max_pfs_read);
    }

    int num_blocks = 0;
    std::vector<u8> pfsc;
    if (length != 0) {
        auto load_pfsc = [&](u64 read_length) -> bool {
            if (read_length == 0) {
                return false;
            }

            std::vector<u8> pfs_encrypted(read_length);
            if (!file.Seek(pkgheader.pfs_image_offset)) {
                return false;
            }
            file.Read(pfs_encrypted);

            std::vector<u8> pfs_decrypted(read_length);
            PKG::crypto.decryptPFS(dataKey, tweakKey, pfs_encrypted, pfs_decrypted, 0);

            // Try decrypted first, then encrypted
            pfsc_offset = GetPFSCOffset(pfs_decrypted);
            const u8* source = pfs_decrypted.data();
            
            if (pfsc_offset == static_cast<u32>(-1)) {
                pfsc_offset = GetPFSCOffset(pfs_encrypted);
                source = pfs_encrypted.data();
            }

            if (pfsc_offset == static_cast<u32>(-1)) {
                return false;  // No PFSC found in either
            }

            // Ensure offset is within bounds
            if (pfsc_offset >= read_length) {
                pfsc_offset = static_cast<u32>(-1);
                return false;  // Offset out of bounds
            }

            const u64 pfsc_size = read_length - pfsc_offset;
            pfsc.resize(pfsc_size);
            std::memcpy(pfsc.data(), source + pfsc_offset, pfsc_size);
            return true;
        };

        auto ensure_pfsc_capacity = [&](u64 required_pfsc_size) -> bool {
            if (required_pfsc_size <= pfsc.size()) {
                return true;
            }

            if (pfsc_offset == static_cast<u32>(-1)) {
                return false;
            }

            const u64 minimum_read_length = pfsc_offset + required_pfsc_size;
            if (minimum_read_length > max_pfs_read) {
                return false;
            }

            const u64 growth_target = std::max<u64>(length * 2ULL, minimum_read_length + 0x1000ULL);
            const u64 retry_length = std::min(max_pfs_read, growth_target);
            if (retry_length <= length) {
                return false;
            }

            if (!load_pfsc(retry_length)) {
                return false;
            }

            length = retry_length;
            return required_pfsc_size <= pfsc.size();
        };

        bool pfsc_loaded = load_pfsc(length);
        if (!pfsc_loaded && max_pfs_read > length) {
            const u64 retry_length = std::min(max_pfs_read, std::max<u64>(length * 4ULL, 0x800000ULL));
            if (retry_length > length) {
                pfsc_loaded = load_pfsc(retry_length);
                length = retry_length;
            }
        }

        if (!pfsc_loaded && pkgheader.pfs_image_size != 0) {
            const u64 full_retry_length =
                std::min(max_pfs_read, static_cast<u64>(pkgheader.pfs_image_size));
            if (full_retry_length > length) {
                pfsc_loaded = load_pfsc(full_retry_length);
                length = full_retry_length;
            }
        }

        file.Close();

        if (!pfsc_loaded) {
            failreason = "Failed to find PFSC header";
            return false;
        }

        if (pfsc.size() < sizeof(PFSCHdr)) {
            failreason = "PFSC buffer too small";
            return false;
        }

        PFSCHdr pfsChdr;
        std::memcpy(&pfsChdr, pfsc.data(), sizeof(pfsChdr));

        if (pfsChdr.block_sz2 == 0) {
            failreason = "Invalid PFSC block size (0)";
            return false;
        }

        if (pfsChdr.data_length == 0) {
            failreason = "Invalid PFSC data length (0)";
            return false;
        }

        // Validate block_offsets points to valid data in PFSC buffer
        if (pfsChdr.block_offsets < sizeof(PFSCHdr) || pfsChdr.block_offsets >= pfsc.size()) {
            failreason = "PFSC block_offsets out of bounds (offset=" + std::to_string(pfsChdr.block_offsets) +
                         ", pfsc_size=" + std::to_string(pfsc.size()) + ")";
            return false;
        }

        num_blocks = static_cast<int>(pfsChdr.data_length / pfsChdr.block_sz2);
        if (num_blocks <= 0 || num_blocks > 100000000) { // sanity check: allows up to ~6.4PB
            failreason = "Invalid PFSC block count (" + std::to_string(num_blocks) + ")";
            return false;
        }

        const u64 sector_map_bytes = static_cast<u64>(num_blocks + 1) * 8ULL;
        const u64 sector_map_end = static_cast<u64>(pfsChdr.block_offsets) + sector_map_bytes;
        if (!ensure_pfsc_capacity(sector_map_end)) {
            failreason = "PFSC sector map out of bounds (required=" + std::to_string(sector_map_end) +
                         ", pfsc_size=" + std::to_string(pfsc.size()) +
                         ", tried_len=" + std::to_string(length) + ")";
            return false;
        }

        sectorMap.resize(num_blocks + 1); // 8 bytes, need extra 1 to get the last offset.

        for (int i = 0; i < num_blocks + 1; i++) {
            u64 read_offset = pfsChdr.block_offsets + static_cast<u64>(i) * 8;
            std::memcpy(&sectorMap[i], pfsc.data() + read_offset, 8);
        }

        // Note: We don't validate all sector map entries here, as many PKGs have unused entries
        // or entries pointing beyond the PFSC buffer. Validation happens per-sector during
        // actual access in the extraction loop below.
    }

    u32 ent_size = 0;
    u32 ndinode = 0;
    int ndinode_counter = 0;
    bool dinode_reached = false;
    bool uroot_reached = false;
    current_dir = extract_path;
    std::vector<char> compressedData;
    std::vector<char> decompressedData(0x10000);

    // Get iNdoes and Dirents.
    for (int i = 0; i < num_blocks; i++) {
        const u64 sectorOffset = sectorMap[i];
        const u64 sectorSize = sectorMap[i + 1] - sectorOffset;

        // Validate sector data is within PFSC buffer
        if (sectorOffset >= pfsc.size() || sectorOffset + sectorSize > pfsc.size()) {
            throw std::runtime_error("Extract: PFSC sector " + std::to_string(i) +
                                     " out of bounds (offset=" + std::to_string(sectorOffset) +
                                     ", size=" + std::to_string(sectorSize) +
                                     ", pfsc_size=" + std::to_string(pfsc.size()) + ")");
        }

        if (sectorSize == 0) {
            throw std::runtime_error("Extract: PFSC sector " + std::to_string(i) + " has zero size");
        }

        if (sectorSize > 0x20000) { // sanity check: sector should not be larger than 128KB
            throw std::runtime_error("Extract: PFSC sector " + std::to_string(i) +
                                     " size suspiciously large (" + std::to_string(sectorSize) + " bytes)");
        }

        compressedData.resize(sectorSize);
        std::memcpy(compressedData.data(), pfsc.data() + sectorOffset, sectorSize);

        if (sectorSize == 0x10000) { // Uncompressed data
            std::memcpy(decompressedData.data(), compressedData.data(), 0x10000);
        } else if (sectorSize < 0x10000) { // Compressed data
            if (!DecompressPFSC(compressedData, decompressedData)) {
                throw std::runtime_error("Extract: PFSC sector decompression failed at metadata block " +
                                         std::to_string(i));
            }
        } else {
            std::fill(decompressedData.begin(), decompressedData.end(), 0);
        }

        if (i == 0) {
            std::memcpy(&ndinode, decompressedData.data() + 0x30, 4); // number of folders and files
        }

        int occupied_blocks =
            (ndinode * 0xA8) / 0x10000; // how many blocks(0x10000) are taken by iNodes.
        if (((ndinode * 0xA8) % 0x10000) != 0)
            occupied_blocks += 1;

        if (i >= 1 && i <= occupied_blocks) { // Get all iNodes, gives type, file size and location.
            for (int p = 0; p < 0x10000; p += 0xA8) {
                Inode node;
                std::memcpy(&node, &decompressedData[p], sizeof(node));
                if (node.Mode == 0) {
                    break;
                }
                iNodeBuf.push_back(node);
            }
        }

        // let's deal with the root/uroot entries here.
        // Sometimes it's more than 2 entries (Tomb Raider Remastered)
        const std::string_view flat_path_table(&decompressedData[0x10], 15);
        if (flat_path_table == "flat_path_table") {
            uroot_reached = true;
        }

        if (uroot_reached) {
            for (int i = 0; i < 0x10000; i += ent_size) {
                Dirent dirent;
                std::memcpy(&dirent, &decompressedData[i], sizeof(dirent));
                if (dirent.entsize <= 0 || dirent.entsize > (0x10000 - i)) {
                    uroot_reached = false;
                    break;
                }
                ent_size = dirent.entsize;
                if (dirent.ino != 0) {
                    ndinode_counter++;
                } else {
                    // Always anchor extraction to the caller-provided extraction path.
                    // Rewriting this to title-id based paths can break path mapping for many entries.
                    extractPaths[ndinode_counter] = extract_path;
                    uroot_reached = false;
                    break;
                }
            }
        }

        const char dot = decompressedData[0x10];
        const std::string_view dotdot(&decompressedData[0x28], 2);
        if (dot == '.' && dotdot == "..") {
            dinode_reached = true;
        }

        // Get folder and file names.
        bool end_reached = false;
        if (dinode_reached) {
            for (int j = 0; j < 0x10000; j += ent_size) { // Skip the first parent and child.
                Dirent dirent;
                std::memcpy(&dirent, &decompressedData[j], sizeof(dirent));

                if (dirent.entsize <= 0 || dirent.entsize > (0x10000 - j)) {
                    break;
                }

                // Stop here and continue the main loop
                if (dirent.ino == 0) {
                    break;
                }

                ent_size = dirent.entsize;

                if (dirent.ino < 0) {
                    continue;
                }

                if (dirent.type != PFS_FILE && dirent.type != PFS_DIR &&
                    dirent.type != PFS_CURRENT_DIR && dirent.type != PFS_PARENT_DIR) {
                    continue;
                }

                if (dirent.namelen < 0 ||
                    dirent.namelen > static_cast<s32>(sizeof(dirent.name))) {
                    continue;
                }

                auto& table = fsTable.emplace_back();
                table.name = std::string(dirent.name, dirent.namelen);
                table.inode = dirent.ino;
                table.type = dirent.type;

                if (table.type == PFS_CURRENT_DIR) {
                    // PFS_CURRENT_DIR (".") should reference a directory we've already seen
                    if (extractPaths.find(table.inode) != extractPaths.end()) {
                        current_dir = extractPaths[table.inode];
                    } else {
                        // If not found, keep using the previous current_dir
                        // This happens for the root directory on first encounter
                        extractPaths[table.inode] = current_dir.empty() ? extract_path : current_dir;
                        current_dir = extractPaths[table.inode];
                    }
                } else if (table.type == PFS_PARENT_DIR) {
                    // Don't update extractPaths for ".." entry
                } else {
                    // Sanitize filename before adding to extractPaths to prevent invalid Windows filenames
                    std::string sanitized_name = SanitizeFileName(table.name);
                    const auto& base_dir = current_dir.empty() ? extract_path : current_dir;
                    if (!sanitized_name.empty()) {
                        extractPaths[table.inode] = base_dir / std::filesystem::path(sanitized_name);
                    } else {
                        // Fallback for completely invalid filenames
                        extractPaths[table.inode] = base_dir / std::filesystem::path("unnamed_file_" + std::to_string(table.inode));
                    }
                }

                if (table.type == PFS_FILE || table.type == PFS_DIR) {
                    if (table.type == PFS_DIR) { // Create dirs.
                        std::filesystem::create_directory(extractPaths[table.inode]);
                    }
                    ndinode_counter++;
                    if ((ndinode_counter + 1) == ndinode) // 1 for the image itself (root).
                        end_reached = true;
                }
            }
            if (end_reached) {
                break;
            }
        }
    }
    return true;
}

bool PKG::ExportFileList(const std::filesystem::path& output_path) {
    try {
        std::ofstream file_list(output_path);
        if (!file_list.is_open()) {
            return false;
        }

        file_list << "PKG File List Export\n";
        file_list << "====================\n";
        file_list << "Total entries: " << fsTable.size() << "\n\n";
        file_list << "Format: [Index] Inode:#### Type:# Name: \"filename\" -> Path: \"full_path\"\n";
        file_list << "Types: 2=FILE, 3=DIR, 4=CURRENT_DIR(.), 5=PARENT_DIR(..)\n\n";

        int file_count = 0;
        int dir_count = 0;
        int other_count = 0;

        for (size_t i = 0; i < fsTable.size(); ++i) {
            const auto& entry = fsTable[i];
            file_list << "[" << i << "] ";
            file_list << "Inode:" << entry.inode << " ";
            file_list << "Type:" << static_cast<int>(entry.type) << " ";
            file_list << "Name: \"" << entry.name << "\"";

            // Show the extracted path if it exists
            if (extractPaths.find(entry.inode) != extractPaths.end()) {
                file_list << " -> Path: \"" << extractPaths.at(entry.inode).string() << "\"";
            } else {
                file_list << " -> Path: [NOT SET]";
            }

            file_list << "\n";

            // Count by type
            if (entry.type == PFS_FILE) {
                file_count++;
            } else if (entry.type == PFS_DIR) {
                dir_count++;
            } else {
                other_count++;
            }
        }

        file_list << "\n====================\n";
        file_list << "Summary:\n";
        file_list << "  Files: " << file_count << "\n";
        file_list << "  Directories: " << dir_count << "\n";
        file_list << "  Other (., ..): " << other_count << "\n";
        file_list << "  Total: " << fsTable.size() << "\n";

        file_list.close();
        return true;
    } catch (...) {
        return false;
    }
}

bool PKG::CanExtractFile(const int index, std::string& reason) const {
    if (index < 0 || static_cast<size_t>(index) >= fsTable.size()) {
        reason = "fsTable index out of range";
        return false;
    }

    const auto& entry = fsTable[index];
    
    // Non-file entries (directories, . , ..) are handled gracefully in ExtractFiles - don't reject them
    // Only validate metadata for file entries
    if (entry.type != PFS_FILE) {
        reason.clear();
        return true;  // Safe - directories are handled by early return in ExtractFiles
    }

    const int inode_number = static_cast<int>(entry.inode);
    if (inode_number < 0 || static_cast<size_t>(inode_number) >= iNodeBuf.size()) {
        reason = "inode out of range";
        return false;
    }

    if (extractPaths.find(inode_number) == extractPaths.end()) {
        reason = "missing extract path";
        return false;
    }

    const auto& inode = iNodeBuf[inode_number];
    if ((inode.Mode & InodeMode::file) == 0) {
        reason = "inode mode is not a file";
        return false;
    }

    if (inode.Size < 0) {
        reason = "inode size out of bounds";
        return false;
    }

    const u64 sector_loc = inode.loc;
    const u64 nblocks = inode.Blocks;
    const s64 file_size = inode.Size;
    const u64 max_file_size_from_blocks = nblocks * 0x10000ULL;

    if (static_cast<u64>(file_size) > max_file_size_from_blocks) {
        reason = "inode size exceeds block capacity";
        return false;
    }

    if (nblocks == 0 && file_size > 0) {
        reason = "zero blocks for non-empty file";
        return false;
    }

    if (sector_loc >= sectorMap.size()) {
        reason = "inode sector location out of bounds";
        return false;
    }

    const u64 remaining_entries = static_cast<u64>(sectorMap.size()) - sector_loc;
    if (nblocks + 1 > remaining_entries) {
        reason = "inode sector range out of bounds";
        return false;
    }

    for (u64 block = 0; block < nblocks; ++block) {
        const u64 current_index = sector_loc + block;
        const u64 next_index = current_index + 1;
        if (next_index >= sectorMap.size()) {
            reason = "sector map index out of range";
            return false;
        }

        const u64 sector_offset = sectorMap[current_index];
        const u64 next_sector_offset = sectorMap[next_index];
        if (next_sector_offset <= sector_offset) {
            reason = "non-monotonic sector offsets";
            return false;
        }

        const u64 sector_size = next_sector_offset - sector_offset;
        if (sector_size == 0 || sector_size > 0x10000) {
            reason = "invalid sector size";
            return false;
        }

        const u64 file_offset = pkgheader.pfs_image_offset + pfsc_offset + sector_offset;
        const u64 previous_data = (sector_offset + pfsc_offset) & 0xFFFULL;
        if (file_offset < previous_data) {
            reason = "invalid sector seek underflow";
            return false;
        }

        const u64 required_plain = previous_data + sector_size;
        const u64 required_buf_size = (required_plain + 0xFFFULL) & ~0xFFFULL;
        const u64 read_start = file_offset - previous_data;
        if (required_buf_size == 0 || read_start > pkgSize || required_buf_size > (pkgSize - read_start)) {
            reason = "sector read out of pkg bounds";
            return false;
        }
    }

    const auto& output_path = extractPaths.at(inode_number);
    const std::string filename_str = output_path.filename().string();
    if (filename_str.empty() || filename_str == "." || filename_str == "..") {
        reason = "invalid output filename";
        return false;
    }

    reason.clear();
    return true;
}

void PKG::ExtractFiles(const int index) {
    if (index < 0 || static_cast<size_t>(index) >= fsTable.size()) {
        throw std::out_of_range("ExtractFiles: fsTable index out of range: " +
                                std::to_string(index));
    }

    int inode_number = fsTable[index].inode;
    int inode_type = fsTable[index].type;
    std::string inode_name = fsTable[index].name;

    // Skip non-file entries (directories, current dir, parent dir markers)
    if (inode_type != PFS_FILE) {
        return; // Silently skip non-file entries
    }

    if (inode_number < 0 || static_cast<size_t>(inode_number) >= iNodeBuf.size()) {
        throw std::runtime_error("ExtractFiles: inode out of range for entry index " +
                                 std::to_string(index) + ", inode " +
                                 std::to_string(inode_number));
    }

    if (extractPaths.find(inode_number) == extractPaths.end()) {
        throw std::runtime_error("ExtractFiles: missing extract path for inode " +
                                 std::to_string(inode_number) + " (name: '" + inode_name + "', type: " +
                                 std::to_string(inode_type) + ")");
    }

    const auto& inode = iNodeBuf[inode_number];

    // Ensure inode represents a file and metadata is sane
    if ((inode.Mode & InodeMode::file) == 0) {
        throw std::runtime_error("ExtractFiles: inode mode is not a file for inode " +
                                 std::to_string(inode_number));
    }

    if (inode.Size < 0) {
        throw std::runtime_error("ExtractFiles: invalid inode size for inode " +
                                 std::to_string(inode_number));
    }

    const u64 sector_loc = inode.loc;
    const u64 nblocks = inode.Blocks;
    const s64 file_size = inode.Size;
    const u64 max_file_size_from_blocks = nblocks * 0x10000ULL;

    if (static_cast<u64>(file_size) > max_file_size_from_blocks) {
        throw std::runtime_error("ExtractFiles: inode size exceeds block capacity for inode " +
                                 std::to_string(inode_number));
    }

    if (nblocks == 0 && file_size > 0) {
        throw std::runtime_error("ExtractFiles: zero blocks for non-empty file inode " +
                                 std::to_string(inode_number));
    }

    if (sector_loc >= sectorMap.size()) {
        throw std::runtime_error("ExtractFiles: inode sector location out of bounds for inode " +
                                 std::to_string(inode_number));
    }

    const u64 remaining_entries = static_cast<u64>(sectorMap.size()) - sector_loc;
    if (nblocks + 1 > remaining_entries) {
        throw std::runtime_error("ExtractFiles: inode sector range out of bounds for inode " +
                                 std::to_string(inode_number));
    }

    // Get the path (filename already sanitized when added to extractPaths)
    auto output_path_it = extractPaths.find(inode_number);
    std::filesystem::path output_path;
    if (output_path_it != extractPaths.end()) {
        output_path = output_path_it->second;
    }

    if (output_path.empty()) {
        std::string fallback_name = SanitizeFileName(inode_name);
        if (fallback_name.empty()) {
            fallback_name = "unnamed_file_" + std::to_string(inode_number);
        }
        output_path = extract_path / fallback_name;
        extractPaths[inode_number] = output_path;
    }
    
    // Validate that the path has a filename component
    std::string filename_str = output_path.filename().string();
    if (filename_str.empty() || filename_str == "." || filename_str == "..") {
        throw std::runtime_error(
            "ExtractFiles: invalid file path (no filename) for inode " + 
            std::to_string(inode_number) + " (name: '" + inode_name + "', type: " +
            std::to_string(inode_type) + ") path: " + output_path.string());
    }
    
    // Ensure parent directory exists
    std::filesystem::path parent_dir = output_path.parent_path();
    if (parent_dir.empty()) {
        parent_dir = extract_path;
        output_path = parent_dir / output_path.filename();
        extractPaths[inode_number] = output_path;
    }

    try {
        std::filesystem::create_directories(parent_dir);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "ExtractFiles: failed to create parent directory: " + 
            std::string(e.what()) + " (inode " + std::to_string(inode_number) + ")");
    }

    Common::FS::IOFile inflated;
    inflated.Open(output_path, Common::FS::FileAccessMode::Write);
    if (!inflated.IsOpen()) {
        throw std::runtime_error("ExtractFiles: failed to open output file for inode " +
                                 std::to_string(inode_number) + " (name: '" + inode_name + 
                                 "', filename: '" + filename_str + "') at path: " +
                                 output_path.string());
    }

    Common::FS::IOFile pkgFile; // Open the file for each iteration to avoid conflict.
    pkgFile.Open(pkgpath, Common::FS::FileAccessMode::Read);
    if (!pkgFile.IsOpen()) {
        throw std::runtime_error("ExtractFiles: failed to open PKG for reading");
    }

        s64 size_decompressed = 0;
        std::vector<char> compressedData;
        std::vector<char> decompressedData(0x10000);

        u64 pfsc_buf_size = 0;
        std::vector<u8> pfsc(pfsc_buf_size);
        std::vector<u8> pfs_decrypted(pfsc_buf_size);

        for (u64 j = 0; j < nblocks; j++) {
            if (sector_loc + j + 1 >= sectorMap.size()) {
                throw std::runtime_error("ExtractFiles: sector map out of range at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j));
            }

            u64 sectorOffset =
                sectorMap[sector_loc + j]; // offset into PFSC_image and not pfs_image.
            u64 sectorSize = sectorMap[sector_loc + j + 1] -
                             sectorOffset; // indicates if data is compressed or not.
            u64 fileOffset = (pkgheader.pfs_image_offset + pfsc_offset + sectorOffset);
            u64 currentSector1 =
                (pfsc_offset + sectorOffset) / 0x1000; // block size is 0x1000 for xts decryption.

            if (sectorSize == 0) {
                throw std::runtime_error("ExtractFiles: zero-sized sector at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j));
            }
            if (sectorSize > 0x10000) {
                throw std::runtime_error("ExtractFiles: sector too large at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j) + ", size " +
                                         std::to_string(sectorSize));
            }

            u64 sectorOffsetMask = (sectorOffset + pfsc_offset) & 0xFFFFFFFFFFFFF000ULL;
            u64 previousData = (sectorOffset + pfsc_offset) - sectorOffsetMask;

            const u64 required_plain = previousData + sectorSize;
            const u64 required_buf_size = (required_plain + 0xFFFULL) & ~0xFFFULL;
            if (required_buf_size == 0) {
                throw std::runtime_error("ExtractFiles: invalid zero aligned PFSC read size at file index " +
                                         std::to_string(index) + ", block " + std::to_string(j));
            }
            if (required_buf_size > pfsc_buf_size) {
                pfsc_buf_size = required_buf_size;
            }
            pfsc.resize(required_buf_size);
            pfs_decrypted.resize(required_buf_size);

            if (fileOffset < previousData) {
                throw std::runtime_error("ExtractFiles: invalid PFSC seek underflow at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j));
            }
            const u64 read_start = fileOffset - previousData;
            if (read_start + required_buf_size > pkgSize) {
                throw std::runtime_error("ExtractFiles: PFSC read out of PKG bounds at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j) + " (read_start=" + std::to_string(read_start) +
                                         ", required=" + std::to_string(required_buf_size) +
                                         ", pkgSize=" + std::to_string(pkgSize) + ")");
            }

            pkgFile.Seek(read_start);
            pkgFile.Read(pfsc);

            PKG::crypto.decryptPFS(dataKey, tweakKey, pfsc, pfs_decrypted, currentSector1);

            if (previousData + sectorSize > pfs_decrypted.size()) {
                throw std::runtime_error("ExtractFiles: decrypted buffer overflow risk at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j));
            }

            compressedData.resize(sectorSize);
            std::memcpy(compressedData.data(), pfs_decrypted.data() + previousData, sectorSize);

            if (sectorSize == 0x10000) { // Uncompressed data
                std::memcpy(decompressedData.data(), compressedData.data(), 0x10000);
            } else if (sectorSize < 0x10000) { // Compressed data
                if (!DecompressPFSC(compressedData, decompressedData)) {
                    throw std::runtime_error("ExtractFiles: PFSC sector decompression failed at file index " +
                                             std::to_string(index) + ", block " +
                                             std::to_string(j));
                }
            } else {
                throw std::runtime_error("ExtractFiles: invalid PFSC sector size at file index " +
                                         std::to_string(index) + ", block " +
                                         std::to_string(j) + ", size " +
                                         std::to_string(sectorSize));
            }

            size_decompressed += 0x10000;

            if (j < nblocks - 1) {
                inflated.WriteRaw<u8>(decompressedData.data(), decompressedData.size());
            } else {
                // This is to remove the zeros at the end of the file.
                const s64 bytes_before_last_block = size_decompressed - 0x10000;
                s64 write_size = file_size - bytes_before_last_block;
                if (write_size < 0) {
                    write_size = 0;
                }
                if (write_size > static_cast<s64>(decompressedData.size())) {
                    write_size = static_cast<s64>(decompressedData.size());
                }
                inflated.WriteRaw<u8>(decompressedData.data(), static_cast<size_t>(write_size));
            }
        }
        pkgFile.Close();
        inflated.Close();
}
