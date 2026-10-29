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

#include <cstdint>
#include <string>

// The web-view host behind Chronicle's page. One backend is compiled in per
// build: PrismaUI (UiHostPrisma.cpp, the default) or Magelight UI
// (UiHostMagelight.cpp, CHRONICLE_UI_HOST=Magelight). Chronicle itself only
// talks to this surface.
//
// Threading: every call is allowed from Skyrim's main thread. Listener and
// DOM-ready callbacks may arrive on a host thread; the callee must copy the
// text and post its work to the main thread before touching game state.
class IUiHost
{
public:
    using Listener = void (*)(const char* text);
    using DomReadyFn = void (*)();

    virtual ~IUiHost() = default;

    // Resolves the host DLL and creates Chronicle's (hidden) view. Called once
    // on kDataLoaded. False when the host is not installed.
    virtual bool CreateView(DomReadyFn onDomReady) = 0;
    virtual bool Available() const = 0;   // host API resolved
    virtual bool ViewValid() const = 0;   // view handle received

    // Page -> native: `window.<name>(text)` reaches `fn`.
    virtual void RegisterListener(const char* name, Listener fn) = 0;
    // Native -> page: runs a script in the page.
    virtual void Invoke(const char* script) = 0;
    virtual void Show(bool visible) = 0;

    // Input ownership. Focus() asks for keyboard/mouse; it may be granted
    // asynchronously, so HasFocus() is what the watchdog polls.
    virtual bool HasFocus() = 0;          // Chronicle's view owns input
    virtual bool AnyFocus() = 0;          // any view of this host owns input
    virtual bool Focus() = 0;             // request accepted (or queued)
    virtual void Unfocus() = 0;

    // First-run defaults the page cannot fetch itself (Magelight pages are
    // blocked from reading their own files). Empty when the page fetches
    // config.json on its own.
    virtual std::string FirstRunDefaults() = 0;

    virtual const char* Name() const = 0;       // "PrismaUI" or "Magelight"
    // The engine menu the host pushes while one of its views owns input.
    virtual const char* FocusMenu() const = 0;  // "PrismaUI_FocusMenu" or "MagelightFocus"
    virtual int ApiVersion() const = 0;
    virtual std::uint64_t ViewHandle() const = 0;
};

IUiHost& UiHost();
