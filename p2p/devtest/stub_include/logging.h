#pragma once
// devtest stand-in for engine/logging/logging.h
extern "C" void Msg(const char* fmt, ...);
extern "C" void Warning(const char* fmt, ...);
void Cbuf_AddText(int a1, const char* a2, unsigned int a3);
