// ClaudeBox.Container Launcher — C0 skeleton. NOT IMPLEMENTED / NOT TESTED.
using System;

namespace ClaudeBox.Container
{
    public static class Launcher
    {
        public static int Main(string[] args)
        {
            // TODO C0-1: ensure local user "ClaudeBoxUser" exists (standard user, no admin).
            // TODO C0-2: LogonUser -> CreateRestrictedToken (drop Administrators, privileges; keep Medium IL).
            // TODO C0-3: CreateJobObject; SetInformationJobObject(KILL_ON_JOB_CLOSE, no BREAKAWAY_OK).
            // TODO C0-4: build env: USERPROFILE/APPDATA/LOCALAPPDATA -> <ContainerRoot>\profile; HTTPS_PROXY -> gateway.
            // TODO C0-5: CreateProcessAsUser(Claude.exe --proxy-server=..., CREATE_SUSPENDED) ; AssignProcessToJobObject ; ResumeThread.
            // TODO C0-6: grant ClaudeBoxUser access to WinSta0\Default (pending verification V2).
            // TODO C1: WFP filters by ClaudeBoxUser SID (allow 127.0.0.1:gateway, block rest).
            // TODO C2: inject hook DLL for env spoofing.
            Console.Error.WriteLine("ClaudeBox.Container launcher: not implemented (C0 skeleton).");
            return 1;
        }
    }
}
