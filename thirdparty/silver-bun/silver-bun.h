#pragma once

#if !defined( SILVER_BUN_CPP_HDRS )
#include <iostream>
#include <vector>
#include <windows.h>
#include <intrin.h>
#endif // #if !defined( SILVER_BUN_CPP_HDRS )

#if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || __cplusplus >= 201703L) // Check for C++17 or later.
#define CUSTOM_WINAPI_FUNC
#endif // #if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || __cplusplus >= 201703L)

typedef const unsigned char* rsig_t;
namespace silverbun
{
#if defined ( CUSTOM_WINAPI_FUNC )
	typedef bool (*API_ProtectVirtualMemory)(void* const, const size_t, const uint32_t, uint32_t*);
	typedef size_t(*API_QueryVirtualMemory)(const void* const, MEMORY_BASIC_INFORMATION* const, size_t);
	typedef int(*API_WideCharToMultiByte)(const uint32_t, const uint32_t, const wchar_t* const, const int, char*, const int, const char* const, bool* const);

	inline API_ProtectVirtualMemory pPVM = nullptr;
	inline API_QueryVirtualMemory pQVM = nullptr;
	inline API_WideCharToMultiByte pWCTMB = nullptr;

	template<class T> void SetPVM(T const pFunc)
	{
		pPVM = reinterpret_cast<API_ProtectVirtualMemory>(pFunc);
	}

	template<class T> void SetQVM(T const pFunc)
	{
		pQVM = reinterpret_cast<API_QueryVirtualMemory>(pFunc);
	}

	template<class T> void SetWCTMB(T const pFunc)
	{
		pWCTMB = reinterpret_cast<API_WideCharToMultiByte>(pFunc);
	}

	__forceinline bool CallPVM(void* const pAddress, const size_t nSize, const uint32_t nProtect, uint32_t* pOldProtect)
	{
		return pPVM != nullptr ? pPVM(pAddress, nSize, nProtect, pOldProtect) : VirtualProtect(pAddress, nSize, nProtect, reinterpret_cast<DWORD*>(pOldProtect));
	}

	__forceinline bool CallQVM(const void* const pAddress, MEMORY_BASIC_INFORMATION* const pBuf, const uint32_t nLen)
	{
		return pQVM != nullptr ? pQVM(pAddress, pBuf, nLen) : VirtualQuery(pAddress, pBuf, nLen);
	}

	__forceinline int CallWCTMB(const uint32_t nCodePage, const uint32_t nFlags, const wchar_t* const wszInWideString, const int nSizeOfInString, char* szOutString, const int nSizeOfOutString, const char* const szDefaultChar, bool* const usedDefaultChar)
	{
		return pWCTMB != nullptr ? pWCTMB(nCodePage, nFlags, wszInWideString, nSizeOfInString, szOutString, nSizeOfOutString, szDefaultChar, usedDefaultChar) : 
			WideCharToMultiByte(nCodePage, nFlags, wszInWideString, nSizeOfInString, szOutString, nSizeOfOutString, szDefaultChar, reinterpret_cast<LPBOOL>(usedDefaultChar));
	}
#else
	template<class T> void SetPVM(T const pFunc)
	{
		return;
	}

	template<class T> void SetQVM(T const pFunc)
	{
		return;
	}
	template<class T> void SetWCTMB(T const pFunc)
	{
		return;
	}

	bool CallPVM(void* const pAddress, const size_t nSize, const uint32_t nProtect, uint32_t* pOldProtect)
	{
		return VirtualProtect(pAddress, nSize, nProtect, reinterpret_cast<DWORD*>(pOldProtect));
	}

	bool CallQVM(const void* const pAddress, MEMORY_BASIC_INFORMATION* const pBuf, const uint32_t nLen)
	{
		return VirtualQuery(pAddress, pBuf, nLen);
	}

	int CallWCTMB(const uint32_t nCodePage, const uint32_t nFlags, const wchar_t* const wszInWideString, const int nSizeOfInString, char* szOutString, const int nSizeOfOutString, const char* szDefaultChar, bool* usedDefaultChar)
	{
		return WideCharToMultiByte(nCodePage, nFlags, wszInWideString, nSizeOfInString, szOutString, nSizeOfOutString, szDefaultChar, reinterpret_cast<LPBOOL>(usedDefaultChar));
	}
#endif // #if defined ( CUSTOM_WINAPI_FUNC )

#if !defined( USE_WINAPI_FOR_MODULE_NAME )
	// We don't need vise versa methods on both x86 and x64. Only get the version we actually need.
	// Alignment is not gonna ruin my day.
#pragma pack(push)
#pragma pack(1)

	// Smaller structs, we dont need the full picture.
	template <class T>
	struct UNICODE_STRING_T
	{
		union
		{
			struct
			{
				WORD Length;
				WORD MaximumLength;
			} u;
			T align;
		};
		T Buffer;
	};

	template <class T>
	struct LDR_DATA_TABLE_ENTRY_T
	{
		LIST_ENTRY InLoadOrderLinks;
		LIST_ENTRY InMemoryOrderLinks;
		union
		{
			LIST_ENTRY InInitializationOrderLinks;
			LIST_ENTRY InProgressLinks;
		} u;
		PVOID DllBase;
		PVOID EntryPoint;
		T SizeOfImage; // We need to combat alignment here.
		UNICODE_STRING_T<T> FullDllName;
		UNICODE_STRING_T<T> BaseDllName;
	};

	struct PEB_LDR_DATA_S
	{
		ULONG Length;
		BOOLEAN Initialized;
		BYTE pad[3];
		HANDLE SsHandle;
		LIST_ENTRY InLoadOrderModuleList;
	};

