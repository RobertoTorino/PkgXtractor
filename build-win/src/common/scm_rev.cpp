// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include "common/scm_rev.h"

namespace Common {

constexpr char g_version[]  = "0.12.0";
constexpr bool g_is_release = true;

constexpr char g_scm_rev[]         = "ad8505888eb74aebb26999aaf1110e24476a3ffb";
constexpr char g_scm_branch[]      = "main";
constexpr char g_scm_desc[]        = "ad85058-dirty";
constexpr char g_scm_remote_name[] = "origin";
constexpr char g_scm_remote_url[]  = "https://github.com/RobertoTorino/PkgXtractor.git";
constexpr char g_scm_date[]        = "2026-03-03 20:20:00";

const std::string GetRemoteNameFromLink() {
    std::string remote_url(Common::g_scm_remote_url);
    std::string remote_host;
    try {
        if (remote_url.starts_with("http")) {
            if (*remote_url.rbegin() == '/') {
                remote_url.pop_back();
            }
            remote_host = remote_url.substr(19, remote_url.rfind('/') - 19);
        } else if (remote_url.starts_with("git@")) {
            auto after_comma_pos = remote_url.find(':') + 1, slash_pos = remote_url.find('/');
            remote_host = remote_url.substr(after_comma_pos, slash_pos - after_comma_pos);
        } else {
            remote_host = "unknown";
        }
    } catch (...) {
        remote_host = "unknown";
    }
    return remote_host;
}

} // namespace

