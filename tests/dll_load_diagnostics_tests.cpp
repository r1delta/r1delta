#include "../shared/dll_load_diagnostics.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace r1d_dllload;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++g_failures;
	}
}

bool Contains(const std::wstring& haystack, const std::wstring& needle)
{
	return haystack.find(needle) != std::wstring::npos;
}

// Writes a minimal 64-bit PE image whose only content is an import table naming 'imports'.
void WriteImportingImage(const std::wstring& path, const std::vector<std::string>& imports)
{
	constexpr DWORD kSectionRva = 0x1000;
	constexpr DWORD kSectionFileOffset = 0x200;

	std::vector<unsigned char> section;
	const size_t descriptorBytes = (imports.size() + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
	section.resize(descriptorBytes, 0);
	std::vector<DWORD> nameRvas;
	for (const std::string& name : imports) {
		nameRvas.push_back(kSectionRva + static_cast<DWORD>(section.size()));
		section.insert(section.end(), name.begin(), name.end());
		section.push_back(0);
	}
	for (size_t i = 0; i < imports.size(); ++i) {
		IMAGE_IMPORT_DESCRIPTOR desc{};
		desc.Name = nameRvas[i];
		std::memcpy(section.data() + i * sizeof(desc), &desc, sizeof(desc));
	}

	IMAGE_DOS_HEADER dos{};
	dos.e_magic = IMAGE_DOS_SIGNATURE;
	dos.e_lfanew = sizeof(IMAGE_DOS_HEADER);

	IMAGE_NT_HEADERS64 nt{};
	nt.Signature = IMAGE_NT_SIGNATURE;
	nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
	nt.FileHeader.NumberOfSections = 1;
	nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
	nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
	nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = kSectionRva;
	nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = static_cast<DWORD>(descriptorBytes);

	IMAGE_SECTION_HEADER sec{};
	std::memcpy(sec.Name, ".idata", 6);
	sec.VirtualAddress = kSectionRva;
	sec.Misc.VirtualSize = static_cast<DWORD>(section.size());
	sec.SizeOfRawData = static_cast<DWORD>(section.size());
	sec.PointerToRawData = kSectionFileOffset;

	std::vector<unsigned char> image(kSectionFileOffset + section.size(), 0);
	std::memcpy(image.data(), &dos, sizeof(dos));
	std::memcpy(image.data() + dos.e_lfanew, &nt, sizeof(nt));
	std::memcpy(image.data() + dos.e_lfanew + sizeof(nt), &sec, sizeof(sec));
	std::memcpy(image.data() + kSectionFileOffset, section.data(), section.size());

	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
}

std::wstring MakeScratchDirectory()
{
	wchar_t temp[MAX_PATH];
	GetTempPathW(MAX_PATH, temp);
	std::wstring dir = JoinPath(temp, L"r1d_dllload_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

void RemoveTree(const std::wstring& dir)
{
	std::wstring pattern = JoinPath(dir, L"*");
	WIN32_FIND_DATAW find;
	HANDLE h = FindFirstFileW(pattern.c_str(), &find);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::wstring name = find.cFileName;
			if (name == L"." || name == L"..")
				continue;
			const std::wstring child = JoinPath(dir, name);
			if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveTree(child);
			else
				DeleteFileW(child.c_str());
		} while (FindNextFileW(h, &find));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

void TestIsApplicationControlBlock()
{
	Check(IsApplicationControlBlock(4551), "4551 (system integrity policy violation) is an application control block");
	Check(IsApplicationControlBlock(1260), "1260 (disabled by policy) is an application control block");
	Check(!IsApplicationControlBlock(126), "126 is not an application control block");
	Check(!IsApplicationControlBlock(193), "193 is not an application control block");
}

void TestClassifyImport()
{
	Check(ClassifyImport(L"vstdlib.dll") == ImportKind::GameFile, "vstdlib.dll is a game file");
	Check(ClassifyImport(L"tier0.dll") == ImportKind::GameFile, "tier0.dll is a game file");
	Check(ClassifyImport(L"VCRUNTIME140.dll") == ImportKind::VcRuntime, "VCRUNTIME140.dll is a Visual C++ runtime file");
	Check(ClassifyImport(L"MSVCP140.dll") == ImportKind::VcRuntime, "MSVCP140.dll is a Visual C++ runtime file");
	Check(ClassifyImport(L"d3dx9_43.dll") == ImportKind::DirectXRuntime, "d3dx9_43.dll is a DirectX runtime file");
	Check(ClassifyImport(L"XINPUT1_3.dll") == ImportKind::DirectXRuntime, "XINPUT1_3.dll is a DirectX runtime file");
}

void TestReadPeImportNames()
{
	const std::wstring dir = MakeScratchDirectory();
	const std::wstring image = JoinPath(dir, L"launcher.dll");
	WriteImportingImage(image, { "vstdlib.dll", "KERNEL32.dll" });
	std::vector<std::wstring> names;
	Check(ReadPeImportNames(image, names), "a well-formed image is read");
	Check(names.size() == 2 && names[0] == L"vstdlib.dll" && names[1] == L"KERNEL32.dll", "import names are returned in table order");

	const std::wstring notPe = JoinPath(dir, L"notpe.dll");
	{
		std::ofstream out(notPe, std::ios::binary);
		out << "this is not a portable executable image, only text that is long enough to pass the size check....";
	}
	std::vector<std::wstring> none;
	Check(!ReadPeImportNames(notPe, none), "a file that is not a PE image is rejected");
	Check(!ReadPeImportNames(JoinPath(dir, L"absent.dll"), none), "a file that does not exist is rejected");
	RemoveTree(dir);
}

// The report from the field: content on D:\ lost its bin\x64_retail folder; launcher.dll exists in r1delta\bin and
// imports vstdlib.dll, so LoadLibrary failed with 126 and the message blamed the Visual C++ runtime.
void TestMissingRetailLibraryNamesTheFile()
{
	const std::wstring root = MakeScratchDirectory();
	const std::wstring game = JoinPath(root, L"game");
	const std::wstring r1deltaBin = JoinPath(JoinPath(root, L"r1delta"), L"bin");
	CreateDirectoryW(game.c_str(), nullptr);
	CreateDirectoryW(JoinPath(root, L"r1delta").c_str(), nullptr);
	CreateDirectoryW(r1deltaBin.c_str(), nullptr);
	const std::wstring launcherDll = JoinPath(r1deltaBin, L"launcher.dll");
	WriteImportingImage(launcherDll, { "KERNEL32.dll", "vstdlib.dll" });

	Diagnosis d = Diagnose(kErrorModNotFound, L"launcher.dll", launcherDll, game);
	d.systemMessage = L"The specified module could not be found.";
	Check(d.libraryExists, "the launcher library is detected as present");
	Check(d.missingImport == L"vstdlib.dll", "the missing import is vstdlib.dll");
	Check(d.importChain == L"launcher.dll", "the importer chain names launcher.dll");
	Check(!d.gameBinExists, "the absent bin\\x64_retail folder is detected");

	const std::wstring expectedPath = JoinPath(GameBinDir(game), L"vstdlib.dll");
	const std::wstring text = FormatMessageText(d);
	Check(Contains(text, L"Missing file: vstdlib.dll"), "the message names the missing file");
	Check(Contains(text, expectedPath), "the message gives the exact path the file should have");
	Check(Contains(text, L"bin\\x64_retail folder is missing"), "the message says the whole folder is missing");
	Check(Contains(text, L"Verify or repair your Titanfall install"), "the message tells the user to verify or repair Titanfall");
	Check(Contains(text, L"holding F4"), "the message points installer users at setup");
	Check(!Contains(text, L"vc_redist"), "the message does not send the user to the Visual C++ download for a game file");

	// The folder exists but the file is gone.
	CreateDirectoryW(JoinPath(game, L"bin").c_str(), nullptr);
	CreateDirectoryW(GameBinDir(game).c_str(), nullptr);
	d = Diagnose(kErrorModNotFound, L"launcher.dll", launcherDll, game);
	d.systemMessage = L"The specified module could not be found.";
	Check(d.gameBinExists && d.missingImport == L"vstdlib.dll" && !d.importExistsInGameBin, "folder present, file absent");
	Check(Contains(FormatMessageText(d), L"the file is gone from it"), "the message distinguishes a missing file from a missing folder");

	// The file exists in the game folder but Windows was not pointed at it.
	{
		std::ofstream out(expectedPath, std::ios::binary);
		out << "x";
	}
	d = Diagnose(kErrorModNotFound, L"launcher.dll", launcherDll, game);
	d.systemMessage = L"The specified module could not be found.";
	Check(d.importExistsInGameBin, "an existing retail file that is not on the search path is detected");
	Check(Contains(FormatMessageText(d), L"Start R1Delta through its launcher"), "the message explains the search path problem");

	RemoveTree(root);
}

void TestMissingVcRuntimeStillGetsRuntimeAdvice()
{
	const std::wstring root = MakeScratchDirectory();
	const std::wstring game = JoinPath(root, L"game");
	CreateDirectoryW(game.c_str(), nullptr);
	const std::wstring dll = JoinPath(root, L"launcher.dll");
	WriteImportingImage(dll, { "msvcp140_r1d_test_absent.dll" });

	Diagnosis d = Diagnose(kErrorModNotFound, L"launcher.dll", dll, game);
	d.systemMessage = L"The specified module could not be found.";
	Check(d.missingImport == L"msvcp140_r1d_test_absent.dll", "the missing runtime import is found");
	const std::wstring text = FormatMessageText(d);
	Check(Contains(text, L"Microsoft Visual C++ Redistributable"), "a missing runtime library gets the Visual C++ advice");
	Check(!Contains(text, L"Titanfall game file"), "a missing runtime library is not blamed on the game files");
	RemoveTree(root);
}

void TestPolicyBlockNamesTheFile()
{
	Diagnosis d;
	d.errorCode = kErrorSystemIntegrityPolicyViolation;
	d.libName = L"launcher.dll";
	d.location = L"C:\\Users\\pilot\\AppData\\Local\\R1Delta\\app-3.0.5\\r1delta\\bin\\launcher.dll";
	d.systemMessage = L"An Application Control policy has blocked this file.";
	d.libraryExists = true;
	const std::wstring text = FormatMessageText(d);
	Check(Contains(text, d.location), "the policy block message names the blocked file");
	Check(Contains(text, L"An Application Control policy has blocked this file."), "the policy block message carries Windows' own text");
	Check(Contains(text, L"Smart App Control"), "the policy block message names Smart App Control");
	Check(Contains(text, L"error 4551"), "the policy block message carries the error code");
	Check(!Contains(text, L"Visual C++"), "a policy block is not blamed on the runtime");
}

void TestUnrelatedErrorsKeepTheExtractionHints()
{
	Diagnosis d;
	d.errorCode = 2;
	d.libName = L"tier0.dll";
	d.location = L"C:\\Titanfall\\r1delta\\bin_delta\\tier0.dll";
	d.systemMessage = L"The system cannot find the file specified.";
	d.titanfallExeInWorkingDir = false;
	d.titanfallExeInParentDir = true;
	d.workingDirName = L"r1delta_extract";
	d.parentDirName = L"Titanfall";
	Check(Contains(FormatMessageText(d), L"subdirectory of your Titanfall installation"), "an extraction inside a subfolder is still reported");

	d.titanfallExeInParentDir = false;
	Check(Contains(FormatMessageText(d), L"unpack the contents of the R1Delta archive"), "a folder without Titanfall.exe is still reported");
}

} // namespace

int main()
{
	TestIsApplicationControlBlock();
	TestClassifyImport();
	TestReadPeImportNames();
	TestMissingRetailLibraryNamesTheFile();
	TestMissingVcRuntimeStillGetsRuntimeAdvice();
	TestPolicyBlockNamesTheFile();
	TestUnrelatedErrorsKeepTheExtractionHints();

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("dll_load_diagnostics_tests: all checks passed\n");
	return 0;
}
