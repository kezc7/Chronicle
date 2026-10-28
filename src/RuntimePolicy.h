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

    // settings "auto"/unknown follows Skyrim's sLanguage; unsupported -> English.
    inline Lang ResolveLanguage(std::string_view setting, std::string_view gameLanguage)
    {
        struct Entry { std::string_view code, game; Lang lang; };
        static constexpr Entry entries[] = {
            { "en", "ENGLISH", Lang::En }, { "fr", "FRENCH", Lang::Fr }, { "de", "GERMAN", Lang::De },
            { "it", "ITALIAN", Lang::It }, { "es", "SPANISH", Lang::Es }, { "pl", "POLISH", Lang::Pl },
            { "tr", "TURKISH", Lang::Tr }, { "zh", "CHINESE", Lang::Zh }, { "ja", "JAPANESE", Lang::Ja },
            { "ko", "KOREAN", Lang::Ko },
        };
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
        MoreLines, UnknownCommand, Commands, NoCommands
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
        bool apiHidden = false;   // hidden by another mod (Chronicle_API.h SetVisible)
        std::unordered_set<std::string> menus;

        void Menu(std::string_view name, bool opening, std::uint32_t menuFlags = kInputMenuFlags)
        {
            if (!opening) {
                menus.erase(std::string(name));
                return;
            }
            if (HidesChronicle(name, menuFlags)) menus.emplace(name);
        }

        bool Visible() const
        {
            if (!loaded || !ready || userHidden || apiHidden) return false;
            for (const auto& name : menus) {
                if (IsTransition(name) || (name == "PhotoMenu" ? hidePhoto : hideMenus)) return false;
            }
            return true;
        }
    };
}
