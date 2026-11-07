#include "FreeMU.h"

static bool Fmu_RunTrapList(struct FMUProc * proc, struct FMUTrapDef * trapDef, int x, int y)
{
    struct Trap * trap;

    if (!Fmu_IsInBounds(x, y))
        return FALSE;

    trap = GetTrapAt(x, y);

    if (trap == NULL)
        return FALSE;

    for (; trapDef->TrapID != 0; trapDef++)
    {
        if ((trapDef->TrapID != trap->type) || (trapDef->Func == NULL))
            continue;

        if ((trapDef->IsUsable != NULL) && !trapDef->IsUsable(proc, trap))
            return FALSE;

        trapDef->Func(proc, trap);
        return TRUE;
    }

    return FALSE;
}

// The trap under the unit, then the one it faces
static bool Fmu_RunTraps(struct FMUProc * proc, struct FMUTrapDef * onList, struct FMUTrapDef * facedList)
{
    int x, y;

    if (Fmu_RunTrapList(proc, onList, proc->xTo, proc->yTo))
        return TRUE;

    Fmu_GetFacedTile(proc, &x, &y);

    return Fmu_RunTrapList(proc, facedList, x, y);
}

// Vanilla traps run the way they do after a unit moves
bool Fmu_CanRunVanillaTrap(struct FMUProc * proc, struct Trap * trap)
{
    switch (GetPickTrapType(proc->unit))
    {
    case TRAP_8:
    case TRAP_MINE:
    case TRAP_FIRE_THIEF:
    case TRAP_MINE_ASSASSIN:
        return TRUE;

    default:
        return FALSE;
    }
}

void Fmu_RunVanillaTrap(struct FMUProc * proc, struct Trap * trap)
{
    ExecTrap((ProcPtr) proc, proc->unit, 0);
}

bool FMU_RunTrapASMC(FMUProc * proc)
{
    if (!Fmu_RunTraps(proc, HookListFMU_TrapList_OnPressA, HookListFMU_TrapList_OnPressAFaced))
        return FALSE;

    // clean up after its animation, as for traps stepped on
    proc->trapRan = TRUE;
    return TRUE;
}

// The chapter's misc events (Area, AFEV...) at the unit's tile
static bool Fmu_StartMiscEvents(void)
{
    struct EventInfo info;

    if (GetBattleMapKind() == BATTLEMAP_KIND_SKIRMISH)
        return FALSE;

    info.listScript = GetChapterEventDataPointer(gPlaySt.chapterIndex)->miscBasedEvents;
    info.xPos = gActiveUnit->xPos;
    info.yPos = gActiveUnit->yPos;

    if (!SearchAvailableEvent(&info))
        return FALSE;

    ClearActiveEventRegistry();

    do
    {
        StartEventFromInfo(&info, EV_EXEC_CUTSCENE);
    } while (SearchNextAvailableEvent(&info));

    return TRUE;
}

/*
 * Runs once for each tile the unit walks onto. The unit's position has to be
 * that tile already, since Area events and GetPickTrapType read it.
 * TRUE: stop walking here.
 */
bool Fmu_OnArrive(struct FMUProc * proc)
{
    struct Unit * unit = proc->unit;

    if ((unit->xPos == proc->xCur) && (unit->yPos == proc->yCur))
        return FALSE;

    proc->xTo = proc->xCur = unit->xPos;
    proc->yTo = proc->yCur = unit->yPos;

    gActiveUnit = unit;

    if (Fmu_RunTraps(proc, HookListFMU_TrapList_Auto, HookListFMU_TrapList_AutoFaced))
    {
        proc->yield = TRUE;
        proc->countdown = FMU_ACTION_FRAMES;
        proc->trapRan = TRUE;
        return TRUE;
    }

    if (!Fmu_StartMiscEvents())
        return FALSE;

    // The event only starts running next frame, so we can't tell yet whether
    // it shows anything. If it doesn't, walking goes on once it's over.
    proc->yield = TRUE;
    proc->yieldMove = TRUE;
    proc->countdown = FMU_STEP_EVENT_FRAMES;
    proc->stepEventShown = TRUE;
    return TRUE;
}

static bool Fmu_RunLocEvents(struct FMUProc * proc, struct LocEventDef * def, int x, int y)
{
    int command;

    if (!Fmu_IsInBounds(x, y))
        return FALSE;

    command = GetAvailableTileEventCommand(x, y);

    if (command == TILE_COMMAND_NONE)
        return FALSE;

    if ((command == TILE_COMMAND_SEIZE) && !CanUnitSeize(proc->unit))
        return FALSE;

    // TrapID is a terrain here, 0 for any
    for (; def->LocID != 0; def++)
    {
        if (def->LocID != command)
            continue;

        if ((def->TrapID != 0) && (def->TrapID != gBmMapTerrain[y][x]))
            continue;

        StartAvailableTileEvent(x, y);
        return TRUE;
    }

    return FALSE;
}

// Villages, chests, shops and thrones under the unit, doors in front of it
bool FMUmisc_RunMapEvents(struct FMUProc * proc)
{
    int x, y;

    if (Fmu_RunLocEvents(proc, HookListFMU_LocationBasedEvent, proc->xTo, proc->yTo))
        return TRUE;

    Fmu_GetFacedTile(proc, &x, &y);

    return Fmu_RunLocEvents(proc, HookListFMU_LocationBasedEventDoor, x, y);
}

// Talk to the unit in front
bool FMUmisc_RunTalkEvents(struct FMUProc * proc)
{
    int x, y, uid;
    u8 pidA, pidB;

    Fmu_GetFacedTile(proc, &x, &y);

    if (!Fmu_IsInBounds(x, y))
        return FALSE;

    uid = gBmMapUnit[y][x];

    if (uid == 0)
        return FALSE;

    pidA = proc->unit->pCharacterData->number;
    pidB = GetUnit(uid)->pCharacterData->number;

    if (!CheckForCharacterEvents(pidA, pidB))
        return FALSE;

    gActionData.targetIndex = uid;
    StartCharacterEvent(pidA, pidB);
    return TRUE;
}
