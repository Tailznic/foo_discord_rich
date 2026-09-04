#!/usr/bin/env python3

import shutil
import subprocess
from pathlib import Path

import call_wrapper

def reset_submodule( submodule_dir: Path ):
    '''Best effort reset, so that the patching below stays idempotent across setup reruns'''
    if ( not ( submodule_dir/".git" ).exists() ):
        return

    try:
        subprocess.check_call( "git reset --hard", cwd=submodule_dir, shell=True )
        subprocess.check_call( "git clean -fd", cwd=submodule_dir, shell=True )
    except subprocess.CalledProcessError:
        # The patching below is marker-guarded and idempotent anyway
        pass

def patch_file( path: Path, patches ):
    '''Applies marker-guarded text patches to the given file (idempotent)'''
    content = path.read_text()
    for ( marker, old, new ) in patches:
        if ( marker in content ):
            print( f"Skipping patch (already applied): {marker}" )
            continue

        assert old in content, f"Anchor for patch '{marker}' was not found in {path}"
        content = content.replace( old, new, 1 )

    path.write_text( content, newline="\n" )

def patch_discord_rpc( discord_dir: Path ):
    '''Teaches the ancient discord-rpc about the Discord ActivityType field
    (so that the component can send "Listening" presence instead of "Playing"),
    and bumps the minimum CMake version for modern CMake generators'''

    patch_file(
        discord_dir/"include"/"discord_rpc.h",
        [
            (
                "DRP_PATCH_DISCORD_ACTIVITY_TYPE",
                """typedef struct DiscordRichPresence {
    const char* state;   /* max 128 bytes */""",
                """typedef enum DiscordActivityType {
    DiscordActivityType_Playing = 0,
    DiscordActivityType_Streaming = 1,
    DiscordActivityType_Listening = 2,
    DiscordActivityType_Watching = 3,
    DiscordActivityType_Competing = 5
} DiscordActivityType;

typedef struct DiscordRichPresence {
    DiscordActivityType activityType; /* DRP_PATCH_DISCORD_ACTIVITY_TYPE */
    const char* state;   /* max 128 bytes */""",
            ),
        ]
    )

    patch_file(
        discord_dir/"src"/"serialization.cpp",
        [
            (
                "DRP_PATCH_DISCORD_ACTIVITY_TYPE",
                """            if (presence != nullptr) {
                WriteObject activity(writer, "activity");

                WriteOptionalString(writer, "state", presence->state);""",
                """            if (presence != nullptr) {
                WriteObject activity(writer, "activity");

                WriteKey(writer, "type");
                writer.Int(presence->activityType);

                WriteOptionalString(writer, "state", presence->state);""",
            ),
        ]
    )

    patch_file(
        discord_dir/"CMakeLists.txt",
        [
            (
                "DRP_PATCH_CMAKE_MIN_VERSION",
                "cmake_minimum_required (VERSION 3.2.0)",
                "cmake_minimum_required (VERSION 3.5.0) # DRP_PATCH_CMAKE_MIN_VERSION",
            ),
        ]
    )

    # Modern clang-format (18+) fails on duplicated YAML mapping keys, and discord-rpc's
    # build runs `clang-format` through a custom CMake target. Remove the duplicate key.
    patch_file(
        discord_dir/".clang-format",
        [
            (
                "DRP_PATCH_DUPLICATE_INDENTCASELABELS_REMOVED",
                "DisableFormat: false\nFixNamespaceComments: true\nForEachMacros: []\nIndentCaseLabels: false\nIncludeCategories:",
                "DisableFormat: false\nFixNamespaceComments: true\nForEachMacros: []\n# DRP_PATCH_DUPLICATE_INDENTCASELABELS_REMOVED\nIncludeCategories:",
            ),
        ]
    )

def configure():
    cur_dir = Path(__file__).parent.absolute()
    root_dir = cur_dir.parent
    discord_dir = root_dir/"submodules"/"discord-rpc"
    assert(discord_dir.exists() and discord_dir.is_dir())

    reset_submodule( discord_dir )

    shutil.copy2(cur_dir/"additional_files"/"discord-rpc.vcxproj", str(discord_dir/"src") + '/')

    patch_discord_rpc( discord_dir )

if __name__ == '__main__':
    call_wrapper.final_call_decorator(
        "Configuring Discord RPC",
        "Configuring Discord RPC: success",
        "Configuring Discord RPC: failure!"
    )(configure)()
