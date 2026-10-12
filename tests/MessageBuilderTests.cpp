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

#include "MessageBuilder.h"

#include <stdexcept>
#include <nlohmann/json.hpp>
#include <string>

void Require(bool ok) { if (!ok) throw std::runtime_error("UTF-8 regression"); }
int main()
{
    std::string malformed = "Before ";
    malformed.push_back(static_cast<char>(0xC1));
    malformed += " after";

    const auto sanitized = ChronicleUtf8::ReplaceInvalid(malformed);
    Require(sanitized.changed);
    Require(sanitized.firstInvalidOffset == 7);
    Require(sanitized.firstInvalidByte == 0xC1);
    Require(sanitized.value == "Before \xEF\xBF\xBD after");

    // The exact sanitized value must be safe for nlohmann::json serialization.
    const nlohmann::json payload = { { "message", sanitized.value } };
    const auto encoded = payload.dump(-1, ' ', false,
                                      nlohmann::json::error_handler_t::replace);
    const auto roundTrip = nlohmann::json::parse(encoded);
    Require(roundTrip.at("message").get<std::string>() == sanitized.value);
    const auto full = nlohmann::json::parse(MessageBuilder::Build(malformed, malformed, malformed));
    Require(full.at("channel") == sanitized.value);
    Require(full.at("speaker") == sanitized.value);
    Require(full.at("message") == sanitized.value);
    Require(MessageBuilder::EscapeForLog("\n\xC1") == "\\x0A\\xC1");
    for (int byte = 0; byte <= 255; ++byte) {
        const std::string value(1, static_cast<char>(byte));
        const auto safe = ChronicleUtf8::ReplaceInvalid(value);
        Require(!ChronicleUtf8::ReplaceInvalid(safe.value).changed);
        Require(nlohmann::json::parse(nlohmann::json(safe.value).dump()).get<std::string>() == safe.value);
    }
    for (const auto value : {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82"}) {
        Require(ChronicleUtf8::ReplaceInvalid(value).changed);
    }
    const std::string valid = "Caf\xC3\xA9 \xF0\x9F\x90\x89";
    Require(!ChronicleUtf8::ReplaceInvalid(valid).changed);
    Require(nlohmann::json::parse(MessageBuilder::Build("dialogue", "Lydia", valid)).at("message") == valid);

    // Trailing "[REF]" markers are removed; other text is left alone.
    Require(MessageBuilder::StripTrailingRefMarker("Dovahkiin! [REF]") == "Dovahkiin!");
    Require(MessageBuilder::StripTrailingRefMarker("Hello[REF] \r\n") == "Hello");
    Require(MessageBuilder::StripTrailingRefMarker("Hi [REF] [REF]") == "Hi");
    Require(MessageBuilder::StripTrailingRefMarker("[REF] stays") == "[REF] stays");
    Require(MessageBuilder::StripTrailingRefMarker("No marker") == "No marker");
    Require(MessageBuilder::StripTrailingRefMarker("[REF]").empty());
    Require(nlohmann::json::parse(MessageBuilder::Build("dialogue", "Merida", "Well met. [REF]"))
                .at("message") == "Well met.");
    return 0;
}
