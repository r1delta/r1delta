using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32;

namespace TitanfallFinder
{
    public static class TitanfallLocator
    {
        // ========================
        // Prefetch Decompression
        // ========================
        private const ushort CompressionFormatXpressHuff = 4;

        [DllImport("ntdll.dll")]
        private static extern uint RtlGetCompressionWorkSpaceSize(
            ushort CompressionFormat,
            out ulong CompressBufferWorkSpaceSize,
            out ulong CompressFragmentWorkSpaceSize);

        [DllImport("ntdll.dll")]
        private static extern uint RtlDecompressBufferEx(
            ushort CompressionFormat,
            byte[] UncompressedBuffer,
            int UncompressedBufferSize,
            byte[] CompressedBuffer,
            int CompressedBufferSize,
            out int FinalUncompressedSize,
            byte[] WorkSpace);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern bool GetVolumeInformation(
            string rootPathName,
            StringBuilder volumeNameBuffer,
            int volumeNameSize,
            out uint volumeSerialNumber,
            out uint maximumComponentLength,
            out uint fileSystemFlags,
            StringBuilder fileSystemNameBuffer,
            int nFileSystemNameSize);

        // In-memory debugging log (optional)
        private static StringBuilder debugLog = new StringBuilder();

        // For searching both Titanfall and R1
        private static readonly string[] ExeNames = { "titanfall.exe", "r1delta.exe" };

        /// <summary>
        /// Public call to attempt to find either Titanfall or r1delta,
        /// while confirming that r1\gameinfo.txt has the expected MD5.
        /// Returns null if nothing validated.
        /// </summary>
        public static string FindTitanfallOrR1Delta(bool includeDebugLog = false)
        {
            debugLog.Clear();

            // Gather all "candidates" from various methods, then validate them:
            List<Tuple<string, string>> candidates = new List<Tuple<string, string>>();

            // 1) Prefetch
            try
            {
                string prefetchResult = TryFindViaPrefetch();
                if (!string.IsNullOrEmpty(prefetchResult))
                    candidates.Add(new Tuple<string, string>("Prefetch", prefetchResult));
            }
            catch (Exception ex)
            {
                debugLog.AppendLine("TryFindViaPrefetch ERROR: " + ex.Message);
            }

            // 2) Known Hardcoded Paths
            string fixedPathResult = TryKnownFixedPaths();
            if (!string.IsNullOrEmpty(fixedPathResult))
                candidates.Add(new Tuple<string, string>("KnownFixedPaths", fixedPathResult));

            // 3) Steam / libraryfolders.vdf
            string steamResult = TryFindViaSteamMethod();
            if (!string.IsNullOrEmpty(steamResult))
                candidates.Add(new Tuple<string, string>("SteamMethod", steamResult));

            // 4) Registry MUI Cache
            string muiResult = TryFindViaMuiCache();
            if (!string.IsNullOrEmpty(muiResult))
                candidates.Add(new Tuple<string, string>("MuiCache", muiResult));

            // 5) Valve.Source registry entry
            string valveResult = TryFindViaValveSourceCommand();
            if (!string.IsNullOrEmpty(valveResult))
                candidates.Add(new Tuple<string, string>("ValveSourceCommand", valveResult));

            // 6) Other registry checks (Origin/EA/Respawn)
            string registryResult = TryFindViaOtherRegistry();
            if (!string.IsNullOrEmpty(registryResult))
                candidates.Add(new Tuple<string, string>("OtherRegistry", registryResult));
            candidates.Add(new Tuple<string, string>("CurrentDirectory", "."));
            // Now validate them in order
            foreach (var candidate in candidates)
            {
                string method = candidate.Item1;
                string path = candidate.Item2;

                if (ValidateCandidateExe(path, out string validationReport))
                {
                    debugLog.AppendLine($"Candidate from {method} validated:");
                    debugLog.AppendLine($"  => {path}");
                    debugLog.AppendLine(validationReport);
                    return path; // Return on first valid candidate
                }
                else
                {
                    debugLog.AppendLine($"Candidate from {method} did NOT validate:");
                    debugLog.AppendLine($"  => {path}");
                    debugLog.AppendLine(validationReport);
                }
            }

            return null;
        }


