#pragma once

// clang-format off

// This file is generated from the Game's Reflection data

#include <cstdint>
#include <RED4ext/Common.hpp>
#include <RED4ext/Scripting/Natives/Generated/red/Event.hpp>

namespace RED4ext
{
namespace ent { 
struct AppearanceChangeFinishEvent : red::Event
{
    static constexpr const char* NAME = "entAppearanceChangeFinishEvent";
    static constexpr const char* ALIAS = NAME;

};
RED4EXT_ASSERT_SIZE(AppearanceChangeFinishEvent, 0x40);
} // namespace ent
} // namespace RED4ext

// clang-format on