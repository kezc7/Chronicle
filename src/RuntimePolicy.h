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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ChronicleRuntime
{
    // Dialogue radius is measured in Skyrim world units. Zero disables the
    // filter so older settings keep the existing unlimited behaviour.
    inline constexpr float kMaxDialogueRadius = 5000.0F;

    inline float NormalizeDialogueRadius(float radius) noexcept
    {
        if (!std::isfinite(radius)) return 0.0F;
        return std::clamp(radius, 0.0F, kMaxDialogueRadius);
    }

    inline bool WithinDialogueRadius(float distance, float radius) noexcept
    {
        return radius <= 0.0F || distance <= radius;
    }

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

    // The player's dialogue choice is logged when the NPC starts answering it:
    // the answer is the chosen option's own info, or another info of its topic.
    // A greeting or follow-up line belongs to neither, so nothing is logged.
    inline bool AnswersChoice(const void* answerInfo, const void* answerTopic,
        const void* choiceInfo, const void* choiceTopic)
    {
        if (!answerInfo) return false;
        return answerInfo == choiceInfo || (choiceTopic && answerTopic == choiceTopic);
    }

    // A dialogue option with no prompt text shows its topic's editor ID
    // ("DialogueGenericHello"). That is one token of letters, digits and
    // underscores with an underscore or a capital after lower case. Real
    // choices ("Goodbye.", "Yes", "HELLO", "What happened?") are kept.
    inline bool LooksLikeEditorId(std::string_view text)
    {
        if (text.size() < 4) return false;
        bool underscore = false, lower = false, innerCapital = false;
        for (const char ch : text) {
            const auto c = static_cast<unsigned char>(ch);
            if (c == '_') underscore = true;
            else if (c >= 'a' && c <= 'z') lower = true;
            else if (c >= 'A' && c <= 'Z') innerCapital = innerCapital || lower;
            else if (c < '0' || c > '9') return false;
        }
        return underscore || innerCapital;
    }

    // How a typed chat line reaches SkyrimNet ("skyrimnet_send" in
    // Chronicle.json): as the player's own words (SkyrimNetApi.RegisterDialogue,
    // to the NPC in conversation when there is one), rewritten in character by
    // SkyrimNet's Transform Dialogue (TransformDialogue), or not at all.
    enum class SkyrimNetSend { Dialogue, Transform, Off };

    inline SkyrimNetSend ParseSkyrimNetSend(std::string_view value)
    {
        if (value == "transform") return SkyrimNetSend::Transform;
        if (value == "off") return SkyrimNetSend::Off;
        return SkyrimNetSend::Dialogue;
    }

    inline const char* SkyrimNetSendName(SkyrimNetSend mode)
    {
        return mode == SkyrimNetSend::Transform ? "transform" : mode == SkyrimNetSend::Off ? "off" : "dialogue";
    }

    // The name a chat line opens with, for picking who it is said to:
    // "sally, what are your thoughts" -> "sally", "Hey Lydia: wait" -> "Lydia".
    // Up to four words before a comma or colon in the first 40 characters; a
    // leading greeting is dropped. "" when the line names no one.
    inline std::string AddressedName(std::string_view text)
    {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        const auto stop = text.substr(0, 41).find_first_of(",:");
        if (stop == std::string_view::npos) return {};
        std::string_view name = text.substr(0, stop);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        for (const std::string_view greeting : { "hey ", "hi ", "hello ", "oi ", "excuse me " }) {
            if (name.size() > greeting.size()) {
                bool match = true;
                for (std::size_t i = 0; i < greeting.size() && match; ++i) {
                    const auto c = static_cast<unsigned char>(name[i]);
                    match = (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : name[i]) == greeting[i];
                }
                if (match) { name.remove_prefix(greeting.size()); break; }
            }
        }
        while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
        if (name.size() < 2 || name.size() > 32) return {};
        int words = 1;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c == ' ') {
                if (name[i - 1] == ' ') return {};
                ++words;
            } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80 || c == '\'' || c == '-' || c == '.')) {
                return {};
            }
        }
        return words <= 4 ? std::string(name) : std::string{};
    }

    // The SkyrimNetApi function for a line; nullptr when nothing is sent.
    inline const char* SkyrimNetFunction(SkyrimNetSend mode, bool hasListener)
    {
        switch (mode) {
        case SkyrimNetSend::Transform: return "TransformDialogue";
        case SkyrimNetSend::Off: return nullptr;
        default: return hasListener ? "RegisterDialogueToListener" : "RegisterDialogue";
        }
    }

    // SkyrimNet narration: the whole line in *asterisks*, third person
    // ("*Cerberus rolls his shoulder.*"). A line that only starts or ends with
    // an action ("*shrugs* Fine.") is still dialogue.
    inline bool IsNarration(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
        return text.size() >= 3 && text.front() == '*' && text.back() == '*' &&
            std::count(text.begin(), text.end(), '*') == 2;
    }

    // One line's text for matching its copies: outer asterisks and spaces
    // dropped, inner runs of spaces collapsed, ASCII letters in lower case.
    inline std::string EchoKey(std::string_view text)
    {
        std::string key;
        key.reserve(text.size());
        bool space = false;
        for (const char ch : text) {
            const auto c = static_cast<unsigned char>(ch);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { space = !key.empty(); continue; }
            if (space) key.push_back(' ');
            space = false;
            key.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : ch);
        }
        while (!key.empty() && key.front() == '*') key.erase(key.begin());
        while (!key.empty() && key.back() == '*') key.pop_back();
        return key;
    }

    // SkyrimNet's spoken directions for its voice engines: [square bracket
    // tags] anywhere in a line, such as "[say with a hint of amusement]",
    // "[growl]", "[sfx-sigh]Haah," or "[reset]" (its carry_tags_*.yaml and
    // 0650_audio_tags.prompt). They steer the voice and are not words, so the
    // shown line drops them. A "[" without a "]" within 80 characters is text.
    inline std::string StripAudioTags(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '[') {
                const auto close = text.find(']', i + 1);
                const auto inner = close == std::string_view::npos ? std::string_view{} : text.substr(i + 1, close - i - 1);
                if (close != std::string_view::npos && !inner.empty() && inner.size() <= 80 &&
                    inner.find_first_of("[\r\n") == std::string_view::npos) {
                    i = close + 1;
                    if (!out.empty() && out.back() != ' ') out.push_back(' ');
                    continue;
                }
            }
            out.push_back(text[i]);
            ++i;
        }
        // Spaces the tags leave behind: runs collapsed, none before punctuation or at the ends.
        std::string tidy;
        tidy.reserve(out.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            const char c = out[i];
            if (c == ' ') {
                if (tidy.empty() || tidy.back() == ' ') continue;
                const char next = i + 1 < out.size() ? out[i + 1] : '\0';
                if (next == '\0' || next == ',' || next == '.' || next == '!' || next == '?' || next == ';' || next == ':') continue;
            }
            tidy.push_back(c);
        }
        while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
        return tidy;
    }

    // A SkyrimNet reply mixes speech and one short narrated action in
    // *asterisks*: "Herman is right. *Coralyn glances away.* Come on." gives
    // speech, narration, speech, in order. A lone "*" is text. One segment
    // when there is nothing to split.
    struct Segment
    {
        bool narration{ false };
        std::string text;
    };

    inline std::vector<Segment> SplitNarration(std::string_view text)
    {
        std::vector<Segment> out;
        const auto push = [&out](bool narration, std::string_view part) {
            while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
            while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
            const bool words = std::any_of(part.begin(), part.end(), [](char ch) {
                const auto c = static_cast<unsigned char>(ch);
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80;
            });
            if (words) out.push_back({ narration, std::string(part) });
        };
        std::size_t from = 0;
        while (from < text.size()) {
            const auto open = text.find('*', from);
            const auto close = open == std::string_view::npos ? std::string_view::npos : text.find('*', open + 1);
            if (close == std::string_view::npos) break;
            push(false, text.substr(from, open - from));
            push(true, text.substr(open, close - open + 1));
            from = close + 1;
        }
        push(false, text.substr(std::min(from, text.size())));
        if (out.empty()) out.push_back({ false, std::string(text) });
        return out;
    }

    // SkyrimNet lines arrive more than once: its speech events (per sentence
    // and whole), its dialogue event and the game's subtitle, which can name
    // the player as the speaker of a thought or narration SkyrimNet voices.
    // A line is an echo when, within the window, the same text was already
    // shown, or a longer line already shown contains it. Two NPC dialogue
    // lines are only compared when the same NPC said them, so different NPCs
    // with the same vanilla line are both kept. The first copy is kept.
    class EchoFilter
    {
    public:
        static constexpr std::int64_t kWindowMs = 20000;
        static constexpr std::size_t kMinContained = 12;  // shorter lines must match exactly

        bool IsEcho(std::string_view channel, std::string_view speaker, std::string_view text, std::int64_t nowMs)
        {
            while (!m_recent.empty() && nowMs - m_recent.front().at > kWindowMs) m_recent.pop_front();
            const bool checked = channel == "dialogue" || channel == "skyrimnet" || channel == "narrator" || channel == "player";
            if (!checked) return false;
            auto key = EchoKey(text);
            if (key.empty()) return false;
            for (const auto& seen : m_recent) {
                if (channel == "dialogue" && seen.channel == "dialogue" && seen.speaker != speaker) continue;
                if (seen.key == key || (key.size() >= kMinContained && seen.key.find(key) != std::string::npos)) return true;
            }
            if (m_recent.size() >= 64) m_recent.pop_front();
            m_recent.push_back({ std::move(key), std::string(channel), std::string(speaker), nowMs });
            return false;
        }

        void Clear() { m_recent.clear(); }

    private:
        struct Seen
        {
            std::string key;
            std::string channel;
            std::string speaker;
            std::int64_t at;
        };
        std::deque<Seen> m_recent;
    };

    // Scaleform translation keys in captured text ("$Assault has started!",
    // "Found $SKI_Item"). The HUD looks a "$" string up in the Interface
    // translation files only when it draws it, so text captured before that
    // still holds the key. The whole string is looked up first, then each
    // $token on its own; a whole-string key with no translation loses its
    // "$". lookup(key) returns the translation, or nothing when there is none.
    template <class Lookup>
    std::string TranslateKeys(std::string_view text, Lookup&& lookup)
    {
        if (text.find('$') == std::string_view::npos) return std::string(text);
        if (text.front() == '$') {
            if (auto whole = lookup(text); whole && !whole->empty() && *whole != text) return std::string(*whole);
        }
        const auto word = [](char ch) {
            const auto c = static_cast<unsigned char>(ch);
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        };
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '$' && (i == 0 || !word(text[i - 1]))) {
                std::size_t end = i + 1;
                while (end < text.size() && word(text[end])) ++end;
                if (end > i + 1) {
                    const auto key = text.substr(i, end - i);
                    if (auto part = lookup(key); part && !part->empty() && *part != key) {
                        out += *part;
                        i = end;
                        continue;
                    }
                }
            }
            out.push_back(text[i]);
            ++i;
        }
        if (!out.empty() && out.front() == '$') out.erase(out.begin());
        return out;
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

    // Languages for the lines Chronicle writes itself.
    // The UI has its own table (i18n.js); these are the few phrases composed
    // natively. Game text (dialogue, notifications, names) is already localized.
    // Keep in step with LANGUAGES / GAME_LANGUAGES in i18n.js.
    enum class Lang { En, Fr, De, It, Es, Pl, Tr, Zh, Ja, Ko };

    // The suite's language code for a stored Language value (en, fr, de, it,
    // es, pl, tr, zh-CN, ja, ko, or "auto"). Older values read too: "zh"
    // (Chronicle up to 1.4.6), "zh-Hans" and English language names, any
    // case. "" when the value is none of these.
    inline std::string CanonicalLanguage(std::string_view value)
    {
        while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
        std::string lower(value);
        for (auto& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        static constexpr std::pair<std::string_view, std::string_view> known[] = {
            { "auto", "auto" }, { "en", "en" }, { "fr", "fr" }, { "de", "de" }, { "it", "it" }, { "es", "es" },
            { "pl", "pl" }, { "tr", "tr" }, { "zh-cn", "zh-CN" }, { "ja", "ja" }, { "ko", "ko" },
            { "zh", "zh-CN" }, { "zh-hans", "zh-CN" }, { "chinese", "zh-CN" }, { "simplified chinese", "zh-CN" },
            { "english", "en" }, { "french", "fr" }, { "german", "de" }, { "italian", "it" }, { "spanish", "es" },
            { "polish", "pl" }, { "turkish", "tr" }, { "japanese", "ja" }, { "korean", "ko" },
        };
        for (const auto& [from, to] : known) {
            if (lower == from) return std::string(to);
        }
        return {};
    }

    // The key DovahMenu shows on Chronicle's entry: the entry opens settings,
    // which Shift + the settings hotkey also does ("Shift+0", "Shift+F5").
    inline std::string LauncherHotkey(std::string_view configHotkey)
    {
        std::string key(configHotkey.empty() ? std::string_view("0") : configHotkey);
        if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') key[0] = static_cast<char>(key[0] - 'a' + 'A');
        return "Shift+" + key;
    }

    // The controller combo that opens the chat window: hold the first button,
    // press the second. Codes are the game's gamepad button IDs (XInput's).
    namespace PadButton
    {
        inline constexpr std::uint32_t kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
    }

    struct PadCombo
    {
        std::uint32_t modifier{ 0 };
        std::uint32_t button{ 0 };
        bool On() const { return modifier && button; }
        bool operator==(const PadCombo&) const = default;
    };

    // The setting's values: "lb+y" (the default), "lb+x", "rb+y", "rb+x" or
    // "off". DovahMenu has Back+Start / Back+Y and DovahHub LB+A by default.
    inline PadCombo ParsePadCombo(std::string_view value)
    {
        using namespace PadButton;
        if (value == "off") return {};
        if (value == "lb+x") return { kLB, kX };
        if (value == "rb+y") return { kRB, kY };
        if (value == "rb+x") return { kRB, kX };
        return { kLB, kY };
    }

    inline std::string PadComboName(PadCombo combo)
    {
        if (!combo.On()) return "off";
        const auto name = [](std::uint32_t code) -> std::string {
            using namespace PadButton;
            switch (code) {
            case kLB: return "LB";
            case kRB: return "RB";
            case kA: return "A";
            case kB: return "B";
            case kX: return "X";
            case kY: return "Y";
            default: return "?";
            }
        };
        return name(combo.modifier) + "+" + name(combo.button);
    }

    // Follows the pad's button events for the combo. OnButton answers Fire for
    // the press that completes it; the caller decides whether to act and then
    // calls Swallow, so the rest of that press (held, released) stays Chronicle's.
    class PadComboGate
    {
    public:
        enum class Step { None, Fire, Swallow };

        void Set(PadCombo combo)
        {
            m_combo = combo;
            Reset();
        }
        // On a load and whenever input is uncertain: nothing counts as held.
        void Reset()
        {
            m_modifierHeld = false;
            m_swallowing = false;
        }
        void Swallow() { m_swallowing = true; }
        const PadCombo& Combo() const { return m_combo; }

        // pressed: the button is down in this event; down: it went down in this event.
        Step OnButton(std::uint32_t code, bool pressed, bool down)
        {
            if (!m_combo.On()) return Step::None;
            if (code == m_combo.modifier) {
                m_modifierHeld = pressed;
                return Step::None;
            }
            if (code != m_combo.button) return Step::None;
            if (m_swallowing) {
                if (!pressed) m_swallowing = false;
                return Step::Swallow;
            }
            return down && m_modifierHeld ? Step::Fire : Step::None;
        }

    private:
        PadCombo m_combo;
        bool m_modifierHeld{ false };
        bool m_swallowing{ false };
    };

    // The suite's code for a language, as i18n.js names it.
    inline std::string_view LanguageCode(Lang lang)
    {
        static constexpr std::string_view codes[] = { "en", "fr", "de", "it", "es", "pl", "tr", "zh-CN", "ja", "ko" };
        const auto index = static_cast<std::size_t>(lang);
        return index < sizeof(codes) / sizeof(codes[0]) ? codes[index] : "en";
    }

    // settings "auto"/unknown follows Skyrim's sLanguage; unsupported -> English.
    inline Lang ResolveLanguage(std::string_view stored, std::string_view gameLanguage)
    {
        struct Entry { std::string_view code, game; Lang lang; };
        static constexpr Entry entries[] = {
            { "en", "ENGLISH", Lang::En }, { "fr", "FRENCH", Lang::Fr }, { "de", "GERMAN", Lang::De },
            { "it", "ITALIAN", Lang::It }, { "es", "SPANISH", Lang::Es }, { "pl", "POLISH", Lang::Pl },
            { "tr", "TURKISH", Lang::Tr }, { "zh-CN", "CHINESE", Lang::Zh }, { "ja", "JAPANESE", Lang::Ja },
            { "ko", "KOREAN", Lang::Ko },
        };
        const auto setting = CanonicalLanguage(stored);
        for (const auto& e : entries) {
            if (setting == e.code) return e.lang;
        }
        std::string game(gameLanguage);
        for (auto& c : game) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        for (const auto& e : entries) {
            if (game == e.game) return e.lang;
        }
        return Lang::En;
    }

    enum class Phrase
    {
        GameSaved, ReachedLevel, Killed, Hit, Died, Combat, Enemy, Someone, Target, Player,
        QuestStarted, QuestCompleted, QuestFailed, NewObjective, ObjectiveCompleted, ObjectiveFailed,
        MoreLines, UnknownCommand, Commands, NoCommands, Narrator, MenuDescription,
        SelfTestStarted, SelfTestDone, SelfTestBusy
    };

    // "{0}" marks the inserted value (a name, a level, a line count).
    inline std::string_view PhraseText(Lang lang, Phrase phrase)
    {
        using enum Phrase;
        switch (lang) {
        case Lang::Fr:
            switch (phrase) {
            case GameSaved: return "Partie sauvegardée";
            case ReachedLevel: return "Niveau {0} atteint";
            case Killed: return "a tué {0}";
            case Hit: return "a frappé {0}";
            case Died: return "{0} est mort";
            case Combat: return "Combat";
            case Enemy: return "Ennemi";
            case Someone: return "Quelqu'un";
            case Target: return "la cible";
            case Player: return "Joueur";
            case QuestStarted: return "Quête commencée : {0}";
            case QuestCompleted: return "Quête terminée : {0}";
            case QuestFailed: return "Quête échouée : {0}";
            case NewObjective: return "Nouvel objectif : {0}";
            case ObjectiveCompleted: return "Objectif terminé : {0}";
            case ObjectiveFailed: return "Objectif échoué : {0}";
            case MoreLines: return "... {0} lignes de plus - ouvrez la console (~) pour tout voir";
            case UnknownCommand: return "Commande inconnue : {0}";
            case Commands: return "Commandes : {0}";
            case NoCommands: return "Aucune commande de mod enregistrée";
            case Narrator: return "Narrateur";
            case MenuDescription: return "Fenêtre de discussion et journal des messages";
            case SelfTestStarted: return "Auto-test lancé : les résultats arrivent dans quelques secondes";
            case SelfTestDone: return "Auto-test terminé : {0}. Chaque étape est dans Chronicle.log";
            case SelfTestBusy: return "Un auto-test est déjà en cours";
            }
            break;
        case Lang::De:
            switch (phrase) {
            case GameSaved: return "Spiel gespeichert";
            case ReachedLevel: return "Stufe {0} erreicht";
            case Killed: return "hat {0} getötet";
            case Hit: return "hat {0} getroffen";
            case Died: return "{0} ist gestorben";
            case Combat: return "Kampf";
            case Enemy: return "Feind";
            case Someone: return "Jemand";
            case Target: return "das Ziel";
            case Player: return "Spieler";
            case QuestStarted: return "Quest begonnen: {0}";
            case QuestCompleted: return "Quest abgeschlossen: {0}";
            case QuestFailed: return "Quest fehlgeschlagen: {0}";
            case NewObjective: return "Neues Ziel: {0}";
            case ObjectiveCompleted: return "Ziel erreicht: {0}";
            case ObjectiveFailed: return "Ziel fehlgeschlagen: {0}";
            case MoreLines: return "... {0} weitere Zeilen - Konsole (~) öffnen, um alle zu sehen";
            case UnknownCommand: return "Unbekannter Befehl: {0}";
            case Commands: return "Befehle: {0}";
            case NoCommands: return "Keine Mod-Befehle registriert";
            case Narrator: return "Erzähler";
            case MenuDescription: return "Chatfenster und Nachrichtenprotokoll";
            case SelfTestStarted: return "Selbsttest gestartet: Die Ergebnisse folgen in wenigen Sekunden";
            case SelfTestDone: return "Selbsttest beendet: {0}. Jeder Schritt steht in Chronicle.log";
            case SelfTestBusy: return "Ein Selbsttest läuft bereits";
            }
            break;
        case Lang::It:
            switch (phrase) {
            case GameSaved: return "Partita salvata";
            case ReachedLevel: return "Livello {0} raggiunto";
            case Killed: return "ha ucciso {0}";
            case Hit: return "ha colpito {0}";
            case Died: return "{0} è morto";
            case Combat: return "Combattimento";
            case Enemy: return "Nemico";
            case Someone: return "Qualcuno";
            case Target: return "il bersaglio";
            case Player: return "Giocatore";
            case QuestStarted: return "Missione iniziata: {0}";
            case QuestCompleted: return "Missione completata: {0}";
            case QuestFailed: return "Missione fallita: {0}";
            case NewObjective: return "Nuovo obiettivo: {0}";
            case ObjectiveCompleted: return "Obiettivo completato: {0}";
            case ObjectiveFailed: return "Obiettivo fallito: {0}";
            case MoreLines: return "... altre {0} righe - apri la console (~) per vederle tutte";
            case UnknownCommand: return "Comando sconosciuto: {0}";
            case Commands: return "Comandi: {0}";
            case NoCommands: return "Nessun comando di mod registrato";
            case Narrator: return "Narratore";
            case MenuDescription: return "Finestra di chat e registro dei messaggi";
            case SelfTestStarted: return "Autotest avviato: i risultati arrivano tra pochi secondi";
            case SelfTestDone: return "Autotest terminato: {0}. Ogni passaggio è in Chronicle.log";
            case SelfTestBusy: return "Un autotest è già in corso";
            }
            break;
        case Lang::Es:
            switch (phrase) {
            case GameSaved: return "Partida guardada";
            case ReachedLevel: return "Nivel {0} alcanzado";
            case Killed: return "ha matado a {0}";
            case Hit: return "ha golpeado a {0}";
            case Died: return "{0} ha muerto";
            case Combat: return "Combate";
            case Enemy: return "Enemigo";
            case Someone: return "Alguien";
            case Target: return "el objetivo";
            case Player: return "Jugador";
            case QuestStarted: return "Misión iniciada: {0}";
            case QuestCompleted: return "Misión completada: {0}";
            case QuestFailed: return "Misión fallida: {0}";
            case NewObjective: return "Nuevo objetivo: {0}";
            case ObjectiveCompleted: return "Objetivo completado: {0}";
            case ObjectiveFailed: return "Objetivo fallido: {0}";
            case MoreLines: return "... {0} líneas más - abre la consola (~) para verlas todas";
            case UnknownCommand: return "Comando desconocido: {0}";
            case Commands: return "Comandos: {0}";
            case NoCommands: return "No hay comandos de mods registrados";
            case Narrator: return "Narrador";
            case MenuDescription: return "Ventana de chat y registro de mensajes";
            case SelfTestStarted: return "Autoprueba iniciada: los resultados llegan en unos segundos";
            case SelfTestDone: return "Autoprueba terminada: {0}. Cada paso está en Chronicle.log";
            case SelfTestBusy: return "Ya hay una autoprueba en curso";
            }
            break;
        case Lang::Pl:
            // Colon forms avoid Polish case endings on game-supplied names.
            switch (phrase) {
            case GameSaved: return "Gra zapisana";
            case ReachedLevel: return "Osiągnięto poziom {0}";
            case Killed: return "zabija: {0}";
            case Hit: return "trafia: {0}";
            case Died: return "{0} ginie";
            case Combat: return "Walka";
            case Enemy: return "Wróg";
            case Someone: return "Ktoś";
            case Target: return "cel";
            case Player: return "Gracz";
            case QuestStarted: return "Rozpoczęto zadanie: {0}";
            case QuestCompleted: return "Ukończono zadanie: {0}";
            case QuestFailed: return "Zadanie nieudane: {0}";
            case NewObjective: return "Nowy cel: {0}";
            case ObjectiveCompleted: return "Cel osiągnięty: {0}";
            case ObjectiveFailed: return "Cel nieosiągnięty: {0}";
            case MoreLines: return "... jeszcze {0} wierszy - otwórz konsolę (~), aby zobaczyć wszystkie";
            case UnknownCommand: return "Nieznane polecenie: {0}";
            case Commands: return "Polecenia: {0}";
            case NoCommands: return "Brak zarejestrowanych poleceń modów";
            case Narrator: return "Narrator";
            case MenuDescription: return "Okno czatu i dziennik wiadomości";
            case SelfTestStarted: return "Autotest uruchomiony: wyniki pojawią się za kilka sekund";
            case SelfTestDone: return "Autotest zakończony: {0}. Każdy krok jest w Chronicle.log";
            case SelfTestBusy: return "Autotest już trwa";
            }
            break;
        case Lang::Tr:
            // Colon forms avoid Turkish case suffixes on game-supplied names.
            switch (phrase) {
            case GameSaved: return "Oyun kaydedildi";
            case ReachedLevel: return "Seviye {0} oldu";
            case Killed: return "öldürdü: {0}";
            case Hit: return "vurdu: {0}";
            case Died: return "{0} öldü";
            case Combat: return "Savaş";
            case Enemy: return "Düşman";
            case Someone: return "Biri";
            case Target: return "hedef";
            case Player: return "Oyuncu";
            case QuestStarted: return "Görev başladı: {0}";
            case QuestCompleted: return "Görev tamamlandı: {0}";
            case QuestFailed: return "Görev başarısız: {0}";
            case NewObjective: return "Yeni hedef: {0}";
            case ObjectiveCompleted: return "Hedef tamamlandı: {0}";
            case ObjectiveFailed: return "Hedef başarısız: {0}";
            case MoreLines: return "... {0} satır daha - hepsini görmek için konsolu (~) açın";
            case UnknownCommand: return "Bilinmeyen komut: {0}";
            case Commands: return "Komutlar: {0}";
            case NoCommands: return "Kayıtlı mod komutu yok";
            case Narrator: return "Anlatıcı";
            case MenuDescription: return "Sohbet penceresi ve mesaj günlüğü";
            case SelfTestStarted: return "Kendi kendine test başladı: sonuçlar birkaç saniye içinde gelir";
            case SelfTestDone: return "Kendi kendine test bitti: {0}. Her adım Chronicle.log dosyasında";
            case SelfTestBusy: return "Bir kendi kendine test zaten çalışıyor";
            }
            break;
        case Lang::Zh:
            switch (phrase) {
            case GameSaved: return "游戏已保存";
            case ReachedLevel: return "达到 {0} 级";
            case Killed: return "击杀了 {0}";
            case Hit: return "击中了 {0}";
            case Died: return "{0} 死亡";
            case Combat: return "战斗";
            case Enemy: return "敌人";
            case Someone: return "某人";
            case Target: return "目标";
            case Player: return "玩家";
            case QuestStarted: return "任务开始：{0}";
            case QuestCompleted: return "任务完成：{0}";
            case QuestFailed: return "任务失败：{0}";
            case NewObjective: return "新目标：{0}";
            case ObjectiveCompleted: return "目标完成：{0}";
            case ObjectiveFailed: return "目标失败：{0}";
            case MoreLines: return "……还有 {0} 行 - 打开控制台 (~) 查看全部";
            case UnknownCommand: return "未知命令：{0}";
            case Commands: return "命令：{0}";
            case NoCommands: return "没有已注册的模组命令";
            case Narrator: return "旁白";
            case MenuDescription: return "聊天窗口和消息记录";
            case SelfTestStarted: return "自检已开始：结果将在几秒内显示";
            case SelfTestDone: return "自检完成：{0}。每个步骤都记录在 Chronicle.log 中";
            case SelfTestBusy: return "自检已在运行";
            }
            break;
        case Lang::Ja:
            switch (phrase) {
            case GameSaved: return "ゲームをセーブしました";
            case ReachedLevel: return "レベル {0} に到達";
            case Killed: return "{0} を倒した";
            case Hit: return "{0} に命中";
            case Died: return "{0} が死亡";
            case Combat: return "戦闘";
            case Enemy: return "敵";
            case Someone: return "誰か";
            case Target: return "対象";
            case Player: return "プレイヤー";
            case QuestStarted: return "クエスト開始: {0}";
            case QuestCompleted: return "クエスト完了: {0}";
            case QuestFailed: return "クエスト失敗: {0}";
            case NewObjective: return "新しい目標: {0}";
            case ObjectiveCompleted: return "目標達成: {0}";
            case ObjectiveFailed: return "目標失敗: {0}";
            case MoreLines: return "...他 {0} 行 - すべて見るにはコンソール (~) を開いてください";
            case UnknownCommand: return "不明なコマンド：{0}";
            case Commands: return "コマンド：{0}";
            case NoCommands: return "登録された MOD コマンドはありません";
            case Narrator: return "ナレーター";
            case MenuDescription: return "チャットウィンドウとメッセージログ";
            case SelfTestStarted: return "セルフテストを開始しました。数秒で結果が出ます";
            case SelfTestDone: return "セルフテスト完了：{0}。各ステップは Chronicle.log にあります";
            case SelfTestBusy: return "セルフテストはすでに実行中です";
            }
            break;
        case Lang::Ko:
            // Noun forms avoid Korean particles that depend on the name's last sound.
            switch (phrase) {
            case GameSaved: return "게임 저장됨";
            case ReachedLevel: return "레벨 {0} 달성";
            case Killed: return "{0} 처치";
            case Hit: return "{0} 공격";
            case Died: return "{0} 사망";
            case Combat: return "전투";
            case Enemy: return "적";
            case Someone: return "누군가";
            case Target: return "대상";
            case Player: return "플레이어";
            case QuestStarted: return "퀘스트 시작: {0}";
            case QuestCompleted: return "퀘스트 완료: {0}";
            case QuestFailed: return "퀘스트 실패: {0}";
            case NewObjective: return "새 목표: {0}";
            case ObjectiveCompleted: return "목표 완료: {0}";
            case ObjectiveFailed: return "목표 실패: {0}";
            case MoreLines: return "... {0}줄 더 있음 - 모두 보려면 콘솔(~)을 여세요";
            case UnknownCommand: return "알 수 없는 명령: {0}";
            case Commands: return "명령: {0}";
            case NoCommands: return "등록된 모드 명령이 없습니다";
            case Narrator: return "내레이터";
            case MenuDescription: return "채팅 창과 메시지 기록";
            case SelfTestStarted: return "자체 테스트를 시작했습니다. 몇 초 후 결과가 표시됩니다";
            case SelfTestDone: return "자체 테스트 완료: {0}. 각 단계는 Chronicle.log에 있습니다";
            case SelfTestBusy: return "자체 테스트가 이미 실행 중입니다";
            }
            break;
        case Lang::En:
            break;
        }
        switch (phrase) {
        case GameSaved: return "Game saved";
        case ReachedLevel: return "Reached level {0}";
        case Killed: return "killed {0}";
        case Hit: return "hit {0}";
        case Died: return "{0} died";
        case Combat: return "Combat";
        case Enemy: return "Enemy";
        case Someone: return "Someone";
        case Target: return "target";
        case Player: return "Player";
        case QuestStarted: return "Quest started: {0}";
        case QuestCompleted: return "Quest completed: {0}";
        case QuestFailed: return "Quest failed: {0}";
        case NewObjective: return "New objective: {0}";
        case ObjectiveCompleted: return "Objective completed: {0}";
        case ObjectiveFailed: return "Objective failed: {0}";
        case MoreLines: return "... {0} more lines - open the console (~) to see them all";
        case UnknownCommand: return "Unknown command: {0}";
        case Commands: return "Commands: {0}";
        case NoCommands: return "No mod commands are registered";
        case Narrator: return "Narrator";
        case MenuDescription: return "Chat box and message log";
        case SelfTestStarted: return "Self-test started: the results follow in a few seconds";
        case SelfTestDone: return "Self-test finished: {0}. Each step is in Chronicle.log";
        case SelfTestBusy: return "A self-test is already running";
        }
        return "";
    }

    inline std::string Phrased(Lang lang, Phrase phrase, std::string_view value = {})
    {
        std::string text(PhraseText(lang, phrase));
        if (const auto at = text.find("{0}"); at != std::string::npos) text.replace(at, 3, value);
        return text;
    }

    // Quest and objective HUD messages carry only the name, so label them.
    inline std::string NotificationText(Lang lang, std::uint32_t type, std::string_view text)
    {
        switch (type) {
        case 14: return Phrased(lang, Phrase::QuestStarted, text);
        case 15: return Phrased(lang, Phrase::QuestCompleted, text);
        case 16: return Phrased(lang, Phrase::QuestFailed, text);
        case 17: return Phrased(lang, Phrase::NewObjective, text);
        case 18: return Phrased(lang, Phrase::ObjectiveCompleted, text);
        case 19: return Phrased(lang, Phrase::ObjectiveFailed, text);
        default: return std::string(text);
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
    inline std::vector<std::string> ConsoleLines(std::string_view output, std::size_t limit, Lang lang = Lang::En)
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
        if (more) lines.push_back(Phrased(lang, Phrase::MoreLines, std::to_string(more)));
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

    // SKSE Menu Framework draws its windows itself and opens no game menu;
    // Chronicle reports its blocking windows under this name.
    inline constexpr std::string_view kMenuFrameworkMenu = "SKSE Menu Framework";

    // DovahHub's page is a web view on the same UI host as Chronicle and opens
    // no game menu of its own; Chronicle reports it under this name while open.
    inline constexpr std::string_view kDovahHubMenu = "DovahHub";

    inline bool IsBlocking(std::string_view name)
    {
        return IsTransition(name) || name == "SystemMenu" || name == "TweenMenu" ||
            name == "InventoryMenu" || name == "MagicMenu" || name == "MapMenu" ||
            name == "ContainerMenu" || name == "Journal Menu" || name == "LevelUpMenu" ||
            name == "StatsMenu" || name == "SleepWaitMenu" || name == "FavoritesMenu" ||
            name == "BarterMenu" || IsCrafting(name) || name == "PhotoMenu" ||
            // PrismaUI menus such as DFC also hide the Magelight build.
            // State::Visible exempts Chronicle's own focused view.
            name == "PrismaUI_FocusMenu" ||
            // Meridian UI opens this modal menu while one of its web views
            // (e.g. Wayframe) has focus. PrismaUI draws above Meridian, so
            // without it Chronicle stays on top of those menus.
            name == "MeridianUI_FocusMenu" ||
            // Magelight UI opens its own engine menu while one of its views
            // (e.g. SeverActions) holds UI mode; PrismaUI draws above it too.
            name == "MagelightFocus" ||
            // Skyrim's own console (~) is drawn under PrismaUI too.
            name == "Console" ||
            // Other full-screen vanilla menus (RaceSex Menu is character
            // creation, also used by RaceMenu). Dialogue Menu stays visible.
            name == "RaceSex Menu" || name == "Book Menu" || name == "Lockpicking Menu" ||
            name == "Training Menu" || name == "GiftMenu" || name == "Tutorial Menu" ||
            name == "Credits Menu" || name == "Mod Manager Menu" || name == kMenuFrameworkMenu ||
            name == kDovahHubMenu;
    }

    // IMenu flags of a menu that takes the player's input (CommonLib's
    // UI_MENU_FLAGS kPausesGame, kUsesCursor, kUsesMenuContext and kModal).
    inline constexpr std::uint32_t kInputMenuFlags = 1u << 0 | 1u << 2 | 1u << 3 | 1u << 4;

    // SKSE's CustomMenu hosts whatever SWF a mod opens with UI.OpenCustomMenu:
    // a command menu that pauses the game or takes the cursor (Swiftly Order
    // Squad's follower commands) hides Chronicle; a widget opened with no
    // flags is an overlay and does not. Unknown flags count as input.
    inline bool HidesChronicle(std::string_view name, std::uint32_t menuFlags = kInputMenuFlags)
    {
        return IsBlocking(name) || (name == "CustomMenu" && (menuFlags & kInputMenuFlags) != 0);
    }

    // Opening a menu that takes input releases Chronicle's focus. Overlay
    // menus (HUD widgets, minimaps) open and close at any time, including
    // while the host's focus menu opens, and must not close a panel the
    // player just opened. A menu whose flags are unknown counts as input.
    // PrismaUI_FocusMenu and MagelightFocus are the engine menus the two web
    // hosts push while one of their views owns input, Chronicle's included.
    inline bool InterruptsFocus(std::string_view name, std::uint32_t menuFlags = kInputMenuFlags)
    {
        if (name == "PrismaUI_FocusMenu" || name == "MagelightFocus" ||
            name == "HUD Menu" || name == "Cursor Menu") return false;
        return IsBlocking(name) || (menuFlags & kInputMenuFlags) != 0;
    }

    // A menu whose keys are its own while it is open (CHRON-21): every menu
    // that takes input (InterruptsFocus), and the two that leave Chronicle on
    // screen, whose flags may not say so. Overlays (HUD, Cursor, widgets) don't.
    inline bool TakesKeyboard(std::string_view name, std::uint32_t menuFlags = kInputMenuFlags)
    {
        return name == "Dialogue Menu" || name == "MessageBoxMenu" || name == "Console" ||
            name == "Quantity Menu" || InterruptsFocus(name, menuFlags);
    }

    // What the input sink does with a key (CHRON-21). Pass: the game, a menu or
    // another mod gets it. Everything else Chronicle keeps.
    enum class KeyAction
    {
        Pass,
        Keep,           // Chronicle's own window has the keyboard
        Toggle,         // the show/hide key
        ShowSettings,   // Shift + the settings key while hidden by the show/hide key
        Unfocus,        // Escape, or the interact key, while Chronicle's window has the keyboard
        OpenSettings,
        OpenInteract,
        OpenInput,
    };

    struct KeyState
    {
        bool bootstrapped{ false };
        bool loaded{ false };
        bool hidden{ true };
        bool hostAvailable{ false };
        bool anyFocus{ false };   // some web view (any mod's) has the keyboard
        bool ownFocus{ false };   // Chronicle's view has it
        bool keysFree{ false };   // State::KeysFree: no menu or other window has the keyboard
        bool dialogueOnly{ false };  // State::DialogueOnly: only a conversation has it; Chronicle's keys work
        bool userHidden{ false };
        std::uint32_t toggleKey{ 0 };
        std::uint32_t settingsKey{ '0' };
        std::uint32_t interactKey{ 0x77 };  // F8
        std::uint32_t inputKey{ 0x0D };     // Enter
    };

    // vk: the Windows virtual key. Chronicle takes a key only when its own
    // window has the keyboard, or when nothing else does and the key is one of
    // its hotkeys. With a menu or another mod's window open, every key passes,
    // except in a conversation alone, where Chronicle's keys work as they did
    // before 1.4.19 (the input key opens the chat bar to speak to that NPC).
    inline KeyAction DecideKey(std::uint32_t vk, bool shift, const KeyState& s)
    {
        using enum KeyAction;
        // The show/hide key works while Chronicle is hidden by it, but never while
        // any view or menu has the keyboard.
        const bool free = s.keysFree || s.dialogueOnly;
        const bool canToggle = s.bootstrapped && s.loaded && s.hostAvailable && !s.anyFocus && free;
        if (canToggle && s.toggleKey && vk == s.toggleKey && !shift) return Toggle;
        if (canToggle && s.userHidden && shift && vk == s.settingsKey) return ShowSettings;
        if (!s.bootstrapped || s.hidden || !s.loaded || !s.hostAvailable) return Pass;
        if (s.anyFocus) {
            if (!s.ownFocus) return Pass;  // another mod's window
            // Typing in Chronicle: the host routes keys to the page; the game gets none.
            const bool functionKey = vk >= 0x70 && vk <= 0x7B;  // F1..F12
            if (vk == 0x1B || (functionKey && vk == s.interactKey)) return Unfocus;
            return Keep;
        }
        if (!free) return Pass;
        if (shift && vk == s.settingsKey) return OpenSettings;
        if (vk == s.interactKey) return OpenInteract;
        if (vk == s.inputKey) return OpenInput;
        return Pass;
    }

    // A key Chronicle kept to open its window holds the keyboard until the window
    // has it. If focus hasn't come within this long, the keyboard goes back.
    inline constexpr std::int64_t kCaptureGraceMs = 1500;

    // keysFree: nothing else has the keyboard, or only a conversation does (the
    // input key opened the chat bar over it).
    inline bool HoldsKeyboard(bool capture, bool ownFocus, bool keysFree, std::int64_t msSinceCapture)
    {
        if (!capture) return false;
        if (ownFocus) return true;
        return keysFree && msSinceCapture >= 0 && msSinceCapture < kCaptureGraceMs;
    }

    // PrismaUI's Focus() can succeed before HasFocus() reports the view as
    // focused. Focus counts as lost only once it was seen, or when it never
    // arrives within the grace period; earlier ticks would close a panel
    // that is still opening.
    inline constexpr int kFocusGraceMs = 1000;

    inline bool FocusLost(bool hasFocus, bool seen, int msSinceGranted)
    {
        return !hasFocus && (seen || msSinceGranted >= kFocusGraceMs);
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
        bool userHidden = false;  // hidden with the show/hide hotkey
        // The host's focus menu hides Chronicle only while another mod's view
        // owns input: on the Magelight build Chronicle's own focus opens it too.
        std::string ownFocusMenu;
        bool ownsFocus = false;
        bool apiHidden = false;   // hidden by another mod (Chronicle_API.h SetVisible)
        std::unordered_set<std::string> menus;
        // Open menus that take the keyboard (CHRON-21), whether or not they hide
        // Chronicle: Dialogue Menu and message boxes leave it shown, but their
        // Enter is theirs.
        std::unordered_set<std::string> keyboardMenus;

        void Menu(std::string_view name, bool opening, std::uint32_t menuFlags = kInputMenuFlags)
        {
            if (!opening) {
                menus.erase(std::string(name));
                keyboardMenus.erase(std::string(name));
                return;
            }
            if (HidesChronicle(name, menuFlags)) menus.emplace(name);
            if (TakesKeyboard(name, menuFlags)) keyboardMenus.emplace(name);
        }

        // A conversation is the only thing holding the keyboard: Chronicle's keys
        // work as they did before 1.4.19, so its chat bar can open to speak to that
        // NPC (CHRON-21).
        bool DialogueOnly() const
        {
            bool dialogue = false;
            for (const auto* set : { &menus, &keyboardMenus }) {
                for (const auto& name : *set) {
                    if (ownsFocus && name == ownFocusMenu) continue;
                    if (name != "Dialogue Menu") return false;
                    dialogue = true;
                }
            }
            return dialogue;
        }

        // No game menu or other mod's window has the keyboard: Chronicle's keys
        // may open it. Chronicle's own focus menu doesn't count while it holds focus.
        bool KeysFree() const
        {
            for (const auto* set : { &menus, &keyboardMenus }) {
                for (const auto& name : *set) {
                    if (ownsFocus && name == ownFocusMenu) continue;
                    return false;
                }
            }
            return true;
        }

        bool Visible() const
        {
            if (!loaded || !ready || userHidden || apiHidden) return false;
            for (const auto& name : menus) {
                if (ownsFocus && name == ownFocusMenu) continue;
                if (IsTransition(name) || (name == "PhotoMenu" ? hidePhoto : hideMenus)) return false;
            }
            return true;
        }
    };
}
