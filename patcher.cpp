// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include "core.h"

#include "load.h"
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"

#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "load.h"
#include "MinHook.h"
#pragma intrinsic(_ReturnAddress)

struct PatchInstruction {
    PatchInstruction() = default;
    PatchInstruction(std::string moduleName, std::string sectionName, long long offset, std::string originalBytes, std::string newBytes) {
        this->moduleName = moduleName;
        this->sectionName = "." + sectionName;
        this->offset = offset;
        this->originalBytes = std::vector<unsigned char>(originalBytes.begin(), originalBytes.end());
        this->newBytes = std::vector<unsigned char>(newBytes.begin(), newBytes.end());
        this->lineNumber = 666;
        this->fileName = "<native>";
    }
    std::string moduleName;
    std::string sectionName;
    long long offset; // For 64-bit compatibility
    std::vector<unsigned char> originalBytes;
    std::vector<unsigned char> newBytes;
    int lineNumber;
    std::string fileName;
};

// Helper function to convert hex string to bytes

std::vector<unsigned char> hexStringToBytes(const std::string& hexStr) {
    ZoneScoped;

    std::vector<unsigned char> bytes;
    std::istringstream hexStream(hexStr);
    std::string byteString;

    while (std::getline(hexStream, byteString, ' ')) {
        if (!byteString.empty()) {
            // Convert the hex string (e.g., "74") to an integer using base 16
            int byteValue = std::stoi(byteString, nullptr, 16);
            // Cast the integer to unsigned char and add it to the bytes vector
            bytes.push_back(static_cast<unsigned char>(byteValue));
        }
    }

    return bytes;
}


// Helper function to parse byte string, handling escape sequences
std::vector<unsigned char> parseByteString(const std::string& byteStr) {
    ZoneScoped;
    
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < byteStr.length(); ++i) {
        if (byteStr[i] == '\\' && i + 1 < byteStr.length() && byteStr[i + 1] == 'x') {
            std::string hexStr = byteStr.substr(i + 2, 2);
            i += 3; // Skip '\x' and two hex digits
            bytes.push_back((unsigned char)std::stoul(hexStr, nullptr, 16));
        }
        else {
            bytes.push_back(byteStr[i]);
        }
    }
    return bytes;
}

std::vector<PatchInstruction> ParsePatchFile(const std::string& filePath) {
    ZoneScoped;
    
    std::vector<PatchInstruction> instructions;
    std::ifstream file(filePath);
    std::string line;
    int lineNumber = 0;

    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return instructions; // Return empty vector on file open failure
    }

    while (getline(file, line)) {
        ++lineNumber;
        // Ignore empty lines or comments
        if (line.empty() || line[0] == ';') continue;

        std::istringstream iss(line);
        std::string moduleName, sectionName, offsetHex, originalBytesStr, newBytesStr;

        getline(iss, moduleName, ',');
        getline(iss, sectionName, ',');
        getline(iss, offsetHex, ',');
        getline(iss, originalBytesStr, ',');
        getline(iss, newBytesStr);

        PatchInstruction instruction;
        instruction.moduleName = moduleName;
        instruction.sectionName = "." + sectionName;
        instruction.offset = std::stoll(offsetHex, nullptr, 16); // Convert hex string to long long

        if (originalBytesStr.front() == '"' && originalBytesStr.back() == '"') {
            instruction.originalBytes = parseByteString(originalBytesStr.substr(1, originalBytesStr.length() - 2));
        }
        else {
            instruction.originalBytes = hexStringToBytes(originalBytesStr);
        }

        if (newBytesStr.front() == '"' && newBytesStr.back() == '"') {
            instruction.newBytes = parseByteString(newBytesStr.substr(1, newBytesStr.length() - 2));
        }
        else {
            instruction.newBytes = hexStringToBytes(newBytesStr);
        }
        instruction.lineNumber = lineNumber; // Set the line number in the instruction
        instruction.fileName = filePath;
        instructions.push_back(instruction);
    }

    file.close();
    return instructions;
}

std::string UnicodeToString(PCUNICODE_STRING unicodeString) {
    ZoneScoped;
    
    if (!unicodeString || !unicodeString->Buffer) return "";

    // Calculate the size required for the new string
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, unicodeString->Buffer, unicodeString->Length / sizeof(WCHAR), nullptr, 0, nullptr, nullptr);
    std::string strTo(sizeNeeded, 0);

    // Perform the conversion
    WideCharToMultiByte(CP_UTF8, 0, unicodeString->Buffer, unicodeString->Length / sizeof(WCHAR), &strTo[0], sizeNeeded, nullptr, nullptr);

    return strTo;
}

static void
Patch(void* addr, const std::vector<uint8_t>& bytes) {
    // TODO(mrsteyk): check for errors!

    DWORD old;
    VirtualProtect(addr, bytes.size(), PAGE_EXECUTE_READWRITE, &old);

    memcpy(addr, bytes.data(), bytes.size());

    VirtualProtect(addr, bytes.size(), old, &old);
}

