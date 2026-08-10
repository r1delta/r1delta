using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace R1DeltaBisect
{
    public sealed class BuildInfo
    {
        public Version Version;
        public string Tag;
        public string Name;
        public string Url;
        public override string ToString() { return Tag; }
    }

    public sealed class MainForm : Form
    {
        private const string Repo = "r1delta/r1delta";
        private static readonly Version MinVersion = new Version(2, 1, 2);

        private readonly List<BuildInfo> _builds = new List<BuildInfo>();
        private readonly string _cacheDir = Path.Combine(Path.GetTempPath(), "r1delta-bisect");

        private ComboBox _runVersion;
        private Button _runButton;
        private Button _openFolderButton;
        private ComboBox _goodVersion;
        private ComboBox _badVersion;
        private NumericUpDown _probeSeconds;
        private CheckBox _serverMode;
        private Button _bisectButton;
        private Label _resultLabel;
        private TextBox _log;
        private CancellationTokenSource _cts;

        public MainForm()
        {
            Text = "R1Delta Bisect";
            Width = 760;
            Height = 620;
            StartPosition = FormStartPosition.CenterScreen;
            Font = new System.Drawing.Font("Segoe UI", 9f);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(10), ColumnCount = 1 };
            Controls.Add(root);

            // --- Run a specific version ---
            var runBox = new GroupBox { Text = "Download and run a specific build", Dock = DockStyle.Fill, Height = 80 };
            var runTable = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 5, Padding = new Padding(8, 4, 8, 4) };
            runTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
            runTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            runTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 130));
            runTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
            runTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 40));
            runTable.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));

            runTable.Controls.Add(new Label { Text = "Version:", Anchor = AnchorStyles.Left }, 0, 0);
            _runVersion = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
            runTable.Controls.Add(_runVersion, 1, 0);
            _runButton = new Button { Text = "Run", Dock = DockStyle.Fill };
            _runButton.Click += (s, e) => RunSelectedVersion();
            runTable.Controls.Add(_runButton, 2, 0);
            _openFolderButton = new Button { Text = "Open Folder", Dock = DockStyle.Fill };
            _openFolderButton.Click += (s, e) => OpenSelectedFolder();
            runTable.Controls.Add(_openFolderButton, 3, 0);
            runTable.Controls.Add(new Label { Text = "", Dock = DockStyle.Fill }, 4, 0);
            runBox.Controls.Add(runTable);
            root.Controls.Add(runBox);

            // --- Bisection ---
            var bisectBox = new GroupBox { Text = "Bisect (find the first bad version)", Dock = DockStyle.Fill, Height = 130 };
            var bisectTable = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 8, Padding = new Padding(8, 4, 8, 4) };
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 55));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 50));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
            bisectTable.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
            bisectTable.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
            bisectTable.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));

            bisectTable.Controls.Add(new Label { Text = "Good:", Anchor = AnchorStyles.Left }, 0, 0);
            _goodVersion = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
            bisectTable.Controls.Add(_goodVersion, 1, 0);
            bisectTable.Controls.Add(new Label { Text = "Bad:", Anchor = AnchorStyles.Left }, 2, 0);
            _badVersion = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
            bisectTable.Controls.Add(_badVersion, 3, 0);
            bisectTable.Controls.Add(new Label { Text = "Probe (s):", Anchor = AnchorStyles.Left }, 4, 0);
            _probeSeconds = new NumericUpDown { Minimum = 20, Maximum = 600, Value = 60, Dock = DockStyle.Fill };
            bisectTable.Controls.Add(_probeSeconds, 5, 0);
            _serverMode = new CheckBox { Text = "Server", Anchor = AnchorStyles.Left };
            bisectTable.Controls.Add(_serverMode, 6, 0);
            _bisectButton = new Button { Text = "Start Bisect", Dock = DockStyle.Fill };
            _bisectButton.Click += (s, e) => ToggleBisect();
            bisectTable.Controls.Add(_bisectButton, 7, 0);

            _resultLabel = new Label { Text = "Ready.", Dock = DockStyle.Fill, ForeColor = System.Drawing.Color.DarkBlue, Anchor = AnchorStyles.Left };
            bisectTable.Controls.Add(_resultLabel, 0, 1);
            bisectTable.SetColumnSpan(_resultLabel, 8);
            bisectBox.Controls.Add(bisectTable);
            root.Controls.Add(bisectBox);

            // --- Log ---
            var logBox = new GroupBox { Text = "Log", Dock = DockStyle.Fill };
            _log = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, Font = new System.Drawing.Font("Consolas", 9f) };
            logBox.Controls.Add(_log);
            root.Controls.Add(logBox);

            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 85));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 130));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

            Shown += async (s, e) => await LoadBuildsAsync();
            FormClosing += (s, e) =>
            {
                if (_cts != null)
                {
                    _cts.Cancel();
                    if (!_bisectingDone)
                        e.Cancel = true;
                }
                RestoreProfileIfNeeded();
            };
        }

        private bool _bisectingDone = true;
        private bool _profileBackedUp;
        private string _profileBackupRoot;

        // ---------------------------------------------------------------- builds

        private async Task LoadBuildsAsync()
        {
            _runButton.Enabled = _bisectButton.Enabled = false;
            Log("Fetching released builds...");
            try
            {
                List<BuildInfo> builds = null;
                string cachePath = Path.Combine(_cacheDir, "builds.json");
                if (File.Exists(cachePath))
                {
                    try
                    {
                        var cached = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(cachePath));
                        DateTime stamp = DateTime.Parse((string)cached["fetched"]);
                        if (DateTime.UtcNow - stamp < TimeSpan.FromHours(24))
                            builds = ((object[])cached["builds"]).Select(b =>
                            {
                                var d = (Dictionary<string, object>)b;
                                return new BuildInfo
                                {
                                    Version = new Version((string)d["version"]),
                                    Tag = (string)d["tag"],
                                    Name = (string)d["name"],
                                    Url = (string)d["url"]
                                };
                            }).ToList();
                    }
                    catch { }
                }
                if (builds == null)
                {
                    builds = await Task.Run(() => FetchBuilds());
                    try
                    {
                        Directory.CreateDirectory(_cacheDir);
                        File.WriteAllText(cachePath, new JavaScriptSerializer().Serialize(new Dictionary<string, object>
                        {
                            { "fetched", DateTime.UtcNow.ToString("o") },
                            { "builds", builds.Select(b => new Dictionary<string, object>
                                {
                                    { "version", b.Version.ToString() },
                                    { "tag", b.Tag },
                                    { "name", b.Name },
                                    { "url", b.Url }
                                }).ToArray() }
                        }));
                    }
                    catch { }
                }
                _builds.Clear();
                _builds.AddRange(builds);
                _runVersion.Items.Clear();
                _runVersion.Items.AddRange(_builds.ToArray());
                if (_builds.Count > 0)
                    _runVersion.SelectedIndex = _builds.Count - 1;

                _goodVersion.Items.Clear();
                _goodVersion.Items.AddRange(_builds.ToArray());
                _badVersion.Items.Clear();
                _badVersion.Items.AddRange(_builds.ToArray());
                if (_builds.Count > 0)
                {
                    _goodVersion.SelectedIndex = 0;
                    _badVersion.SelectedIndex = _builds.Count - 1;
                }
                Log($"Found {_builds.Count} builds (v{MinVersion} .. {(_builds.Count > 0 ? _builds[_builds.Count - 1].Tag : "?")}).");
            }
            catch (Exception ex)
            {
                Log("FAILED to fetch builds: " + Describe(ex));
                _resultLabel.Text = "Could not fetch builds: " + Describe(ex);
            }
            _runButton.Enabled = _bisectButton.Enabled = _builds.Count > 0;
        }

        private static string Describe(Exception ex)
        {
            string detail = ex.Message;
            if (ex is WebException wex && wex.Response != null)
            {
                try
                {
                    using (var r = new StreamReader(wex.Response.GetResponseStream()))
                    {
                        string body = r.ReadToEnd();
                        if (!string.IsNullOrWhiteSpace(body))
                            detail += " | " + body;
                    }
                }
                catch { }
            }
            return detail;
        }

        private static List<BuildInfo> FetchBuilds()
        {
            // Prefer sources that are not subject to the GitHub API rate limit
            // (60 req/hr per IP). The asset URL for a full package is
            // deterministic once the tag is known.
            List<string> tags = FetchTagsViaGit();
            if (tags == null || tags.Count == 0)
                tags = FetchTagsViaHtml();
            if (tags == null || tags.Count == 0)
                tags = FetchTagsViaApi();

            var list = new List<BuildInfo>();
            foreach (string tag in tags.Distinct().OrderBy(t => t))
            {
                var ver = new Version(tag.Substring(1));
                if (ver < MinVersion)
                    continue;
                string name = "R1Delta-" + ver + "-full.nupkg";
                list.Add(new BuildInfo
                {
                    Version = ver,
                    Tag = tag,
                    Name = name,
                    Url = "https://github.com/" + Repo + "/releases/download/" + tag + "/" + name
                });
            }
            list.Sort((x, y) => x.Version.CompareTo(y.Version));
            return list;
        }

        private static List<string> FetchTagsViaGit()
        {
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = "git",
                    Arguments = "ls-remote --tags https://github.com/" + Repo + ".git",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true
                };
                using (var p = Process.Start(psi))
                {
                    string output = p.StandardOutput.ReadToEnd();
                    p.WaitForExit(30000);
                    var tags = new List<string>();
                    foreach (string line in output.Split('\n'))
                    {
                        int i = line.IndexOf("refs/tags/");
                        if (i < 0)
                            continue;
                        string tag = line.Substring(i + "refs/tags/".Length).Trim();
                        if (tag.EndsWith("^{}", StringComparison.Ordinal))
                            continue;
                        if (System.Text.RegularExpressions.Regex.IsMatch(tag, @"^v\d+\.\d+\.\d+$"))
                            tags.Add(tag);
                    }
                    if (tags.Count > 0)
                        return tags;
                }
            }
            catch { }
            return null;
        }

        private static List<string> FetchTagsViaHtml()
        {
            try
            {
                var tags = new List<string>();
                var re = new System.Text.RegularExpressions.Regex(
                    @"/r1delta/r1delta/releases/tag/(v\d+\.\d+\.\d+)");
                using (var wc = new WebClient())
                {
                    wc.Headers[HttpRequestHeader.UserAgent] = "curl/8.4.0";
                    for (int page = 1; page <= 6; page++)
                    {
                        string html = wc.DownloadString("https://github.com/" + Repo + "/releases?page=" + page);
                        bool any = false;
                        foreach (System.Text.RegularExpressions.Match m in re.Matches(html))
                        {
                            tags.Add(m.Groups[1].Value);
                            any = true;
                        }
                        if (!any)
                            break;
                    }
                }
                return tags.Count > 0 ? tags : null;
            }
            catch { }
            return null;
        }

        private static List<string> FetchTagsViaApi()
        {
            var tags = new List<string>();
            int page = 1;
            using (var wc = new WebClient())
            {
                wc.Headers[HttpRequestHeader.UserAgent] = "curl/8.4.0";
                while (true)
                {
                    string json = wc.DownloadString("https://api.github.com/repos/" + Repo + "/releases?per_page=100&page=" + page);
                    var arr = new JavaScriptSerializer().Deserialize<object[]>(json);
                    if (arr == null || arr.Length == 0)
                        break;
                    foreach (var item in arr)
                    {
                        var dict = (Dictionary<string, object>)item;
                        string tag = dict.TryGetValue("tag_name", out var t) ? t as string : null;
                        if (tag != null && System.Text.RegularExpressions.Regex.IsMatch(tag, @"^v\d+\.\d+\.\d+$"))
                            tags.Add(tag);
                    }
                    if (arr.Length < 100)
                        break;
                    page++;
                }
            }
            return tags;
        }

        // ------------------------------------------------------- download / extract

        private string GetGameDir(BuildInfo info)
        {
            string dest = Path.Combine(_cacheDir, info.Version.ToString());
            string nupkg = Path.Combine(dest, info.Name);
            string extract = Path.Combine(dest, "extract");
            string gameDir = Path.Combine(extract, "lib", "net462");
            if (!File.Exists(Path.Combine(gameDir, "Titanfall.exe")))
            {
                Directory.CreateDirectory(dest);
                if (!File.Exists(nupkg))
                    DownloadPackage(info, nupkg);
                Log($"Extracting {info.Name} ...");
                if (Directory.Exists(extract))
                    Directory.Delete(extract, true);
                try
                {
                    ZipFile.ExtractToDirectory(nupkg, extract);
                }
                catch (InvalidDataException)
                {
                    // Corrupt cached copy; drop it and try once more.
                    Log("Cached package is corrupt; re-downloading ...");
                    try { File.Delete(nupkg); } catch { }
                    DownloadPackage(info, nupkg);
                    if (Directory.Exists(extract))
                        Directory.Delete(extract, true);
                    ZipFile.ExtractToDirectory(nupkg, extract);
                }
                Log($"Ready: {gameDir}");
            }
            return gameDir;
        }

        private void DownloadPackage(BuildInfo info, string nupkg)
        {
            int attempts = 0;
            while (true)
            {
                attempts++;
                string part = nupkg + ".part";
                try
                {
                    using (var wc = new WebClient())
                    {
                        wc.Headers[HttpRequestHeader.UserAgent] = "curl/8.4.0";
                        wc.DownloadFile(info.Url, part);
                    }
                    if (!IsValidZip(part))
                        throw new InvalidDataException("downloaded file is not a valid ZIP (truncated download)");
                    if (File.Exists(nupkg))
                        File.Delete(nupkg);
                    File.Move(part, nupkg);
                    return;
                }
                catch (WebException wex)
                {
                    var resp = wex.Response as HttpWebResponse;
                    if (resp != null && resp.StatusCode == HttpStatusCode.NotFound)
                        throw new InvalidOperationException("No full package exists for " + info.Tag + " (" + info.Name + " not found).");
                    if (attempts >= 3)
                        throw;
                    Log("Download attempt " + attempts + " failed (" + wex.Message + "); retrying ...");
                    try { File.Delete(part); } catch { }
                }
                catch (InvalidDataException)
                {
                    if (attempts >= 3)
                        throw;
                    Log("Download attempt " + attempts + " produced an invalid ZIP; retrying ...");
                    try { File.Delete(part); } catch { }
                }
            }
        }

        private bool IsValidZip(string path)
        {
            try
            {
                using (var fs = File.OpenRead(path))
                {
                    if (fs.Length < 4)
                        return false;
                    byte[] magic = new byte[4];
                    fs.Read(magic, 0, 4);
                    return magic[0] == 0x50 && magic[1] == 0x4B
                        && (magic[2] == 0x03 || magic[2] == 0x05 || magic[2] == 0x07);
                }
            }
            catch
            {
                return false;
            }
        }

        private BuildInfo SelectedBuild(ComboBox box)
        {
            return box.SelectedItem as BuildInfo;
        }

        // --------------------------------------------------------------- profile

        private IEnumerable<string> GetProfilePaths()
        {
            string saved = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Saved Games", "Respawn", "R1Delta");
            string legacy = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Documents", "Respawn", "R1Delta");
            if (Directory.Exists(saved)) yield return saved;
            if (Directory.Exists(legacy)) yield return legacy;
        }

        private void BackupProfile()
        {
            if (_profileBackedUp)
                return;
            _profileBackupRoot = Path.Combine(_cacheDir, "profile-backup");
            Directory.CreateDirectory(_profileBackupRoot);
            int i = 0;
            foreach (string p in GetProfilePaths())
            {
                string target = Path.Combine(_profileBackupRoot, "R1Delta-" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + "-" + i++);
                Log("Backing up profile " + p + " -> " + target);
                CopyDirectory(p, target);
            }
            _profileBackedUp = true;
        }

        private void RestoreProfileIfNeeded()
        {
            if (!_profileBackedUp || _profileBackupRoot == null || !Directory.Exists(_profileBackupRoot))
                return;
            foreach (string dir in Directory.GetDirectories(_profileBackupRoot))
            {
                string live = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Saved Games", "Respawn", "R1Delta");
                if (Directory.Exists(live))
                {
                    Log("Restoring profile " + dir + " -> " + live);
                    CopyDirectory(dir, live);
                }
            }
            _profileBackedUp = false;
        }

        private static void CopyDirectory(string source, string target)
        {
            Directory.CreateDirectory(target);
            foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
            {
                string rel = file.Substring(source.Length).TrimStart('\\', '/');
                string dest = Path.Combine(target, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(dest));
                File.Copy(file, dest, true);
            }
        }

        // ---------------------------------------------------------------- launch

        private string[] LaunchArgs(string gameDir)
        {
            if (_serverMode.Checked)
                return new[] { "-console", "-dev", "-novid", "-port", "5555", "+hostport", "5555", "+map", "mp_airbase", "-game", gameDir };
            return new[] { "-novid", "-dev", "-windowed", "-noborder", "-game", gameDir };
        }

        private Process Launch(BuildInfo info, string gameDir)
        {
            string exe = Path.Combine(gameDir, _serverMode.Checked ? "R1Delta_DS.exe" : "Titanfall.exe");
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                WorkingDirectory = gameDir,
                UseShellExecute = false
            };
            var parts = new List<string>();
            foreach (string a in LaunchArgs(gameDir))
                parts.Add(a.Contains(" ") ? "\"" + a + "\"" : a);
            psi.Arguments = string.Join(" ", parts);
            Log("Launching " + info.Tag + ": " + exe + " " + psi.Arguments);
            return Process.Start(psi);
        }

        private bool ProbeAlive(Process p, int seconds, CancellationToken token)
        {
            if (_serverMode.Checked)
                return ProbeServerAlive(seconds, token);

            // The launched exe may be a stub that chains to the real game
            // process, so p.HasExited is not a liveness signal. Kill leftovers
            // from previous candidates first (only builds under our cache dir),
            // then wait for the engine window class "Respawn001".
            KillCacheGameProcesses();

            IntPtr hwnd = IntPtr.Zero;
            int waited = 0;
            while (waited < seconds * 2)
            {
                if (token.IsCancellationRequested)
                    return false;
                hwnd = FindWindow("Respawn001", null);
                if (hwnd != IntPtr.Zero)
                    break;
                System.Threading.Thread.Sleep(500);
                waited++;
            }
            if (hwnd == IntPtr.Zero)
            {
                Log("  no Respawn001 window within " + seconds + "s -> BAD (did not finish loading)");
                return false;
            }
            Log("  Respawn001 window appeared; waiting 15s for the engine to initialize ...");
            if (!SleepCancellable(15000, token))
                return false;

            Log("  sending playlist private_match ...");
            if (!SendCommand(hwnd, "playlist private_match"))
            {
                Log("  WM_COPYDATA not processed within 5s -> BAD (hung)");
                return false;
            }
            Log("  sending map mp_lobby ...");
            if (!SendCommand(hwnd, "map mp_lobby"))
            {
                Log("  WM_COPYDATA not processed within 5s -> BAD (hung)");
                return false;
            }
            Log("  commands accepted; waiting 15s for the map to settle ...");
            if (!SleepCancellable(15000, token))
                return false;

            Log("  sending error_test ...");
            if (!SendCommand(hwnd, "error_test"))
            {
                Log("  WM_COPYDATA not processed within 5s -> BAD (hung)");
                return false;
            }
            Log("  error_test accepted; waiting for the Engine Error dialog ...");
            waited = 0;
            while (waited < 30)
            {
                if (token.IsCancellationRequested)
                    return false;
                IntPtr dlg = FindEngineErrorDialog();
                if (dlg != IntPtr.Zero)
                {
                    Log("  Engine Error dialog appeared -> GOOD");
                    PostMessage(dlg, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
                    return true;
                }
                System.Threading.Thread.Sleep(500);
                waited++;
            }
            Log("  no Engine Error dialog within 15s -> BAD");
            return false;
        }

        private bool SleepCancellable(int millis, CancellationToken token)
        {
            int elapsed = 0;
            while (elapsed < millis)
            {
                if (token.IsCancellationRequested)
                    return false;
                System.Threading.Thread.Sleep(500);
                elapsed += 500;
            }
            return true;
        }

        private bool ProbeServerAlive(int seconds, CancellationToken token)
        {
            KillCacheGameProcesses();
            int waited = 0;
            while (waited < seconds)
            {
                if (token.IsCancellationRequested)
                    return false;
                if (CacheGameProcesses().Any())
                    break;
                System.Threading.Thread.Sleep(1000);
                waited++;
            }
            if (waited >= seconds)
            {
                Log("  no dedicated server process appeared within " + seconds + "s -> BAD");
                return false;
            }
            for (int i = 0; i < seconds; i++)
            {
                if (token.IsCancellationRequested)
                    return false;
                System.Threading.Thread.Sleep(1000);
            }
            return CacheGameProcesses().Any();
        }

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
        private const uint WM_CLOSE = 0x0010;

        private IntPtr FindEngineErrorDialog()
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate (IntPtr hwnd, IntPtr lParam)
            {
                var sb = new System.Text.StringBuilder(256);
                GetWindowText(hwnd, sb, sb.Capacity);
                if (sb.ToString().IndexOf("Engine Error", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    found = hwnd;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        private IEnumerable<Process> CacheGameProcesses()
        {
            var result = new List<Process>();
            foreach (var proc in Process.GetProcesses())
            {
                try
                {
                    if (!proc.HasExited
                        && proc.MainModule.FileName.StartsWith(_cacheDir, StringComparison.OrdinalIgnoreCase))
                        result.Add(proc);
                }
                catch { }
            }
            return result;
        }

        private void KillCacheGameProcesses()
        {
            foreach (var proc in CacheGameProcesses())
            {
                try { proc.Kill(); } catch { }
            }
            System.Threading.Thread.Sleep(1500);
        }

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        private struct COPYDATASTRUCT
        {
            public IntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr SendMessageTimeout(
            IntPtr hWnd, uint msg, IntPtr wParam, ref COPYDATASTRUCT lParam, uint flags, uint timeout, out IntPtr result);

        private const uint WM_COPYDATA = 0x004A;
        private const uint SMTO_ABORTIFHUNG = 0x0002;

        private bool SendCommand(IntPtr hwnd, string cmd)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(cmd);
            IntPtr mem = System.Runtime.InteropServices.Marshal.AllocHGlobal(bytes.Length + 1);
            System.Runtime.InteropServices.Marshal.Copy(bytes, 0, mem, bytes.Length);
            System.Runtime.InteropServices.Marshal.WriteByte(mem, bytes.Length, 0);
            var cds = new COPYDATASTRUCT
            {
                dwData = IntPtr.Zero,
                cbData = bytes.Length + 1,
                lpData = mem
            };
            try
            {
                IntPtr result;
                IntPtr r = SendMessageTimeout(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds, SMTO_ABORTIFHUNG, 5000, out result);
                return r != IntPtr.Zero;
            }
            finally
            {
                System.Runtime.InteropServices.Marshal.FreeHGlobal(mem);
            }
        }

        private void RunSelectedVersion()
        {
            var info = SelectedBuild(_runVersion);
            if (info == null)
                return;
            try
            {
                BackupProfile();
                string gameDir = GetGameDir(info);
                KillCacheGameProcesses();
                var p = Launch(info, gameDir);
                _resultLabel.Text = "Launched " + info.Tag + ". Profile will be restored on exit.";
            }
            catch (Exception ex)
            {
                Log("FAILED: " + ex.Message);
                RestoreProfileIfNeeded();
            }
        }

        private void OpenSelectedFolder()
        {
            var info = SelectedBuild(_runVersion);
            if (info == null)
                return;
            try
            {
                string gameDir = GetGameDir(info);
                Process.Start("explorer.exe", "\"" + gameDir + "\"");
            }
            catch (Exception ex)
            {
                Log("FAILED: " + ex.Message);
            }
        }

        // --------------------------------------------------------------- bisect

        private async void ToggleBisect()
        {
            if (_cts != null)
            {
                _cts.Cancel();
                return;
            }

            var good = SelectedBuild(_goodVersion);
            var bad = SelectedBuild(_badVersion);
            if (good == null || bad == null || good.Version >= bad.Version)
            {
                _resultLabel.Text = "Good must be older than Bad.";
                return;
            }

            _cts = new CancellationTokenSource();
            _bisectingDone = false;
            _bisectButton.Text = "Cancel";
            _runButton.Enabled = false;
            _resultLabel.Text = "Bisecting ...";
            try
            {
                await Task.Run(() => RunBisect(good, bad, _cts.Token));
                _resultLabel.Text = "Bisect finished. See log.";
            }
            catch (Exception ex)
            {
                Log("Bisect FAILED: " + ex.Message);
                _resultLabel.Text = "Bisect failed: " + ex.Message;
            }
            finally
            {
                _cts = null;
                _bisectingDone = true;
                _bisectButton.Text = "Start Bisect";
                _runButton.Enabled = _builds.Count > 0;
                RestoreProfileIfNeeded();
            }
        }

        private void RunBisect(BuildInfo good, BuildInfo bad, CancellationToken token)
        {
            BackupProfile();
            var lo = good.Version;
            var hi = bad.Version;
            string loTag = good.Tag, hiTag = bad.Tag;

            Log($"Bisecting [{loTag} .. {hiTag}]");
            while (!token.IsCancellationRequested)
            {
                var candidates = _builds.Where(b => b.Version > lo && b.Version < hi).ToList();
                if (candidates.Count == 0)
                    break;
                var mid = candidates[(candidates.Count - 1) / 2];

                string gameDir;
                try
                {
                    gameDir = GetGameDir(mid);
                }
                catch (Exception ex)
                {
                    Log("  " + mid.Tag + " unavailable (" + ex.Message + "); excluding it from the search");
                    _builds.RemoveAll(b => b.Tag == mid.Tag);
                    continue;
                }
                try
                {
                    var p = Launch(mid, gameDir);
                    bool alive = ProbeAlive(p, (int)_probeSeconds.Value, token);
                    if (token.IsCancellationRequested)
                    {
                        Log("Bisect cancelled.");
                        break;
                    }
                    if (alive)
                    {
                        lo = mid.Version;
                        loTag = mid.Tag;
                        Log("  -> new GOOD bound: " + loTag);
                    }
                    else
                    {
                        hi = mid.Version;
                        hiTag = mid.Tag;
                        Log("  -> new BAD bound: " + hiTag);
                    }
                }
                finally
                {
                    KillCacheGameProcesses();
                }
            }

            Log("");
            Log("RESULT: first bad version = " + hiTag + "  (last good = " + loTag + ")");
            _resultLabel.Text = "First bad version: " + hiTag;
        }

        // ------------------------------------------------------------------ log

        private void Log(string message)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(Log), message);
                return;
            }
            _log.AppendText("[" + DateTime.Now.ToString("HH:mm:ss") + "] " + message + Environment.NewLine);
            _log.SelectionStart = _log.TextLength;
            _log.ScrollToCaret();
        }
    }
}
