using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices; // For JunctionPoint if needed, assuming it exists
using System.Threading;
using System.Windows; // For MessageBox - consider abstracting UI interactions
// using System.Windows.Forms; // Not used directly here if MessageBox is System.Windows
using System.Threading.Tasks;
using Newtonsoft.Json; // If resume manifest is used later
// using Newtonsoft.Json.Serialization;

// Assuming K4os.Hash.xxHash and launcher_ex (for JunctionPoint) are available
using K4os.Hash.xxHash;
using launcher_ex; // Assuming JunctionPoint is here
using System.Net; // For HttpStatusCode if FastDownloadService throws specific exceptions
using System.Text;
using Monitor.Core.Utilities;
// Assuming Monitor.Core.Utilities exists if needed, not directly used here
// using Monitor.Core.Utilities;

// Assuming FastDownloadService class exists in the project/namespace
// and has been updated with internal cancellation checks as previously discussed.

namespace R1Delta
{
    public static class TitanfallManager
    {
        /// <summary>
        /// Example list of needed Titanfall files:
        ///   (URL, RelativePath, xxHash64, Size).
        /// Typically you’d embed or load this at runtime.
        /// </summary>
        private static readonly List<(string Url, string RelativePath, ulong XxHash, long Size)> s_fileList
            = new List<(string, string, ulong, long)>
        {
            // Example Entry - Populate with your actual list
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
("https://but.nobody-ca.me/r1/r1/media/ea.bik","r1/media/ea.bik",0x3f6f6a74ae1f3fb7,19262316),
("https://but.nobody-ca.me/r1/r1/media/intro.bik","r1/media/intro.bik",0xae5140c89fed0a11,175349816),
("https://but.nobody-ca.me/r1/r1/media/intro_captions.txt","r1/media/intro_captions.txt",0x7a70d5e102dafde3,387),
("https://but.nobody-ca.me/r1/r1/media/legal.bik","r1/media/legal.bik",0x0eb665df24f8a5d9,927840),
("https://but.nobody-ca.me/r1/r1/media/legal_captions.txt","r1/media/legal_captions.txt",0x692700211f3b1383,41),
("https://but.nobody-ca.me/r1/r1/media/menu_act01.bik","r1/media/menu_act01.bik",0xadd0925b541482c9,75002320),
("https://but.nobody-ca.me/r1/r1/media/respawn.bik","r1/media/respawn.bik",0x9c65a5fca0ac31b1,19746596),
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

        /// <summary>
        /// Filename used to persist partial download state across sessions.
        /// You can put this anywhere, e.g. in the “exeDir” or user’s AppData.
        /// </summary>
        private const string ResumeManifestFile = "DownloadResume.json"; // Currently unused but kept for context

        // ResumeManifest class commented out as it's not used in the current download logic
        /*
        private class ResumeManifest
        {
            public Dictionary<string, DownloadPackage> Packages { get; set; }
                = new Dictionary<string, DownloadPackage>(StringComparer.OrdinalIgnoreCase);
        }
        */

        /// <summary>
        /// Main entry point that ensures Titanfall is present.
        /// </summary>
        public static void EnsureTitanfallPresent()
        {
            // 1) Abort if audio_installer.exe is running
            var audioInstallerProcs = Process.GetProcessesByName("audio_installer");
            if (audioInstallerProcs.Any(p => !p.HasExited))
            {
                System.Windows.MessageBox.Show(
                    "audio_installer.exe is running. Please close it before proceeding.",
                    "Cannot Continue",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error
                );
                Environment.Exit(0);
            }

            // 2) Set current working directory to the .exe folder
            string exeDir = Path.GetDirectoryName(Environment.GetCommandLineArgs()[0])
                            ?? Directory.GetCurrentDirectory();
            Directory.SetCurrentDirectory(exeDir);

            // 3) Clean up any invalid junctions
            RemoveInvalidJunction("bin");
            RemoveInvalidJunction("r1");
            RemoveInvalidJunction("vpk");

            // 4) If we already have the critical Titanfall VPK locally, we can skip everything
            //    NOTE: Use a more reliable indicator than just one VPK if possible.
            //    Maybe check for a few core files or a specific marker file.
            string localVpkPath = Path.Combine(exeDir, "vpk", "client_mp_common.bsp.pak000_000.vpk");
            if (File.Exists(localVpkPath))
            {
                Console.WriteLine("Critical VPK found locally. Assuming Titanfall is present.");
                // Done—let the rest of your logic run
                return;
            }

            // 5) Try to find Titanfall folder from external logic
            //    This will return null if not found
            //    Assuming TitanfallFinder.TitanfallLocator exists and works
            string foundInstall = TitanfallFinder.TitanfallLocator.FindTitanfallOrR1Delta();
            if (!string.IsNullOrEmpty(foundInstall))
            {
                // Check if the found installation seems valid (e.g., contains the VPK)
                string foundVpk = Path.Combine(foundInstall, "vpk", "client_mp_common.bsp.pak000_000.vpk");
                if (File.Exists(foundVpk))
                {
                    Console.WriteLine($"Found existing Titanfall installation at: {foundInstall}");
                    CreateJunctionIfNeeded("bin", Path.Combine(foundInstall, "bin"));
                    CreateJunctionIfNeeded("r1", Path.Combine(foundInstall, "r1"));
                    CreateJunctionIfNeeded("vpk", Path.Combine(foundInstall, "vpk"));
                    Console.WriteLine("Junctions created to existing installation.");
                    return;
                }
                else
                {
                    Console.WriteLine($"Found potential installation at {foundInstall}, but critical VPK missing. Proceeding to download prompt.");
                }
            }
            else
            {
                Console.WriteLine("No existing Titanfall installation found. Proceeding to download prompt.");
            }


            // 6) Use a mutex to prevent multiple r1delta.exe instances running the setup
            bool createdNew;
            using (var singleInstanceMutex = new Mutex(true, "R1Delta_DownloadSetup_Mutex", out createdNew)) // More specific mutex name
            {
                if (!createdNew)
                {
                    System.Windows.MessageBox.Show(
                        "Another instance of R1Delta setup/download is already running.",
                        "Cannot Continue",
                        MessageBoxButton.OK,
                        MessageBoxImage.Exclamation
                    );
                    Environment.Exit(1); // Use non-zero exit code for error
                }

                // If mutex is acquired, proceed with setup UI
                Console.WriteLine("Starting setup window...");
                // Assuming SetupWindow exists and implements IInstallProgress correctly
                var setupWindow = new SetupWindow();
                bool? dialogResult = setupWindow.ShowDialog(); // ShowDialog blocks

                if (dialogResult != true)
                {
                    Console.WriteLine("Setup window was cancelled or closed by the user.");
                    Environment.Exit(0); // Clean exit if user cancels
                }

                // If we get here, the user completed the download successfully via the UI
                string selectedDir = setupWindow.SelectedPath;
                Console.WriteLine($"Download completed successfully to: {selectedDir}");

                // Now create the necessary structure/junctions in the *current* directory
                // pointing to the downloaded files in selectedDir.

                // Optional: Create an empty critical VPK file if the server doesn't provide it
                string finalVpkPath = Path.Combine(selectedDir, "vpk", "client_mp_common.bsp.pak000_000.vpk");
                if (!File.Exists(finalVpkPath)) // Only if not downloaded
                {
                    Console.WriteLine("Creating placeholder empty VPK file.");
                    Directory.CreateDirectory(Path.GetDirectoryName(finalVpkPath)!);
                    using (File.Create(finalVpkPath)) { /* empty file */ }
                }


                // Create the junctions in the CWD pointing to the installation dir
                CreateJunctionIfNeeded("bin", Path.Combine(selectedDir, "bin"));
                CreateJunctionIfNeeded("r1", Path.Combine(selectedDir, "r1"));
                CreateJunctionIfNeeded("vpk", Path.Combine(selectedDir, "vpk"));
                Console.WriteLine("Junctions created to new installation.");

                // Mutex is released when the 'using' block ends
            }
        }
        public static T Clamp<T>(this T val, T min, T max) where T : IComparable<T>
        {
            if (val.CompareTo(min) < 0) return min;
            else if (val.CompareTo(max) > 0) return max;
            else return val;
        }

        /// <summary>
        /// Download all needed Titanfall files to <paramref name="installDir"/>,
        /// checking existing files and handling cancellation/errors robustly.
        /// </summary>
        /// <param name="installDir">The target directory for downloaded files.</param>
        /// <param name="progressUI">The UI element implementing progress reporting and cancellation.</param>
        /// <param name="externalCts">The cancellation token source controlled externally (e.g., by the UI cancel button).</param>
        /// <returns>True if all files were downloaded and verified successfully, false otherwise.</returns>
        public static async Task<bool> DownloadAllFilesWithResume(
            string installDir,
            IInstallProgress progressUI,
            CancellationToken externalCts) // Renamed for clarity
        {
            var fileReceivedBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var fileTotalBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var filesToDownload = new List<(string Url, string DestPath, ulong XxHash, long FileSize)>();

            // --- State for coordinating first error reporting ---
            object errorLock = new object();
            bool errorHasOccurred = false;
            // ---

            // --- FIX: Connect UI Cancellation Request ---
            // Ensure progressUI is not null before assigning
            if (progressUI == null)
            {
                Console.WriteLine("Error: Progress UI object is null. Cannot proceed with download.");
                // Decide how to handle this - throw, return false, etc.
                return false; // Or throw new ArgumentNullException(nameof(progressUI));
            }
            // --- End Fix ---

            Console.WriteLine("Checking existing files...");

            // 1. Identify files needing download and calculate total size
            try
            {
                foreach (var (url, relPath, expectedHash, knownSize) in s_fileList)
                {
                    // Basic validation
                    if (knownSize <= 0)
                    {
                        Console.WriteLine($"Warning: File '{relPath}' has size {knownSize} listed. Skipping hash/size check, will assume download needed if missing.");
                        // Decide if zero-size files are valid or errors
                    }
                    if (string.IsNullOrWhiteSpace(relPath))
                    {
                        Console.WriteLine($"Warning: Invalid relative path found in file list for URL '{url}'. Skipping.");
                        continue;
                    }

                    string destPath = Path.Combine(installDir, relPath);
                    string? directoryPath = Path.GetDirectoryName(destPath);

                    if (string.IsNullOrEmpty(directoryPath))
                    {
                        progressUI.ShowError($"Internal Error: Invalid destination path generated for relative path: {relPath}");
                        return false;
                    }
                    // Ensure directory exists before checking/writing file
                    Directory.CreateDirectory(directoryPath);

                    bool needsDownload = true;
                    if (File.Exists(destPath))
                    {
                        try
                        {
                            FileInfo fileInfo = new FileInfo(destPath);
                            // Only verify hash if size matches and is positive
                            if (fileInfo.Length == knownSize && knownSize > 0)
                            {
                                Console.WriteLine($"Verifying checksum for existing file: {relPath}...");
                                ulong localXx = ComputeXxHash64(destPath);
                                if (localXx == expectedHash)
                                {
                                    Console.WriteLine($"File is correct: {relPath}");
                                    needsDownload = false;
                                }
                                else
                                {
                                    Console.WriteLine($"Checksum mismatch for {relPath}. Expected 0x{expectedHash:X}, Got 0x{localXx:X}. Will re-download.");
                                    TryDeleteFile(destPath); // Delete corrupt file
                                }
                            }
                            // Handle zero-byte files: if expected size is 0 and file exists with size 0, it's correct.
                            else if (fileInfo.Length == 0 && knownSize == 0)
                            {
                                Console.WriteLine($"Zero-byte file exists and is expected: {relPath}");
                                needsDownload = false;
                            }
                            else
                            {
                                Console.WriteLine($"Size mismatch for {relPath}. Expected {knownSize}, Got {fileInfo.Length}. Will re-download.");
                                TryDeleteFile(destPath); // Delete incorrect size file
                            }
                        }
                        catch (IOException ex)
                        {
                            Console.WriteLine($"Warning: Error accessing {destPath} for verification: {ex.Message}. Will attempt re-download.");
                            // Don't necessarily fail the whole process here, just mark for download
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"Warning: Unexpected error verifying {destPath}: {ex.Message}. Will attempt re-download.");
                        }
                    }

                    // Track total size and received bytes (initialize based on check results)
                    fileTotalBytes[destPath] = knownSize; // Track expected size
                    if (needsDownload)
                    {
                        filesToDownload.Add((url, destPath, expectedHash, knownSize));
                        fileReceivedBytes[destPath] = 0; // Needs download, start count at 0
                    }
                    else
                    {
                        fileReceivedBytes[destPath] = knownSize; // Already present and correct
                    }
                }
            }
            catch (Exception ex)
            {
                // Catch errors during the file checking phase (e.g., directory creation permissions)
                progressUI.ShowError($"Fatal Error during file checking phase: {ex.Message}");
                Console.WriteLine($"Fatal Error during file checking: {ex.ToString()}");
                return false;
            }

            long totalSizeToProcess = fileTotalBytes.Values.Sum();
            long alreadyDownloadedBytes = fileReceivedBytes.Values.Sum();

            // Initial progress report based on existing files
            double initialPercent = totalSizeToProcess > 0 ? (alreadyDownloadedBytes / (double)totalSizeToProcess) * 100.0 : 100.0;
            progressUI.ReportProgress(initialPercent, 0);


            // If no files actually need downloading
            if (filesToDownload.Count == 0)
            {
                Console.WriteLine("All files are already present and correct.");
                progressUI.ReportProgress(100.0, 0); // Ensure final report
                return true;
            }

            Console.WriteLine($"{filesToDownload.Count} file(s) need downloading. Total size: {totalSizeToProcess:N0} bytes. Already present: {alreadyDownloadedBytes:N0} bytes.");

            // 2. Setup for Parallel Downloads and Progress Reporting
            object progressLock = new object();
            long overallProgressBytes = alreadyDownloadedBytes; // Start count from already present bytes
            Stopwatch stopwatch = Stopwatch.StartNew();
            double lastReportedSpeed = 0;
            const double reportingIntervalSeconds = 2; // Report speed more frequently

            const int MaxParallelDownloads = 8; // Keep concurrency limit reasonable
            using var concurrencySemaphore = new SemaphoreSlim(MaxParallelDownloads, MaxParallelDownloads);

            // --- Use a dedicated internal CTS linked to the external one ---
            // This allows us to cancel internally on error without affecting the external source directly,
            // while still respecting external cancellation requests.
            using var internalCts = CancellationTokenSource.CreateLinkedTokenSource(externalCts);
            var combinedToken = internalCts.Token; // Use this token for all async operations

            // --- FIX: Create FastDownloadService ONCE ---
            using var downloader = new FastDownloadService(); // Reuse this instance

            // 3. Create and Start Download Tasks
            var downloadTasks = filesToDownload.Select(file => Task.Run(async () =>
            {
                // Respect semaphore and cancellation token *before* starting work
                await concurrencySemaphore.WaitAsync(combinedToken);
                combinedToken.ThrowIfCancellationRequested(); // Check before proceeding

                try
                {
                    Console.WriteLine($"Starting download task for: {Path.GetFileName(file.DestPath)}"); // Log task start

                    // Define the progress handler locally to capture 'file' safely
                    Action<long, long> progressHandler = (currentFileBytesReceived, totalFileLength) =>
                    {
                        // --- FIX: Check cancellation at start of handler ---
                        if (combinedToken.IsCancellationRequested) return;

                        lock (progressLock)
                        {
                            // Calculate the *change* in bytes for this specific file since the last report
                            long previousBytesForThisFile = fileReceivedBytes.ContainsKey(file.DestPath) ? fileReceivedBytes[file.DestPath] : 0;
                            long bytesDeltaForThisFile = currentFileBytesReceived - previousBytesForThisFile;

                            // Update the specific file's progress
                            fileReceivedBytes[file.DestPath] = currentFileBytesReceived;

                            // Update the overall progress by the delta
                            overallProgressBytes += bytesDeltaForThisFile;

                            // Ensure overall progress doesn't exceed total size due to rounding or concurrency nuances
                            overallProgressBytes = Math.Min(overallProgressBytes, totalSizeToProcess);

                            double elapsedSeconds = stopwatch.Elapsed.TotalSeconds;
                            double currentOverallSpeed = lastReportedSpeed; // Use last known speed until interval passes

                            // Calculate and update speed periodically
                            if (elapsedSeconds >= reportingIntervalSeconds)
                            {
                                // Calculate speed based on overall progress change since last report
                                // This requires tracking the overall progress at the *last report time*
                                // Simpler approach: Calculate instantaneous speed for the interval
                                long totalBytesNow = fileReceivedBytes.Values.Sum(); // Potentially expensive? No, dictionary sum is fast.
                                // We need overall bytes *at the start of the interval*
                                // Let's reset stopwatch and track bytes transferred *during* the interval

                                // Alternative: Keep it simple - average speed over the interval
                                // Recalculate total bytes received *now*
                                long currentTotalReceived = fileReceivedBytes.Values.Sum();
                                // We need the total bytes at the *last* report interval...
                                // Let's stick to the previous simpler speed calculation for now:
                                // It calculates average speed since the *last speed calculation*, not overall average.

                                // Re-implementing the speed calc based on overall progress change:
                                // Need 'overallPreviousBytesAtLastReport' variable outside the handler? Complex.
                                // Let's use the simpler approach based on total download amount:
                                double percent = totalSizeToProcess > 0 ? (overallProgressBytes / (double)totalSizeToProcess) * 100.0 : (file.FileSize > 0 ? 100.0 : 0);
                                percent = Clamp(percent, 0.0, 100.0); // Use Clamp

                                // Speed calculation needs refinement - let's use total elapsed time for average
                                double totalElapsed = stopwatch.Elapsed.TotalSeconds;
                                if (totalElapsed > 1) // Avoid division by zero/tiny numbers early on
                                {
                                    // Speed based on bytes downloaded *since start*, divided by *total time*
                                    currentOverallSpeed = (overallProgressBytes - alreadyDownloadedBytes) / totalElapsed;
                                    currentOverallSpeed = Math.Max(0, currentOverallSpeed); // Ensure non-negative
                                    lastReportedSpeed = currentOverallSpeed; // Update last reported speed
                                }

                                // Report progress
                                progressUI.ReportProgress(percent, currentOverallSpeed);

                                // Reset stopwatch ONLY if we base speed on intervals (which we aren't right now)
                                // stopwatch.Restart();
                            }
                            else
                            {
                                // Report progress update without recalculating speed if interval hasn't passed
                                double percent = totalSizeToProcess > 0 ? (overallProgressBytes / (double)totalSizeToProcess) * 100.0 : (file.FileSize > 0 ? 100.0 : 0);
                                percent = Clamp(percent, 0.0, 100.0);
                                progressUI.ReportProgress(percent, lastReportedSpeed); // Report with last known speed
                            }
                        }
                    };

                    // --- Download ---
                    try
                    {
                        // Subscribe progress handler *before* starting download
                        downloader.DownloadProgressChanged += progressHandler;
                        Console.WriteLine($"Executing DownloadFileAsync for: {Path.GetFileName(file.DestPath)}");
                        await downloader.DownloadFileAsync(file.Url, file.DestPath, combinedToken);
                    }
                    finally
                    {
                        // Unsubscribe progress handler *after* download finishes or fails
                        downloader.DownloadProgressChanged -= progressHandler;
                        Console.WriteLine($"DownloadFileAsync finished for: {Path.GetFileName(file.DestPath)}");
                    }

                    // --- Verification ---
                    // Check cancellation *after* download completes but *before* verification
                    combinedToken.ThrowIfCancellationRequested();

                    Console.WriteLine($"Download complete, verifying: {Path.GetFileName(file.DestPath)}");
                    FileInfo downloadedFileInfo = new FileInfo(file.DestPath);

                    if (downloadedFileInfo.Length != file.FileSize)
                    {
                        throw new IOException($"Verification Failed: Size mismatch for {Path.GetFileName(file.DestPath)}. Expected: {file.FileSize}, Got: {downloadedFileInfo.Length}");
                    }

                    // Only hash if the file has content and hash is expected
                    if (file.FileSize > 0)
                    {
                        ulong finalXx = ComputeXxHash64(file.DestPath);
                        if (finalXx != file.XxHash)
                        {
                            throw new IOException($"Verification Failed: Checksum mismatch for {Path.GetFileName(file.DestPath)}. Expected: 0x{file.XxHash:X}, Got: 0x{finalXx:X}");
                        }
                    }

                    Console.WriteLine($"Verification successful: {Path.GetFileName(file.DestPath)}");

                    // Final update for this file's progress (ensures it's marked as 100% complete)
                    lock (progressLock)
                    {
                        long previousBytes = fileReceivedBytes.ContainsKey(file.DestPath) ? fileReceivedBytes[file.DestPath] : 0;
                        overallProgressBytes += (file.FileSize - previousBytes); // Add remaining delta
                        fileReceivedBytes[file.DestPath] = file.FileSize; // Mark as complete
                        overallProgressBytes = Math.Min(overallProgressBytes, totalSizeToProcess); // Clamp

                        // Report final progress after successful verification
                        double percent = totalSizeToProcess > 0 ? (overallProgressBytes / (double)totalSizeToProcess) * 100.0 : 100.0;
                        progressUI.ReportProgress(Clamp(percent, 0.0, 100.0), lastReportedSpeed);
                    }
                }
                catch (OperationCanceledException)
                {
                    Console.WriteLine($"Download cancelled for {Path.GetFileName(file.DestPath)}.");
                    // Don't show UI error here, let the WhenAll handler manage cancellation messages.
                    TryDeleteFile(file.DestPath); // Clean up partial file on cancellation
                    throw; // Re-throw OCE
                }
                catch (Exception ex) // Catches download errors AND verification errors
                {
                    bool isFirstError = false;
                    lock (errorLock) // --- CRITICAL SECTION for Error Reporting ---
                    {
                        if (!errorHasOccurred)
                        {
                            errorHasOccurred = true;
                            isFirstError = true;
                        }
                    } // --- END CRITICAL SECTION ---

                    string errorMsg = $"Error processing {Path.GetFileName(file.DestPath)}: {ex.Message}";
                    Console.WriteLine(errorMsg); // Log all errors

                    if (isFirstError)
                    {
                        Console.WriteLine($"First error detected, reporting to UI and requesting cancellation...");
                        progressUI.ShowError(errorMsg); // Show only the first error in the UI

                        // *** CRITICAL: Signal cancellation to other tasks via internal CTS ***
                        try
                        {
                            internalCts.Cancel();
                        }
                        catch (ObjectDisposedException) { /* Ignore if CTS disposed */ }
                        catch (AggregateException aggEx) when (aggEx.InnerExceptions.All(e => e is ObjectDisposedException)) { /* Ignore */ }
                    }
                    else
                    {
                        Console.WriteLine($"Subsequent error (UI report suppressed): {errorMsg}");
                    }

                    TryDeleteFile(file.DestPath); // Clean up potentially corrupt file on error
                    throw; // Re-throw the original exception so Task.WhenAll sees the failure.
                }
                finally
                {
                    concurrencySemaphore.Release();
                    Console.WriteLine($"Semaphore released for task: {Path.GetFileName(file.DestPath)}");
                }
            }, combinedToken)).ToList(); // Pass combinedToken to Task.Run as well

            // 4. Wait for all tasks and handle results
            try
            {
                Console.WriteLine("Waiting for all download tasks to complete...");
                await Task.WhenAll(downloadTasks);
                Console.WriteLine("All download tasks finished or were cancelled/faulted.");

                // Check cancellation state *after* WhenAll returns
                if (combinedToken.IsCancellationRequested)
                {
                    if (externalCts.IsCancellationRequested) // Cancelled by user/external source
                    {
                        Console.WriteLine("Download process cancelled by external request.");
                        // Avoid showing error if already cancelled. UI should reflect cancellation.
                        // progressUI.ShowError("Download cancelled."); // Maybe not needed if UI handles it
                    }
                    else // Cancelled internally due to an error
                    {
                        Console.WriteLine("Download process stopped due to an error in one or more files.");
                        // The first error message should have already been shown via progressUI.ShowError.
                    }
                    return false; // Indicate failure/cancellation
                }
                else
                {
                    // If WhenAll completed without throwing and no cancellation was requested, success!
                    Console.WriteLine("All downloads completed and verified successfully.");
                    progressUI.ReportProgress(100.0, 0); // Final 100% report
                    return true; // Indicate success
                }
            }
            catch (OperationCanceledException)
            {
                // This catches OCE if WhenAll itself is cancelled *before* all tasks finish
                // (e.g., external cancellation happens while WhenAll is awaited).
                // It also catches OCE re-thrown by individual tasks if that's the first exception seen by WhenAll.
                Console.WriteLine("Download operation was cancelled (caught OCE after WhenAll).");
                if (!errorHasOccurred) // Only show generic cancel message if no specific error was reported
                {
                    // progressUI.ShowError("Download cancelled."); // UI should handle this based on token state
                }
                return false; // Indicate failure/cancellation
            }
            catch (Exception ex)
            {
                // This catches the first non-OCE exception re-thrown by a task, if WhenAll aggregates it.
                Console.WriteLine($"An unexpected error occurred during download aggregation: {ex.Message}");
                // The first specific error should have already been logged/shown by the failing task's catch block.
                // Avoid showing a generic error if a specific one was already shown by the 'isFirstError' logic.
                if (!errorHasOccurred)
                {
                    // This case is less likely now but handles unexpected aggregation errors
                    progressUI.ShowError($"Download error: {ex.Message}");
                }
                return false; // Indicate failure
            }
            finally
            {
                stopwatch.Stop();
                Console.WriteLine("Download process finished.");
            }
        }

