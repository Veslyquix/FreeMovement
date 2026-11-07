#include "FreeMU.h"

/*
 * Vanilla functions with free movement added. lyn hooks each one over the
 * original, since they have vanilla names. They behave as vanilla does
 * while free movement is off.
 */

extern const short sMoveOffsetLut[];
extern const u8 sMuWalkSpeedLut[];

#define EVT_CMD_HI(cmd) (((cmd) & 0xFFFF0000) >> 16)

struct EvCheck02 {
    u32 unk0;
    u32 script;
    u8 turn;
    u8 maxTurn;
    u16 faction;
};

// The walking unit keeps walking while a direction is held
void RunMuMoveScript(struct MuProc * proc)
{
    while (true)
    {
        short command = proc->config->movescr[proc->config->pc++];
        int fmuCommand = Fmu_GetContinuedMoveCommand(proc);

        if (fmuCommand >= 0)
            command = fmuCommand;

        switch (command) {
        case MOVE_CMD_SLEEP:
            proc->move_clock_q4 = proc->config->movescr[proc->config->pc++];
            proc->state = MU_STATE_SLEEPING;
            return;

        case MOVE_CMD_BUMP:
            EndMuMovement(proc);
            proc->state = MU_STATE_BUMPING;
            StartMuFogBump(
                (proc->x_q4 >> MU_SUBPIXEL_PRECISION) - gBmSt.camera.x,
                (proc->y_q4 >> MU_SUBPIXEL_PRECISION) - gBmSt.camera.y
            );
            return;

        case MOVE_CMD_HALT:
            HaltMu(proc);
            return;

        case MOVE_CMD_END:
            EndMuMovement(proc);
            EndMu(proc);
            return;

        case MOVE_CMD_MOVE_LEFT:
        case MOVE_CMD_MOVE_RIGHT:
        case MOVE_CMD_MOVE_DOWN:
        case MOVE_CMD_MOVE_UP:
            command = command - MOVE_CMD_MOVE_BASE;
            if (command != proc->facing)
            {
                SetMuFacing(proc, command);
                proc->state = MU_STATE_MOVEMENT;
            }
            return;

        case MOVE_CMD_FACE_LEFT:
        case MOVE_CMD_FACE_RIGHT:
        case MOVE_CMD_FACE_DOWN:
        case MOVE_CMD_FACE_UP:
            command = command - MOVE_CMD_FACE_BASE;
            if (command != proc->facing)
                SetMuFacing(proc, command);

            break;

        case MOVE_CMD_SET_SPEED:
            proc->moveConfig = proc->config->movescr[proc->config->pc++];
            break;

        case MOVE_CMD_CAMERA_ON:
            EnableMuCamera(proc);
            break;

        case MOVE_CMD_CAMERA_OFF:
            DisableMuCamera(proc);
            break;
        }
    }
}

// The camera keeps the walking unit centered
void Mu_OnStateMovement(struct MuProc * proc)
{
    unsigned moveSpeed = GetMuQ4MovementSpeed(proc);

    proc->move_clock_q4 = moveSpeed + proc->move_clock_q4;

    proc->x_q4 += moveSpeed * sMoveOffsetLut[proc->facing * 2 + 0];
    proc->y_q4 += moveSpeed * sMoveOffsetLut[proc->facing * 2 + 1];

    if ((proc->move_clock_q4 / 16) >= 16)
    {
        proc->move_clock_q4 -= 0x100;

        proc->x_q4 -= proc->move_clock_q4 * sMoveOffsetLut[proc->facing * 2 + 0];
        proc->y_q4 -= proc->move_clock_q4 * sMoveOffsetLut[proc->facing * 2 + 1];

        proc->move_clock_q4 = 0;

        proc->x_q4 &= ~0xF;
        proc->y_q4 &= ~0xF;
    }

    if (Fmu_IsActiveForMu(proc))
        Fmu_UpdateMuCamera(proc);

    if (proc->cam_b && !Proc_Find(ProcScr_CamMove))
    {
        gBmSt.camera.x = GetCameraAdjustedX(proc->x_q4 >> MU_SUBPIXEL_PRECISION);
        gBmSt.camera.y = GetCameraAdjustedY(proc->y_q4 >> MU_SUBPIXEL_PRECISION);
    }

    if (!(proc->moveConfig & MU_CONFIG_SPPED_NORMAL))
        UpdateMuStepSounds(proc);
}

