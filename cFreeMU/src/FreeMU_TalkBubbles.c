#include "FreeMU.h"

static const u16 sFmuTalkBubbleSprite[] = { 1, 0x400F, 0x01EE, 0x0870 };

#define FMU_HP_BARS_OFF() (((u8 *)&gPlaySt)[0x41] & 0x80)

static bool Fmu_CanTalkOrSupport(struct Unit * walker, struct Unit * unit)
{
    int i;
    u8 pid = unit->pCharacterData->number;

    if (CheckForCharacterEvents(walker->pCharacterData->number, pid))
        return TRUE;

    if (UNIT_FACTION(unit) != FACTION_BLUE)
        return FALSE;

    for (i = 0; i < GetUnitSupporterCount(walker); i++)
    {
        if (GetUnitSupporterCharacter(walker, i) == pid)
            return CanUnitSupportNow(walker, i) != 0;
    }

    return FALSE;
}

static void Fmu_PutTalkBubbles(void)
{
    struct FMUProc * proc;
    struct Unit * walker;
    int i, x, y;

    if (!Fmu_IsEnabled() || (*FreeMoveFlag & FMU_STATE_PAUSED))
        return;

    if (FMU_HP_BARS_OFF() || CheckFlag(EVFLAG_HIDE_BLINKING_ICON))
        return;

    proc = Fmu_GetProc();

    if ((proc == NULL) || !UNIT_IS_VALID(proc->unit))
        return;

    walker = proc->unit;

    for (i = 1; i < 0xC0; i++)
    {
        struct Unit * unit = GetUnit(i);

        if (!UNIT_IS_VALID(unit) || (unit == walker))
            continue;

        if ((unit->state & US_HIDDEN) || GetUnitSpriteHideFlag(unit))
            continue;

        x = unit->xPos * 16 - gBmSt.camera.x;
        y = unit->yPos * 16 - gBmSt.camera.y;

        if (((unsigned)(x + 16) > 0x100) || ((unsigned)(y + 16) > 0xB0))
            continue;

        if (!Fmu_CanTalkOrSupport(walker, unit))
            continue;

        CallARM_PushToSecondaryOAM(OAM1_X(x + 0x20C), OAM0_Y(y + 0xEE), sFmuTalkBubbleSprite, 0);
    }
}

void Fmu_PutUnitSpritesOam(void)
{
    Fmu_PutTalkBubbles();
    PutUnitSpritesOam();
}
