#pragma once

#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test
{
    struct Case
    {
        const char *name;
        void ( *fn )();
    };

    inline std::vector<Case> &registry()
    {
        static std::vector<Case> r;
        return r;
    }

    struct Registrar
    {
        Registrar( const char *name, void ( *fn )() ) { registry().push_back( { name, fn } ); }
    };

    struct Failure
    {
        std::string message;
    };

    inline int &failures() { static int f = 0; return f; }
    inline int &passed() { static int p = 0; return p; }

    inline void check( bool condition, const std::string &what, const char *file, int line )
    {
        if( !condition )
        {
            ++failures();
            std::ostringstream os;
            os << "  FAIL " << file << ":" << line << " - " << what;
            throw Failure{ os.str() };
        }
        ++passed();
    }

    inline int runAll( const std::vector<Case> &cases )
    {
        int failedCases = 0;
        for( const Case &c : cases )
        {
            failures() = 0;
            try
            {
                c.fn();
                std::cout << "PASS " << c.name << "\n";
            }
            catch( const Failure &f )
            {
                ++failedCases;
                std::cout << "FAIL " << c.name << "\n    " << f.message << "\n";
            }
            catch( const std::exception &e )
            {
                ++failedCases;
                std::cout << "FAIL " << c.name << " (exception: " << e.what() << ")\n";
            }
            catch( ... )
            {
                ++failedCases;
                std::cout << "FAIL " << c.name << " (unknown exception)\n";
            }
        }
        if( failedCases == 0 )
            std::cout << "\nALL TESTS PASSED\n";
        else
            std::cout << "\n" << failedCases << " TEST(S) FAILED\n";
        return failedCases == 0 ? 0 : 1;
    }

    int runAll() { return runAll( registry() ); }
} // namespace test

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static ::test::Registrar registrar_##name( #name, &name );             \
    static void name()

#define CHECK(cond) ::test::check( (cond), #cond, __FILE__, __LINE__ )
#define CHECK_EQ(a, b)\
    do                                                                       \
    {                                                                        \
        auto va = ( a );                                                     \
        auto vb = ( b );                                                     \
        if( !( va == vb ) )                                                  \
        {                                                                    \
            std::ostringstream os_;                                          \
            os_ << #a " == " #b " (actual: " << va << ", expected: " << vb << ")"; \
            ::test::check( false, os_.str(), __FILE__, __LINE__ );           \
        }                                                                    \
        else                                                                 \
            ::test::check( true, "eq", __FILE__, __LINE__ );                 \
    } while( false )
