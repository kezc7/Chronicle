#pragma once

#include "PCH.h"

namespace MessageBuilder
{
    // Build a JSON string for a chat message. speaker may be empty for system messages.
    inline std::string Build(std::string_view a_channel,
                             std::string_view a_speaker,
                             std::string_view a_message)
    {
        nlohmann::json j;
        j["channel"] = a_channel;
        j["speaker"] = a_speaker.empty() ? nullptr : nlohmann::json(std::string(a_speaker));
        j["message"] = a_message;

        // Unix timestamp in milliseconds
        j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();

        return j.dump();
    }
}