bool ApplyPatch(const PatchInstruction& instruction, void* targetModuleBase) {
    ZoneScoped;
    
    // NOTE(mrsteyk): CModule was being constructed every single fucking time anyway
    auto mz = (PIMAGE_DOS_HEADER)targetModuleBase;
    auto pe = (PIMAGE_NT_HEADERS64)((uint8_t*)targetModuleBase + mz->e_lfanew);
    auto sections_num = pe->FileHeader.NumberOfSections;
    auto sections = IMAGE_FIRST_SECTION(pe);

    int section_valid = 0;
    auto mod_section_name = instruction.sectionName.c_str();
    auto mod_section_len = instruction.sectionName.length() + 1;

    if (mod_section_len <= 8) {
        for (size_t i = 0; i < sections_num; i++) {
            auto e = sections + i;
            if (!memcmp(e->Name, mod_section_name, mod_section_len)) {
                section_valid = 1;
                break;
            }
        }
    }

    // why the fuck do you even check a section if all you fucking do is use RVA from imagebase???
    if (!section_valid) {
        std::string errorMsg = "Invalid section name `" + instruction.sectionName + "`\nFile: " + instruction.fileName + "\nLine: " + std::to_string(instruction.lineNumber);
        MessageBoxA(nullptr, errorMsg.c_str(), "Patcher Error", MB_OK);
        return false;
    }

    uintptr_t base = (uintptr_t)targetModuleBase;
    //CMemory sectionMemory(base);

    //sectionMemory.OffsetSelf(instruction.offset);
    auto sectionMemory = (uint8_t*)base + instruction.offset;

    for (size_t i = 0; i < instruction.originalBytes.size(); ++i) {
        if (sectionMemory[i] != instruction.originalBytes[i]) {
            if (i == 0 && !memcmp(sectionMemory, instruction.newBytes.data(), instruction.newBytes.size()))
            {
                // NOTE(mrsteyk): already patched
                return true;
            }
            else
            {
                std::string errorMsg = "Original bytes do not match\nFile: " + instruction.fileName + "\nLine: " + std::to_string(instruction.lineNumber);
                MessageBoxA(nullptr, errorMsg.c_str(), "Patcher Error", MB_OK);
                return false;
            }
        }
    }

    Patch(sectionMemory, instruction.newBytes);

    return true;
}

std::vector<PatchInstruction> patchInstructions;

void initialisePatchInstructions() {
    ZoneScoped;

    // Get the base directory where the executable resides
    std::filesystem::path exeDir = GetExecutableDirectory();
    std::filesystem::path r1deltaBaseDir = exeDir / "r1delta";

    // Construct absolute paths for patch files
    std::filesystem::path mainPatchFile = r1deltaBaseDir / "r1delta.wpatch";
    std::filesystem::path dsPatchFile = r1deltaBaseDir / "r1delta_ds.wpatch";
    std::filesystem::path noOriginPatchFile = r1deltaBaseDir / "r1delta_noorigin.wpatch";

    // Parse the main patch file using its absolute path
    patchInstructions = ParsePatchFile(mainPatchFile.string());

    // If it's a dedicated server, also load and apply instructions from its absolute path
    if (IsDedicatedServer()) {
        std::vector<PatchInstruction> dedicatedServerPatchInstructions = ParsePatchFile(dsPatchFile.string());
        patchInstructions.insert(patchInstructions.end(), dedicatedServerPatchInstructions.begin(), dedicatedServerPatchInstructions.end());

        // This check uses a relative path "vpk/..." - leave as is
        if (!std::filesystem::exists("vpk/englishserver_mp_common.bsp.pak000_dir.vpk")) {
            // These paths "vpk/..." are left relative as requested
            patchInstructions.push_back(PatchInstruction("engine_ds.dll", "rdata", 0x422980, "vpk/server_", "vpk/client_\x00"));
            patchInstructions.push_back(PatchInstruction("engine_ds.dll", "rdata", 0x4229A8, "%sserver_%s%s", "%sclient_%s%s\x00"));
            patchInstructions.push_back(PatchInstruction("dedicated.dll", "rdata", 0x208E50, "vpk/server", "vpk/client"));
            patchInstructions.push_back(PatchInstruction("dedicated.dll", "rdata", 0x205AC8, "vpk/server", "vpk/client"));
        }
    }

    if (IsNoOrigin()) {
        // Load the no-origin patch file using its absolute path
        std::vector<PatchInstruction> noOriginPatchInstructions = ParsePatchFile(noOriginPatchFile.string());
        patchInstructions.insert(patchInstructions.end(), noOriginPatchInstructions.begin(), noOriginPatchInstructions.end());
    }
}

static inline int
instruction_compare_module_name(const PatchInstruction& ins, const PCUNICODE_STRING basename) {
    ZoneScoped;
    
    // NOTE(mrsteyk): this works on the assumption that all modules are non extended ASCII.
    
    auto len = ins.moduleName.length();
    if ((basename->Length / sizeof(basename->Buffer[0])) != len)
        return 0;

    auto p = basename->Buffer;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != ins.moduleName[i])
            return 0;
    }

    return 1;
}

void doBinaryPatchForFile(LDR_DLL_LOADED_NOTIFICATION_DATA data) {
    ZoneScoped;
    
    //std::cout << "Started patching for " << moduleName << std::endl;

    for (const auto& instruction : patchInstructions) {
        if (instruction_compare_module_name(instruction, data.BaseDllName)) {
            ApplyPatch(instruction, data.DllBase);
        }
    }

    //std::cout << "Completed patching for " << moduleName << std::endl;
}