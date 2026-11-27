// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace coney {

/// The crime types (docs/references/crime-types.md).
namespace crime {
inline constexpr int kAssault = 0;
inline constexpr int kBreakAndEnter = 1;
inline constexpr int kCopAssault = 2;
inline constexpr int kCopKill = 3;
inline constexpr int kCustom = 4;
inline constexpr int kDisorderlyConduct = 5;
inline constexpr int kGangFight = 6;
inline constexpr int kGangCurfew = 7;
inline constexpr int kMugging = 8;
inline constexpr int kPrecinctAttack = 9;
inline constexpr int kTagging = 10;
inline constexpr int kTheft = 11;
inline constexpr int kTrespassing = 12;
inline constexpr int kVandalism = 13;
inline constexpr int kNoCrime = 14;
/// How many types there are.
inline constexpr int kTypes = 15;
} // namespace crime

/// A gang as the crime report sees it: its id and kind (`GangCreate`'s type: 1 the police).
struct CrimeGang {
    int id = -1;
    int kind = 0;
};

/// The store front (a flag of activity `0xe`) nearest a break-in, as its group word (`+0xd8`) has it.
struct CrimeStore {
    /// The gang bits' value for no gang.
    static constexpr int kNoGang = 31;
    bool robbed = false; ///< Bit 16.
    int gang = kNoGang;  ///< Bits 18-22: the gang that robbed it, once a later break-in names it.
};

/// A position in metres, game axes.
using CrimePosition = std::array<float, 3>;

/// What a crime report reaches beyond its own state: the gangs, the police spawners, the stores, the statistics, the
/// script callback and the HUD. Every call has a default that does nothing, so a test or an early host overrides only
/// what it has; the play mode implements it over the game's systems as they arrive.
class CrimeServices {
  public:
    virtual ~CrimeServices() = default;
    CrimeServices() = default;
    CrimeServices(const CrimeServices&) = delete;
    CrimeServices& operator=(const CrimeServices&) = delete;
    CrimeServices(CrimeServices&&) = delete;
    CrimeServices& operator=(CrimeServices&&) = delete;

    /// The gang of the human with `handle`; nothing for no human or a human in no gang.
    [[nodiscard]] virtual std::optional<CrimeGang> gangOf(double /*handle*/) { return std::nullopt; }
    /// The player index (0 or 1) of the human with `handle` (human `+0x1b0`); -1 for no player.
    [[nodiscard]] virtual int playerIndex(double /*handle*/) { return -1; }
    /// The brain kind (ai::BrainType, brain `+4`) of the human with `handle`; -1 for none.
    [[nodiscard]] virtual int brainType(double /*handle*/) { return -1; }
    /// The gang player 1 is in; -1 for none.
    [[nodiscard]] virtual int playerOneGang() { return -1; }
    /// Every police gang turns hostile to gang `gang`, and, when `mutual`, `gang` to them (`0x0016c3a8`).
    virtual void makePoliceHostile(int /*gang*/, bool /*mutual*/) {}
    /// The police are neutral to gang `gang` again (`GangClearWanted`'s work, `0x00169a20`).
    virtual void clearPoliceHostility(int /*gang*/) {}
    /// Calls the Lua crime callback `function` with the offender's gang and the type (`0x0041ae60`).
    virtual void callCrimeCallback(const std::string& /*function*/, int /*gang*/, int /*type*/) {}
    /// Moves the level's `CrimeScene` flag to `at` (the report does so when the position changed).
    virtual void moveCrimeScene(const CrimePosition& /*at*/) {}
    /// Queues `count` responders of spawn kind `kind` (1, or 3 for a precinct attack) on the nearest spawner of any
    /// gang in state 4 or 6, for crime `type` at `at`, after `delaySeconds` (`0x0016df68`).
    virtual void queueResponders(int /*type*/, int /*kind*/, int /*count*/, const CrimePosition& /*at*/,
                                 double /*delaySeconds*/) {}
    /// The store front nearest `at` (`Flag_FindNearestByActivity`) when it lies within 10 m (squared distance under
    /// 100); nothing otherwise.
    [[nodiscard]] virtual std::optional<CrimeStore> nearestStore(const CrimePosition& /*at*/) { return std::nullopt; }
    /// A first break-in at the store front nearest `at`: marked robbed with no gang, and message `0x12` sent to the
    /// `strobe` nearest it within 6 m.
    virtual void robStore(const CrimePosition& /*at*/) {}
    /// A later break-in at the robbed store front nearest `at` that no gang holds yet: `gang` is written to it.
    virtual void claimStore(const CrimePosition& /*at*/, int /*gang*/) {}
    /// The leader of the gang of the human with `offender` (`Gang_GetLeader`); nothing for none.
    [[nodiscard]] virtual std::optional<double> gangLeader(double /*offender*/) { return std::nullopt; }
    /// One crime event `event` for the player human `human` (`Stats_AddCrime(..., event, 1)`, `0x004ed948`).
    virtual void scoreCrime(double /*human*/, int /*event*/) {}
    /// Tells the HUD of player 1's crime state (`HUD_SetWanted`, `0x001b2520`): `message` 7 for a new crime (the HUD
    /// shows lastCrime()'s `CfgCrimeMessage`), `0xb` when the wanted time ran out.
    virtual void notifyHud(int /*message*/) {}
};

