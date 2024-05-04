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

#include "load.h"
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include "tier0_orig.h"
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
#include "thirdparty/silver-bun/memaddr.h"
#include "thirdparty/silver-bun/module.h"
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
    if (!unicodeString || !unicodeString->Buffer) return "";

    // Calculate the size required for the new string
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, unicodeString->Buffer, unicodeString->Length / sizeof(WCHAR), nullptr, 0, nullptr, nullptr);
    std::string strTo(sizeNeeded, 0);

    // Perform the conversion
    WideCharToMultiByte(CP_UTF8, 0, unicodeString->Buffer, unicodeString->Length / sizeof(WCHAR), &strTo[0], sizeNeeded, nullptr, nullptr);

    return strTo;
}

bool ApplyPatch(const PatchInstruction& instruction, CModule& targetModule) {
    CModule::ModuleSections_t modSection = targetModule.GetSectionByName(instruction.sectionName.c_str());
    if (!modSection.IsSectionValid()) {
        std::string errorMsg = "Invalid section name\nFile: " + instruction.fileName + "\nLine: " + std::to_string(instruction.lineNumber);
        MessageBoxA(nullptr, errorMsg.c_str(), "Patcher Error", MB_OK);
        return false;
    }

    uintptr_t base = targetModule.GetModuleBase();
    CMemory sectionMemory(base);

    sectionMemory.OffsetSelf(instruction.offset);

    for (size_t i = 0; i < instruction.originalBytes.size(); ++i) {
        if (*(sectionMemory.CCast<unsigned char*>() + i) != instruction.originalBytes[i]) {
            std::string errorMsg = "Original bytes do not match\nFile: " + instruction.fileName + "\nLine: " + std::to_string(instruction.lineNumber);
            MessageBoxA(nullptr, errorMsg.c_str(), "Patcher Error", MB_OK);
            return false;
        }
    }

    sectionMemory.Patch(instruction.newBytes);

    return true;
}


void doBinaryPatchForFile(LDR_DLL_LOADED_NOTIFICATION_DATA data) {
    // Initialize the target module with the base DLL name
    std::string moduleName = UnicodeToString(data.BaseDllName);
    std::cout << "Started patching for " << moduleName << std::endl;

    CModule targetModule(moduleName.c_str());
    targetModule.Init(); // Ensure module is initialized and sections are loaded
    targetModule.LoadSections();

    // Parse the patch file and get patch instructions
    std::vector<PatchInstruction> patchInstructions = ParsePatchFile("r1delta/r1delta.wpatch");

    // If it's a dedicated server, also load and apply instructions from "r1delta_ds.patch"
    if (IsDedicatedServer()) {
        std::vector<PatchInstruction> dedicatedServerPatchInstructions = ParsePatchFile("r1delta/r1delta_ds.wpatch");
        patchInstructions.insert(patchInstructions.end(), dedicatedServerPatchInstructions.begin(), dedicatedServerPatchInstructions.end());
    }

    for (const auto& instruction : patchInstructions) {
        if (instruction.moduleName == moduleName) {
            ApplyPatch(instruction, targetModule);
        }
    }

    std::cout << "Completed patching for " << moduleName << std::endl;
}