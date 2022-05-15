#ifndef SONIC_MANIA_H
#define SONIC_MANIA_H

// =====================================================================
// RSDK Project: Sonic Mania (Plus)
// Decompiled by: Rubberduckycooly & RMGRich
// =====================================================================

// --------------------
// ENGINE VERSIONS
// --------------------

// RSDKv5 was revised & updated for plus onwards
// this is basically the same functionality as "MANIA_USE_PLUS"
// but I split this one to be more specific about engine changes vs game changes
#define RETRO_REV02 (1)

// Controls whether EditorLoad & EditorDraw should be included in the final product or not
// This is a copy of what the original game likely had, as the original game does not include EditorLoad or EditorDraw funcs for any objects
#define RETRO_INCLUDE_EDITOR (1)

#ifndef RETRO_USE_MOD_LOADER
#define RETRO_USE_MOD_LOADER (0)
#endif

#define RETRO_MOD_LOADER_VER (1)

// used to manage standalone (RSDKv5.exe & Game.dll) and combined (Game.exe) modes
#ifndef RETRO_STANDALONE
#define RETRO_STANDALONE (1)
#endif

// --------------------
// GAME VERSIONS
// --------------------

#define VER_100 (0) // 1.00 (initial console release)
#define VER_103 (3) // 1.03 (PC release/console patch)
#define VER_105 (5) // 1.04/1.05
#define VER_106 (6) // 1.06 (steam denuvo removal update)
#define VER_107 (7) // 1.07 (EGS/Origin releases)

#ifdef MANIA_PREPLUS
#define MANIA_GAMEVER VER_103
#else
#define MANIA_GAMEVER VER_106
#endif

#define MANIA_USE_PLUS (MANIA_GAMEVER >= VER_105)
#define MANIA_USE_EGS  (MANIA_GAMEVER == VER_107)

#define MANIA_USE_TOUCH_CONTROLS (1)

// --------------------
// GAME LOGIC
// --------------------

#include "GameLink.h"
#include "GameVariables.h"
#include "GameObjects.h"

extern RSDKSceneInfo *SceneInfo;
extern RSDKGameInfo *GameInfo;
#if MANIA_USE_PLUS
extern RSDKSKUInfo *SKU;
#endif
extern RSDKControllerState *ControllerInfo;
extern RSDKAnalogState *AnalogStickInfoL;
#if MANIA_USE_PLUS
extern RSDKAnalogState *AnalogStickInfoR;
extern RSDKTriggerState *TriggerInfoL;
extern RSDKTriggerState *TriggerInfoR;
#endif
extern RSDKTouchInfo *TouchInfo;
#if MANIA_USE_PLUS
extern RSDKUnknownInfo *UnknownInfo;
#endif
extern RSDKScreenInfo *ScreenInfo;

#endif //! SONIC_MANIA_H
