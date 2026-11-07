#include "FreeMU.h"

/*
 * Up to 3 player units follow the walking unit in a loose formation. After
 * each of its steps they get places behind it, then walk there one tile at a
 * time. They never trigger events or traps.
 */

enum
{
    FMU_FOLLOWER_BLOCKED = -1,
    FMU_FOLLOWER_AT_TARGET = -2,
    FMU_FOLLOWER_WAITING = -3, // another walker is in the way
};

struct FmuFormationSlot
{
    s8 x, y;
    u8 rank;
};

// Places behind the walking unit: tiles back, tiles to the side, rank (best first)
static const s8 sFmuFormationSlots[][3] = {
    { 1, 0, 0 }, { 1, 1, 1 }, { 1, -1, 1 },
    { 2, 0, 2 }, { 2, 1, 3 }, { 2, -1, 3 },
    { 0, 1, 4 }, { 0, -1, 4 },
    { 3, 0, 4 }, { 3, 1, 5 }, { 3, -1, 5 },
    { 4, 0, 6 },
};

static int Fmu_Abs(int value)
{
    return (value < 0) ? -value : value;
}

struct FmuFollower * Fmu_GetFollower(struct FMUProc * proc, struct Unit * unit)
{
    int i;

    for (i = 0; i < proc->followerCount; i++)
    {
        if (GetUnit(proc->followers[i].uid) == unit)
            return &proc->followers[i];
    }

    return NULL;
}

bool Fmu_IsWalker(struct FMUProc * proc, struct Unit * unit)
{
    return (unit == proc->unit) || (Fmu_GetFollower(proc, unit) != NULL);
}

bool Fmu_AnyFollowerStepping(struct FMUProc * proc)
{
    int i;

    for (i = 0; i < proc->followerCount; i++)
    {
        if (proc->followers[i].stepping)
            return TRUE;
    }

    return FALSE;
}

// Not the game's RNG, so battles aren't affected
static int Fmu_RandomFollowerDelay(int salt)
{
    u32 hash = (GetGameClock() + salt * 0x9E3779B9u) * 0x2C1B3C6Du;

    return FMU_FOLLOWER_DELAY_MIN +
        (((hash >> 24) * (FMU_FOLLOWER_DELAY_MAX - FMU_FOLLOWER_DELAY_MIN + 1)) >> 8);
}

// Something else is about to use the map: finish the current step, then wait
static bool Fmu_FollowersHold(struct FMUProc * proc)
{
    if ((GetGameLock() != 0) || EventEngineExists())
        return TRUE;

    if ((proc->pendingKeys != 0) || proc->trapRan || proc->ending)
        return TRUE;

    return (*FreeMoveFlag & FMU_STATE_PAUSED) != 0;
}

static int Fmu_FormationCost(struct FmuFollower * f, const struct FmuFormationSlot * slot)
{
    struct Unit * unit = GetUnit(f->uid);
    int cost = 2 * (Fmu_Abs(unit->xPos - slot->x) + Fmu_Abs(unit->yPos - slot->y)) + slot->rank;

    // keep its place when another is as good
    if ((slot->x != f->xTarget) || (slot->y != f->yTarget))
        cost += FMU_FORMATION_CHANGE_COST;

    return cost;
}

// Tries every assignment (3 followers over 5 places at most)
static void Fmu_AssignSlots(
    struct FMUProc * proc, const struct FmuFormationSlot * slots, int slotCount, int depth,
    int usedSlots, int cost, s8 * pick, s8 * bestPick, int * bestCost)
{
    int i;

    if (cost >= *bestCost)
        return;

    if (depth == proc->followerCount)
    {
        *bestCost = cost;

        for (i = 0; i < proc->followerCount; i++)
            bestPick[i] = pick[i];

        return;
    }

    for (i = 0; i < slotCount; i++)
    {
        if (usedSlots & (1 << i))
            continue;

        pick[depth] = i;
        Fmu_AssignSlots(proc, slots, slotCount, depth + 1, usedSlots | (1 << i),
            cost + Fmu_FormationCost(&proc->followers[depth], &slots[i]), pick, bestPick, bestCost);
    }

    // not enough places: stay put
    pick[depth] = -1;
    Fmu_AssignSlots(proc, slots, slotCount, depth + 1, usedSlots, cost + FMU_FORMATION_NO_SLOT_COST,
        pick, bestPick, bestCost);
}

