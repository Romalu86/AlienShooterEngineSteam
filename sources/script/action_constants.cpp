#include "action_constants.h"

namespace as1
{
#if !defined(_WIN32) || UINTPTR_MAX != 0xFFFFFFFFu
    const char* actionCodeName(std::uint32_t opcode)
    {
        switch (opcode)
        {
        case 32: return "ACT_ATTACK";
        case 33: return "ACT_MOVE";
        case 34: return "ACT_MOVE_TO";
        case 35: return "ACT_BUILD_UNIT";
        case 36: return "ACT_PATROL";
        case 37: return "ACT_COOR_ATTACK";
        case 38: return "ACT_RANDOM";
        case 39: return "ACT_STOP";
        case 40: return "ACT_PAUSE";
        case 41: return "ACT_ROTATE";
        case 42: return "ACT_CLEAR_COMMAND";
        case 43: return "ACT_FLAGMAN_TRIGGER";
        case 50: return "ACT_PATH_BLOCK";
        case 51: return "ACT_PATH_GROUND";
        case 52: return "ACT_PATH_LIMIT";
        case 54: return "ACT_ADD_ITEM";
        case 55: return "ACT_DELETE_ITEM";
        case 56: return "ACT_HAVE_ITEM";
        case 57: return "ACT_DELETE_ALL_ITEM";
        case 58: return "ACT_GET_ITEM";
        case 60: return "ACT_CHANGE_DIRECTION";
        case 61: return "ACT_CHANGE_ANIMATION";
        case 62: return "ACT_CHANGE_VID";
        case 63: return "ACT_CHANGE_COOR";
        case 70: return "ACT_BACKUP_COMMAND";
        case 71: return "ACT_CYCLE_STACK";
        case 72: return "ACT_CLEAR_STACK";
        case 73: return "ACT_STOP_STACK";
        case 80: return "ACT_SAVE";
        case 81: return "ACT_RESTORE";
        case 85: return "ACT_DAMAGE";
        case 86: return "ACT_REPAIR";
        case 87: return "ACT_GET_HP";
        case 88: return "ACT_SET_HP";
        case 89: return "ACT_GET_PERCENT_HP";
        case 90: return "ACT_GET_GOAL";
        case 91: return "ACT_GET_ANIMATION";
        case 92: return "ACT_GET_AMMO";
        case 93: return "ACT_ADD_AMMO";
        case 94: return "ACT_GET_BEHAVE";
        case 95: return "ACT_SET_BEHAVE";
        case 96: return "ACT_GET_ARMY";
        case 97: return "ACT_SET_ARMY";
        case 98: return "ACT_SET_INVISIBLE";
        case 100: return "ACT_GET_BATTLE_RANGE";
        case 101: return "ACT_GET_LINK";
        case 102: return "ACT_SET_LINK";
        case 103: return "ACT_GET_UPLINK";
        case 104: return "ACT_SET_UPLINK";
        case 105: return "ACT_GET_TIMER";
        case 106: return "ACT_SET_TIMER";
        case 107: return "ACT_GET_ZSPEED";
        case 108: return "ACT_SET_ZSPEED";
        case 109: return "ACT_GET_SPEED";
        case 110: return "ACT_SET_SPEED";
        case 111: return "ACT_GET_COMMAND";
        case 112: return "ACT_SET_DEATH_TIMER";
        case 120: return "ACT_SET_TEXT";
        case 121: return "ACT_GET_TEXT_DESC";
        case 122: return "ACT_SET_TEXT_COUNT";
        case 123: return "ACT_SET_FILE";
        case 124: return "ACT_GET_TEXT";
        case 130: return "ACT_NEXT_COMMAND";
        case 131: return "ACT_LOGIC_RUN";
        case 132: return "ACT_UNDO_REMOVE";
        case 133: return "ACT_UNDO_INSERT";
        case 134: return "ACT_DESTROY_UNIT";
        case 135: return "ACT_PLAY_SFX";
        case 150: return "ACT_LINK_ENGINE";
        case 151: return "ACT_CLASH_ENGINE";
        case 152: return "ACT_FORCELINK_ENGINE";
        case 153: return "ACT_TRAIN_BEHAVE";
        case 154: return "ACT_FIRST_ENGINE";
        case 155: return "ACT_LAST_ENGINE";
        case 156: return "ACT_NEXT_ENGINE";
        case 157: return "ACT_IS_FIRST";
        case 158: return "ACT_IN_TRAIN";
        case 159: return "ACT_IS_TRAIN";
        case 255: return "ACT_NONE";
        default: return nullptr;
        }
    }

    const char* animationCodeName(std::uint32_t opcode)
    {
        switch (opcode)
        {
        case 0: return "ANI_STAND";
        case 1: return "ANI_STOP_MOVE";
        case 2: return "ANI_GO";
        case 3: return "ANI_START_MOVE";
        case 4: return "ANI_L_ROTATE";
        case 5: return "ANI_R_ROTATE";
        case 6: return "ANI_OPEN";
        case 7: return "ANI_HIT";
        case 8: return "ANI_FIGHT";
        case 9: return "ANI_SALUT";
        case 10: return "ANI_STAND_OPEN";
        case 11: return "ANI_LOAD";
        case 12: return "ANI_UNLOAD";
        case 13: return "ANI_WOUND";
        case 14: return "ANI_BIRTH";
        case 15: return "ANI_DEATH";
        case 16: return "ANI_DEATH2";
        default: return nullptr;
        }
    }

    const char* spriteClassCodeName(std::uint32_t value)
    {
        switch (value)
        {
        case 0: return "B_TERRAIN";
        case 1: return "B_OBJECT";
        case 2: return "B_UNIT";
        case 3: return "B_BUILDING";
        case 4: return "B_AVIA";
        case 5: return "B_CANNON";
        case 6: return "B_PRIMITIVE";
        case 9: return "B_SPRITE";
        case 10: return "B_FRAME";
        case 12: return "B_LINKER";
        case 19: return "B_TEXT";
        case 20: return "B_CIV_ROBOT";
        case 21: return "B_ENGINE";
        case 22: return "B_RAIL";
        case 23: return "B_REGION";
        case 24: return "B_DEPO";
        case 25: return "B_CREATURE";
        case 26: return "B_BALLOON";
        case 27: return "B_MISSILE";
        default: return nullptr;
        }
    }

    bool isActionCode(std::uint32_t opcode) { return actionCodeName(opcode) != nullptr; }
    bool isAnimationCode(std::uint32_t opcode) { return animationCodeName(opcode) != nullptr; }
#endif
}
