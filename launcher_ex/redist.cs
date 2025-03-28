using System;
using System.Diagnostics;
using System.IO;
using System.Net;


namespace launcher_ex
{
    public static class VisualCppInstaller
    {
        /// <summary>
        /// Ensures that Visual C++ 2010 and Visual C++ 2015/2017/2019 redistributables (x64) are installed.
        /// Checks for key DLLs in the System32 folder and installs the redistributable if not found.
        /// </summary>
        public static void EnsureVisualCppRedistributables()
        {
            bool needInstall2010 = false;
            bool needInstall2015 = false;

            // Get the system32 path (for 64-bit, this is where the DLLs should reside)
            string system32Path = Environment.ExpandEnvironmentVariables("%SystemRoot%\\System32");

            // Check for Visual C++ 2010 by verifying if msvcr100.dll exists
            string msvcr100Path = Path.Combine(system32Path, "msvcr100.dll");
            if (!File.Exists(msvcr100Path))
            {
                Console.WriteLine("Visual C++ 2010 redistributable not found.");
                needInstall2010 = true;
            }
            else
            {
                Console.WriteLine("Visual C++ 2010 redistributable appears to be installed.");
            }

            // Check for Visual C++ 2015/2017/2019 by verifying if vcruntime140.dll exists
            string vcruntime140Path = Path.Combine(system32Path, "vcruntime140.dll");
            if (!File.Exists(vcruntime140Path))
            {
                Console.WriteLine("Visual C++ 2015/2017/2019 redistributable not found.");
                needInstall2015 = true;
            }
            else
            {
                Console.WriteLine("Visual C++ 2015/2017/2019 redistributable appears to be installed.");
            }

            // If both redistributables are present, nothing more to do
            if (!needInstall2010 && !needInstall2015)
            {
                Console.WriteLine("All required Visual C++ redistributables are installed.");
                return;
            }

            // Define download URLs
            string url2010 = "http://download.microsoft.com/download/A/8/0/A80747C3-41BD-45DF-B505-E9710D2744E0/vcredist_x64.exe";
            string url2015 = "https://aka.ms/vs/16/release/vc_redist.x64.exe";

            // Download and install each missing redistributable
            if (needInstall2010)
            {
                Console.WriteLine("Installing Visual C++ 2010 redistributable...");
                DownloadAndInstall(url2010, "vcredist_2010_x64.exe");
            }

            if (needInstall2015)
            {
                Console.WriteLine("Installing Visual C++ 2015/2017/2019 redistributable...");
                DownloadAndInstall(url2015, "vcredist_2015_x64.exe");
            }
        }

        /// <summary>
        /// Downloads the installer from the specified URL and runs it with silent arguments.
        /// </summary>
        /// <param name="url">The URL to download the installer from.</param>
        /// <param name="fileName">The temporary file name to use for the downloaded installer.</param>
        private static void DownloadAndInstall(string url, string fileName)
        {
            // Download to a temporary file location
            string tempFilePath = Path.Combine(Path.GetTempPath(), fileName);

            try
            {
                using (WebClient client = new WebClient())
                {
                    Console.WriteLine($"Downloading installer from: {url}");
                    client.DownloadFile(url, tempFilePath);
                }

                // Prepare and start the installer process with silent parameters.
                Process process = new Process();
                process.StartInfo.FileName = tempFilePath;
                process.StartInfo.Arguments = "/quiet /norestart";
                process.StartInfo.UseShellExecute = false;
                process.Start();

                // Wait for the installation to complete
                process.WaitForExit();

                // Optionally check exit code (0 usually means success)
                if (process.ExitCode != 0)
                {
                    Console.WriteLine($"Installation of {fileName} failed with exit code {process.ExitCode}.");
                }
                else
                {
                    Console.WriteLine($"{fileName} installed successfully.");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error during installation of {fileName}: {ex.Message}");
            }
            finally
            {
                // Clean up the temporary file if it exists.
                if (File.Exists(tempFilePath))
                {
                    try
                    {
                        File.Delete(tempFilePath);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Failed to delete temporary file {tempFilePath}: {ex.Message}");
                    }
                }
            }
        }
    }
}