static void Fmu_UpdateFormation(struct FMUProc * proc)
{
    s8 costs[TERRAIN_COUNT];
    struct FmuFormationSlot slots[FMU_MAX_FOLLOWERS + 2];
    s8 pick[FMU_MAX_FOLLOWERS];
    s8 bestPick[FMU_MAX_FOLLOWERS];
    int bestCost = 0x7FFF;
    int px = proc->unit->xPos;
    int py = proc->unit->yPos;
    int fx = gFmuFacingOffsets[proc->moveDir][0];
    int fy = gFmuFacingOffsets[proc->moveDir][1];
    int slotCount = 0;
    unsigned i;
    int t;

    if (proc->followerCount == 0)
        return;

    // steps from the walking unit, over terrain all followers can cross
    for (t = 0; t < TERRAIN_COUNT; t++)
        costs[t] = 1;

    for (i = 0; i < proc->followerCount; i++)
    {
        const s8 * own = GetUnitMovementCost(GetUnit(proc->followers[i].uid));

        for (t = 0; t < TERRAIN_COUNT; t++)
        {
            if (own[t] <= 0)
                costs[t] = -1;
        }
    }

    SetWorkingMoveCosts(costs);
    SetWorkingBmMap(gBmMapMovement);
    GenerateMovementMap(px, py, FMU_FORMATION_FLOOD_STEPS, 0);

    for (i = 0; (i < ARRAY_COUNT(sFmuFormationSlots)) && (slotCount < proc->followerCount + 2); i++)
    {
        int back = sFmuFormationSlots[i][0];
        int side = sFmuFormationSlots[i][1];
        int x = px - fx * back - fy * side;
        int y = py - fy * back + fx * side;
        int uid;

        if (!Fmu_IsInBounds(x, y) || (costs[gBmMapTerrain[y][x]] <= 0))
            continue;

        // not round a wall's corner
        if (gBmMapMovement[y][x] > back + Fmu_Abs(side) + 2)
            continue;

        uid = gBmMapUnit[y][x];

        if ((uid != 0) && !Fmu_IsWalker(proc, GetUnit(uid)))
            continue;

        if (gBmMapHidden[y][x] & HIDDEN_BIT_UNIT)
            continue;

        slots[slotCount].x = x;
        slots[slotCount].y = y;
        slots[slotCount].rank = sFmuFormationSlots[i][2];
        slotCount++;
    }

    Fmu_AssignSlots(proc, slots, slotCount, 0, 0, 0, pick, bestPick, &bestCost);

    for (i = 0; i < proc->followerCount; i++)
    {
        struct FmuFollower * f = &proc->followers[i];

        if (bestPick[i] >= 0)
        {
            f->xTarget = slots[bestPick[i]].x;
            f->yTarget = slots[bestPick[i]].y;
        }
        else
        {
            f->xTarget = GetUnit(f->uid)->xPos;
            f->yTarget = GetUnit(f->uid)->yPos;
        }
    }
}

static bool Fmu_IsWalkerAt(struct FMUProc * proc, int x, int y)
{
    int i;

    if ((proc->unit->xPos == x) && (proc->unit->yPos == y))
        return TRUE;

    for (i = 0; i < proc->followerCount; i++)
    {
        struct Unit * unit = GetUnit(proc->followers[i].uid);

        if ((unit->xPos == x) && (unit->yPos == y))
            return TRUE;
    }

    return FALSE;
}

