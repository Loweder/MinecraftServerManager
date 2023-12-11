#include <iostream>
#include <sys/wait.h>
#include <zip.h>
#include "main.hpp"
#include "bit_defines.hpp"

int switchOp(set<string> &, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    root_pack root;
    parseConfig(root.root, "mserman.conf");
    parseConfig(root.cache, "cache.conf", false);
    if (!checkHash(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!root.root("VERSIONS")(arguments[1])) printError("Version with name \"" + arguments[1] + "\" doesn't exist");
    root.root["GENERAL"]["MINECRAFT"]["VERSION"].value = arguments[1];
    cout << "Please, run \"mserman --minecraft verify\" to finish switching\n";
    flushConfig(root.root, "mserman.conf");
    return 0;
}
int makeOp(set<string> &, vector<string> &arguments) {
    if (arguments.size() != 4) exitWithUsage();
    root_pack root;
    parseConfig(root.root, "mserman.conf");
    parseConfig(root.cache, "cache.conf", false);
    if (!checkHash(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" already exists");
    if (!root.root("VERSIONS")(arguments[2])) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
    if (!root.root("CORES")(arguments[3])) printError("Core with name \"" + arguments[3] + "\" doesn't exist");
    root.root["SERVERS"][arguments[1]]["VERSION"].value = arguments[2];
    root.root["SERVERS"][arguments[1]]["CORE"].value = arguments[3];
    cout << "Please, run \"mserman verify\" to finish creation\n";
    flushConfig(root.root, "mserman.conf");
    return 0;
}
int sortOp(set<string> &, vector<string> &arguments) {
    if (arguments.size() != 2 && arguments.size() != 1) exitWithUsage();
    if (arguments.size() == 2 && arguments[1] != "raw" && arguments[1] != "compact") exitWithUsage();

    bool raw = false,  compact = false;
    if (arguments.size() == 2)
        raw = arguments[1] == "raw",  compact = arguments[1] == "compact";
    root_pack root;
    parseConfig(root.root, "mserman.conf");
    parseConfig(root.cache, "cache.conf", false);
    flushConfig(root.root, "mserman.conf", !raw, compact);
    flushConfig(root.cache, "cache.conf", !raw, compact);
    return 0;
}
int collectOp(set<string> &, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    root_pack root;
    parseConfig(root.root, "mserman.conf");
    parseConfig(root.cache, "cache.conf", false);
    if (!checkHash(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
    if (!root.cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
    string_node &rootServer = root.root["SERVERS"][arguments[1]];
    string_node &rootCore = root.root["CORES"][rootServer["CORE"]];
    if (!(stringToCode(rootCore["SUPPORT"].value, 3, 0b000) & (SUPPORT_FORGE | SUPPORT_FABRIC)))
        printError("Server with name \"" + arguments[1] + "\" doesn't use mods");
    zip_t *archive = zip_open((arguments[1] + "_user.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    if (!archive) printError("Was unable to make archive");
    fs::path dirVersion = ((fs::path)root.root["GENERAL"]["ROOT-DIR"].value) / ("version/" + rootServer["VERSION"].value);
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

    for (auto &[pack, mc_version]: rootServer["PACKS"]) {
        //TODO obvious
//        if (!root.stats("VALID")("PACKS")(pack)) {
//            cout << "Pack \"" << pack << "\" not valid\n";
//            root.stats["LOST"]["PACKS"][pack]["BY-USER"]++;
//            continue;
//        }
        set<pair<string, string>> mods = collectPack(root.root["PACKS"][pack], root, mc_version.value, SIDE_CLIENT);
        for (auto &[module, version]: mods) {
            string realName = root.root["ENTRIES"][module][mc_version][version].value;
            fs::path file = dirMods / realName;
            if (!is_regular_file(file)) printError("One of mods is not a regular file");
            string filePath = file.string();

            zip_source *source = zip_source_file(archive, filePath.c_str(), 0, 0);
            if (!source) printError("Was unable to open mod file as zip source");

            zip_file_add(archive, ("mods/" + realName).c_str(), source, ZIP_FL_OVERWRITE);
        }
    }
    if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err));
    return 0;
}
//TODO this
int backupOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 3) exitWithUsage();
    //TODO: Implement loading and '-a' support
    string_node config{};
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
int bootOp(set<string> &, vector<string> &arguments) {
    if (arguments.size() != 2) exitWithUsage();
    root_pack root;
    parseConfig(root.root, "mserman.conf");
    parseConfig(root.cache, "cache.conf", false);
    if (!checkHash(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (!root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
    if (!root.cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
    if (pid_t pid = forkToServer(arguments[1], root, 0, 1, 2); pid != -1 && pid != 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) cout << "Server process exited with status " << WEXITSTATUS(status) << endl;
    } else {
        cerr << "Failed to start server.\n";
        return 1;
    }
    return 0;
}