// Walking/running speed
u16 GetMuQ4MovementSpeed(struct MuProc * proc)
{
    int config = proc->moveConfig;

    if (Fmu_IsActiveForMu(proc))
        return Fmu_GetMuQ4MovementSpeed(proc);

    if (config & 0x80)
        config += 0x80;

    if (proc->fast_walk_b)
        return 0x100;

    if (config != 0x40)
    {
        if (config != 0x00)
        {
            int speed = config;

            if (speed & 0x40)
                speed ^= 0x40;
            else if (gPlaySt.config.gameSpeed || (gKeyStatusPtr->heldKeys & A_BUTTON))
                speed *= 4;

            if (speed > 0x80)
                speed = 0x80;

            return speed;
        }

        if (!IsFirstPlaythrough() && (gKeyStatusPtr->heldKeys & A_BUTTON))
            return 0x80;

        if (gPlaySt.config.gameSpeed)
            return 0x40;
    }

    return 16 * sMuWalkSpeedLut[GetClassData(proc->jid)->slowWalking];
}

// We move our walkers without MuCtr, so this is an event moving one of them
void MuCtr_OnEnd(struct MuCtrlProc * proc)
{
    struct MuProc * muProc = proc->muProc;
    struct Unit * unit = proc->unit;
    struct FMUProc * fmu;

    if ((muProc->fast_walk_b != 0) && (proc->redaCount != 0))
    {
        MoveUnit_(unit, proc->x, proc->y, proc->flags & 0xFFFE);
    }
    else
    {
        unit->xPos = proc->x;
        unit->yPos = proc->y;
        UnitFinalizeMovement(unit);

        ShowUnitSprite(unit);
        unit->state &= ~US_HIDDEN;

        gBmMapUnit[unit->yPos][unit->xPos] = unit->index;

        RefreshEntityBmMaps();
        RefreshUnitSprites();
    }

    EndMu(muProc);
    SortMus();

    ClearEventMoveBuffer(proc->redas);

    // the followers find new places afterwards
    if (Fmu_IsActive())
    {
        fmu = Fmu_GetProc();

        if (Fmu_IsWalker(fmu, unit))
            fmu->regroup = TRUE;
    }
}

// No turn events
int EvCheck02_TURN(struct EventInfo * info)
{
    struct EvCheck02 * listScript = (void *)info->listScript;

    int turn = listScript->turn;
    int maxTurn = listScript->maxTurn;
    int faction = listScript->faction;

    if (maxTurn == 0)
        maxTurn = turn;
    else if (maxTurn == 0xff)
        maxTurn = INT32_MAX;

    if ((turn <= gPlaySt.chapterTurnNumber) && (gPlaySt.chapterTurnNumber <= maxTurn) && (gPlaySt.faction == faction))
    {
        if (Fmu_IsEnabled())
            return 0;

        info->script = listScript->script;
        info->flag = EVT_CMD_HI(listScript->unk0);

        return 1;
    }

    return 0;
}

// A unit we stood on can be missing from the unit map
u8 ItemSubMenu_IsTradeAvailable(const struct MenuItemDef * def, int number)
{
    if (Fmu_IsActive())
        RefreshEntityBmMaps();

    if (gActiveUnit->state & US_HAS_MOVED)
        return MENU_NOTSHOWN;

    if (gBmSt.taken_action & BM_TAKEN_ACTION_TRADE)
        return MENU_NOTSHOWN;

    if (UNIT_CATTRIBUTES(gActiveUnit) & CA_SUPPLY)
        return MENU_NOTSHOWN;

    MakeTradeTargetList(gActiveUnit);

    if (GetSelectTargetCount() == 0)
        return MENU_NOTSHOWN;

    return MENU_ENABLED;
}

