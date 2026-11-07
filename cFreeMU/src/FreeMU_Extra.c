#include "FreeMU.h"

const s8 gFmuFacingOffsets[4][2] = {
    [FACING_LEFT] = { -1, 0 },
    [FACING_RIGHT] = { +1, 0 },
    [FACING_DOWN] = { 0, +1 },
    [FACING_UP] = { 0, -1 },
};

/*
 * Basic!
 */
bool Fmu_IsInBounds(int x, int y)
{
    return (x >= 0) && (y >= 0) && (x < gBmMapSize.x) && (y < gBmMapSize.y);
}

bool FMU_CanUnitBeOnPos(struct Unit * unit, s8 x, s8 y)
{
    int uid;

    if (!Fmu_IsInBounds(x, y))
        return FALSE; // position out of bounds

    // player units can be walked through
    uid = gBmMapUnit[y][x];

    if ((uid != 0) && (UNIT_FACTION(GetUnit(uid)) != FACTION_BLUE))
        return FALSE;

    if (gBmMapHidden[y][x] & HIDDEN_BIT_UNIT)
        return FALSE; // a hidden unit is occupying this position

    return GetUnitMovementCost(unit)[gBmMapTerrain[y][x]] > 0;
}

void Fmu_SetStateBits(u8 bits, bool on)
{
    if (on)
        *FreeMoveFlag |= bits;
    else
        *FreeMoveFlag &= ~bits;
}

bool Fmu_IsEnabled(void)
{
    return (*FreeMoveFlag & FMU_STATE_ENABLED) != 0;
}

u8 GetFreeMovementState(void)
{
    return (*FreeMoveFlag & FMU_STATE_ENABLED);
}

// Running right now, not just enabled for the next player phase
bool Fmu_IsActive(void)
{
    if (!Fmu_IsEnabled())
        return FALSE;

    return Fmu_GetProc() != NULL;
}

bool Fmu_StartsThisPhase(void)
{
    return Fmu_IsEnabled() && (gPlaySt.faction == FACTION_BLUE);
}

void Fmu_GetFacedTile(struct FMUProc * proc, int * x, int * y)
{
    *x = proc->xTo + gFmuFacingOffsets[proc->facing][0];
    *y = proc->yTo + gFmuFacingOffsets[proc->facing][1];
}

bool Fmu_CanControlUnit(struct Unit * unit)
{
    if (!UNIT_IS_VALID(unit))
        return FALSE;

    if (UNIT_FACTION(unit) != FACTION_BLUE)
        return FALSE;

    if (unit->state & (US_DEAD | US_NOT_DEPLOYED | US_RESCUED | US_UNDER_A_ROOF | US_BIT16))
        return FALSE;

    if ((unit->xPos < 0) || (GetUnitCurrentHp(unit) <= 0))
        return FALSE;

    return TRUE;
}

// The next player unit we can walk as after `after` (or the first one), wrapping around
struct Unit * Fmu_FindNextUnit(struct Unit * after)
{
    int start = (after != NULL) ? after->index : FACTION_BLUE;
    int i;

    for (i = 1; i < FACTION_GREEN; i++)
    {
        int id = start + i;
        struct Unit * unit;

        if (id >= FACTION_GREEN)
            id -= FACTION_GREEN - 1;

        unit = GetUnit(id);

        if (Fmu_CanControlUnit(unit))
            return unit;
    }

    return NULL;
}

void Fmu_SetUnit(struct FMUProc * proc, struct Unit * unit)
{
    proc->unit = unit;
    proc->xTo = proc->xCur = unit->xPos;
    proc->yTo = proc->yCur = unit->yPos;

    gActiveUnit = unit;
    gActiveUnitId = unit->index;

    // suspends save gActionData, so this keeps the unit
    gActionData.subjectIndex = unit->index;
    Fmu_SetStateBits(FMU_STATE_KEEP_UNIT, TRUE);
}

void Fmu_SetFacing(struct FMUProc * proc, int facing)
{
    struct MuProc * mu;

    proc->facing = facing;

    *FreeMoveFlag = (*FreeMoveFlag & ~FMU_STATE_FACING_MASK) |
        ((facing << FMU_STATE_FACING_SHIFT) & FMU_STATE_FACING_MASK);

    if (!proc->stepping && ((mu = Fmu_FindUnitMu(proc->unit)) != NULL))
        SetMuFacing(mu, facing);
}

void Fmu_ResetDisplayEffects(void)
{
    gLCDControlBuffer.dispcnt.win0_on = FALSE;
    gLCDControlBuffer.dispcnt.win1_on = FALSE;
    gLCDControlBuffer.dispcnt.objWin_on = FALSE;
    gLCDControlBuffer.bldcnt.effect = 0;
}

int Fmu_GetCameraTargetX(int x)
{
    int result = x - FMU_CAMERA_MARGIN_X;

    if (result < 0)
        result = 0;

    if (result > gBmSt.cameraMax.x)
        result = gBmSt.cameraMax.x;

    return result;
}

int Fmu_GetCameraTargetY(int y)
{
    int result = y - FMU_CAMERA_MARGIN_Y;

    if (result < 0)
        result = 0;

    if (result > gBmSt.cameraMax.y)
        result = gBmSt.cameraMax.y;

    return result;
}

