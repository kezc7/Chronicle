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
#define DOVAH_GAME_THREAD_QUEUE_ONLY
#include "DovahGameThread.h"
#include <map>
#include <optional>
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
    state.Menu("MagelightFocus", true); // a Magelight UI view holding UI mode
    Require(!state.Visible());
    state.ownFocusMenu = "MagelightFocus"; // Magelight build: Chronicle's own view holds it
    state.ownsFocus = true;
    Require(state.Visible());
    state.ownsFocus = false;               // another mod's view holds it
    Require(!state.Visible());
    state.Menu("MagelightFocus", false);
    Require(state.Visible());
    // DFC's panel and wheel use PrismaUI, including with Chronicle on Magelight.
    for (const std::string_view hostMenu : { "PrismaUI_FocusMenu", "MagelightFocus" }) {
        ChronicleRuntime::State hostState;
        hostState.loaded = hostState.ready = true;
        hostState.ownFocusMenu = hostMenu;
        hostState.Menu("PrismaUI_FocusMenu", true);
        Require(!hostState.Visible());
        hostState.hideMenus = false;
        Require(hostState.Visible());
        hostState.hideMenus = true;
        hostState.Menu("PrismaUI_FocusMenu", false);
        Require(hostState.Visible());

        hostState.Menu(hostMenu, true);
        hostState.ownsFocus = true;
        Require(hostState.Visible()); // Chronicle's own input must remain visible.
        hostState.ownsFocus = false;  // Same menu stays open as another view takes focus.
        Require(!hostState.Visible());
        hostState.Menu(hostMenu, false);
        Require(hostState.Visible());

        hostState.ownsFocus = true;
        hostState.Menu("PrismaUI_FocusMenu", true);
        hostState.Menu("MagelightFocus", true);
        Require(!hostState.Visible()); // Owning one host cannot exempt the other host.
        hostState.userHidden = true;
        hostState.Menu("PrismaUI_FocusMenu", false);
        hostState.Menu("MagelightFocus", false);
        Require(!hostState.Visible()); // Closing DFC must not undo the hide hotkey.
        hostState.userHidden = false;
        hostState.apiHidden = true;
        hostState.Menu("PrismaUI_FocusMenu", true);
        hostState.Menu("PrismaUI_FocusMenu", false);
        Require(!hostState.Visible()); // Nor another mod's explicit hide.
        hostState.apiHidden = false;
        Require(hostState.Visible());
    }
    state.Menu("Console", true); // Skyrim's own console hides Chronicle like a menu
    Require(!state.Visible());
    state.Menu("Console", false);
    Require(state.Visible());
    state.Menu("CustomMenu", true); // mod SWF menus (e.g. Swiftly Order Squad); unknown flags count as input
    Require(!state.Visible());
    state.Menu("CustomMenu", false);
    Require(state.Visible());
    state.Menu("CustomMenu", true, 1u << 0); // pauses the game: a command menu
    Require(!state.Visible());
    state.Menu("CustomMenu", false, 1u << 0);
    Require(state.Visible());
    state.Menu("CustomMenu", true, 0); // no flags: an overlay widget, not a menu
    Require(state.Visible());
    Require(!ChronicleRuntime::InterruptsFocus("CustomMenu", 0));
    state.Menu("CustomMenu", false, 0);
    Require(state.Visible());
    state.Menu(ChronicleRuntime::kMenuFrameworkMenu, true); // SKSE Menu Framework window
    Require(!state.Visible());
    Require(ChronicleRuntime::InterruptsFocus(ChronicleRuntime::kMenuFrameworkMenu, 0));
    state.Menu(ChronicleRuntime::kMenuFrameworkMenu, false);
    Require(state.Visible());
    state.apiHidden = true; // another mod's SetVisible(false)
    Require(!state.Visible());
    state.apiHidden = false;
    Require(state.Visible());
    state.Menu(ChronicleRuntime::kDovahHubMenu, true); // DovahHub's page is open
    Require(!state.Visible());
    Require(ChronicleRuntime::InterruptsFocus(ChronicleRuntime::kDovahHubMenu, 0));
    state.Menu(ChronicleRuntime::kDovahHubMenu, false);
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
    Require(!ChronicleRuntime::InterruptsFocus("MagelightFocus", ChronicleRuntime::kInputMenuFlags));
    Require(ChronicleRuntime::HidesChronicle("MagelightFocus", ChronicleRuntime::kInputMenuFlags));
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
        // Four objects of one array. Separate zero-valued locals whose addresses are only compared can
        // share one stack slot under whole-program optimisation (MSVC 14.44 and 14.51 both do it), and
        // the test then compared equal addresses. Game forms are separate objects, so the DLL is unaffected.
        int ids[4] = {};
        int &infoA = ids[0], &infoB = ids[1], &topicA = ids[2], &topicB = ids[3];
        using ChronicleRuntime::AnswersChoice;
        Require(AnswersChoice(&infoA, &topicA, &infoA, &topicA));   // NPC answers the chosen option
        Require(AnswersChoice(&infoB, &topicA, &infoA, &topicA));   // another answer of the same topic
        Require(!AnswersChoice(&infoB, &topicB, &infoA, &topicA));  // greeting / unrelated line
        Require(!AnswersChoice(nullptr, nullptr, &infoA, &topicA)); // NPC not talking
        Require(!AnswersChoice(&infoB, nullptr, &infoA, nullptr));  // unknown topics never match
    }
    // A choice with no prompt text shows its topic's editor ID; those are not logged.
    {
        using ChronicleRuntime::LooksLikeEditorId;
        Require(LooksLikeEditorId("DialogueGenericHello"));
        Require(LooksLikeEditorId("MQ101_Hello") && LooksLikeEditorId("dunHello01Topic"));
        Require(!LooksLikeEditorId("Goodbye.") && !LooksLikeEditorId("Goodbye") && !LooksLikeEditorId("Yes"));
        Require(!LooksLikeEditorId("HELLO") && !LooksLikeEditorId("What happened here?"));
        Require(!LooksLikeEditorId("SALLY, WHAT'S ON YOUR MIND?") && !LooksLikeEditorId(""));
        Require(!LooksLikeEditorId("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));  // UTF-8 text is never an editor ID
    }
    // SkyrimNet narration and the copies of one SkyrimNet line.
    {
        using ChronicleRuntime::IsNarration;
        Require(IsNarration("*Cerberus rolls his shoulder, working the stiffness out of it.*"));
        Require(IsNarration("  *Lydia sighs.* "));
        Require(!IsNarration("*shrugs* Fine.") && !IsNarration("Fine. *shrugs*") && !IsNarration("*a* and *b*"));
        Require(!IsNarration("That Dragonstone won't fetch itself.") && !IsNarration("**") && !IsNarration(""));
        Require(ChronicleRuntime::EchoKey("  *Cerberus  rolls\this Shoulder.* ") == "cerberus rolls his shoulder.");

        ChronicleRuntime::EchoFilter echo;
        // A thought: SkyrimNet's labelled copy first, then the subtitle under the player's name.
        Require(!echo.IsEcho("skyrimnet", "Player Thoughts", "That Dragonstone won't fetch itself.", 1000));
        Require(echo.IsEcho("dialogue", "Cerberus", "That Dragonstone won't fetch itself.", 3000));
        Require(echo.IsEcho("skyrimnet", "Player Thoughts", "That Dragonstone won't fetch itself.", 4000));  // SpeechComplete repeat
        // Narration moved to the narrator channel, then its subtitle copy.
        Require(!echo.IsEcho("narrator", "Narrator", "*Cerberus rolls his shoulder.*", 5000));
        Require(echo.IsEcho("narrator", "Narrator", "*Cerberus rolls his shoulder.*", 6000));
        // The player's typed line, then SkyrimNet's own echo of it.
        Require(!echo.IsEcho("player", "Cerberus", "Lydia, wait here.", 7000));
        Require(echo.IsEcho("skyrimnet", "Cerberus", "Lydia, wait here.", 7500));
        // A whole reply, then a sentence of it from the subtitle (a playtest's Herman lines).
        Require(!echo.IsEcho("skyrimnet", "Herman", "That fancy elf is still breathing! Why are we standing around chatting?", 8000));
        Require(echo.IsEcho("dialogue", "Herman", "That fancy elf is still breathing!", 8500));
        Require(!echo.IsEcho("dialogue", "Herman", "Why?", 8600));  // too short to match inside a longer line
        // Two NPCs with the same vanilla line are both kept; one NPC repeating it is not.
        Require(!echo.IsEcho("dialogue", "Guard", "I used to be an adventurer like you.", 9000));
        Require(!echo.IsEcho("dialogue", "Whiterun Guard", "I used to be an adventurer like you.", 9100));
        Require(echo.IsEcho("dialogue", "Guard", "I used to be an adventurer like you.", 9200));
        // Other channels and old lines are left alone.
        Require(!echo.IsEcho("system", "", "That Dragonstone won't fetch itself.", 9300));
        Require(!echo.IsEcho("dialogue", "Cerberus", "That Dragonstone won't fetch itself.", 1000 + ChronicleRuntime::EchoFilter::kWindowMs + 1));
        echo.Clear();
        Require(!echo.IsEcho("dialogue", "Lydia", "Lydia, wait here.", 7600));
    }
    // SkyrimNet's voice directions and inline narration (a playtest's Coralyn and Zora lines).
    {
        using ChronicleRuntime::StripAudioTags;
        Require(StripAudioTags("[say with a hint of amusement] Herman is right.") == "Herman is right.");
        Require(StripAudioTags("[growl] That fancy elf is still breathing!") == "That fancy elf is still breathing!");
        Require(StripAudioTags("We've made a mess. [chuckle] Where to now?") == "We've made a mess. Where to now?");
        Require(StripAudioTags("[sfx-sigh]Haah, fine [reset].") == "Haah, fine.");
        Require(StripAudioTags("[sigh]") == "" && StripAudioTags("No tags here.") == "No tags here.");
        Require(StripAudioTags("A [ without a close") == "A [ without a close" && StripAudioTags("[] stays") == "[] stays");
        Require(StripAudioTags("[" + std::string(81, 'a') + "] long") == "[" + std::string(81, 'a') + "] long");

        using ChronicleRuntime::SplitNarration;
        auto parts = SplitNarration("Herman is right. *Coralyn glances toward the necromancer* We shouldn't let the target slip away.");
        Require(parts.size() == 3 && !parts[0].narration && parts[0].text == "Herman is right.");
        Require(parts[1].narration && parts[1].text == "*Coralyn glances toward the necromancer*");
        Require(!parts[2].narration && parts[2].text == "We shouldn't let the target slip away.");
        parts = SplitNarration("Whiterun, Solitude. *Lydia swallows hard.*");
        Require(parts.size() == 2 && parts[1].narration);
        parts = SplitNarration("*Cerberus rolls his shoulder.*");
        Require(parts.size() == 1 && parts[0].narration);
        parts = SplitNarration("Five * three is fifteen.");
        Require(parts.size() == 1 && !parts[0].narration && parts[0].text == "Five * three is fifteen.");
        parts = SplitNarration("Plain speech.");
        Require(parts.size() == 1 && !parts[0].narration);
    }
    // Scaleform $ keys in captured notifications (CHRON-12).
    {
        const std::map<std::string, std::string, std::less<>> table{
            { "$Assault has started!", "Assault has started!" },
            { "$SKI_Item", "Iron Sword" },
            { "$Same", "$Same" },  // a translator that hands the key back has no translation
        };
        const auto lookup = [&table](std::string_view key) -> std::optional<std::string> {
            const auto it = table.find(key);
            return it == table.end() ? std::nullopt : std::optional<std::string>(it->second);
        };
        using ChronicleRuntime::TranslateKeys;
        Require(TranslateKeys("$Assault has started!", lookup) == "Assault has started!");
        Require(TranslateKeys("Found $SKI_Item.", lookup) == "Found Iron Sword.");
        Require(TranslateKeys("$Unknown key here", lookup) == "Unknown key here");  // no translation: the "$" goes
        Require(TranslateKeys("$Same", lookup) == "Same");
        Require(TranslateKeys("Cost: 5$ or US$4", lookup) == "Cost: 5$ or US$4");  // "$" inside a word is text
        Require(TranslateKeys("No keys at all.", lookup) == "No keys at all." && TranslateKeys("", lookup) == "");
        Require(TranslateKeys("$", lookup) == "");
    }
    // Which SkyrimNetApi function a typed line goes to.
    {
        using ChronicleRuntime::SkyrimNetSend;
        using ChronicleRuntime::SkyrimNetFunction;
        using ChronicleRuntime::ParseSkyrimNetSend;
        Require(ParseSkyrimNetSend("") == SkyrimNetSend::Dialogue && ParseSkyrimNetSend("dialogue") == SkyrimNetSend::Dialogue);
        Require(ParseSkyrimNetSend("transform") == SkyrimNetSend::Transform && ParseSkyrimNetSend("off") == SkyrimNetSend::Off);
        Require(ParseSkyrimNetSend("Transform") == SkyrimNetSend::Dialogue);  // exact values only
        Require(std::string_view(SkyrimNetFunction(SkyrimNetSend::Dialogue, true)) == "RegisterDialogueToListener");
        Require(std::string_view(SkyrimNetFunction(SkyrimNetSend::Dialogue, false)) == "RegisterDialogue");
        Require(std::string_view(SkyrimNetFunction(SkyrimNetSend::Transform, true)) == "TransformDialogue");
        Require(SkyrimNetFunction(SkyrimNetSend::Off, true) == nullptr);
        Require(std::string_view(ChronicleRuntime::SkyrimNetSendName(SkyrimNetSend::Transform)) == "transform");
        // Who a typed line is said to, by name.
        using ChronicleRuntime::AddressedName;
        Require(AddressedName("sally, what are your thoughts") == "sally");
        Require(AddressedName("  Hey Lydia: wait here") == "Lydia" && AddressedName("hello Serana, ready?") == "Serana");
        Require(AddressedName("Jarl Balgruuf the Greater, I bring news") == "Jarl Balgruuf the Greater");
        Require(AddressedName("J'zargo, a word") == "J'zargo" && AddressedName("Ri'saad: hello") == "Ri'saad");
        Require(AddressedName("what are your thoughts") == "" && AddressedName("Sally?") == "");
        Require(AddressedName("well then you lot over there, listen") == "");  // more than four words
        Require(AddressedName("a, b") == "" && AddressedName(", hello") == "" && AddressedName("123, go") == "");
        Require(AddressedName("this line is long enough that its first comma comes after forty, characters") == "");
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
    // The suite's language codes, with older stored values still read (zh-CN, 5 Nov decision).
    using ChronicleRuntime::CanonicalLanguage;
    Require(CanonicalLanguage("zh-CN") == "zh-CN" && CanonicalLanguage("zh") == "zh-CN" && CanonicalLanguage("zh-Hans") == "zh-CN");
    Require(CanonicalLanguage("Simplified Chinese") == "zh-CN" && CanonicalLanguage(" English ") == "en" && CanonicalLanguage("KOREAN") == "ko");
    Require(CanonicalLanguage("auto") == "auto" && CanonicalLanguage("fr") == "fr" && CanonicalLanguage("klingon").empty());
    Require(ChronicleRuntime::ResolveLanguage("zh-CN", "") == Lang::Zh && ChronicleRuntime::ResolveLanguage("zh", "") == Lang::Zh);
    Require(ChronicleRuntime::ResolveLanguage("German", "FRENCH") == Lang::De);
    Require(ChronicleRuntime::ResolveLanguage("auto", "CHINESE") == Lang::Zh);
    Require(ChronicleRuntime::ResolveLanguage("auto", "FRENCH") == Lang::Fr);
    Require(ChronicleRuntime::ResolveLanguage("auto", "german") == Lang::De);
    Require(ChronicleRuntime::ResolveLanguage("auto", "RUSSIAN") == Lang::En);
    Require(ChronicleRuntime::ResolveLanguage("de", "FRENCH") == Lang::De);
    Require(ChronicleRuntime::ResolveLanguage("", "") == Lang::En);
    // Codes round-trip, so the DovahHub tab and the chat window agree on the language.
    for (const auto code : { "en", "fr", "de", "it", "es", "pl", "tr", "zh-CN", "ja", "ko" }) {
        Require(ChronicleRuntime::LanguageCode(ChronicleRuntime::ResolveLanguage(code, "")) == code);
    }
    // DovahMenu's entry opens settings, so it shows Shift + the settings key (DMN-25).
    Require(ChronicleRuntime::LauncherHotkey("0") == "Shift+0");
    Require(ChronicleRuntime::LauncherHotkey("") == "Shift+0");
    Require(ChronicleRuntime::LauncherHotkey("k") == "Shift+K");
    Require(ChronicleRuntime::LauncherHotkey("F5") == "Shift+F5");
    // Keys while a menu or another mod's window has the keyboard (CHRON-21).
    {
        using namespace ChronicleRuntime;
        using enum KeyAction;
        const std::uint32_t keys[] = { 0x0D, 0x77, '0', 'K' };  // Enter (input), F8 (interact), settings, a show/hide key
        KeyState free;
        free.bootstrapped = free.loaded = free.hostAvailable = true;
        free.hidden = false;
        free.keysFree = true;
        free.toggleKey = 'K';
        Require(DecideKey(0x0D, false, free) == OpenInput);
        Require(DecideKey(0x77, false, free) == OpenInteract);
        Require(DecideKey('0', true, free) == OpenSettings);
        Require(DecideKey('K', false, free) == Toggle);
        Require(DecideKey('W', false, free) == Pass);
        // Each menu the report names, and a few more: every bound key passes, with or without Shift.
        for (const auto* menu : { "Crafting Menu", "BarterMenu", "ContainerMenu", "InventoryMenu", "Dialogue Menu",
                 "MessageBoxMenu", "Console", "Quantity Menu", "Lockpicking Menu", "Book Menu" }) {
            State state;
            state.Menu(menu, true);
            Require(!state.KeysFree());
            KeyState s = free;
            s.keysFree = state.KeysFree();
            s.dialogueOnly = state.DialogueOnly();
            const bool dialogue = std::string_view(menu) == "Dialogue Menu";
            Require(s.dialogueOnly == dialogue);
            for (const auto key : keys) {
                // In a conversation alone every key decides as with nothing open (as before 1.4.19).
                for (const bool shift : { false, true }) {
                    Require(DecideKey(key, shift, s) == (dialogue ? DecideKey(key, shift, free) : Pass));
                }
            }
            state.Menu(menu, false);
            Require(state.KeysFree() && !state.DialogueOnly());
        }
        // Dialogue leaves Chronicle shown, and Chronicle's keys work there as before 1.4.19:
        // the input key opens the chat bar to speak to that NPC through SkyrimNet.
        {
            State state;
            state.loaded = state.ready = true;
            state.Menu("Dialogue Menu", true);
            Require(state.Visible() && !state.KeysFree() && state.DialogueOnly());
            KeyState s = free;
            s.keysFree = state.KeysFree();
            s.dialogueOnly = state.DialogueOnly();
            Require(DecideKey(0x0D, false, s) == OpenInput);   // the input key (Enter by default)
            Require(DecideKey(0x77, false, s) == OpenInteract); // the interact key (F8 by default)
            Require(DecideKey('0', true, s) == OpenSettings);   // Shift + the settings key
            Require(DecideKey('K', false, s) == Toggle);        // the show/hide key
            Require(DecideKey('E', false, s) == Pass && DecideKey(0x26, false, s) == Pass);  // the game's own keys
            // A message box over the conversation takes every key, Enter included.
            state.Menu("MessageBoxMenu", true);
            Require(!state.DialogueOnly());
            s.dialogueOnly = state.DialogueOnly();
            Require(DecideKey(0x0D, false, s) == Pass);
            state.Menu("MessageBoxMenu", false);
            // So does another mod's window, on this host or another.
            Require(state.DialogueOnly());
            s.dialogueOnly = true;
            s.anyFocus = true;
            Require(DecideKey(0x0D, false, s) == Pass);
            state.Menu("PrismaUI_FocusMenu", true);
            Require(!state.DialogueOnly());
            // With the input key rebound (to I, say), Enter is the conversation's again.
            KeyState rebound = free;
            rebound.keysFree = false;
            rebound.dialogueOnly = true;
            rebound.inputKey = 'I';
            Require(DecideKey(0x0D, false, rebound) == Pass && DecideKey('I', false, rebound) == OpenInput);
            // Chronicle hidden by the show/hide key: nothing opens over the conversation.
            rebound.hidden = true;
            Require(DecideKey('I', false, rebound) == Pass);
        }
        // A menu with no input flags (a HUD widget) leaves the keys free; one with them doesn't.
        {
            State state;
            state.Menu("Some Widget Menu", true, 0);
            Require(state.KeysFree());
            state.Menu("Some Modal Menu", true, kInputMenuFlags);
            Require(!state.KeysFree());
        }
        // Another mod's window: on this host (anyFocus without ownFocus), or another host's focus menu.
        {
            KeyState s = free;
            s.anyFocus = true;
            for (const auto key : keys) Require(DecideKey(key, false, s) == Pass);
            State state;
            state.ownFocusMenu = "MagelightFocus";
            state.Menu("PrismaUI_FocusMenu", true);
            Require(!state.KeysFree());
            State own;
            own.ownFocusMenu = "MagelightFocus";
            own.ownsFocus = true;
            own.Menu("MagelightFocus", true);
            Require(own.KeysFree());  // Chronicle's own focus menu
            own.ownsFocus = false;
            Require(!own.KeysFree());  // another Magelight mod's window
        }
        // Chronicle's own window has the keyboard: the game gets nothing, Escape and F8 close it.
        {
            KeyState s = free;
            s.anyFocus = s.ownFocus = true;
            s.keysFree = false;
            Require(DecideKey(0x0D, false, s) == Keep && DecideKey('W', false, s) == Keep);
            Require(DecideKey(0x1B, false, s) == Unfocus && DecideKey(0x77, false, s) == Unfocus);
        }
        // Hidden by the show/hide key: that key and Shift + settings bring it back, nothing else opens.
        {
            KeyState s = free;
            s.hidden = s.userHidden = true;
            Require(DecideKey('K', false, s) == Toggle && DecideKey('0', true, s) == ShowSettings);
            Require(DecideKey(0x0D, false, s) == Pass);
        }
        // A key kept to open the window holds the keyboard only until focus comes,
        // only while no menu has it, and never past the grace period.
        Require(HoldsKeyboard(true, true, false, 99999));
        Require(HoldsKeyboard(true, false, true, 100));
        Require(!HoldsKeyboard(true, false, false, 100));
        Require(!HoldsKeyboard(true, false, true, kCaptureGraceMs));
        Require(!HoldsKeyboard(false, true, true, 0));
    }
    // The controller combo that opens the window (CHRON-17).
    {
        using namespace ChronicleRuntime;
        using Step = PadComboGate::Step;
        namespace P = PadButton;
        Require(ParsePadCombo("lb+y") == PadCombo{ P::kLB, P::kY });
        Require(ParsePadCombo("") == PadCombo{ P::kLB, P::kY });          // unknown or missing: the default
        Require(ParsePadCombo("rb+x") == PadCombo{ P::kRB, P::kX });
        Require(!ParsePadCombo("off").On());
        Require(PadComboName(ParsePadCombo("lb+x")) == "LB+X" && PadComboName({}) == "off");
        PadComboGate gate;
        gate.Set(ParsePadCombo("lb+y"));
        Require(gate.OnButton(P::kY, true, true) == Step::None);         // Y alone is the game's jump
        Require(gate.OnButton(P::kY, false, false) == Step::None);
        Require(gate.OnButton(P::kLB, true, true) == Step::None);        // LB down...
        Require(gate.OnButton(P::kLB, true, false) == Step::None);       // ...and held
        Require(gate.OnButton(P::kA, true, true) == Step::None);         // other buttons pass
        Require(gate.OnButton(P::kY, true, true) == Step::Fire);         // LB held + Y pressed
        gate.Swallow();
        Require(gate.OnButton(P::kY, true, false) == Step::Swallow);     // Y held: still Chronicle's
        Require(gate.OnButton(P::kY, false, false) == Step::Swallow);    // Y released: the last one
        Require(gate.OnButton(P::kY, true, true) == Step::Fire);         // again while LB is held
        Require(gate.OnButton(P::kY, false, false) == Step::None);       // not swallowed: the caller didn't act
        Require(gate.OnButton(P::kLB, false, false) == Step::None);
        Require(gate.OnButton(P::kY, true, true) == Step::None);         // LB let go
        gate.OnButton(P::kLB, true, true);
        gate.Reset();                                                     // a load forgets LB
        Require(gate.OnButton(P::kY, true, true) == Step::None);
        gate.Set(ParsePadCombo("off"));
        gate.OnButton(P::kLB, true, true);
        Require(gate.OnButton(P::kY, true, true) == Step::None);         // off: never
    }
    // The narrator's name and DovahMenu's description come in every language.
    Require(ChronicleRuntime::Phrased(Lang::En, Phrase::Narrator) == "Narrator");
    Require(ChronicleRuntime::Phrased(Lang::De, Phrase::Narrator) == "Erzähler");
    Require(ChronicleRuntime::Phrased(Lang::Ko, Phrase::MenuDescription) == "채팅 창과 메시지 기록");
    for (int lang = 0; lang <= static_cast<int>(Lang::Ko); ++lang) {
        Require(!ChronicleRuntime::Phrased(static_cast<Lang>(lang), Phrase::MenuDescription).empty());
    }
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
        for (int p = 0; p <= static_cast<int>(Phrase::NoCommands); ++p) {
            Require(!ChronicleRuntime::PhraseText(lang, static_cast<Phrase>(p)).empty());
            // Every language has its own text for the command replies.
            if (lang != Lang::En && p >= static_cast<int>(Phrase::UnknownCommand)) {
                Require(ChronicleRuntime::PhraseText(lang, static_cast<Phrase>(p)) != ChronicleRuntime::PhraseText(Lang::En, static_cast<Phrase>(p)));
            }
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

    // The game-thread pump's queue (DovahGameThread.h): one frame runs what was
    // queued when it started, in posting order; work posted meanwhile waits.
    {
        using DovahGameThread::detail::Job;
        struct Probe { DovahGameThread::detail::Queue* queue; std::string* ran; char name; };
        DovahGameThread::detail::Queue queue;
        std::string ran;
        Probe a{ &queue, &ran, 'a' }, b{ &queue, &ran, 'b' }, again{ &queue, &ran, 'c' };
        const auto record = [](void* ctx) { auto* p = static_cast<Probe*>(ctx); p->ran->push_back(p->name); };
        // 'b' posts 'c' while the frame runs: 'c' belongs to the next frame.
        static Probe* s_again = nullptr;
        s_again = &again;
        const auto postsAgain = [](void* ctx) {
            auto* p = static_cast<Probe*>(ctx);
            p->ran->push_back(p->name);
            p->queue->Push(Job{ +[](void* c) { auto* q = static_cast<Probe*>(c); q->ran->push_back(q->name); }, s_again, "again" });
        };
        queue.Push(Job{ record, &a, "a" });
        queue.Push(Job{ postsAgain, &b, "b" });
        Require(queue.Size() == 2);
        Require(queue.Drain().ran == 2);
        Require(ran == "ab");
        Require(queue.Size() == 1);
        Require(queue.Drain().ran == 1);
        Require(ran == "abc");
        Require(queue.Drain().ran == 0);
    }
}