        // =============================================================
        // Prefetch scanning
        // =============================================================
        private static string TryFindViaPrefetch()
        {
            debugLog.AppendLine("Entering TryFindViaPrefetch...");

            string prefetchDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Prefetch");
            if (!Directory.Exists(prefetchDir))
            {
                debugLog.AppendLine("Prefetch directory not found.");
                return null;
            }

            // We'll search for any prefetch file that references *titanfall.exe* or *r1delta.exe*.
            var patterns = new[] { "*titanfall.exe*", "*r1delta.exe*" };
            foreach (var pattern in patterns)
            {
                debugLog.AppendLine("Using pattern: " + pattern);
                string[] files = Directory.GetFiles(prefetchDir, pattern);
                foreach (string file in files)
                {
                    debugLog.AppendLine("Processing prefetch file: " + file);
                    try
                    {
                        byte[] data = File.ReadAllBytes(file);
                        data = Decompress(data);

                        // Basic checks
                        if (data.Length < 84)
                            continue;

                        // In a typical Win8+ prefetch, the EXE name is a UNICODE string starting at offset 16 with length 60 bytes
                        string headerExeName = Encoding.Unicode.GetString(data, 16, 60).TrimEnd('\0');
                        if (string.IsNullOrWhiteSpace(headerExeName))
                            continue;

                        // Now parse file info from offset 84
                        if (data.Length < 84 + 224)
                            continue;

                        byte[] fileInfoBytes = new byte[224];
                        Buffer.BlockCopy(data, 84, fileInfoBytes, 0, 224);

                        int filenameStringsOffset = BitConverter.ToInt32(fileInfoBytes, 16);
                        int filenameStringsSize = BitConverter.ToInt32(fileInfoBytes, 20);
                        int volumesInfoOffset = BitConverter.ToInt32(fileInfoBytes, 24);
                        int volumeCount = BitConverter.ToInt32(fileInfoBytes, 28);
                        int volumesInfoSize = BitConverter.ToInt32(fileInfoBytes, 32);

                        if (filenameStringsOffset < 0 ||
                            filenameStringsSize <= 0 ||
                            data.Length < filenameStringsOffset + filenameStringsSize)
                            continue;

                        // Extract the file names
                        byte[] filenameStringsBytes = new byte[filenameStringsSize];
                        Buffer.BlockCopy(data, filenameStringsOffset, filenameStringsBytes, 0, filenameStringsSize);
                        string filenamesRaw = Encoding.Unicode.GetString(filenameStringsBytes);
                        string[] fileNames = filenamesRaw.Split(new char[] { '\0' }, StringSplitOptions.RemoveEmptyEntries);

                        // Attempt to find a full path that ends with the EXE name
                        string fullPath = fileNames
                            .FirstOrDefault(fn => fn.EndsWith(headerExeName, StringComparison.OrdinalIgnoreCase));

                        if (string.IsNullOrEmpty(fullPath))
                            continue;

                        // Attempt volume info reconstruction
                        if (volumeCount > 0 && volumesInfoSize > 0 &&
                            volumesInfoOffset >= 0 && volumesInfoOffset + volumesInfoSize <= data.Length)
                        {
                            byte[] volumeInfoBlock = new byte[volumesInfoSize];
                            Buffer.BlockCopy(data, volumesInfoOffset, volumeInfoBlock, 0, volumesInfoSize);

                            if (volumesInfoSize >= 96)
                            {
                                byte[] volRecord = new byte[96];
                                Buffer.BlockCopy(volumeInfoBlock, 0, volRecord, 0, 96);

                                string prefetchSerial = BitConverter.ToInt32(volRecord, 16).ToString("X");
                                int volDevOffset = BitConverter.ToInt32(volRecord, 0);
                                int volDevNumChar = BitConverter.ToInt32(volRecord, 4);

                                // Resolve drive letter from serial
                                string driveLetter = GetDriveLetterForSerial(prefetchSerial);
                                if (!string.IsNullOrEmpty(driveLetter))
                                {
                                    if (volDevOffset >= 0 && volDevNumChar > 0 &&
                                        (volumesInfoOffset + volDevOffset + volDevNumChar * 2) <= data.Length)
                                    {
                                        // e.g. \VOLUME{...}
                                        byte[] devNameBytes = new byte[volDevNumChar * 2];
                                        Buffer.BlockCopy(data, volumesInfoOffset + volDevOffset, devNameBytes, 0, devNameBytes.Length);
                                        string volumeLabel = Encoding.Unicode.GetString(devNameBytes).TrimEnd('\0');

                                        if (fullPath.StartsWith(volumeLabel, StringComparison.OrdinalIgnoreCase))
                                        {
                                            fullPath = driveLetter.TrimEnd('\\') + fullPath.Substring(volumeLabel.Length);
                                        }
                                    }
                                }
                            }
                        }

                        // If path still references the GUID, try each drive
                        if (!File.Exists(fullPath) && fullPath.StartsWith(@"\VOLUME{", StringComparison.OrdinalIgnoreCase))
                        {
                            int idx = fullPath.IndexOf("}\\");
                            if (idx != -1)
                            {
                                string remainder = fullPath.Substring(idx + 1).TrimStart('\\');
                                foreach (DriveInfo di in DriveInfo.GetDrives())
                                {
                                    if (!di.IsReady) continue;
                                    string candidate = Path.Combine(di.RootDirectory.FullName, remainder);
                                    if (File.Exists(candidate))
                                    {
                                        fullPath = candidate;
                                        break;
                                    }
                                }
                            }
                        }

                        if (File.Exists(fullPath))
                        {
                            debugLog.AppendLine($"Prefetch discovered potential path: {fullPath}");
                            return fullPath;
                        }
                    }
                    catch (Exception ex)
                    {
                        debugLog.AppendLine("Prefetch file error: " + ex.Message);
                    }
                }
            }
            return null;
        }

