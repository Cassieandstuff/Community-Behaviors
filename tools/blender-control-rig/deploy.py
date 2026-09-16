"""Deploy the CB Control Rig Importer to a local Blender addons folder.

Copies the `cb_control_rig/` package to `$BLENDER_PLUGINS_DIRECTORY/cb_control_rig/` (the
convention: one subfolder per plugin under the env var's path), replacing any prior install.
Pure stdlib, cross-platform, no build.

Usage:
    set BLENDER_PLUGINS_DIRECTORY=...\\Blender\\<ver>\\scripts\\addons   (once, in your env)
    python deploy.py                # deploy to $BLENDER_PLUGINS_DIRECTORY
    python deploy.py <target-dir>   # deploy to an explicit addons dir (overrides the env var)

Then in Blender: Preferences > Add-ons > enable "Community Behaviors — Control Rig Importer"
(or Edit > (re)load scripts if already enabled). __pycache__/*.pyc are never copied.
"""

import os
import shutil
import sys

PKG = "cb_control_rig"
_IGNORE = shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo")


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, PKG)
    if not os.path.isdir(src):
        print(f"ERROR: source package not found: {src}", file=sys.stderr)
        return 1

    target_root = argv[1] if len(argv) > 1 else os.environ.get("BLENDER_PLUGINS_DIRECTORY", "")
    if not target_root:
        print("ERROR: BLENDER_PLUGINS_DIRECTORY is not set (and no path argument given).\n"
              "       Set it to your Blender addons folder, e.g.\n"
              "         %APPDATA%\\Blender Foundation\\Blender\\4.2\\scripts\\addons\n"
              "       or pass the folder as an argument: python deploy.py <addons-dir>",
              file=sys.stderr)
        return 2
    if not os.path.isdir(target_root):
        print(f"ERROR: target folder does not exist: {target_root}", file=sys.stderr)
        return 3

    dst = os.path.join(target_root, PKG)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=_IGNORE)
    print(f"Deployed {PKG} -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
