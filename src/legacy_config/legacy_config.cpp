// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/port_utils.h"

namespace wf_ht::legacy {

// The shipped default for each smoothing key, mirroring the Config member
// initialisers. They are named here because a refused value has to land on the
// default of the key it came from, and the two keys do not share one.
static constexpr float kDefaultLocalSmoothing  = 0.0f;
static constexpr float kDefaultRemoteSmoothing = 0.15f;

// A value the mod refused is exactly what a "my INI setting does nothing" bug
// report needs to show, so every substitution is logged rather than swallowed.
// A NaN raw value compares unequal to everything, including itself, so it
// takes this branch too.
static float UseSanitized(const char* name, float raw, float clean) {
    if (raw != clean) {
        Log::Line("[config] %s=%.4f is out of range or not finite; using %.4f",
                  name, raw, clean);
    }
    return clean;
}

// Present but unparseable was the one INI failure with nothing in the log to
// show for it. Every reader in IniReader answers it with the fallback the caller
// passed, and each caller here passes the value the Config already holds, so the
// VALUE was already right - the key kept what it had. What was missing is any
// way to tell that from a key the user never wrote, which is the difference
// between a triageable "my setting is ignored" report and an untriageable one.
// Every other refusal in this file is logged; these were not.
//
// Two probes with opposite fallbacks separate the two cases without duplicating
// the parser: agree, and the reader parsed the text; differ, and it echoed each
// fallback straight back.
//
// The trap this exists for is a bool with a trailing comment. IniReader's own
// header documents it: GetPrivateProfileString does not treat ';' as a comment
// introducer, and ReadBool matches the WHOLE value, so `Enabled=0 ; no lean`
// matches nothing, position tracking stays on, and the user is told why.
static bool ParsedBool(const cameraunlock::IniReader& ini, const char* section,
                       const char* key, bool& out) {
    const bool as_true = ini.ReadBool(section, key, true);
    if (as_true != ini.ReadBool(section, key, false)) return false;
    out = as_true;
    return true;
}

// Inf still counts as parsed and goes on to the finite checks in
// config_sanitize.h, which is where a value out of the float range is answered.
// Only text no parser can read reaches the refusal below.
static bool ParsedFloat(const cameraunlock::IniReader& ini, const char* section,
                        const char* key, float& out) {
    const float as_zero = ini.ReadFloat(section, key, 0.0f);
    if (as_zero != ini.ReadFloat(section, key, 1.0f)) return false;
    out = as_zero;
    return true;
}

static bool ReadFlag(const cameraunlock::IniReader& ini, const char* section,
                     const char* key, bool current) {
    if (ini.ReadString(section, key, "").empty()) return current;

    bool parsed = false;
    if (ParsedBool(ini, section, key, parsed)) return parsed;

    Log::Line("[config] %s=%s is not 0 or 1 (or true/false, yes/no, on/off); keeping %d. "
              "A trailing ; comment is part of the value here - put comments on their own "
              "line above the key.",
              key, ini.ReadString(section, key, "").c_str(), current ? 1 : 0);
    return current;
}

// Shared by every float key: refuse text that will not parse, and hand what does
// parse to the caller's own boundary check. `current` is what the key keeps when
// the text is unreadable; where an out-of-range or non-finite number lands is
// `sanitize`'s business, because the two smoothing keys do not share a default.
template <typename Sanitize>
static float ReadFloatValue(const cameraunlock::IniReader& ini, const char* section,
                            const char* key, float current, Sanitize sanitize) {
    if (ini.ReadString(section, key, "").empty()) return current;

    float raw = 0.0f;
    if (!ParsedFloat(ini, section, key, raw)) {
        Log::Line("[config] %s=%s is not a number; keeping %.4f",
                  key, ini.ReadString(section, key, "").c_str(), current);
        return current;
    }
    return UseSanitized(key, raw, sanitize(raw));
}

static float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                             const char* key, float current) {
    return ReadFloatValue(ini, section, key, current,
                          [](float raw) { return SanitizeSensitivity(raw); });
}

