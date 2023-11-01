#include "main.hpp"
#include <zip.h>
#include <sys/wait.h>
#include <iostream>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

int main(int argc, const char** argv) {
    set<string> options;
    vector<string> arguments;
    {
        bool readingOptions = true;
        for (int i = 1; i < argc; ++i) {
            string asString = argv[i];
            if (asString.starts_with('-') && readingOptions) {
                if (asString == "--")
                    readingOptions = false;
                else
                    options.insert(mapArgument(asString));
            } else {
                arguments.push_back(asString);
                readingOptions = false;
            }
        }
    }
    if (options.contains("version")) {
        cout << "MSERMAN - Minecraft SERver MANager by Loweder\n"
                "Version: " << MSERMAN_VERSION << "\n";
        return 0;
    } else if (arguments.empty() || options.contains("help")) exitWithUsage();
    if (arguments[0] == "verify") {
        StringNode root{};
        parseConfig(root, "mserman.conf");
        StatNode stats;
        fs::path dirRoot = root["GENERAL"]["ROOT-DIR"].value;
        fs::path dirMinecraft = root["GENERAL"]["MINECRAFT"]["DIR"].value;
        fs::path dirVersions = ensureExists(dirRoot, "version");
        fs::path dirCores = ensureExists(dirRoot, "core");
        fs::path dirBackups = ensureExists(dirRoot, "backup");
        fs::path dirServers = ensureExists(dirRoot, "server");
        for (const auto &version: fs::directory_iterator(dirVersions)) {
            if (!version.is_directory()) cout << "Found non-version object " << version.path().filename() << " in versions folder\n";
            else if (!root("VERSIONS")(version.path().filename())) cout << "Found unknown version " << version.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["VERSIONS"][version.path().filename()]["SELF"]++;
        }
        for (auto &versionEntry: root["VERSIONS"]) {
            const string version = versionEntry.first;
            cout << "Processing version '" << version << "'\n";
            StringNode &rootVersion = versionEntry.second;
            StringNode &rootMods = rootVersion["MODS"];
            StringNode &rootPlugins = rootVersion["PLUGINS"];
            StringNode &rootMFamilies = rootVersion["MOD-FAMILIES"];
            StringNode &rootPFamilies = rootVersion["PLUGIN-FAMILIES"];
            fs::path dirMods = ensureExists(dirVersions / version, "mods");
            fs::path dirPlugins = ensureExists(dirVersions / version, "plugins");
            fs::path dirConfigs = ensureExists(dirVersions / version, "config");
            function<void(StringNode&, StringNode&, fs::path&, bool)> entryProcessor = [&stats, &version](StringNode &rootEntry, StringNode &rootFamily, fs::path &dir, bool isMod) {
                for (const auto& file : fs::directory_iterator(dir)) {
                    string fileName = relative(file.path(), dir);
                    if (!rootEntry.val(fileName)) {
                        if (!file.is_regular_file() || file.path().extension() != ".jar") {
                            cout << "Found invalid " << (isMod ? "mod" : "plugin") << " file " << fileName << "\n";
                            stats["UNKNOWN"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][fileName]++;
                        } else {
                            string prefix = file.path().stem().filename();
                            string key;
                            for (int counter = 1; rootEntry.children.contains(key = (prefix + to_string(counter))); counter++);
                            rootEntry[key].value = fileName;
                        }
                    }
                }
                set<string> processed;
                set<string> removed;
                function<void(StringNode&, fs::path&, const string&, StringNode&, bool)> dfs = [&removed, &processed, &dfs, &stats, &version](StringNode &entryRoot, fs::path &dir, const string& name, StringNode &entry, bool isMod) {
                    if (!processed.insert(name).second) return;
                    string &status = stringToUpper(entry["STATUS"].value);
                    fs::path filePath = dir / entry.value;
                    if (is_regular_file(filePath) && filePath.extension() == ".jar") {
                        int codeSide = stringToCode(isMod ? entry["SIDE"].value : "", 2, 0b11);
                        int codeType = stringToCode(entry["TYPE"].value, 1, 0b1);
                        if (isMod) entry["SIDE"].value = codeToString(codeSide, 2);
                        entry["TYPE"].value = codeToString(codeType, 1);
                        entry["DEPENDENCIES"];
                        if (status != "VALID") {
                            cout << "Need to configure " << (isMod ? "mod" : "plugin") << " '" << name << "'\n";
                            stats["NEW"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][name]++;
                            status = "NEW";
                            return;
                        }
                        bool fine = true;
                        for (auto &dependency : entry["DEPENDENCIES"]) {
                            if (entryRoot(dependency.first)) {
                                dfs(entryRoot, dir, dependency.first, entryRoot[dependency.first], isMod);
                                if (!stats("VALID")("VERSIONS")(version)(isMod ? "MODS" : "PLUGINS")(name)) {
                                    cout << "Failed to resolve dependency for " << (isMod ? "mod" : "plugin") << " '" << name << "'\n";
                                    stats["LOST"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][dependency.first][isMod ? "BY-MOD" : "BY-PLUGIN"][name]++;
                                    fine = false;
                                } else if (isMod) {
                                    int depSide = stringToCode(entryRoot[dependency.first]["SIDE"].value, 2, 0b11);
                                    if ((depSide & codeSide) != codeSide) {
                                        cout << "Dependency for mod " << " '" << name << "' is on the wrong side\n";
                                        stats["LOST"]["VERSIONS"][version]["MODS"][dependency.first]["BY-MOD"][name]["SIDE"]++;
                                        fine = false;
                                    }
                                }
                            } else {
                                cout << "Failed to resolve dependency for " << (isMod ? "mod" : "plugin") << " '" << name << "'\n";
                                stats["LOST"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][dependency.first][isMod ? "BY-MOD" : "BY-PLUGIN"][name]++;
                                fine = false;
                            }
                        }
                        if (fine)
                            stats["VALID"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][name]++;
                    } else if (!exists(filePath)) {
                        if (status != "GONE") {
                            cout << "Failed to find " << (isMod ? "mod \"" : "plugin \"") << name << "\" with file \"" << entry.value << "\"\n";
                        }
                        if (status == "NOT-VALID")
                            removed.insert(name);
                        else {
                            cout << "Need to clean entry for gone " << (isMod ? "mod" : "plugin") << " \"" << name << "\"\n";
                        }
                        status = "GONE";
                    } else {
                        cout << "Found invalid " << (isMod ? "mod \"" : "plugin \"") << name << "\" with file \"" << entry.value << "\"\n";
                        stats["UNKNOWN"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][entry.value]++;
                        status = "NOT-VALID";
                    }
                };
                for (auto &entry: rootEntry) dfs(rootEntry, dir, entry.first, entry.second, isMod);
                for (const auto &entry: removed) rootEntry.children.erase(entry);
                for (auto &family: rootFamily) {
                    bool fine = true;
                    for (auto &entry: family.second) {
                        if (!stats("VALID")("VERSIONS")(version)(isMod ? "MODS" : "PLUGINS")(entry.first)) {
                            cout << "Failed to resolve entry for " << (isMod ? "mod" : "plugin") << " family '"
                                 << family.first << "'\n";
                            stats["LOST"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][entry.first]["BY-FAMILY"][family.first]++;
                            fine = false;
                        } else if(!stringToCode(rootEntry[entry.first]["TYPE"].value, 1, 0b1)) {
                            cout << "Entry for " << (isMod ? "mod" : "plugin") << " family '"
                                 << family.first << "' should not be a library\n";
                            stats["LOST"]["VERSIONS"][version][isMod ? "MODS" : "PLUGINS"][entry.first]["BY-FAMILY"][family.first]++;
                            fine = false;
                        }
                    }
                    if (fine)
                        stats["VALID"]["VERSIONS"][version][isMod ? "MOD-FAMILIES" : "PLUGIN-FAMILIES"][family.first]++;
                }
            };
            entryProcessor(rootMods, rootMFamilies, dirMods, true);
            entryProcessor(rootPlugins, rootPFamilies, dirPlugins, false);
            for (const auto &configs: fs::directory_iterator(dirConfigs)) {
                if (!configs.is_directory()) cout << "Found non-meta-config object " << configs.path().filename() << " in meta-configs folder of '" << versionEntry.first << "'\n";
                else if (!rootVersion("META-CONFIGS")(configs.path().filename())) cout << "Found unknown meta-config " << configs.path().filename() << " in folder of '" << versionEntry.first << "'\n";
                else continue;
                stats["UNKNOWN"]["VERSIONS"][versionEntry.first]["META-CONFIGS"][configs.path().filename()]++;
            }
            for (auto &metaConfigEntry: rootVersion["META-CONFIGS"]) {
                const string metaConfig = metaConfigEntry.first;
                StringNode &rootConfigs = metaConfigEntry.second;
                fs::path dirConfig = ensureExists(dirConfigs, metaConfig);
                for (const auto& file : fs::recursive_directory_iterator(dirConfig)) {
                    string fileName = relative(file.path(), dirConfig);
                    if (file.is_regular_file()) {
                        if (!rootConfigs(fileName))
                            rootConfigs[fileName];
                    } else if (!file.is_directory()) {
                        cout << "Found invalid config file '" << fileName << "'\n";
                        stats["UNKNOWN"]["VERSIONS"][versionEntry.first]["META-CONFIGS"][metaConfigEntry.first][fileName]++;
                    }
                }
                set<string> removed;
                bool fine = true;
                for (auto &config : rootConfigs) {
                    string &status = stringToUpper(config.second.value);
                    fs::path filePath = dirConfig / config.first;
                    if (is_regular_file(filePath)) {
                        if (status != "VALID") {
                            cout << "Need to configure config '" << config.first << "'\n";
                            status = "NEW";
                            fine = false;
                        }
                    } else if (!exists(filePath)) {
                        if (status != "GONE") {
                            cout << "Failed to find config file '" << config.first << "'\n";
                        }
                        if (status == "NOT-VALID")
                            removed.insert(config.first);
                        else {
                            cout << "Need to clean entry for gone config '" << config.first << "'\n";
                        }
                        fine = false;
                        status = "GONE";
                    } else {
                        cout << "Found invalid config file '" << config.first << "'\n";
                        stats["UNKNOWN"]["VERSIONS"][versionEntry.first]["META-CONFIGS"][metaConfigEntry.first][config.first]++;
                        fine = false;
                        status = "NOT-VALID";
                    }
                }
                if (fine) stats["VALID"]["VERSIONS"][versionEntry.first]["META-CONFIGS"][metaConfig]++;
                else stats["NEW"]["VERSIONS"][versionEntry.first]["META-CONFIGS"][metaConfig]++;
                for (const auto &entry: removed) metaConfigEntry.second.children.erase(entry);
            }
        }
        cout.flush();
        for (const auto &core: fs::directory_iterator(dirCores)) {
            if (!core.is_directory()) cout << "Found non-core object " << core.path().filename() << " in cores folder\n";
            else if (!root("CORES")(core.path().filename())) cout << "Found unknown core " << core.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["CORES"][core.path().filename()]++;
        }
        for (auto &coreEntry: root["CORES"]) {
            const string core = coreEntry.first;
            cout << "Processing core '" << core << "'\n";
            fs::path dir = ensureExists(dirCores, core);
            StringNode &rootCore = coreEntry.second;
            string &status = stringToUpper(rootCore["STATUS"].value);
            int codeSupport = stringToCode(rootCore["SUPPORT"].value, 2, 0b00);
            rootCore["SUPPORT"].value = codeToString(codeSupport, 2);
            int validTests = stringToCode(rootCore["VALID-FOR-TESTING"].value, 1, 0b0);
            rootCore["VALID-FOR-TESTING"].value = codeToString(validTests, 1);
            const string coreRuntime = rootCore["JAVA-RUNTIME"].value;
            const string coreVersion = rootCore["VERSION"].value;
            const string coreMain = rootCore["MAIN"].value;
            if (status != "NOT-VALID" && status != "VALID") {
                cout << "Need to configure core '" << core << "'\n";
                stats["NEW"]["CORES"][core]++;
                status = "NEW";
                continue;
            }
            bool fine = true;
            if (!root("VERSIONS")(coreVersion)) {
                cout << "Version for core '" << coreEntry.first << "' not found\n";
                stats["LOST"]["VERSIONS"][coreVersion]["BY-CORE"][core]++;
                fine = false;
            }
            if (!fs::is_regular_file(dir / coreMain)) {
                cout << "Main file for core '" << core << "' not found\n";
                stats["LOST"]["CORES"][core]["MAIN"]++;
                fine = false;
            }
            if (!fs::is_regular_file(((fs::path)coreRuntime) / "java")) {
                cout << "Runtime for core '" << core << "' not found\n";
                stats["LOST"]["CORES"][core]["RUNTIME"]++;
                fine = false;
            } else {
                cout << "Checking Java runtime '" << coreRuntime << "' for core '" << core << "'" << endl;
                string command = ((((fs::path)coreRuntime)) / "java").string() + " -version";
                bool bufferFine = !system(command.c_str());
                cout << (bufferFine ? "------------------------\nNot a valid Java runtime\n------------------------" : "-------------\nRuntime valid\n-------------") << endl;
                fine = fine & bufferFine;
            }
            status = fine ? "VALID" : "NOT-VALID";
            if (fine) stats["VALID"]["CORES"][core]++;
        }
        cout.flush();
        function<void(StringNode&, bool)> packProcessor = [&root, &stats](StringNode &rootPacks, bool isMod) {
            string packType = isMod ? "MOD" : "PLUGIN";
            string familyName = isMod ? "MOD-FAMILIES" : "PLUGIN-FAMILIES";
            string packTypeLow = isMod ? "mod" : "plugin";
            for (auto &packEntry: rootPacks) {
                const string pack = packEntry.first;
                cout << "Processing " << packTypeLow << " pack '" << pack << "'\n";
                StringNode &rootPack = packEntry.second;
                StringNode &packEntries = rootPack[packType + "S"];
                StringNode &packFamilies = rootPack[familyName];
                const string packVersion = rootPack["VERSION"].value;
                if (!root("VERSIONS")(packVersion)) {
                    cout << "Version for " << packTypeLow << " pack '" << pack << "' not found";
                    stats["LOST"]["VERSIONS"][packVersion]["BY-PACK"][packType][pack]++;
                    continue;
                }
                StringNode &version = root["VERSIONS"][packVersion];
                StringNode &rootEntries = version[packType + "S"];
                StringNode &rootFamilies = version[familyName];
                bool fine = true;
                for (auto &entry: packEntries)
                    if (!stats("VALID")("VERSIONS")(packVersion)(packType + "S")(entry.first)) {
                        cout << "Entry for " << packTypeLow << " pack '" << pack << "' not found";
                        stats["LOST"]["VERSIONS"][packVersion][packType + 'S'][entry.first]["BY-PACK"][pack]++;
                        fine = false;
                    } else if (!stringToCode(rootEntries[entry.first]["TYPE"].value, 1, 0b1)) {
                        cout << "Entry for " << packTypeLow << " pack '"
                             << pack << "' should not be a library\n";
                        stats["LOST"]["VERSIONS"][packVersion][packType + 'S'][entry.first]["BY-PACK"][pack]++;
                        fine = false;
                    }
                for (auto &family: packFamilies)
                    if (!stats("VALID")("VERSIONS")(packVersion)(familyName)(family.first)) {
                        cout << "Entry family for " << packTypeLow << " pack '" << pack << "' not found";
                        stats["LOST"]["VERSIONS"][packVersion][familyName][family.first]["BY-PACK"][pack]++;
                        fine = false;
                    }
                if (fine)
                    stats["VALID"]["PACKS"][packType][pack]++;
            }
        };
        packProcessor(root["PACKS"]["MOD"], true);
        packProcessor(root["PACKS"]["PLUGIN"], false);
        cout.flush();
        for (const auto &server: fs::directory_iterator(dirServers)) {
            if (!server.is_directory()) cout << "Found non-server object " << server.path().filename() << " in servers folder\n";
            else if (!root("SERVERS")(server.path().filename())) cout << "Found unknown server " << server.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["SERVERS"][server.path().filename()]++;
        }
        for (const auto &backup: fs::directory_iterator(dirBackups)) {
            if (!backup.is_regular_file() || backup.path().extension() != ".bak") cout << "Found non-backup object " << backup.path().filename() << " in backups folder\n";
            else if (!root("SERVERS")(backup.path().stem().filename())) cout << "Found unknown backup " << backup.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["SERVER-BACKUPS"][backup.path().filename()]++;
        }
        for (auto &serverEntry: root["SERVERS"]) {
            const string server = serverEntry.first;
            cout << "Processing server '" << server << "'\n";
            StringNode &rootServer = serverEntry.second;
            string &status = stringToUpper(rootServer["STATUS"].value);
            const string serverCore = rootServer["CORE"].value;
            rootServer["JAVA-ARGS"];
            if (status != "NOT-VALID" && status != "VALID") {
                cout << "Need to configure server '" << server << "'\n";
                stats["NEW"]["SERVERS"][server]++;
                status = "NEW";
                continue;
            }
            bool fine = true;
            if (!stats("VALID")("CORES")(rootServer["CORE"])) {
                cout << "Core for server '" << server << "' not valid\n";
                stats["LOST"]["CORES"][rootServer["CORE"]]["BY-SERVER"][server];
                status = "NOT-VALID";
                continue;
            }
            StringNode &core = root["CORES"][serverCore];
            StringNode &version = root["VERSIONS"][core["VERSION"]];
            int support = stringToCode(core["SUPPORT"].value, 2, 0b00);
            if (support & 0b01) {
                if (!stats("VALID")("PACKS")("MOD")(rootServer["MOD-PACK"])) {
                    cout << "Mod-pack for server '" << server << "' not valid\n";
                    stats["LOST"]["PACKS"]["MOD"][rootServer["MOD-PACK"]]["BY-SERVER"][server];
                    fine = false;
                } else if (root["PACKS"]["MOD"][rootServer["MOD-PACK"]]["VERSION"].value != core["VERSION"].value) {
                    cout << "Mod-pack for server '" << server << "' uses other version\n";
                    stats["LOST"]["PACKS"]["MOD"][rootServer["MOD-PACK"]]["BY-SERVER"][server]["MISMATCH"];
                    fine = false;
                }
                if (!version("META-CONFIGS")(rootServer["META-CONFIG"])) {
                    cout << "Meta-config for server '" << server << "' not found\n";
                    stats["LOST"]["VERSIONS"][core["VERSION"]]["META-CONFIGS"][rootServer["META-CONFIG"]]["BY-SERVER"][server];
                    fine = false;
                }
            }
            if (support & 0b10) {
                if (!stats("VALID")("PACKS")("PLUGIN")(rootServer["PLUGIN-PACK"])) {
                    cout << "Plugin-pack for server '" << server << "' not valid\n";
                    stats["LOST"]["PACKS"]["PLUGIN"][rootServer["MOD-PACK"]]["BY-SERVER"][server];
                    fine = false;
                } else if (root["PACKS"]["PLUGIN"][rootServer["PLUGIN-PACK"]]["VERSION"].value != core["VERSION"].value) {
                    cout << "Plugin-pack for server '" << server << "' uses other version\n";
                    stats["LOST"]["PACKS"]["PLUGIN"][rootServer["PLUGIN-PACK"]]["BY-SERVER"][server]["MISMATCH"];
                    fine = false;
                }
            }
            status = fine ? "VALID" : "NOT-VALID";
            if (fine) stats["VALID"]["SERVERS"][core]++;
        }
        cout.flush();
        const string minecraftModPack = root["GENERAL"]["MINECRAFT"]["MOD-PACK"].value;
        const string minecraftMetaConfig = root["GENERAL"]["MINECRAFT"]["META-CONFIG"].value;
        if (options.contains("minecraft")) {
            if (!is_directory(dirMinecraft)) {
                cout << "Invalid minecraft directory " << dirMinecraft << "\n";
            } else if (!stats("VALID")("PACKS")("MOD")(minecraftModPack)) {
                cout << "Mod pack selected in Minecraft configuration \"" << minecraftModPack << "\" is invalid\n";
            } else if (!stats("VALID")("VERSIONS")(root["PACKS"]["MOD"][minecraftModPack]["VERSION"])("META-CONFIGS")(minecraftMetaConfig)) {
                cout << "Meta-config selected in Minecraft configuration \"" << minecraftMetaConfig << "\" is invalid\n";
            } else {
                StringNode &rootModPack = root["PACKS"]["MOD"][minecraftModPack];
                const string version = rootModPack["VERSION"].value;
                StringNode &rootVersion = root["VERSIONS"][version];
                fs::path dirVersion = ensureExists(dirVersions, version);
                fs::path dirMinecraftMod = ensureExists(dirMinecraft, "mods");
                fs::path dirMinecraftConfig = ensureExists(dirMinecraft, "config");
                fs::path dirMod = ensureExists(dirVersion, "mods");
                fs::path dirConfig = ensureExists(dirVersion / "config", minecraftMetaConfig);
                set<string> mods = collectModPack(rootModPack, root, true);
                for (auto &mod: mods) {
                    string modName = rootVersion["MODS"][mod].value;
                    fs::path absoluteItem = dirMod / modName;
                    fs::path absoluteLink = dirMinecraftMod / modName;
                    try {
                        if (is_symlink(absoluteLink) &&
                            !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                            remove(absoluteLink);
                            fs::create_symlink(absolute(absoluteItem), absoluteLink);
                            cout << "Symlink in minecraft configuration to mod fixed: " << modName
                                 << endl;
                        } else if (!exists(absoluteLink)) {
                            fs::create_symlink(absolute(absoluteItem), absoluteLink);
                            cout << "Symlink in minecraft configuration to mod created: " << modName
                                 << endl;
                        } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                            cout << "Failed to create symlink to mod in minecraft configuration, location already taken: " << modName << endl;
                        }
                    } catch (const fs::filesystem_error &ex) {
                        if (is_symlink(absoluteLink)) {
                            remove(absoluteLink);
                            fs::create_symlink(absolute(absoluteItem), absoluteLink);
                            cout << "Symlink in minecraft configuration to mod fixed: " << modName
                                 << endl;
                        }
                    }
                }
                try {
                    if (is_symlink(dirMinecraftConfig) &&
                        !(absolute(read_symlink(dirMinecraftConfig)) == absolute(dirConfig))) {
                        remove(dirMinecraftConfig);
                        fs::create_symlink(absolute(dirConfig), dirMinecraftConfig);
                        cout << "Symlink in minecraft configuration to config folder fixed\n";
                    } else if (!exists(dirMinecraftConfig)) {
                        fs::create_symlink(absolute(dirConfig), dirMinecraftConfig);
                        cout << "Symlink in minecraft configuration to config folder created\n";
                    } else if (exists(dirMinecraftConfig) && !is_symlink(dirMinecraftConfig)) {
                        cout << "Failed to create symlink to config folder in minecraft configuration, location already taken\n";
                    }
                } catch (const fs::filesystem_error &ex) {
                    if (is_symlink(dirMinecraftConfig)) {
                        remove(dirMinecraftConfig);
                        fs::create_symlink(absolute(dirConfig), dirMinecraftConfig);
                        cout << "Symlink in minecraft configuration to config folder fixed\n";
                    }
                }
            }
        } else {
            for (auto &serverEntry: root["SERVERS"]) {
                if (!stats("VALID")("SERVERS")(serverEntry.first)) continue;
                StringNode &rootServer = serverEntry.second;
                StringNode &rootCore = root["CORES"][rootServer["CORE"]];
                StringNode &rootVersion = root["VERSIONS"][rootServer["VERSION"]];
                fs::path dirServer = ensureExists(dirServers, serverEntry.first);
                fs::path coreDir = ensureExists(dirCores, rootServer["CORE"].value);
                fs::path versionDir = ensureExists(dirVersions, rootServer["VERSION"].value);
                for (const auto &item2: fs::directory_iterator(coreDir)) {
                    fs::path relativeItem = relative(item2.path(), coreDir);
                    fs::path absoluteLink = dirServer / relativeItem;
                    try {
                        if (is_symlink(absoluteLink) &&
                            !(absolute(read_symlink(absoluteLink)) == absolute(item2.path()))) {
                            remove(absoluteLink);
                            fs::create_symlink(absolute(item2.path()), absoluteLink);
                            cout << "Symlink in server '" << serverEntry.first << "' fixed: " << relativeItem << endl;
                        } else if (!exists(absoluteLink)) {
                            fs::create_symlink(absolute(item2.path()), absoluteLink);
                            cout << "Symlink in server '" << serverEntry.first << "' created: " << relativeItem << endl;
                        } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                            cout << "Failed to create symlink in server '" << serverEntry.first
                                 << "', location already taken: " << relativeItem << endl;
                        }
                    } catch (const fs::filesystem_error &ex) {
                        if (is_symlink(absoluteLink)) {
                            remove(absoluteLink);
                            fs::create_symlink(absolute(item2.path()), absoluteLink);
                            cout << "Symlink in server '" << serverEntry.first << "' fixed: " << relativeItem << endl;
                        }
                    }
                }
                int support = stringToCode(rootCore["SUPPORT"].value, 2, 0b00);
                if (support & 0b10) {
                    StringNode &rootPluginPack = root["PACKS"]["PLUGIN"][rootServer["PLUGIN-PACK"]];
                    fs::path dirServerPlugin = ensureExists(dirServer, "plugins");
                    fs::path dirPlugins = ensureExists(versionDir, "plugins");
                    set<string> plugins = collectPluginPack(rootPluginPack, root);
                    for (auto &plugin: plugins) {
                        string pluginName = rootVersion["PLUGINS"][plugin].value;
                        fs::path absoluteItem = dirPlugins / pluginName;
                        fs::path absoluteLink = dirServerPlugin / pluginName;
                        try {
                            if (is_symlink(absoluteLink) &&
                                !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to plugin fixed: " << pluginName
                                     << endl;
                            } else if (!exists(absoluteLink)) {
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to plugin created: " << pluginName
                                     << endl;
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to plugin in server '" << serverEntry.first
                                     << "', location already taken: " << pluginName << endl;
                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to plugin fixed: " << pluginName
                                     << endl;
                            }
                        }
                    }
                }
                if (support & 0b01) {
                    StringNode &rootModPack = root["PACKS"]["MOD"][rootServer["MOD-PACK"]];
                    fs::path dirServerMod = ensureExists(dirServer, "mods");
                    fs::path dirServerConfig = ensureExists(dirServer, "config");
                    fs::path dirMod = ensureExists(versionDir, "mods");
                    fs::path dirConfig = ensureExists(versionDir / "config", rootCore["META-CONFIG"].value);
                    set<string> mods = collectModPack(rootModPack, root, false);
                    for (auto &mod: mods) {
                        string modName = rootVersion["MODS"][mod].value;
                        fs::path absoluteItem = dirMod / modName;
                        fs::path absoluteLink = dirServerMod / modName;
                        try {
                            if (is_symlink(absoluteLink) &&
                                !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to mod fixed: " << modName
                                     << endl;
                            } else if (!exists(absoluteLink)) {
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to mod created: " << modName
                                     << endl;
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to mod in server '" << serverEntry.first
                                     << "', location already taken: " << modName << endl;
                            } else {

                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << serverEntry.first << "' to mod fixed: " << modName
                                     << endl;
                            }
                        }
                    }
                    try {
                        if (is_symlink(dirServerConfig) &&
                            !(absolute(read_symlink(dirServerConfig)) == absolute(dirConfig))) {
                            remove(dirServerConfig);
                            fs::create_symlink(absolute(dirConfig), dirServerConfig);
                            cout << "Symlink in server '" << serverEntry.first << "' to config folder fixed\n";
                        } else if (!exists(dirServerConfig)) {
                            fs::create_symlink(absolute(dirConfig), dirServerConfig);
                            cout << "Symlink in server '" << serverEntry.first << "' to config folder created\n";
                        } else if (exists(dirServerConfig) && !is_symlink(dirServerConfig)) {
                            cout << "Failed to create symlink to config folder in server '" << serverEntry.first
                                 << "', location already taken\n";
                        } else {

                        }
                    } catch (const fs::filesystem_error &ex) {
                        if (is_symlink(dirServerConfig)) {
                            remove(dirServerConfig);
                            fs::create_symlink(absolute(dirConfig), dirServerConfig);
                            cout << "Symlink in server '" << serverEntry.first << "' to config folder fixed\n";
                        }
                    }
                }
            }
        }
        setHash(root);
        flushConfig(root, "mserman.conf");
        auto stringStats = stats.convert<StringNode>(+([](const int &toMap) {return to_string(toMap);}));
        flushConfig(stringStats, "report.log");
        return 0;
    } else if (arguments[0] == "make") {
        if (arguments.size() != 3) exitWithUsage();
        StringNode config{};
        parseConfig(config, "mserman.conf");
        if (!checkHash(config)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
        if (config("SERVERS")(arguments[2])) printError("Server with name \"" + arguments[2] + "\" already exists");
        if (!config("CORES")(arguments[1])) printError("Core with name \"" + arguments[1] + "\" doesn't exist");
        config["SERVERS"][arguments[2]]["CORE"].value = arguments[1];
        cout << "Please, run \"mserman verify\" to finish creation\n";
        flushConfig(config, "mserman.conf");
    } else if (arguments[0] == "backup") {
        if (arguments.size() != 3) exitWithUsage();
        //TODO: Implement loading and '-a' support
        StringNode config{};
        parseConfig(config, "mserman.conf");
        if (!config("SERVERS")(arguments[2])) printError("Server with name \"" + arguments[2] + "\" doesn't exist");
        if (arguments[1] == "load") {
            fs::path archivePath = ((fs::path)config["GENERAL"]["ROOT-DIR"].value) / ("backup/" + arguments[2] + ".bak");
            fs::path serverDir = ((fs::path)config["GENERAL"]["ROOT-DIR"].value) / ("server/" + arguments[2] + "/");
            if (!exists(archivePath))
                printError("Server '" + arguments[2] + "' doesn't have a backup");
            if (!options.contains("force-yes"))
                while (true) {
                    char choice;
                    cout << "Do you want to proceed? (y/n): ";
                    cin >> choice;

                    if (choice == 'y' || choice == 'Y') break;
                    else if (choice == 'n' || choice == 'N') printError("Aborted");
                    else cout << "Invalid choice. Please enter 'y' or 'n'.\n";
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
                    else if (choice == 'n' || choice == 'N') printError("Aborted");
                    else cout << "Invalid choice. Please enter 'y' or 'n'.\n";
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
    } else if (arguments[0] == "collect") {
        if (arguments.size() != 2) exitWithUsage();
        StringNode root{};
        parseConfig(root, "mserman.conf");
        if (!checkHash(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
        if (!root("SERVERS")(arguments[1]))
            printError("Server with name \"" + arguments[1] + "\" doesn't exist");
        StringNode &rootServer = root["SERVERS"][arguments[1]];
        StringNode &rootCore = root["CORES"][rootServer["CORE"]];
        StringNode &rootVersion = root["VERSIONS"][rootCore["VERSION"]];
        if (!(stringToCode(rootCore["SUPPORT"].value, 2, 0b00) & 0b01))
            printError("Server with name \"" + arguments[1] + "\" doesn't use mods");
        zip_t *archive = zip_open((arguments[1] + "_user.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
        if (!archive) printError("Was unable to make archive");
        fs::path dirVersion = ((fs::path)root["GENERAL"]["ROOT-DIR"].value) / ("version/" + rootCore["VERSION"].value);
        fs::path dirMods = dirVersion / "mods/";
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
        set<string> mods = collectModPack(root["PACKS"]["MOD"][rootServer["MOD-PACK"]], root, true);
        for (auto &mod: mods) {
            string realName = rootVersion["MODS"][mod].value;
            fs::path file = dirMods / realName;
            if (!fs::is_regular_file(file)) printError("One of mods is not a regular file");
            string filePath = file.string();

            struct zip_source *source = zip_source_file(archive, filePath.c_str(), 0, 0);
            if (!source) printError("Was unable to open mod file as zip source");

            zip_file_add(archive, ("mods/" + realName).c_str(), source, ZIP_FL_OVERWRITE);
        }
        if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err));
    } else if (arguments[0] == "boot") {
        if (arguments.size() != 2) exitWithUsage();
        StringNode config{};
        parseConfig(config, "mserman.conf");
        if (!checkHash(config)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
        if (!config("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
        pid_t pid = forkToServer({arguments[1], config["SERVERS"][arguments[1]]}, config, 0, 1, 2);
        if (pid != -1 && pid != 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                cout << "Server process exited with status " << WEXITSTATUS(status) << endl;
            }
        } else {
            cerr << "Failed to start server.\n";
            return 1;
        }
    } else exitWithUsage();
    return 0;
}

#pragma clang diagnostic pop