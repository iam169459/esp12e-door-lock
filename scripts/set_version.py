Import("env")

import os
import re

version = os.environ.get("FIRMWARE_VERSION", "1.0.0").strip()
version = version.lstrip("vV")
if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    version = "1.0.0"

env.Append(CPPDEFINES=[("APP_VERSION", '\\"' + version + '\\"')])
