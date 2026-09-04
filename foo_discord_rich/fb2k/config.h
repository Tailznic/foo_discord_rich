#pragma once

#include <qwr/fb2k_config.h>

namespace drp::config
{

enum class ImageSetting : uint8_t
{
    Light = 0,
    Dark,
    Disabled
};
enum class TimeSetting : uint8_t
{
    Elapsed = 0,
    Remaining,
    Disabled
};
enum class PresenceType : uint8_t
{
    Playing = 0,
    Listening,
    Watching,
    Competing
};

extern qwr::fb2k::ConfigBool isEnabled;
extern qwr::fb2k::ConfigUint8Enum<ImageSetting> largeImageSettings;
extern qwr::fb2k::ConfigUint8Enum<ImageSetting> smallImageSettings;
extern qwr::fb2k::ConfigUint8Enum<TimeSetting> timeSettings;
extern qwr::fb2k::ConfigUint8Enum<PresenceType> presenceType;
extern qwr::fb2k::ConfigString stateQuery;
extern qwr::fb2k::ConfigString detailsQuery;

/// @brief Maps the configured PresenceType to the Discord ActivityType constant
int GetDiscordActivityType();

extern qwr::fb2k::ConfigString discordAppToken;
extern qwr::fb2k::ConfigString largeImageId_Light;
extern qwr::fb2k::ConfigString largeImageId_Dark;
extern qwr::fb2k::ConfigString playingImageId_Light;
extern qwr::fb2k::ConfigString playingImageId_Dark;
extern qwr::fb2k::ConfigString pausedImageId_Dark;
extern qwr::fb2k::ConfigString pausedImageId_Light;
extern qwr::fb2k::ConfigString uploadArtworkCommand;
extern qwr::fb2k::ConfigString artworkMetadbKey;

extern qwr::fb2k::ConfigBool disableWhenPaused;
extern qwr::fb2k::ConfigBool swapSmallImages;
extern qwr::fb2k::ConfigBool uploadArtwork;

}; // namespace drp::config
