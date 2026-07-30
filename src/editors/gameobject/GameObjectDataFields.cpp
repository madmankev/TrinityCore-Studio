// Type -> named Data-field map. See GameObjectDataFields.h. Transcribed verbatim from
// GameObjectData.h (union field names + reference comments).

#include "editors/gameobject/GameObjectDataFields.h"

namespace qe
{
namespace
{
// Shorthands: N(i,label) = plain numeric field; P(i,label,kind) = id-name picker.
constexpr GoDataField N(uint8_t i, const char* l) { return {i, l, false, RefKind::Item}; }
constexpr GoDataField P(uint8_t i, const char* l, RefKind k) { return {i, l, true, k}; }
} // namespace

const std::vector<GoDataField>& GameObjectDataFields(uint8_t type)
{
    static const std::vector<GoDataField> door = {
        N(0, "startOpen"), N(1, "lockId (Lock.dbc)"), N(2, "autoCloseTime"), N(3, "noDamageImmune"),
        N(4, "openTextID"), N(5, "closeTextID"), N(6, "ignoredByPathing"), N(7, "conditionID1")};
    static const std::vector<GoDataField> button = {
        N(0, "startOpen"), N(1, "lockId (Lock.dbc)"), N(2, "autoCloseTime"),
        P(3, "linkedTrap", RefKind::GameObject), N(4, "noDamageImmune"), N(5, "large"),
        N(6, "openTextID"), N(7, "closeTextID"), N(8, "losOK"), N(9, "conditionID1")};
    static const std::vector<GoDataField> questgiver = {
        N(0, "lockId (Lock.dbc)"), N(1, "questList"), N(2, "pageMaterial"), N(3, "gossipID"),
        N(4, "customAnim"), N(5, "noDamageImmune"), N(6, "openTextID"), N(7, "losOK"),
        N(8, "allowMounted"), N(9, "large"), N(10, "conditionID1")};
    static const std::vector<GoDataField> chest = {
        N(0, "lockId (Lock.dbc)"), N(1, "lootId (edit on Loot tab)"), N(2, "chestRestockTime"),
        N(3, "consumable"), N(4, "minSuccessOpens"), N(5, "maxSuccessOpens"), N(6, "eventId"),
        P(7, "linkedTrapId", RefKind::GameObject), P(8, "questId", RefKind::Quest), N(9, "level"),
        N(10, "losOK"), N(11, "leaveLoot"), N(12, "notInCombat"), N(13, "logLoot"), N(14, "openTextID"),
        N(15, "groupLootRules"), N(16, "floatingTooltip"), N(17, "conditionID1")};
    static const std::vector<GoDataField> generic = {
        N(0, "floatingTooltip"), N(1, "highlight"), N(2, "serverOnly"), N(3, "large"),
        N(4, "floatOnWater"), P(5, "questID", RefKind::Quest), N(6, "conditionID1")};
    static const std::vector<GoDataField> trap = {
        N(0, "lockId (Lock.dbc)"), N(1, "level"), N(2, "diameter"), P(3, "spellId", RefKind::Spell),
        N(4, "type"), N(5, "cooldown"), N(6, "autoCloseTime"), N(7, "startDelay"), N(8, "serverOnly"),
        N(9, "stealthed"), N(10, "large"), N(11, "invisible"), N(12, "openTextID"), N(13, "closeTextID"),
        N(14, "ignoreTotems"), N(15, "conditionID1")};
    static const std::vector<GoDataField> chair = {
        N(0, "slots"), N(1, "height"), N(2, "onlyCreatorUse"), N(3, "triggeredEvent"),
        N(4, "conditionID1")};
    static const std::vector<GoDataField> spellFocus = {
        N(0, "focusId"), N(1, "dist"), P(2, "linkedTrapId", RefKind::GameObject), N(3, "serverOnly"),
        P(4, "questID", RefKind::Quest), N(5, "large"), N(6, "floatingTooltip"), N(7, "floatOnWater"),
        N(8, "conditionID1")};
    static const std::vector<GoDataField> text = {
        N(0, "pageID"), N(1, "language"), N(2, "pageMaterial"), N(3, "allowMounted"),
        N(4, "conditionID1")};
    static const std::vector<GoDataField> goober = {
        N(0, "lockId (Lock.dbc)"), P(1, "questId", RefKind::Quest), N(2, "eventId"),
        N(3, "autoCloseTime"), N(4, "customAnim"), N(5, "consumable"), N(6, "cooldown"), N(7, "pageId"),
        N(8, "language"), N(9, "pageMaterial"), P(10, "spellId", RefKind::Spell), N(11, "noDamageImmune"),
        P(12, "linkedTrapId", RefKind::GameObject), N(13, "large"), N(14, "openTextID"),
        N(15, "closeTextID"), N(16, "losOK"), N(17, "allowMounted"), N(18, "floatingTooltip"),
        N(19, "gossipID"), N(20, "WorldStateSetsState"), N(21, "floatOnWater"), N(22, "conditionID1")};
    static const std::vector<GoDataField> transport = {
        N(0, "pause"), N(1, "startOpen"), N(2, "autoCloseTime"), N(3, "pause1EventID"),
        N(4, "pause2EventID"), N(5, "mapID")};
    static const std::vector<GoDataField> areadamage = {
        N(0, "lockId (Lock.dbc)"), N(1, "radius"), N(2, "damageMin"), N(3, "damageMax"),
        N(4, "damageSchool"), N(5, "autoCloseTime"), N(6, "openTextID"), N(7, "closeTextID")};
    static const std::vector<GoDataField> camera = {
        N(0, "lockId (Lock.dbc)"), N(1, "cinematicId"), N(2, "eventID"), N(3, "openTextID"),
        N(4, "conditionID1")};
    static const std::vector<GoDataField> moTransport = {
        N(0, "taxiPathId"), N(1, "moveSpeed"), N(2, "accelRate"), N(3, "startEventID"),
        N(4, "stopEventID"), N(5, "transportPhysics"), N(6, "mapID"), N(7, "worldState1"),
        N(8, "canBeStopped")};
    static const std::vector<GoDataField> ritual = {
        N(0, "reqParticipants"), P(1, "spellId", RefKind::Spell), P(2, "animSpell", RefKind::Spell),
        N(3, "ritualPersistent"), P(4, "casterTargetSpell", RefKind::Spell),
        N(5, "casterTargetSpellTargets"), N(6, "castersGrouped"), N(7, "ritualNoTargetCheck"),
        N(8, "conditionID1")};
    static const std::vector<GoDataField> mailbox = {N(0, "conditionID1")};
    static const std::vector<GoDataField> guardpost = {
        P(0, "creatureID", RefKind::Creature), N(1, "charges")};
    static const std::vector<GoDataField> spellcaster = {
        P(0, "spellId", RefKind::Spell), N(1, "charges"), N(2, "partyOnly"), N(3, "allowMounted"),
        N(4, "large"), N(5, "conditionID1")};
    static const std::vector<GoDataField> meetingstone = {
        N(0, "minLevel"), N(1, "maxLevel"), N(2, "areaID")};
    static const std::vector<GoDataField> flagstand = {
        N(0, "lockId"), P(1, "pickupSpell", RefKind::Spell), N(2, "radius"),
        P(3, "returnAura", RefKind::Spell), P(4, "returnSpell", RefKind::Spell), N(5, "noDamageImmune"),
        N(6, "openTextID"), N(7, "losOK"), N(8, "conditionID1")};
    static const std::vector<GoDataField> fishinghole = {
        N(0, "radius"), N(1, "lootId (edit on Loot tab)"), N(2, "minSuccessOpens"),
        N(3, "maxSuccessOpens"), N(4, "lockId (Lock.dbc)")};
    static const std::vector<GoDataField> flagdrop = {
        N(0, "lockId"), N(1, "eventID"), P(2, "pickupSpell", RefKind::Spell), N(3, "noDamageImmune"),
        N(4, "openTextID")};
    static const std::vector<GoDataField> miniGame = {N(0, "gameType")};
    static const std::vector<GoDataField> capturePoint = {
        N(0, "radius"), P(1, "spell", RefKind::Spell), N(2, "worldState1"), N(3, "worldstate2"),
        N(4, "winEventID1"), N(5, "winEventID2"), N(6, "contestedEventID1"), N(7, "contestedEventID2"),
        N(8, "progressEventID1"), N(9, "progressEventID2"), N(10, "neutralEventID1"),
        N(11, "neutralEventID2"), N(12, "neutralPercent"), N(13, "worldstate3"), N(14, "minSuperiority"),
        N(15, "maxSuperiority"), N(16, "minTime"), N(17, "maxTime"), N(18, "large"), N(19, "highlight"),
        N(20, "startingValue"), N(21, "unidirectional")};
    static const std::vector<GoDataField> auraGenerator = {
        N(0, "startOpen"), N(1, "radius"), P(2, "auraID1", RefKind::Spell), N(3, "conditionID1"),
        P(4, "auraID2", RefKind::Spell), N(5, "conditionID2"), N(6, "serverOnly")};
    static const std::vector<GoDataField> dungeonDifficulty = {N(0, "mapID"), N(1, "difficulty")};
    static const std::vector<GoDataField> barberChair = {N(0, "chairheight"), N(1, "heightOffset")};
    static const std::vector<GoDataField> building = {
        N(0, "intactNumHits"), P(1, "creditProxyCreature", RefKind::Creature), N(2, "empty1"),
        N(3, "intactEvent"), N(4, "empty2"), N(5, "damagedNumHits"), N(6, "empty3"), N(7, "empty4"),
        N(8, "empty5"), N(9, "damagedEvent"), N(10, "empty6"), N(11, "empty7"), N(12, "empty8"),
        N(13, "empty9"), N(14, "destroyedEvent"), N(15, "empty10"), N(16, "rebuildingTimeSecs"),
        N(17, "empty11"), N(18, "destructibleData"), N(19, "rebuildingEvent"), N(20, "empty12"),
        N(21, "empty13"), N(22, "damageEvent"), N(23, "empty14")};
    static const std::vector<GoDataField> guildbank = {N(0, "conditionID1")};
    static const std::vector<GoDataField> trapdoor = {
        N(0, "whenToPause"), N(1, "startOpen"), N(2, "autoClose")};
    static const std::vector<GoDataField> empty = {};

    switch (type)
    {
        case 0:  return door;
        case 1:  return button;
        case 2:  return questgiver;
        case 3:  return chest;
        case 5:  return generic;
        case 6:  return trap;
        case 7:  return chair;
        case 8:  return spellFocus;
        case 9:  return text;
        case 10: return goober;
        case 11: return transport;
        case 12: return areadamage;
        case 13: return camera;
        case 15: return moTransport;
        case 18: return ritual;
        case 19: return mailbox;
        case 21: return guardpost;
        case 22: return spellcaster;
        case 23: return meetingstone;
        case 24: return flagstand;
        case 25: return fishinghole;
        case 26: return flagdrop;
        case 27: return miniGame;
        case 29: return capturePoint;
        case 30: return auraGenerator;
        case 31: return dungeonDifficulty;
        case 32: return barberChair;
        case 33: return building;
        case 34: return guildbank;
        case 35: return trapdoor;
        default: return empty;  // 4 Binder, 14 MapObject, 16/17/20/28 empty/unused
    }
}
} // namespace qe
