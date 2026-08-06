Import("env")

import os


project_dir = env.subst("$PROJECT_DIR")
version_path = os.path.join(project_dir, "VERSION")
with open(version_path, "r", encoding="ascii") as version_file:
    version = version_file.read().strip()
if not version:
    raise RuntimeError("VERSION must not be empty")

env.Append(BUILD_FLAGS=[f'-DMIDEAFOLLOWME_VERSION=\\"{version}\\"'])
print(f"Firmware version: {version}")