	template <class T>
	struct PEB_T
	{
		union
		{
			struct
			{
				BYTE InheritedAddressSpace;
				BYTE ReadImageFileExecOptions;
				BYTE BeingDebugged;
				BYTE Unk; // Never found out whatever that was.
			} u;
			T dword0;
		};
		T Mutant;
		T ImageBaseAddress;
		PEB_LDR_DATA_S* Ldr;
	};

#if defined( _WIN64 ) // https://en.wikipedia.org/wiki/Win32_Thread_Information_Block
	constexpr ptrdiff_t pebOffset = 0x60;
#else
	constexpr ptrdiff_t pebOffset = 0x30;
#endif // #if defined( _WIN64 )

	typedef PEB_T<uintptr_t> PEB_S;
	typedef LDR_DATA_TABLE_ENTRY_T<uintptr_t> LDR_DATA_TABLE_ENTRY_S;
#pragma pack(pop)
#endif // #if !defined( USE_WINAPI_FOR_MODULE_NAME )

#if defined( _WIN64 ) 
	constexpr uint32_t COL_SIG_REV = 1u;
#else
	constexpr uint32_t COL_SIG_REV = 0u;
#endif // #if defined( _WIN64 ) 
}

class CMemory
{
private:
	uintptr_t ptr;
public:
	enum class Direction : int
	{
		DOWN = 0,
		UP,
	};

	CMemory() : ptr(0) {}
	CMemory(const uintptr_t ptr) : ptr(ptr) {}
	CMemory(const void* const ptr) : ptr(reinterpret_cast<uintptr_t>(ptr)) {}

	inline operator uintptr_t(void) const
	{
		return ptr;
	}

	inline operator void* (void) const
	{
		return reinterpret_cast<void*>(ptr);
	}

	inline operator bool(void) const
	{
		return ptr != 0;
	}

	inline bool operator!= (const CMemory& addr) const
	{
		return ptr != addr.ptr;
	}

	inline bool operator== (const CMemory& addr) const
	{
		return ptr == addr.ptr;
	}

	inline bool operator== (const uintptr_t addr) const
	{
		return ptr == addr;
	}

	inline uintptr_t GetPtr(void) const
	{
		return ptr;
	}

	template<class T> inline T GetValue(void) const
	{
		return *reinterpret_cast<T*>(ptr);
	}

	template<class T> inline T GetVirtualFunctionIndex(void) const
	{
		return *reinterpret_cast<T*>(ptr) / sizeof(uintptr_t);
	}

	template<typename T> inline T RCast(void) const
	{
		return reinterpret_cast<T>(ptr);
	}

	inline CMemory Offset(const ptrdiff_t offset) const
	{
		return CMemory(ptr + offset);
	}

	inline CMemory OffsetSelf(const ptrdiff_t offset)
	{
		ptr += offset;
		return *this;
	}

	inline CMemory Deref(int deref = 1) const
	{
		uintptr_t reference = ptr;

		while (deref--)
		{
			if (reference)
				reference = *reinterpret_cast<uintptr_t*>(reference);
		}

		return CMemory(reference);
	}

	inline CMemory DerefSelf(int deref = 1)
	{
		while (deref--)
		{
			if (ptr)
				ptr = *reinterpret_cast<uintptr_t*>(ptr);
		}

		return *this;
	}

	inline CMemory WalkVTable(const ptrdiff_t vfuncIdx)
	{
		const uintptr_t reference = ptr + (sizeof(uintptr_t) * vfuncIdx);
		return CMemory(reference);
	}

	inline CMemory WalkVTableSelf(const ptrdiff_t vfuncIdx)
	{
		ptr += (sizeof(uintptr_t) * vfuncIdx);
		return *this;
	}

	inline CMemory FollowNearCall(const ptrdiff_t opcodeOffset = 0x1, const ptrdiff_t nextInstructionOffset = 0x5) const
	{
		return ResolveRelativeAddress(opcodeOffset, nextInstructionOffset);
	}

	inline CMemory FollowNearCallSelf(const ptrdiff_t opcodeOffset = 0x1, const ptrdiff_t nextInstructionOffset = 0x5)
	{
		return ResolveRelativeAddressSelf(opcodeOffset, nextInstructionOffset);
	}

	inline CMemory ResolveRelativeAddress(const ptrdiff_t registerOffset = 0x0, const ptrdiff_t nextInstructionOffset = 0x4) const
	{
		const uintptr_t skipRegister = ptr + registerOffset;
		const int32_t relativeAddress = *reinterpret_cast<int32_t*>(skipRegister);

		const uintptr_t nextInstruction = ptr + nextInstructionOffset;
		return CMemory(nextInstruction + relativeAddress);
	}

	inline CMemory ResolveRelativeAddressSelf(const ptrdiff_t registerOffset = 0x0, const ptrdiff_t nextInstructionOffset = 0x4)
	{
		const uintptr_t skipRegister = ptr + registerOffset;
		const int32_t relativeAddress = *reinterpret_cast<int32_t*>(skipRegister);

		const uintptr_t nextInstruction = ptr + nextInstructionOffset;
		ptr = nextInstruction + relativeAddress;
		return *this;
	}

	bool CheckOpCodes(const std::vector<uint8_t>& vOpcodeArray) const
	{
		uintptr_t ref = ptr;

		for (size_t i = 0; i < vOpcodeArray.size(); i++, ref++)
		{
			const uint8_t byteAtCurrentAddress = *reinterpret_cast<const uint8_t*>(ref);

			// If byte at ptr doesn't equal in the byte array return false.
			if (byteAtCurrentAddress != vOpcodeArray[i])
				return false;
		}

		return true;
	}

	void Patch(const std::vector<uint8_t>& vOpcodeArray) const
	{
		using namespace silverbun;

		uint32_t nOldProt = 0u;

		size_t nSize = vOpcodeArray.size();
		CallPVM(reinterpret_cast<void*>(ptr), nSize, PAGE_EXECUTE_READWRITE, &nOldProt); // Patch page to be able to read and write to it.

		for (size_t i = 0; i < vOpcodeArray.size(); i++)
		{
			*reinterpret_cast<uint8_t*>(ptr + i) = vOpcodeArray[i];
		}

		CallPVM(reinterpret_cast<void*>(ptr), nSize, nOldProt, &nOldProt);
	}

