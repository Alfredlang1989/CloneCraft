#include "TestHarness.h"
#include "config/ContentRoot.h"
#include "config/Settings.h"

#include <filesystem>
#include <string>

namespace
{
    class TempTree
    {
    public:
        TempTree()
        {
            mRoot = std::filesystem::temp_directory_path() / "omnigrid-contentroot-tests";
            std::error_code ignored;
            std::filesystem::remove_all( mRoot, ignored );
            std::filesystem::create_directories( mRoot );
        }

        ~TempTree()
        {
            std::error_code ignored;
            std::filesystem::remove_all( mRoot, ignored );
        }

        void createMod( const std::string &mod )
        {
            std::filesystem::create_directories( mRoot / "MODS" / mod );
        }

        const std::filesystem::path &root() const { return mRoot; }

    private:
        std::filesystem::path mRoot;
    };
} // namespace


TEST_CASE(content_root_uses_configured_mod_when_present)
{
    TempTree tree;
    tree.createMod( "Adventure" );
    tree.createMod( "Default" );

    const config::ContentRoot root = config::resolveContentRoot( tree.root(), "Adventure" );
    CHECK( root.path == tree.root() / "MODS" / "Adventure" );
    CHECK( root.mod == "Adventure" );
}

TEST_CASE(content_root_falls_back_to_default_when_configured_mod_missing)
{
    TempTree tree;
    tree.createMod( "Default" );

    const config::ContentRoot root = config::resolveContentRoot( tree.root(), "Adventure" );
    CHECK( root.path == tree.root() / "MODS" / "Default" );
    CHECK( root.mod == "Default" );
}

TEST_CASE(content_root_uses_default_when_no_mod_configured)
{
    TempTree tree;
    tree.createMod( "Default" );

    const config::ContentRoot root = config::resolveContentRoot( tree.root(), "" );
    CHECK( root.path == tree.root() / "MODS" / "Default" );
    CHECK( root.mod == "Default" );
}

TEST_CASE(content_root_rejects_path_traversal_mod_names)
{
    TempTree tree;
    tree.createMod( "Default" );

    CHECK( config::resolveContentRoot( tree.root(), ".." ).mod == "Default" );
    CHECK( config::resolveContentRoot( tree.root(), "." ).mod == "Default" );
    CHECK( config::resolveContentRoot( tree.root(), "MODS/Default" ).mod == "Default" );
    CHECK( config::resolveContentRoot( tree.root(), "../Default" ).mod == "Default" );
    CHECK( config::resolveContentRoot( tree.root(), "/etc" ).mod == "Default" );
}

TEST_CASE(content_root_without_any_content_returns_empty_path)
{
    TempTree tree;

    const config::ContentRoot root = config::resolveContentRoot( tree.root(), "Adventure" );
    CHECK( root.path.empty() );
    CHECK( root.mod.empty() );
}

TEST_CASE(content_root_never_throws)
{
    TempTree tree;
    (void)config::resolveContentRoot( tree.root() / "does" / "not" / "exist", "Whatever" );
    (void)config::resolveContentRoot( tree.root(), "Default" );
    (void)config::resolveContentRoot( tree.root(), "" );
    (void)config::resolveContentRoot( tree.root(), ".." );
    CHECK( true );
}

int main() { return test::runAll(); }
