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
#include <string>

namespace saddle::ipc {

enum class RequestType {
    GetJobs,
    AddJob,
    UpdateJob,
    DeleteJob,
    RunJob,
    StopJob,
    GetRemotes,
    Quit,
};

enum class ResponseType {
    JobsList,
    JobAdded,
    JobUpdated,
    JobDeleted,
    JobStarted,
    JobStopped,
    JobProgress,
    JobCompleted,
    RemotesList,
    Error,
};

struct Message {
    ResponseType type;
    nlohmann::json payload;
};

NLOHMANN_JSON_SERIALIZE_ENUM(RequestType, {
    {RequestType::GetJobs, "get_jobs"},
    {RequestType::AddJob, "add_job"},
    {RequestType::UpdateJob, "update_job"},
    {RequestType::DeleteJob, "delete_job"},
    {RequestType::RunJob, "run_job"},
    {RequestType::StopJob, "stop_job"},
    {RequestType::GetRemotes, "get_remotes"},
    {RequestType::Quit, "quit"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(ResponseType, {
    {ResponseType::JobsList, "jobs_list"},
    {ResponseType::JobAdded, "job_added"},
    {ResponseType::JobUpdated, "job_updated"},
    {ResponseType::JobDeleted, "job_deleted"},
    {ResponseType::JobStarted, "job_started"},
    {ResponseType::JobStopped, "job_stopped"},
    {ResponseType::JobProgress, "job_progress"},
    {ResponseType::JobCompleted, "job_completed"},
    {ResponseType::RemotesList, "remotes_list"},
    {ResponseType::Error, "error"},
})

std::string get_socket_path();

} // namespace saddle::ipc