	void PatchString(const char* const szString) const
	{
		using namespace silverbun;

		uint32_t nOldProt = 0u;
		size_t nSize = strlen(szString);

		CallPVM(reinterpret_cast<void*>(ptr), nSize, PAGE_EXECUTE_READWRITE, &nOldProt); // Patch page to be able to read and write to it.

		for (size_t i = 0; i < nSize; i++)
		{
			*reinterpret_cast<uint8_t*>(ptr + i) = szString[i];
		}

		CallPVM(reinterpret_cast<void*>(ptr), nSize, nOldProt, &nOldProt);
	}

	CMemory FindPattern(const char* const szPattern, const Direction searchDirect = Direction::DOWN, const int opCodesToScan = 512, const ptrdiff_t nOccurences = 1) const
	{
		const uint8_t* const pScanBytes = reinterpret_cast<uint8_t*>(ptr);

		const std::vector<int> PatternBytes = PatternToBytes(szPattern);
		const std::pair<size_t, const int*> bytesInfo = std::make_pair<size_t, const int*>(PatternBytes.size(), PatternBytes.data());
		ptrdiff_t occurrences = 0;

		for (long i = 01; i < opCodesToScan + static_cast<int>(bytesInfo.first); i++)
		{
			bool bFound = true;
			int nMemOffset = searchDirect == Direction::DOWN ? i : -i;

			for (DWORD j = 0ul; j < bytesInfo.first; j++)
			{
				// If either the current byte equals to the byte in our pattern or our current byte in the pattern is a wildcard
				// our if clause will be false.
				const uint8_t* const pCurrentAddr = (pScanBytes + nMemOffset + j);
				_mm_prefetch(reinterpret_cast<const char*>(pCurrentAddr + 64), _MM_HINT_T0); // precache some data in L1.
				if (*pCurrentAddr != bytesInfo.second[j] && bytesInfo.second[j] != -1)
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
			{
				occurrences++;
				if (nOccurences == occurrences)
				{
					return CMemory(&*(pScanBytes + nMemOffset));
				}
			}
		}

		return CMemory();
	}

	CMemory FindPatternSelf(const char* const szPattern, const Direction searchDirect = Direction::DOWN, const int opCodesToScan = 512, const ptrdiff_t occurrence = 1)
	{
		const uint8_t* const pScanBytes = reinterpret_cast<uint8_t*>(ptr);

		const std::vector<int> PatternBytes = PatternToBytes(szPattern); // Convert our pattern to a byte array.
		const std::pair<size_t, const int*> bytesInfo = std::make_pair<size_t, const int*>(PatternBytes.size(), PatternBytes.data());
		ptrdiff_t occurrences = 0;

		for (long i = 01; i < opCodesToScan + static_cast<int>(bytesInfo.first); i++)
		{
			bool bFound = true;
			int nMemOffset = searchDirect == Direction::DOWN ? i : -i;

			for (DWORD j = 0ul; j < bytesInfo.first; j++)
			{
				// If either the current byte equals to the byte in our pattern or our current byte in the pattern is a wildcard
				// our if clause will be false.
				const uint8_t* const pCurrentAddr = (pScanBytes + nMemOffset + j);
				_mm_prefetch(reinterpret_cast<const char*>(pCurrentAddr + 64), _MM_HINT_T0); // precache some data in L1.
				if (*pCurrentAddr != bytesInfo.second[j] && bytesInfo.second[j] != -1)
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
			{
				occurrences++;
				if (occurrence == occurrences)
				{
					ptr = uintptr_t(&*(pScanBytes + nMemOffset));
					return *this;
				}
			}
		}

		ptr = uintptr_t();
		return *this;
	}

	std::vector<CMemory> FindAllCallReferences(const uintptr_t sectionBase, const size_t sectionSize) const
	{
		std::vector<CMemory> referencesInfo;

		uint8_t* const pTextStart = reinterpret_cast<uint8_t*>(sectionBase);
		for (size_t i = 0ull; i < sectionSize - 0x5; i++, _mm_prefetch(reinterpret_cast<const char*>(pTextStart + 64), _MM_HINT_NTA))
		{
			if (pTextStart[i] == 0xE8) // Call instruction = 0xE8
			{
				CMemory memAddr = CMemory(&pTextStart[i]);
				if (!memAddr.Offset(0x1).CheckOpCodes({ 0x00, 0x00, 0x00, 0x00 })) // Check if its not a dynamic resolved call.
				{
					if (memAddr.FollowNearCall() == *this)
						referencesInfo.push_back(std::move(memAddr));
				}
			}
		}

		return referencesInfo;
	}

	static void HookVirtualMethod(const uintptr_t virtualTable, const void* const pHookMethod, const ptrdiff_t methodIndex, void** const ppOriginalMethod)
	{
		using namespace silverbun;

		uint32_t nOldProt = 0u;

		// Calculate delta to next virtual method.
		const uintptr_t virtualMethod = virtualTable + (methodIndex * sizeof(ptrdiff_t));
		const uintptr_t originalFunction = *reinterpret_cast<uintptr_t*>(virtualMethod);

		// Set page for current virtual method to execute n read n write so we can actually hook it.
		CallPVM(reinterpret_cast<void*>(virtualMethod), sizeof(virtualMethod), PAGE_EXECUTE_READWRITE, &nOldProt);

		// Patch virtual method to our hook.
		*reinterpret_cast<uintptr_t*>(virtualMethod) = reinterpret_cast<uintptr_t>(pHookMethod);

		CallPVM(reinterpret_cast<void*>(virtualMethod), sizeof(virtualMethod), nOldProt, &nOldProt);

		*ppOriginalMethod = reinterpret_cast<void*>(originalFunction);
	}

	static void HookImportedFunction(const uintptr_t pImportedMethod, const void* const pHookMethod, void** const ppOriginalMethod)
	{
		using namespace silverbun;

		uint32_t nOldProt = 0u;

		const uintptr_t originalFunction = *reinterpret_cast<uintptr_t*>(pImportedMethod);

		// Set page for current iat entry to execute n read n write so we can actually hook it.
		CallPVM(reinterpret_cast<void*>(pImportedMethod), sizeof(void*), PAGE_EXECUTE_READWRITE, &nOldProt);

		// Patch method to our hook.
		*reinterpret_cast<uintptr_t*>(pImportedMethod) = reinterpret_cast<uintptr_t>(pHookMethod);

		CallPVM(reinterpret_cast<void*>(pImportedMethod), sizeof(uintptr_t), nOldProt, &nOldProt);

		*ppOriginalMethod = reinterpret_cast<void*>(originalFunction);
	}

	static std::vector<int> PatternToBytes(const char* const szInput)
	{
		char* const pszPatternStart = const_cast<char*>(szInput);
		const char* const pszPatternEnd = pszPatternStart + strlen(szInput);
		std::vector<int> vBytes;

		for (char* pszCurrentByte = pszPatternStart; pszCurrentByte < pszPatternEnd; ++pszCurrentByte)
		{
			if (*pszCurrentByte == '?')
			{
				++pszCurrentByte;
				if (*pszCurrentByte == '?')
					++pszCurrentByte; // Skip double wildcard.

				vBytes.push_back(-1); // Push the byte back as invalid.
			}
			else
			{
				vBytes.push_back(strtoul(pszCurrentByte, &pszCurrentByte, 16));
			}
		}

		return vBytes;
	};

	static std::pair<std::vector<uint8_t>, std::string> PatternToMaskedBytes(const char* const szInput)
	{
		const char* pszPatternStart = const_cast<char*>(szInput);
		const char* pszPatternEnd = pszPatternStart + strlen(szInput);

		std::vector<uint8_t> vBytes;
		std::string svMask;

		for (const char* pszCurrentByte = pszPatternStart; pszCurrentByte < pszPatternEnd; ++pszCurrentByte)
		{
			if (*pszCurrentByte == '?')
			{
				++pszCurrentByte;
				if (*pszCurrentByte == '?')
				{
					++pszCurrentByte; // Skip double wildcard.
				}

				// Technically skipped but we need to push any value here.
				vBytes.push_back(0);
				svMask += '?';
			}
			else
			{
				vBytes.push_back(uint8_t(strtoul(pszCurrentByte, const_cast<char**>(&pszCurrentByte), 16)));
				svMask += 'x';
			}
		}
		return make_pair(vBytes, svMask);
	};

	static std::vector<int> StringToBytes(const char* const szInput, bool bNullTerminator)
	{
		const char* pszStringStart = const_cast<char*>(szInput);
		const char* pszStringEnd = pszStringStart + strlen(szInput);
		std::vector<int> vBytes;

		for (const char* pszCurrentByte = pszStringStart; pszCurrentByte < pszStringEnd; ++pszCurrentByte)
		{
			// Dereference character and push back the byte.
			vBytes.push_back(*pszCurrentByte);
		}

		if (bNullTerminator)
		{
			vBytes.push_back('\0');
		}
		return vBytes;
	};

	static std::pair<std::vector<uint8_t>, std::string> StringToMaskedBytes(const char* const szInput, bool bNullTerminator)
	{
		const char* pszStringStart = const_cast<char*>(szInput);
		const char* pszStringEnd = pszStringStart + strlen(szInput);
		std::vector<uint8_t> vBytes;
		std::string svMask;

		for (const char* pszCurrentByte = pszStringStart; pszCurrentByte < pszStringEnd; ++pszCurrentByte)
		{
			// Dereference character and push back the byte.
			vBytes.push_back(*pszCurrentByte);
			svMask += 'x';
		}

		if (bNullTerminator)
		{
			vBytes.push_back(0x0);
			svMask += 'x';
		}
		return make_pair(vBytes, svMask);
	};
};

class CModule
{
public:
	struct ModuleSections_t
	{
		ModuleSections_t(void) = default;
		ModuleSections_t(const char* sectionName, uintptr_t pSectionBase, size_t nSectionSize) : m_SectionName(sectionName), m_pSectionBase(pSectionBase), m_nSectionSize(nSectionSize) {}
		inline bool IsSectionValid(void) const { return m_nSectionSize != 0; }

		const char* m_SectionName;
		uintptr_t   m_pSectionBase;
		size_t      m_nSectionSize;
	};

	// I highly expect you to not pass garbage for any of these values.
	CModule(void) = default;
	CModule(const char* const szModuleName)
	{
		// We need a module name here, abort if null.
		strcpy_s(m_ModuleName, szModuleName);
		if (m_ModuleName[0] == '\0')
			abort();

		m_pModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(szModuleName));

		ParsePEHdr();
		LoadSections(".text", ".pdata", ".data", ".rdata");
	}

	CModule(const char* const szModuleName, const char* const szExecuteable, const char* const szExeception, const char* const szRunTime, const char* const szReadOnly)
	{
		// Same as above.
		strcpy_s(m_ModuleName, szModuleName);
		if (m_ModuleName[0] == '\0')
			abort();

		m_pModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(szModuleName));

		ParsePEHdr();
		LoadSections(szExecuteable, szExeception, szRunTime, szReadOnly);
	}

