#!/usr/bin/env python3
"""
NexusMatch Web Terminal Launcher
Starts the C++ matching engine bridge and serves the interactive trading terminal.
"""
import sys
import subprocess
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent

def main():
    server_script = ROOT_DIR / "web" / "server.py"
    if not server_script.exists():
        print(f"[Error] Web server script not found at {server_script}")
        sys.exit(1)

    print("==================================================================")
    print("      NEXUSMATCH HIGH-FREQUENCY MATCHING ENGINE WEB TERMINAL     ")
    print("==================================================================")
    print("  Engine:       C++20 In-Memory Deterministic Matching Core       ")
    print("  Frontend URL: http://localhost:8088                             ")
    print("  WebSocket:    ws://localhost:8088/ws                            ")
    print("==================================================================")
    print("Starting server... Press Ctrl+C to terminate.\n")

    try:
        subprocess.run([sys.executable, str(server_script)] + sys.argv[1:])
    except KeyboardInterrupt:
        print("\n[NexusMatch] Web terminal shutting down cleanly.")

if __name__ == "__main__":
    main()
