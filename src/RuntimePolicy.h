#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ChronicleRuntime
{
    // HUDNotifications queue entries carry a HUDData::Type. Skill increases
    // (20) and Words of Power (21, 22) are character progression and go to
    // System; items, quests and script notifications stay Notifications.
    // Mods format notifications with Scaleform HTML (e.g. <font color='...'>),
    // which the HUD renders but Chronicle would show literally. Drop tags,
    // decode common entities and trim. A '<' with no closing '>' is kept.
    inline std::string StripHtml(std::string_view in)
    {
        static constexpr std::pair<std::string_view, char> entities[] = {
            { "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' },
            { "&apos;", '\'' }, { "&#39;", '\'' }, { "&nbsp;", ' ' }
        };
        std::string out;
        out.reserve(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '<') {
                if (const auto close = in.find('>', i); close != std::string_view::npos) {
                    i = close;
                    continue;
                }
            } else if (in[i] == '&') {
                bool decoded = false;
                for (const auto& [name, ch] : entities) {
                    if (in.substr(i).starts_with(name)) {
                        out += ch;
                        i += name.size() - 1;
                        decoded = true;
                        break;
                    }
                }
                if (decoded) continue;
            }
            out += in[i];
        }
        const auto first = out.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        return out.substr(first, out.find_last_not_of(" \t\r\n") - first + 1);
    }

    // HUDData types worth logging: notifications (1), quest and objective
    // updates (14-19), skill increases (20) and Words of Power (21, 22).
    // Crosshair, activate prompts and subtitles are left out.
    inline bool IsNotificationType(std::uint32_t type)
    {
        return type == 1 || (type >= 14 && type <= 22);
    }

    inline std::string_view NotificationChannel(std::uint32_t type)
    {
        return type == 20 || type == 21 || type == 22 ? "system" : "notification";
    }

    // Combat channel: allies are player teammates (followers) and actors the
    // player commands (summons). Deaths are reported when the player or an ally
    // dies or kills; fights between other actors stay out to avoid noise.
    enum class Side { Other, Player, Ally };

    inline bool ReportDeath(Side dying, Side killer)
    {
        return dying != Side::Other || killer != Side::Other;
    }

    // Quest and objective HUD messages carry only the name, so label them.
    inline std::string_view NotificationPrefix(std::uint32_t type)
    {
        switch (type) {
        case 14: return "Quest started: ";
        case 15: return "Quest completed: ";
        case 16: return "Quest failed: ";
        case 17: return "New objective: ";
        case 18: return "Objective completed: ";
        case 19: return "Objective failed: ";
        default: return "";
        }
    }

    // Text a console command appended to the console log. The log normally just
    // grows; if the game trimmed its oldest text, locate the old tail instead.
    inline std::string ConsoleOutputSince(std::string_view before, std::string_view after)
    {
        if (before.empty()) return std::string(after);
        const auto tail = before.substr(before.size() - std::min<std::size_t>(before.size(), 256));
        if (after.size() >= before.size() && after.substr(before.size() - tail.size(), tail.size()) == tail) {
            return std::string(after.substr(before.size()));
        }
        const auto pos = after.find(tail);
        return pos == std::string_view::npos ? std::string{} : std::string(after.substr(pos + tail.size()));
    }

    // Non-empty console lines, capped so a huge "help" listing cannot flush
    // the history; a final line says how many more are in Skyrim's console.
    inline std::vector<std::string> ConsoleLines(std::string_view output, std::size_t limit)
    {
        std::vector<std::string> lines;
        std::size_t more = 0;
        while (!output.empty()) {
            const auto end = output.find('\n');
            auto line = output.substr(0, end);
            output = end == std::string_view::npos ? std::string_view{} : output.substr(end + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
            if (line.empty()) continue;
            if (lines.size() < limit) lines.emplace_back(line);
            else ++more;
        }
        if (more) lines.push_back("... " + std::to_string(more) + " more lines - open the console (~) to see them all");
        return lines;
    }

    inline bool IsTransition(std::string_view name)
    {
        return name == "Main Menu" || name == "Loading Menu" ||
               name == "LoadWaitSpinner" || name == "Fader Menu";
    }

    inline bool IsCrafting(std::string_view name)
    {
        return name == "CraftingMenu" || name == "Crafting Menu" ||
            name == "SmithingMenu" || name == "Smithing Menu" ||
            name == "AlchemyMenu" || name == "Alchemy Menu" ||
            name == "EnchantingMenu" || name == "Enchanting Menu" ||
            name == "CookingMenu" || name == "Cooking Menu";
    }

    inline bool IsBlocking(std::string_view name)
    {
        return IsTransition(name) || name == "SystemMenu" || name == "TweenMenu" ||
            name == "InventoryMenu" || name == "MagicMenu" || name == "MapMenu" ||
            name == "ContainerMenu" || name == "Journal Menu" || name == "LevelUpMenu" ||
            name == "StatsMenu" || name == "SleepWaitMenu" || name == "FavoritesMenu" ||
            name == "BarterMenu" || IsCrafting(name) || name == "PhotoMenu" ||
            // Meridian UI opens this modal menu while one of its web views
            // (e.g. Wayframe) has focus. PrismaUI draws above Meridian, so
            // without it Chronicle stays on top of those menus.
            name == "MeridianUI_FocusMenu";
    }

    inline bool InterruptsFocus(std::string_view name)
    {
        return name != "PrismaUI_FocusMenu" && name != "HUD Menu" && name != "Cursor Menu";
    }

    // SKSE::log::log_directory() reads the "My Games" folder name via AE ID
    // 380738, which is absent from the 1.6.1170 address library; the lookup
    // resolves to a neighbouring string, "Skyrim.INI". Fall back to the standard
    // folder for an empty or INI-file-looking name; otherwise keep it (VR, GOG).
    inline std::wstring MyGamesFolder(std::wstring_view name)
    {
        const auto lower = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? wchar_t(c + 32) : c; };
        const bool ini = name.size() >= 4 && name[name.size() - 4] == L'.' &&
            lower(name[name.size() - 3]) == L'i' && lower(name[name.size() - 2]) == L'n' &&
            lower(name[name.size() - 1]) == L'i';
        if (name.empty() || ini || name.find_first_of(L"\\/:") != std::wstring_view::npos)
            return L"Skyrim Special Edition";
        return std::wstring(name);
    }

    struct State
    {
        bool loaded = false;
        bool ready = false;
        bool hideMenus = true;
        bool hidePhoto = true;
        std::unordered_set<std::string> menus;

        void Menu(std::string_view name, bool opening)
        {
            if (!IsBlocking(name)) return;
            if (opening) menus.emplace(name);
            else menus.erase(std::string(name));
        }

        bool Visible() const
        {
            if (!loaded || !ready) return false;
            for (const auto& name : menus) {
                if (IsTransition(name) || (name == "PhotoMenu" ? hidePhoto : hideMenus)) return false;
            }
            return true;
        }
    };
}
