#pragma once

#include <string>

namespace drp::external_assets
{

/// @brief Checks whether the URL can be resolved into a Discord media-proxy asset key
bool IsResolvableArtworkUrl( const std::u8string& url );

/// @brief Non-blocking lookup of a previously resolved key. Returns false on cache miss.
///        On hit the key may still be empty, which means the resolution was already tried and failed.
bool TryGetCachedArtworkKey( const std::u8string& imageUrl, std::u8string& key );

void StoreArtworkKey( const std::u8string& imageUrl, const std::u8string& key );

void ClearArtworkKeyCache();

/// @brief Resolves a public image URL into a Discord media-proxy key (``mp:external/...``),
///        which can be used as an activity image key.
///        Blocking network call: must be called from a worker thread only.
///        Returns an empty string on failure.
std::u8string ResolveArtworkAssetKey( const std::u8string& imageUrl, const std::u8string& applicationId );

} // namespace drp::external_assets