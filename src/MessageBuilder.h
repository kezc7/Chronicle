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
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

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

    // A mod that writes Windows-1252 does it on every line, so the log names
    // each speaker once.
    class OncePerSpeaker
    {
    public:
        bool First(std::string_view a_speaker)
        {
            std::scoped_lock lock(m_mutex);
            if (m_seen.size() >= kMax) return false;
            return m_seen.emplace(a_speaker).second;
        }

    private:
        static constexpr std::size_t kMax = 512;
        std::mutex m_mutex;
        std::unordered_set<std::string> m_seen;
    };

    inline OncePerSpeaker& Warned()
    {
        static OncePerSpeaker instance;
        return instance;
    }

    // Valid UTF-8 for the log, cut on a whole character.
    inline std::string ShownForLog(std::string_view a_value, std::size_t a_limit = 160)
    {
        if (a_value.size() <= a_limit) return std::string(a_value);
        std::size_t cut = a_limit;
        while (cut > 0 && ChronicleUtf8::IsContinuation(static_cast<std::uint8_t>(a_value[cut]))) --cut;
        return std::string(a_value.substr(0, cut)) + "...";
    }

    // The warning for text that wasn't valid UTF-8: where, how it was read
    // and what is shown.
    inline std::string DecodingNotice(const ChronicleUtf8::Sanitized& a_result, std::string_view a_value,
                                      std::string_view a_field, std::string_view a_speaker, std::string_view a_text)
    {
        auto notice = fmt::format(
            "Chronicle: {} from '{}' is not valid UTF-8 (first bad byte 0x{:02X} at {}); read as {}: {} byte(s) decoded",
            a_field, EscapeForLog(a_speaker), a_result.firstInvalidByte, a_result.firstInvalidOffset,
            ChronicleUtf8::kFallbackDecoding, a_result.fromWindows1252);
        if (a_result.replaced) {
            notice += fmt::format(", {} with no meaning there shown as U+FFFD", a_result.replaced);
        }
        notice += fmt::format("; original='{}'; shown='{}'; text='{}'. Later lines from this speaker are decoded "
                              "the same way without a warning.",
            EscapeForLog(a_value), ShownForLog(a_result.value), EscapeForLog(a_text));
        return notice;
    }

    // a_who is the speaker, or the channel for a line without one.
    inline std::string Sanitize(std::string_view a_value, std::string_view a_field,
                                std::string_view a_speaker, std::string_view a_text, std::string_view a_who)
    {
        auto sanitized = ChronicleUtf8::ReplaceInvalid(a_value);
        if (sanitized.changed && Warned().First(a_who)) {
            spdlog::warn("{}", DecodingNotice(sanitized, a_value, a_field, a_speaker, a_text));
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
        const auto who = a_speaker.empty() ? a_channel : a_speaker;
        const auto channel = Sanitize(a_channel, "channel", a_speaker, a_message, who);
        const auto speaker = Sanitize(a_speaker, "speaker", a_speaker, a_message, who);
        const auto message = Sanitize(StripTrailingRefMarker(a_message), "message/text", a_speaker, a_message, who);

        nlohmann::json j;
        j["channel"] = channel;
        j["speaker"] = speaker.empty() ? nullptr : nlohmann::json(speaker);
        j["message"] = message;

        j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();

        // Keep a second safety net in case another producer bypasses the
        // sanitizer in the future. The replace handler makes dump non-throwing
        // for malformed UTF-8 stored in JSON strings.
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}
