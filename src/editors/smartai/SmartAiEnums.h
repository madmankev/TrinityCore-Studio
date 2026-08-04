#pragma once
// SmartAI enum metadata (value -> label + param tooltip) for smart_scripts, transcribed from
// TrinityCore 3.3.5a SmartScriptMgr.h. Same EnumEntry pattern as util/Enums.h.
#include <vector>
#include "util/Enums.h"   // we::EnumEntry
namespace we {
const std::vector<EnumEntry>& SmartEventValues();        // smart_scripts.event_type (SMART_EVENT_*)
const std::vector<EnumEntry>& SmartActionValues();       // action_type (SMART_ACTION_*)
const std::vector<EnumEntry>& SmartTargetValues();       // target_type (SMART_TARGET_*)
const std::vector<EnumEntry>& SmartScriptSourceTypeValues(); // source_type (SMART_SCRIPT_TYPE_*)

const std::vector<FlagEntry>& SmartEventFlagBits();   // event_flags (SMART_EVENT_FLAG_*)
const std::vector<FlagEntry>& SmartPhaseMaskBits();   // event_phase_mask (phases 1..12)
}
