#include "main.hpp"
#include <sys/wait.h>

pid_t forkToServer(pair<const string, StringNode &> serverEntry, StringNode &root, int input, int output, int error) {
    pid_t child_pid = fork();
    if (child_pid == 0) {
        const string &server = serverEntry.first;
        StringNode &rootServer = serverEntry.second;
        StringNode &core = root["CORES"][rootServer["CORE"]];
        dup2(input, STDIN_FILENO);
        dup2(output, STDOUT_FILENO);
        dup2(error, STDERR_FILENO);
        fs::path pathTo = ((fs::path)root["GENERAL"]["ROOT-DIR"].value) / ("server/" + server + "/");
        fs::current_path(pathTo);
        vector<char *> args;
        args.push_back((char*)"java");
        string strArgs = rootServer["JAVA-ARGS"].value;
        size_t pos;
        while ((pos = strArgs.find(' ')) != string::npos) {
            string token = strArgs.substr(0, pos);
            args.push_back(token.data());
            strArgs.erase(0, pos + 1);
        }
        args.push_back(strArgs.data());
        args.push_back((char*)"-jar");
        args.push_back(core["MAIN"].value.data());
        args.push_back(nullptr);
        execvp((((fs::path)core["JAVA-RUNTIME"].value) / "java").c_str(), args.data());
    }
    return child_pid;
}

set<string> collectModPack(StringNode &pack, StringNode &root, bool side) {
    StringNode &rootVersion = root["VERSIONS"][pack["VERSION"]];
    set<string> mods{};
    set<string> checked;
    function<void(const string&)> checker = [&rootVersion, &side, &mods, &checker, &checked](const string &modId) {
        if (!checked.insert(modId).second) return;
        StringNode &rootMod = rootVersion["MODS"][modId];
        int modSide = stringToCode(rootMod["SIDE"].value, 2, 0b11);
        if (modSide & (1 << side)) {
            mods.emplace(modId);
            for (auto &item: rootMod["DEPENDENCIES"]) {
                checker(item.first);
            }
        }
    };
    for (auto &mod: pack["MODS"]) {
        checker(mod.first);
    }
    for (auto &family: pack["MOD-FAMILIES"]) {
        const string &mode = family.second.value;
        for (auto &mod: rootVersion["MOD-FAMILIES"][family.first]) {
            if (mod.second(mode))
                checker(mod.first);
        }
    }
    return mods;
}
set<string> collectPluginPack(StringNode &pack, StringNode& root) {
    StringNode &rootVersion = root["VERSIONS"][pack["VERSION"]];
    set<string> plugins{};
    set<string> checked;
    function<void(const string&)> checker = [&rootVersion, &plugins, &checker, &checked](const string &modId) {
        if (!checked.insert(modId).second) return;
        StringNode &rootMod = rootVersion["PLUGINS"][modId];
        plugins.emplace(modId);
        for (auto &item: rootMod["DEPENDENCIES"]) {
            checker(item.first);
        }
    };
    for (auto &mod: pack["PLUGINS"]) {
        checker(mod.first);
    }
    for (auto &family: pack["PLUGIN-FAMILIES"]) {
        const string &mode = family.second.value;
        for (auto &mod: rootVersion["PLUGIN-FAMILIES"][family.first]) {
            if (mod.second(mode))
                checker(mod.first);
        }
    }
    return plugins;
}
