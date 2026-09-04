#include <stdafx.h>

#include "external_assets.h"

#include <fb2k/config.h>

#include <nlohmann/json.hpp>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <winhttp.h>
#pragma comment( lib, "winhttp.lib" )

namespace drp::external_assets
{
namespace
{
constexpr wchar_t DiscordHostName[] = L"discord.com";
constexpr wchar_t DiscordApiPathPrefix[] = L"/api/v9/applications/";
constexpr DWORD RequestTimeoutMs = 5000;

/**
 * Discord does not render raw external urls in activity images.
 * Instead such urls must be registered via the "external-assets" endpoint,
 * which returns a media-proxy key ("mp:external/...") that Discord can render.
 */
struct ArtworkKeyCache
{
    std::mutex mutex;
    /// url -> resolved key; an empty value means that the resolution was already tried and failed
    std::map<std::u8string, std::u8string> urlToKey;
};

ArtworkKeyCache& GetCache()
{
    static ArtworkKeyCache cache;
    return cache;
}

std::string ToStdString( const std::u8string& str )
{
    return std::string( reinterpret_cast<const char*>( str.c_str() ), str.size() );
}

std::u8string ToU8String( const std::string& str )
{
    return std::u8string( reinterpret_cast<const char8_t*>( str.c_str() ), str.size() );
}

template <typename T>
class HandleGuard
{
public:
    HandleGuard() = default;
    explicit HandleGuard( T handle )
        : handle_( handle )
    {
    }
    ~HandleGuard()
    {
        if ( handle_ )
        {
            WinHttpCloseHandle( handle_ );
        }
    }
    HandleGuard( const HandleGuard& ) = delete;
    HandleGuard& operator=( const HandleGuard& ) = delete;

    T Get() const { return handle_; }
    explicit operator bool() const { return ( handle_ != nullptr ); }

private:
    T handle_ = nullptr;
};

std::u8string RequestExternalAssetKey( const std::u8string& imageUrl, const std::u8string& applicationId )
{
    std::wstring path( DiscordApiPathPrefix );
    for ( const auto ch : applicationId )
    {
        path += static_cast<wchar_t>( ch );
    }

    const nlohmann::json requestBody = { { "urls", { ToStdString( imageUrl ) } } };
    const std::string bodyStr = requestBody.dump();
    const std::wstring headers = L"Content-Type: application/json\r\nUser-Agent: foo_discord_rich\r\n";

    HandleGuard<HINTERNET> session( WinHttpOpen( L"foo_discord_rich",
                                                 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                                 WINHTTP_NO_PROXY_NAME,
                                                 WINHTTP_NO_PROXY_BYPASS,
                                                 0 ) );
    if ( !session )
    {
        return std::u8string{};
    }
    WinHttpSetTimeouts( session.Get(), RequestTimeoutMs, RequestTimeoutMs, RequestTimeoutMs, RequestTimeoutMs );

    HandleGuard<HINTERNET> connection( WinHttpConnect( session.Get(), DiscordHostName, INTERNET_DEFAULT_HTTPS_PORT, 0 ) );
    if ( !connection )
    {
        return std::u8string{};
    }

    HandleGuard<HINTERNET> request( WinHttpOpenRequest( connection.Get(),
                                                        L"POST",
                                                        path.c_str(),
                                                        nullptr,
                                                        WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE ) );
    if ( !request )
    {
        return std::u8string{};
    }

    if ( !WinHttpSendRequest( request.Get(),
                              headers.c_str(),
                              static_cast<DWORD>( headers.length() ),
                              static_cast<LPVOID>( const_cast<char*>( bodyStr.data() ) ),
                              static_cast<DWORD>( bodyStr.size() ),
                              static_cast<DWORD>( bodyStr.size() ),
                              0 ) )
    {
        return std::u8string{};
    }
    if ( !WinHttpReceiveResponse( request.Get(), nullptr ) )
    {
        return std::u8string{};
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof( statusCode );
    if ( !WinHttpQueryHeaders( request.Get(),
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &statusCode,
                               &statusCodeSize,
                               WINHTTP_NO_HEADER_INDEX )
         || ( statusCode != HTTP_STATUS_OK ) )
    {
        return std::u8string{};
    }

    std::string response;
    for ( ;; )
    {
        DWORD dwSize = 0;
        if ( !WinHttpQueryDataAvailable( request.Get(), &dwSize ) || ( dwSize == 0 ) )
        {
            break;
        }
        std::vector<char> buf( dwSize );
        DWORD dwRead = 0;
        if ( !WinHttpReadData( request.Get(), buf.data(), dwSize, &dwRead ) || ( dwRead == 0 ) )
        {
            break;
        }
        response.append( buf.data(), dwRead );
    }

    try
    {
        const auto parsed = nlohmann::json::parse( response );
        if ( !parsed.is_array() || parsed.empty() )
        {
            return std::u8string{};
        }
        const auto& entry = parsed.at( 0 );
        if ( !entry.contains( "external_asset_path" ) )
        {
            return std::u8string{};
        }

        auto assetPath = entry.at( "external_asset_path" ).get<std::string>();
        if ( assetPath.empty() )
        {
            return std::u8string{};
        }
        if ( assetPath.rfind( "mp:", 0 ) != 0 )
        {
            assetPath = "mp:" + assetPath;
        }
        return ToU8String( assetPath );
    }
    catch ( const nlohmann::detail::exception& )
    {
        return std::u8string{};
    }
}

} // namespace

bool IsResolvableArtworkUrl( const std::u8string& url )
{
    const bool isHttp = ( url.rfind( u8"http://", 0 ) == 0 );
    const bool isHttps = ( url.rfind( u8"https://", 0 ) == 0 );
    // Discord rejects non http(s) links and very long urls
    return ( ( isHttp || isHttps ) && url.length() <= 256 );
}

bool TryGetCachedArtworkKey( const std::u8string& imageUrl, std::u8string& key )
{
    auto& cache = GetCache();
    std::lock_guard<std::mutex> lock( cache.mutex );
    const auto it = cache.urlToKey.find( imageUrl );
    if ( it == cache.urlToKey.cend() )
    {
        return false;
    }
    key = it->second;
    return true;
}

void StoreArtworkKey( const std::u8string& imageUrl, const std::u8string& key )
{
    auto& cache = GetCache();
    std::lock_guard<std::mutex> lock( cache.mutex );
    cache.urlToKey[imageUrl] = key;
}

void ClearArtworkKeyCache()
{
    auto& cache = GetCache();
    std::lock_guard<std::mutex> lock( cache.mutex );
    cache.urlToKey.clear();
}

std::u8string ResolveArtworkAssetKey( const std::u8string& imageUrl, const std::u8string& applicationId )
{
    std::u8string cachedKey;
    if ( TryGetCachedArtworkKey( imageUrl, cachedKey ) )
    {
        return cachedKey;
    }

    auto key = std::u8string{};
    try
    {
        key = RequestExternalAssetKey( imageUrl, applicationId );
    }
    catch ( ... )
    {
        key = std::u8string{};
    }
    StoreArtworkKey( imageUrl, key );
    return key;
}

} // namespace drp::external_assets