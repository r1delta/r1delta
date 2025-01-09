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
            std::string errorMsg = "Original bytes do not match\nFile: " + instruction.fileName + "\nLine: " + std::to_string(instruction.lineNumber);
            MessageBoxA(nullptr, errorMsg.c_str(), "Patcher Error", MB_OK);
            return false;
        }
    }

    Patch(sectionMemory, instruction.newBytes);

    return true;
}

std::vector<PatchInstruction> patchInstructions;

void
initialisePatchInstructions() {
    ZoneScoped;
    
    // Parse the patch file and get patch instructions
    patchInstructions = ParsePatchFile("r1delta/r1delta.wpatch");

    // If it's a dedicated server, also load and apply instructions from "r1delta_ds.patch"
    if (IsDedicatedServer()) {
        std::vector<PatchInstruction> dedicatedServerPatchInstructions = ParsePatchFile("r1delta/r1delta_ds.wpatch");
        patchInstructions.insert(patchInstructions.end(), dedicatedServerPatchInstructions.begin(), dedicatedServerPatchInstructions.end());
    }

    if (IsNoOrigin()) {
        std::vector<PatchInstruction> noOriginPatchInstructions = ParsePatchFile("r1delta/r1delta_noorigin.wpatch");
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