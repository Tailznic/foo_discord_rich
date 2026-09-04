#include <stdafx.h>

#include "discord_impl.h"

#include <fb2k/config.h>

#include <ctime>

#include "external_assets.h"
#include "uploader.h"
#include <fb2k/artwork_metadb.h>

namespace drp::internal
{

PresenceData::PresenceData()
{
    memset( &presence, 0, sizeof( presence ) );
    presence.state = state.c_str();
    presence.details = details.c_str();
}

PresenceData::PresenceData( const PresenceData& other )
{
    CopyData( other );
}

PresenceData& PresenceData::operator=( const PresenceData& other )
{
    if ( this != &other )
    {
        CopyData( other );
    }

    return *this;
}

bool PresenceData::operator==( const PresenceData& other )
{
    auto areStringsSame = []( const char* a, const char* b ) {
        return ( ( a == b ) || ( a && b && !strcmp( a, b ) ) );
    };

    return ( areStringsSame( presence.state, other.presence.state )
             && areStringsSame( presence.details, other.presence.details )
             && areStringsSame( presence.largeImageKey, other.presence.largeImageKey )
             && areStringsSame( presence.largeImageText, other.presence.largeImageText )
             && areStringsSame( presence.smallImageKey, other.presence.smallImageKey )
             && areStringsSame( presence.smallImageText, other.presence.smallImageText )
             && presence.startTimestamp == other.presence.startTimestamp
             && presence.endTimestamp == other.presence.endTimestamp
             && trackLength == other.trackLength );
}

bool PresenceData::operator!=( const PresenceData& other )
{
    return !operator==( other );
}

void PresenceData::CopyData( const PresenceData& other )
{
    metadb = other.metadb;
    state = other.state;
    details = other.details;
    largeImageKey = other.largeImageKey;
    smallImageKey = other.smallImageKey;
    trackLength = other.trackLength;

    memcpy( &presence, &other.presence, sizeof( presence ) );
    presence.state = state.c_str();
    presence.details = details.c_str();
    presence.largeImageKey = ( largeImageKey.empty() ? nullptr : largeImageKey.c_str() );
    presence.smallImageKey = ( smallImageKey.empty() ? nullptr : smallImageKey.c_str() );
}

} // namespace drp::internal

