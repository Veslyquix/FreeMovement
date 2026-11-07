#include "FreeMU.h"

/*
 * The lists in HookList.event are tried in order until one returns TRUE.
 * Only one button is handled at a time, in this order. B has its own list,
 * which runs at once, even while walking.
 */
static const struct
{
    u16 key;
    ButtonFunc * funcs;
} sFmuButtons[] = {
    { A_BUTTON, FMU_FunctionList_OnPressA },
    { L_BUTTON, FMU_FunctionList_OnPressL },
    { R_BUTTON, FMU_FunctionList_OnPressR },
    { SELECT_BUTTON, FMU_FunctionList_OnPressSelect },
    { START_BUTTON, FMU_FunctionList_OnPressStart },
};

bool Fmu_RunButtonList(struct FMUProc * proc, ButtonFunc * it)
{
    while (*it)
    {
        if ((*it++)(proc))
            return TRUE;
    }

    return FALSE;
}

// TRUE if one of the buttons was pressed, even if nothing happened
bool Fmu_HandleButtons(struct FMUProc * proc, u16 keys)
{
    unsigned i;

    for (i = 0; i < ARRAY_COUNT(sFmuButtons); i++)
    {
        if (!(keys & sFmuButtons[i].key))
            continue;

        Fmu_RunButtonList(proc, sFmuButtons[i].funcs);
        Fmu_ResetDisplayEffects();
        return TRUE;
    }

    return FALSE;
}


/*!!!!*/

bool FMU_OnButton_StartMenu(FMUProc * proc)
{
    StartOrphanMenuAdjusted(&FreeMovementLMenu, proc->unit->xPos * 16 - gBmSt.camera.x, 1, 0x17);
    return TRUE;
}

// Talk, Visit, Trade, Supply: only commands that don't need the player phase
bool FMU_OnButton_UnitMenu(FMUProc * proc)
{
    const struct MenuItemDef * item;
    int i;

    for (i = 0, item = FreeMovementUnitMenu.menuItems; item->isAvailable != NULL; i++, item++)
    {
        if (item->isAvailable(item, i) != MENU_NOTSHOWN)
            break;
    }

    if (item->isAvailable == NULL)
        return FALSE;

    StartSemiCenteredOrphanMenu(&FreeMovementUnitMenu, proc->unit->xPos * 16 - gBmSt.camera.x, 1, 0x16);
    return TRUE;
}

// Ends free movement and the player phase
bool FMU_OnButton_EndFreeMove(FMUProc * proc)
{
    DisableFreeMovementASMC();
    return TRUE;
}

bool FMU_OnButton_ChangeUnit(FMUProc * proc)
{
    struct Unit * unit = Fmu_FindNextUnit(proc->unit);

    if ((unit == NULL) || (unit == proc->unit))
        return FALSE;

    // buttons wait for every walker to stand still, so no step gets cut off
    Fmu_EndWalkerMus(proc, TRUE);
    Fmu_SetUnit(proc, unit);
    Fmu_RefreshFollowers(proc);
    Fmu_CenterCamera(proc, unit->xPos, unit->yPos);

    return TRUE;
}

// Stats of the unit in front, or of the walking unit
bool FMU_OnButton_ShowStats(FMUProc * proc)
{
    struct Unit * unit = proc->unit;
    int x, y;

    Fmu_GetFacedTile(proc, &x, &y);

    if (Fmu_IsInBounds(x, y) && (gBmMapUnit[y][x] != 0))
        unit = GetUnit(gBmMapUnit[y][x]);

    if (!UNIT_IS_VALID(unit) || !CanShowUnitStatScreen(unit))
        return FALSE;

    EndAllMus();
    EndPlayerPhaseSideWindows();

    SetStatScreenConfig(
        STATSCREEN_CONFIG_NONDEAD | STATSCREEN_CONFIG_NONBENCHED | STATSCREEN_CONFIG_NONUNK9 |
        STATSCREEN_CONFIG_NONROOFED | STATSCREEN_CONFIG_NONUNK16);

    StartStatScreen(unit, proc);
    return TRUE;
}

bool FMU_OnButton_ToggleRun(FMUProc * proc)
{
    bool run = !(*FreeMoveFlag & FMU_STATE_RUNNING);

    Fmu_SetStateBits(FMU_STATE_RUNNING, run);
    proc->moveSpeed = run ? FreeMU_RunningSpeed : FreeMU_MovingSpeed;

    return TRUE;
}

// Grid Mode: the normal player phase for the rest of this turn
u8 FMU_GridModeCommand(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    struct FMUProc * proc = Fmu_GetProc();

    if (proc != NULL)
    {
        Fmu_SetStateBits(FMU_STATE_ENABLED | FMU_STATE_KEEP_UNIT | FMU_STATE_PAUSED, FALSE);
        Fmu_HandOffToPlayerPhase(proc);
    }

    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

// MenuStdHelpBox, but no box for items without help text
u8 FMU_MenuHelpBox(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    if (menuItem->def->helpMsgId == 0)
    {
        CloseHelpBox();
        return 0;
    }

    StartHelpBox(menuItem->xTile * 8, menuItem->yTile * 8, menuItem->def->helpMsgId);
    return 0;
}