/// The game state's crime fields and the crime report (`0x0041b8b0` on the game state `0x0051489c`): reporting on or
/// off (`ReportCrime`, `+0x288`), the responders per type (`CfgCrimeResponders`, `+0x294 + type`), the Lua callback
/// (`CfgSetCrimeCallback`, `+0x2dc`), the types switched on (`CfgEnableCrimeType`, `+0x32b + type`), player 1's last
/// crime type (`+0x290`), and each gang's **wanted** timer (gang `+0x5e8`, kept here by gang id), which a report sets
/// to 10 s and update() clears when it runs out (`Gang_UpdateWanted`, `0x001698f0`).
///
/// Times are game-timer milliseconds; a timer runs out once the time is past its end.
///
/// Research: docs/research/ai-gangs.md#crimes, docs/research/crimes.md#wanted, docs/references/crime-types.md
class CrimeReports {
  public:
    /// How long a report keeps the offender's gang wanted.
    static constexpr std::uint64_t kWantedMs = 10'000;

    /// `ReportCrime(on)`: whether reports do anything (on at start).
    /// @orig 0x0041b6a8 ReportCrime (unknown)
    void setReporting(bool on) { m_reporting = on; }
    /// Whether reporting is on.
    [[nodiscard]] bool reporting() const { return m_reporting; }
    /// `CfgCrimeResponders(type, count)`.
    void setResponders(int type, int count);
    /// The responders type `type` draws; 0 outside the types.
    [[nodiscard]] int responders(int type) const;
    /// `CfgSetCrimeCallback(fn)`.
    void setCallback(std::string function) { m_callback = std::move(function); }
    /// `CfgEnableCrimeType(type, on)`.
    void setEnabled(int type, bool on);
    /// Whether type `type` is enabled (read by the police brain, for type 12 only).
    [[nodiscard]] bool enabled(int type) const;
    /// `CfgBreakAndEnterDelay`'s delay, seconds (the difficulty's value).
    void setBreakInDelay(double seconds) { m_breakInDelay = seconds; }
    /// `ForceCrimeLevel(on)` (`+0x28c`, which the binding sets to 0 or 1 only): while on, a wanted gang stays wanted
    /// with both its timers.
    void setForced(bool on) { m_forced = on; }
    /// Whether `ForceCrimeLevel` holds the wanted gangs.
    [[nodiscard]] bool forced() const { return m_forced; }

    /// A crime report: `type` at `at` by `offender` (0: none) against `victim` (0: none) at game time `nowMs`.
    /// `sendResponders` is the report's mode 1; `count` the responders a custom crime (type 4) asks for.
    /// @orig 0x0041b8b0 Crime_Report (unknown)
    void report(CrimeServices& services, int type, const CrimePosition& at, double offender, double victim,
                bool sendResponders, int count, std::uint64_t nowMs);

