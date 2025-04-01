﻿using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices; // For P/Invokes if ever needed again
using System.Threading;
using System.Windows; // For MessageBox - consider abstracting UI interactions
using System.Threading.Tasks;
using Newtonsoft.Json; // If resume manifest is used later
using K4os.Hash.xxHash;
using launcher_ex; // For IInstallProgress, SetupWindow
using System.Net.Http; // Using HttpClient for FastDownloadService likely
using System.Text;
// using Monitor.Core.Utilities; // Not used directly here
using System.Reflection;
using Microsoft.Win32;
using Dark.Net; // For Registry access

// Assuming FastDownloadService class exists and handles downloads
//using FastDownloadService; // Or whatever namespace it's in

namespace R1Delta
{
    // Helper class for Registry operations
    internal static class RegistryHelper
    {
        private const string RegistryBaseKey = @"Software\R1Delta"; // Use HKCU implicitly
        private const string InstallPathValueName = "InstallPath";
        // --- NEW: Constants for Setup Window Settings ---
        private const string ShowSetupOnLaunchValueName = "ShowSetupOnLaunch";
        private const string LaunchArgumentsValueName = "LaunchArguments";


        public static string GetInstallPath()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        return key.GetValue(InstallPathValueName) as string;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{InstallPathValueName}: {ex.Message}");
            }
            return null;
        }

        public static void SaveInstallPath(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) return;
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        key.SetValue(InstallPathValueName, path, RegistryValueKind.String);
                        Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{InstallPathValueName} = {path}");
                    }
                    else
                    {
                        Debug.WriteLine($"Error: Could not create or open registry key for writing: HKCU\\{RegistryBaseKey}");
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{InstallPathValueName}: {ex.Message}");
                MessageBox.Show($"Warning: Could not save the installation path to the registry.\nThe game might ask for the location again on next launch.\n\nError: {ex.Message}",
                                "Registry Warning", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        // --- NEW: Methods for Setup Window Settings ---

        /// <summary>
        /// Gets the 'Show Setup Window on Launch' setting. Defaults to true if not found or error.
        /// </summary>
        public static bool GetShowSetupOnLaunch()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        object value = key.GetValue(ShowSetupOnLaunchValueName);
                        // Check for both REG_DWORD (common for bools) and REG_SZ ("True"/"False")
                        if (value is int intValue)
                        {
                            return intValue != 0; // Non-zero means true
                        }
                        if (value is string stringValue && bool.TryParse(stringValue, out bool boolValue))
                        {
                            return boolValue;
                        }
                        // If value exists but is wrong type, or doesn't exist, default to true
                        if (value != null)
                        {
                            Debug.WriteLine($"Warning: Registry value {ShowSetupOnLaunchValueName} has unexpected type {value.GetType()}. Defaulting to true.");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{ShowSetupOnLaunchValueName}: {ex.Message}. Defaulting to true.");
            }
            return false; // Default: show setup
        }

        /// <summary>
        /// Saves the 'Show Setup Window on Launch' setting.
        /// </summary>
        public static void SaveShowSetupOnLaunch(bool show)
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        // Use REG_DWORD (0 or 1) for boolean values as it's more standard
                        key.SetValue(ShowSetupOnLaunchValueName, show ? 1 : 0, RegistryValueKind.DWord);
                        Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{ShowSetupOnLaunchValueName} = {show}");
                    }
                    else
                    {
                        Debug.WriteLine($"Error: Could not create or open registry key for writing: HKCU\\{RegistryBaseKey}");
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{ShowSetupOnLaunchValueName}: {ex.Message}");
                // Non-critical, don't show a MessageBox for this one.
            }
        }

        /// <summary>
        /// Gets the persisted Launch Arguments. Returns empty string if not found or error.
        /// </summary>
        public static string GetLaunchArguments()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        return (key.GetValue(LaunchArgumentsValueName) as string) ?? string.Empty;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{LaunchArgumentsValueName}: {ex.Message}");
            }
            return string.Empty;
        }

        /// <summary>
        /// Saves the Launch Arguments.
        /// </summary>
        public static void SaveLaunchArguments(string args)
        {
            // Allow saving null or empty string to clear the value
            string valueToSave = args ?? string.Empty;
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey))
                {
                    if (key != null)
                    {
                        key.SetValue(LaunchArgumentsValueName, valueToSave, RegistryValueKind.String);
                        Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{LaunchArgumentsValueName} = \"{valueToSave}\"");
                    }
                    else
                    {
                        Debug.WriteLine($"Error: Could not create or open registry key for writing: HKCU\\{RegistryBaseKey}");
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{LaunchArgumentsValueName}: {ex.Message}");
                // Non-critical, don't show a MessageBox.
            }
        }
    }


    public static class TitanfallManager
    {
        // Truncated s_fileList (Keep as is)
        public static readonly List<(string Url, string RelativePath, ulong XxHash, long Size)> s_fileList
            = new List<(string, string, ulong, long)>
        {
            ("https://but.nobody-ca.me/r1/bin/dxsupport.cfg","bin/dxsupport.cfg",0x7a90e15c4da10d29,94158),
("https://but.nobody-ca.me/r1/bin/x64_retail/AdminServer.dll","bin/x64_retail/AdminServer.dll",0x105a95ce4f6a2720,2021888),
("https://but.nobody-ca.me/r1/bin/x64_retail/bink2w64.dll","bin/x64_retail/bink2w64.dll",0x43b964575bc2ca2a,432128),
("https://but.nobody-ca.me/r1/bin/x64_retail/bsppack.dll","bin/x64_retail/bsppack.dll",0xc86c0404f2244d7b,1081856),
("https://but.nobody-ca.me/r1/bin/x64_retail/datacache.dll","bin/x64_retail/datacache.dll",0x9b1cf55b61556f89,835584),
("https://but.nobody-ca.me/r1/bin/x64_retail/dedicated.dll","bin/x64_retail/dedicated.dll",0x66cb7bb70926897e,3562496),
("https://but.nobody-ca.me/r1/bin/x64_retail/engine.dll","bin/x64_retail/engine.dll",0xb53f1d82938fcfac,9358848),
("https://but.nobody-ca.me/r1/bin/x64_retail/filesystem_stdio.dll","bin/x64_retail/filesystem_stdio.dll",0xe5c631f3d24c0f1f,1097728),
("https://but.nobody-ca.me/r1/bin/x64_retail/inputsystem.dll","bin/x64_retail/inputsystem.dll",0xea5adfbea0f37dad,381952),
("https://but.nobody-ca.me/r1/bin/x64_retail/launcher.dll","bin/x64_retail/launcher.dll",0xafd3617f8007dfba,1138176),
("https://but.nobody-ca.me/r1/bin/x64_retail/localize.dll","bin/x64_retail/localize.dll",0xfc7dca157a41c3da,449536),
("https://but.nobody-ca.me/r1/bin/x64_retail/materialsystem_dx11.dll","bin/x64_retail/materialsystem_dx11.dll",0x8be7286937110470,2801664),
("https://but.nobody-ca.me/r1/bin/x64_retail/materialsystem_nodx.dll","bin/x64_retail/materialsystem_nodx.dll",0x08d32b9e6f67d821,821760),
("https://but.nobody-ca.me/r1/bin/x64_retail/scenefilecache.dll","bin/x64_retail/scenefilecache.dll",0x872c2a00b0825dd3,244224),
("https://but.nobody-ca.me/r1/bin/x64_retail/studiorender.dll","bin/x64_retail/studiorender.dll",0x73ed4e0699beca8d,534528),
("https://but.nobody-ca.me/r1/bin/x64_retail/tier0.dll","bin/x64_retail/tier0.dll",0x77a3ad75f03c71d1,380416),
("https://but.nobody-ca.me/r1/bin/x64_retail/valve_avi.dll","bin/x64_retail/valve_avi.dll",0x01586cd2822e4fb5,414720),
("https://but.nobody-ca.me/r1/bin/x64_retail/vaudio_speex.dll","bin/x64_retail/vaudio_speex.dll",0xa831593de41b9c2e,327168),
("https://but.nobody-ca.me/r1/bin/x64_retail/vgui2.dll","bin/x64_retail/vgui2.dll",0xd80ebd5e63590cca,866304),
("https://but.nobody-ca.me/r1/bin/x64_retail/vguimatsurface.dll","bin/x64_retail/vguimatsurface.dll",0xf30ee4dad7486ee6,2189312),
("https://but.nobody-ca.me/r1/bin/x64_retail/vphysics.dll","bin/x64_retail/vphysics.dll",0x3d1cceda4e50e11b,2084352),
("https://but.nobody-ca.me/r1/bin/x64_retail/vstdlib.dll","bin/x64_retail/vstdlib.dll",0x731e15882c2d07bc,484352),
("https://but.nobody-ca.me/r1/r1/aidata.bin","r1/aidata.bin",0x1282c6fd25877252,258),
("https://but.nobody-ca.me/r1/r1/GameInfo.txt","r1/GameInfo.txt",0x8f242af57fc26bb1,1622),
("https://but.nobody-ca.me/r1/r1/pilotaidata.bin","r1/pilotaidata.bin",0xe0e8e7f37ae0f6a4,258),
("https://but.nobody-ca.me/r1/r1/titanaidata.bin","r1/titanaidata.bin",0xe0e8e7f37ae0f6a4,258),
("https://but.nobody-ca.me/r1/r1/bin/x64_retail/client.dll","r1/bin/x64_retail/client.dll",0x660ea97d1aa878a2,13346816),
("https://but.nobody-ca.me/r1/r1/cfg/config_default_pc.cfg","r1/cfg/config_default_pc.cfg",0xf199cf45d4271839,1170),
("https://but.nobody-ca.me/r1/r1/cfg/cpu_level_0_pc.ekv","r1/cfg/cpu_level_0_pc.ekv",0x1659551a5591fdb3,393),
("https://but.nobody-ca.me/r1/r1/cfg/cpu_level_1_pc.ekv","r1/cfg/cpu_level_1_pc.ekv",0x66598b53d5fcd914,398),
("https://but.nobody-ca.me/r1/r1/cfg/cpu_level_2_pc.ekv","r1/cfg/cpu_level_2_pc.ekv",0x4c702bdfda5726f5,395),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_level_0_pc.ekv","r1/cfg/gpu_level_0_pc.ekv",0xe60f5291cb977fd0,442),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_level_1_pc.ekv","r1/cfg/gpu_level_1_pc.ekv",0xac5f865d290d8c0b,442),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_level_2_pc.ekv","r1/cfg/gpu_level_2_pc.ekv",0x158bd26e25c204d5,443),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_level_3_pc.ekv","r1/cfg/gpu_level_3_pc.ekv",0x6da6765fcebff64e,442),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_mem_level_0_pc.ekv","r1/cfg/gpu_mem_level_0_pc.ekv",0x24f8c5b522fc7462,61),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_mem_level_1_pc.ekv","r1/cfg/gpu_mem_level_1_pc.ekv",0x9da35660d0f80179,61),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_mem_level_2_pc.ekv","r1/cfg/gpu_mem_level_2_pc.ekv",0xc08ca6e66bfec366,61),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_mem_level_3_pc.ekv","r1/cfg/gpu_mem_level_3_pc.ekv",0x25014c9fb08bd525,61),
("https://but.nobody-ca.me/r1/r1/cfg/gpu_mem_level_4_pc.ekv","r1/cfg/gpu_mem_level_4_pc.ekv",0x239674274dbc17ba,61),
("https://but.nobody-ca.me/r1/r1/cfg/mem_level_0_pc.ekv","r1/cfg/mem_level_0_pc.ekv",0x68c3205053948e66,53),
("https://but.nobody-ca.me/r1/r1/cfg/mem_level_1_pc.ekv","r1/cfg/mem_level_1_pc.ekv",0x1ca5df60e66c635f,53),
("https://but.nobody-ca.me/r1/r1/cfg/mem_level_2_pc.ekv","r1/cfg/mem_level_2_pc.ekv",0xfcc41236fbb72585,53),
("https://but.nobody-ca.me/r1/r1/cfg/mem_level_3_pc.ekv","r1/cfg/mem_level_3_pc.ekv",0xb7b53791befe74c3,53),
("https://but.nobody-ca.me/r1/r1/cfg/video_settings_changed_quit.cfg","r1/cfg/video_settings_changed_quit.cfg",0x707db5f765aed6d8,4),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_english.txt","r1/cfg/client/language_font_control_english.txt",0x53e4be3fe02c0058,7),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_french.txt","r1/cfg/client/language_font_control_french.txt",0x96076bab4ba96232,6),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_german.txt","r1/cfg/client/language_font_control_german.txt",0xe1a5c362c8dc104b,6),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_italian.txt","r1/cfg/client/language_font_control_italian.txt",0x58f0e69e5bf00594,7),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_japanese.txt","r1/cfg/client/language_font_control_japanese.txt",0xd0a306279dacbb4d,8),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_korean.txt","r1/cfg/client/language_font_control_korean.txt",0x75dab118284de659,6),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_polish.txt","r1/cfg/client/language_font_control_polish.txt",0x4451a0ada1c5bc33,6),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_portuguese.txt","r1/cfg/client/language_font_control_portuguese.txt",0x7ddf649a228cc6c9,10),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_russian.txt","r1/cfg/client/language_font_control_russian.txt",0x7d433b2097c158b0,7),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_spanish.txt","r1/cfg/client/language_font_control_spanish.txt",0xdf0f1545d76ae577,7),
("https://but.nobody-ca.me/r1/r1/cfg/client/language_font_control_tchinese.txt","r1/cfg/client/language_font_control_tchinese.txt",0x189f9f4385356647,8),
("https://but.nobody-ca.me/r1/r1/cfg/client/st_data.bin","r1/cfg/client/st_data.bin",0xecf3f5718056216e,90403),
//("https://but.nobody-ca.me/r1/r1/media/ea.bik","r1/media/ea.bik",0x3f6f6a74ae1f3fb7,19262316),
("https://but.nobody-ca.me/r1/r1/media/intro.bik","r1/media/intro.bik",0xae5140c89fed0a11,175349816),
("https://but.nobody-ca.me/r1/r1/media/intro_captions.txt","r1/media/intro_captions.txt",0x7a70d5e102dafde3,387),
//("https://but.nobody-ca.me/r1/r1/media/legal.bik","r1/media/legal.bik",0x0eb665df24f8a5d9,927840),
//("https://but.nobody-ca.me/r1/r1/media/legal_captions.txt","r1/media/legal_captions.txt",0x692700211f3b1383,41),
("https://but.nobody-ca.me/r1/r1/media/menu_act01.bik","r1/media/menu_act01.bik",0xadd0925b541482c9,75002320),
//("https://but.nobody-ca.me/r1/r1/media/respawn.bik","r1/media/respawn.bik",0x9c65a5fca0ac31b1,19746596),
("https://but.nobody-ca.me/r1/r1/media/startupvids.txt","r1/media/startupvids.txt",0xdbecc211ff3e8efe,48),
("https://but.nobody-ca.me/r1/vpk/client_mp_delta_common.bsp.pak000_000.vpk","vpk/client_mp_delta_common.bsp.pak000_000.vpk",0x89587a474464eee3,12575532246),
("https://but.nobody-ca.me/r1/vpk/enable.txt","vpk/enable.txt",0xf0e67c946529f5d3,4),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_airbase.bsp.pak000_dir.vpk","vpk/englishclient_mp_airbase.bsp.pak000_dir.vpk",0x9464547f630d870d,296920),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/englishclient_mp_angel_city.bsp.pak000_dir.vpk",0x28007170ba14c417,414978),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_backwater.bsp.pak000_dir.vpk","vpk/englishclient_mp_backwater.bsp.pak000_dir.vpk",0xcaef1962e8566c34,345653),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/englishclient_mp_boneyard.bsp.pak000_dir.vpk",0xc82dcafe89ef856b,269645),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_box.bsp.pak000_dir.vpk","vpk/englishclient_mp_box.bsp.pak000_dir.vpk",0x9d1e47b1363a3d4f,16691),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_colony.bsp.pak000_dir.vpk","vpk/englishclient_mp_colony.bsp.pak000_dir.vpk",0x8b32960bf432d68e,370635),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_common.bsp.pak000_dir.vpk","vpk/englishclient_mp_common.bsp.pak000_dir.vpk",0x2b6c1c9faf5c4f31,2359958),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_corporate.bsp.pak000_dir.vpk","vpk/englishclient_mp_corporate.bsp.pak000_dir.vpk",0xc38a3c714e6496e5,331963),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_fracture.bsp.pak000_dir.vpk","vpk/englishclient_mp_fracture.bsp.pak000_dir.vpk",0x67e7061af1d45730,330810),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/englishclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x512222e3672e6a11,267624),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_haven.bsp.pak000_dir.vpk","vpk/englishclient_mp_haven.bsp.pak000_dir.vpk",0xb38ee1b56b73b772,320702),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/englishclient_mp_lagoon.bsp.pak000_dir.vpk",0x84650a86be008b6c,356496),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_mia.bsp.pak000_dir.vpk","vpk/englishclient_mp_mia.bsp.pak000_dir.vpk",0x0bff683a2a378b70,235701),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_nest2.bsp.pak000_dir.vpk","vpk/englishclient_mp_nest2.bsp.pak000_dir.vpk",0x4c5a00ed7468bb80,272550),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_nexus.bsp.pak000_dir.vpk","vpk/englishclient_mp_nexus.bsp.pak000_dir.vpk",0x6c90ad480eea4645,425692),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_npe.bsp.pak000_dir.vpk","vpk/englishclient_mp_npe.bsp.pak000_dir.vpk",0x3e2a058406c59db6,191910),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_o2.bsp.pak000_dir.vpk","vpk/englishclient_mp_o2.bsp.pak000_dir.vpk",0x176b56ddcb5f28cd,300681),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/englishclient_mp_outpost_207.bsp.pak000_dir.vpk",0x9b48464b876b1fed,275753),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_overlook.bsp.pak000_dir.vpk","vpk/englishclient_mp_overlook.bsp.pak000_dir.vpk",0xd77ff1794a7fb0e5,285698),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_relic.bsp.pak000_dir.vpk","vpk/englishclient_mp_relic.bsp.pak000_dir.vpk",0x7bd88aaff03351c1,392388),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_rise.bsp.pak000_dir.vpk","vpk/englishclient_mp_rise.bsp.pak000_dir.vpk",0xb7a12ad195bd46dc,198196),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_runoff.bsp.pak000_dir.vpk","vpk/englishclient_mp_runoff.bsp.pak000_dir.vpk",0xfef659012ac4971e,248412),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/englishclient_mp_sandtrap.bsp.pak000_dir.vpk",0xf6df46ecdce22856,241183),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/englishclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0xdc35b611731b9f67,325077),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_swampland.bsp.pak000_dir.vpk","vpk/englishclient_mp_swampland.bsp.pak000_dir.vpk",0x611da07d813ad084,221263),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_switchback.bsp.pak000_dir.vpk","vpk/englishclient_mp_switchback.bsp.pak000_dir.vpk",0x83b7c6565f612761,411126),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/englishclient_mp_training_ground.bsp.pak000_dir.vpk",0x33bbe0a9181f8cec,216217),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_wargames.bsp.pak000_dir.vpk","vpk/englishclient_mp_wargames.bsp.pak000_dir.vpk",0x08242684c52c8e03,384166),
("https://but.nobody-ca.me/r1/vpk/englishclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/englishclient_mp_zone_18.bsp.pak000_dir.vpk",0xaf7b523f270272eb,288128),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_airbase.bsp.pak000_dir.vpk","vpk/frenchclient_mp_airbase.bsp.pak000_dir.vpk",0x70a7e29f2fbc9b86,296920),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/frenchclient_mp_angel_city.bsp.pak000_dir.vpk",0x5fe15fdabe1f8636,414978),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_backwater.bsp.pak000_dir.vpk","vpk/frenchclient_mp_backwater.bsp.pak000_dir.vpk",0x1a96b23eb1936ff2,345653),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/frenchclient_mp_boneyard.bsp.pak000_dir.vpk",0x65cbeb07d9f93b9f,269645),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_box.bsp.pak000_dir.vpk","vpk/frenchclient_mp_box.bsp.pak000_dir.vpk",0x227258433f88d907,16691),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_colony.bsp.pak000_dir.vpk","vpk/frenchclient_mp_colony.bsp.pak000_dir.vpk",0xf73fd315f867e4ef,370635),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_common.bsp.pak000_dir.vpk","vpk/frenchclient_mp_common.bsp.pak000_dir.vpk",0x75c4f865f0ed02ab,2360191),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_corporate.bsp.pak000_dir.vpk","vpk/frenchclient_mp_corporate.bsp.pak000_dir.vpk",0x3a3052517b5ac757,331963),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_fracture.bsp.pak000_dir.vpk","vpk/frenchclient_mp_fracture.bsp.pak000_dir.vpk",0xce3f1b33e08adb1d,330810),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/frenchclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x3c1c70d5ce17c8f0,267624),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_haven.bsp.pak000_dir.vpk","vpk/frenchclient_mp_haven.bsp.pak000_dir.vpk",0xa80bfe50aca34b1d,320702),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/frenchclient_mp_lagoon.bsp.pak000_dir.vpk",0x0cc2b5d47bfc536a,356496),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_mia.bsp.pak000_dir.vpk","vpk/frenchclient_mp_mia.bsp.pak000_dir.vpk",0xbfddada5826b2cc8,235701),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_nest2.bsp.pak000_dir.vpk","vpk/frenchclient_mp_nest2.bsp.pak000_dir.vpk",0x7da524dc447f3323,272550),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_nexus.bsp.pak000_dir.vpk","vpk/frenchclient_mp_nexus.bsp.pak000_dir.vpk",0x3715870d49e0f03c,425692),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_npe.bsp.pak000_dir.vpk","vpk/frenchclient_mp_npe.bsp.pak000_dir.vpk",0xc3863f416e139cdc,191910),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_o2.bsp.pak000_dir.vpk","vpk/frenchclient_mp_o2.bsp.pak000_dir.vpk",0xdfd1c4f5a120778e,300681),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/frenchclient_mp_outpost_207.bsp.pak000_dir.vpk",0xc397e1498bb664c2,275753),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_overlook.bsp.pak000_dir.vpk","vpk/frenchclient_mp_overlook.bsp.pak000_dir.vpk",0x163ee07b30108062,285698),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_relic.bsp.pak000_dir.vpk","vpk/frenchclient_mp_relic.bsp.pak000_dir.vpk",0xa05602f67c8d7c5b,392388),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_rise.bsp.pak000_dir.vpk","vpk/frenchclient_mp_rise.bsp.pak000_dir.vpk",0x09724fce9a379e1a,198196),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_runoff.bsp.pak000_dir.vpk","vpk/frenchclient_mp_runoff.bsp.pak000_dir.vpk",0xb5b907aa1e66c679,248412),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/frenchclient_mp_sandtrap.bsp.pak000_dir.vpk",0xf349463bf9f82fa9,241183),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/frenchclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x47d6b3b7d6ab9478,325077),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_swampland.bsp.pak000_dir.vpk","vpk/frenchclient_mp_swampland.bsp.pak000_dir.vpk",0xe3c0a446315947ab,221263),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_switchback.bsp.pak000_dir.vpk","vpk/frenchclient_mp_switchback.bsp.pak000_dir.vpk",0xffee26971ae94bd2,411126),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/frenchclient_mp_training_ground.bsp.pak000_dir.vpk",0x4511048426aa35f7,216217),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_wargames.bsp.pak000_dir.vpk","vpk/frenchclient_mp_wargames.bsp.pak000_dir.vpk",0x675d91bc90154343,384166),
("https://but.nobody-ca.me/r1/vpk/frenchclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/frenchclient_mp_zone_18.bsp.pak000_dir.vpk",0xa7bc92c4a1bf13f9,288128),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_airbase.bsp.pak000_dir.vpk","vpk/germanclient_mp_airbase.bsp.pak000_dir.vpk",0xbd7e5cd6d2b85701,296920),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/germanclient_mp_angel_city.bsp.pak000_dir.vpk",0xadb8474c7024873b,414978),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_backwater.bsp.pak000_dir.vpk","vpk/germanclient_mp_backwater.bsp.pak000_dir.vpk",0xe571ed016aae9c95,345653),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/germanclient_mp_boneyard.bsp.pak000_dir.vpk",0x99f3718eb8999f3e,269645),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_box.bsp.pak000_dir.vpk","vpk/germanclient_mp_box.bsp.pak000_dir.vpk",0xb69451f3ee0c920c,16691),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_colony.bsp.pak000_dir.vpk","vpk/germanclient_mp_colony.bsp.pak000_dir.vpk",0xe31aeb10fe7e1696,370635),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_common.bsp.pak000_dir.vpk","vpk/germanclient_mp_common.bsp.pak000_dir.vpk",0xb5e7eb8f1870b911,2360191),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_corporate.bsp.pak000_dir.vpk","vpk/germanclient_mp_corporate.bsp.pak000_dir.vpk",0x2abbcfae44032269,331963),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_fracture.bsp.pak000_dir.vpk","vpk/germanclient_mp_fracture.bsp.pak000_dir.vpk",0x9ce7f7a737c3275c,330810),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/germanclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x71f292b0acb01ee5,267624),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_haven.bsp.pak000_dir.vpk","vpk/germanclient_mp_haven.bsp.pak000_dir.vpk",0x423ec986f1b3bfe1,320702),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/germanclient_mp_lagoon.bsp.pak000_dir.vpk",0xf7e41ad2f72e5385,356496),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_mia.bsp.pak000_dir.vpk","vpk/germanclient_mp_mia.bsp.pak000_dir.vpk",0x4fc6706ad6d55b02,235701),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_nest2.bsp.pak000_dir.vpk","vpk/germanclient_mp_nest2.bsp.pak000_dir.vpk",0xb8d799a97144e436,272550),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_nexus.bsp.pak000_dir.vpk","vpk/germanclient_mp_nexus.bsp.pak000_dir.vpk",0xcc1f807c60c2777f,425692),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_npe.bsp.pak000_dir.vpk","vpk/germanclient_mp_npe.bsp.pak000_dir.vpk",0xd7306f19ec29d781,191910),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_o2.bsp.pak000_dir.vpk","vpk/germanclient_mp_o2.bsp.pak000_dir.vpk",0x3183cb243c36d188,300681),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/germanclient_mp_outpost_207.bsp.pak000_dir.vpk",0x5476d4d51eccd020,275753),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_overlook.bsp.pak000_dir.vpk","vpk/germanclient_mp_overlook.bsp.pak000_dir.vpk",0x84429fdb25211b57,285698),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_relic.bsp.pak000_dir.vpk","vpk/germanclient_mp_relic.bsp.pak000_dir.vpk",0xa028c324bc7ae280,392388),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_rise.bsp.pak000_dir.vpk","vpk/germanclient_mp_rise.bsp.pak000_dir.vpk",0xf9e9e2e8de63d583,198196),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_runoff.bsp.pak000_dir.vpk","vpk/germanclient_mp_runoff.bsp.pak000_dir.vpk",0x0db54d264aa95506,248412),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/germanclient_mp_sandtrap.bsp.pak000_dir.vpk",0x421efae430d7303f,241183),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/germanclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x4f2f27039e2c0fcc,325077),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_swampland.bsp.pak000_dir.vpk","vpk/germanclient_mp_swampland.bsp.pak000_dir.vpk",0x30342849324f32ef,221263),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_switchback.bsp.pak000_dir.vpk","vpk/germanclient_mp_switchback.bsp.pak000_dir.vpk",0x83864bdc68971511,411126),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/germanclient_mp_training_ground.bsp.pak000_dir.vpk",0xf358e28f121acc31,216217),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_wargames.bsp.pak000_dir.vpk","vpk/germanclient_mp_wargames.bsp.pak000_dir.vpk",0x365da95d0b4adaad,384166),
("https://but.nobody-ca.me/r1/vpk/germanclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/germanclient_mp_zone_18.bsp.pak000_dir.vpk",0x78496cf4056f1cb2,288128),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_airbase.bsp.pak000_dir.vpk","vpk/italianclient_mp_airbase.bsp.pak000_dir.vpk",0xcf8b5293d53f8006,296920),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/italianclient_mp_angel_city.bsp.pak000_dir.vpk",0x20f5754dfe8c1df1,414978),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_backwater.bsp.pak000_dir.vpk","vpk/italianclient_mp_backwater.bsp.pak000_dir.vpk",0xdd3c3befb786652f,345653),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/italianclient_mp_boneyard.bsp.pak000_dir.vpk",0x9ec876d28110db20,269645),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_box.bsp.pak000_dir.vpk","vpk/italianclient_mp_box.bsp.pak000_dir.vpk",0x70ad87cc97164dd9,16691),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_colony.bsp.pak000_dir.vpk","vpk/italianclient_mp_colony.bsp.pak000_dir.vpk",0xd7d471ff6eeb2476,370635),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_common.bsp.pak000_dir.vpk","vpk/italianclient_mp_common.bsp.pak000_dir.vpk",0xe38b03f6fc896c11,2360195),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_corporate.bsp.pak000_dir.vpk","vpk/italianclient_mp_corporate.bsp.pak000_dir.vpk",0xf24234f004965066,331963),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_fracture.bsp.pak000_dir.vpk","vpk/italianclient_mp_fracture.bsp.pak000_dir.vpk",0x4fa6213047b47432,330810),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/italianclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x6066679b21258cba,267624),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_haven.bsp.pak000_dir.vpk","vpk/italianclient_mp_haven.bsp.pak000_dir.vpk",0xb325833210720a23,320702),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/italianclient_mp_lagoon.bsp.pak000_dir.vpk",0x824159e09876a74c,356496),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_mia.bsp.pak000_dir.vpk","vpk/italianclient_mp_mia.bsp.pak000_dir.vpk",0xc5df0e2780956573,235701),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_nest2.bsp.pak000_dir.vpk","vpk/italianclient_mp_nest2.bsp.pak000_dir.vpk",0xda49af16a98c2d3a,272550),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_nexus.bsp.pak000_dir.vpk","vpk/italianclient_mp_nexus.bsp.pak000_dir.vpk",0x16b16f0b4e047b89,425692),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_npe.bsp.pak000_dir.vpk","vpk/italianclient_mp_npe.bsp.pak000_dir.vpk",0x7667489c867ea23b,191910),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_o2.bsp.pak000_dir.vpk","vpk/italianclient_mp_o2.bsp.pak000_dir.vpk",0x2da354c1967109fd,300681),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/italianclient_mp_outpost_207.bsp.pak000_dir.vpk",0x6ca87391be2a1f3c,275753),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_overlook.bsp.pak000_dir.vpk","vpk/italianclient_mp_overlook.bsp.pak000_dir.vpk",0xcc36f2f8040d0afe,285698),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_relic.bsp.pak000_dir.vpk","vpk/italianclient_mp_relic.bsp.pak000_dir.vpk",0xe94f577709261016,392388),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_rise.bsp.pak000_dir.vpk","vpk/italianclient_mp_rise.bsp.pak000_dir.vpk",0xc24befb59ef547fb,198196),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_runoff.bsp.pak000_dir.vpk","vpk/italianclient_mp_runoff.bsp.pak000_dir.vpk",0xd2dea43aafd05a51,248412),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/italianclient_mp_sandtrap.bsp.pak000_dir.vpk",0x83ebdf8ae2541f67,241183),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/italianclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x447dea14fa0df3bc,325077),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_swampland.bsp.pak000_dir.vpk","vpk/italianclient_mp_swampland.bsp.pak000_dir.vpk",0x4e550485d3ee1a8f,221263),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_switchback.bsp.pak000_dir.vpk","vpk/italianclient_mp_switchback.bsp.pak000_dir.vpk",0xcad46036f1f881c9,411126),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/italianclient_mp_training_ground.bsp.pak000_dir.vpk",0xa221d4220b245766,216217),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_wargames.bsp.pak000_dir.vpk","vpk/italianclient_mp_wargames.bsp.pak000_dir.vpk",0x0bdf74d87190327a,384166),
("https://but.nobody-ca.me/r1/vpk/italianclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/italianclient_mp_zone_18.bsp.pak000_dir.vpk",0x99fa3f202e2004fc,288128),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_airbase.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_airbase.bsp.pak000_dir.vpk",0xc8eaea664402481d,296920),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_angel_city.bsp.pak000_dir.vpk",0x7c18441979f5bbcf,414978),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_backwater.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_backwater.bsp.pak000_dir.vpk",0x4961afd5a005bdde,345653),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_boneyard.bsp.pak000_dir.vpk",0x0f219668c0c59f2d,269645),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_box.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_box.bsp.pak000_dir.vpk",0x02a5b6600505d193,16691),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_colony.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_colony.bsp.pak000_dir.vpk",0x4b7b2b63c689321e,370635),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_common.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_common.bsp.pak000_dir.vpk",0x616ebede83c73a6a,2360199),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_corporate.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_corporate.bsp.pak000_dir.vpk",0x8f746a1b283e0100,331963),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_fracture.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_fracture.bsp.pak000_dir.vpk",0x309662e8d71d9d3c,330810),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x21155fb9dbf95373,267624),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_haven.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_haven.bsp.pak000_dir.vpk",0xaeb99834cd8cf07b,320702),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_lagoon.bsp.pak000_dir.vpk",0xfb98f3178a0104af,356496),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_mia.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_mia.bsp.pak000_dir.vpk",0xe748e5b934780b52,235701),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_nest2.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_nest2.bsp.pak000_dir.vpk",0x6ba3d3edf5cb620e,272550),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_nexus.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_nexus.bsp.pak000_dir.vpk",0xa881f8bf7fe29233,425692),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_npe.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_npe.bsp.pak000_dir.vpk",0x389ebefaac1b48cb,191910),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_o2.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_o2.bsp.pak000_dir.vpk",0x3412067b6ff59683,300681),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_outpost_207.bsp.pak000_dir.vpk",0xaf01d137d36531ea,275753),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_overlook.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_overlook.bsp.pak000_dir.vpk",0x2bb527f050877a3e,285698),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_relic.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_relic.bsp.pak000_dir.vpk",0x7179a0c503f8c592,392388),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_rise.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_rise.bsp.pak000_dir.vpk",0x1452c8afa6fdacc8,198196),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_runoff.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_runoff.bsp.pak000_dir.vpk",0x032509181efdffaa,248412),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_sandtrap.bsp.pak000_dir.vpk",0x0b981aac302a3cff,241183),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0xbc5cf0fe17cc3a81,325077),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_swampland.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_swampland.bsp.pak000_dir.vpk",0x7d4a9017cf657194,221263),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_switchback.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_switchback.bsp.pak000_dir.vpk",0x071aff989c508af9,411126),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_training_ground.bsp.pak000_dir.vpk",0xa46c18d888dc14a9,216217),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_wargames.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_wargames.bsp.pak000_dir.vpk",0x864721dcb8c4cdfc,384166),
("https://but.nobody-ca.me/r1/vpk/japaneseclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/japaneseclient_mp_zone_18.bsp.pak000_dir.vpk",0x0c513eed9f6355f4,288128),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_airbase.bsp.pak000_dir.vpk","vpk/koreanclient_mp_airbase.bsp.pak000_dir.vpk",0xb2d7db1557c8824a,296920),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/koreanclient_mp_angel_city.bsp.pak000_dir.vpk",0xbe8b5e1e0254cdbf,414978),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_backwater.bsp.pak000_dir.vpk","vpk/koreanclient_mp_backwater.bsp.pak000_dir.vpk",0x400416f22f474141,345653),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/koreanclient_mp_boneyard.bsp.pak000_dir.vpk",0xecaf5ea0ef0a6830,269645),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_box.bsp.pak000_dir.vpk","vpk/koreanclient_mp_box.bsp.pak000_dir.vpk",0xea073a529e66ca5b,16691),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_colony.bsp.pak000_dir.vpk","vpk/koreanclient_mp_colony.bsp.pak000_dir.vpk",0x982252e457e8f76e,370635),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_common.bsp.pak000_dir.vpk","vpk/koreanclient_mp_common.bsp.pak000_dir.vpk",0xda79c1ec874385f9,2360191),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_corporate.bsp.pak000_dir.vpk","vpk/koreanclient_mp_corporate.bsp.pak000_dir.vpk",0x2ebb13ff6b241fd1,331963),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_fracture.bsp.pak000_dir.vpk","vpk/koreanclient_mp_fracture.bsp.pak000_dir.vpk",0x19c7a2b42e778c71,330810),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/koreanclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x9f0ef374990e7afc,267624),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_haven.bsp.pak000_dir.vpk","vpk/koreanclient_mp_haven.bsp.pak000_dir.vpk",0x95112e0a5d2807f0,320702),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/koreanclient_mp_lagoon.bsp.pak000_dir.vpk",0xa6b0c17ae77b530b,356496),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_mia.bsp.pak000_dir.vpk","vpk/koreanclient_mp_mia.bsp.pak000_dir.vpk",0x94261d7e45b48e69,235701),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_nest2.bsp.pak000_dir.vpk","vpk/koreanclient_mp_nest2.bsp.pak000_dir.vpk",0xbccae7ecffbb665b,272550),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_nexus.bsp.pak000_dir.vpk","vpk/koreanclient_mp_nexus.bsp.pak000_dir.vpk",0x5d4c9eef8c5ff196,425692),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_npe.bsp.pak000_dir.vpk","vpk/koreanclient_mp_npe.bsp.pak000_dir.vpk",0x0a7bf41d60defbf8,191910),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_o2.bsp.pak000_dir.vpk","vpk/koreanclient_mp_o2.bsp.pak000_dir.vpk",0xc110c31ff55487ec,300681),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/koreanclient_mp_outpost_207.bsp.pak000_dir.vpk",0x2c55813cd70e8ad9,275753),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_overlook.bsp.pak000_dir.vpk","vpk/koreanclient_mp_overlook.bsp.pak000_dir.vpk",0x8d62ec8c188e1435,285698),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_relic.bsp.pak000_dir.vpk","vpk/koreanclient_mp_relic.bsp.pak000_dir.vpk",0x1d0189a966ca7b04,392388),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_rise.bsp.pak000_dir.vpk","vpk/koreanclient_mp_rise.bsp.pak000_dir.vpk",0x19ca57db23ba7790,198196),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_runoff.bsp.pak000_dir.vpk","vpk/koreanclient_mp_runoff.bsp.pak000_dir.vpk",0x40c277b61a57c66c,248412),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/koreanclient_mp_sandtrap.bsp.pak000_dir.vpk",0x56dbde225fc0f8e8,241183),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/koreanclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x8a9cbd5c100637ca,325077),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_swampland.bsp.pak000_dir.vpk","vpk/koreanclient_mp_swampland.bsp.pak000_dir.vpk",0x41cec2f3dedaa8ed,221263),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_switchback.bsp.pak000_dir.vpk","vpk/koreanclient_mp_switchback.bsp.pak000_dir.vpk",0x062ae45ad52a890c,411126),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/koreanclient_mp_training_ground.bsp.pak000_dir.vpk",0x217a84be0039e80c,216217),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_wargames.bsp.pak000_dir.vpk","vpk/koreanclient_mp_wargames.bsp.pak000_dir.vpk",0xcc600b06573cef5e,384166),
("https://but.nobody-ca.me/r1/vpk/koreanclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/koreanclient_mp_zone_18.bsp.pak000_dir.vpk",0x613d8a1423709759,288128),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_airbase.bsp.pak000_dir.vpk","vpk/polishclient_mp_airbase.bsp.pak000_dir.vpk",0xedb9ce1826413a04,296920),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/polishclient_mp_angel_city.bsp.pak000_dir.vpk",0x62736383fe604562,414978),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_backwater.bsp.pak000_dir.vpk","vpk/polishclient_mp_backwater.bsp.pak000_dir.vpk",0x71fc89c48a385c17,345653),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/polishclient_mp_boneyard.bsp.pak000_dir.vpk",0x417749f373019617,269645),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_box.bsp.pak000_dir.vpk","vpk/polishclient_mp_box.bsp.pak000_dir.vpk",0xe097e49fcf6376a4,16691),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_colony.bsp.pak000_dir.vpk","vpk/polishclient_mp_colony.bsp.pak000_dir.vpk",0x9b33e25b7d67ffc4,370635),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_common.bsp.pak000_dir.vpk","vpk/polishclient_mp_common.bsp.pak000_dir.vpk",0x6eca803df63ae0c7,2360191),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_corporate.bsp.pak000_dir.vpk","vpk/polishclient_mp_corporate.bsp.pak000_dir.vpk",0x2f934febf2889050,331963),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_fracture.bsp.pak000_dir.vpk","vpk/polishclient_mp_fracture.bsp.pak000_dir.vpk",0x7605e71a0667c2b3,330810),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/polishclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x952f90bab08aa2b0,267624),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_haven.bsp.pak000_dir.vpk","vpk/polishclient_mp_haven.bsp.pak000_dir.vpk",0x90d34c8645e1aef6,320702),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/polishclient_mp_lagoon.bsp.pak000_dir.vpk",0x01373d9a813eb531,356496),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_mia.bsp.pak000_dir.vpk","vpk/polishclient_mp_mia.bsp.pak000_dir.vpk",0x75e039d7566adc01,235701),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_nest2.bsp.pak000_dir.vpk","vpk/polishclient_mp_nest2.bsp.pak000_dir.vpk",0x6e5ad2b4224e338e,272550),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_nexus.bsp.pak000_dir.vpk","vpk/polishclient_mp_nexus.bsp.pak000_dir.vpk",0xa5e45839e8d5686b,425692),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_npe.bsp.pak000_dir.vpk","vpk/polishclient_mp_npe.bsp.pak000_dir.vpk",0x42d25e673b37fbcd,191910),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_o2.bsp.pak000_dir.vpk","vpk/polishclient_mp_o2.bsp.pak000_dir.vpk",0x57b624c7bb0e24dc,300681),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/polishclient_mp_outpost_207.bsp.pak000_dir.vpk",0xc31fc4ae4b6a3921,275753),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_overlook.bsp.pak000_dir.vpk","vpk/polishclient_mp_overlook.bsp.pak000_dir.vpk",0xdd830bb8fea55c73,285698),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_relic.bsp.pak000_dir.vpk","vpk/polishclient_mp_relic.bsp.pak000_dir.vpk",0x71f97ef78ebe41ec,392388),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_rise.bsp.pak000_dir.vpk","vpk/polishclient_mp_rise.bsp.pak000_dir.vpk",0x333a84ae25a2833f,198196),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_runoff.bsp.pak000_dir.vpk","vpk/polishclient_mp_runoff.bsp.pak000_dir.vpk",0xdd4a5c2f48d5c28f,248412),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/polishclient_mp_sandtrap.bsp.pak000_dir.vpk",0x8cb90c5830d5b258,241183),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/polishclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x5fd2b3366fb7a33a,325077),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_swampland.bsp.pak000_dir.vpk","vpk/polishclient_mp_swampland.bsp.pak000_dir.vpk",0x367514b633bbb05d,221263),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_switchback.bsp.pak000_dir.vpk","vpk/polishclient_mp_switchback.bsp.pak000_dir.vpk",0x9f5430066386dee5,411126),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/polishclient_mp_training_ground.bsp.pak000_dir.vpk",0xde00d12c42fb16d7,216217),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_wargames.bsp.pak000_dir.vpk","vpk/polishclient_mp_wargames.bsp.pak000_dir.vpk",0x5b338d16deadcb76,384166),
("https://but.nobody-ca.me/r1/vpk/polishclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/polishclient_mp_zone_18.bsp.pak000_dir.vpk",0xb60318813f00fd5e,288128),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_airbase.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_airbase.bsp.pak000_dir.vpk",0xa93b1bd02fa6f6df,296920),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_angel_city.bsp.pak000_dir.vpk",0x7a08eb9bf7162eb5,414978),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_backwater.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_backwater.bsp.pak000_dir.vpk",0xd2dad3f82db64bbf,345653),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_boneyard.bsp.pak000_dir.vpk",0x12fc9b1b2c842b46,269645),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_box.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_box.bsp.pak000_dir.vpk",0x8b6d93bbe76d1e31,16691),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_colony.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_colony.bsp.pak000_dir.vpk",0x0a3832d59a23d729,370635),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_common.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_common.bsp.pak000_dir.vpk",0xf713de5c207a999d,2360207),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_corporate.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_corporate.bsp.pak000_dir.vpk",0xab6a78dd40d0d4ae,331963),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_fracture.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_fracture.bsp.pak000_dir.vpk",0x8c77c4323dca2a19,330810),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x09d6cb856a2f1290,267624),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_haven.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_haven.bsp.pak000_dir.vpk",0x8ff50574834fcf85,320702),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_lagoon.bsp.pak000_dir.vpk",0xa85b5e729cd8c265,356496),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_mia.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_mia.bsp.pak000_dir.vpk",0x755894c5600e42b6,235701),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_nest2.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_nest2.bsp.pak000_dir.vpk",0x8de8d6f5d2340df9,272550),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_nexus.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_nexus.bsp.pak000_dir.vpk",0x3f274ff4416a53ef,425692),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_npe.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_npe.bsp.pak000_dir.vpk",0x0e30646c44534541,191910),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_o2.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_o2.bsp.pak000_dir.vpk",0x5b79abaf9e67f34c,300681),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_outpost_207.bsp.pak000_dir.vpk",0x98331773623b309f,275753),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_overlook.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_overlook.bsp.pak000_dir.vpk",0xaa940134396f003e,285698),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_relic.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_relic.bsp.pak000_dir.vpk",0xe4f45ccc39d39aad,392388),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_rise.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_rise.bsp.pak000_dir.vpk",0x38a7e0309cdc6adb,198196),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_runoff.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_runoff.bsp.pak000_dir.vpk",0x69fd8ef49db8acc2,248412),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_sandtrap.bsp.pak000_dir.vpk",0x1ae27876038e7278,241183),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x02f9b3c4fa7d32c2,325077),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_swampland.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_swampland.bsp.pak000_dir.vpk",0xd1b7a8ca3f9c0c6b,221263),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_switchback.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_switchback.bsp.pak000_dir.vpk",0x07d005088c18fe39,411126),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_training_ground.bsp.pak000_dir.vpk",0x300568e036a9f613,216217),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_wargames.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_wargames.bsp.pak000_dir.vpk",0x320a7cf9a280a1e9,384166),
("https://but.nobody-ca.me/r1/vpk/portugueseclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/portugueseclient_mp_zone_18.bsp.pak000_dir.vpk",0x15ac67c267f62447,288128),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_airbase.bsp.pak000_dir.vpk","vpk/russianclient_mp_airbase.bsp.pak000_dir.vpk",0xe86b6261dc1f4140,296920),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/russianclient_mp_angel_city.bsp.pak000_dir.vpk",0x956749ec1e53037d,414978),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_backwater.bsp.pak000_dir.vpk","vpk/russianclient_mp_backwater.bsp.pak000_dir.vpk",0x539fb04cbc595566,345653),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/russianclient_mp_boneyard.bsp.pak000_dir.vpk",0xa5b458d1e530ba61,269645),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_box.bsp.pak000_dir.vpk","vpk/russianclient_mp_box.bsp.pak000_dir.vpk",0x862dc9ae0839d8ae,16691),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_colony.bsp.pak000_dir.vpk","vpk/russianclient_mp_colony.bsp.pak000_dir.vpk",0x3e7526e58fa78f24,370635),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_common.bsp.pak000_dir.vpk","vpk/russianclient_mp_common.bsp.pak000_dir.vpk",0x58aa6b908f79bd10,2360195),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_corporate.bsp.pak000_dir.vpk","vpk/russianclient_mp_corporate.bsp.pak000_dir.vpk",0xd342ac04c58c3c7e,331963),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_fracture.bsp.pak000_dir.vpk","vpk/russianclient_mp_fracture.bsp.pak000_dir.vpk",0xc8aea823441b7337,330810),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/russianclient_mp_harmony_mines.bsp.pak000_dir.vpk",0xb4b8bc18c92704bb,267624),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_haven.bsp.pak000_dir.vpk","vpk/russianclient_mp_haven.bsp.pak000_dir.vpk",0xbb496fd64b103c8d,320702),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/russianclient_mp_lagoon.bsp.pak000_dir.vpk",0x5b747356d3d2a47a,356496),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_mia.bsp.pak000_dir.vpk","vpk/russianclient_mp_mia.bsp.pak000_dir.vpk",0x9d9ba5d1bc2f26c6,235701),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_nest2.bsp.pak000_dir.vpk","vpk/russianclient_mp_nest2.bsp.pak000_dir.vpk",0x8cf15196c9fc927c,272550),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_nexus.bsp.pak000_dir.vpk","vpk/russianclient_mp_nexus.bsp.pak000_dir.vpk",0xf0e3960f9d514598,425692),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_npe.bsp.pak000_dir.vpk","vpk/russianclient_mp_npe.bsp.pak000_dir.vpk",0x2333542d7433068c,191910),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_o2.bsp.pak000_dir.vpk","vpk/russianclient_mp_o2.bsp.pak000_dir.vpk",0x63cc9145382ae50a,300681),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/russianclient_mp_outpost_207.bsp.pak000_dir.vpk",0xa8c2270ba3bf395e,275753),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_overlook.bsp.pak000_dir.vpk","vpk/russianclient_mp_overlook.bsp.pak000_dir.vpk",0x20cf92f54768d9ab,285698),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_relic.bsp.pak000_dir.vpk","vpk/russianclient_mp_relic.bsp.pak000_dir.vpk",0x6448ccfef067e254,392388),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_rise.bsp.pak000_dir.vpk","vpk/russianclient_mp_rise.bsp.pak000_dir.vpk",0xce234cb144be8f87,198196),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_runoff.bsp.pak000_dir.vpk","vpk/russianclient_mp_runoff.bsp.pak000_dir.vpk",0x1285f52898f01703,248412),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/russianclient_mp_sandtrap.bsp.pak000_dir.vpk",0x9ec298aa8efabeb4,241183),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/russianclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x0773514eb672a854,325077),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_swampland.bsp.pak000_dir.vpk","vpk/russianclient_mp_swampland.bsp.pak000_dir.vpk",0x6e800bdcc5d1684d,221263),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_switchback.bsp.pak000_dir.vpk","vpk/russianclient_mp_switchback.bsp.pak000_dir.vpk",0xfd0256e898eedb03,411126),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/russianclient_mp_training_ground.bsp.pak000_dir.vpk",0x731930756622e60d,216217),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_wargames.bsp.pak000_dir.vpk","vpk/russianclient_mp_wargames.bsp.pak000_dir.vpk",0xf0b96b1943dc4c88,384166),
("https://but.nobody-ca.me/r1/vpk/russianclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/russianclient_mp_zone_18.bsp.pak000_dir.vpk",0x4ec0c482e45e3c12,288128),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_airbase.bsp.pak000_dir.vpk","vpk/spanishclient_mp_airbase.bsp.pak000_dir.vpk",0xdccd12a5bf9e1765,296920),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/spanishclient_mp_angel_city.bsp.pak000_dir.vpk",0x4c0ae19916c69751,414978),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_backwater.bsp.pak000_dir.vpk","vpk/spanishclient_mp_backwater.bsp.pak000_dir.vpk",0xb123bd7d05709988,345653),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/spanishclient_mp_boneyard.bsp.pak000_dir.vpk",0xc09ba9a7542c631c,269645),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_box.bsp.pak000_dir.vpk","vpk/spanishclient_mp_box.bsp.pak000_dir.vpk",0xe54428e82bef6c92,16691),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_colony.bsp.pak000_dir.vpk","vpk/spanishclient_mp_colony.bsp.pak000_dir.vpk",0x15f12e4ee9dcc196,370635),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_common.bsp.pak000_dir.vpk","vpk/spanishclient_mp_common.bsp.pak000_dir.vpk",0x980e217acd5b31f3,2360195),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_corporate.bsp.pak000_dir.vpk","vpk/spanishclient_mp_corporate.bsp.pak000_dir.vpk",0x6027db48921375b8,331963),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_fracture.bsp.pak000_dir.vpk","vpk/spanishclient_mp_fracture.bsp.pak000_dir.vpk",0xeb3c2bc30a470a3f,330810),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/spanishclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x934c3c2c63ba1519,267624),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_haven.bsp.pak000_dir.vpk","vpk/spanishclient_mp_haven.bsp.pak000_dir.vpk",0x2dcb70b2309ad463,320702),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/spanishclient_mp_lagoon.bsp.pak000_dir.vpk",0x423da750688ba21d,356496),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_mia.bsp.pak000_dir.vpk","vpk/spanishclient_mp_mia.bsp.pak000_dir.vpk",0x320b96cb2712c024,235701),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_nest2.bsp.pak000_dir.vpk","vpk/spanishclient_mp_nest2.bsp.pak000_dir.vpk",0xea3fd7d101a46a8b,272550),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_nexus.bsp.pak000_dir.vpk","vpk/spanishclient_mp_nexus.bsp.pak000_dir.vpk",0x10466f0567912a41,425692),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_npe.bsp.pak000_dir.vpk","vpk/spanishclient_mp_npe.bsp.pak000_dir.vpk",0x9fc5a9564ba572c8,191910),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_o2.bsp.pak000_dir.vpk","vpk/spanishclient_mp_o2.bsp.pak000_dir.vpk",0x4f0321a7c76ad848,300681),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/spanishclient_mp_outpost_207.bsp.pak000_dir.vpk",0x67fdf0fdd006d3b1,275753),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_overlook.bsp.pak000_dir.vpk","vpk/spanishclient_mp_overlook.bsp.pak000_dir.vpk",0x6d0ff0eacdb1ca3b,285698),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_relic.bsp.pak000_dir.vpk","vpk/spanishclient_mp_relic.bsp.pak000_dir.vpk",0x18383744c4c733fa,392388),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_rise.bsp.pak000_dir.vpk","vpk/spanishclient_mp_rise.bsp.pak000_dir.vpk",0x5b75976d591c4884,198196),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_runoff.bsp.pak000_dir.vpk","vpk/spanishclient_mp_runoff.bsp.pak000_dir.vpk",0x30c0def5f1c71647,248412),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/spanishclient_mp_sandtrap.bsp.pak000_dir.vpk",0xc621f7783d2f2ebf,241183),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/spanishclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x3ae136b51a4d46f5,325077),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_swampland.bsp.pak000_dir.vpk","vpk/spanishclient_mp_swampland.bsp.pak000_dir.vpk",0xb61a2bc0759f3c0c,221263),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_switchback.bsp.pak000_dir.vpk","vpk/spanishclient_mp_switchback.bsp.pak000_dir.vpk",0xbd8bb8e82fe0776f,411126),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/spanishclient_mp_training_ground.bsp.pak000_dir.vpk",0xab587a913a22f561,216217),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_wargames.bsp.pak000_dir.vpk","vpk/spanishclient_mp_wargames.bsp.pak000_dir.vpk",0x48f30b93830efbfe,384166),
("https://but.nobody-ca.me/r1/vpk/spanishclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/spanishclient_mp_zone_18.bsp.pak000_dir.vpk",0x9a4511eb61e1678e,288128),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_airbase.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_airbase.bsp.pak000_dir.vpk",0xd2c38a73e77688a9,296920),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_angel_city.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_angel_city.bsp.pak000_dir.vpk",0xeca18278d55eb9c0,414978),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_backwater.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_backwater.bsp.pak000_dir.vpk",0x18ab59b0743abdb0,345653),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_boneyard.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_boneyard.bsp.pak000_dir.vpk",0x0e755efbb2d377bb,269645),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_box.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_box.bsp.pak000_dir.vpk",0x1e9cb8fc8e89a2af,16691),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_colony.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_colony.bsp.pak000_dir.vpk",0x6a0fd92d9c15ed9f,370635),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_common.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_common.bsp.pak000_dir.vpk",0xf7a83eb9cb0a8f0c,2360199),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_corporate.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_corporate.bsp.pak000_dir.vpk",0x8669f12b748f1e65,331963),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_fracture.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_fracture.bsp.pak000_dir.vpk",0x246e4ceb33243e93,330810),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_harmony_mines.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_harmony_mines.bsp.pak000_dir.vpk",0x17a06a65ad24a36b,267624),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_haven.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_haven.bsp.pak000_dir.vpk",0x344d6443803aa6d2,320702),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_lagoon.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_lagoon.bsp.pak000_dir.vpk",0xda226f802d596242,356496),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_mia.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_mia.bsp.pak000_dir.vpk",0x9769ff96a2a0b5d9,235701),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_nest2.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_nest2.bsp.pak000_dir.vpk",0x53f3eb9366d0b418,272550),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_nexus.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_nexus.bsp.pak000_dir.vpk",0xa5249a3d776d1d6c,425692),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_npe.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_npe.bsp.pak000_dir.vpk",0x2f9667406c068679,191910),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_o2.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_o2.bsp.pak000_dir.vpk",0x32968c932d5e38d0,300681),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_outpost_207.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_outpost_207.bsp.pak000_dir.vpk",0x38974e09621ea585,275753),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_overlook.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_overlook.bsp.pak000_dir.vpk",0x682ddfbdbde09ae4,285698),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_relic.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_relic.bsp.pak000_dir.vpk",0x062166bc171a9f52,392388),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_rise.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_rise.bsp.pak000_dir.vpk",0x2e434c995daaced2,198196),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_runoff.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_runoff.bsp.pak000_dir.vpk",0xc9b1d547f5411829,248412),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_sandtrap.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_sandtrap.bsp.pak000_dir.vpk",0xcd8335b93320e1e0,241183),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_smugglers_cove.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_smugglers_cove.bsp.pak000_dir.vpk",0x7e35ed513a849e74,325077),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_swampland.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_swampland.bsp.pak000_dir.vpk",0x2ebe92b1bc942e95,221263),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_switchback.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_switchback.bsp.pak000_dir.vpk",0xf80fe164f26dfded,411126),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_training_ground.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_training_ground.bsp.pak000_dir.vpk",0x5c58dc4ffe362403,216217),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_wargames.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_wargames.bsp.pak000_dir.vpk",0xd156ef8cc0b0177a,384166),
("https://but.nobody-ca.me/r1/vpk/tchineseclient_mp_zone_18.bsp.pak000_dir.vpk","vpk/tchineseclient_mp_zone_18.bsp.pak000_dir.vpk",0x317467d95046f629,288128)
        };

        // Constants
        // --- Made internal const for use in App.xaml.cs ---
        internal const string ValidationFileRelativePath = @"vpk\client_mp_common.bsp.pak000_000.vpk";


        /// <summary>
        /// Ensures Titanfall files are present and CWD is set. Exits if setup fails/cancelled.
        /// (REMOVED - Logic moved to App.xaml.cs)
        /// </summary>
        // public static void EnsureTitanfallPresent(string originalLauncherDir) { ... }


        /// <summary>
        /// **NEW Internal Helper:** Attempts to find an *existing* valid Titanfall game directory
        /// using Registry and TitanfallFinder, without showing UI or exiting.
        /// </summary>
        /// <param name="originalLauncherDir">Needed for validation context.</param>
        /// <param name="fullValidate">If true, runs the full ValidateGamePath check. If false, only checks basic existence/finder result.</param>
        /// <returns>The validated absolute path if found automatically, otherwise null.</returns>
        internal static string TryFindExistingValidPath(string originalLauncherDir, bool fullValidate)
        {
            // a) Check Registry
            string registryPath = RegistryHelper.GetInstallPath();
            if (ValidateGamePath(registryPath, originalLauncherDir)) // Always validate registry path fully if found
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found valid path in registry: {registryPath}");
                return registryPath;
            }

            // b) Check TitanfallFinder
            string finderResultExe = TitanfallFinder.TitanfallLocator.FindTitanfallOrR1Delta();
            string finderPath = null;
            if (!string.IsNullOrEmpty(finderResultExe))
            {
                try
                {
                    finderPath = Path.GetDirectoryName(finderResultExe);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[TryFindExistingValidPath] Error getting directory from finder path '{finderResultExe}': {ex.Message}");
                    finderPath = null;
                }
            }

            // Only perform full validation if requested *and* finderPath is not null/empty
            bool finderPathIsValid = !string.IsNullOrEmpty(finderPath) && (!fullValidate || ValidateGamePath(finderPath, originalLauncherDir));

            if (finderPathIsValid)
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found potentially valid path via TitanfallFinder: {finderPath} (Full validation performed: {fullValidate})");
                // NOTE: We do NOT save to registry here. The calling function
                // (like App.xaml.cs) should handle that if appropriate.
                return finderPath;
            }

            // c) No valid path found automatically
            Debug.WriteLine("[TryFindExistingValidPath] No valid existing path found automatically.");
            return null;
        }


        /// <summary>
        /// **MODIFIED:** Attempts to find a valid Titanfall game directory.
        /// First tries automatic detection (Registry/Finder via TryFindExistingValidPath).
        /// If that fails, prompts the user via SetupWindow.
        /// Exits the application if setup is cancelled or fails.
        /// (REMOVED - Logic moved to App.xaml.cs)
        /// </summary>
        // private static string FindValidGamePathOrExit(string originalLauncherDir) { ... }


        /// <summary>
        /// **MODIFIED:** Checks if a given path points to a valid Titanfall installation directory.
        /// Made `internal static` to be callable from SetupWindow and App.xaml.cs.
        /// </summary>
        internal static bool ValidateGamePath(string path, string originalLauncherDir) // Changed to internal static
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                // Debug.WriteLine($"[ValidateGamePath] Validation failed: Path is null or whitespace."); // Optional: Can be noisy
                return false;
            }

            try
            {
                // Normalize path early to handle relative paths, etc.
                string fullPath = Path.GetFullPath(path);


                // Check if directory exists
                if (!Directory.Exists(fullPath))
                {
                    Debug.WriteLine($"[ValidateGamePath] Validation failed: Path does not exist: {fullPath}");
                    return false;
                }

                // Check for the presence of a critical file
                string validationFilePath = Path.Combine(fullPath, ValidationFileRelativePath);
                if (!File.Exists(validationFilePath))
                {
                    Debug.WriteLine($"[ValidateGamePath] Validation failed: VPK file does not exist: {validationFilePath}");
                    return false;
                }

                // If all checks pass
                Debug.WriteLine($"[ValidateGamePath] Validation successful for path: {fullPath}");
                return true;
            }
            catch (NotSupportedException nsex) // e.g., invalid chars in path derived from registry/finder
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' due to format/support issue: {nsex.Message}");
                return false;
            }
            catch (System.Security.SecurityException secEx)
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' due to security permissions: {secEx.Message}");
                return false;
            }
            catch (ArgumentException argEx) // Path contains invalid characters, is empty, or only whitespace
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' due to invalid argument: {argEx.Message}");
                return false;
            }
            catch (PathTooLongException ptle)
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' because it is too long: {ptle.Message}");
                return false;
            }
            catch (IOException ioEx) // Generic IO errors during checks
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' due to IO error: {ioEx.Message}");
                return false;
            }
            catch (Exception ex) // Catch-all for unexpected issues
            {
                Debug.WriteLine($"[ValidateGamePath] Validation failed for path '{path}' due to unexpected exception: {ex.GetType().Name} - {ex.Message}");
                return false;
            }
        }


        /// <summary>
        /// Creates placeholder VPK if needed. (Unchanged)
        /// </summary>
        private static void EnsurePlaceholderVpkExists(string installDir)
        {
            // ... (implementation unchanged)
            string placeholderPath = Path.Combine(installDir, ValidationFileRelativePath);
            if (!File.Exists(placeholderPath))
            {
                try
                {
                    string dir = Path.GetDirectoryName(placeholderPath);
                    if (!string.IsNullOrEmpty(dir))
                    {
                        Directory.CreateDirectory(dir);
                        using (File.Create(placeholderPath)) { /* Create empty file */ }
                        Debug.WriteLine($"Created placeholder file: {placeholderPath}");
                    }
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Warning: Could not create placeholder VPK file '{placeholderPath}': {ex.Message}");
                }
            }
        }


        /// <summary>
        /// Downloads files. (Unchanged)
        /// </summary>
        public static async Task<bool> DownloadAllFilesWithResume(
            string installDir,
            IInstallProgress progressUI,
            CancellationToken externalCts)
        {
            // ... (implementation unchanged)
            // --- 1. Validate parameters and prepare data structures ---
            if (progressUI == null) { Debug.WriteLine("Error: progressUI is null..."); return false; }
            if (string.IsNullOrWhiteSpace(installDir) || !Directory.Exists(installDir)) { progressUI.ShowError($"Internal Error: Invalid installation directory..."); return false; }
            var fileReceivedBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var fileTotalBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var filesToDownload = new List<(string Url, string DestPath, ulong XxHash, long FileSize)>();
            var progressHistory = new Queue<(double Time, long Progress)>();
            const double rollingInterval = 5.0;
            double lastUpdateTime = -1;
            object errorLock = new object();
            bool errorReported = false;
            Debug.WriteLine($"Starting file verification in: {installDir}");

            // --- 2. Check existing files, build download list ---
            try
            {
                foreach (var (url, relPath, expectedHash, knownSize) in s_fileList)
                {
                    externalCts.ThrowIfCancellationRequested();
                    if (string.IsNullOrWhiteSpace(relPath)) { Debug.WriteLine($"Warning: Invalid relative path..."); continue; }
                    string destPath = Path.Combine(installDir, relPath);
                    string directoryPath = Path.GetDirectoryName(destPath);
                    if (string.IsNullOrEmpty(directoryPath)) { progressUI.ShowError($"Internal Error: Could not determine directory..."); return false; }
                    Directory.CreateDirectory(directoryPath);
                    bool needsDownload = true;
                    if (File.Exists(destPath))
                    {
                        try
                        {
                            var existing = new FileInfo(destPath);
                            if (existing.Length == knownSize)
                            {
                                if (knownSize == 0) { needsDownload = false; }
                                else { ulong localXx = ComputeXxHash64(destPath); if (localXx == expectedHash) { needsDownload = false; } else { Debug.WriteLine($"Checksum mismatch: {relPath}...");  } }
                            }
                            else { Debug.WriteLine($"Size mismatch: {relPath}..."); }
                        }
                        catch (Exception ex) { Debug.WriteLine($"Warning: Could not verify {destPath} ({ex.GetType().Name}: {ex.Message})..."); }
                    }
                    fileTotalBytes[destPath] = knownSize;
                    fileReceivedBytes[destPath] = needsDownload ? 0 : knownSize;
                    if (needsDownload) { filesToDownload.Add((url, destPath, expectedHash, knownSize)); }
                }
            }
            catch (OperationCanceledException) { Debug.WriteLine("File verification cancelled."); progressUI.ShowError("Operation Cancelled"); return false; }
            catch (Exception ex) { Debug.WriteLine($"Fatal error during file verification phase: {ex}"); progressUI.ShowError($"Error during file check: {ex.Message}"); return false; }

            long totalBytesNeeded = fileTotalBytes.Values.Sum();
            long alreadyPresentBytes = fileReceivedBytes.Values.Sum();
            double initialPercent = totalBytesNeeded > 0 ? (alreadyPresentBytes * 100.0 / totalBytesNeeded) : 100.0;
            progressUI.ReportProgress(alreadyPresentBytes, totalBytesNeeded, 0.0); // Corrected line 1
            if (filesToDownload.Count == 0) { Debug.WriteLine("All files are already present..."); progressUI.ReportProgress(totalBytesNeeded, totalBytesNeeded, 0.0); EnsurePlaceholderVpkExists(installDir); return true; }
            Debug.WriteLine($"{filesToDownload.Count} file(s) need download...");

            // --- 3. Setup concurrency, progress tracking, cancellation ---
            object progressLock = new object();
            long overallProgress = alreadyPresentBytes;
            Stopwatch stopwatch = Stopwatch.StartNew();
            using var internalCts = CancellationTokenSource.CreateLinkedTokenSource(externalCts);
            CancellationToken token = internalCts.Token;
            const int MAX_CONCURRENT = 1;
            using var concurrency = new SemaphoreSlim(MAX_CONCURRENT, MAX_CONCURRENT);

            // --- 4. Build and run download tasks ---
            var downloadTasks = filesToDownload.Select(file => Task.Run(async () =>
            {
                await concurrency.WaitAsync(token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();
                using var downloader = new FastDownloadService(); // Assumed class
                try
                {
                    // --- MODIFIED OnProgress Action ---
                    void OnProgress(long currentFileBytesReceived, long currentFileTotalBytes) // Note: FastDownloadService reports per-file progress
                    {
                        if (token.IsCancellationRequested) return;

                        lock (progressLock)
                        {
                            // Update the specific file's progress
                            // Note: currentFileBytesReceived is the *total* for the current file being reported by FastDownloadService
                            long previousBytesForThisFile = fileReceivedBytes[file.DestPath];
                            long delta = currentFileBytesReceived - previousBytesForThisFile;

                            // Update overall progress
                            overallProgress += delta;
                            fileReceivedBytes[file.DestPath] = currentFileBytesReceived;

                            // Clamp overall progress just in case
                            overallProgress = Math.Max(alreadyPresentBytes, Math.Min(overallProgress, totalBytesNeeded));

                            // Calculate rolling speed (remains the same logic)
                            double currentTime = stopwatch.Elapsed.TotalSeconds;
                            progressHistory.Enqueue((currentTime, overallProgress));
                            while (progressHistory.Count > 1 && progressHistory.Peek().Time < currentTime - rollingInterval)
                            {
                                progressHistory.Dequeue();
                            }

                            // Only report UI updates periodically or at the end
                            if (currentTime - lastUpdateTime >= 1 || overallProgress == totalBytesNeeded)
                            {
                                double rollingSpeed = 0;
                                if (progressHistory.Count > 1)
                                {
                                    var oldest = progressHistory.Peek();
                                    double timeDelta = currentTime - oldest.Time;
                                    long progressDelta = overallProgress - oldest.Progress;
                                    if (timeDelta > 0.01) // Avoid division by zero/tiny numbers
                                    {
                                        rollingSpeed = progressDelta / timeDelta;
                                    }
                                }

                                // --- CALL THE NEW ReportProgress SIGNATURE ---
                                // Pass overall progress, total needed, and calculated rolling speed
                                progressUI.ReportProgress(overallProgress, totalBytesNeeded, rollingSpeed);
                                lastUpdateTime = currentTime;
                            }
                        }
                    }
                    // --- End of MODIFIED OnProgress Action ---

                    downloader.DownloadProgressChanged += OnProgress;
                    try
                    {
                        Debug.WriteLine($"Downloading: {Path.GetFileName(file.DestPath)}...");
                        // Pass the known total size for *this file* to FastDownloadService
                        // Note: FastDownloadService's progress event gives (bytesReceivedForThisFile, totalBytesForThisFile)
                        await downloader.DownloadFileAsync(file.Url, file.DestPath, token).ConfigureAwait(false);
                    }
                    finally
                    {
                        downloader.DownloadProgressChanged -= OnProgress;
                    }

                    // --- Final progress report for THIS file to ensure overallProgress is accurate ---
                    // This helps correct any rounding or timing issues if the last event wasn't processed
                    lock (progressLock)
                    {
                        long finalBytesForThisFile = file.FileSize; // Use the expected size
                        long previousBytesForThisFile = fileReceivedBytes[file.DestPath];
                        long delta = finalBytesForThisFile - previousBytesForThisFile;
                        overallProgress += delta;
                        fileReceivedBytes[file.DestPath] = finalBytesForThisFile;
                        overallProgress = Math.Max(alreadyPresentBytes, Math.Min(overallProgress, totalBytesNeeded));
                        // Optionally trigger one last UI update here if needed, but WhenAll completion usually handles the final 100%
                    }


                    token.ThrowIfCancellationRequested();
                    var fi = new FileInfo(file.DestPath);
                    if (fi.Length != file.FileSize)
                    {
                        throw new IOException($"Size mismatch after download for {Path.GetFileName(file.DestPath)}: Expected {file.FileSize}, Got {fi.Length}");
                    }
                    if (file.FileSize > 0)
                    {
                        ulong actualXx = ComputeXxHash64(file.DestPath);
                        if (actualXx != file.XxHash)
                        {
                            throw new IOException($"Checksum mismatch after download for {Path.GetFileName(file.DestPath)}");
                        }
                    }
                    Debug.WriteLine($"Verified: {Path.GetFileName(file.DestPath)}");
                }
                catch (OperationCanceledException)
                {
                    Debug.WriteLine($"Download cancelled for {Path.GetFileName(file.DestPath)}.");
                    // Don't delete here if resume is intended on next launch
                    // TryDeleteFile(file.DestPath);
                    throw; // Re-throw OCE
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error downloading/verifying {Path.GetFileName(file.DestPath)}: {ex}");
                    // Don't delete here if resume is intended on next launch
                    // TryDeleteFile(file.DestPath);
                    bool firstError;
                    lock (errorLock)
                    {
                        firstError = !errorReported;
                        if (firstError) errorReported = true;
                    }
                    if (firstError)
                    {
                        // Show specific error but allow other downloads to potentially continue/finish
                        progressUI.ShowError($"Download Error ({Path.GetFileName(file.DestPath)}): {ex.Message}");
                        // Don't cancel all others immediately unless that's the desired behavior
                        // internalCts.Cancel();
                    }
                    // Indicate failure for this task, WhenAll will catch it
                    throw; // Re-throw to mark task as faulted
                }
                finally
                {
                    concurrency.Release();
                }
            }, token)).ToList();

            // ... rest of the method ...

            // --- 5. Await all tasks and interpret result ---
            try
            {
                Debug.WriteLine($"Waiting for {downloadTasks.Count} download tasks...");
                await Task.WhenAll(downloadTasks).ConfigureAwait(false);

                // Final progress update to ensure 100% is shown if successful
                // Use the calculated totalBytesNeeded and the final overallProgress value
                // Check if cancellation happened *before* reporting final progress
                token.ThrowIfCancellationRequested(); // Check for cancellation first

                // Report final state (potentially 100% or last known progress if error)
                double finalSpeed = 0; // Speed is 0 when finished
                progressUI.ReportProgress(overallProgress, totalBytesNeeded, finalSpeed);


                // Now check for errors after attempting WhenAll
                if (errorReported) // Check our custom flag
                {
                    Debug.WriteLine("Download process completed with one or more errors.");
                    // Don't show a generic error if specific ones were already shown.
                    // progressUI.ShowError("One or more downloads failed. Check logs.");
                    return false; // Indicate failure
                }
                // If no errors and not cancelled, it's a success
                Debug.WriteLine("All downloads completed and verified successfully.");
                EnsurePlaceholderVpkExists(installDir);
                // progressUI.ReportProgress(100.0, 0); // Already called above
                return true;

            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Download operation was cancelled (caught OCE).");
                //if (!errorReported) // Only show generic cancel if no specific error was shown
                //{
                //    progressUI.ShowError("Download Cancelled");
                //}
                // Report last known progress before cancellation
                progressUI.ReportProgress(overallProgress, totalBytesNeeded, 0);
                return false;
            }
            catch (Exception ex) // Catch potential exceptions from Task.WhenAll itself (like AggregateException)
            {
                Debug.WriteLine($"An unexpected error occurred waiting for downloads: {ex}");
                if (!errorReported)
                {
                    progressUI.ShowError($"An unexpected error occurred: {ex.Message}");
                }
                // Report last known progress
                progressUI.ReportProgress(overallProgress, totalBytesNeeded, 0);
                return false;
            }
            finally
            {
                stopwatch.Stop();
                Debug.WriteLine($"Download process finished in {stopwatch.Elapsed.TotalSeconds:F1} seconds.");
            }
        }
        /// <summary> Formats byte count into KB/MB/GB string. </summary>
        private static string FormatBytes(long bytes)
        {
            if (bytes < 0) bytes = 0;
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            const double GB = MB * 1024.0;

            if (bytes < KB) return $"{bytes} B";
            if (bytes < MB) return $"{bytes / KB:F1} KB";
            if (bytes < GB) return $"{bytes / MB:F1} MB";
            return $"{bytes / GB:F1} GB";
        }

        // --- Helper Methods (Unchanged) ---

        /// <summary> Computes xxHash64 for a file. </summary>
        private static ulong ComputeXxHash64(string filePath)
        {
            // ... (implementation unchanged)
            const int bufferSize = 4 * 1024 * 1024;
            try { using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, bufferSize, FileOptions.SequentialScan); if (stream.Length == 0) { return 0xEF46DB3751D8E999; } var hasher = new XXH64(); byte[] buffer = new byte[bufferSize]; int bytesRead; while ((bytesRead = stream.Read(buffer, 0, buffer.Length)) > 0) { hasher.Update(buffer.AsSpan(0, bytesRead)); } return hasher.Digest(); }
            catch (Exception ex) { Debug.WriteLine($"Error computing xxHash64 for {filePath}: {ex.Message}"); return 0; }
        }

        /// <summary> Attempts to delete a file, ignoring errors. </summary>
        private static void TryDeleteFile(string filePath)
        {
            // ... (implementation unchanged)
            if (string.IsNullOrEmpty(filePath)) return; try { if (File.Exists(filePath)) { File.Delete(filePath); /* Debug.WriteLine($"Deleted file: {filePath}"); */ } } catch (Exception ex) { Debug.WriteLine($"Warning: Failed to delete file '{filePath}': {ex.Message}"); }
        }


        /// <summary> Generic Clamp extension method. </summary>
        public static T Clamp<T>(this T val, T min, T max) where T : IComparable<T>
        {
            // ... (implementation unchanged)
            if (val.CompareTo(min) < 0) return min; else if (val.CompareTo(max) > 0) return max; else return val;
        }
    }

    // Example IInstallProgress Interface (Unchanged)
    public interface IInstallProgress : IDisposable
    {
        // OLD: void ReportProgress(double percent, double bytesPerSecond);
        // NEW: Pass raw byte counts for more detailed reporting
        void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond);

        Action OnCancelRequested { get; set; }
        void ShowError(string message);
    }
}