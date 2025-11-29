#pragma once

// Forward declaration
struct SQVM;
typedef SQVM* HSQUIRRELVM;
typedef long SQInteger;

void SetupSurfaceRenderHooks();
void SetupSquirrelErrorNotificationHooks();
void SetupLocalizeIface();
SQInteger Script_AddDamageNumber(HSQUIRRELVM v);