        private static byte[] Decompress(byte[] data)
        {
            // Check if compressed (Win8+ "MAM" signature)
            if (data.Length < 8 || Encoding.ASCII.GetString(data, 0, 3) != "MAM")
                return data;

            uint uncompressedSize = BitConverter.ToUInt32(data, 4);
            int compLen = data.Length - 8;
            byte[] compData = new byte[compLen];
            Buffer.BlockCopy(data, 8, compData, 0, compLen);

            byte[] output = new byte[uncompressedSize];
            if (RtlGetCompressionWorkSpaceSize(CompressionFormatXpressHuff, out ulong ws, out ulong fws) != 0)
                throw new Exception("Failed to get workspace size.");

            byte[] workspace = new byte[fws];
            if (RtlDecompressBufferEx(
                    CompressionFormatXpressHuff,
                    output, output.Length,
                    compData, compData.Length,
                    out int finalSize,
                    workspace) != 0)
            {
                throw new Exception("Decompression failed.");
            }

            return output;
        }

        private static string GetDriveLetterForSerial(string targetSerial)
        {
            foreach (DriveInfo di in DriveInfo.GetDrives())
            {
                if (!di.IsReady)
                    continue;

                string root = di.RootDirectory.FullName;
                if (GetVolumeInformation(root, null, 0,
                                         out uint serial,
                                         out uint maxComponent,
                                         out uint flags,
                                         null, 0))
                {
                    if (serial.ToString("X").Equals(targetSerial, StringComparison.OrdinalIgnoreCase))
                        return root;
                }
            }
            return null;
        }

