#include "FreeMU.h"

/*
 * The idle proc blocks BMapMain in place of the player phase, and the main
 * proc runs in tree 3. Tree 3 keeps going while the game is locked, so short
 * events don't stop the unit; the main loop waits on longer ones itself.
 */

static void Fmu_Idle_OnEnd(ProcPtr proc);
static void Fmu_OnEnd(struct FMUProc * proc);
static bool Fmu_Init(struct FMUProc * proc);
static void Fmu_MainLoop(struct FMUProc * proc);
static void Fmu_EndCleanup(struct FMUProc * proc);

static const struct ProcCmd sProcScr_FmuIdle[] =
{
    PROC_NAME("E_FREEMOVE_IDLE"),
    PROC_SET_END_CB(Fmu_Idle_OnEnd),
    PROC_BLOCK,
};

const struct ProcCmd FreeMovementControlProc[] =
{
    PROC_NAME("E_FREEMOVE"),
    PROC_SET_END_CB(Fmu_OnEnd),
    PROC_MARK(PROC_MARK_MAIN),
    PROC_YIELD,

    PROC_CALL_2(Fmu_Init),
    PROC_CALL(EndPlayerPhaseSideWindows),
    PROC_YIELD,

    PROC_CALL(RenderBmMap),
    PROC_YIELD,

    PROC_CALL(EndAllMus),
    PROC_YIELD,

    PROC_CALL(RefreshUnitSprites),
    PROC_YIELD,

    PROC_CALL(StartMapSongBgm),
    PROC_CALL(ResetUnitSpriteHover),

PROC_LABEL(L_FMU_MAIN),
    PROC_REPEAT(Fmu_MainLoop),

PROC_LABEL(L_FMU_END),
    PROC_WHILE(MuCtrExists),
    PROC_CALL(Fmu_EndCleanup),
    PROC_END,
};

struct FMUProc * Fmu_GetProc(void)
{
    return Proc_Find(FreeMovementControlProc);
}

// The MU is a walker's, and walks a step we started
static bool Fmu_IsWalkerStepping(struct MuProc * mu)
{
    struct FMUProc * proc;
    struct FmuFollower * f;

    if (!Fmu_IsEnabled())
        return FALSE;

    proc = Fmu_GetProc();

    if ((proc == NULL) || (mu->unit == NULL))
        return FALSE;

    if (mu->unit == proc->unit)
        return proc->stepping;

    f = Fmu_GetFollower(proc, mu->unit);

    return (f != NULL) && f->stepping;
}

bool Fmu_IsActiveForMu(struct MuProc * mu)
{
    if (*FreeMoveFlag & FMU_STATE_PAUSED)
        return FALSE;

    return Fmu_IsWalkerStepping(mu);
}

/*
 * A walker's position is the tile it steps onto, from the start of the
 * step. The tile it leaves is free at once, so two walkers never share or
 * swap tiles.
 */

struct MuProc * Fmu_FindUnitMu(struct Unit * unit)
{
    int i;

    if (unit == NULL)
        return NULL;

    for (i = 0; i < MU_MAX_COUNT; i++)
    {
        struct MuProc * mu = GetMu(i);

        if ((mu != NULL) && (mu->unit == unit))
            return mu;
    }

    return NULL;
}

bool Fmu_IsShownAsMu(struct Unit * unit)
{
    struct FMUProc * proc;

    if (Fmu_FindUnitMu(unit) == NULL)
        return FALSE;

    proc = Fmu_GetProc();

    return (proc != NULL) && Fmu_IsWalker(proc, unit);
}

struct MuProc * Fmu_StartWalkerMu(struct Unit * unit)
{
    struct MuProc * mu;

    if (!CanStartMu())
        return NULL;

    mu = StartMu(unit);

    if (mu != NULL)
        HideUnitSprite(unit);

    return mu;
}

