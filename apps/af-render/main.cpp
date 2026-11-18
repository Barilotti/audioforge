#include "core/Version.h"

#include <cstdio>
#include <string_view>

namespace
{

constexpr int exitOk = 0;
constexpr int exitUsage = 64; // EX_USAGE

void printUsage()
{
    std::puts("Usage: af-render [--version] [--help]\n"
              "\n"
              "Renders an AudioForge project to an audio file without the GUI.\n"
              "Rendering is not implemented yet (stage S7).");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc == 2)
    {
        const std::string_view arg{argv[1]};
        if (arg == "--version")
        {
            const auto description = af::core::buildDescription();
            std::printf("af-render %.*s\n", static_cast<int>(description.size()),
                        description.data());
            return exitOk;
        }
        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return exitOk;
        }
    }

    printUsage();
    return exitUsage;
}