static bool Fmu_FollowerCanCross(struct Unit * unit, int x, int y)
{
    if (!Fmu_IsInBounds(x, y))
        return FALSE;

    if (GetUnitMovementCost(unit)[gBmMapTerrain[y][x]] <= 0)
        return FALSE;

    return !(gBmMapHidden[y][x] & HIDDEN_BIT_UNIT);
}

// A FACING_*, or FMU_FOLLOWER_BLOCKED/_AT_TARGET/_WAITING
static int Fmu_FollowerChooseStep(struct FMUProc * proc, struct FmuFollower * f)
{
    s8 costs[TERRAIN_COUNT];
    struct Unit * unit = GetUnit(f->uid);
    const s8 * own = GetUnitMovementCost(unit);
    int x = unit->xPos;
    int y = unit->yPos;
    int best = FMU_FOLLOWER_BLOCKED;
    int bestScore = 0x7FFF;
    int blockerScore = 0x7FFF;
    struct FmuFollower * blocker = NULL;
    bool waiting = FALSE;
    int cur, dir, t;

    Fmu_UpdateFormation(proc);

    if ((x == f->xTarget) && (y == f->yTarget))
        return FMU_FOLLOWER_AT_TARGET;

    // steps to its place, over its own terrain
    for (t = 0; t < TERRAIN_COUNT; t++)
        costs[t] = (own[t] > 0) ? 1 : -1;

    SetWorkingMoveCosts(costs);
    SetWorkingBmMap(gBmMapRange);
    GenerateMovementMap(f->xTarget, f->yTarget, FMU_FOLLOWER_FLOOD_STEPS, 0);

    cur = gBmMapRange[y][x];

    for (dir = 0; dir < 4; dir++)
    {
        int nx = x + gFmuFacingOffsets[dir][0];
        int ny = y + gFmuFacingOffsets[dir][1];
        int score;

        if (!Fmu_FollowerCanCross(unit, nx, ny))
            continue;

        if (cur <= FMU_FOLLOWER_FLOOD_STEPS)
        {
            int d = gBmMapRange[ny][nx];

            if (d < cur)
                score = d * 4;
            // stuck behind someone a while: go round, but never straight back
            else if ((d == cur) && (f->blocked >= FMU_FOLLOWER_SIDESTEP_TRIES) && (dir != (f->lastDir ^ 1)))
                score = d * 4 + 8;
            else
                continue;
        }
        else
        {
            // too far for the flood, head straight there
            int d = Fmu_Abs(f->xTarget - nx) + Fmu_Abs(f->yTarget - ny);

            if (d >= Fmu_Abs(f->xTarget - x) + Fmu_Abs(f->yTarget - y))
                continue;

            score = d * 4;
        }

        if (dir != f->lastDir)
            score++;

        if (score >= bestScore)
            continue;

        if ((gBmMapUnit[ny][nx] != 0) || Fmu_IsWalkerAt(proc, nx, ny))
        {
            struct FmuFollower * other = (gBmMapUnit[ny][nx] != 0) ? Fmu_GetFollower(proc, GetUnit(gBmMapUnit[ny][nx])) : NULL;

            if (Fmu_IsWalkerAt(proc, nx, ny))
                waiting = TRUE;

            // a follower standing on its own place
            if ((other != NULL) && !other->stepping && (score < blockerScore))
            {
                struct Unit * otherUnit = GetUnit(other->uid);

                if ((otherUnit->xPos == other->xTarget) && (otherUnit->yPos == other->yTarget))
                {
                    blocker = other;
                    blockerScore = score;
                }
            }

            continue;
        }

        best = dir;
        bestScore = score;
    }

    if (best != FMU_FOLLOWER_BLOCKED)
        return best;

    // swap places with it
    if (blocker != NULL)
    {
        s8 xTarget = f->xTarget;
        s8 yTarget = f->yTarget;

        f->xTarget = blocker->xTarget;
        f->yTarget = blocker->yTarget;
        blocker->xTarget = xTarget;
        blocker->yTarget = yTarget;

        if ((blocker->wait == 0) || (blocker->wait > FMU_FOLLOWER_QUICK_RETRY))
            blocker->wait = FMU_FOLLOWER_QUICK_RETRY;

        return FMU_FOLLOWER_WAITING;
    }

    return waiting ? FMU_FOLLOWER_WAITING : FMU_FOLLOWER_BLOCKED;
}

