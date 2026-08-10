#pragma once

#include <string>

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
     * A black offset copy is rendered behind the white text so it remains readable
     * over both sky and terrain without requiring a full UI framework.
     */
    class DebugOverlay
    {
    public:
        DebugOverlay() = default;
        ~DebugOverlay() = default;

        DebugOverlay( const DebugOverlay & ) = delete;
        DebugOverlay &operator=( const DebugOverlay & ) = delete;

        bool initialize();
        void setVisible( bool visible );
        bool isVisible() const { return mVisible; }
        void setText( const std::string &text );

    private:
        Ogre::v1::Overlay *mOverlay = nullptr;
        Ogre::v1::OverlayContainer *mPanel = nullptr;
        Ogre::v1::TextAreaOverlayElement *mText = nullptr;
        Ogre::v1::TextAreaOverlayElement *mShadow = nullptr;
        bool mVisible = false;
    };
} // namespace render
