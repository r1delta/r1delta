#pragma once
typedef void (*CAI_NetworkManager__DelayedInitType)(__int64 a1);
extern CAI_NetworkManager__DelayedInitType CAI_NetworkManager__DelayedInitOriginal;
typedef void (*CAI_NetworkManager__FixupHintsType)();
extern CAI_NetworkManager__FixupHintsType CAI_NetworkManager__FixupHintsOriginal;
void __fastcall CAI_NetworkManager__DelayedInit(__int64 a1);
void __fastcall CAI_NetworkManager__FixupHints();
void sub_364140(int node1, int node2, const char* pszFormat, ...);
typedef void (*sub_36BC30Type)(__int64* a1);
typedef __int64 (*sub_36C150Type)(__int64 a1, int a2, int a3);
