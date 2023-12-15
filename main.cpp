#include "main.hpp"

int main(int argc, const char** argv) {
    set<string> options;
    vector<string> arguments;
    bool readingOptions = true;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (!readingOptions){
            arguments.push_back(arg);
        } else if (arg.starts_with("--")) {
            if (arg == "--") readingOptions = false;
            else options.emplace(mapOption(arg));
        } else if (arg.starts_with('-')) {
            for (size_t j = 1; j < arg.size(); ++j)
                options.emplace(mapOption(string(1, arg[j])));
        } else {
            arguments.push_back(arg);
            readingOptions = false;
        }
    }
    if (!options.contains("force-root")) {
        string_node global_config;
        std::filesystem::path homeDir = std::filesystem::path(getenv("HOME"));
        std::filesystem::path appDir = homeDir / ".config/mserman.global";
        parseConfig(global_config, appDir, false);
        if (fs::is_directory(global_config["DEFAULT-DIR"].value)) {
            fs::current_path(global_config["DEFAULT-DIR"].value);
        }
        flushConfig(global_config, appDir, false);
    }
    for (const auto &item: options)
        if (item.starts_with("functional-")) return mapOperation(item.substr(11))(options, arguments);

    if (arguments.empty()) return helpOp(options, arguments);
    return mapOperation(arguments[0])(options, arguments);
}