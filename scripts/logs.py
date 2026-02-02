#!/usr/bin/env python3
"""
TinkLink-USB Log Monitor

Fetch and tail logs from a TinkLink-USB device via HTTP API.
Useful for debugging when USB CDC is disabled (USB OTG mode).

Usage:
    logs.py                     # Tail logs continuously
    logs.py --recent 50         # Show 50 most recent logs and exit
    logs.py --follow            # Tail logs continuously (default)
    logs.py --clear             # Clear log buffer on device
    logs.py --host 192.168.1.100  # Use specific IP instead of mDNS

Environment:
    TINKLINK_HOST   Device hostname/IP (default: tinklink.local)
"""

import argparse
import json
import os
import sys
import time
import urllib.request
import urllib.error

# Log level names
LOG_LEVELS = ['DEBUG', 'INFO', 'WARN', 'ERROR']

def get_host():
    """Get device hostname from environment or default."""
    return os.environ.get('TINKLINK_HOST', 'tinklink.local')

def check_connectivity(host, timeout=3):
    """Check if device is reachable."""
    url = f"http://{host}/api/status"
    try:
        req = urllib.request.Request(url, method='GET')
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status == 200
    except Exception:
        return False

def fetch_logs(host, since=0, count=50, timeout=5):
    """Fetch logs from device API."""
    url = f"http://{host}/api/logs?since={since}&count={count}"
    try:
        req = urllib.request.Request(url, method='GET')
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return json.loads(response.read().decode('utf-8'))
    except urllib.error.URLError as e:
        return None
    except json.JSONDecodeError:
        return None
    except Exception:
        return None

def clear_logs(host):
    """Clear log buffer on device."""
    url = f"http://{host}/api/logs?clear=1"
    try:
        req = urllib.request.Request(url, method='GET')
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status == 200
    except Exception:
        return False

def format_log(entry):
    """Format a single log entry for display."""
    ts = entry.get('ts', 0) / 1000.0  # Convert ms to seconds
    lvl_idx = entry.get('lvl', 1)
    lvl = LOG_LEVELS[lvl_idx] if lvl_idx < len(LOG_LEVELS) else '?'
    msg = entry.get('msg', '')

    # Color codes for different log levels
    colors = {
        'DEBUG': '\033[90m',  # Gray
        'INFO': '\033[0m',    # Default
        'WARN': '\033[33m',   # Yellow
        'ERROR': '\033[31m',  # Red
    }
    reset = '\033[0m'

    color = colors.get(lvl, '')
    return f"{color}[{ts:8.2f}s] [{lvl:5}] {msg}{reset}"

def print_logs(logs):
    """Print formatted log entries."""
    for entry in logs:
        print(format_log(entry))

def tail_logs(host, interval=1.0):
    """Continuously tail logs from device."""
    last_total = 0
    connected = True
    disconnect_time = None

    print(f"Tailing logs from {host} (Ctrl+C to stop)...")
    print("-" * 60)

    try:
        while True:
            # If we were disconnected, fetch from beginning to catch boot logs
            fetch_since = 0 if not connected else last_total
            data = fetch_logs(host, since=fetch_since, count=100, timeout=3)

            if data is None:
                if connected:
                    # Just lost connection
                    connected = False
                    disconnect_time = time.time()
                    print(f"\033[31m[Connection lost - waiting for device...]\033[0m")
                elif time.time() - disconnect_time > 10:
                    # Periodic reminder every 10 seconds
                    print(f"\033[31m[Still waiting for device...]\033[0m")
                    disconnect_time = time.time()
                time.sleep(interval)
                continue

            total = data.get('total', 0)
            logs = data.get('logs', [])

            # Handle reconnection after disconnect
            if not connected:
                print(f"\033[32m[Device reconnected]\033[0m")
                connected = True
            # Detect device reboot while connected: total count went backwards
            elif total < last_total:
                print(f"\033[33m[Device rebooted - fetching boot logs]\033[0m")
                # Fetch from beginning to get boot logs
                data = fetch_logs(host, since=0, count=100, timeout=3)
                if data:
                    logs = data.get('logs', [])
                    total = data.get('total', 0)

            if logs:
                print_logs(logs)

            last_total = total
            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n[Stopped]")

def show_recent(host, count):
    """Show recent logs and exit."""
    print(f"Fetching {count} recent logs from {host}...")
    print("-" * 60)

    data = fetch_logs(host, since=0, count=count)

    if data is None:
        print(f"Error: Could not connect to {host}", file=sys.stderr)
        return False

    logs = data.get('logs', [])
    total = data.get('total', 0)

    if logs:
        print_logs(logs)
        print("-" * 60)
        print(f"Showing {len(logs)} of {total} total log entries")
    else:
        print("No logs available")

    return True

def main():
    parser = argparse.ArgumentParser(
        description='Fetch and tail logs from TinkLink-USB device',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                      # Tail logs continuously
  %(prog)s -n 50                # Show 50 most recent logs
  %(prog)s -f                   # Tail logs (same as default)
  %(prog)s --clear              # Clear device log buffer
  %(prog)s --host 192.168.1.100 # Use specific IP

Environment:
  TINKLINK_HOST   Device hostname/IP (default: tinklink.local)
        """
    )

    parser.add_argument('-f', '--follow', action='store_true', default=True,
                        help='Tail logs continuously (default behavior)')
    parser.add_argument('-n', '--recent', type=int, metavar='COUNT',
                        help='Show COUNT recent logs and exit')
    parser.add_argument('--clear', action='store_true',
                        help='Clear log buffer on device')
    parser.add_argument('--host', type=str, default=None,
                        help='Device hostname or IP (default: tinklink.local or $TINKLINK_HOST)')
    parser.add_argument('-i', '--interval', type=float, default=1.0,
                        help='Polling interval in seconds for tail mode (default: 1.0)')

    args = parser.parse_args()

    # Determine host
    host = args.host or get_host()

    # Check connectivity
    sys.stdout.write(f"Connecting to {host}... ")
    sys.stdout.flush()

    if not check_connectivity(host):
        print("FAILED")
        print(f"Error: Could not connect to {host}", file=sys.stderr)
        print("Make sure the device is powered on and connected to WiFi.", file=sys.stderr)
        sys.exit(1)

    print("OK")

    # Handle clear request
    if args.clear:
        if clear_logs(host):
            print("Log buffer cleared")
        else:
            print("Failed to clear logs", file=sys.stderr)
            sys.exit(1)
        return

    # Handle recent logs request
    if args.recent:
        if not show_recent(host, args.recent):
            sys.exit(1)
        return

    # Default: tail logs
    tail_logs(host, interval=args.interval)

if __name__ == '__main__':
    main()
