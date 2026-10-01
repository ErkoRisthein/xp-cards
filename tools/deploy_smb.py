#!/usr/bin/env python3
"""Copy files to a Windows XP share over SMB1 (macOS's own SMB client no longer speaks it).

usage: deploy_smb.py [--host KONE] [--ip 192.168.8.198] [--share Downloads] [--dir FreeCellHD]
                     [--user guest] [--password ''] FILE...

Needs impacket (pip install impacket). Defaults come from the environment: XP_HOST, XP_IP,
XP_SHARE, XP_DIR, XP_USER, XP_PASSWORD.
"""
import argparse
import os
import sys

try:
    from impacket.smb import SMB_DIALECT
    from impacket.smbconnection import SMBConnection
except ImportError:
    sys.exit("deploy_smb.py needs impacket: pip install impacket")


def main():
    env = os.environ.get
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default=env("XP_HOST", "KONE"))
    ap.add_argument("--ip", default=env("XP_IP", ""))
    ap.add_argument("--share", default=env("XP_SHARE", "Downloads"))
    ap.add_argument("--dir", default=env("XP_DIR", "FreeCellHD"))
    ap.add_argument("--user", default=env("XP_USER", "guest"))
    ap.add_argument("--password", default=env("XP_PASSWORD", ""))
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()

    conn = SMBConnection(a.host, a.ip or a.host, sess_port=445, timeout=10, preferredDialect=SMB_DIALECT)
    conn.login(a.user, a.password)
    if a.dir:
        try:
            conn.createDirectory(a.share, a.dir)
        except Exception:
            pass  # already exists
    for f in a.files:
        remote = (a.dir + "\\" if a.dir else "") + os.path.basename(f)
        with open(f, "rb") as fh:
            conn.putFile(a.share, remote, fh.read)
        print(f"copied {f} -> \\\\{a.host}\\{a.share}\\{remote} ({os.path.getsize(f)} bytes)")
    conn.logoff()


if __name__ == "__main__":
    main()