// Back to map sprites. Walkers mid-step keep their MUs unless includeStepping
void Fmu_EndWalkerMus(struct FMUProc * proc, bool includeStepping)
{
    struct MuProc * mu;
    bool ended = FALSE;
    int i;

    if (includeStepping || !proc->stepping)
    {
        if ((mu = Fmu_FindUnitMu(proc->unit)) != NULL)
        {
            EndMu(mu);
            ended = TRUE;
        }

        proc->stepping = FALSE;
    }

    for (i = 0; i < proc->followerCount; i++)
    {
        struct FmuFollower * f = &proc->followers[i];

        f->wait = 0;

        if (f->stepping && !includeStepping)
            continue;

        if ((mu = Fmu_FindUnitMu(GetUnit(f->uid))) != NULL)
        {
            EndMu(mu);
            ended = TRUE;
        }

        f->stepping = FALSE;
    }

    if (ended)
        RefreshUnitSprites();

    proc->regroup = TRUE;
}

// Puts whoever else stands on (x, y) back on the unit map
static void Fmu_RewriteMapTile(int x, int y, struct Unit * leaving)
{
    int uid = 0;
    int i;

    for (i = 1; i < UNIT_ID_END; i++)
    {
        struct Unit * unit = GetUnit(i);

        if ((unit == leaving) || !UNIT_IS_VALID(unit))
            continue;

        // same as RefreshUnitsOnBmMap
        if (unit->state & (US_HIDDEN | US_BIT9))
            continue;

        if ((unit->xPos == x) && (unit->yPos == y))
        {
            uid = i;
            break;
        }
    }

    gBmMapUnit[y][x] = uid;
}

void Fmu_MoveWalker(struct Unit * unit, int x, int y)
{
    int xFrom = unit->xPos;
    int yFrom = unit->yPos;

    unit->xPos = x;
    unit->yPos = y;
    UnitFinalizeMovement(unit);

    Fmu_RewriteMapTile(xFrom, yFrom, unit);

    if (gBmMapUnit[y][x] == 0)
        gBmMapUnit[y][x] = unit->index;
}

// One step, then halt (RunMuMoveScript can extend it)
void Fmu_SetMuStep(struct MuProc * mu, int facing, bool stepSound)
{
    u8 script[MOVE_CMD_MAX_COUNT];
    int i;

    script[0] = MOVE_CMD_MOVE_BASE + facing;

    for (i = 1; i < MOVE_CMD_MAX_COUNT; i++)
        script[i] = MOVE_CMD_HALT;

    if (stepSound)
    {
        SetMuMoveScript(mu, script);
        return;
    }

    // SetMuMoveScript without the step sound
    for (i = 0; i < MOVE_CMD_MAX_COUNT; i++)
        mu->config->movescr[i] = script[i];

    mu->config->pc = 0;
    mu->state = MU_STATE_MOVEMENT;
}

static int Fmu_KeysToFacing(u16 keys)
{
    if (keys & DPAD_RIGHT)
        return FACING_RIGHT;

    if (keys & DPAD_LEFT)
        return FACING_LEFT;

    if (keys & DPAD_UP)
        return FACING_UP;

    if (keys & DPAD_DOWN)
        return FACING_DOWN;

    return -1;
}

// With several directions held, the last one pressed wins
static u16 Fmu_FilterDirKeys(struct FMUProc * proc, u16 keys)
{
    u16 pressed = gKeyStatusPtr->newKeys & DPAD_ANY;

    keys &= DPAD_ANY;

    if (pressed)
        proc->lastDirKey = pressed;

    if (proc->lastDirKey & keys)
        return proc->lastDirKey & keys;

    return keys;
}

static void Fmu_StartStep(struct FMUProc * proc, int x, int y)
{
    struct MuProc * mu = Fmu_FindUnitMu(proc->unit);

    // no MU free this frame, try again next frame
    if ((mu == NULL) && ((mu = Fmu_StartWalkerMu(proc->unit)) == NULL))
        return;

    EnableMuCamera(mu);
    Fmu_SetMuStep(mu, proc->facing, TRUE);
    Fmu_MoveWalker(proc->unit, x, y);

    proc->stepping = TRUE;
    proc->idleFrames = 0;
    proc->moveDir = proc->facing;
    proc->xTo = x;
    proc->yTo = y;

    gActionData.xMove = x;
    gActionData.yMove = y;

    Fmu_ScheduleFollowers(proc);
}

