#pragma once
#include "gbafe.h"

// 1: the walking unit stands as its moving sprite, so you can see which way it faces
#ifndef FMU_IDLE_MU
#define FMU_IDLE_MU 0
#endif

// There are only 4 MUs, so 3 followers at most
#define FMU_MAX_FOLLOWERS 3
#define FMU_DEFAULT_FOLLOWERS 0

enum
{
    // Running starts at half speed for this many frames
    FMU_RUN_RAMP_FRAMES = 40,

    // Frames to stand after turning to a new direction
    FMU_TURN_FRAMES = 8,

    // Frames to ignore input after a button action
    FMU_ACTION_FRAMES = 3,

    // Events shorter than this don't interrupt walking
    FMU_EVENT_MIN_FRAMES = 3,
    FMU_STEP_EVENT_FRAMES = FMU_EVENT_MIN_FRAMES + 1,

    // Keeps the walking unit in the middle of the screen
    FMU_CAMERA_MARGIN_X = 16 * 7,
    FMU_CAMERA_MARGIN_Y = 16 * 5,

    // Handled once the unit stands still (B is handled at once)
    FMU_ACTION_KEYS = A_BUTTON | L_BUTTON | R_BUTTON | SELECT_BUTTON | START_BUTTON,

    // A standing walker goes back to its map sprite after this many frames
    FMU_MU_IDLE_FRAMES = 8,

    // Followers start walking 2-6 frames after the walking unit
    FMU_FOLLOWER_DELAY_MIN = 2,
    FMU_FOLLOWER_DELAY_MAX = 6,

    // Blocked followers retry quickly a few times, then back off
    FMU_FOLLOWER_QUICK_RETRIES = 6,
    FMU_FOLLOWER_QUICK_RETRY = 2,
    FMU_FOLLOWER_RETRY_MAX = 24,
    FMU_FOLLOWER_SIDESTEP_TRIES = FMU_FOLLOWER_QUICK_RETRIES,

    // Close to the walking unit and still stuck: wait for it to move again
    FMU_FOLLOWER_GIVE_UP_TRIES = 12,
    FMU_FOLLOWER_GIVE_UP_STEPS = 2,

    FMU_FORMATION_CHANGE_COST = 2,
    FMU_FORMATION_NO_SLOT_COST = 40,
    FMU_FORMATION_FLOOD_STEPS = 6,
    FMU_FOLLOWER_FLOOD_STEPS = 16,
};

// The state byte (*FreeMoveFlag)
enum FmuStateBits
{
    FMU_STATE_ENABLED      = (1 << 0),
    FMU_STATE_RUNNING      = (1 << 1),
    FMU_STATE_FACING_SHIFT = 2,
    FMU_STATE_FACING_MASK  = (3 << FMU_STATE_FACING_SHIFT),
    FMU_STATE_PAUSED       = (1 << 4),
    FMU_STATE_KEEP_UNIT    = (1 << 5), // gActionData.subjectIndex is the walking unit
    FMU_STATE_FOLLOWERS_SHIFT = 6,
    FMU_STATE_FOLLOWERS_MASK  = (3 << FMU_STATE_FOLLOWERS_SHIFT),
};

enum
{
    L_FMU_MAIN = 0,
    L_FMU_END = 1,
};

struct FmuFollower
{
    u8 uid;
    s8 xTarget, yTarget; // its place in the formation
    u8 wait;             // frames until it tries to step, 0: not scheduled
    u8 idleFrames;
    u8 blocked;          // tries in a row that found no step
    s8 lastDir;          // -1: none yet
    bool8 stepping;
};

typedef struct FMUProc FMUProc;
typedef bool (*ButtonFunc)(struct FMUProc *);

struct FMUProc
{
    /* 00 */ PROC_HEADER;

