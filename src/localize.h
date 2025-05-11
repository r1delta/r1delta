#pragma once
#include "appsystem.h"

typedef uint32_t LocalizeStringIndex_t;

class ILocalize : public IAppSystem {
public:
	// adds the contents of a file to the localization table
	virtual bool AddFile(const char* fileName, const char* pPathID = nullptr, bool bIncludeFallbackSearchPaths = false) = 0;

	// Remove all strings from the table
	virtual void RemoveAll() = 0;

	// Finds the localized text for tokenName. Returns nullptr if none is found.
	virtual wchar_t* Find(const char* tokenName) = 0;

	// Like Find(), but as a failsafe, returns an error message instead of nullptr if the string isn't found.
	virtual const wchar_t* FindSafe(const char* tokenName) = 0;

	// converts an english string to unicode
	// returns the number of wchar_t in resulting string, including null terminator
	virtual int ConvertANSIToUnicode(const char* ansi, wchar_t* unicode, int unicodeBufferSizeInBytes) = 0;

	// converts an unicode string to an english string
	// unrepresentable characters are converted to system default
	// returns the number of characters in resulting string, including null terminator
	virtual int ConvertUnicodeToANSI(const wchar_t* unicode, char* ansi, int ansiBufferSize) = 0;

	// finds the index of a token by token name, INVALID_STRING_INDEX if not found
	virtual LocalizeStringIndex_t FindIndex(const char* tokenName) = 0;

	// builds a localized formatted string
	// uses the format strings first: %s1, %s2, ...  unicode strings (wchar_t* )
	virtual void ConstructString(wchar_t* unicodeOuput, int unicodeBufferSizeInBytes, const wchar_t* formatString,
		int numFormatParameters, ...) = 0;

	// gets the values by the string index
	virtual const char* GetNameByIndex(LocalizeStringIndex_t index) = 0;

	virtual wchar_t* GetValueByIndex(LocalizeStringIndex_t index) = 0;
};