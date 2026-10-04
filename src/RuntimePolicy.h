#pragma once
#include <string>
#include <string_view>
#include <unordered_set>

namespace ChronicleRuntime
{
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

    // The game's "My Games" folder name is not initialised yet when SKSE loads
    // plugins and reads back as "Skyrim.INI". Fall back to the standard folder
    // for an empty or INI-file-looking name; otherwise keep the game's (VR, GOG).
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
