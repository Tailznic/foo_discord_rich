#!/usr/bin/env python3

import argparse
import zipfile
from pathlib import Path
from zipfile import ZipFile

import call_wrapper

def path_basename_tuple(path):
    return (path, path.name)

def zipdir(zip_file, path, arc_path):
    assert(path.exists() and path.is_dir())

    for file in path.rglob("*"):
        if (file.name.startswith(".")):
            # skip `hidden` files
            continue
        if (arc_path):
            file_arc_path = f"{arc_path}/{file.relative_to(path)}"
        else:
            file_arc_path = file.relative_to(path)
        zip_file.write(file, file_arc_path)

def pack(is_debug = False):
    cur_dir = Path(__file__).parent.absolute()
    root_dir = cur_dir.parent

    result_machine_dir = root_dir/"_result"/("Win32_Debug" if is_debug else "Win32_Release")
    result_machine_dir_x64 = root_dir/"_result"/("x64_Debug" if is_debug else "x64_Release")

    # read current version from VERSION file
    version_file = root_dir / "VERSION"
    if version_file.exists():
        current_version = version_file.read_text().strip()
    else:
        current_version = "unknown"

    # check if builds exist and match current version
    def check_build_version(build_dir):
        if not (build_dir.exists() and build_dir.is_dir()):
            return False
        version_meta = build_dir / "bin" / ".version"
        if not version_meta.exists():
            print(f"Warning: No version metadata found in {build_dir}/bin/. Build may be outdated.")
            return False
        built_version = version_meta.read_text().strip()
        if built_version != current_version:
            print(f"ERROR: Build version mismatch in {build_dir}")
            print(f"\tCurrent VERSION: {current_version}, while built version: {built_version}")
            print("Rebuild before attempting packaging..")
            return False
        return True

    pack_win32 = check_build_version(result_machine_dir)
    pack_x64 = check_build_version(result_machine_dir_x64)

    # At least one must be defined
    assert(pack_win32 or pack_x64)

    # use the validated current version
    version = current_version

    # multiarch when both are built, or individual if not
    if pack_win32 and pack_x64:
        output_dir = root_dir / "_result" / ("multiarch_debug" if is_debug else "multiarch_release")
        component_name = f"foo_discord_rich_{version}.fb2k-component"
    else:
        output_dir = result_machine_dir if pack_win32 else result_machine_dir_x64
        arch_suffix = "win32" if pack_win32 else "x64"
        component_name = f"foo_discord_rich_{arch_suffix}_{version}.fb2k-component"
    output_dir.mkdir(parents=True, exist_ok=True)

    component_zip = output_dir / component_name
    component_zip.unlink(missing_ok=True)

    with ZipFile(component_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        zipdir(z, root_dir/'licenses', 'licenses')

        z.write(*path_basename_tuple(root_dir/"LICENSE"))
        z.write(*path_basename_tuple(root_dir/"CHANGELOG.md"))

        if pack_win32:
            z.write(*path_basename_tuple(result_machine_dir/"bin"/"foo_discord_rich.dll"))

        if pack_x64:
            z.write(*path_basename_tuple(result_machine_dir_x64/"bin"/"foo_discord_rich.dll", 'x64'))

        if is_debug:
            # Only debug package should have pdbs inside

            if pack_win32:
                z.write(*path_basename_tuple(result_machine_dir/"dbginfo"/"foo_discord_rich.pdb"))

            if pack_x64:
                z.write(*path_basename_tuple(result_machine_dir_x64/"dbginfo"/"foo_discord_rich.pdb", 'x64'))

    print(f"Generated file: {component_zip}")

    if not is_debug:
        # Release pdbs are packed in a separate package
        if pack_win32 and pack_x64:
            pdb_name = f"foo_discord_rich_{version}_pdb.zip"
        else:
            arch_suffix = "win32" if pack_win32 else "x64"
            pdb_name = f"foo_discord_rich_{arch_suffix}_{version}_pdb.zip"
        pdb_zip = output_dir / pdb_name
        if pdb_zip.exists():
            pdb_zip.unlink()

        with ZipFile(pdb_zip, "w", zipfile.ZIP_DEFLATED) as z:
            if pack_win32:
                z.write(*path_basename_tuple(result_machine_dir/"dbginfo"/"foo_discord_rich.pdb"))

            if pack_x64:
                z.write(*path_basename_tuple(result_machine_dir_x64/"dbginfo"/"foo_discord_rich.pdb", 'x64'))

        print(f"Generated file: {pdb_zip}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Pack component to .fb2k-component')
    parser.add_argument('--debug', default=False, action='store_true')

    args = parser.parse_args()

    call_wrapper.final_call_decorator(
        "Packing component",
        "Packing component: success",
        "Packing component: failure!"
    )(
    pack
    )(
        args.debug
    )