static void Fmu_UpdatePlayerMu(struct FMUProc * proc)
{
    struct MuProc * mu = Fmu_FindUnitMu(proc->unit);

    if (proc->stepping)
    {
        if ((mu != NULL) && (mu->state != MU_STATE_INACTIVE))
            return;

        // something else ended or halted its MU
        proc->stepping = FALSE;
        proc->walkEnded = TRUE;
    }

    // as MuCtr_OnEnd does after a move
    if (proc->walkEnded)
    {
        proc->walkEnded = FALSE;
        RefreshEntityBmMaps();
        RefreshUnitSprites();
    }

#if FMU_IDLE_MU
    if ((mu == NULL) && ((mu = Fmu_StartWalkerMu(proc->unit)) != NULL))
        SetMuFacing(mu, proc->facing);
#else
    if ((mu != NULL) && (++proc->idleFrames > FMU_MU_IDLE_FRAMES))
    {
        EndMu(mu);
        RefreshUnitSprites();
    }
#endif
}

static void Fmu_TryStep(struct FMUProc * proc, u16 keys)
{
    int facing = Fmu_KeysToFacing(keys);
    int x, y;

    if (facing < 0)
        return;

    if (facing != proc->facing)
    {
        // turn first, keep holding to walk
        Fmu_SetFacing(proc, facing);
        proc->queuedDirKey = 0;
        proc->countdown = FMU_TURN_FRAMES;
        return;
    }

    x = proc->xTo + gFmuFacingOffsets[facing][0];
    y = proc->yTo + gFmuFacingOffsets[facing][1];

    if (FMU_CanUnitBeOnPos(proc->unit, x, y))
        Fmu_StartStep(proc, x, y);
}

// After anything that may have moved or killed the walking unit
bool Fmu_EnsureUnit(struct FMUProc * proc)
{
    struct Unit * unit = proc->unit;

    if (!Fmu_CanControlUnit(unit))
    {
        unit = Fmu_FindNextUnit(unit);

        if (unit == NULL)
        {
            Fmu_HandOffToPlayerPhase(proc);
            return FALSE;
        }

        Fmu_EndWalkerMus(proc, TRUE);
        proc->recenterCamera = TRUE;
    }

    Fmu_SetUnit(proc, unit);

    // replace followers that died or left
    if (!Fmu_AnyFollowerStepping(proc))
        Fmu_RefreshFollowers(proc);

    return TRUE;
}

/*
 * Each proc ends the other. Proc_End runs the end callback before the proc
 * counts as ended, hence `ending`.
 */
static void Fmu_Idle_OnEnd(ProcPtr proc)
{
    struct FMUProc * fmu = Fmu_GetProc();

    if ((fmu != NULL) && !fmu->ending)
    {
        fmu->ending = TRUE;
        Proc_End(fmu);
    }
}

static void Fmu_OnEnd(struct FMUProc * proc)
{
    struct MuProc * mu;
    int i;

    EndAllMenus();

    // no RefreshUnitSprites, the map may be going away
    if ((mu = Fmu_FindUnitMu(proc->unit)) != NULL)
        EndMu(mu);

    for (i = 0; i < proc->followerCount; i++)
    {
        if ((mu = Fmu_FindUnitMu(GetUnit(proc->followers[i].uid))) != NULL)
            EndMu(mu);

        proc->followers[i].stepping = FALSE;
    }

    if (!proc->ending)
    {
        proc->ending = TRUE;
        Proc_EndEach(sProcScr_FmuIdle);
    }
}

void Fmu_End(struct FMUProc * proc)
{
    Proc_Goto(proc, L_FMU_END);
}

// Ending the idle proc takes the main proc with it
void Fmu_EndForChapterChange(void)
{
    Proc_EndEach(sProcScr_FmuIdle);
}

static void Fmu_EndCleanup(struct FMUProc * proc)
{
    Fmu_EndWalkerMus(proc, TRUE);
    Fmu_SeparateUnit(proc->unit);
}

