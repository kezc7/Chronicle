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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ChronicleUtf8
{
    struct Sanitized
    {
        std::string value;
        bool changed{ false };
        std::size_t firstInvalidOffset{ std::string_view::npos };
        std::uint8_t firstInvalidByte{ 0 };
    };

    inline bool IsContinuation(std::uint8_t a_byte)
    {
        return (a_byte & 0xC0u) == 0x80u;
    }

    // Replace malformed UTF-8 with U+FFFD. Invalid bytes are consumed one at
    // a time so a bad subtitle can never make the returned string invalid.
    inline Sanitized ReplaceInvalid(std::string_view a_input)
    {
        Sanitized result;
        result.value.reserve(a_input.size());

        auto replace = [&](std::size_t a_offset) {
            const auto byte = static_cast<std::uint8_t>(
                static_cast<unsigned char>(a_input[a_offset]));
            if (!result.changed) {
                result.firstInvalidOffset = a_offset;
                result.firstInvalidByte = byte;
            }
            result.changed = true;
            result.value += "\xEF\xBF\xBD";
        };

        std::size_t i = 0;
        while (i < a_input.size()) {
            const auto lead = static_cast<std::uint8_t>(
                static_cast<unsigned char>(a_input[i]));

            if (lead <= 0x7Fu) {
                result.value.push_back(a_input[i++]);
                continue;
            }

            std::size_t length = 0;
            bool validSecondRange = true;
            if (lead >= 0xC2u && lead <= 0xDFu) {
                length = 2;
            } else if (lead == 0xE0u) {
                length = 3;
                validSecondRange = i + 1 < a_input.size() &&
                                    static_cast<std::uint8_t>(
                                        static_cast<unsigned char>(a_input[i + 1])) >= 0xA0u;
            } else if (lead >= 0xE1u && lead <= 0xECu) {
                length = 3;
            } else if (lead == 0xEDu) {
                length = 3;
                validSecondRange = i + 1 < a_input.size() &&
                                    static_cast<std::uint8_t>(
                                        static_cast<unsigned char>(a_input[i + 1])) <= 0x9Fu;
            } else if (lead >= 0xEEu && lead <= 0xEFu) {
                length = 3;
            } else if (lead == 0xF0u) {
                length = 4;
                validSecondRange = i + 1 < a_input.size() &&
                                    static_cast<std::uint8_t>(
                                        static_cast<unsigned char>(a_input[i + 1])) >= 0x90u;
            } else if (lead >= 0xF1u && lead <= 0xF3u) {
                length = 4;
            } else if (lead == 0xF4u) {
                length = 4;
                validSecondRange = i + 1 < a_input.size() &&
                                    static_cast<std::uint8_t>(
                                        static_cast<unsigned char>(a_input[i + 1])) <= 0x8Fu;
            }

            const bool enoughBytes = length != 0 && i + length <= a_input.size();
            bool valid = enoughBytes && validSecondRange;
            if (valid) {
                for (std::size_t j = 1; j < length; ++j) {
                    if (!IsContinuation(static_cast<std::uint8_t>(
                            static_cast<unsigned char>(a_input[i + j])))) {
                        valid = false;
                        break;
                    }
                }
            }

            if (!valid) {
                replace(i++);
                continue;
            }

            result.value.append(a_input.data() + i, length);
            i += length;
        }

        return result;
    }
}