// Frames until a blocked follower tries again, 0: not until the walking unit moves
static int Fmu_FollowerRetryWait(struct FMUProc * proc, struct FmuFollower * f, int result, int salt)
{
    struct Unit * unit = GetUnit(f->uid);
    int near = Fmu_Abs(unit->xPos - proc->unit->xPos) + Fmu_Abs(unit->yPos - proc->unit->yPos);
    int retry;

    if (f->blocked < 0xFF)
        f->blocked++;

    if ((f->blocked > FMU_FOLLOWER_GIVE_UP_TRIES) && (near <= FMU_FOLLOWER_GIVE_UP_STEPS))
        return 0;

    if ((result == FMU_FOLLOWER_WAITING) && (f->blocked <= FMU_FOLLOWER_QUICK_RETRIES))
        return FMU_FOLLOWER_QUICK_RETRY;

    retry = 2 * f->blocked;

    if (retry > FMU_FOLLOWER_RETRY_MAX)
        retry = FMU_FOLLOWER_RETRY_MAX;

    return Fmu_RandomFollowerDelay(salt) + retry;
}

static bool Fmu_FollowerStartStep(struct FmuFollower * f, int dir)
{
    struct Unit * unit = GetUnit(f->uid);
    struct MuProc * mu = Fmu_FindUnitMu(unit);

    if ((mu == NULL) && ((mu = Fmu_StartWalkerMu(unit)) == NULL))
        return FALSE;

    // the walking unit's step sounds and camera are enough
    DisableMuCamera(mu);
    mu->moveConfig = MU_CONFIG_SPPED_NORMAL;

    Fmu_SetMuStep(mu, dir, FALSE);
    Fmu_MoveWalker(unit, unit->xPos + gFmuFacingOffsets[dir][0], unit->yPos + gFmuFacingOffsets[dir][1]);

    f->stepping = TRUE;
    f->lastDir = dir;
    f->idleFrames = 0;
    f->blocked = 0;

    return TRUE;
}

static void Fmu_FollowerTry(struct FMUProc * proc, struct FmuFollower * f, int salt)
{
    int dir;

    if (Fmu_FollowersHold(proc))
    {
        f->wait = FMU_FOLLOWER_DELAY_MAX;
        return;
    }

    dir = Fmu_FollowerChooseStep(proc, f);

    if ((dir >= 0) && Fmu_FollowerStartStep(f, dir))
        return;

    if (dir == FMU_FOLLOWER_AT_TARGET)
    {
        f->blocked = 0;
        return;
    }

    // blocked, or no MU free
    f->wait = Fmu_FollowerRetryWait(proc, f, (dir >= 0) ? FMU_FOLLOWER_BLOCKED : dir, salt);
}

// A follower's step is over: the next move command, or -1
int Fmu_ContinueFollower(struct FMUProc * proc, struct FmuFollower * f, struct MuProc * mu)
{
    struct Unit * unit = GetUnit(f->uid);
    int dir;

    if (Fmu_FollowersHold(proc))
    {
        f->stepping = FALSE;
        f->idleFrames = 0;
        f->wait = FMU_FOLLOWER_DELAY_MAX;
        return -1;
    }

    dir = Fmu_FollowerChooseStep(proc, f);

    if (dir < 0)
    {
        f->stepping = FALSE;
        f->idleFrames = 0;

        if (dir != FMU_FOLLOWER_AT_TARGET)
            f->wait = Fmu_FollowerRetryWait(proc, f, dir, f->uid);

        return -1;
    }

    Fmu_MoveWalker(unit, unit->xPos + gFmuFacingOffsets[dir][0], unit->yPos + gFmuFacingOffsets[dir][1]);

    mu->config->pc = 1;
    mu->config->movescr[0] = MOVE_CMD_MOVE_BASE + dir;

    f->lastDir = dir;
    f->blocked = 0;

    return MOVE_CMD_MOVE_BASE + dir;
}

