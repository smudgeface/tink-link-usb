#!/usr/bin/env python3
"""
OTA Upload Script for TinkLink-USB

Uploads firmware or filesystem to the device over WiFi.
Can be run standalone or integrated with PlatformIO.

Usage:
    python scripts/ota_upload.py firmware .pio/build/esp32s3/firmware.bin
    python scripts/ota_upload.py filesystem .pio/build/esp32s3/littlefs.bin

Environment variables:
    TINKLINK_HOST - Device hostname/IP (default: tinklink.local)
"""

import sys
import os
import time
import argparse

try:
    import requests
except ImportError:
    print("Error: 'requests' module not found.")
    print("Install it with: pip install requests")
    sys.exit(1)


def upload_ota(host: str, filepath: str, mode: str, timeout: int = 120) -> bool:
    """
    Upload a binary file to the device via OTA.

    Args:
        host: Device hostname or IP address
        filepath: Path to the .bin file
        mode: 'firmware' or 'fs' (filesystem)
        timeout: Upload timeout in seconds

    Returns:
        True if successful, False otherwise
    """
    url = f"http://{host}/api/ota/upload"

    # Validate file exists
    if not os.path.exists(filepath):
        print(f"Error: File not found: {filepath}")
        return False

    filesize = os.path.getsize(filepath)
    filename = os.path.basename(filepath)

    print(f"{'='*50}")
    print(f"OTA Upload")
    print(f"{'='*50}")
    print(f"Host:     {host}")
    print(f"File:     {filename}")
    print(f"Size:     {filesize:,} bytes ({filesize/1024:.1f} KB)")
    print(f"Mode:     {mode}")
    print(f"{'='*50}")
    print()

    # Check device is reachable
    print("Checking device connectivity...", end=" ", flush=True)
    try:
        status_url = f"http://{host}/api/status"
        resp = requests.get(status_url, timeout=5)
        if resp.status_code != 200:
            print("FAILED")
            print(f"Error: Device returned status {resp.status_code}")
            return False
        print("OK")
    except requests.exceptions.RequestException as e:
        print("FAILED")
        print(f"Error: Cannot connect to {host}")
        print(f"       {e}")
        return False

    # Upload file
    print(f"Uploading {filename}...")

    try:
        with open(filepath, 'rb') as f:
            files = {'file': (filename, f, 'application/octet-stream')}
            data = {'mode': mode}

            start_time = time.time()

            # Use a session for connection reuse
            session = requests.Session()

            resp = session.post(
                url,
                files=files,
                data=data,
                timeout=timeout
            )

            elapsed = time.time() - start_time

            if resp.status_code == 200:
                print(f"\nUpload complete! ({elapsed:.1f}s)")
                print(f"Transfer rate: {filesize/elapsed/1024:.1f} KB/s")

                try:
                    result = resp.json()
                    print(f"Response: {result.get('message', 'OK')}")
                except:
                    pass

                print("\nDevice is rebooting...")
                print("Wait a few seconds, then reconnect.")
                return True
            else:
                print(f"\nUpload failed! Status: {resp.status_code}")
                try:
                    result = resp.json()
                    print(f"Error: {result.get('error', resp.text)}")
                except:
                    print(f"Response: {resp.text}")
                return False

    except requests.exceptions.Timeout:
        print("\nError: Upload timed out")
        print("The device may have rebooted during the update.")
        print("Check if it comes back online.")
        return False
    except requests.exceptions.RequestException as e:
        print(f"\nError during upload: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Upload firmware or filesystem to TinkLink-USB via OTA',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s firmware .pio/build/esp32s3/firmware.bin
  %(prog)s fs .pio/build/esp32s3/littlefs.bin
  %(prog)s firmware firmware.bin --host 192.168.1.100

Environment:
  TINKLINK_HOST   Device hostname/IP (default: tinklink.local)
        """
    )

    parser.add_argument(
        'mode',
        choices=['firmware', 'fs', 'filesystem'],
        help='Upload mode: firmware or fs/filesystem'
    )

    parser.add_argument(
        'file',
        help='Path to the .bin file to upload'
    )

    parser.add_argument(
        '--host',
        default=os.environ.get('TINKLINK_HOST', 'tinklink.local'),
        help='Device hostname or IP (default: tinklink.local or $TINKLINK_HOST)'
    )

    parser.add_argument(
        '--timeout',
        type=int,
        default=120,
        help='Upload timeout in seconds (default: 120)'
    )

    args = parser.parse_args()

    # Normalize mode
    mode = 'fs' if args.mode in ('fs', 'filesystem') else 'firmware'

    success = upload_ota(args.host, args.file, mode, args.timeout)
    sys.exit(0 if success else 1)


# PlatformIO integration
def ota_upload_firmware(source, target, env):
    """PlatformIO target: Upload firmware via OTA"""
    firmware_path = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    host = os.environ.get('TINKLINK_HOST', 'tinklink.local')

    print()
    success = upload_ota(host, firmware_path, 'firmware')
    if not success:
        env.Exit(1)


def ota_upload_filesystem(source, target, env):
    """PlatformIO target: Upload filesystem via OTA"""
    fs_path = os.path.join(env.subst("$BUILD_DIR"), "littlefs.bin")
    host = os.environ.get('TINKLINK_HOST', 'tinklink.local')

    print()
    success = upload_ota(host, fs_path, 'fs')
    if not success:
        env.Exit(1)


# Called by PlatformIO when script is loaded
Import = None  # Placeholder for PlatformIO's Import function
try:
    from SCons.Script import Import
    Import("env")

    # Register custom targets
    env.AddCustomTarget(
        name="ota",
        dependencies=["$BUILD_DIR/firmware.bin"],
        actions=[ota_upload_firmware],
        title="OTA Firmware",
        description="Build and upload firmware via OTA"
    )

    env.AddCustomTarget(
        name="otafs",
        dependencies=["$BUILD_DIR/littlefs.bin"],
        actions=[ota_upload_filesystem],
        title="OTA Filesystem",
        description="Build and upload filesystem via OTA"
    )

except (ImportError, NameError):
    # Running standalone, not in PlatformIO
    pass


if __name__ == '__main__':
    main()