        // =============================================================
        // Known Hardcoded Paths
        // =============================================================
        private static string TryKnownFixedPaths()
        {
            // For each drive, see if we can find "Titanfall.exe" or "r1delta.exe" in these known sub-paths.
            // You can modify or expand these as needed.
            string[] knownPaths = new string[]
            {
                @"Games\EA\Titanfall",
                @"SteamLibrary\steamapps\common\Titanfall",
                @"Program Files (x86)\Steam\steamapps\common\Titanfall",
                @"Program Files\EA Games\Titanfall",
                @"Program Files (x86)\Origin Games\Titanfall"
            };

            foreach (var drive in DriveInfo.GetDrives())
            {
                if (!drive.IsReady)
                    continue;

                foreach (var subPath in knownPaths)
                {
                    string basePath = Path.Combine(drive.RootDirectory.FullName, subPath);
                    foreach (string exeName in ExeNames)
                    {
                        string candidate = Path.Combine(basePath, exeName);
                        if (File.Exists(candidate))
                        {
                            debugLog.AppendLine($"Found candidate in known path: {candidate}");
                            return candidate;
                        }
                    }
                }
            }
            return null;
        }

        // =============================================================
        // Steam searching
        // =============================================================
        private static string TryFindViaSteamMethod()
        {
            // Try 64-bit registry then 32-bit for Steam InstallPath
            string steamPath = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Valve\Steam",
                                                 "InstallPath", null) as string
                               ??
                               Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam",
                                                 "InstallPath", null) as string;

            if (string.IsNullOrEmpty(steamPath) || !Directory.Exists(steamPath))
                return null;

            string steamApps = Path.Combine(steamPath, "steamapps");
            if (!Directory.Exists(steamApps))
                return null;

            // First check main library
            string candidate = SearchSteamLibraryFolders(steamApps);
            if (!string.IsNullOrEmpty(candidate))
                return candidate;

            // If not found, parse libraryfolders.vdf for extra libraries
            string libraryVdf = Path.Combine(steamApps, "libraryfolders.vdf");
            if (File.Exists(libraryVdf))
            {
                List<string> extraLibraries = ParseLibraryFolders(libraryVdf);
                foreach (string lib in extraLibraries)
                {
                    string subSteamApps = Path.Combine(lib, "steamapps");
                    if (Directory.Exists(subSteamApps))
                    {
                        candidate = SearchSteamLibraryFolders(subSteamApps);
                        if (!string.IsNullOrEmpty(candidate))
                            return candidate;
                    }
                }
            }

