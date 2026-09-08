#include "RuntimePolicy.h"
#include <stdexcept>

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
    state.Menu("InventoryMenu", true); // duplicate notification is idempotent
    state.Menu("InventoryMenu", false);
    Require(state.Visible());
    state.Menu("Loading Menu", true);
    Require(!state.Visible());
    state.Menu("Loading Menu", false);
    state.loaded = false;
    Require(!state.Visible());
    Require(!ChronicleRuntime::InterruptsFocus("PrismaUI_FocusMenu"));
    Require(ChronicleRuntime::InterruptsFocus("InventoryMenu"));
}