    /* 29 */ u8 countdown;     // frames before input is read again
    /* 2A */ u8 moveSpeed;
    /* 2B */ u8 eventFrames;   // how long the current event has run
    /* 2C */ struct Unit * unit;
    /* 30 */ const u16 * event;
    /* 34 */ u32 moveStartTime;
    /* 38 */ s8 xTo, yTo;      // the tile it stands on or is stepping onto
    /* 3A */ s8 xCur, yCur;    // last tile whose step events ran
    /* 3C */ u8 facing;
    /* 3D */ bool8 yield;
    /* 3E */ bool8 yieldMove;
    /* 3F */ bool8 stepping;   // its MU walks a step we started (not an event's move)
    /* 40 */ bool8 recenterCamera;
    /* 41 */ bool8 restoreMap;
    /* 42 */ bool8 stepEventShown;
    /* 43 */ bool8 ending;
    /* 44 */ u16 pendingKeys;
    /* 46 */ u16 lastDirKey;
    /* 48 */ u16 queuedDirKey;
    /* 4A */ bool8 trapRan;
    /* 4B */ u8 moveDir;
    /* 4C */ u8 idleFrames;
    /* 4D */ bool8 walkEnded;
    /* 4E */ bool8 regroup;
    /* 4F */ u8 followerCount;
    /* 50 */ struct FmuFollower followers[FMU_MAX_FOLLOWERS];
};

typedef void (*TrapFunc)(struct FMUProc *, struct Trap *);
typedef bool (*TrapUsabilityFunc)(struct FMUProc *, struct Trap *);

struct FMUTrapDef
{
    u8 TrapID;
    TrapFunc Func;
    TrapUsabilityFunc IsUsable; // 0: always
};
extern struct FMUTrapDef HookListFMU_TrapList_OnPressA[];
extern struct FMUTrapDef HookListFMU_TrapList_OnPressAFaced[];
extern struct FMUTrapDef HookListFMU_TrapList_Auto[];
extern struct FMUTrapDef HookListFMU_TrapList_AutoFaced[];


struct LocEventDef
{
    u8 LocID;
    u8 TrapID;
};


extern struct LocEventDef HookListFMU_LocationBasedEvent[];
extern struct LocEventDef HookListFMU_LocationBasedEventDoor[];
extern ButtonFunc FMU_FunctionList_OnPressA[];
extern ButtonFunc FMU_FunctionList_OnPressB[];
extern ButtonFunc FMU_FunctionList_OnPressR[];
extern ButtonFunc FMU_FunctionList_OnPressL[];
extern ButtonFunc FMU_FunctionList_OnPressSelect[];
extern ButtonFunc FMU_FunctionList_OnPressStart[];

extern u8* const FreeMoveFlag;
extern const u8 FreeMU_MovingSpeed;
extern const u8 FreeMU_RunningSpeed;

extern const struct MenuDef FreeMovementLMenu;
extern const struct MenuDef FreeMovementUnitMenu;

extern const s8 gFmuFacingOffsets[4][2];

#define UNIT_ID_END (FACTION_PURPLE + 6)

// vanilla, missing from the headers
extern struct ProcCmd sProc_Menu[];
extern struct ProcCmd gProcScr_TargetSelection[];
bool CanShowUnitStatScreen(struct Unit * unit);
void ClearEventMoveBuffer(const struct REDA * redas);
void GetPlayerStartCursorPosition(int * px, int * py);
void GetEnemyStartCursorPosition(int * px, int * py);


/*------------- External --------------*/
bool FMU_CanUnitBeOnPos(struct Unit*, s8, s8);
void EnableFreeMovementASMC(void);
void DisableFreeMovementASMC(void);
void PauseFreeMovementASMC(void);
void ChangeFreeMovementUnitASMC(void);
void SetFreeMovementFollowersASMC(void);
u8 GetFreeMovementState(void);
void NewPlayerPhaseEvaluationFunc(struct Proc*);
void NewMakePhaseControllerFunc(struct Proc*);