        // --------------------------------------------------------------------
        // Helper Methods (Keep as they were, assuming they work correctly)
        // --------------------------------------------------------------------

        private static void RemoveInvalidJunction(string folderName)
        {
            string path = Path.Combine(Directory.GetCurrentDirectory(), folderName);
            if (!Directory.Exists(path)) return; // No directory/junction exists

            try
            {
                // Check if it's a reparse point (junction/symlink)
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) == FileAttributes.ReparsePoint)
                {
                    // Try accessing it to see if it's valid. GetDirectories is a common way.
                    // Using Directory.Exists already implicitly checks some level of validity,
                    // but GetDirectories might throw on broken links where Exists might not.
                    try
                    {
                        Directory.GetDirectories(path); // Attempt access
                    }
                    catch (Exception ex) // Catches various exceptions for broken links
                    {
                        Console.WriteLine($"Detected invalid junction '{folderName}'. Removing. Error: {ex.Message}");
                        try
                        {
                            // Use the JunctionPoint class's delete method if it exists and is safer
                            if (JunctionPoint.Exists(path)) // Assuming Exists checks if it's a junction
                            {
                                JunctionPoint.Delete(path);
                            }
                            else // Fallback if not using JunctionPoint class or it fails
                            {
                                Directory.Delete(path, false); // Try deleting the link itself
                            }
                            Console.WriteLine($"Removed invalid junction: {path}");
                        }
                        catch (Exception delEx)
                        {
                            Console.WriteLine($"Failed to remove invalid junction '{path}': {delEx.Message}");
                        }
                    }
                }
            }
            catch (FileNotFoundException) { /* Link target doesn't exist, might be ok or invalid */ }
            catch (DirectoryNotFoundException) { /* Link target doesn't exist */ }
            catch (IOException ioex) { Console.WriteLine($"IO Error checking junction '{path}': {ioex.Message}"); }
            catch (Exception ex) { Console.WriteLine($"Error checking junction '{path}': {ex.Message}"); }
        }

        private static void CreateJunctionIfNeeded(string junctionPointRelative, string targetDir)
        {
            string junctionPath = Path.Combine(Directory.GetCurrentDirectory(), junctionPointRelative);

            // Ensure target exists and is a directory
            if (!Directory.Exists(targetDir))
            {
                Console.WriteLine($"Cannot create junction '{junctionPointRelative}'. Target directory '{targetDir}' does not exist.");
                return;
            }

            // Check if junction path already exists
            if (Directory.Exists(junctionPath) || File.Exists(junctionPath))
            {
                // Check if it's already the correct junction
                if ((File.GetAttributes(junctionPath) & FileAttributes.ReparsePoint) == FileAttributes.ReparsePoint)
                {
                    try
                    {
                        // Assuming JunctionPoint.GetTarget exists
                        string currentTarget = JunctionPoint.GetTarget(junctionPath);
                        if (string.Equals(Path.GetFullPath(currentTarget).TrimEnd('\\'), Path.GetFullPath(targetDir).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase))
                        {
                            // Console.WriteLine($"Junction '{junctionPointRelative}' already exists and points correctly.");
                            return; // Already correct
                        }
                        else
                        {
                            Console.WriteLine($"Junction '{junctionPointRelative}' exists but points to '{currentTarget}'. Removing and recreating.");
                            JunctionPoint.Delete(junctionPath);
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Error checking existing junction '{junctionPointRelative}'. Attempting to delete and recreate. Error: {ex.Message}");
                        try { JunctionPoint.Delete(junctionPath); } catch { /* Ignore delete error if it fails */ }
                    }
                }
                else
                {
                    Console.WriteLine($"Path '{junctionPointRelative}' exists but is not a junction. Please remove it manually.");
                    // Handle this case - maybe throw an error or exit?
                    // For robustness, we could try deleting it if it's empty? Risky.
                    return; // Don't proceed
                }
            }

            // Create the junction
            try
            {
                Console.WriteLine($"Creating junction: '{junctionPath}' -> '{targetDir}'");
                JunctionPoint.Create(junctionPath, targetDir, false); // false = don't overwrite (already handled above)
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to create junction '{junctionPath}': {ex.Message}");
                // Consider showing an error to the user
            }
        }

        private static ulong ComputeXxHash64(string filePath)
        {
            const int bufferSize = 64 * 1024; // 64KB buffer
            try
            {
                using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, bufferSize, FileOptions.SequentialScan);
                // Check if file is empty before hashing
                if (stream.Length == 0)
                {
                    return 0xEF46DB3751D8E999; // Standard xxHash64 value for empty input
                }

                var hasher = new XXH64();
                byte[] buffer = new byte[bufferSize];
                int bytesRead;
                while ((bytesRead = stream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    hasher.Update(buffer.AsSpan(0, bytesRead));
                }
                // Use Digest() which returns the ulong directly if available, otherwise convert bytes
                // Assuming K4os library might have DigestValue or similar? If not:
                byte[] hashBytes = hasher.DigestBytes();
                // Ensure correct conversion on different endianness systems if necessary,
                // but BitConverter usually handles this based on system architecture.
                return BitConverter.ToUInt64(hashBytes, 0);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error computing xxHash64 for {filePath}: {ex.Message}");
                // Decide how to handle hash errors - return 0, throw?
                return 0; // Return 0 or another indicator of failure
            }
        }

        private static void TryDeleteFile(string filePath)
        {
            if (string.IsNullOrEmpty(filePath)) return;
            try
            {
                if (File.Exists(filePath))
                {
                    File.Delete(filePath);
                    Console.WriteLine($"Deleted potentially partial/corrupt file: {filePath}");
                }
            }
            catch (Exception ex)
            {
                // Log or handle the inability to delete if necessary
                Console.WriteLine($"Warning: Failed to delete file '{filePath}': {ex.Message}");
            }
        }
    }

    // =====================================================================
    // Example interface for your progress window:
    // =====================================================================
    public interface IInstallProgress : IDisposable // Make it disposable if the UI window needs cleanup
    {
        /// <summary>
        /// Reports overall download progress and current speed.
        /// </summary>
        /// <param name="percent">Overall progress percentage (0.0 to 100.0).</param>
        /// <param name="bytesPerSecond">Current estimated download speed in bytes/sec.</param>
        void ReportProgress(double percent, double bytesPerSecond);

        /// <summary>
        /// Action to be invoked when the user requests cancellation (e.g., clicks Cancel button).
        /// The implementation of the UI *must* call Cancel() on its CancellationTokenSource within this action.
        /// </summary>
        Action OnCancelRequested { get; set; }

        /// <summary>
        /// Shows an error message in the UI. Should handle being called multiple times
        /// but ideally only display the first critical error.
        /// </summary>
        /// <param name="message">The error message to display.</param>
        void ShowError(string message);

        // Consider adding methods like:
        // void SetStatusMessage(string message); // For "Verifying...", "Downloading...", etc.
        // void Complete(); // To indicate successful completion
    }
}