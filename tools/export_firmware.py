Import("env")

import gzip
import os
import shutil


build_dir = env.subst("$BUILD_DIR")
program_name = env.subst("$PROGNAME")
app_bin = os.path.normpath(os.path.join(build_dir, f"{program_name}.bin"))
project_dir = env.subst("$PROJECT_DIR")
environment = env.subst("$PIOENV") or "unknown"
source_name = env.GetProjectOption("custom_source_name", "PortaSplit2MQTT")
version = env.GetProjectOption("custom_source_version", "0.0.0")
artifact_dir = os.path.normpath(os.path.join(project_dir, ".firmware"))


def export_firmware(source, target, env):
    del source, target, env
    os.makedirs(artifact_dir, exist_ok=True)
    base_name = f"{source_name}_{environment}_V{version}"
    serial_target = os.path.join(artifact_dir, f"{base_name}.bin")
    ota_target = os.path.join(artifact_dir, f"{base_name}.bin.ota.gz")
    legacy_ota_target = os.path.join(artifact_dir, f"{base_name}.bin.ota")
    shutil.copyfile(app_bin, serial_target)
    with open(app_bin, "rb") as source_file, open(ota_target, "wb") as target_file:
        with gzip.GzipFile(filename="", mode="wb", compresslevel=9,
                           fileobj=target_file, mtime=0) as gzip_file:
            shutil.copyfileobj(source_file, gzip_file)
    if os.path.isfile(legacy_ota_target):
        os.remove(legacy_ota_target)
    print(f"Serial firmware exported to: {serial_target}")
    print(f"GZIP OTA firmware exported to: {ota_target}")


env.AddPostAction(app_bin, export_firmware)
