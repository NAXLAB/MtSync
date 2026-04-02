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

#include "rclone_cli.hpp"
#include <glibmm.h>
#include <nlohmann/json.hpp>

namespace saddle::rclone {

using json = nlohmann::json;

RcloneCli::RcloneCli(std::string rclone_path) {
    auto resolved = Glib::find_program_in_path(rclone_path);
    m_rclone_path = resolved.empty() ? std::move(rclone_path) : std::move(resolved);
}

void RcloneCli::run_command(std::vector<std::string> args, CompletionHandler on_complete) {
    std::vector<std::string> full_args;
    full_args.push_back(m_rclone_path);
    for (auto& a : args)
        full_args.push_back(std::move(a));

    try {
        auto proc = Gio::Subprocess::create(
            full_args,
            Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_PIPE);

        // communicate_utf8_async dispatches on the GLib main loop
        proc->communicate_utf8_async("",
            [proc, on_complete = std::move(on_complete)](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    auto [stdout_data, stderr_data] = proc->communicate_utf8_finish(result);
                    int exit_code = proc->get_exit_status();
                    on_complete(std::string(stdout_data), std::string(stderr_data), exit_code);
                } catch (const Glib::Error& e) {
                    on_complete("", e.what(), -1);
                }
            });
    } catch (const Glib::Error& e) {
        on_complete("", std::string("Failed to spawn rclone: ") + e.what(), -1);
    }
}

void RcloneCli::list_remotes(AsyncCallback<std::vector<RemoteInfo>> callback) {
    run_command({"config", "dump"}, [callback = std::move(callback)](
        const std::string& out, const std::string& err, int code) {
        if (code != 0) {
            callback(std::unexpected("rclone config dump failed: " + err));
            return;
        }
        try {
            auto j = json::parse(out);
            std::vector<RemoteInfo> remotes;
            for (auto& [name, config] : j.items()) {
                RemoteInfo info;
                info.name = name;
                info.type = config.value("type", "unknown");
                info.params = config;
                remotes.push_back(std::move(info));
            }
            callback(std::move(remotes));
        } catch (const json::exception& e) {
            callback(std::unexpected(std::string("JSON parse error: ") + e.what()));
        }
    });
}

void RcloneCli::get_providers(AsyncCallback<std::vector<ProviderInfo>> callback) {
    run_command({"config", "providers"}, [callback = std::move(callback)](
        const std::string& out, const std::string& err, int code) {
        if (code != 0) {
            callback(std::unexpected("rclone config providers failed: " + err));
            return;
        }
        try {
            auto j = json::parse(out);
            std::vector<ProviderInfo> providers;
            // rclone config providers outputs a bare JSON array
            auto& arr = j.is_array() ? j : j["providers"];
            for (auto& prov : arr) {
                ProviderInfo info;
                info.name = prov.value("Name", "");
                info.description = prov.value("Description", "");
                info.prefix = prov.value("Prefix", info.name);

                if (prov.contains("Options") && prov["Options"].is_array()) {
                    for (auto& opt : prov["Options"]) {
                        ProviderOption po;
                        po.name = opt.value("Name", "");
                        po.help = opt.value("Help", "");
                        po.default_value = opt.value("DefaultStr", "");
                        po.type = opt.value("Type", "string");
                        po.required = opt.value("Required", false);
                        po.is_password = opt.value("IsPassword", false);
                        po.advanced = opt.value("Advanced", false);
                        po.exclusive = opt.value("Exclusive", false);

                        if (opt.contains("Examples") && opt["Examples"].is_array()) {
                            for (auto& ex : opt["Examples"]) {
                                ProviderOption::Example e;
                                e.value = ex.value("Value", "");
                                e.help = ex.value("Help", "");
                                po.examples.push_back(std::move(e));
                            }
                        }
                        info.options.push_back(std::move(po));
                    }
                }
                providers.push_back(std::move(info));
            }
            callback(std::move(providers));
        } catch (const json::exception& e) {
            callback(std::unexpected(std::string("JSON parse error: ") + e.what()));
        }
    });
}

