using System;
using System.ComponentModel;

namespace launcher_ex
{
    /// <summary>
    /// Turns a failure to start the game executable into a message that names the file and the
    /// actual cause, instead of a generic "error while loading DLLs".
    /// </summary>
    internal static class LaunchFailureMessage
    {
        private const int ErrorFileNotFound = 2;
        private const int ErrorPathNotFound = 3;
        private const int ErrorAccessDeniedByGroupPolicy = 1260; // ERROR_ACCESS_DISABLED_BY_POLICY
        private const int ErrorSystemIntegrityPolicyViolation = 4551; // ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION

        /// <summary>
        /// True for the Win32 errors Windows returns when Smart App Control, Windows Defender
        /// Application Control (WDAC), AppLocker or a group policy refuses to run a file.
        /// </summary>
        internal static bool IsApplicationControlBlock(int win32Error)
        {
            return win32Error == ErrorSystemIntegrityPolicyViolation ||
                   win32Error == ErrorAccessDeniedByGroupPolicy;
        }

        internal static string Describe(string filePath, int win32Error, string systemMessage)
        {
            string systemText = string.IsNullOrWhiteSpace(systemMessage) ? "(no message)" : systemMessage.Trim();

            if (IsApplicationControlBlock(win32Error))
            {
                return
                    "Windows blocked R1Delta from starting this file:\n" +
                    $"{filePath}\n\n" +
                    $"Windows reported (error {win32Error}): {systemText}\n\n" +
                    "The file is present. An application control policy refused to run it. That is Smart App Control, " +
                    "Windows Defender Application Control (WDAC), AppLocker, or a policy set by your organization, " +
                    "which allow or block programs by signature and reputation.\n\n" +
                    "To play, either allow that file in your policy, or on a personal PC turn Smart App Control off " +
                    "(Windows Security > App & browser control > Smart App Control settings; read Microsoft's note " +
                    "there first), then start R1Delta again.";
            }

            if (win32Error == ErrorFileNotFound || win32Error == ErrorPathNotFound)
            {
                return
                    "R1Delta could not start the game because this file is missing:\n" +
                    $"{filePath}\n\n" +
                    $"Windows reported (error {win32Error}): {systemText}\n\n" +
                    "Reinstall R1Delta (or run its setup again); Windows Defender or another antivirus may also have " +
                    "removed the file.";
            }

            return
                "R1Delta could not start the game executable:\n" +
                $"{filePath}\n\n" +
                $"Windows reported (error {win32Error}): {systemText}";
        }

        internal static string DescribeProcessStartFailure(string exePath, Exception exception)
        {
            var win32 = exception as Win32Exception;
            if (win32 != null)
                return Describe(exePath, win32.NativeErrorCode, win32.Message);
            return
                "R1Delta could not start the game executable:\n" +
                $"{exePath}\n\n" +
                $"{exception.GetType().Name}: {exception.Message}";
        }
    }
}