            return null;
        }

        private static List<string> ParseLibraryFolders(string libraryFoldersFile)
        {
            var results = new List<string>();
            if (!File.Exists(libraryFoldersFile))
                return results;

            string[] lines = File.ReadAllLines(libraryFoldersFile);
            foreach (var line in lines)
            {
                string trimmed = line.Trim();
                // Typically lines look like:  "1" "D:\\SteamLibrary"
                // We just want to find any "path" "X:\somewhere"
                if (trimmed.StartsWith("\""))
                {
                    string[] parts = trimmed.Split('"')
                                            .Select(s => s.Trim())
                                            .Where(s => !string.IsNullOrEmpty(s))
                                            .ToArray();
                    // Typically parts = [ 1, D:\SteamLibrary ], or [ path, D:\SteamLibrary ]
                    if (parts.Length >= 2)
                    {
                        string key = parts[0];
                        string val = parts[1];
                        if (key.Equals("path", StringComparison.OrdinalIgnoreCase) && Directory.Exists(val))
                        {
                            results.Add(val);
                        }
                    }
                }
            }
            return results;
        }

        private static string SearchSteamLibraryFolders(string steamAppsPath)
        {
            // Quick check: see if Titanfall or r1 is in "common\Titanfall\[exe]"
            foreach (string exeName in ExeNames)
            {
                string quickPath = Path.Combine(steamAppsPath, "common", "Titanfall", exeName);
                if (File.Exists(quickPath))
                {
                    debugLog.AppendLine($"Steam quick path found: {quickPath}");
                    return quickPath;
                }
            }

            // If that fails, scan the appmanifest_*.acf files for "installdir"
            string[] acfFiles = Directory.GetFiles(steamAppsPath, "appmanifest_*.acf", SearchOption.TopDirectoryOnly);
            foreach (string acf in acfFiles)
            {
                string text = File.ReadAllText(acf);
                // Check if it references Titanfall or "r1delta" in some official name
                if (text.IndexOf("Titanfall", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    // Extract "installdir" "somefolder"
                    int idx = text.IndexOf("\"installdir\"", StringComparison.OrdinalIgnoreCase);
                    if (idx > 0)
                    {
                        int nextQuote = text.IndexOf('"', idx + 12);
                        if (nextQuote >= 0)
                        {
                            int endQuote = text.IndexOf('"', nextQuote + 1);
                            if (endQuote > nextQuote)
                            {
                                string installdir = text.Substring(nextQuote + 1, endQuote - (nextQuote + 1));
                                if (!string.IsNullOrWhiteSpace(installdir))
                                {
                                    // Check for either EXE
                                    foreach (var exeName in ExeNames)
                                    {
                                        string candidate = Path.Combine(steamAppsPath, "common", installdir, exeName);
                                        if (File.Exists(candidate))
                                        {
                                            return candidate;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return null;
        }

        // =============================================================
        // MUI Cache
        // =============================================================
        private static string TryFindViaMuiCache()
        {
            const string muiCachePath = @"Local Settings\Software\Microsoft\Windows\Shell\MuiCache";
            using (RegistryKey root = Registry.ClassesRoot.OpenSubKey(muiCachePath))
            {
                if (root == null)
                    return null;

                foreach (string valueName in root.GetValueNames())
                {
                    // We only care if it references "titanfall.exe" or "r1delta.exe"
                    if (ExeNames.Any(exe => valueName.IndexOf(exe, StringComparison.OrdinalIgnoreCase) >= 0))
                    {
                        string possiblePath = valueName;
                        int dotIndex = possiblePath.IndexOf(".Application", StringComparison.OrdinalIgnoreCase);
                        if (dotIndex > 0)
                            possiblePath = possiblePath.Substring(0, dotIndex);

                        if (File.Exists(possiblePath))
                            return possiblePath;
                    }
                }
            }
            return null;
        }

        // =============================================================
        // Valve.Source Command
        // =============================================================
        private static string TryFindViaValveSourceCommand()
        {
            // Some registry keys might store something like "C:\...\titanfall.exe" or "C:\...\r1delta.exe" in quotes
            const string valveSourcePath = @"Valve.Source\shell\open\command";
            using (RegistryKey root = Registry.ClassesRoot.OpenSubKey(valveSourcePath))
            {
                if (root == null)
                    return null;

                string commandVal = root.GetValue("") as string;
                if (string.IsNullOrEmpty(commandVal) ||
                    !ExeNames.Any(exe => commandVal.IndexOf(exe, StringComparison.OrdinalIgnoreCase) >= 0))
                {
                    return null;
                }

                int firstQuote = commandVal.IndexOf('"');
                int secondQuote = commandVal.IndexOf('"', firstQuote + 1);
                if (firstQuote >= 0 && secondQuote > firstQuote)
                {
                    string path = commandVal.Substring(firstQuote + 1, secondQuote - (firstQuote + 1));
                    if (File.Exists(path))
                        return path;
                }
            }
            return null;
        }

        // =============================================================
        // Other Registry
        // =============================================================
        private static string TryFindViaOtherRegistry()
        {
            // Check for various known registry entries where Titanfall or R1 might be installed

            // Steam Titanfall App 1454890
            using (RegistryKey key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 1454890"))
            {
                if (key != null)
                {
                    string pathStr = key.GetValue("InstallLocation") as string;
                    if (!string.IsNullOrEmpty(pathStr))
                    {
                        // Check for both EXEs
                        foreach (string exeName in ExeNames)
                        {
                            string possible = Path.Combine(pathStr, exeName);
                            if (File.Exists(possible))
                                return possible;
                        }
                    }
                }
            }

            // Respawn\Titanfall
            using (RegistryKey key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Respawn\Titanfall"))
            {
                if (key != null)
                {
                    string pathStr = key.GetValue("Install Dir") as string;
                    if (!string.IsNullOrEmpty(pathStr))
                    {
                        foreach (string exeName in ExeNames)
                        {
                            string possible = Path.Combine(pathStr, exeName);
                            if (File.Exists(possible))
                                return possible;
                        }
                    }
                }
            }

            // Wow6432Node\Respawn\Titanfall
            using (RegistryKey key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Wow6432Node\Respawn\Titanfall"))
            {
                if (key != null)
                {
                    string pathStr = key.GetValue("Install Dir") as string;
                    if (!string.IsNullOrEmpty(pathStr))
                    {
                        foreach (string exeName in ExeNames)
                        {
                            string possible = Path.Combine(pathStr, exeName);
                            if (File.Exists(possible))
                                return possible;
                        }
                    }
                }
            }

            // Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{347EE0C3-0690-48F6-A231-53853C2A80D6}
            using (RegistryKey key = Registry.LocalMachine.OpenSubKey(
                @"SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{347EE0C3-0690-48F6-A231-53853C2A80D6}"))
            {
                if (key != null)
                {
                    string pathStr = key.GetValue("InstallLocation") as string;
                    if (!string.IsNullOrEmpty(pathStr))
                    {
                        foreach (string exeName in ExeNames)
                        {
                            string possible = Path.Combine(pathStr, exeName);
                            if (File.Exists(possible))
                                return possible;
                        }
                    }
                }
            }

            return null;
        }

        // =============================================================
        // Validation
        // =============================================================
        private static bool ValidateCandidateExe(string exePath, out string validationReport)
        {
            StringBuilder report = new StringBuilder();

            if (!File.Exists(exePath))
            {
                report.AppendLine("File not found: " + exePath);
                validationReport = report.ToString();
                return false;
            }

            // Optionally, you can do an MD5 check on the EXE itself if you want.
            // (This code is commented out by default.)
            /*
            try
            {
                string exeMD5 = GetMD5Hash(exePath);
                // Compare to known Titanfall or R1 MD5 if you have them
                report.AppendLine("EXE MD5: " + exeMD5);
                // if (!exeMD5.Equals(...)) { ... fail ... }
            }
            catch (Exception ex)
            {
                report.AppendLine("Error computing EXE MD5: " + ex.Message);
                validationReport = report.ToString();
                return false;
            }
            */

            // Check for r1\gameinfo.txt
            string baseDir = Path.GetDirectoryName(exePath);
            string gameInfoPath = Path.Combine(baseDir, "r1", "gameinfo.txt");
            if (!File.Exists(gameInfoPath))
            {
                report.AppendLine("Missing r1\\gameinfo.txt => " + gameInfoPath);
                validationReport = report.ToString();
                return false;
            }

            // MD5 check on gameinfo.txt
            try
            {
                string md5 = GetMD5Hash(gameInfoPath);
                string expected = "832870561f0f9745894f94e2e3d4db16"; // your known good MD5
                report.AppendLine("gameinfo.txt MD5: " + md5);
                if (!md5.Equals(expected, StringComparison.OrdinalIgnoreCase))
                {
                    report.AppendLine("gameinfo.txt MD5 did not match expected: " + expected);
                    validationReport = report.ToString();
                    return false;
                }
            }
            catch (Exception ex)
            {
                report.AppendLine("Error computing MD5 for gameinfo.txt: " + ex.Message);
                validationReport = report.ToString();
                return false;
            }

            report.AppendLine("Validation PASSED for: " + exePath);
            validationReport = report.ToString();
            return true;
        }

        private static string GetMD5Hash(string filePath)
        {
            using (MD5 md5 = MD5.Create())
            using (FileStream stream = File.OpenRead(filePath))
            {
                byte[] hash = md5.ComputeHash(stream);
                return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
            }
        }

        // (Optional) If you want the complete debug log from outside:
        public static string GetDebugLog() => debugLog.ToString();
    }
}