	CModule(const uintptr_t nModuleBase) : m_pModuleBase(nModuleBase)
	{
	#if !defined( USE_WINAPI_FOR_MODULE_NAME )
		GetModuleNameFromLdr(nModuleBase, m_ModuleName, sizeof(m_ModuleName));
	#else
		GetModuleNameFromWinAPI(nModuleBase, m_ModuleName, sizeof(m_ModuleName));
	#endif // #if !defined( USE_WINAPI_FOR_MODULE_NAME )

		ParsePEHdr();
		LoadSections(".text", ".pdata", ".data", ".rdata");
	}

	CModule(const uintptr_t nModuleBase, const char* const szExecuteable, const char* const szExeception, const char* const szRunTime, const char* const szReadOnly) : m_pModuleBase(nModuleBase)
	{
#if !defined( USE_WINAPI_FOR_MODULE_NAME )
		GetModuleNameFromLdr(nModuleBase, m_ModuleName, sizeof(m_ModuleName));
#else
		GetModuleNameFromWinAPI(nModuleBase, m_ModuleName, sizeof(m_ModuleName));
#endif // #if !defined( USE_WINAPI_FOR_MODULE_NAME )

		ParsePEHdr();
		LoadSections(szExecuteable, szExeception, szRunTime, szReadOnly);
	}

