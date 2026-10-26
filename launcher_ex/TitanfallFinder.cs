using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
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
            string steamPath = GetSteamInstallPath();

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

        private static string GetSteamInstallPath()
        {
            return Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Valve\Steam",
                                     "InstallPath", null) as string
                   ??
                   Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam",
                                     "InstallPath", null) as string;
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

        // =============================================================
        // Titanfall 2
        // =============================================================
        // Titanfall 2 is a different game from the one FindTitanfallOrR1Delta looks for, so it has
        // its own candidate sources, Steam library reader and validator; nothing is shared with that finder.
        private const string Titanfall2SteamAppId = "1237970";
        private const string Titanfall2EnvironmentVariable = "R1DELTA_TITANFALL2_PATH";

        // Product GUID the EA installer registers Titanfall 2 under (Uninstall and Respawn\Titanfall2).
        private const string Titanfall2EaProductGuid = "{4BD80373-FEE7-45B6-8249-6E8E98717405}";

        // The EA installer writes a mangled trademark sign, so the display name can be "TitanfallT 2".
        private static readonly Regex Titanfall2DisplayName =
            new Regex(@"^Titanfall\S{0,3}\s*2\b", RegexOptions.IgnoreCase);

        private static readonly RegistryView[] RegistryViews = { RegistryView.Registry64, RegistryView.Registry32 };

        private static readonly string[] Titanfall2FolderNames = { "Titanfall2", "Titanfall 2" };

        // Parent folders tried on every drive, mirroring the layouts in TryKnownFixedPaths.
        private static readonly string[] Titanfall2KnownParents =
        {
            "",
            "Games",
            @"Games\EA",
            "EA Games",
            @"Program Files\EA Games",
            @"Program Files (x86)\Origin Games",
            @"Program Files (x86)\Steam\steamapps\common",
            @"SteamLibrary\steamapps\common"
        };

        /// <summary>
        /// Finds a complete Titanfall 2 install and returns its root directory, or null.
        /// The search order is shared with the engine's R2 map provider (DiscoverR2Install in
        /// engine/filesystem/r2_map_provider.cpp) and documented in docs/r2port.md; change both together:
        ///   1. manualPath (a root directory, or a file inside it; the launcher has no saved setting yet)
        ///   2. the R1DELTA_TITANFALL2_PATH environment variable
        ///      Both are explicit choices: if one does not validate, null is returned instead of
        ///      falling back to some other install. The engine has no manualPath.
        ///   3. HKLM\SOFTWARE\Respawn\Titanfall2 "Install Dir" (64-bit then 32-bit view)
        ///   4. Uninstall entries: the EA product GUID, "Steam App 1237970", or a display name that reads Titanfall 2
        ///   5. Steam: root from the registry, libraries from libraryfolders.vdf, appmanifest_1237970.acf
        ///      installdir, then common\Titanfall2 and common\Titanfall 2
        ///   6. Titanfall2 / "Titanfall 2" under well-known parent folders on every fixed or removable drive
        /// Every candidate must pass the install validator.
        /// </summary>
        public static string FindTitanfall2(string manualPath = null)
        {
            debugLog.Clear();

            if (!string.IsNullOrWhiteSpace(manualPath))
                return ValidateTitanfall2Candidate("ManualPath", NormalizeInstallRoot(manualPath));

            string envPath = Environment.GetEnvironmentVariable(Titanfall2EnvironmentVariable);
            if (!string.IsNullOrWhiteSpace(envPath))
                return ValidateTitanfall2Candidate(Titanfall2EnvironmentVariable, NormalizeInstallRoot(envPath));

            var sources = new List<Tuple<string, Func<IEnumerable<string>>>>
            {
                Tuple.Create<string, Func<IEnumerable<string>>>("Titanfall2Registry", FindTitanfall2ViaRegistry),
                Tuple.Create<string, Func<IEnumerable<string>>>("Titanfall2Uninstall", FindTitanfall2ViaUninstallRegistry),
                Tuple.Create<string, Func<IEnumerable<string>>>("Titanfall2Steam", FindTitanfall2ViaSteam),
                Tuple.Create<string, Func<IEnumerable<string>>>("Titanfall2KnownPaths", FindTitanfall2ViaKnownPaths)
            };

            var tested = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var source in sources)
            {
                List<string> paths;
                try
                {
                    paths = source.Item2().ToList();
                }
                catch (Exception ex)
                {
                    debugLog.AppendLine(source.Item1 + " ERROR: " + ex.Message);
                    continue;
                }

                foreach (string path in paths)
                {
                    string root = NormalizeInstallRoot(path);
                    if (root == null || !tested.Add(root))
                        continue;

                    string validated = ValidateTitanfall2Candidate(source.Item1, root);
                    if (validated != null)
                        return validated;
                }
            }

            return null;
        }

        private static IEnumerable<string> FindTitanfall2ViaRegistry()
        {
            // Written by the EA installer, and kept up to date by EA app/Origin when the install is moved.
            return ReadLocalMachineStrings(@"SOFTWARE\Respawn\Titanfall2", "Install Dir");
        }

        private static IEnumerable<string> FindTitanfall2ViaUninstallRegistry()
        {
            const string uninstallPath = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";
            string steamKeyName = "Steam App " + Titanfall2SteamAppId;

            foreach (RegistryView view in RegistryViews)
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (RegistryKey uninstall = hklm.OpenSubKey(uninstallPath))
                {
                    if (uninstall == null)
                        continue;

                    foreach (string name in uninstall.GetSubKeyNames())
                    {
                        using (RegistryKey key = uninstall.OpenSubKey(name))
                        {
                            if (key == null)
                                continue;

                            string displayName = key.GetValue("DisplayName") as string;
                            bool known = name.Equals(Titanfall2EaProductGuid, StringComparison.OrdinalIgnoreCase) ||
                                         name.Equals(steamKeyName, StringComparison.OrdinalIgnoreCase);
                            if (!known && (displayName == null || !Titanfall2DisplayName.IsMatch(displayName)))
                                continue;

                            string location = key.GetValue("InstallLocation") as string;
                            if (!string.IsNullOrWhiteSpace(location))
                                yield return location;
                        }
                    }
                }
            }
        }

        private static IEnumerable<string> FindTitanfall2ViaSteam()
        {
            // Steam roots, then every library listed in their libraryfolders.vdf. Registry locations,
            // value names and vdf formats match the engine's DiscoverR2Install.
            var libraries = new List<string>();
            foreach (string steamRoot in ReadTitanfall2SteamRoots())
            {
                libraries.Add(steamRoot);
                libraries.AddRange(ReadSteamLibraryPaths(Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf")));
            }

            foreach (string library in libraries)
            {
                string steamApps = Path.Combine(library, "steamapps");
                string common = Path.Combine(steamApps, "common");

                // The manifest names the real folder; the default names cover a deleted manifest.
                string manifest = Path.Combine(steamApps, "appmanifest_" + Titanfall2SteamAppId + ".acf");
                List<string> manifestTokens = ReadVdfTokens(manifest);
                for (int i = 0; i + 1 < manifestTokens.Count; i++)
                {
                    if (manifestTokens[i] == "installdir")
                    {
                        yield return Path.Combine(common, manifestTokens[i + 1]);
                        break;
                    }
                }

                foreach (string folderName in Titanfall2FolderNames)
                    yield return Path.Combine(common, folderName);
            }
        }

        private static IEnumerable<string> ReadTitanfall2SteamRoots()
        {
            var locations = new[]
            {
                Tuple.Create(RegistryHive.LocalMachine, @"SOFTWARE\Valve\Steam"),
                Tuple.Create(RegistryHive.LocalMachine, @"SOFTWARE\WOW6432Node\Valve\Steam"),
                Tuple.Create(RegistryHive.CurrentUser, @"Software\Valve\Steam")
            };

            foreach (var location in locations)
            {
                foreach (string valueName in new[] { "InstallPath", "SteamPath" })
                {
                    foreach (RegistryView view in RegistryViews)
                    {
                        string value = null;
                        using (RegistryKey hive = RegistryKey.OpenBaseKey(location.Item1, view))
                        using (RegistryKey key = hive.OpenSubKey(location.Item2))
                            value = key?.GetValue(valueName) as string;
                        if (!string.IsNullOrWhiteSpace(value))
                            yield return value;
                    }
                }
            }
        }

        private static List<string> ReadVdfTokens(string file)
        {
            var tokens = new List<string>();
            if (!File.Exists(file))
                return tokens;

            foreach (Match match in Regex.Matches(File.ReadAllText(file), "\"((?:[^\"\\\\]|\\\\.)*)\""))
                tokens.Add(match.Groups[1].Value.Replace("\\\\", "\\").Replace("\\\"", "\""));
            return tokens;
        }

        // Both libraryfolders.vdf layouts: "path" "D:\\Lib" entries and the older "1" "D:\\Lib" entries.
        private static List<string> ReadSteamLibraryPaths(string libraryFoldersFile)
        {
            var libraries = new List<string>();
            List<string> tokens = ReadVdfTokens(libraryFoldersFile);
            for (int i = 0; i + 1 < tokens.Count; i++)
            {
                string key = tokens[i];
                string value = tokens[i + 1];
                bool legacyNumericEntry = key.Length > 0 && key.All(char.IsDigit) && value.Length > 2 && value[1] == ':';
                if (key == "path" || legacyNumericEntry)
                {
                    libraries.Add(value);
                    i++;
                }
            }
            return libraries;
        }

        private static IEnumerable<string> FindTitanfall2ViaKnownPaths()
        {
            foreach (DriveInfo drive in DriveInfo.GetDrives())
            {
                // Skip network and optical drives so a dead share cannot stall the search.
                if (drive.DriveType != DriveType.Fixed && drive.DriveType != DriveType.Removable)
                    continue;
                if (!drive.IsReady)
                    continue;

                foreach (string parent in Titanfall2KnownParents)
                {
                    foreach (string folderName in Titanfall2FolderNames)
                        yield return Path.Combine(drive.RootDirectory.FullName, parent, folderName);
                }
            }
        }

        private static IEnumerable<string> ReadLocalMachineStrings(string subKeyPath, string valueName)
        {
            foreach (RegistryView view in RegistryViews)
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (RegistryKey key = hklm.OpenSubKey(subKeyPath))
                {
                    string value = key?.GetValue(valueName) as string;
                    if (!string.IsNullOrWhiteSpace(value))
                        yield return value;
                }
            }
        }

        /// <summary>
        /// Turns a directory, or a file inside the install, into a trimmed full directory path.
        /// Returns null when the text is not a usable path.
        /// </summary>
        private static string NormalizeInstallRoot(string path)
        {
            try
            {
                string full = Path.GetFullPath(path.Trim().Trim('"'));
                if (File.Exists(full))
                    full = Path.GetDirectoryName(full);

                string pathRoot = Path.GetPathRoot(full);
                if (!string.Equals(full, pathRoot, StringComparison.OrdinalIgnoreCase))
                    full = full.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                return full;
            }
            catch (Exception ex) when (
                ex is NotSupportedException ||
                ex is System.Security.SecurityException ||
                ex is ArgumentException ||
                ex is PathTooLongException)
            {
                debugLog.AppendLine($"Not a usable path '{path}': {ex.Message}");
                return null;
            }
        }

        private static string ValidateTitanfall2Candidate(string method, string root)
        {
            if (root == null)
                return null;

            if (ValidateTitanfall2Root(root, out string validationReport))
            {
                debugLog.AppendLine($"Candidate from {method} validated:");
                debugLog.AppendLine($"  => {root}");
                debugLog.AppendLine(validationReport);
                return root;
            }

            debugLog.AppendLine($"Candidate from {method} did NOT validate:");
            debugLog.AppendLine($"  => {root}");
            debugLog.AppendLine(validationReport);
            return null;
        }

        // Titanfall and R1Delta share most of their layout with Titanfall 2 (bin\x64_retail, vpk\ with the
        // same file names), so those files cannot tell them apart. rtech_game.dll and the r2 rpaks exist
        // only in Titanfall 2, and r1\gameinfo.txt (the marker ValidateCandidateExe requires) only in
        // Titanfall/R1Delta, which keeps the two validators exclusive.
        private static bool ValidateTitanfall2Root(string root, out string validationReport)
        {
            StringBuilder report = new StringBuilder();

            if (!Directory.Exists(root))
            {
                report.AppendLine("Directory not found: " + root);
                validationReport = report.ToString();
                return false;
            }

            if (File.Exists(Path.Combine(root, "r1", "gameinfo.txt")))
            {
                report.AppendLine("Has r1\\gameinfo.txt, so this is Titanfall or R1Delta");
                validationReport = report.ToString();
                return false;
            }

            // Wildcards: the vpk directory file is prefixed with the install language, and the rpak set varies.
            // The numbered vpk data file separates a full install from a partial one.
            var requirements = new[]
            {
                Tuple.Create(@"bin\x64_retail", "rtech_game.dll"),
                Tuple.Create(@"r2\paks\Win64", "*.rpak"),
                Tuple.Create("vpk", "*client_mp_common.bsp.pak000_dir.vpk"),
                Tuple.Create("vpk", "client_mp_common.bsp.pak000_000.vpk")
            };

            bool valid = true;
            foreach (var requirement in requirements)
            {
                if (!HasMatchingFile(Path.Combine(root, requirement.Item1), requirement.Item2))
                {
                    report.AppendLine($"Missing {requirement.Item1}\\{requirement.Item2}");
                    valid = false;
                }
            }

            if (valid)
                report.AppendLine("Validation PASSED for Titanfall 2: " + root);
            validationReport = report.ToString();
            return valid;
        }

        private static bool HasMatchingFile(string directory, string pattern)
        {
            try
            {
                return Directory.Exists(directory) && Directory.EnumerateFiles(directory, pattern).Any();
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                // Dangling junctions report as existing, then fail to enumerate.
                debugLog.AppendLine($"Cannot list '{directory}': {ex.Message}");
                return false;
            }
        }

        // (Optional) If you want the complete debug log from outside:
        public static string GetDebugLog() => debugLog.ToString();
    }
}
