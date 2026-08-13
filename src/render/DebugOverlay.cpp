#include "render/DebugOverlay.h"

#include "core/Logging.h"
#include "config/Settings.h"

#include <OgreColourValue.h>
#include <OgreException.h>
#include <OgreFont.h>
#include <OgreFontManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreOverlay.h>
#include <OgreOverlayContainer.h>
#include <OgreOverlayManager.h>
#include <OgreTextAreaOverlayElement.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace
{
    constexpr const char *DEBUG_FONT_NAME = "Omnigrid/DebugFont";
    constexpr const char *OGRE_SAMPLE_DEBUG_FONT = "DebugFont";
    constexpr const char *DEBUG_RESOURCE_GROUP = "OmnigridDebug";
    constexpr const char *SYSTEM_FONT_GROUP = "OmnigridSystemFont";

    bool fontExists( const char *name )
    {
        return static_cast<bool>( Ogre::FontManager::getSingleton().getByName( name ) );
    }

    /**
     * OgreNext's sample media normally declares DebugFont from DebugPack.zip.
     * Prefer that exact, tested route. Only if the sample pack is absent do we
     * construct a font from an already installed system TTF. We never install
     * fonts and never copy them into the project.
     */
    std::string ensureDebugFont( float fontSizePx )
    {
        Ogre::FontManager &fonts = Ogre::FontManager::getSingleton();

        if( fontExists( OGRE_SAMPLE_DEBUG_FONT ) )
        {
            Ogre::FontPtr sampleFont = fonts.getByName( OGRE_SAMPLE_DEBUG_FONT );
            try
            {
                // Validate the complete official path now. A .fontdef can exist
                // while the referenced TTF/pack is missing; merely finding the
                // Font resource is therefore not enough.
                sampleFont->load();
                core::logInfo( "Debug HUD font backend: Ogre DebugFont (DebugPack)" );
                return OGRE_SAMPLE_DEBUG_FONT;
            }
            catch( const Ogre::Exception &e )
            {
                core::logWarn( std::string( "Ogre DebugFont exists but failed to load; "
                                            "trying system-font fallback: " ) +
                               e.getFullDescription() );
            }
        }

        if( fontExists( DEBUG_FONT_NAME ) )
        {
            Ogre::FontPtr font = fonts.getByName( DEBUG_FONT_NAME );
            font->load();
            return DEBUG_FONT_NAME;
        }

        Ogre::ResourceGroupManager &resources = Ogre::ResourceGroupManager::getSingleton();

        // The mounted debug pack may contain a TTF even when its .fontdef was
        // not shipped. Search the dedicated group before touching host fonts.
        Ogre::String source;
        if( resources.resourceGroupExists( DEBUG_RESOURCE_GROUP ) )
        {
            Ogre::StringVectorPtr candidates =
                resources.findResourceNames( DEBUG_RESOURCE_GROUP, "*.ttf" );
            if( candidates && !candidates->empty() )
            {
                source = candidates->front();
                for( const Ogre::String &candidate : *candidates )
                {
                    if( candidate.find( "Mono" ) != Ogre::String::npos ||
                        candidate.find( "mono" ) != Ogre::String::npos )
                    {
                        source = candidate;
                        break;
                    }
                }
            }
        }

        std::string sourceGroup = DEBUG_RESOURCE_GROUP;

        // Ubuntu and most Linux desktops already have at least one of these.
        // Merely expose an existing directory to Ogre's resource manager; no OS
        // modification or installation is performed.
        if( source.empty() )
        {
            namespace fs = std::filesystem;
            static constexpr std::array<const char *, 5> fontDirs = {
                "/usr/share/fonts/truetype/dejavu",
                "/usr/share/fonts/truetype/liberation2",
                "/usr/share/fonts/truetype/freefont",
                "/usr/share/fonts/TTF",
                "/usr/local/share/fonts",
            };

            if( !resources.resourceGroupExists( SYSTEM_FONT_GROUP ) )
                resources.createResourceGroup( SYSTEM_FONT_GROUP );

            for( const char *dir : fontDirs )
            {
                if( !fs::is_directory( dir ) )
                    continue;
                if( !resources.resourceLocationExists( dir, SYSTEM_FONT_GROUP ) )
                    resources.addResourceLocation( dir, "FileSystem", SYSTEM_FONT_GROUP, true );
            }

            Ogre::StringVectorPtr candidates =
                resources.findResourceNames( SYSTEM_FONT_GROUP, "*.ttf" );
            if( candidates && !candidates->empty() )
            {
                source = candidates->front();
                for( const Ogre::String &candidate : *candidates )
                {
                    if( candidate.find( "DejaVuSansMono" ) != Ogre::String::npos ||
                        candidate.find( "LiberationMono" ) != Ogre::String::npos ||
                        candidate.find( "Mono" ) != Ogre::String::npos ||
                        candidate.find( "mono" ) != Ogre::String::npos )
                    {
                        source = candidate;
                        break;
                    }
                }
                sourceGroup = SYSTEM_FONT_GROUP;
            }
        }

        if( source.empty() )
        {
            core::logError(
                "Debug HUD: neither Ogre DebugFont/DebugPack nor an existing system TTF was found" );
            return {};
        }

        core::logInfo( "Debug HUD font fallback: " + source + " (group " + sourceGroup + ")" );

        Ogre::FontPtr font = fonts.create( DEBUG_FONT_NAME, sourceGroup );
        font->setType( Ogre::FT_TRUETYPE );
        font->setSource( source );
        font->setTrueTypeSize( fontSizePx );
        font->setTrueTypeResolution( 96u );
        font->clearCodePointRanges();
        font->addCodePointRange( Ogre::Font::CodePointRange( 32u, 126u ) );

        // Force the load here so any FreeType / texture / HLMS failure is
        // attributed to the font stage in our log instead of surfacing later
        // during the first Overlay render queue update.
        font->load();
        return DEBUG_FONT_NAME;
    }

    unsigned hexNibble( char c )
    {
        if( c >= '0' && c <= '9' ) return static_cast<unsigned>( c - '0' );
        if( c >= 'a' && c <= 'f' ) return static_cast<unsigned>( c - 'a' + 10 );
        return static_cast<unsigned>( c - 'A' + 10 );
    }

    float hexByte( const std::string &value, std::size_t offset )
    {
        const unsigned byte = ( hexNibble( value[offset] ) << 4u ) |
                              hexNibble( value[offset + 1u] );
        return static_cast<float>( byte ) / 255.0f;
    }

    Ogre::ColourValue parseHudColour( const std::string &value )
    {
        const float alpha = value.size() == 9u ? hexByte( value, 7u ) : 1.0f;
        return Ogre::ColourValue( hexByte( value, 1u ), hexByte( value, 3u ),
                                  hexByte( value, 5u ), alpha );
    }
} // namespace

