#ifndef OBJ_IWAMODOKI_H
#define OBJ_IWAMODOKI_H

#include "../SonicMania.h"

// Object Class
typedef struct {
	RSDK_OBJECT
} ObjectIwamodoki;

// Entity Class
typedef struct {
	RSDK_ENTITY
} EntityIwamodoki;

// Object Struct
extern ObjectIwamodoki *Iwamodoki;

// Standard Entity Events
void Iwamodoki_Update();
void Iwamodoki_LateUpdate();
void Iwamodoki_StaticUpdate();
void Iwamodoki_Draw();
void Iwamodoki_Create(void* data);
void Iwamodoki_StageLoad();
void Iwamodoki_EditorDraw();
void Iwamodoki_EditorLoad();
void Iwamodoki_Serialize();

// Extra Entity Functions


#endif //!OBJ_IWAMODOKI_H