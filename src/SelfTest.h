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

#include <string_view>

// Chronicle's part of the suite self-test. It runs only when started: by
// typing /selftest in the chat box, or from DovahHub's Settings > Diagnostics
// > Run self-test. It changes nothing in the save: it writes a scratch file
// next to the history files and removes it again. Each step is one line in
// Chronicle.log ("SELFTEST chronicle <step> PASS|FAIL|SKIP <detail>") and,
// in DovahHub's run, one ReportSelfTestStep.
namespace ChronicleSelfTestRun
{
    // Game thread. From "/selftest": the summary goes to the chat box too.
    void StartFromChat();
    // Game thread. DovahHub's runner (SelfTestStartFn); runJson as DovahHub sends it.
    void StartFromHub(std::string_view runJson);
    // Game thread. The page's answer to Chronicle::AskPageSelfTest.
    void OnPageReply(std::string_view json);
}
