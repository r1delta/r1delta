#pragma once
#ifdef DEDICATED
#define SERVER_DLL "server_ds.dll"
#define SERVER_DLLWIDE L"server_ds.dll"
#define ENGINE_DLL "engine_ds.dll"
#else
#define SERVER_DLLWIDE L"server.dll"

#define SERVER_DLL "server.dll"
#define ENGINE_DLL "engine.dll"
#endif