	CModule(const char* const szModuleName, const uintptr_t nModuleBase) : m_pModuleBase(nModuleBase)
	{
		strcpy_s(m_ModuleName, szModuleName);

		ParsePEHdr();
		LoadSections(".text", ".pdata", ".data", ".rdata");
	}

	CModule(const char* const szModuleName, const uintptr_t nModuleBase, const char* const szExecuteable, const char* const szExeception, const char* const szRunTime, const char* const szReadOnly) : m_pModuleBase(nModuleBase)
	{
		strcpy_s(m_ModuleName, szModuleName);

		ParsePEHdr();
		LoadSections(szExecuteable, szExeception, szRunTime, szReadOnly);
	}

	~CModule()
	{
		if (m_ModuleSections)
		{
			delete[] m_ModuleSections;
			m_ModuleSections = nullptr;
		}
	}

	// In case you are facing custom section names, just call this.
	void LoadSections(const char* const szExecuteable, const char* const szExeception, const char* const szRunTime, const char* const szReadOnly)
	{
		m_ExecutableCode = GetSectionByName(szExecuteable);
		m_ExceptionTable = GetSectionByName(szExeception);
		m_RunTimeData	 = GetSectionByName(szRunTime);
		m_ReadOnlyData	 = GetSectionByName(szReadOnly);
	}

