/*
 * Saddle — GTK4 frontend to rclone
 * Copyright (C) 2026  Saddle contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace saddle::rclone {

struct ProviderOption {
    std::string name;
    std::string help;
    std::string default_value;
    std::string type; // "string", "bool", "int", etc.
    bool required = false;
    bool is_password = false;
    bool advanced = false;
    bool exclusive = false;
    struct Example {
        std::string value;
        std::string help;
    };
    std::vector<Example> examples;
};

struct ProviderInfo {
    std::string name;
    std::string description;
    std::string prefix;
    std::vector<ProviderOption> options;

    bool needs_oauth() const {
        for (auto& o : options)
            if (o.name == "token") return true;
        return false;
    }
};

struct RemoteInfo {
    std::string name;
    std::string type;
    nlohmann::json params;
};

struct FileEntry {
    std::string name;
    std::string path;
    int64_t size = 0;
    std::string mod_time;
    std::string mime_type;
    bool is_dir = false;
};

struct SyncStats {
    int64_t bytes = 0;
    int64_t total_bytes = 0;
    int transfers = 0;
    int total_transfers = 0;
    int checks = 0;
    int total_checks = 0;
    int errors = 0;
    double speed = 0.0;
    double elapsed_time = 0.0;
    std::optional<double> eta;
    bool fatal_error = false;
};

struct JobStatus {
    int64_t id;
    bool finished = false;
    bool success = false;
    std::string error;
    double duration = 0.0;
};

enum class JobType { Sync, Copy, Move };

NLOHMANN_JSON_SERIALIZE_ENUM(JobType, {
    {JobType::Sync, "sync"},
    {JobType::Copy, "copy"},
    {JobType::Move, "move"},
})

struct Job {
    std::string id;
    JobType     type             = JobType::Sync;
    std::string source;
    std::string destination;
    bool        dry_run          = false;
    std::string bandwidth;
    bool        schedule_enabled = false;
    std::string cron_minute      = "*";
    std::string cron_hour        = "*";
    std::string cron_day         = "*";
    std::string cron_month       = "*";
    std::string cron_weekday     = "*";
    std::string last_run;
    std::string last_status;
    std::vector<std::string> includes;  // Files to include; empty = entire directory
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Job,
    id, type, source, destination, dry_run, bandwidth,
    schedule_enabled, cron_minute, cron_hour, cron_day, cron_month, cron_weekday,
    last_run, last_status, includes)

// Async callback type used throughout
template <typename T>
using AsyncCallback = std::function<void(std::expected<T, std::string>)>;

} // namespace saddle::rclone
