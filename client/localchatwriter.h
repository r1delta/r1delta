#pragma once
#include "color.h"

class vgui_BaseRichText;

class CHudChat
{
public:
	static CHudChat** allHuds;

	char unknown1[712];

	Color m_sameTeamColor;
	Color m_enemyTeamColor;
	Color m_mainTextColor;

	char unknown2[20];

	vgui_BaseRichText* m_richText;

	CHudChat* next;
	CHudChat* previous;
};
static_assert(sizeof(CHudChat) == 768);
static_assert(offsetof(CHudChat, m_richText) == 0x2E8);
static_assert(offsetof(CHudChat, next) == 0x2f0);

class LocalChatWriter
{
public:
	enum SwatchColor
	{
		MainTextColor,
		SameTeamNameColor,
		EnemyTeamNameColor
	};

	explicit LocalChatWriter();

	// Custom chat writing with ANSI escape codes
	void Write(const char* str);
	void WriteLine(const char* str);

	// Low-level RichText access
	void InsertChar(wchar_t ch);
	void InsertText(const char* str);
	void InsertText(const wchar_t* str);
	void InsertColorChange(Color color);
	void InsertSwatchColorChange(SwatchColor color);

private:
	const char* ApplyAnsiEscape(const char* escape);
	void InsertDefaultFade();
};

void SetupChatWriter();