#include "main.hpp"

int main(int argc, const char** argv) {
    set<string> options;
    vector<string> arguments;
    bool readingOptions = true;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg.starts_with('-') && readingOptions) {
            if (arg == "--") readingOptions = false;
            else options.emplace(mapOption(arg));
        } else {
            arguments.push_back(arg);
            readingOptions = false;
        }
    }
    for (const auto &item: options)
        if (item.starts_with("functional-")) return mapOperation(item.substr(11))(options, arguments);

    if (arguments.empty()) return helpOp(options, arguments);
    return mapOperation(arguments[0])(options, arguments);
}