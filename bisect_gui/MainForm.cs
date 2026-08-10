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
                var builds = await Task.Run(() => FetchBuilds());
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
                Log("FAILED to fetch builds: " + ex.Message);
                _resultLabel.Text = "Could not reach GitHub: " + ex.Message;
            }
            _runButton.Enabled = _bisectButton.Enabled = _builds.Count > 0;
        }

        private static List<BuildInfo> FetchBuilds()
        {
            var list = new List<BuildInfo>();
            int page = 1;
            using (var wc = new WebClient())
            {
                wc.Headers[HttpRequestHeader.UserAgent] = "r1delta-bisect";
                while (true)
                {
                    string json = wc.DownloadString($"https://api.github.com/repos/{Repo}/releases?per_page=100&page={page}");
                    var arr = new JavaScriptSerializer().Deserialize<object[]>(json);
                    if (arr == null || arr.Length == 0)
                        break;
                    foreach (var item in arr)
                    {
                        var dict = (Dictionary<string, object>)item;
                        string tag = dict.TryGetValue("tag_name", out var t) ? t as string : null;
                        if (tag == null || !System.Text.RegularExpressions.Regex.IsMatch(tag, @"^v\d+\.\d+\.\d+$"))
                            continue;
                        var ver = new Version(tag.Substring(1));
                        if (ver < MinVersion)
                            continue;
                        var assets = dict.TryGetValue("assets", out var a) ? a as object[] : null;
                        if (assets == null)
                            continue;
                        foreach (var assetObj in assets)
                        {
                            var asset = (Dictionary<string, object>)assetObj;
                            string name = asset.TryGetValue("name", out var n) ? n as string : null;
                            if (name != null && name.EndsWith("-full.nupkg", StringComparison.OrdinalIgnoreCase))
                            {
                                string url = asset.TryGetValue("browser_download_url", out var u) ? u as string : null;
                                list.Add(new BuildInfo { Version = ver, Tag = tag, Name = name, Url = url });
                                break;
                            }
                        }
                    }
                    if (arr.Length < 100)
                        break;
                    page++;
                }
            }
            list.Sort((x, y) => x.Version.CompareTo(y.Version));
            return list;
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
                {
                    Log($"Downloading {info.Name} ...");
                    using (var wc = new WebClient())
                    {
                        wc.Headers[HttpRequestHeader.UserAgent] = "r1delta-bisect";
                        wc.DownloadFile(info.Url, nupkg);
                    }
                }
                Log($"Extracting {info.Name} ...");
                if (Directory.Exists(extract))
                    Directory.Delete(extract, true);
                ZipFile.ExtractToDirectory(nupkg, extract);
                Log($"Ready: {gameDir}");
            }
            return gameDir;
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

        private bool ProbeAlive(Process p, int seconds)
        {
            System.Threading.Thread.Sleep(5000);
            if (p.HasExited)
                return false;
            if (!_serverMode.Checked)
            {
                System.Threading.Thread.Sleep(3000);
                try
                {
                    var titans = Process.GetProcessesByName("Titanfall");
                    foreach (var t in titans)
                    {
                        if (!t.HasExited && (t.MainWindowTitle.IndexOf("crashed", StringComparison.OrdinalIgnoreCase) >= 0
                            || t.MainWindowTitle.IndexOf("unfortunately", StringComparison.OrdinalIgnoreCase) >= 0))
                            return false;
                    }
                }
                catch { }
            }
            System.Threading.Thread.Sleep(Math.Max(0, (seconds - 8) * 1000));
            return !p.HasExited;
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

                string gameDir = GetGameDir(mid);
                Process p = null;
                bool alive;
                try
                {
                    p = Launch(mid, gameDir);
                    alive = ProbeAlive(p, (int)_probeSeconds.Value);
                }
                finally
                {
                    if (p != null && !p.HasExited)
                    {
                        try { p.Kill(); } catch { }
                    }
                    System.Threading.Thread.Sleep(2000);
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
