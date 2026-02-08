"""
Pre-build script for ESP32-C3 environment.
Overrides the LittleFS data directory to use data_c3/ instead of data/.
"""
import os

Import("env")

project_dir = env.subst("$PROJECT_DIR")
data_dir = os.path.join(project_dir, "data_c3")
env.Replace(PROJECT_DATA_DIR=data_dir)