// The walking unit moved, or the map changed
void Fmu_ScheduleFollowers(struct FMUProc * proc)
{
    int i;

    for (i = 0; i < proc->followerCount; i++)
    {
        struct FmuFollower * f = &proc->followers[i];

        if (!f->stepping && (f->wait == 0))
        {
            f->wait = Fmu_RandomFollowerDelay(i + 1);
            f->blocked = 0;
        }
    }
}

void Fmu_UpdateFollowers(struct FMUProc * proc)
{
    bool ended = FALSE;
    int i;

    for (i = 0; i < proc->followerCount; i++)
    {
        struct FmuFollower * f = &proc->followers[i];
        struct MuProc * mu = Fmu_FindUnitMu(GetUnit(f->uid));

        if (f->stepping)
        {
            if ((mu != NULL) && (mu->state != MU_STATE_INACTIVE))
                continue;

            // something else ended or halted its MU
            f->stepping = FALSE;
        }

        if ((mu != NULL) && (f->wait == 0) && (++f->idleFrames > FMU_MU_IDLE_FRAMES))
        {
            EndMu(mu);
            ended = TRUE;
        }

        if ((f->wait != 0) && (--f->wait == 0))
            Fmu_FollowerTry(proc, f, i + 1);
    }

    if (ended)
        RefreshUnitSprites();
}

// The first player units in deployment order follow
void Fmu_RefreshFollowers(struct FMUProc * proc)
{
    struct FmuFollower old[FMU_MAX_FOLLOWERS];
    int oldCount = proc->followerCount;
    int count = (*FreeMoveFlag & FMU_STATE_FOLLOWERS_MASK) >> FMU_STATE_FOLLOWERS_SHIFT;
    bool kept[FMU_MAX_FOLLOWERS];
    bool ended = FALSE;
    int i, j, n = 0;

    for (i = 0; i < oldCount; i++)
    {
        old[i] = proc->followers[i];
        kept[i] = FALSE;
    }

    if (count > FMU_MAX_FOLLOWERS)
        count = FMU_MAX_FOLLOWERS;

    for (i = FACTION_BLUE + 1; (i < FACTION_GREEN) && (n < count); i++)
    {
        struct Unit * unit = GetUnit(i);
        struct FmuFollower * f = &proc->followers[n];

        if ((unit == proc->unit) || !Fmu_CanControlUnit(unit))
            continue;

        for (j = 0; j < oldCount; j++)
        {
            if (old[j].uid == unit->index)
                break;
        }

        if (j < oldCount)
        {
            *f = old[j];
            kept[j] = TRUE;
        }
        else
        {
            f->uid = unit->index;
            f->xTarget = unit->xPos;
            f->yTarget = unit->yPos;
            f->wait = Fmu_RandomFollowerDelay(n + 1);
            f->idleFrames = 0;
            f->blocked = 0;
            f->lastDir = -1;
            f->stepping = FALSE;
        }

        n++;
    }

    proc->followerCount = n;

    // units that stopped following show their map sprites again
    for (i = 0; i < oldCount; i++)
    {
        struct MuProc * mu;
        struct Unit * unit = GetUnit(old[i].uid);

        if (kept[i] || (unit == proc->unit))
            continue;

        if ((mu = Fmu_FindUnitMu(unit)) != NULL)
        {
            EndMu(mu);
            ended = TRUE;
        }
    }

    if (ended)
        RefreshUnitSprites();
}
