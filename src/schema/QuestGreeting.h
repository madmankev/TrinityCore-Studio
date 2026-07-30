#pragma once

// Mirror of the `quest_greeting` table (CREATE TABLE at line 2564).
// PK (`ID`,`Type`) where Type 0 = creature, 1 = gameobject (see GreetingType).

#include <cstdint>
#include <string>

namespace qe
{
struct QuestGreeting
{
    uint32_t id = 0;                        // `ID`               int unsigned
    uint8_t  type = 0;                      // `Type`             tinyint unsigned (0=creature,1=GO)
    uint16_t greetEmoteType = 0;            // `GreetEmoteType`   smallint unsigned
    uint32_t greetEmoteDelay = 0;           // `GreetEmoteDelay`  int unsigned
    std::string greeting;                   // `Greeting`         mediumtext
    int32_t  verifiedBuild = 0;             // `VerifiedBuild`    int  DEFAULT NULL
};
} // namespace qe
