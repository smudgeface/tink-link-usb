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


def backup_config(host: str) -> dict | None:
    """Back up device config before filesystem flash."""
    try:
        resp = requests.get(f"http://{host}/api/config/backup", timeout=5)
        if resp.status_code == 200:
            backup = resp.json()
            if backup.get("config") or backup.get("wifi"):
                return backup
    except Exception:
        pass
    return None


def restore_config(host: str, backup: dict, retries: int = 5) -> bool:
    """Restore device config after filesystem flash."""
    import json
    for attempt in range(retries):
        try:
            resp = requests.post(
                f"http://{host}/api/config/restore",
                json=backup,
                timeout=10
            )
            if resp.status_code == 200:
                return True
        except Exception:
            pass
        time.sleep(3)
    return False


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

    # Back up config before filesystem flash
    config_backup = None
    if mode == 'fs':
        print("Backing up device config...", end=" ", flush=True)
        config_backup = backup_config(host)
        if config_backup:
            print(f"OK ({len(config_backup)} sections)")
        else:
            print("SKIP (no config found or backup not supported)")

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

                # Restore config after filesystem flash
                if config_backup:
                    print("Waiting for device to come back online...", flush=True)
                    time.sleep(8)
                    print("Restoring config...", end=" ", flush=True)
                    if restore_config(host, config_backup):
                        print("OK")
                        print("Rebooting to apply restored config...")
                        try:
                            requests.post(f"http://{host}/api/system/reboot", timeout=5)
                        except Exception:
                            pass  # Connection drops on reboot
                    else:
                        print("FAILED")
                        print("WARNING: Could not restore config. You may need to reconfigure manually.")
                else:
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


# Post-build action: Copy binaries to bin/ folder for easy access
def copy_binaries_post_build(source, target, env):
    """Copy firmware.bin to bin/ folder after successful build"""
    import shutil

    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    bin_dir = os.path.join(project_dir, "bin")

    # Create bin/ directory if it doesn't exist
    os.makedirs(bin_dir, exist_ok=True)

    # Copy firmware.bin
    firmware_src = os.path.join(build_dir, "firmware.bin")
    if os.path.exists(firmware_src):
        firmware_dst = os.path.join(bin_dir, "firmware.bin")
        shutil.copy2(firmware_src, firmware_dst)
        print(f"Copied firmware.bin to bin/")


def copy_filesystem_post_build(source, target, env):
    """Copy littlefs.bin to bin/ folder after filesystem build"""
    import shutil

    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    bin_dir = os.path.join(project_dir, "bin")

    # Create bin/ directory if it doesn't exist
    os.makedirs(bin_dir, exist_ok=True)

    # Copy littlefs.bin
    fs_src = os.path.join(build_dir, "littlefs.bin")
    if os.path.exists(fs_src):
        fs_dst = os.path.join(bin_dir, "littlefs.bin")
        shutil.copy2(fs_src, fs_dst)
        print(f"Copied littlefs.bin to bin/")


# Called by PlatformIO when script is loaded
Import = None  # Placeholder for PlatformIO's Import function
try:
    from SCons.Script import Import
    Import("env")

    # Register post-build actions to copy binaries to bin/ folder
    env.AddPostAction("$BUILD_DIR/firmware.bin", copy_binaries_post_build)
    env.AddPostAction("$BUILD_DIR/littlefs.bin", copy_filesystem_post_build)

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