// No player phase will carry out the action, so visit now
u8 VisitCommandEffect(struct MenuProc * menu, struct MenuItemProc * menuItem)
{
    if (menuItem->availability == MENU_DISABLED)
    {
        MenuFrozenHelpBox(menu, 0x84C);
        return MENU_ACT_SND6B;
    }

    gActionData.unitActionType = UNIT_ACTION_VISIT;

    if (Fmu_IsActive())
        StartAvailableTileEvent(gActiveUnit->xPos, gActiveUnit->yPos);

    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

// Same for Talk
u8 TalkSelection_OnSelect(ProcPtr proc, struct SelectTarget * target)
{
    gActionData.unitActionType = UNIT_ACTION_TALK;
    gActionData.targetIndex = target->uid;

    if (Fmu_IsActive())
        StartCharacterEvent(gActiveUnit->pCharacterData->number, GetUnit(target->uid)->pCharacterData->number);

    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

// Cancelling Talk or Trade goes back to our unit menu
u8 GenericSelection_BackToUM(ProcPtr proc, struct SelectTarget * target)
{
    EndTargetSelection(proc);

    BG_Fill(gBG2TilemapBuffer, 0);
    BG_EnableSyncByMask(BG2_SYNC_BIT);

    ResetTextFont();

    HideMoveRangeGraphics();

    if (Fmu_IsActive())
    {
        StartSemiCenteredOrphanMenu(&FreeMovementUnitMenu, gActiveUnit->xPos * 16 - gBmSt.camera.x, 1, 22);
        return MENU_ACT_SKIPCURSOR | MENU_ACT_SND6B | MENU_ACT_CLEAR;
    }

    EnsureCameraOntoPosition(
        StartSemiCenteredOrphanMenu(&gUnitActionMenuDef, gBmSt.cursorTarget.x - gBmSt.camera.x, 1, 22),
        gActiveUnit->xPos,
        gActiveUnit->yPos
    );

    return MENU_ACT_SKIPCURSOR | MENU_ACT_SND6B | MENU_ACT_CLEAR;
}

// Redraw the map afterwards, as the player phase does
void StatScreen_OnClose(void)
{
    struct FMUProc * proc;

    gPlaySt.chapterStateBits = (gPlaySt.chapterStateBits &~ 3) | (gStatScreen.page & 3);
    sStatScreenInfo.unitId = gStatScreen.unit->index;

    SetInterrupt_LCDVCountMatch(NULL);

    gLCDControlBuffer.dispcnt.bg0_on = FALSE;
    gLCDControlBuffer.dispcnt.bg1_on = FALSE;
    gLCDControlBuffer.dispcnt.bg2_on = FALSE;
    gLCDControlBuffer.dispcnt.bg3_on = FALSE;
    gLCDControlBuffer.dispcnt.obj_on = FALSE;

    if (Fmu_IsActive())
    {
        proc = Fmu_GetProc();

        proc->restoreMap = TRUE;
        proc->yield = TRUE;
        proc->countdown = 1;
    }
}

// No phase banner
void PhaseIntro_EndIfNoUnits(ProcPtr proc)
{
    if (Fmu_StartsThisPhase() || (GetPhaseAbleUnitCount(gPlaySt.faction) == FACTION_BLUE))
        Proc_End(proc);
}

// No cursor reset, free movement moves the camera itself
void ProcFun_ResetCursorPosition(ProcPtr proc)
{
    int x, y;

    x = -1;
    y = -1;

    if (Fmu_StartsThisPhase() || (0 == GetPhaseAbleUnitCount(gPlaySt.faction)))
    {
        Proc_End(proc);
        return;
    }

    switch (gPlaySt.faction) {
    case FACTION_BLUE:
        GetPlayerStartCursorPosition(&x, &y);
        break;

    case FACTION_GREEN:
    case FACTION_RED:
        GetEnemyStartCursorPosition(&x, &y);
        break;

    default:
        break;
    }

    if ((x >= 0) && (y >= 0))
    {
        EnsureCameraOntoPosition(proc, x, y);
        SetCursorMapPosition(x, y);
    }
}

// After Supply: the unit stays a map sprite
void sub_809EB14(void)
{
    if (Fmu_IsActive())
        return;

    if (gActiveUnit)
    {
        HideUnitSprite(gActiveUnit);
        StartMu(gActiveUnit);
        SetAutoMuDefaultFacing();
    }
}

void GotoChapterWithoutSave(u16 chapterId)
{
    gPlaySt.chapterIndex = chapterId;

    Proc_Goto(Proc_Find(gProc_BMapMain), 2);
    Proc_EndEach(gProcScr_PlayerPhase);
    Proc_EndEach(gProcScr_CpPhase);
    Proc_EndEach(gProcScr_BerserkCpPhase);
    Fmu_EndForChapterChange();
}

// Chapter start: free movement stays off until an event enables it
void ResetChapterFlags(void)
{
    int i;

    for (i = 0; i < (32 + 8) / 8; i++)
        gChapterFlagBits[i] = 0;

    *FreeMoveFlag = FMU_DEFAULT_FOLLOWERS << FMU_STATE_FOLLOWERS_SHIFT;
}

// Closing Status and the like doesn't bring back the terrain and goal windows
void StartPlayerPhaseSideWindows(void)
{
    if (Fmu_IsActive())
        return;

    Proc_Start(gProcScr_SideWindowMaker, PROC_TREE_3);
}

// For the hook in RefreshUnitSprites ($271E0)
bool Fmu_ShouldDrawUnitSprite(struct Unit * unit)
{
    if (!Fmu_IsActive())
        return gBmMapUnit[unit->yPos][unit->xPos] != 0;

    // walkers are drawn by their MUs, and a unit we walked over may be off the unit map
    return !Fmu_IsShownAsMu(unit);
}
