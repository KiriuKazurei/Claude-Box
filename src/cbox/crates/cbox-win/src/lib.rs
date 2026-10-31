//! Windows backend. C0 SKELETON — NOT IMPLEMENTED, NOT COMPILED ON WINDOWS YET.
//! There are no kernel namespaces on Windows: this is a Docker-like UX over
//! user accounts / restricted tokens / job objects / ACLs / WFP.
use cbox_core::Spec;

pub fn run(spec: &Spec) -> anyhow::Result<()> {
    // TODO C0-1 user:    ensure local user "cbox-<name>" (NetUserAdd, standard user)
    // TODO C0-2 token:   LogonUserW -> CreateRestrictedToken (drop admins/privs, keep Medium IL)
    //                    isolation="appcontainer": CreateAppContainerProfile + SECURITY_CAPABILITIES (no Computer use)
    // TODO C0-3 job:     CreateJobObjectW + KILL_ON_JOB_CLOSE, no BREAKAWAY_OK
    // TODO C0-4 fs:      profile/APPDATA/LOCALAPPDATA -> spec.root\profile; ACL grant image dir RX
    // TODO C0-5 proc:    CreateProcessAsUserW(entrypoint --proxy-server=gateway, CREATE_SUSPENDED)
    //                    -> AssignProcessToJobObject -> ResumeThread
    // TODO C0-6 desktop: grant user SID on WinSta0\Default (pending verification V2)
    // TODO C1  net:      FwpmFilterAdd0 by user SID: permit gateway, block rest
    // TODO C2  spoof:    inject hook DLL (separate crate, e.g. retour/minhook)
    let _ = spec;
    anyhow::bail!("cbox-win::run not implemented (C0 skeleton)")
}