// The library's CamMoveProc puts `to` at 0x2A, but the game reads it at 0x2C
struct FmuCamMoveProc
{
    /* 00 */ PROC_HEADER;
    /* 29 */ u8 pad[3];
    /* 2C */ struct Vec2 to;
    /* 30 */ struct Vec2 from;
    /* 34 */ struct Vec2 watchedCoordinate;
};

// Like EnsureCameraOntoPosition, but centers the tile
bool Fmu_CenterCamera(ProcPtr parent, int x, int y)
{
    struct FmuCamMoveProc * cam;

    int xTarget = Fmu_GetCameraTargetX(x * 16);
    int yTarget = Fmu_GetCameraTargetY(y * 16);

    if ((xTarget == gBmSt.camera.x) && (yTarget == gBmSt.camera.y))
        return FALSE;

    if (Proc_Find(ProcScr_CamMove))
        return FALSE;

    if (parent)
        cam = Proc_StartBlocking(ProcScr_CamMove, parent);
    else
        cam = Proc_Start(ProcScr_CamMove, PROC_TREE_3);

    cam->from.x = gBmSt.camera.x;
    cam->from.y = gBmSt.camera.y;

    cam->to.x = xTarget;
    cam->to.y = yTarget;

    cam->watchedCoordinate.x = x;
    cam->watchedCoordinate.y = y;

    return TRUE;
}

// Only free movement lets units share a tile, so step off when leaving it
void Fmu_SeparateUnit(struct Unit * unit)
{
    struct Vec2 pos;
    int i;

    if (!Fmu_CanControlUnit(unit))
        return;

    for (i = 1; i < UNIT_ID_END; i++)
    {
        struct Unit * other = GetUnit(i);

        if ((other == unit) || !UNIT_IS_VALID(other) || (other->state & US_HIDDEN))
            continue;

        if ((other->xPos != unit->xPos) || (other->yPos != unit->yPos))
            continue;

        if (AiGetUnitClosestValidPosition(unit, unit->xPos, unit->yPos, &pos))
        {
            unit->xPos = pos.x;
            unit->yPos = pos.y;
        }

        break;
    }

    RefreshEntityBmMaps();
    RefreshUnitSprites();
}


/*
 * On Game Control
 */

void EnableFreeMovementASMC(void)
{
    Fmu_SetStateBits(FMU_STATE_ENABLED, TRUE);
}

// While free movement runs, this ends the player phase too
void DisableFreeMovementASMC(void)
{
    struct FMUProc * proc = Fmu_GetProc();

    Fmu_SetStateBits(FMU_STATE_ENABLED | FMU_STATE_KEEP_UNIT | FMU_STATE_PAUSED, FALSE);

    if (proc != NULL)
        Fmu_End(proc);
}

void PauseFreeMovementASMC(void)
{
    struct FMUProc * proc = Fmu_GetProc();

    Fmu_SetStateBits(FMU_STATE_PAUSED, TRUE);

    if (proc == NULL)
        return;

    proc->recenterCamera = TRUE;

    if (Proc_Find(sProc_Menu))
    {
        EndAllMenus();
        ClearBg0Bg1();
    }
}

// Slot 1: character id
void ChangeFreeMovementUnitASMC(void)
{
    struct Unit * unit = GetUnitFromCharId(gEventSlots[EVT_SLOT_1]);
    struct FMUProc * proc = Fmu_GetProc();

    if (!Fmu_CanControlUnit(unit))
        return;

    if (proc != NULL)
    {
        Fmu_EndWalkerMus(proc, TRUE);
        Fmu_SetUnit(proc, unit);
        Fmu_RefreshFollowers(proc);
        proc->recenterCamera = TRUE;
        return;
    }

    gActionData.subjectIndex = unit->index;
    Fmu_SetStateBits(FMU_STATE_KEEP_UNIT, TRUE);
}

// Slot 1: 0 to 3
void SetFreeMovementFollowersASMC(void)
{
    struct FMUProc * proc = Fmu_GetProc();
    int count = gEventSlots[EVT_SLOT_1];

    if (count < 0)
        count = 0;

    if (count > FMU_MAX_FOLLOWERS)
        count = FMU_MAX_FOLLOWERS;

    *FreeMoveFlag = (*FreeMoveFlag & ~FMU_STATE_FOLLOWERS_MASK) |
        (count << FMU_STATE_FOLLOWERS_SHIFT);

    if (proc != NULL)
    {
        Fmu_EndWalkerMus(proc, TRUE);
        Fmu_RefreshFollowers(proc);
    }
}

void NewPlayerPhaseEvaluationFunc(struct Proc * ParentProc)
{
    if (!Fmu_TryStartPhase(ParentProc))
        Proc_Goto(Proc_StartBlocking(gProcScr_PlayerPhase, ParentProc), 7);

    Proc_Break(ParentProc);
}

void NewMakePhaseControllerFunc(struct Proc * ParentProc)
{
    if (!Fmu_TryStartPhase(ParentProc))
    {
        switch (gPlaySt.faction)
        {
        case FACTION_BLUE:
            Proc_StartBlocking(gProcScr_PlayerPhase, ParentProc);
            break;

        case FACTION_RED:
        case FACTION_GREEN:
            Proc_StartBlocking(gProcScr_CpPhase, ParentProc);
            break;
        }
    }

    Proc_Break(ParentProc);
}
