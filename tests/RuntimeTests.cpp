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

#include "RuntimePolicy.h"
#include <stdexcept>
#include <string>

void Require(bool value) { if (!value) throw std::runtime_error("Runtime policy regression"); }
int main()
{
    ChronicleRuntime::State state;
    state.ready = true;
    Require(!state.Visible()); // DOM ready at main menu cannot display the overlay.
    state.loaded = true;
    Require(state.Visible());
    state.Menu("Main Menu", true);
    state.Menu("Fader Menu", false); // unmatched close must not expose the overlay
    state.hideMenus = false;
    Require(!state.Visible());
    state.Menu("Main Menu", false);
    Require(state.Visible());
    state.Menu("InventoryMenu", true);
    Require(state.Visible());
    state.Menu("PhotoMenu", true); // nested menu applies its own hide preference
    Require(!state.Visible());
    state.Menu("PhotoMenu", false);
    Require(state.Visible());
    state.hideMenus = true;
    state.Menu("Crafting Menu", true);
    Require(!state.Visible());
    state.Menu("Crafting Menu", false);
    state.Menu("SmithingMenu", true);
    Require(!state.Visible());
    state.Menu("SmithingMenu", false);
    state.Menu("Enchanting Menu", true);
    Require(!state.Visible());
    state.Menu("Enchanting Menu", false);
    state.Menu("InventoryMenu", true); // duplicate notification is idempotent
    state.Menu("InventoryMenu", false);
    Require(state.Visible());
    state.Menu("MeridianUI_FocusMenu", true); // a focused Meridian UI view (e.g. Wayframe)
    Require(!state.Visible());
    state.Menu("MeridianUI_FocusMenu", false);
    Require(state.Visible());
    state.Menu("Console", true); // Skyrim's own console hides Chronicle like a menu
    Require(!state.Visible());
    state.Menu("Console", false);
    Require(state.Visible());
    state.Menu("RaceSex Menu", true); // character creation / RaceMenu
    Require(!state.Visible());
    state.Menu("RaceSex Menu", false);
    state.Menu("Book Menu", true);
    Require(!state.Visible());
    state.Menu("Book Menu", false);
    state.Menu("Dialogue Menu", true); // conversations stay visible
    Require(state.Visible());
    state.Menu("Dialogue Menu", false);
    Require(state.Visible());
    state.userHidden = true; // show/hide hotkey
    Require(!state.Visible());
    state.userHidden = false;
    Require(state.Visible());
    state.Menu("Loading Menu", true);
    Require(!state.Visible());
    state.Menu("Loading Menu", false);
    state.loaded = false;
    Require(!state.Visible());
    Require(!ChronicleRuntime::InterruptsFocus("PrismaUI_FocusMenu"));
    Require(ChronicleRuntime::InterruptsFocus("InventoryMenu"));
    // Menus opened by other mods: overlays keep Chronicle's focus, input menus take it.
    Require(!ChronicleRuntime::InterruptsFocus("SomeMinimapMenu", 0));            // HUD widget
    Require(!ChronicleRuntime::InterruptsFocus("TrueHUD", 1u << 1 | 1u << 8));     // always open, requires update
    Require(ChronicleRuntime::InterruptsFocus("Dialogue Menu", 1u << 2));         // uses cursor
    Require(ChronicleRuntime::InterruptsFocus("MessageBoxMenu", 1u << 4));        // modal
    Require(ChronicleRuntime::InterruptsFocus("CustomMenu", 1u << 0));            // pauses the game
    Require(ChronicleRuntime::InterruptsFocus("Favorites", 1u << 3));             // menu input context
    Require(ChronicleRuntime::InterruptsFocus("UnknownModMenu"));                 // flags unknown
    Require(ChronicleRuntime::InterruptsFocus("Console", 0));                     // blocking by name
    Require(!ChronicleRuntime::InterruptsFocus("PrismaUI_FocusMenu", ChronicleRuntime::kInputMenuFlags));
    // Focus granted but not reported yet is not a loss until the grace period ends.
    Require(!ChronicleRuntime::FocusLost(false, false, 0));
    Require(!ChronicleRuntime::FocusLost(false, false, ChronicleRuntime::kFocusGraceMs - 1));
    Require(ChronicleRuntime::FocusLost(false, false, ChronicleRuntime::kFocusGraceMs));
    Require(!ChronicleRuntime::FocusLost(true, true, 5000));
    Require(ChronicleRuntime::FocusLost(false, true, 0));  // seen, then taken away
    // NPC speech radius: zero keeps the legacy unlimited behaviour and
    // positive values include the boundary while invalid/large values clamp.
    Require(ChronicleRuntime::NormalizeDialogueRadius(0.0F) == 0.0F);
    Require(ChronicleRuntime::NormalizeDialogueRadius(6000.0F) == ChronicleRuntime::kMaxDialogueRadius);
    Require(ChronicleRuntime::WithinDialogueRadius(500.0F, 0.0F));
    Require(ChronicleRuntime::WithinDialogueRadius(500.0F, 500.0F));
    Require(!ChronicleRuntime::WithinDialogueRadius(500.1F, 500.0F));
    // Combat deaths: player and followers in, unrelated fights out.
    using ChronicleRuntime::Side;
    Require(ChronicleRuntime::ReportDeath(Side::Other, Side::Ally));   // Serana kills a vampire
    Require(ChronicleRuntime::ReportDeath(Side::Ally, Side::Other));   // a vampire kills Serana
    Require(ChronicleRuntime::ReportDeath(Side::Other, Side::Player));
    Require(ChronicleRuntime::ReportDeath(Side::Player, Side::Other));
    Require(!ChronicleRuntime::ReportDeath(Side::Other, Side::Other)); // guards vs. a dragon
    // Player dialogue choices: logged only when the NPC answers that choice.
    {
        int infoA = 0, infoB = 0, topicA = 0, topicB = 0;
        using ChronicleRuntime::AnswersChoice;
        Require(AnswersChoice(&infoA, &topicA, &infoA, &topicA));   // NPC answers the chosen option
        Require(AnswersChoice(&infoB, &topicA, &infoA, &topicA));   // another answer of the same topic
        Require(!AnswersChoice(&infoB, &topicB, &infoA, &topicA));  // greeting / unrelated line
        Require(!AnswersChoice(nullptr, nullptr, &infoA, &topicA)); // NPC not talking
        Require(!AnswersChoice(&infoB, nullptr, &infoA, nullptr));  // unknown topics never match
    }
    // Scaleform HTML in mod notifications.
    Require(ChronicleRuntime::StripHtml("<font color='#00FFFFFF'>The Curators Companion: Start up tasks complete...</font>")
        == "The Curators Companion: Start up tasks complete...");
    Require(ChronicleRuntime::StripHtml("Fish &amp; Chips &lt;3") == "Fish & Chips <3");
    Require(ChronicleRuntime::StripHtml("  <b></b>  ") == "");
    Require(ChronicleRuntime::StripHtml("a < b") == "a < b"); // unclosed '<' is text
    Require(ChronicleRuntime::StripHtml("Iron Sword added") == "Iron Sword added");
    // HUD message filtering: notifications, quests and progression only.
    Require(ChronicleRuntime::IsNotificationType(1));
    Require(ChronicleRuntime::IsNotificationType(14));
    Require(ChronicleRuntime::IsNotificationType(22));
    Require(!ChronicleRuntime::IsNotificationType(0));
    Require(!ChronicleRuntime::IsNotificationType(5));  // subtitle
    Require(!ChronicleRuntime::IsNotificationType(24)); // crosshair
    Require(!ChronicleRuntime::IsNotificationType(25));
    // HUD notification routing and text.
    Require(ChronicleRuntime::NotificationChannel(1) == "notification");
    Require(ChronicleRuntime::NotificationChannel(14) == "notification"); // quest started
    Require(ChronicleRuntime::NotificationChannel(20) == "system");       // skill increase
    Require(ChronicleRuntime::NotificationChannel(21) == "system");       // word of power
    Require(ChronicleRuntime::NotificationChannel(22) == "system");
    using ChronicleRuntime::Lang;
    using ChronicleRuntime::Phrase;
    Require(ChronicleRuntime::NotificationText(Lang::En, 17, "Return the proof") == "New objective: Return the proof");
    Require(ChronicleRuntime::NotificationText(Lang::En, 18, "Talk to Orgnar") == "Objective completed: Talk to Orgnar");
    Require(ChronicleRuntime::NotificationText(Lang::En, 1, "Gold added") == "Gold added");
    Require(ChronicleRuntime::NotificationText(Lang::En, 20, "Alchemy") == "Alchemy"); // skill text is complete
    Require(ChronicleRuntime::NotificationText(Lang::Fr, 17, "X") == "Nouvel objectif : X");
    Require(ChronicleRuntime::NotificationText(Lang::De, 15, "X") == "Quest abgeschlossen: X");

    // Native phrases: language resolution and word order per language.
    Require(ChronicleRuntime::ResolveLanguage("auto", "FRENCH") == Lang::Fr);
    Require(ChronicleRuntime::ResolveLanguage("auto", "german") == Lang::De);
    Require(ChronicleRuntime::ResolveLanguage("auto", "RUSSIAN") == Lang::En);
    Require(ChronicleRuntime::ResolveLanguage("de", "FRENCH") == Lang::De);
    Require(ChronicleRuntime::ResolveLanguage("", "") == Lang::En);
    Require(ChronicleRuntime::Phrased(Lang::En, Phrase::Killed, "Vampire") == "killed Vampire");
    Require(ChronicleRuntime::Phrased(Lang::Fr, Phrase::Killed, "Vampire") == "a tué Vampire");
    Require(ChronicleRuntime::Phrased(Lang::De, Phrase::Killed, "Vampire") == "hat Vampire getötet");
    Require(ChronicleRuntime::Phrased(Lang::De, Phrase::ReachedLevel, "12") == "Stufe 12 erreicht");
    Require(ChronicleRuntime::Phrased(Lang::Fr, Phrase::GameSaved) == "Partie sauvegardée");
    Require(ChronicleRuntime::ResolveLanguage("auto", "JAPANESE") == Lang::Ja);
    Require(ChronicleRuntime::ResolveLanguage("auto", "CHINESE") == Lang::Zh);
    Require(ChronicleRuntime::ResolveLanguage("ko", "ENGLISH") == Lang::Ko);
    Require(ChronicleRuntime::Phrased(Lang::Ja, Phrase::Killed, "Vampire") == "Vampire を倒した");
    Require(ChronicleRuntime::Phrased(Lang::Es, Phrase::Killed, "Vampire") == "ha matado a Vampire");
    for (auto lang : { Lang::En, Lang::Fr, Lang::De, Lang::It, Lang::Es, Lang::Pl, Lang::Tr, Lang::Zh, Lang::Ja, Lang::Ko }) {
        for (int p = 0; p <= static_cast<int>(Phrase::MoreLines); ++p) {
            Require(!ChronicleRuntime::PhraseText(lang, static_cast<Phrase>(p)).empty());
        }
    }

    // Console output capture.
    Require(ChronicleRuntime::ConsoleOutputSince("old\n", "old\nnew line\n") == "new line\n");
    Require(ChronicleRuntime::ConsoleOutputSince("", "first\n") == "first\n");
    Require(ChronicleRuntime::ConsoleOutputSince("x\nsame\n", "x\nsame\nsame\n") == "same\n"); // repeated output kept
    {   // the game trimmed the oldest console text before appending
        const std::string before = std::string(300, 'a') + "\nend\n";
        Require(ChronicleRuntime::ConsoleOutputSince(before, before.substr(20) + "new\n") == "new\n");
    }
    Require(ChronicleRuntime::ConsoleOutputSince("abc\n", "unrelated\n").empty());
    {
        const auto lines = ChronicleRuntime::ConsoleLines("a\r\n\nb\nc\nd\n", 2);
        Require(lines.size() == 3 && lines[0] == "a" && lines[1] == "b");
        Require(lines[2] == "... 2 more lines - open the console (~) to see them all");
        Require(ChronicleRuntime::ConsoleLines("", 50).empty());
    }
    // Uninitialised game value during plugin load must not become the log folder.
    Require(ChronicleRuntime::MyGamesFolder(L"Skyrim.INI") == L"Skyrim Special Edition");
    Require(ChronicleRuntime::MyGamesFolder(L"skyrim.ini") == L"Skyrim Special Edition");
    Require(ChronicleRuntime::MyGamesFolder(L"") == L"Skyrim Special Edition");
    Require(ChronicleRuntime::MyGamesFolder(L"..\\x") == L"Skyrim Special Edition");
    Require(ChronicleRuntime::MyGamesFolder(L"Skyrim Special Edition") == L"Skyrim Special Edition");
    Require(ChronicleRuntime::MyGamesFolder(L"Skyrim Special Edition GOG") == L"Skyrim Special Edition GOG");
    Require(ChronicleRuntime::MyGamesFolder(L"Skyrim VR") == L"Skyrim VR");
}