	CMemory FindPatternSIMD(const uint8_t* const pPattern, const char* const szMask, const ModuleSections_t* const moduleSection = nullptr, const size_t nOccurrence = 0u) const
	{
		if (!m_ExecutableCode->IsSectionValid())
			return CMemory();

		const bool bSectionValid = moduleSection ? moduleSection->IsSectionValid() : false;
		const uintptr_t nBase = bSectionValid ? moduleSection->m_pSectionBase : m_ExecutableCode->m_pSectionBase;
		const uintptr_t nSize = bSectionValid ? moduleSection->m_nSectionSize : m_ExecutableCode->m_nSectionSize;

		const size_t nMaskLen = strlen(szMask);
		uint8_t* pData = reinterpret_cast<uint8_t*>(nBase);
		const uint8_t* const pEnd = pData + nSize - nMaskLen;

		size_t nOccurrenceCount = 0u;
		int nMasks[64]; // 64*16 = enough masks for 1024 bytes.
		const int iNumMasks = static_cast<int>(nMaskLen + 15) / 16;

		memset(nMasks, '\0', iNumMasks * sizeof(int));
		for (intptr_t i = 0; i < iNumMasks; ++i)
		{
			for (intptr_t j = strnlen(szMask + i * 16, 16) - 1; j >= 0; --j)
			{
				if (szMask[i * 16 + j] == 'x')
				{
					_bittestandset(reinterpret_cast<LONG*>(&nMasks[i]), static_cast<LONG>(j));
				}
			}
		}
		const __m128i xmm1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pPattern));
		__m128i xmm2, xmm3, msks;
		for (; pData != pEnd; _mm_prefetch(reinterpret_cast<const char*>(++pData + 64), _MM_HINT_NTA))
		{
			if (pPattern[0] == pData[0])
			{
				xmm2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pData));
				msks = _mm_cmpeq_epi8(xmm1, xmm2);
				if ((_mm_movemask_epi8(msks) & nMasks[0]) == nMasks[0])
				{
					for (uintptr_t i = 1; i < static_cast<uintptr_t>(iNumMasks); ++i)
					{
						xmm2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>((pData + i * 16)));
						xmm3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>((pPattern + i * 16)));
						msks = _mm_cmpeq_epi8(xmm2, xmm3);
						if ((_mm_movemask_epi8(msks) & nMasks[i]) == nMasks[i])
						{
							if ((i + 1) == static_cast<uintptr_t>(iNumMasks))
							{
								if (nOccurrenceCount == nOccurrence)
								{
									return static_cast<CMemory>(const_cast<uint8_t*>(pData));
								}
								nOccurrenceCount++;
							}
						}
						else
						{
							goto cont;
						}
					}
					if (nOccurrenceCount == nOccurrence)
					{
						return static_cast<CMemory>((&*(pData)));
					}
					nOccurrenceCount++;
				}
			}cont:;
		}
		return CMemory();
	}

	CMemory FindPatternSIMD(const char* szPattern, const ModuleSections_t* moduleSection = nullptr, const size_t nOccurrence = 0u) const
	{
		const std::pair<std::vector<uint8_t>, std::string> patternInfo = CMemory::PatternToMaskedBytes(szPattern);
		return FindPatternSIMD(patternInfo.first.data(), patternInfo.second.c_str(), moduleSection, nOccurrence);
	}

	CMemory FindString(const char* const szString, const ptrdiff_t nOccurrence = 1, bool bNullTerminator = false) const
	{
		if (!m_ExecutableCode->IsSectionValid())
			return CMemory();

		// Get address for the string in the .rdata section.
		const CMemory stringAddress = FindStringReadOnly(szString, bNullTerminator);
		if (!stringAddress)
			return CMemory();

		uint8_t* pLatestOccurrence = nullptr;
		uint8_t* const pTextStart = reinterpret_cast<uint8_t*>(m_ExecutableCode->m_pSectionBase);
		ptrdiff_t dOccurrencesFound = 0;

		for (size_t i = 0ull; i < m_ExecutableCode->m_nSectionSize - 0x5; i++)
		{
			byte byte = pTextStart[i];
			if (byte == 0x8D) // 0x8D = LEA
			{
				const CMemory skipOpCode = CMemory(reinterpret_cast<uintptr_t>(&pTextStart[i])).OffsetSelf(0x2); // Skip next 2 opcodes, those being the instruction and the register.
				const int32_t relativeAddress = skipOpCode.GetValue<int32_t>();
				const uintptr_t nextInstruction = skipOpCode.Offset(0x4).GetPtr();
				const CMemory potentialLocation = CMemory(nextInstruction + relativeAddress);

				if (potentialLocation == stringAddress)
				{
					dOccurrencesFound++;
					if (nOccurrence == dOccurrencesFound)
					{
						return CMemory(&pTextStart[i]);
					}

					pLatestOccurrence = &pTextStart[i]; // Stash latest occurrence.
				}
			}
		}

		return CMemory(pLatestOccurrence);
	}

	CMemory FindStringReadOnly(const char* const szString, const bool bNullTerminator) const
	{
		if (!m_ReadOnlyData->IsSectionValid())
			return CMemory();

		const std::vector<int> vBytes = CMemory::StringToBytes(szString, bNullTerminator);
		const std::pair<size_t, const int*> bytesInfo = std::make_pair<size_t, const int*>(vBytes.size(), vBytes.data());

		const uint8_t* const pBase = reinterpret_cast<uint8_t*>(m_ReadOnlyData->m_pSectionBase);

		for (size_t i = 0ull; i < m_ReadOnlyData->m_nSectionSize - bytesInfo.first; i++)
		{
			bool bFound = true;

			// If either the current byte equals to the byte in our pattern or our current byte in the pattern is a wildcard
			// our if clause will be false.
			for (size_t j = 0ull; j < bytesInfo.first; j++)
			{
				if (pBase[i + j] != bytesInfo.second[j] && bytesInfo.second[j] != -1)
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
			{
				return CMemory(&pBase[i]);
			}
		}

		return CMemory();
	}

	CMemory FindFreeDataPage(const size_t nSize) const
	{
		auto checkDataSection = [](const void* address, const std::size_t size)
		{
			using namespace silverbun;

			MEMORY_BASIC_INFORMATION membInfo = { 0 };

			CallQVM(address, &membInfo, sizeof(membInfo));

			if (membInfo.AllocationBase && membInfo.BaseAddress && membInfo.State == MEM_COMMIT && !(membInfo.Protect & PAGE_GUARD) && membInfo.Protect != PAGE_NOACCESS)
			{
				if ((membInfo.Protect & (PAGE_EXECUTE_READWRITE | PAGE_READWRITE)) && membInfo.RegionSize >= size)
				{
					return ((membInfo.Protect & (PAGE_EXECUTE_READWRITE | PAGE_READWRITE)) && membInfo.RegionSize >= size) ? true : false;
				}
			}
			return false;
		};

		// This is very unstable, this doesn't check for the actual 'page' sizes.
		// Also can be optimized to search per 'section'.
		const uintptr_t endOfModule = m_pModuleBase + m_nModuleSize - sizeof(uintptr_t);
		for (uintptr_t currAddr = endOfModule; m_pModuleBase < currAddr; currAddr -= sizeof(uintptr_t))
		{
			if (*reinterpret_cast<uintptr_t*>(currAddr) == 0 && checkDataSection(reinterpret_cast<void*>(currAddr), nSize))
			{
				bool bIsGoodPage = true;
				uint32_t nPageCount = 0;

				for (; nPageCount < nSize && bIsGoodPage; nPageCount += sizeof(uintptr_t))
				{
					const uintptr_t pageData = *reinterpret_cast<std::uintptr_t*>(currAddr + nPageCount);
					if (pageData != 0)
						bIsGoodPage = false;
				}

				if (bIsGoodPage && nPageCount >= nSize)
					return currAddr;
			}
		}

		return CMemory();
	}

	CMemory GetVirtualMethodTable(const char* const szTableName, const size_t nRefIndex = 0) const
	{
		using namespace silverbun;

		if (!m_ReadOnlyData->IsSectionValid())
			return CMemory();

		ModuleSections_t moduleSection(".data", m_RunTimeData->m_pSectionBase, m_RunTimeData->m_nSectionSize);

		const auto tableNameInfo = CMemory::StringToMaskedBytes(szTableName, false);

#if _WIN64
		const CMemory rttiTypeDescriptor = FindPatternSIMD(tableNameInfo.first.data(), tableNameInfo.second.c_str(), &moduleSection).OffsetSelf(-0x10);
#else
		const CMemory rttiTypeDescriptor = FindPatternSIMD(tableNameInfo.first.data(), tableNameInfo.second.c_str(), &moduleSection).OffsetSelf(-0x8);
#endif // _WIN64

		if (!rttiTypeDescriptor)
			return CMemory();

		uintptr_t scanStart = m_ReadOnlyData->m_pSectionBase; // Get the start address of our scan.

		const uintptr_t scanEnd = (m_ReadOnlyData->m_pSectionBase + m_ReadOnlyData->m_nSectionSize); // Calculate the end of our scan.

#if _WIN64
		const uintptr_t rttiTDRva = rttiTypeDescriptor.GetPtr() - m_pModuleBase; // The RTTI gets referenced by a 4-Byte RVA address. We need to scan for that address.
#else
		const uintptr_t rttiTDRva = rttiTypeDescriptor.GetPtr();
#endif

		while (scanStart < scanEnd)
		{
			moduleSection = { ".rdata", scanStart, static_cast<size_t>(scanEnd - scanStart) };
			const CMemory reference = FindPatternSIMD(reinterpret_cast<rsig_t>(&rttiTDRva), "xxxx", &moduleSection, nRefIndex);
			if (!reference)
				break;

			// If the offset is not 0, it means the vtable belongs to a base class, not the class we want.
			// TODO: Look into how to utilize this offset.
			const uint32_t vtableOffset = reference.Offset(-0x8).GetValue<uint32_t>();
			if (vtableOffset != 0)
			{
				scanStart = reference.Offset(0x4).GetPtr();
				continue;
			}

			CMemory referenceOffset = reference.Offset(-0xC);
			if (referenceOffset.GetValue<uint32_t>() != COL_SIG_REV) // Check if we got a RTTI Object Locator for this reference by checking if -0xC is 1, which is the 'signature' field.
			{
				scanStart = reference.Offset(0x4).GetPtr();
				continue;
			}

			moduleSection = { ".rdata", m_ReadOnlyData->m_pSectionBase, m_ReadOnlyData->m_nSectionSize };

			// TODO: get rid of mask through another method.
#if _WIN64
			return FindPatternSIMD(reinterpret_cast<rsig_t>(&referenceOffset), "xxxxxxxx", &moduleSection).OffsetSelf(sizeof(uintptr_t));
#else
			return FindPatternSIMD(reinterpret_cast<rsig_t>(&referenceOffset), "xxxx", &moduleSection).OffsetSelf(sizeof(uintptr_t));
#endif
		}

		return CMemory();
	}

	CMemory GetImportedFunction(const char* const szModuleName, const char* const szFunctionName, const bool bGetFunctionReference) const
	{
		// Get the location of IMAGE_IMPORT_DESCRIPTOR for this module by adding the IMAGE_DIRECTORY_ENTRY_IMPORT relative virtual address onto our module base address.
		IMAGE_IMPORT_DESCRIPTOR* const pImageImportDescriptors = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR* const>(m_pModuleBase + m_pNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		if (!pImageImportDescriptors)
			return CMemory();

		for (IMAGE_IMPORT_DESCRIPTOR* pIID = pImageImportDescriptors; pIID->Name != 0; pIID++)
		{
			// Get virtual relative Address of the imported module name. Then add module base Address to get the actual location.
			const char* const szImportedModuleName = reinterpret_cast<char*>(reinterpret_cast<DWORD*>(m_pModuleBase + pIID->Name));

			if (_stricmp(szImportedModuleName, szModuleName) == 0)
			{
				// Original first thunk to get function name.
				IMAGE_THUNK_DATA* pOgFirstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(m_pModuleBase + pIID->OriginalFirstThunk);

				// To get actual function address.
				IMAGE_THUNK_DATA* pFirstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(m_pModuleBase + pIID->FirstThunk);
				for (; pOgFirstThunk->u1.AddressOfData; ++pOgFirstThunk, ++pFirstThunk)
				{
					// Get image import by name.
					const IMAGE_IMPORT_BY_NAME* pImageImportByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(m_pModuleBase + pOgFirstThunk->u1.AddressOfData);

					if (_stricmp(pImageImportByName->Name, szFunctionName) == 0)
					{
						// Grab function address from firstThunk.
					#if defined( _WIN64 )
						uintptr_t* pFunctionAddress = &pFirstThunk->u1.Function;
					#else
						uintptr_t* pFunctionAddress = reinterpret_cast<uintptr_t*>(&pFirstThunk->u1.Function);
					#endif // #if defined( _WIN64 

						// Reference or address?
						return bGetFunctionReference ? CMemory(pFunctionAddress) : CMemory(*pFunctionAddress);
					}
				}

			}
		}
		return CMemory();
	}

	CMemory GetExportedFunction(const char* szFunctionName) const
	{
		const IMAGE_EXPORT_DIRECTORY* pImageExportDirectory = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(m_pModuleBase + m_pNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
		if (!pImageExportDirectory)
			return CMemory();

		if (!pImageExportDirectory->NumberOfFunctions)
			return CMemory();

		const DWORD* const pAddressOfFunctions = reinterpret_cast<DWORD*>(m_pModuleBase + pImageExportDirectory->AddressOfFunctions);
		if (!pAddressOfFunctions)
			return CMemory();

		const DWORD* const pAddressOfName = reinterpret_cast<DWORD*>(m_pModuleBase + pImageExportDirectory->AddressOfNames);
		if (!pAddressOfName)
			return CMemory();

		DWORD* const pAddressOfOrdinals = reinterpret_cast<DWORD*>(m_pModuleBase + pImageExportDirectory->AddressOfNameOrdinals);
		if (!pAddressOfOrdinals)
			return CMemory();

		for (DWORD i = 0; i < pImageExportDirectory->NumberOfNames; i++)
		{
			const char* const szExportFunctionName = reinterpret_cast<char*>(reinterpret_cast<DWORD*>(m_pModuleBase + pAddressOfName[i]));
			if (_stricmp(szExportFunctionName, szFunctionName) == 0)
			{
				return CMemory(m_pModuleBase + pAddressOfFunctions[reinterpret_cast<WORD*>(pAddressOfOrdinals)[i]]);
			}
		}

		return CMemory();
	}

	ModuleSections_t* const GetSectionByName(const char* const szSectionName) const
	{
		for (uint16_t i = 0; i < m_nNumSections; i++)
		{
			ModuleSections_t* const pSection = &m_ModuleSections[i];
			if (strcmp(szSectionName, pSection->m_SectionName) == 0)
				return pSection;
		}

		return nullptr;
	}
	inline ModuleSections_t* const GetSections() const { return m_ModuleSections; }
	inline ModuleSections_t* const GetExecuteableSection() const { return m_ExecutableCode; };
	inline ModuleSections_t* const GetExceptionTableSection() const { return m_ExceptionTable; };
	inline ModuleSections_t* const GetRunTimeDataSection() const { return m_RunTimeData; };
	inline ModuleSections_t* const GetReadOnlyDataSection() const { return m_ReadOnlyData; };

	inline uintptr_t GetModuleBase() const { return m_pModuleBase; }
	inline DWORD GetModuleSize() const { return m_nModuleSize; }
	inline const char* const GetModuleName() const { return m_ModuleName; }
	inline uintptr_t GetRVA(const uintptr_t nAddress) const { return (nAddress - m_pModuleBase); }

#if !defined( USE_WINAPI_FOR_MODULE_NAME )
	static __declspec(noinline) void GetModuleNameFromLdr(const uintptr_t moduleBase, char* const szModuleName, const size_t nModuleNameSize)
	{
		using namespace silverbun;

	#if defined( _WIN64 )
		const PEB_S* const pPeb = reinterpret_cast<const PEB_S* const>(__readgsqword(pebOffset));
	#else
		const PEB_S* const pPeb = reinterpret_cast<const PEB_S* const>(__readfsdword(pebOffset));
	#endif // #if defined( _WIN64 )

		const LIST_ENTRY* const pInLoadOrderList = &pPeb->Ldr->InLoadOrderModuleList;
		for (LIST_ENTRY* entry = pInLoadOrderList->Flink; entry != pInLoadOrderList; entry = entry->Flink)
		{
			const LDR_DATA_TABLE_ENTRY_S* const pLdrEntry = reinterpret_cast<const LDR_DATA_TABLE_ENTRY_S* const>(entry->Flink);
			const uintptr_t ldrModuleBase = reinterpret_cast<uintptr_t>(pLdrEntry->DllBase);
			if (!ldrModuleBase) // Pretty sure happens if module hasn't loaded fully yet.
				continue;

			if (ldrModuleBase != moduleBase)
				continue;

			const UNICODE_STRING_T<uintptr_t>* const winBaseDllName = &pLdrEntry->BaseDllName;
			const wchar_t* const wszDllName = reinterpret_cast<const wchar_t* const>(winBaseDllName->Buffer);

			const int nBytesWritten = CallWCTMB(CP_UTF8, 0, wszDllName, (winBaseDllName->u.Length / sizeof(wchar_t)), szModuleName, static_cast<int>(nModuleNameSize), nullptr, nullptr);
			if (nBytesWritten <= 0 || nBytesWritten >= static_cast<int>(nModuleNameSize)) // Should never go above >=, still >= for easier comparison sake.
			{
				// See if we wrote all the bytes and if the last character is a null terminator.
				if (!(nBytesWritten == static_cast<int>(nModuleNameSize) && szModuleName[nModuleNameSize - 1] == '\0'))
				{
					*szModuleName = '\0';
				}

				break;
			}
			else
			{
				szModuleName[nBytesWritten] = '\0';
				break;
			}
		}
	}
#else
	static __declspec(noinline) void GetModuleNameFromWinAPI(const uintptr_t moduleBase, char* const szModuleName, const size_t nModuleNameSize)
	{
		const uint32_t nBytesWritten = GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase), szModuleName, static_cast<uint32_t>(nModuleNameSize));
		if (nBytesWritten == 0)
			abort();

		// Locate last path separator.
		const char* szFileName = strrchr(szModuleName, '\\');
		if (!szFileName)
			szFileName = strrchr(szModuleName, '/');

		// If we found a path separator, move over the memory for the module name. Otherwise we already got the full dll name.
		if (szFileName)
		{
			szFileName++; // Skip the separator.
			const ptrdiff_t stringPtrDiff = szFileName - szModuleName;
			const size_t nStringSizeWithNullTerminator = strnlen_s(szFileName, (nModuleNameSize - stringPtrDiff)) + 1;

			memmove(szModuleName, szFileName, nStringSizeWithNullTerminator);
			szFileName = nullptr; // szFileName points to nothing now, lets just reset.
			memset(szModuleName + nStringSizeWithNullTerminator, '\0', nModuleNameSize - nStringSizeWithNullTerminator); // Set anything past the name back to 0.
		}
	}
#endif // #if !defined( USE_WINAPI_FOR_MODULE_NAME )

private:
	void ParsePEHdr()
	{
		m_pDOSHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(m_pModuleBase);
		m_pNTHeaders = reinterpret_cast<decltype(m_pNTHeaders)>(m_pModuleBase + m_pDOSHeader->e_lfanew);
		m_nModuleSize = m_pNTHeaders->OptionalHeader.SizeOfImage;

		// Populate sections now from NT Header.
		const IMAGE_SECTION_HEADER* const pFirstSection = IMAGE_FIRST_SECTION(m_pNTHeaders);
		m_nNumSections = m_pNTHeaders->FileHeader.NumberOfSections;

		if (m_nNumSections != 0)
		{
			m_ModuleSections = new ModuleSections_t[m_nNumSections];
			for (WORD i = 0; i < m_nNumSections; i++)
			{
				const IMAGE_SECTION_HEADER* const pCurrentSection = &pFirstSection[i];

				ModuleSections_t* const pSection = &m_ModuleSections[i];
				pSection->m_SectionName = reinterpret_cast<const char* const>(pCurrentSection->Name); // Should never move, getting a pointer here will do the trick.
				pSection->m_pSectionBase = static_cast<uintptr_t>(m_pModuleBase + pCurrentSection->VirtualAddress);
				pSection->m_nSectionSize = pCurrentSection->Misc.VirtualSize;
			}
		}
	}

#if defined( _WIN64 )
	IMAGE_NT_HEADERS64* m_pNTHeaders;
#else
	IMAGE_NT_HEADERS32* m_pNTHeaders;
#endif // #if defined( _WIN64 ) 
	IMAGE_DOS_HEADER* m_pDOSHeader;

	ModuleSections_t* m_ExecutableCode;
	ModuleSections_t* m_ExceptionTable;
	ModuleSections_t* m_RunTimeData;
	ModuleSections_t* m_ReadOnlyData;

	char m_ModuleName[MAX_PATH]; // 260 should suffice.
	uintptr_t m_pModuleBase;
	DWORD m_nModuleSize;
	WORD m_nNumSections;
	ModuleSections_t* m_ModuleSections;
};
