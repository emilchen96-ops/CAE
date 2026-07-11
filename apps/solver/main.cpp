#include <emilcae/core/Version.hpp>

#include <iostream>
#include <string_view>

namespace {

void printHelp() {
    std::cout << "Usage: EmilCAE.Solver [--help] [--version]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const auto version = emilcae::core::Version::current();

    if (argc == 1) {
        std::cout << "EmilCAE Solver " << version.text() << '\n'
                  << "No solver backend is currently installed.\n";
        return 0;
    }

    const std::string_view argument{argv[1]};
    if (argc == 2 && argument == "--help") {
        printHelp();
        return 0;
    }
    if (argc == 2 && argument == "--version") {
        std::cout << "EmilCAE Solver " << version.text() << '\n';
        return 0;
    }

    std::cerr << "Unknown argument. Use --help for usage.\n";
    return 1;
}