namespace drp
{

PresenceModifier::PresenceModifier( DiscordHandler& parent,
                                    const std::shared_ptr<drp::internal::PresenceData> presenceData )
    : parent_( parent )
    , presenceData_( presenceData )
{
}

PresenceModifier::~PresenceModifier()
{
    const bool hasChanged = ( *parent_.presenceData_ != *presenceData_ );
    if ( hasChanged )
    {
        parent_.presenceData_ = presenceData_;
    }

    const bool needsToBeDisabled = ( isDisabled_
                                     || !playback_control::get()->is_playing()
                                     || ( playback_control::get()->is_paused() && config::disableWhenPaused ) );
    if ( needsToBeDisabled )
    {
        if ( parent_.HasPresence() )
        {
            parent_.ClearPresence();
        }
    }
    else
    {
        if ( !parent_.HasPresence() || hasChanged )
        {
            parent_.SendPresence();
        }
    }
}

static void setImageKey(const std::u8string& imageKey, std::shared_ptr<internal::PresenceData> pd)
{
    pd->largeImageKey = imageKey;
    pd->presence.largeImageKey = pd->largeImageKey.empty() ? nullptr : pd->largeImageKey.c_str();
}

struct sharedData_t
{
    // Must be a shared_ptr or it will be unloaded from memory during the threaded operation
    std::shared_ptr<internal::PresenceData> pm;
    // Normal pointers should be fine for this as it's a static instance
    DiscordHandler* handler;
};
    
std::u8string GetFallbackImageKey()
{
    switch ( config::largeImageSettings )
    {
    case config::ImageSetting::Light:
    {
        return config::largeImageId_Light;
    }
    case config::ImageSetting::Dark:
    {
        return config::largeImageId_Dark;
    }
    case config::ImageSetting::Disabled:
    default:
    {
        return std::u8string{};
    }
    }
}

/**
 * Sets the artwork image for the presence: Discord media-proxy keys are applied directly,
 * while uploaded artwork urls are first resolved into such a key (async, in a worker thread).
 */
static void ApplyArtworkUrl( const std::u8string& artworkUrl,
                             const std::u8string& fallbackKey,
                             std::shared_ptr<internal::PresenceData> pd,
                             DiscordHandler* handler )
{
    if ( artworkUrl.rfind( u8"mp:", 0 ) == 0 )
    { // Already a Discord media-proxy asset key
        setImageKey( artworkUrl, pd );
        return;
    }

    if ( !external_assets::IsResolvableArtworkUrl( artworkUrl ) )
    {
        setImageKey( fallbackKey, pd );
        return;
    }

    std::u8string cachedKey;
    if ( external_assets::TryGetCachedArtworkKey( artworkUrl, cachedKey ) )
    {
        setImageKey( cachedKey.empty() ? fallbackKey : cachedKey, pd );
        return;
    }

    // Use the fallback image while the artwork url is being resolved
    setImageKey( fallbackKey, pd );

    auto shared = std::make_shared<sharedData_t>();
    shared->pm = pd;
    shared->handler = handler;

    const std::u8string applicationId = config::discordAppToken;

    fb2k::splitTask( [artworkUrl, applicationId, shared] {
        // In worker thread!
        try {
            const auto assetKey = external_assets::ResolveArtworkAssetKey( artworkUrl, applicationId );
            if ( assetKey.empty() )
            {
                FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": Failed to resolve Discord asset key for artwork url: " << pfc::string8( artworkUrl.c_str() );
            }

            // Back to the main thread: reapply the image (now from the cache)
            fb2k::inMainThread( [shared] {
                shared->handler->RefreshImageForPresence( shared->pm );
            } );
        } catch(std::exception const & e) {
            external_assets::StoreArtworkKey( artworkUrl, std::u8string{} );
            FB2K_console_formatter() << DRP_NAME_WITH_VERSION << "Critical error: " << e;
        }
    } );
}

void PresenceModifier::UpdateImage()
{
    auto pc = playback_control::get();

    if ( config::largeImageSettings == config::ImageSetting::Disabled )
    {
        setImageKey( std::u8string{}, presenceData_ );
        return;
    }

    metadb_handle_ptr p_out;
    metadb_index_hash hash;
    std::u8string artworkUrl;

    // Check if we already have an uploaded artwork url for the current track
    if ( config::uploadArtwork && pc->get_now_playing( p_out ) )
    {
        clientByGUID( guid::artwork_url_index )->hashHandle( p_out, hash );
        const auto rec = record_get( hash );
        if ( rec.artwork_url.get_length() > 0 )
        {
            artworkUrl = std::u8string( rec.artwork_url );
        }
    }

    const auto fallbackKey = GetFallbackImageKey();

    if ( !artworkUrl.empty() )
    {
        ApplyArtworkUrl( artworkUrl, fallbackKey, presenceData_, &parent_ );
        return;
    }

    setImageKey( fallbackKey, presenceData_ );

    if ( config::uploadArtwork && p_out.is_valid() )
    {
        auto shared = std::make_shared<sharedData_t>();
        shared->pm = presenceData_;
        shared->handler = &parent_;

        fb2k::splitTask( [p_out, hash, shared]{
            // In worker thread!
            try {
                pfc::string8 artwork_url;
                if( uploader::extractAndUploadArtwork(p_out, fb2k::noAbort, artwork_url, hash) )
                {
                    // Back to the main thread: reload the image with the freshly uploaded artwork
                    fb2k::inMainThread( [shared] {
                        shared->handler->RefreshImageForPresence( shared->pm );
                    } );
                }
            } catch(std::exception const & e) {
                // should not really get here
                FB2K_console_formatter() << DRP_NAME_WITH_VERSION << "Critical error: " << e;
            }
        } );
    }
}

void PresenceModifier::UpdateSmallImage()
{
    auto pd = presenceData_;
    auto pc = playback_control::get();

    auto setImageKey = [&pd]( const std::u8string& imageKey ) {
        pd->smallImageKey = imageKey;
        pd->presence.smallImageKey = pd->smallImageKey.empty() ? nullptr : pd->smallImageKey.c_str();
    };

    const bool usePausedImage = ( pc->is_paused() || config::swapSmallImages );

    switch ( config::smallImageSettings )
    {
    case config::ImageSetting::Light:
    {
        setImageKey( usePausedImage ? config::pausedImageId_Light : config::playingImageId_Light );
        break;
    }
    case config::ImageSetting::Dark:
    {
        setImageKey( usePausedImage ? config::pausedImageId_Dark : config::playingImageId_Dark );
        break;
    }
    case config::ImageSetting::Disabled:
    {
        setImageKey( std::u8string{} );
        break;
    }
    }
}

/**
 * https://stackoverflow.com/a/59691895
 * Calculate the number of characters in a utf-8 string.
 *  Some composite characters that are composed of multiple different characters, such as some emojis,
 *  are counted as multiple characters.
 */
size_t count_codepoints( const std::u8string& str )
{
    size_t count = 0;
    for ( auto& c: str )
        if ( ( c & 0b1100'0000 ) != 0b1000'0000 ) // Not a trailing byte
            ++count;
    return count;
}

void PresenceModifier::UpdateTrack( metadb_handle_ptr metadb )
{
    auto pd = presenceData_;

    pd->state.clear();
    pd->details.clear();
    pd->trackLength = 0;

    if ( metadb.is_valid() )
    { // Need to save, since refresh might be required when settings are changed
        pd->metadb = metadb;
    }

    auto pc = playback_control::get();
    const auto queryData = [&pc, metadb = pd->metadb]( const std::u8string& query ) -> std::u8string {
        titleformat_object::ptr tf;
        titleformat_compiler::get()->compile_safe( tf, query.c_str() );
        pfc::string8_fast result;

        if ( pc->is_playing() )
        {
            metadb_handle_ptr dummyHandle;
            pc->playback_format_title_ex( dummyHandle, nullptr, result, tf, nullptr, playback_control::display_level_all );
        }
        else if ( metadb.is_valid() )
        {
            metadb->format_title( nullptr, result, tf, nullptr );
        }

        return result.c_str();
    };
    const auto fixStringLength = []( std::u8string& str ) {
        // Required for correct calculation of utf-8 string length
        if ( count_codepoints(str) == 1 )
        { // minimum allowed non-zero string length is 2, so we need to pad it
            str += ' ';
        }
        // Normal length used here as resizing truncates the string to 127 bytes anyways
        else if ( str.length() > 127 )
        { // maximum allowed length is 127
            str.resize( 127 );
        }
    };

    pd->state = queryData( config::stateQuery );
    fixStringLength( pd->state );
    pd->details = queryData( config::detailsQuery );
    fixStringLength( pd->details );

    const std::u8string lengthStr = queryData( "[%length_seconds_fp%]" );
    pd->trackLength = ( lengthStr.empty() ? 0 : stold( lengthStr ) );

    const std::u8string durationStr = queryData( "[%playback_time_seconds%]" );

    pd->presence.state = pd->state.c_str();
    pd->presence.details = pd->details.c_str();
    UpdateDuration( durationStr.empty() ? 0 : stold( durationStr ) );
}

void PresenceModifier::UpdateDuration( double time )
{
    auto pd = presenceData_;
    auto pc = playback_control::get();
    const config::TimeSetting timeSetting = ( ( pd->trackLength && pc->is_playing() && !pc->is_paused() )
                                                  ? config::timeSettings
                                                  : config::TimeSetting::Disabled );
    switch ( timeSetting )
    {
    case config::TimeSetting::Elapsed:
    {
        pd->presence.startTimestamp = std::time( nullptr ) - std::llround( time );
        pd->presence.endTimestamp = 0;

        break;
    }
    case config::TimeSetting::Remaining:
    {
        pd->presence.startTimestamp = 0;
        pd->presence.endTimestamp = std::time( nullptr ) + std::max<uint64_t>( 0, std::llround( pd->trackLength - time ) );

        break;
    }
    case config::TimeSetting::Disabled:
    {
        pd->presence.startTimestamp = 0;
        pd->presence.endTimestamp = 0;

        break;
    }
    }
}

void PresenceModifier::DisableDuration()
{
    auto pd = presenceData_;
    pd->presence.startTimestamp = 0;
    pd->presence.endTimestamp = 0;
}

void PresenceModifier::Disable()
{
    isDisabled_ = true;
}

DiscordHandler& DiscordHandler::GetInstance()
{
    static DiscordHandler discordHandler;
    return discordHandler;
}

void DiscordHandler::Initialize()
{
    appToken_ = config::discordAppToken;

    DiscordEventHandlers handlers{};

    handlers.ready = OnReady;
    handlers.disconnected = OnDisconnected;
    handlers.errored = OnErrored;

    Discord_Initialize( appToken_.c_str(), &handlers, 1, nullptr );
    Discord_RunCallbacks();

    hasPresence_ = true; ///< Discord may use default app handler, which we need to override

    auto pm = GetPresenceModifier();
    pm.UpdateImage();
    pm.Disable(); ///< we don't want to activate presence yet
}

void DiscordHandler::Finalize()
{
    Discord_ClearPresence();
    Discord_Shutdown();
}

void DiscordHandler::OnSettingsChanged()
{
    if ( appToken_ != static_cast<std::string>( config::discordAppToken ) )
    {
        Finalize();
        Initialize();
    }

    // Allow artwork key resolution to retry with the new settings
    external_assets::ClearArtworkKeyCache();

    {
        auto pm = GetPresenceModifier();
        pm.UpdateImage();
        pm.UpdateSmallImage();
        pm.UpdateTrack();
        if ( !config::isEnabled )
        {
            pm.Disable();
        }
    }

    if ( config::isEnabled )
    {
        auto pc = playback_control::get();
        if ( pc->is_playing() && !( pc->is_paused() && config::disableWhenPaused ) )
        { // Force presence refresh, so that setting changes (e.g. the activity type) are applied immediately
            SendPresence();
        }
    }
}

void DiscordHandler::RefreshImageForPresence( std::shared_ptr<internal::PresenceData> pd )
{
    // If it's not the same pointer, the presence has changed while the artwork
    // was being uploaded/resolved, so the stale presence data is discarded
    if ( pd != presenceData_ )
    {
        return;
    }

    auto pm = GetPresenceModifier();
    pm.UpdateImage();
}

bool DiscordHandler::HasPresence() const
{
    return hasPresence_;
}

void DiscordHandler::SendPresence()
{
    if ( config::isEnabled )
    {
        presenceData_->presence.activityType = (DiscordActivityType)config::GetDiscordActivityType();
        Discord_UpdatePresence( &presenceData_->presence );
        hasPresence_ = true;
    }
    else
    {
        Discord_ClearPresence();
        hasPresence_ = false;
    }
    Discord_RunCallbacks();
}

void DiscordHandler::ClearPresence()
{
    Discord_ClearPresence();
    hasPresence_ = false;

    Discord_RunCallbacks();
}

PresenceModifier DiscordHandler::GetPresenceModifier()
{
    return PresenceModifier( *this, std::make_shared<internal::PresenceData>(*presenceData_) );
}

void DiscordHandler::OnReady( const DiscordUser* request )
{
    FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": connected to " << ( request->username ? request->username : "<null>" );
}

void DiscordHandler::OnDisconnected( int errorCode, const char* message )
{
    FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": disconnected with code " << errorCode;
    if ( message )
    {
        FB2K_console_formatter() << message;
    }
}

void DiscordHandler::OnErrored( int errorCode, const char* message )
{
    FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": error " << errorCode;
    if ( message )
    {
        FB2K_console_formatter() << message;
    }
}

} // namespace drp