void RcloneCli::config_create(const std::string& name, const std::string& type,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               AsyncCallback<std::monostate> callback) {
    std::vector<std::string> args = {"config", "create", name, type};
    for (auto& [k, v] : params)
        args.push_back(k + "=" + v);
    args.push_back("--non-interactive");

    run_command(std::move(args), [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("config create failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::config_update(const std::string& name,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               AsyncCallback<std::monostate> callback) {
    std::vector<std::string> args = {"config", "update", name};
    for (auto& [k, v] : params)
        args.push_back(k + "=" + v);
    args.push_back("--non-interactive");

    run_command(std::move(args), [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("config update failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::config_delete(const std::string& name, AsyncCallback<std::monostate> callback) {
    run_command({"config", "delete", name}, [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("config delete failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::authorize(const std::string& backend,
                           const std::string& client_id,
                           const std::string& client_secret,
                           AsyncCallback<std::string> callback) {
    std::vector<std::string> args = {"authorize", backend};
    if (!client_id.empty()) {
        args.push_back(client_id);
        if (!client_secret.empty())
            args.push_back(client_secret);
    }

    run_command(std::move(args), [callback = std::move(callback)](
        const std::string& out, const std::string& err, int code) {
        if (code != 0) {
            callback(std::unexpected("authorize failed: " + err));
            return;
        }
        // rclone prints the token in the log output (stderr) between markers:
        //   Paste the following into your remote machine --->
        //   {"access_token":"...","token_type":"...","refresh_token":"...","expiry":"..."}
        //   <---End paste
        // It may also appear in stdout. Search both.
        auto extract_token = [](const std::string& text) -> std::string {
            auto start = text.find("--->");
            auto end = text.find("<---");
            if (start != std::string::npos && end != std::string::npos) {
                start += 4; // skip "--->"
                auto token = text.substr(start, end - start);
                // Trim whitespace
                auto first = token.find_first_not_of(" \t\n\r");
                auto last = token.find_last_not_of(" \t\n\r");
                if (first != std::string::npos)
                    return token.substr(first, last - first + 1);
            }
            return {};
        };

        auto token = extract_token(err);
        if (token.empty())
            token = extract_token(out);

        if (token.empty())
            callback(std::unexpected("Could not extract token from rclone authorize output"));
        else
            callback(std::move(token));
    });
}

void RcloneCli::lsjson(const std::string& remote_path,
                        AsyncCallback<std::vector<FileEntry>> callback) {
    run_command({"lsjson", remote_path}, [callback = std::move(callback)](
        const std::string& out, const std::string& err, int code) {
        if (code != 0) {
            callback(std::unexpected("lsjson failed: " + err));
            return;
        }
        try {
            auto j = json::parse(out);
            std::vector<FileEntry> entries;
            for (auto& item : j) {
                FileEntry fe;
                fe.name = item.value("Name", "");
                fe.path = item.value("Path", "");
                fe.size = item.value("Size", int64_t{0});
                fe.mod_time = item.value("ModTime", "");
                fe.mime_type = item.value("MimeType", "");
                fe.is_dir = item.value("IsDir", false);
                entries.push_back(std::move(fe));
            }
            callback(std::move(entries));
        } catch (const json::exception& e) {
            callback(std::unexpected(std::string("JSON parse error: ") + e.what()));
        }
    });
}

void RcloneCli::copy_files(const std::string& src_dir,
                            const std::string& dst_dir,
                            const std::vector<std::string>& includes,
                            AsyncCallback<std::monostate> callback) {
    std::vector<std::string> args = {"copy", src_dir, dst_dir};
    for (auto& inc : includes) {
        args.push_back("--include");
        args.push_back(inc);
    }
    run_command(std::move(args), [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("copy failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::move_files(const std::string& src_dir,
                            const std::string& dst_dir,
                            const std::vector<std::string>& includes,
                            AsyncCallback<std::monostate> callback) {
    std::vector<std::string> args = {"move", src_dir, dst_dir};
    for (auto& inc : includes) {
        args.push_back("--include");
        args.push_back(inc);
    }
    run_command(std::move(args), [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("move failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::delete_files(const std::string& dir,
                               const std::vector<std::string>& includes,
                               AsyncCallback<std::monostate> callback) {
    if (includes.empty()) {
        callback(std::unexpected("No files specified for deletion"));
        return;
    }
    std::vector<std::string> args = {"delete", dir};
    for (auto& inc : includes) {
        args.push_back("--include");
        args.push_back(inc);
    }
    run_command(std::move(args), [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("delete failed: " + err));
        else
            callback(std::monostate{});
    });
}

void RcloneCli::mkdir(const std::string& path, AsyncCallback<std::monostate> callback) {
    run_command({"mkdir", path}, [callback = std::move(callback)](
        const std::string&, const std::string& err, int code) {
        if (code != 0)
            callback(std::unexpected("mkdir failed: " + err));
        else
            callback(std::monostate{});
    });
}

} // namespace saddle::rclone
