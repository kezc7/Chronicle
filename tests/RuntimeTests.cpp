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
    state.Menu("Loading Menu", true);
    Require(!state.Visible());
    state.Menu("Loading Menu", false);
    state.loaded = false;
    Require(!state.Visible());
    Require(!ChronicleRuntime::InterruptsFocus("PrismaUI_FocusMenu"));
    Require(ChronicleRuntime::InterruptsFocus("InventoryMenu"));
    // Combat deaths: player and followers in, unrelated fights out.
    using ChronicleRuntime::Side;
    Require(ChronicleRuntime::ReportDeath(Side::Other, Side::Ally));   // Serana kills a vampire
    Require(ChronicleRuntime::ReportDeath(Side::Ally, Side::Other));   // a vampire kills Serana
    Require(ChronicleRuntime::ReportDeath(Side::Other, Side::Player));
    Require(ChronicleRuntime::ReportDeath(Side::Player, Side::Other));
    Require(!ChronicleRuntime::ReportDeath(Side::Other, Side::Other)); // guards vs. a dragon
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
    Require(ChronicleRuntime::NotificationPrefix(17) == "New objective: ");
    Require(ChronicleRuntime::NotificationPrefix(18) == "Objective completed: ");
    Require(ChronicleRuntime::NotificationPrefix(1).empty());
    Require(ChronicleRuntime::NotificationPrefix(20).empty()); // skill text is already complete

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