// Plays the rest of this phase as the normal player phase
void Fmu_HandOffToPlayerPhase(struct FMUProc * proc)
{
    ProcPtr mapMain = Proc_Find(gProc_BMapMain);

    Fmu_EndWalkerMus(proc, TRUE);

    if (Fmu_CanControlUnit(proc->unit))
    {
        Fmu_SeparateUnit(proc->unit);
        SetCursorMapPosition(proc->unit->xPos, proc->unit->yPos);
    }

    // start it before the idle proc ends, so BMapMain stays on this turn
    if (mapMain != NULL)
        Proc_StartBlocking(gProcScr_PlayerPhase, mapMain);

    Fmu_End(proc);
}

static bool Fmu_Init(struct FMUProc * proc)
{
    struct Unit * unit = NULL;
    int i;

    if (*FreeMoveFlag & FMU_STATE_KEEP_UNIT)
    {
        if ((gActionData.subjectIndex > FACTION_BLUE) && (gActionData.subjectIndex < FACTION_GREEN))
            unit = GetUnit(gActionData.subjectIndex);
    }

    if (!Fmu_CanControlUnit(unit))
        unit = Fmu_FindNextUnit(NULL);

    // nobody to walk with: play this phase normally
    if (unit == NULL)
    {
        Fmu_HandOffToPlayerPhase(proc);
        return FALSE;
    }

    // so they can all act if we hand off to the player phase
    for (i = FACTION_BLUE + 1; i < FACTION_GREEN; i++)
    {
        struct Unit * it = GetUnit(i);

        if (UNIT_IS_VALID(it))
            it->state &= ~(US_UNSELECTABLE | US_HAS_MOVED);
    }

    Fmu_ResetDisplayEffects();
    EndAllMus();
    RefreshEntityBmMaps();

    proc->stepping = FALSE;
    Fmu_SetUnit(proc, unit);
    Fmu_SetFacing(proc, (*FreeMoveFlag & FMU_STATE_FACING_MASK) >> FMU_STATE_FACING_SHIFT);
    proc->moveDir = proc->facing;
    proc->idleFrames = 0;
    proc->walkEnded = FALSE;
    proc->regroup = FALSE;

    proc->followerCount = 0;
    Fmu_RefreshFollowers(proc);

    proc->moveSpeed = (*FreeMoveFlag & FMU_STATE_RUNNING) ? FreeMU_RunningSpeed : FreeMU_MovingSpeed;
    proc->moveStartTime = GetGameClock();

    proc->countdown = 2;
    proc->yield = TRUE;
    proc->yieldMove = TRUE;

    proc->eventFrames = 0;
    proc->event = NULL;
    proc->trapRan = FALSE;
    proc->recenterCamera = FALSE;
    proc->restoreMap = FALSE;
    proc->stepEventShown = FALSE;

    proc->pendingKeys = 0;
    proc->lastDirKey = 0;
    proc->queuedDirKey = 0;

    Fmu_CenterCamera(proc, unit->xPos, unit->yPos);

    return TRUE;
}

