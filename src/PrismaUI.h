#pragma once

#include "PCH.h"

// ---------------------------------------------------------------------------
// PrismaUI API stub
//
// PrismaUI registers itself with Chronicle by calling PrismaUI::SetSingleton()
// during its own SKSEPluginLoad (via SKSE messaging). Until that call arrives,
// GetSingleton() returns nullptr and all Chronicle features degrade gracefully.
//
// Targets PrismaUI API Version 2 (RegisterConsoleCallback).
// ---------------------------------------------------------------------------

class PrismaUI
{
public:
    // Called by PrismaUI's plugin during kPostLoad messaging
    static void Set(PrismaUI* a_instance) noexcept { s_instance = a_instance; }

    static PrismaUI* GetSingleton() noexcept { return s_instance; }

    // Retrieve a named view handle. Returns nullptr if the view is not loaded.
    virtual void* GetView(const char* a_viewName) = 0;

    // Execute JavaScript in the given view.
    virtual void Invoke(void* a_view, const char* a_js) = 0;

    // Focus management — must be called when input becomes active/inactive.
    // Mandatory: fixes the mouse-stuck-after-Enter bug (kUsesCursor flag).
    virtual void Focus(void* a_view) = 0;
    virtual void Unfocus(void* a_view) = 0;

    // Register a callback invoked when JS calls window.skse.sendEvent(name, data)
    virtual void RegisterJSListener(void* a_view,
                                    const char* a_eventName,
                                    std::function<void(const char*)> a_callback) = 0;

    // API v2: intercept all console output
    virtual void RegisterConsoleCallback(std::function<void(const char*)> a_callback) = 0;

protected:
    ~PrismaUI() = default;

private:
    inline static PrismaUI* s_instance{ nullptr };
};