struct FMUProc * Fmu_GetProc(void);
void Fmu_SetStateBits(u8 bits, bool on);
bool Fmu_IsEnabled(void);
bool Fmu_IsActive(void);
bool Fmu_StartsThisPhase(void);
bool Fmu_IsInBounds(int x, int y);
void Fmu_GetFacedTile(struct FMUProc * proc, int * x, int * y);
bool Fmu_CanControlUnit(struct Unit * unit);
struct Unit * Fmu_FindNextUnit(struct Unit * after);
void Fmu_SetUnit(struct FMUProc * proc, struct Unit * unit);
void Fmu_SetFacing(struct FMUProc * proc, int facing);
void Fmu_ResetDisplayEffects(void);
int Fmu_GetCameraTargetX(int x);
int Fmu_GetCameraTargetY(int y);
bool Fmu_CenterCamera(ProcPtr parent, int x, int y);
void Fmu_SeparateUnit(struct Unit * unit);


/*------------- Core --------------*/
bool Fmu_TryStartPhase(ProcPtr mapMain);
bool Fmu_EnsureUnit(struct FMUProc * proc);
void Fmu_End(struct FMUProc * proc);
void Fmu_EndForChapterChange(void);
void Fmu_HandOffToPlayerPhase(struct FMUProc * proc);
bool Fmu_IsActiveForMu(struct MuProc * mu);
bool Fmu_IsShownAsMu(struct Unit * unit);
struct MuProc * Fmu_FindUnitMu(struct Unit * unit);
struct MuProc * Fmu_StartWalkerMu(struct Unit * unit);
void Fmu_EndWalkerMus(struct FMUProc * proc, bool includeStepping);
void Fmu_MoveWalker(struct Unit * unit, int x, int y);
void Fmu_SetMuStep(struct MuProc * mu, int facing, bool stepSound);
int Fmu_GetContinuedMoveCommand(struct MuProc * mu);
u16 Fmu_GetMuQ4MovementSpeed(struct MuProc * mu);
void Fmu_UpdateMuCamera(struct MuProc * mu);


/*------------- Followers --------------*/
struct FmuFollower * Fmu_GetFollower(struct FMUProc * proc, struct Unit * unit);
bool Fmu_IsWalker(struct FMUProc * proc, struct Unit * unit);
bool Fmu_AnyFollowerStepping(struct FMUProc * proc);
void Fmu_ScheduleFollowers(struct FMUProc * proc);
void Fmu_UpdateFollowers(struct FMUProc * proc);
void Fmu_RefreshFollowers(struct FMUProc * proc);
int Fmu_ContinueFollower(struct FMUProc * proc, struct FmuFollower * f, struct MuProc * mu);


/*------------- Events --------------*/
bool Fmu_OnArrive(struct FMUProc * proc);
bool FMUmisc_RunMapEvents(struct FMUProc*);
bool FMUmisc_RunTalkEvents(struct FMUProc*);
bool FMU_RunTrapASMC(FMUProc*);
bool Fmu_CanRunVanillaTrap(struct FMUProc * proc, struct Trap * trap);
void Fmu_RunVanillaTrap(struct FMUProc * proc, struct Trap * trap);

/*------------- KeyPress --------------*/
bool Fmu_RunButtonList(struct FMUProc * proc, ButtonFunc * list);
bool Fmu_HandleButtons(struct FMUProc * proc, u16 keys);
bool FMU_OnButton_StartMenu(FMUProc*);
bool FMU_OnButton_UnitMenu(FMUProc*);
bool FMU_OnButton_EndFreeMove(FMUProc*);
bool FMU_OnButton_ChangeUnit(FMUProc*);
bool FMU_OnButton_ShowStats(FMUProc*);
bool FMU_OnButton_ToggleRun(FMUProc*);
u8 FMU_GridModeCommand(struct MenuProc * menu, struct MenuItemProc * menuItem);
u8 FMU_MenuHelpBox(struct MenuProc * menu, struct MenuItemProc * menuItem);
