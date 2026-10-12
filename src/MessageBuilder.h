/*
 * Chronicle — Skyrim SE/AE chat and activity log
 * Copyright (C) 2026 kezc7
 *
 * This file is part of Chronicle.
 *
 * Chronicle is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Chronicle is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Chronicle. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Chronicle — Skyrim SE/AE chat and activity log
 * Copyright (C) 2026 kezc7
 *
 * This file is part of Chronicle.
 *
 * Chronicle is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Chronicle is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Chronicle. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "Utf8.h"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace MessageBuilder
{
    inline std::string EscapeForLog(std::string_view a_value, std::size_t a_limit = 160)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string escaped;
        escaped.reserve(std::min(a_value.size(), a_limit));

        const auto count = std::min(a_value.size(), a_limit);
        for (std::size_t i = 0; i < count; ++i) {
            const auto byte = static_cast<unsigned char>(a_value[i]);
            if (byte >= 0x20u && byte <= 0x7Eu && byte != '\\') {
                escaped.push_back(static_cast<char>(byte));
            } else {
                escaped += "\\x";
                escaped.push_back(kHex[(byte >> 4u) & 0x0Fu]);
                escaped.push_back(kHex[byte & 0x0Fu]);
            }
        }
        if (a_value.size() > a_limit) {
            escaped += "...";
        }
        return escaped;
    }

    inline std::string Sanitize(std::string_view a_value, std::string_view a_field,
                                std::string_view a_speaker, std::string_view a_text)
    {
        auto sanitized = ChronicleUtf8::ReplaceInvalid(a_value);
        if (sanitized.changed) {
            spdlog::warn(
                "Chronicle: invalid UTF-8 sanitized in {} at byte {} (0x{:02X}); "
                "original='{}'; replacement='{}'; speaker='{}'; text='{}'",
                a_field,
                sanitized.firstInvalidOffset,
                sanitized.firstInvalidByte,
                EscapeForLog(a_value),
                EscapeForLog(sanitized.value), EscapeForLog(a_speaker), EscapeForLog(a_text));
        }
        return std::move(sanitized.value);
    }

    // Some game/mod text arrives with a trailing "[REF]" marker that Skyrim's
    // own subtitle display hides. Remove it (and any whitespace around it).
    inline std::string_view StripTrailingRefMarker(std::string_view a_value)
    {
        constexpr std::string_view kMarker = "[REF]";
        constexpr std::string_view kSpace = " \t\r\n";
        while (true) {
            const auto end = a_value.find_last_not_of(kSpace);
            a_value = end == std::string_view::npos ? std::string_view{} : a_value.substr(0, end + 1);
            if (!a_value.ends_with(kMarker)) return a_value;
            a_value.remove_suffix(kMarker.size());
        }
    }

    // Build a JSON string for a chat message. speaker may be empty for system messages.
    inline std::string Build(std::string_view a_channel,
                             std::string_view a_speaker,
                             std::string_view a_message)
    {
        const auto channel = Sanitize(a_channel, "channel", a_speaker, a_message);
        const auto speaker = Sanitize(a_speaker, "speaker", a_speaker, a_message);
        const auto message = Sanitize(StripTrailingRefMarker(a_message), "message/text", a_speaker, a_message);

        nlohmann::json j;
        j["channel"] = channel;
        j["speaker"] = speaker.empty() ? nullptr : nlohmann::json(speaker);
        j["message"] = message;

        // Unix timestamp in milliseconds
        j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();

        // Keep a second safety net in case another producer bypasses the
        // sanitizer in the future. The replace handler makes dump non-throwing
        // for malformed UTF-8 stored in JSON strings.
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}
