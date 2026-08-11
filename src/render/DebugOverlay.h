#pragma once

#include <string>

namespace config
{
    struct DebugHudSettings;
}

namespace Ogre
{
    namespace v1
    {
        class Overlay;
        class OverlayContainer;
        class TextAreaOverlayElement;
    }
}

namespace render
{
    /**
     * Small Minecraft-style diagnostic text overlay.
     *
     * The overlay is intentionally presentation-only: Application assembles the
     * diagnostic values, while this class only owns the Ogre Overlay elements.
     * Colour and size come from runtime settings. The overlay deliberately renders
     * one clean text layer without an outline/shadow copy.
     */
    class DebugOverlay
    {
    public:
        DebugOverlay() = default;
        ~DebugOverlay() = default;

        DebugOverlay( const DebugOverlay & ) = delete;
        DebugOverlay &operator=( const DebugOverlay & ) = delete;

        bool initialize( const config::DebugHudSettings &settings, int viewportHeight );
        void setVisible( bool visible );
        void setViewportHeight( int viewportHeight );
        bool isVisible() const { return mVisible; }
        void setText( const std::string &text );

    private:
        Ogre::v1::Overlay *mOverlay = nullptr;
        Ogre::v1::OverlayContainer *mPanel = nullptr;
        Ogre::v1::TextAreaOverlayElement *mText = nullptr;
        float mFontSizePx = 18.0f;
        bool mVisible = false;
    };
} // namespace render
