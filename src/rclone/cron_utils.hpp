/*
 * Mt. Sync — GTK4 frontend to rclone
 * Copyright (C) 2026  Mt. Sync contributors
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

#include "rclone_types.hpp"
#include <glib.h>
#include <algorithm>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mtsync::cron {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace detail {

// Parse one cron field (e.g. "*", "5", "9-17", "1,3,5", "*/10", "0-30/5")
// and return the sorted, deduplicated list of matching integers in [lo, hi].
// Returns an empty vector if the field is syntactically invalid.
inline std::vector<int> expand_field(const std::string& field, int lo, int hi) {
    std::vector<int> result;
    if (field.empty()) return result;

    // Split on commas
    std::istringstream ss(field);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty()) return {}; // malformed

        // Check for step suffix "/N"
        int step = 1;
        auto slash = part.find('/');
        if (slash != std::string::npos) {
            try { step = std::stoi(part.substr(slash + 1)); }
            catch (...) { return {}; }
            if (step <= 0) return {};
            part = part.substr(0, slash);
        }

        // Expand range
        int start = lo, end = hi;
        if (part == "*") {
            start = lo; end = hi;
        } else {
            auto dash = part.find('-');
            if (dash != std::string::npos) {
                try {
                    start = std::stoi(part.substr(0, dash));
                    end   = std::stoi(part.substr(dash + 1));
                } catch (...) { return {}; }
            } else {
                try { start = end = std::stoi(part); }
                catch (...) { return {}; }
            }
        }

        for (int v = start; v <= end; v += step) {
            if (v >= lo && v <= hi)
                result.push_back(v);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

// Days in a month (1-based month).
inline int days_in_month(int year, int month) {
    constexpr int d[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[month];
}

// Day-of-week as cron convention: 0=Sun, 1=Mon, … 6=Sat.
// Uses the Tomohiko Sakamoto algorithm.
inline int day_of_week(int y, int m, int d_) {
    static constexpr int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d_) % 7;
}

// Advance a y/m/d/h/min tuple by one minute (in-place).
inline void add_minute(int& year, int& month, int& day, int& hour, int& minute) {
    if (++minute < 60) return;
    minute = 0;
    if (++hour < 24) return;
    hour = 0;
    if (++day <= days_in_month(year, month)) return;
    day = 1;
    if (++month <= 12) return;
    month = 1;
    ++year;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Compute the next time after `from` that matches all five cron fields.
// Returns a new GDateTime* (caller must g_date_time_unref), or nullptr if
// no occurrence is found within 4 years (e.g. cron_day=31, cron_month=2).
inline GDateTime* next_occurrence(const rclone::Job& job, GDateTime* from) {
    auto minutes = detail::expand_field(job.cron_minute,  0, 59);
    auto hours   = detail::expand_field(job.cron_hour,    0, 23);
    auto days    = detail::expand_field(job.cron_day,     1, 31);
    auto months  = detail::expand_field(job.cron_month,   1, 12);
    auto wdays   = detail::expand_field(job.cron_weekday, 0,  6);

    if (minutes.empty() || hours.empty() || days.empty() ||
        months.empty() || wdays.empty())
        return nullptr;

    auto in = [](int v, const std::vector<int>& vec) {
        return std::binary_search(vec.begin(), vec.end(), v);
    };

    // Start one minute after `from`
    int year   = g_date_time_get_year(from);
    int month  = g_date_time_get_month(from);
    int day    = g_date_time_get_day_of_month(from);
    int hour   = g_date_time_get_hour(from);
    int minute = g_date_time_get_minute(from);
    detail::add_minute(year, month, day, hour, minute);

    // Search up to 4 years (~2.1M iterations of cheap integer arithmetic)
    constexpr int MAX_ITER = 366 * 24 * 60 * 4;
    for (int i = 0; i < MAX_ITER; ++i) {
        int wday = detail::day_of_week(year, month, day); // 0=Sun

        if (in(month, months) && in(day, days) && in(wday, wdays) &&
            in(hour, hours)   && in(minute, minutes)) {
            return g_date_time_new_local(year, month, day, hour, minute, 0);
        }
        detail::add_minute(year, month, day, hour, minute);
    }
    return nullptr;
}

// Return a human-readable description of the cron schedule in `job`.
inline std::string describe(const rclone::Job& job) {
    const auto& M  = job.cron_minute;
    const auto& H  = job.cron_hour;
    const auto& D  = job.cron_day;
    const auto& Mo = job.cron_month;
    const auto& W  = job.cron_weekday;

    auto is_star = [](const std::string& s) { return s == "*"; };

    // Try to parse a single integer from a field
    auto to_int = [](const std::string& s, int& out) -> bool {
        try { out = std::stoi(s); return true; } catch (...) { return false; }
    };

    if (is_star(M) && is_star(H) && is_star(D) && is_star(Mo) && is_star(W))
        return "Every minute";

    int h = 0, m = 0;
    bool spec_h = to_int(H, h);
    bool spec_m = to_int(M, m);
    bool all_dom = is_star(D);
    bool all_mo  = is_star(Mo);
    bool all_w   = is_star(W);

    // Every hour at :MM
    if (spec_m && is_star(H) && all_dom && all_mo && all_w)
        return std::format("Every hour at :{:02d}", m);

    // Every day at HH:MM
    if (spec_h && spec_m && all_dom && all_mo && all_w)
        return std::format("Every day at {:02d}:{:02d}", h, m);

    // Every [weekday] at HH:MM
    if (spec_h && spec_m && all_dom && all_mo && !is_star(W)) {
        constexpr const char* wnames[] = {"Sunday","Monday","Tuesday","Wednesday",
                                          "Thursday","Friday","Saturday"};
        int w = 0;
        if (to_int(W, w) && w >= 0 && w <= 6)
            return std::format("Every {} at {:02d}:{:02d}", wnames[w], h, m);
    }

    // Monthly on day D at HH:MM
    if (spec_h && spec_m && !all_dom && all_mo && all_w) {
        int d = 0;
        if (to_int(D, d))
            return std::format("Monthly on day {} at {:02d}:{:02d}", d, h, m);
    }

    // Fallback: show the expression
    return std::format("{} {} {} {} {}", M, H, D, Mo, W);
}

} // namespace mtsync::cron
