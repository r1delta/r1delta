#pragma once
#include <stdarg.h>
#include "cvar.h"
//-----------------------------------------------------------------------------
// Purpose: Basic handler for an rgb set of colors
//			This class is fully inline
//-----------------------------------------------------------------------------
class Color
{
public:
	// constructors
	Color()
	{
		*((int*)this) = 0;
	}
	Color(int _r, int _g, int _b)
	{
		SetColor(_r, _g, _b, 0);
	}
	Color(int _r, int _g, int _b, int _a)
	{
		SetColor(_r, _g, _b, _a);
	}

	// set the color
	// r - red component (0-255)
	// g - green component (0-255)
	// b - blue component (0-255)
	// a - alpha component, controls transparency (0 - transparent, 255 - opaque);
	void SetColor(int _r, int _g, int _b, int _a = 0)
	{
		_color[0] = (unsigned char)_r;
		_color[1] = (unsigned char)_g;
		_color[2] = (unsigned char)_b;
		_color[3] = (unsigned char)_a;
	}

	void GetColor(int& _r, int& _g, int& _b, int& _a) const
	{
		_r = _color[0];
		_g = _color[1];
		_b = _color[2];
		_a = _color[3];
	}

	void SetRawColor(int color32)
	{
		*((int*)this) = color32;
	}

	int GetRawColor() const
	{
		return *((int*)this);
	}

	inline int r() const { return _color[0]; }
	inline int g() const { return _color[1]; }
	inline int b() const { return _color[2]; }
	inline int a() const { return _color[3]; }

	unsigned char& operator[](int index)
	{
		return _color[index];
	}

	const unsigned char& operator[](int index) const
	{
		return _color[index];
	}

	bool operator == (const Color& rhs) const
	{
		return (*((int*)this) == *((int*)&rhs));
	}

	bool operator != (const Color& rhs) const
	{
		return !(operator==(rhs));
	}

	Color& operator=(const Color& rhs)
	{
		SetRawColor(rhs.GetRawColor());
		return *this;
	}


private:
	unsigned char _color[4];
};

extern void (*Msg)(const char* pMsg, ...);
extern void (*Warning)(const char* pMsg, ...);
extern void (*Warning_SpewCallStack)(int iMaxCallStackLength, const char* pMsg, ...);
extern void (*DevMsg)(int level, const char* pMsg, ...);
extern void (*DevWarning)(int level, const char* pMsg, ...);
extern void (*ConColorMsg)(const Color& clr, const char* pMsg, ...);
extern void (*ConDMsg)(const char* pMsg, ...);
extern void (*COM_TimestampedLog)(const char* fmt, ...);
typedef void (*ConVar_PrintDescriptionType)(const ConCommandBaseR1* pVar);
extern ConVar_PrintDescriptionType ConVar_PrintDescriptionOriginal;
void ConVar_PrintDescription(const ConCommandBaseR1* pVar);
bool __fastcall SVC_Print_Process_Hook(__int64 a1);
void sub_18027F2C0(__int64 a1, const char* a2, __int64 a3);
int __cdecl vsnprintf_l_hk(
	char* const Buffer,
	const size_t BufferCount,
	const char* const Format,
	const _locale_t Locale,
	va_list ArgList);
void Cbuf_AddText(int a1, const char* a2, unsigned int a3);
char* __fastcall sub_1804722E0(char* Destination, const char* a2, unsigned __int64 a3, __int64 a4);
typedef void (*sub_18027F2C0Type)(__int64 a1, const char* a2, __int64 a3);
extern sub_18027F2C0Type sub_18027F2C0Original;
typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
extern Cbuf_AddTextType Cbuf_AddTextOriginal;