namespace render
{
    bool DebugOverlay::initialize( const config::DebugHudSettings &settings, int viewportHeight )
    {
        try
        {
            mFontSizePx = settings.fontSizePx;
            core::logInfo( "Debug HUD init: resolving font" );
            const std::string fontName = ensureDebugFont( mFontSizePx );
            if( fontName.empty() )
                return false;

            core::logInfo( "Debug HUD init: creating Ogre overlay elements" );
            Ogre::v1::OverlayManager &manager = Ogre::v1::OverlayManager::getSingleton();

            mOverlay = manager.create( "Omnigrid/DebugOverlay" );
            mPanel = static_cast<Ogre::v1::OverlayContainer *>(
                manager.createOverlayElement( "Panel", "Omnigrid/DebugPanel" ) );
            mText = static_cast<Ogre::v1::TextAreaOverlayElement *>(
                manager.createOverlayElement( "TextArea", "Omnigrid/DebugText" ) );
            mText->setFontName( fontName );
            mText->setColour( parseHudColour( settings.color ) );
            mText->setPosition( 0.008f, 0.010f );
            setViewportHeight( viewportHeight );

            mPanel->addChild( mText );
            mOverlay->add2D( mPanel );
            mOverlay->hide();
            mVisible = false;

            core::logInfo( "Debug overlay initialized (F5 toggles)" );
            return true;
        }
        catch( const Ogre::Exception &e )
        {
            core::logError( std::string( "Debug overlay initialization failed (Ogre): " ) +
                            e.getFullDescription() );
            mOverlay = nullptr;
            mPanel = nullptr;
            mText = nullptr;
            mVisible = false;
            return false;
        }
        catch( const std::exception &e )
        {
            core::logError( std::string( "Debug overlay initialization failed (std): " ) + e.what() );
            mOverlay = nullptr;
            mPanel = nullptr;
            mText = nullptr;
            mVisible = false;
            return false;
        }
    }

    void DebugOverlay::setViewportHeight( int viewportHeight )
    {
        if( !mText )
            return;
        const int safeHeight = std::max( viewportHeight, 1 );
        mText->setCharHeight( static_cast<Ogre::Real>( mFontSizePx /
                                                       static_cast<float>( safeHeight ) ) );
    }

    void DebugOverlay::setVisible( bool visible )
    {
        mVisible = visible;
        if( !mOverlay )
            return;
        if( visible )
            mOverlay->show();
        else
            mOverlay->hide();
    }

    void DebugOverlay::setText( const std::string &text )
    {
        if( !mText )
            return;
        mText->setCaption( text );
    }
} // namespace render