static void Fmu_MainLoop(struct FMUProc * proc)
{
    struct EventEngineProc * event = Proc_Find(ProcScr_StdEventEngine);
    bool waiting = FALSE;
    bool otherScreen = FALSE;
    u16 held;

    // a menu, shop, stat screen or similar is up
    if ((GetGameLock() != 0) && (event == NULL))
    {
        waiting = TRUE;

        // menus and target selection still show the map
        otherScreen = !Proc_Find(sProc_Menu) && !Proc_Find(gProcScr_TargetSelection);
    }

    if ((event != NULL) && (*FreeMoveFlag & FMU_STATE_PAUSED))
        waiting = TRUE;

    if (waiting)
    {
        proc->yield = TRUE;
        proc->countdown = 1;
    }

    if ((event != NULL) && (event->evStallTimer || event->pUnitLoadData || event->activeTextType))
    {
        proc->yield = TRUE;
        waiting = TRUE;
    }

    if (event != NULL)
    {
        if (event->pEventIdk == proc->event)
        {
            if (proc->eventFrames < 0xFF)
                proc->eventFrames++;
        }
        else
        {
            proc->event = event->pEventIdk;
            proc->eventFrames = 0;
        }
    }
    else
    {
        proc->event = NULL;
        proc->eventFrames = 0;
    }

    // events and screens get plain map sprites once steps are over
    if ((event != NULL) || otherScreen)
        Fmu_EndWalkerMus(proc, FALSE);

    // drop presses made before something else took over
    if (waiting)
    {
        proc->pendingKeys = 0;
        return;
    }

    if (proc->eventFrames >= FMU_EVENT_MIN_FRAMES)
    {
        proc->recenterCamera = TRUE;
        proc->pendingKeys = 0;
        return;
    }

    if (*FreeMoveFlag & FMU_STATE_PAUSED)
        Fmu_SetStateBits(FMU_STATE_PAUSED, FALSE);

    if (proc->trapRan)
    {
        // the trap animation is over, tidy up like PlayerPhase_FinishAction
        EndAllMus();
        RefreshEntityBmMaps();
        RenderBmMap();
        RefreshUnitSprites();

        proc->trapRan = FALSE;
        proc->regroup = TRUE;
    }

    if (proc->regroup && (event == NULL))
    {
        proc->regroup = FALSE;
        RefreshEntityBmMaps();
        Fmu_ScheduleFollowers(proc);
    }

    if ((event == NULL) && !proc->stepping && !Fmu_EnsureUnit(proc))
        return;

    if (proc->recenterCamera)
    {
        Fmu_CenterCamera(proc, proc->unit->xPos, proc->unit->yPos);
        proc->recenterCamera = FALSE;
        return;
    }

    if (proc->restoreMap && (proc->countdown == 0))
    {
        // what the player phase does after its stat screen
        RefreshBMapGraphics();
        SetDefaultColorEffects();

        gLCDControlBuffer.dispcnt.bg0_on = TRUE;
        gLCDControlBuffer.dispcnt.bg1_on = TRUE;
        gLCDControlBuffer.dispcnt.bg2_on = TRUE;
        gLCDControlBuffer.dispcnt.bg3_on = TRUE;
        gLCDControlBuffer.dispcnt.obj_on = TRUE;
        Fmu_ResetDisplayEffects();

        proc->restoreMap = FALSE;
        proc->recenterCamera = TRUE;
    }

    if (proc->countdown == 0)
        proc->yield = FALSE;
    else
        proc->countdown--;

    if (proc->stepEventShown)
    {
        EndAllMenus();
        proc->stepEventShown = FALSE;
    }

    if (event == NULL)
    {
        Fmu_UpdatePlayerMu(proc);
        Fmu_UpdateFollowers(proc);
    }

    if (gKeyStatusPtr->newKeys & DPAD_ANY)
        proc->queuedDirKey = gKeyStatusPtr->newKeys & DPAD_ANY;

    if (!proc->yield)
    {
        if (gKeyStatusPtr->newKeys & FMU_ACTION_KEYS)
            proc->pendingKeys = gKeyStatusPtr->newKeys & FMU_ACTION_KEYS;

        if (gKeyStatusPtr->newKeys & B_BUTTON)
            Fmu_RunButtonList(proc, FMU_FunctionList_OnPressB);

        // other buttons wait until every walker stands still
        if ((proc->pendingKeys != 0) && !proc->stepping && !Fmu_AnyFollowerStepping(proc))
        {
            u16 keys = proc->pendingKeys;

            proc->pendingKeys = 0;

            if (Fmu_HandleButtons(proc, keys))
            {
                proc->yieldMove = TRUE;
                proc->countdown = FMU_ACTION_FRAMES;
                proc->yield = TRUE;
                proc->moveStartTime = GetGameClock();
                return;
            }
        }
    }

    if (proc->stepping)
        return;

    if (proc->yieldMove && (proc->countdown == 0))
        proc->yieldMove = FALSE;

    held = gKeyStatusPtr->heldKeys;

    if ((proc->countdown == 0) && !proc->yield && !proc->yieldMove && (held & DPAD_ANY))
    {
        proc->queuedDirKey = 0;
        Fmu_TryStep(proc, Fmu_FilterDirKeys(proc, held));
        proc->yieldMove = TRUE;
    }

    if (!(held & DPAD_ANY))
        proc->moveStartTime = GetGameClock();
}

