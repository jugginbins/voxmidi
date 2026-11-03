// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Parameter IDs shared by the processor and the editor.
#pragma once

namespace ids
{
// global
inline constexpr const char* mode       = "mode";
inline constexpr const char* inputsrc   = "inputsrc";
inline constexpr const char* inputgain  = "inputgain";
inline constexpr const char* monitor    = "monitor";
inline constexpr const char* range      = "range";

// pitch
inline constexpr const char* root       = "root";
inline constexpr const char* scale      = "scale";
inline constexpr const char* gate       = "gate";
inline constexpr const char* clarity    = "clarity";
inline constexpr const char* stick      = "stick";
inline constexpr const char* retrig     = "retrig";
inline constexpr const char* dip        = "dip";
inline constexpr const char* octave     = "octave";
inline constexpr const char* transpose  = "transpose";
inline constexpr const char* dynvel     = "dynvel";
inline constexpr const char* fixedvel   = "fixedvel";
inline constexpr const char* velfloor   = "velfloor";
inline constexpr const char* velceil    = "velceil";
inline constexpr const char* bendmode   = "bendmode";
inline constexpr const char* bendrange  = "bendrange";
inline constexpr const char* melch      = "melch";
inline constexpr const char* chord      = "chord";
inline constexpr const char* chordch    = "chordch";

// triggers
inline constexpr const char* trigsens    = "trigsens";
inline constexpr const char* trigfloor   = "trigfloor";
inline constexpr const char* trigdelay   = "trigdelay";
inline constexpr const char* trigstrict  = "trigstrict";
inline constexpr const char* triglock    = "triglock";
inline constexpr const char* trignotelen = "trignotelen";
inline constexpr const char* trigch      = "trigch";
inline constexpr const char* trigignore  = "trigignore";
inline constexpr const char* triginhibit = "triginhibit";
inline constexpr const char* trigdyn     = "trigdyn";
inline constexpr const char* trigvel     = "trigvel";

// controllers
inline constexpr const char* ccch        = "ccch";
inline constexpr const char* vowelsmooth = "vowelsmooth";
} // namespace ids

// Indexed parameter IDs: "cust0".."cust11", "pad0".."pad7", "cc0on".."cc3on", "cc0num".."cc3num"
inline juce::String indexedId (const char* prefix, int i, const char* suffix = "")
{
    return juce::String (prefix) + juce::String (i) + juce::String (suffix);
}
