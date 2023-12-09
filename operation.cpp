#include <iostream>
#include <sys/wait.h>
#include <zip.h>
#include "main.hpp"
#include "bit_defines.hpp"

int switchOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    StringNode config;
    StringNode cache;
    parseConfig(config, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    if (!checkHash(config, cache)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!config("VERSIONS")(arguments[1])) printError("Version with name \"" + arguments[1] + "\" doesn't exist");
    config["GENERAL"]["MINECRAFT"]["VERSION"].value = arguments[1];
    cout << "Please, run \"mserman --minecraft verify\" to finish switching\n";
    flushConfig(config, "mserman.conf");
    return 0;
}
int makeOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 4) exitWithUsage();
    StringNode config;
    StringNode cache;
    parseConfig(config, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    if (!checkHash(config, cache)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (config("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" already exists");
    if (!config("VERSIONS")(arguments[2])) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
    if (!config["VERSIONS"][arguments[2]]("CORES")(arguments[3])) printError("Core with name \"" + arguments[3] + "\" doesn't exist");
    config["SERVERS"][arguments[1]]["VERSION"].value = arguments[2];
    config["SERVERS"][arguments[1]]["CORE"].value = arguments[3];
    cout << "Please, run \"mserman verify\" to finish creation\n";
    flushConfig(config, "mserman.conf");
    return 0;
}
int collectOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    StringNode root;
    StringNode cache;
    parseConfig(root, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    if (!checkHash(root, cache)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
    if (!cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
    StringNode &rootServer = root["SERVERS"][arguments[1]];
    StringNode &rootVersion = root["VERSIONS"][rootServer["VERSION"]];
    StringNode &rootCore = rootVersion["CORES"][rootServer["CORE"]];
    if (!(stringToCode(rootCore["SUPPORT"].value, 3, 0b000) & (SUPPORT_FORGE | SUPPORT_FABRIC)))
        printError("Server with name \"" + arguments[1] + "\" doesn't use mods");
    zip_t *archive = zip_open((arguments[1] + "_user.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    if (!archive) printError("Was unable to make archive");
    fs::path dirVersion = ((fs::path)root["GENERAL"]["ROOT-DIR"].value) / ("version/" + rootCore["VERSION"].value);
    fs::path dirMods = dirVersion / "entries/";
    fs::path dirConfig = dirVersion / ("config/" + rootServer["META-CONFIG"].value);
    function<void(const fs::path&)> recFunc = [&archive, &dirConfig, &recFunc](const fs::path &dir){
        for (const auto &item2: fs::directory_iterator(dir)) {
            if (item2.is_regular_file()) {
                zip_source_t *source = zip_source_file(archive, item2.path().c_str(), 0, 0);
                if (!source) printError("Was unable to open file as zip source");
                zip_file_add(archive, ("config" / relative(item2.path(), dirConfig)).c_str(), source, ZIP_FL_OVERWRITE);
            } else if (item2.is_directory()) {
                zip_dir_add(archive, ("config" / relative(item2.path(), dirConfig)).c_str(), 0);
                recFunc(item2.path());
            }
        }
    };
    zip_dir_add(archive, "config", 0);
    recFunc(dirConfig);
    zip_dir_add(archive, "mods", 0);
    set<pair<string, string>> mods = collectPack(root["PACKS"]["MOD"][rootServer["MOD-PACK"]], rootVersion, SIDE_CLIENT);
    for (auto &[module, version]: mods) {
        string realName = rootVersion["ENTRIES"][module][version].value;
        fs::path file = dirMods / realName;
        if (!is_regular_file(file)) printError("One of mods is not a regular file");
        string filePath = file.string();

        zip_source *source = zip_source_file(archive, filePath.c_str(), 0, 0);
        if (!source) printError("Was unable to open mod file as zip source");

        zip_file_add(archive, ("mods/" + realName).c_str(), source, ZIP_FL_OVERWRITE);
    }
    if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err));
    return 0;
}
int backupOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 3) exitWithUsage();
    //TODO: Implement loading and '-a' support
    StringNode config{};
    parseConfig(config, "mserman.conf");
    if (!config("SERVERS")(arguments[2])) printError("Server with name \"" + arguments[2] + "\" doesn't exist");
    if (arguments[1] == "load") {
        fs::path archivePath = static_cast<fs::path>(config["GENERAL"]["ROOT-DIR"].value) / ("backup/" + arguments[2] + ".bak");
        fs::path serverDir = static_cast<fs::path>(config["GENERAL"]["ROOT-DIR"].value) / ("server/" + arguments[2] + "/");
        if (!exists(archivePath))
            printError("Server '" + arguments[2] + "' doesn't have a backup");
        if (!options.contains("force-yes"))
            while (true) {
                char choice;
                cout << "Do you want to proceed? (y/n): ";
                cin >> choice;

                if (choice == 'y' || choice == 'Y') break;
                if (choice == 'n' || choice == 'N') printError("Aborted");
                cout << "Invalid choice. Please enter 'y' or 'n'.\n";
            }
        zip_stat_t sb;
        zip_t *archive = zip_open(archivePath.c_str(), ZIP_RDONLY, nullptr);
//            for (int i = 0; i < zip_get_num_entries(archive, 0); ++i) {
//                if (zip_stat_index(archive, i, 0, &sb) == 0) {
//                    printf("==================\n");
//                    printf("Name: [%s], ", sb.name);
//                    printf("Size: [%lu], ", sb.size);
//                    printf("mtime: [%u]\n", (unsigned int)sb.mtime);
//                }
//            }
        zip_discard(archive);
    } else if (arguments[1] == "save") {
        if (!options.contains("force-yes"))
            while (true) {
                char choice;
                cout << "Do you want to proceed? (y/n): ";
                cin >> choice;

                if (choice == 'y' || choice == 'Y') break;
                if (choice == 'n' || choice == 'N') printError("Aborted");
                cout << "Invalid choice. Please enter 'y' or 'n'.\n";
            }
        fs::path archivePath = ((fs::path)config["GENERAL"]["ROOT-DIR"].value) / ("backup/" + arguments[2] + ".bak");
        fs::path serverDir = ((fs::path)config["GENERAL"]["ROOT-DIR"].value) / ("server/" + arguments[2] + "/");
        zip_t *archive = zip_open(archivePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
        for (const auto &item: fs::directory_iterator(serverDir)) {
            if (item.is_directory() && item.path().filename().string().starts_with("world")) {
                function<void(const fs::path&)> recFunc = [&archive, &serverDir, &recFunc](const fs::path &dir){
                    for (const auto &item2: fs::directory_iterator(dir)) {
                        if (item2.is_regular_file()) {
                            zip_source_t *source = zip_source_file(archive, item2.path().c_str(), 0, 0);
                            if (!source) printError("Was unable to open file as zip source");
                            zip_file_add(archive, relative(item2.path(), serverDir).c_str(), source, ZIP_FL_OVERWRITE);
                        } else if (item2.is_directory()) {
                            zip_dir_add(archive, relative(item2.path(), serverDir).c_str(), 0);
                            recFunc(item2.path());
                        }
                    }
                };
                zip_dir_add(archive, relative(item.path(), serverDir).c_str(), 0);
                recFunc(item.path());
            }
        }
        if (zip_get_num_entries(archive, 0) == 0) printError("No world files found");
        if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err) + "");
    } else exitWithUsage();
    flushConfig(config, "mserman.conf");
    return 0;
}
int bootOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    StringNode config;
    StringNode cache;
    parseConfig(config, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    if (!checkHash(config, cache)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!config("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
    if (!cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
    if (pid_t pid = forkToServer({arguments[1], config["SERVERS"][arguments[1]]}, config, 0, 1, 2); pid != -1 && pid != 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) cout << "Server process exited with status " << WEXITSTATUS(status) << endl;
    } else {
        cerr << "Failed to start server.\n";
        return 1;
    }
    return 0;
}
int versionOp(set<string>&, vector<string>&) {
    cout << "MSERMAN - Minecraft SERver MANager by Loweder\n"
            "Version: " << MSERMAN_VERSION << "\n";
    return 0;
}
int helpOp(set<string> &options, vector<string> &arguments) {
    //TODO: Add reset/delete/edit/sort/schedule functionality, implement no-report, no-hash-gen, simulate. Add support for 1 letter arguments
    //  Add "no comment gen", clearing other files in mods/configs, compact config write (1 long lists on same line). ClassNotFoundException processing in simulation
    //FIXME:  Add option for "allow absolute paths" in verification, currently unsafe
    cerr << "Minecraft SERver MANager - Utility for easy server managements"
            "Usage: mserman [options...] [--] <command>\n"
            "Commands:\n"
            "    switch <version>                   Switch to specified Minecraft version\n"
            "    boot <server>                      Start Minecraft server\n"
            "    schedule <server> <time>           Schedule Minecraft server\n"
            "    edit <path> <value>                Edit data in config\n"
            "    sort                               Sort data in config\n"
            "    [-m|s] verify                      Verify data in config\n"
            "    [-y] reset [hard]                  Clean data in config (CAUTION! Dangerous)\n"
            "    diversify <type> <name>            Remove version requirements from pack/family/entry dependencies\n"
            "    collect <server>                   Collect user mods in archive\n"
            "    import <server> <version> <path>   Import server\n"
            "    make <server> <version> <core>     Create server\n"
            "    [-y] delete <server>               Delete server (CAUTION! Dangerous)\n"
            "    -a[y] backup (save|load)           Load/backup all worlds (CAUTION! Dangerous)\n"
            "    [-y] backup (save|load) <server>   Load/backup world (CAUTION! Dangerous)\n"
            "Options:\n"
            "    -h    --help                       Display this window\n"
            "    -v    --version                    Display version\n"
            "    -a    --all                        Select all\n"
            "    -s    --simulate                   Simulate server with every mod/plugin to get dependencies/ids\n"
            "    -m    --minecraft                  Only link in Minecraft folder\n"
            "    -n    --no-report                  Do not generate verification report\n"
            "    -y    --force-yes                  Do not ask for confirmation (CAUTION! Dangerous)\n"
            "    -g    --ignore-hash-gen            Do not verify HASH-GEN value. Only use this if you know what you're doing\n";
    return arguments.empty() && !options.contains("functional-help") ? EXIT_FAILURE : 0;
}
int exitOp(set<string>&, vector<string>&) {
    //TODO exit from interactive console
    return 0;
}
int nullOp(set<string>&, vector<string>&) {
    exitWithUsage();
}