bool Fmu_TryStartPhase(ProcPtr mapMain)
{
    if (!Fmu_StartsThisPhase())
        return FALSE;

    Proc_StartBlocking(sProcScr_FmuIdle, mapMain);
    Proc_Start(FreeMovementControlProc, PROC_TREE_3);

    return TRUE;
}

// The walking unit's step is over: walk on, or stop here
static int Fmu_ContinuePlayer(struct FMUProc * proc, struct MuProc * mu)
{
    struct Unit * unit = proc->unit;
    int facing = -1;
    bool stop;
    u16 keys;
    int x, y;

    stop = Fmu_OnArrive(proc);

    if (!stop)
    {
        stop = proc->yield || proc->countdown || (proc->pendingKeys != 0) ||
            (proc->eventFrames >= FMU_EVENT_MIN_FRAMES) || (*FreeMoveFlag & FMU_STATE_PAUSED);
    }

    if (!stop)
    {
        keys = gKeyStatusPtr->heldKeys & DPAD_ANY;

        if (gKeyStatusPtr->newKeys & DPAD_ANY)
            keys = gKeyStatusPtr->newKeys & DPAD_ANY;

        if (keys == 0)
            keys = proc->queuedDirKey;

        proc->queuedDirKey = 0;

        facing = Fmu_KeysToFacing(Fmu_FilterDirKeys(proc, keys));

        // turning stops here, the main loop turns and walks on
        stop = (facing < 0) || (facing != proc->facing);
    }

    if (!stop)
    {
        x = unit->xPos + gFmuFacingOffsets[facing][0];
        y = unit->yPos + gFmuFacingOffsets[facing][1];

        stop = !FMU_CanUnitBeOnPos(unit, x, y);
    }

    if (stop)
    {
        proc->stepping = FALSE;
        proc->walkEnded = TRUE;
        proc->idleFrames = 0;

        if ((facing >= 0) && (facing != proc->facing))
            Fmu_SetFacing(proc, facing);

        return -1;
    }

    mu->config->pc = 1;
    mu->config->movescr[0] = MOVE_CMD_MOVE_BASE + facing;

    Fmu_MoveWalker(unit, x, y);

    proc->moveDir = facing;
    proc->xTo = x;
    proc->yTo = y;

    gActionData.xMove = x;
    gActionData.yMove = y;

    Fmu_ScheduleFollowers(proc);

    return MOVE_CMD_MOVE_BASE + facing;
}

// RunMuMoveScript, at the command after a walker's step: the next move command, or -1
int Fmu_GetContinuedMoveCommand(struct MuProc * mu)
{
    struct FMUProc * proc;

    if (!Fmu_IsWalkerStepping(mu))
        return -1;

    // pc 1 is the step's own move command
    if (mu->config->pc == 1)
        return -1;

    proc = Fmu_GetProc();

    if (mu->unit == proc->unit)
        return Fmu_ContinuePlayer(proc, mu);

    return Fmu_ContinueFollower(proc, Fmu_GetFollower(proc, mu->unit), mu);
}

u16 Fmu_GetMuQ4MovementSpeed(struct MuProc * mu)
{
    struct FMUProc * proc = Fmu_GetProc();
    int speed = proc->moveSpeed;

    // followers catch up at full speed
    if ((mu->unit != proc->unit) && !proc->stepping)
        return speed;

    if ((speed > FreeMU_MovingSpeed) && ((GetGameClock() - proc->moveStartTime) < FMU_RUN_RAMP_FRAMES))
        speed /= 2;

    return speed;
}

void Fmu_UpdateMuCamera(struct MuProc * mu)
{
    struct FMUProc * proc = Fmu_GetProc();

    if (mu->unit != proc->unit)
        return;

    gBmSt.camera.x = Fmu_GetCameraTargetX(mu->x_q4 >> MU_SUBPIXEL_PRECISION);
    gBmSt.camera.y = Fmu_GetCameraTargetY(mu->y_q4 >> MU_SUBPIXEL_PRECISION);
}
