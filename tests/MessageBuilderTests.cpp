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
    // 0xC1 is never valid UTF-8; in Windows-1252 it is A acute.
    Require(sanitized.value == "Before \xC3\x81 after");
    Require(sanitized.fromWindows1252 == 1 && sanitized.replaced == 0);

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

    // Text that isn't valid UTF-8 is read as Windows-1252.
    using ChronicleUtf8::ReplaceInvalid;
    const auto apostrophe = ReplaceInvalid("I\x92m ready, Master!");
    Require(apostrophe.value == "I\xE2\x80\x99m ready, Master!");
    Require(apostrophe.changed && apostrophe.firstInvalidOffset == 1 && apostrophe.firstInvalidByte == 0x92);
    Require(apostrophe.fromWindows1252 == 1 && apostrophe.replaced == 0);
    Require(ReplaceInvalid("\x93Hello\x94").value == "\xE2\x80\x9CHello\xE2\x80\x9D");
    Require(ReplaceInvalid("\x91Hello\x92").value == "\xE2\x80\x98Hello\xE2\x80\x99");
    Require(ReplaceInvalid("one \x96 two \x97 three").value == "one \xE2\x80\x93 two \xE2\x80\x94 three");
    Require(ReplaceInvalid("caf\xE9").value == "caf\xC3\xA9");
    Require(ReplaceInvalid("\xE9t\xE9 \xE0 No\xEBl").value == "\xC3\xA9t\xC3\xA9 \xC3\xA0 No\xC3\xABl");
    Require(ReplaceInvalid("wait\x85").value == "wait\xE2\x80\xA6");
    Require(ReplaceInvalid("\x80").value == "\xE2\x82\xAC");
    Require(ReplaceInvalid("\x9F").value == "\xC5\xB8");
    Require(ReplaceInvalid("\xA0").value == "\xC2\xA0");
    Require(ReplaceInvalid("\xFF").value == "\xC3\xBF");

    // Valid UTF-8 is left alone, curly apostrophe and dragon included.
    const std::string untouched = "Caf\xC3\xA9, I\xE2\x80\x99m here \xF0\x9F\x90\x89";
    const auto same = ReplaceInvalid(untouched);
    Require(same.value == untouched && !same.changed);
    Require(same.fromWindows1252 == 0 && same.replaced == 0 && same.firstInvalidOffset == std::string_view::npos);

    // Mixed: the valid sequences stay, the other bytes are read as
    // Windows-1252, and a byte with no meaning there is U+FFFD.
    const auto mixed = ReplaceInvalid("Caf\xC3\xA9, I\x92m here \x81 \xF0\x9F\x90\x89");
    Require(mixed.value == "Caf\xC3\xA9, I\xE2\x80\x99m here \xEF\xBF\xBD \xF0\x9F\x90\x89");
    Require(mixed.changed && mixed.firstInvalidOffset == 8 && mixed.firstInvalidByte == 0x92);
    Require(mixed.fromWindows1252 == 1 && mixed.replaced == 1);
    // A UTF-8 line cut inside a character keeps the text before the cut.
    Require(ReplaceInvalid("\xD0\x9F\xD1\x80\xD0").value == "\xD0\x9F\xD1\x80\xC3\x90");

    // Only the five undefined bytes become U+FFFD.
    int undefined = 0;
    for (int byte = 0x80; byte <= 0xFF; ++byte) {
        const auto one = ReplaceInvalid(std::string(1, static_cast<char>(byte)));
        Require(one.changed && one.fromWindows1252 + one.replaced == 1);
        if (one.replaced) {
            ++undefined;
            Require(one.value == "\xEF\xBF\xBD");
            Require(byte == 0x81 || byte == 0x8D || byte == 0x8F || byte == 0x90 || byte == 0x9D);
        } else {
            Require(one.value != "\xEF\xBF\xBD");
        }
    }
    Require(undefined == 5);

    // The line as the page gets it.
    const auto laci = nlohmann::json::parse(MessageBuilder::Build("dialogue", "Laci", "I\x92m ready, Master!"));
    Require(laci.at("message") == "I\xE2\x80\x99m ready, Master!");
    Require(laci.at("speaker") == "Laci");

    // The warning names the decoding, and each speaker gets it once.
    const auto notice = MessageBuilder::DecodingNotice(apostrophe, "I\x92m ready, Master!", "message/text", "Laci",
        "I\x92m ready, Master!");
    Require(notice.find("Windows-1252") != std::string::npos);
    Require(notice.find("from 'Laci'") != std::string::npos);
    Require(notice.find("0x92 at 1") != std::string::npos);
    Require(notice.find("original='I\\x92m ready, Master!'") != std::string::npos);
    Require(notice.find("shown='I\xE2\x80\x99m ready, Master!'") != std::string::npos);
    Require(notice.find("U+FFFD") == std::string::npos);
    Require(MessageBuilder::DecodingNotice(mixed, "x", "message/text", "Laci", "x")
                .find("1 with no meaning there shown as U+FFFD") != std::string::npos);
    MessageBuilder::OncePerSpeaker once;
    Require(once.First("Laci"));
    Require(!once.First("Laci"));
    Require(once.First("Lydia"));
    Require(!once.First("Lydia") && !once.First("Laci"));
    Require(MessageBuilder::ShownForLog(std::string(159, 'a') + "\xC3\xA9tail") == std::string(159, 'a') + "...");
    // CHRON-39: a byte cut never ends inside a character.
    Require(ChronicleUtf8::Cut("abc", 5) == "abc" && ChronicleUtf8::Cut("abc", 3) == "abc" && ChronicleUtf8::Cut("abc", 2) == "ab");
    Require(ChronicleUtf8::Cut("a\xC3\xA9", 2) == "a");                        // é is two bytes
    Require(ChronicleUtf8::Cut("a\xC3\xA9", 3) == "a\xC3\xA9");
    Require(ChronicleUtf8::Cut("\xE2\x80\x94x", 1).empty() && ChronicleUtf8::Cut("\xE2\x80\x94x", 2).empty());  // an em dash: three bytes
    Require(ChronicleUtf8::Cut("\xE2\x80\x94x", 3) == "\xE2\x80\x94");
    Require(ChronicleUtf8::Cut("\xF0\x9F\x90\x89!", 3).empty());             // four bytes: cut before it
    {
        // 199 ASCII bytes and a two-byte letter: a 200-byte cut keeps 199, still valid.
        const auto help = std::string(199, 'h') + "\xC3\xA9";
        const auto cut = ChronicleUtf8::Cut(help, 200);
        Require(cut.size() == 199 && ChronicleUtf8::ReplaceInvalid(cut).value == cut);
    }
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
