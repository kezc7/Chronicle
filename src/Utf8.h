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
        // Bytes read as Windows-1252, and bytes that have no meaning there
        // either and became U+FFFD.
        std::size_t fromWindows1252{ 0 };
        std::size_t replaced{ 0 };
    };

    inline constexpr std::string_view kFallbackDecoding = "Windows-1252";

    inline bool IsContinuation(std::uint8_t a_byte)
    {
        return (a_byte & 0xC0u) == 0x80u;
    }

    // At most a_limit bytes of a_text, never ending inside a character
    // (CHRON-39). Valid UTF-8 in, valid UTF-8 out.
    inline std::string_view Cut(std::string_view a_text, std::size_t a_limit)
    {
        if (a_text.size() <= a_limit) return a_text;
        std::size_t cut = a_limit;
        while (cut > 0 && IsContinuation(static_cast<std::uint8_t>(a_text[cut]))) --cut;
        return a_text.substr(0, cut);
    }

    // The character a byte stands for in Windows-1252, the code page of
    // English and other Western European plugins. 0 for the five bytes it
    // leaves undefined: 0x81, 0x8D, 0x8F, 0x90 and 0x9D.
    inline char32_t Windows1252(std::uint8_t a_byte)
    {
        static constexpr char16_t kHigh[32] = {
            0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
            0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178
        };
        if (a_byte < 0x80u || a_byte >= 0xA0u) return a_byte;
        return kHigh[a_byte - 0x80u];
    }

    // UTF-8 for a character of the Basic Multilingual Plane.
    inline void Append(std::string& a_out, char32_t a_char)
    {
        if (a_char < 0x80u) {
            a_out.push_back(static_cast<char>(a_char));
        } else if (a_char < 0x800u) {
            a_out.push_back(static_cast<char>(0xC0u | (a_char >> 6u)));
            a_out.push_back(static_cast<char>(0x80u | (a_char & 0x3Fu)));
        } else {
            a_out.push_back(static_cast<char>(0xE0u | (a_char >> 12u)));
            a_out.push_back(static_cast<char>(0x80u | ((a_char >> 6u) & 0x3Fu)));
            a_out.push_back(static_cast<char>(0x80u | (a_char & 0x3Fu)));
        }
    }

    // Make text valid UTF-8. Mods hand over plugin strings as the game stores
    // them, in Windows-1252, so a byte that isn't part of a valid UTF-8
    // sequence is read as Windows-1252: 0x92 is a curly apostrophe, 0xE9 is
    // an e acute. Only the five bytes with no meaning there become U+FFFD.
    // Valid sequences are kept as they are, so a UTF-8 line cut in the middle
    // of a character keeps the rest of its text. Bytes are consumed one at a
    // time, so no input can make the returned string invalid.
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
            if (const auto character = Windows1252(byte)) {
                Append(result.value, character);
                ++result.fromWindows1252;
            } else {
                result.value += "\xEF\xBF\xBD";
                ++result.replaced;
            }
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