// `shipped_default` is the default of the key being read, not one shared by
// both smoothing keys: LocalSmoothing falls back to 0.0, RemoteSmoothing to
// 0.15. A single fallback would answer a malformed RemoteSmoothing with the
// LOCAL default, so a phone on WiFi would get no smoothing at all on raw
// network jitter, which is the one case RemoteSmoothing exists to cover.
static float ReadSmoothing(const cameraunlock::IniReader& ini, const char* section,
                           const char* key, float current, float shipped_default) {
    return ReadFloatValue(ini, section, key, current, [shipped_default](float raw) {
        return SanitizeSmoothing(raw, shipped_default);
    });
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
static void WarnRetiredSmoothingKey(const cameraunlock::IniReader& ini,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (ini.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "[config] key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// Virtual key codes are published as hex and that is how the shipped INI writes
// them, so that is how they are read: a bare 24 is 0x24, not 36. IniReader's
// ReadHex cannot tell an absent key from an unreadable one, and both matter
// here - the first is the common case, the second is a user who typed a key
// name and needs to be told it is codes only.
static bool ParseVirtualKey(const std::string& text, int& out) {
    const char* start = text.c_str();
    if (text.size() > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        start += 2;
    }
    char* end = nullptr;
    const long value = std::strtol(start, &end, 16);
    if (end == start) return false;

    // The whole value has to be the code, not just the front of it. Half the
    // key names a user would try are made of hex digits - "End" reads as 0xE
    // and "Delete" as 0xDE, both perfectly bindable keys and neither the one
    // that was asked for. Only trailing space and a comment are allowed past
    // the number.
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' && *end != ';' && *end != '#') return false;

    out = static_cast<int>(value);
    return true;
}

// A key the mod refused leaves that action on its previous binding rather than
// on nothing, so a mistyped code costs the user one hotkey and says so.
static int ReadKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const std::string text = ini.ReadString("Hotkeys", key, "");
    if (text.empty()) return fallback;

    int raw = 0;
    if (!ParseVirtualKey(text, raw)) {
        Log::Line("[config] %s=%s is not a virtual key code (0x24, or 24 read as hex); "
                  "keeping 0x%X", key, text.c_str(), fallback);
        return fallback;
    }
    if (!IsBindableVirtualKey(raw)) {
        Log::Line("[config] %s=%s is not a key that can be bound; keeping 0x%X",
                  key, text.c_str(), fallback);
        return fallback;
    }
    return raw;
}

static float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float current) {
    return ReadFloatValue(ini, "Position", key, current, [current](float raw) {
        return SanitizePositionLimit(raw, current);
    });
}

bool LoadConfig(const std::string& path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return false;
    }

    bool portValid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "UdpPort", out.udp_port), out.udp_port, portValid);
    if (!portValid) {
        Log::Line("[config] UdpPort is outside 1024-65535; using %u",
                  static_cast<unsigned>(out.udp_port));
    }

    out.enable_on_startup  = ReadFlag(ini, "General",  "EnableOnStartup",  out.enable_on_startup);

    out.toggle_key            = ReadKey(ini, "ToggleKey",         out.toggle_key);
    out.cycle_mode_key        = ReadKey(ini, "CycleModeKey",      out.cycle_mode_key);
    out.chord_toggle_key      = ReadKey(ini, "ChordToggleKey",    out.chord_toggle_key);
    out.chord_cycle_mode_key  = ReadKey(ini, "ChordCycleModeKey", out.chord_cycle_mode_key);

    out.yaw_sensitivity    = ReadSensitivity(ini, "Rotation", "YawSensitivity",   out.yaw_sensitivity);
    out.pitch_sensitivity  = ReadSensitivity(ini, "Rotation", "PitchSensitivity", out.pitch_sensitivity);
    out.roll_sensitivity   = ReadSensitivity(ini, "Rotation", "RollSensitivity",  out.roll_sensitivity);
    out.invert_yaw         = ReadFlag(ini, "Rotation", "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = ReadFlag(ini, "Rotation", "InvertPitch",      out.invert_pitch);
    out.invert_roll        = ReadFlag(ini, "Rotation", "InvertRoll",       out.invert_roll);
    out.local_smoothing    = ReadSmoothing(ini, "Rotation", "LocalSmoothing",  out.local_smoothing,
                                           kDefaultLocalSmoothing);
    out.remote_smoothing   = ReadSmoothing(ini, "Rotation", "RemoteSmoothing", out.remote_smoothing,
                                           kDefaultRemoteSmoothing);
    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    out.position_enabled   = ReadFlag(ini, "Position", "Enabled",          out.position_enabled);
    out.position_sensitivity_x = ReadSensitivity(ini, "Position", "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ReadSensitivity(ini, "Position", "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ReadSensitivity(ini, "Position", "SensitivityZ", out.position_sensitivity_z);
    out.invert_position_x  = ReadFlag(ini, "Position", "InvertX",          out.invert_position_x);
    out.invert_position_y  = ReadFlag(ini, "Position", "InvertY",          out.invert_position_y);
    out.invert_position_z  = ReadFlag(ini, "Position", "InvertZ",          out.invert_position_z);
    out.limit_x            = ReadLimit(ini, "LimitX",     out.limit_x);
    out.limit_y            = ReadLimit(ini, "LimitY",     out.limit_y);
    out.limit_z            = ReadLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back       = ReadLimit(ini, "LimitZBack", out.limit_z_back);
    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "CycleModeKey"},
        {"Hotkeys", "ChordToggleKey"},
        {"Hotkeys", "ChordCycleModeKey"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
    };
}

}  // namespace wf_ht::legacy