    /// Each frame: clears the wanted state of every gang whose 10 s passed, and the second timers that passed; while
    /// forced, a wanted gang is held at 10 s from now with its second timer.
    /// @orig 0x001698f0 Gang_UpdateWanted (unknown)
    void update(CrimeServices& services, std::uint64_t nowMs);

    /// Whether gang `gang` is wanted.
    [[nodiscard]] bool wanted(int gang) const { return m_wantedUntil.contains(gang); }
    /// The gang's second wanted timer (gang `+0x5f0`): set for 10 s from `nowMs` by a gang's call for help (a scout's
    /// phone call, `GoalCallGang`, `GangRespond` by a gang that is not the police); it runs out as the first does,
    /// without the police's side.
    /// @orig 0x001698c8 Gang_SetSecondWantedTimer (unknown)
    void setSecondWanted(int gang, std::uint64_t nowMs) { m_secondUntil[gang] = nowMs + kSecondWantedMs; }
    /// Whether gang `gang`'s second wanted timer runs (`GangIsWanted(gang, false)`).
    [[nodiscard]] bool secondWanted(int gang) const { return m_secondUntil.contains(gang); }
    /// The second timer's length, ms.
    static constexpr std::uint64_t kSecondWantedMs = 10'000;
    /// The wanted time left of gang `gang` as a fraction of 10 s (what the HUD draws); 0 when not wanted.
    [[nodiscard]] float wantedFraction(int gang, std::uint64_t nowMs) const;
    /// The second timer's time left of gang `gang` as a fraction of 10 s (the radar frame's orange arcs); 0 when it
    /// does not run.
    [[nodiscard]] float secondWantedFraction(int gang, std::uint64_t nowMs) const;
    /// Player 1's last crime type (`+0x290`); crime::kNoCrime at start and once the wanted time ran out.
    [[nodiscard]] int lastCrime() const { return m_lastCrime; }
    /// The game time of a gang's last assault-like report (gang `+0x5f4`); nothing when none.
    [[nodiscard]] std::optional<std::uint64_t> lastAssault(int gang) const;

    /// How many reports have been taken (past the reporting and police checks), and where the last was: what the
    /// hub's shopkeepers watch for a crime in their store (ai/hub_goals.h).
    [[nodiscard]] std::uint64_t reports() const { return m_reports; }
    [[nodiscard]] const std::optional<CrimePosition>& lastPosition() const { return m_lastPosition; }

    /// A new level: no wanted gangs, no scene, no victims scored, player 1's last crime cleared.
    void clearLevel();

  private:
    // Type 1's store test (its `case 1`): police only at a store front within 10 m not yet robbed; a robbed one that
    // no gang holds goes to the offender's gang and scores its leader.
    // @orig 0x0041b8b0 GameState_ReportCrime (unknown)
    void breakIn(CrimeServices& services, const CrimePosition& at, double offender,
                 const std::optional<CrimeGang>& gang, bool sendResponders);
    // A player offender's score against `victim` (types 0, 2 and 8): for a cop, civilian or dealer, once per victim
    // and player.
    // @orig 0x0041b8b0 GameState_ReportCrime (unknown)
    void scoreVictim(CrimeServices& services, double offender, double victim);

    bool m_reporting = true;
    bool m_forced = false;
    std::array<int, crime::kTypes> m_responders{};
    std::array<bool, crime::kTypes> m_enabled = allTrue();
    std::string m_callback;
    double m_breakInDelay = 0.0;
    int m_lastCrime = crime::kNoCrime;
    std::optional<CrimePosition> m_scene;
    std::uint64_t m_reports = 0;
    std::optional<CrimePosition> m_lastPosition;
    std::map<int, std::uint64_t> m_wantedUntil;
    std::map<int, std::uint64_t> m_secondUntil; // gang -> when its second timer runs out
    std::map<int, std::uint64_t> m_lastAssault;
    std::set<std::pair<double, int>> m_scoredVictims; // (victim, player): the victim brain's `+0x290` bits

    // Every type enabled.
    static constexpr std::array<bool, crime::kTypes> allTrue() {
        std::array<bool, crime::kTypes> on{};
        on.fill(true);
        return on;
    }
};

} // namespace coney
