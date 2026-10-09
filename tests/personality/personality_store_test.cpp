#include "pbc_json.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define LOG_ERROR(...) ((void)0)
namespace
{
bool ready = true;
pbc_json sheets = pbc_json::object();
std::filesystem::path stateFile;
#include "production_save.inc"

void Check(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Private test directory required");
        auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        stateFile = directory / "sheets.json";
        std::filesystem::remove(stateFile);
        pbc_json a = {{"quality_ids", {"quality_patient"}}};
        pbc_json b = {{"quality_ids", {"quality_protective"}}};
        Check(Save("first", a), "Initial save");
        Check(!Save("first", b), "Permanent sheet changed");
        Check(sheets.at("first") == a, "Failed update mutated choices");
        Check(Save("second", b), "Second save");
        std::ifstream input(stateFile);
        auto restored = pbc_json::parse(input);
        Check(restored.at("characters") == sheets, "Durable file differs from active store");
        auto before = sheets;
        stateFile = directory / "missing-parent" / "sheets.json";
        Check(!Save("third", b), "Unwritable path succeeded");
        Check(sheets == before, "Write failure published choices");
        stateFile = directory;
        Check(!Save("third", b), "Failed rename succeeded");
        Check(sheets == before, "Rename failure published choices");
        ready = false;
        Check(!Save("third", b), "Disabled store accepted choices");
        std::cout << "PASS: permanent choices, reload data, write/rename failure atomicity\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
