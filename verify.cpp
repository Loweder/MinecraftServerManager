#include "main.hpp"
#include "bit_defines.hpp"
#include <iostream>
#include <sys/wait.h>
#include <zip.h>
#include <regex>

struct module_entry{
    string id;
    string version;
    enum match : char {
        less_exact = '-',
        greater_exact = '+',
        less = '<',
        exact = '=',
        greater = '>',
        approx = 'A',
        not_exact = '^',
    };
    map<string, pair<string, match>> dependency;
    int model{};
};
class zip_buf : public streambuf {
public:
    explicit zip_buf(zip_file_t* zipFile) : zipFile_(zipFile) {}

protected:
    int_type underflow() override {
        uint64_t rc(zip_fread(this->zipFile_, this->buffer_, 8192));
        this->setg(this->buffer_, this->buffer_,
                   this->buffer_ + std::max(0UL, rc));
        return this->gptr() == this->egptr()
               ? traits_type::eof()
               : traits_type::to_int_type(*this->gptr());
    }

private:
    zip_file_t* zipFile_;
    char buffer_[8192] = {};
};

//Module info file names
const string forgeEntry = "mcmod.info";
const string fabricEntry = "fabric.mod.json";
const string bukkitEntry = "plugin.yml";

/**Get related modules from file
 *
 * @param file input file
 * @return list of found modules
 */
vector<module_entry> lookFile(const fs::path &file) {
    zip_t *jar = zip_open(file.c_str(), 0, nullptr);
    vector<module_entry> result{};
    if (!jar) return result;
    int64_t num = zip_get_num_entries(jar, 0);
    for (int i = 0; i < num; ++i) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (!zip_stat_index(jar, i, 0, &stat)) {
            if (forgeEntry != stat.name && fabricEntry != stat.name && bukkitEntry != stat.name) continue;
            zip_file_t *subFile = zip_fopen_index(jar, i, 0);
            zip_buf buffer{subFile};
            istream data{&buffer};
            if (bukkitEntry == stat.name) {
                string line;
                string id, version;
                vector<string> dependItems;
                for (bool inDepend = false; getline(data, line);) {
                    if (line.starts_with("name: "))
                        id = line.substr(6);
                    else if (line.starts_with("version: "))
                        version = line.substr(9);
                    else if (line.starts_with("depend: [")) {
                        line = line.substr(9, line.find(']') - 9);
                        while (!line.empty()) {
                            size_t end = line.find(',');
                            string part = line.substr(0, end);
                            size_t tokenStart = part.find_first_not_of(" \t\n\r");
                            size_t tokenEnd = part.find_last_not_of(" \t\n\r");

                            if (tokenStart != std::string::npos && tokenEnd != std::string::npos)
                                dependItems.push_back(part.substr(tokenStart, tokenEnd - tokenStart + 1));
                            line.erase(0, end == string::npos ? end : end + 1);
                        }
                        continue;
                    } else if (line.starts_with("depend: ")) {
                        inDepend = true;
                        continue;
                    }

                    if (inDepend && line.find('-') != std::string::npos) {
                        dependItems.emplace_back(line.substr(line.find('-') + 2));
                    } else if (inDepend) {
                        inDepend = false;
                    }
                }
                if (id.empty() || version.empty()) continue;
                module_entry modEntry;
                modEntry.model = MODEL_PLUGIN;
                modEntry.id = id;
                transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
                modEntry.version = version;
                for (auto &item: dependItems) {
                    transform(item.begin(), item.end(), item.begin(), ::tolower);
                    modEntry.dependency.emplace(item, pair{"", module_entry::approx});
                }
                result.emplace_back(modEntry);
            } else {
                StringNode info;
                parseJson(info, data);
                if (forgeEntry == stat.name) {
                    if (info("modListVersion"))
                        info = info["modList"];
                    for (auto &[index, mod]: info) {
                        if (!mod("modid") || !mod("version")) continue;
                        module_entry modEntry;
                        modEntry.model = MODEL_FORGE;
                        modEntry.id = mod["modid"].value;
                        transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
                        modEntry.version = mod["version"].value;
                        if (mod("dependencies")) {
                            for (auto &[ignored, depRoot]: mod["dependencies"]) {
                                string dep = depRoot.value, rem;
                                if (dep.empty()) continue;
                                restart:
                                uint64_t versionIndex = dep.find('@');
                                uint64_t commaIndex = dep.find(',');
                                if (commaIndex != string::npos) {
                                    while (count(dep.begin(), dep.begin() + (long) commaIndex, '[') > count(dep.begin(), dep.begin() + (long) commaIndex, ']')) {
                                        commaIndex = dep.find(',', commaIndex + 1);
                                        if (commaIndex == string::npos) break;
                                    }
                                    if (commaIndex != string::npos) {
                                        rem = dep.substr(commaIndex + 1);
                                        dep = dep.substr(0, commaIndex);
                                    }
                                }
                                string id;
                                module_entry::match versionType = module_entry::approx;
                                string version{};
                                if (versionIndex == string::npos) {
                                    id = dep;
                                } else {
                                    id = dep.substr(0, versionIndex);
                                    version = dep.substr(versionIndex+1);
                                    if (version.ends_with("*")) {
                                        versionType = module_entry::approx;
                                        version = version.substr(0, max(version.size() - 2, 0UL));
                                        if (version.starts_with('[')) version = version.substr(1, version.size() - 2);
                                    } else {
                                        if (version.starts_with('[')) version = version.substr(1, version.size() - 2);
                                        if (version.contains(',')) {
                                            versionType = module_entry::greater_exact;
                                            version = version.substr(0, version.find(','));
                                        } else {
                                            versionType = module_entry::exact;
                                        }
                                    }
                                }
                                transform(id.begin(), id.end(), id.begin(), ::tolower);
                                modEntry.dependency.emplace(id, pair{version, versionType});
                                if (commaIndex != string::npos) {
                                    dep = rem;
                                    goto restart;
                                }
                            }
                        }
                        result.emplace_back(modEntry);
                    }
                } else if (fabricEntry == stat.name) {
                    if (!info("id") || !info("version")) continue;
                    module_entry modEntry;
                    modEntry.model = MODEL_FABRIC;
                    modEntry.id = info["id"].value;
                    transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
                    modEntry.version = info["version"].value;
                    if (info("depends")) {
                        for (auto &[dep, versionRoot]: info["depends"]) {
                            string id = dep;
                            string version = versionRoot.value;
                            transform(id.begin(), id.end(), id.begin(), ::tolower);
                            const regex versionRegex(R"((<=|<|>=|>|=|\^)?\s*([\d\*]+(?:\.[^.*x\n]+)*)(\.[\*x])?)");
                            smatch match;

                            module_entry::match versionType = module_entry::approx;
                            if (regex_match(version, match, versionRegex)) {
                                string logicSymbol = match[1].str();
                                string realVersion = match[2].str();

                                if (realVersion == "*" || realVersion.empty()) {
                                    version = "";
                                    versionType = module_entry::approx;
                                } else if (!match[3].str().empty()) {
                                    version = realVersion;
                                    versionType = module_entry::approx;
                                } else if (logicSymbol == "^") {
                                    version = realVersion;
                                    versionType = module_entry::not_exact;
                                } else if (logicSymbol == "<") {
                                    version = realVersion;
                                    versionType = module_entry::less;
                                } else if (logicSymbol == "<=") {
                                    version = realVersion;
                                    versionType = module_entry::less_exact;
                                } else if (logicSymbol == ">") {
                                    version = realVersion;
                                    versionType = module_entry::greater;
                                } else if (logicSymbol == ">=") {
                                    version = realVersion;
                                    versionType = module_entry::greater_exact;
                                } else {
                                    version = realVersion;
                                    versionType = module_entry::exact;
                                }
                            } else {
                                version = "";
                            }
                            modEntry.dependency.emplace(id, pair{version, versionType});
                        }
                    }
                    result.emplace_back(modEntry);
                }
            }
            zip_fclose(subFile);
            break;
        }
    }
    zip_discard(jar);
    return result;
}
/**Get related modules from root
 *
 * @param root root node
 * @param filename file to look for
 * @return list of found modules
 */
vector<module_entry> lookRoot(StringNode &root, string &filename) {
    vector<module_entry> result;
    for (auto &[id, info]: root) {
        for (auto &[versionId, fileInfo]: info) {
            if (fileInfo.value == filename) {
                module_entry entry;
                entry.id = id;
                entry.version = versionId;
                entry.model = stringToCode(fileInfo["MODEL"].value, 2, MODEL_FORGE);
                result.emplace_back(entry);
            }
        }
    }
    return result;
}

bool shouldVerify(StringNode &root, module_entry &entry) {
    if (!root(entry.id)(entry.version)("STATUS")) return true;
    return root[entry.id][entry.version]["STATUS"].value != "F-VALID";
}
bool versionMatch(const string &source, const string &version, module_entry::match condition) {
    if (condition == module_entry::exact) return source == version;
    if (condition == module_entry::approx) return source.starts_with(version);
    if (condition == module_entry::not_exact) return source != version;
    strong_ordering diff = strong_ordering::equal;
    vector<string> version1;
    vector<string> version2;

    size_t start = 0, end;
    version1.push_back(source.substr(0, source.find('.')));
    while ((end = source.find('.', start)) != std::string::npos) {
        version1.push_back(source.substr(start, end - start));
        start = end + 1;
    }
    start = 0;
    version2.push_back(version.substr(0, source.find('.')));
    while ((end = version.find('.', start)) != std::string::npos) {
        version2.push_back(version.substr(start, end - start));
        start = end + 1;
    }


    for (size_t i = 0; i < version1.size() && i < version2.size(); ++i) {
        try {
            diff = stoi(version1[i]) <=> stoi(version2[i]);
            if (diff != strong_ordering::equal) break;
        } catch (invalid_argument&) {
            diff = version1[i] <=> version2[i];
            if (diff != strong_ordering::equal) break;
        }
    }
    if (diff == strong_ordering::equal) {
        diff = version1.size() <=> version2.size();
    }
    if (condition == module_entry::less) return diff == strong_ordering::less;
    if (condition == module_entry::less_exact) return diff != strong_ordering::greater;
    if (condition == module_entry::greater) return diff == strong_ordering::greater;
    if (condition == module_entry::greater_exact) return diff != strong_ordering::less;
    return false;
}
string findVersion(StringNode &in, const string &version, module_entry::match condition) {
    string bestVersion;
    bool detected = false;
    for (auto &[lVersion, entry]: in) {
        if (versionMatch(lVersion, version, condition)) {
            if (!detected) {
                detected = true;
                bestVersion = lVersion;
            } else if (versionMatch(bestVersion, lVersion, module_entry::greater)) bestVersion = lVersion;
        }
    }
    return bestVersion;
}
string combine(const string& id, const string& version) {
    if (version.empty()) return '"' + id + '"';
    return '"' + id + '(' + version + ")\"";
}

//TODO remove those from here
pid_t forkToServer(pair<const string, StringNode &> serverEntry, StringNode &root, int input, int output, int error) {
    pid_t child_pid = fork();
    if (child_pid == 0) {
        const string &server = serverEntry.first;
        StringNode &rootServer = serverEntry.second;
        StringNode &core = root["VERSIONS"][rootServer["VERSION"]]["CORES"][rootServer["CORE"]];
        dup2(input, STDIN_FILENO);
        dup2(output, STDOUT_FILENO);
        dup2(error, STDERR_FILENO);
        const fs::path pathTo = static_cast<fs::path>(root["GENERAL"]["ROOT-DIR"].value) / ("server/" + server + "/");
        current_path(pathTo);
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
int importOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 4) exitWithUsage();
    StringNode root;
    StringNode cache;
    parseConfig(root, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    if (!checkHash(root, cache)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" already exists");
    if (!root("VERSIONS")(arguments[2])) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
    root["SERVERS"][arguments[1]]["VERSION"].value = arguments[2];
    root["SERVERS"][arguments[1]]["CORE"];
    StringNode &rootVersion = root["VERSIONS"][arguments[2]];
    StringNode &rootModules = rootVersion["ENTRIES"];
    fs::path dirServer = arguments[3];
    fs::path dirVersion = fs::path(root["GENERAL"]["ROOT-DIR"].value) / "version" / arguments[2];
    fs::path dirMods = dirServer / "mods";
    fs::path dirPlugins = dirServer / "plugins";
    fs::path dirConfigs = dirServer / "config";
    fs::path dirVersionEntries = ensureExists(dirVersion, "entries");
    if (!is_directory(dirServer)) printError("Path \"" + arguments[2] + "\" doesn't point to server directory");
    string key, prefix = arguments[1] + "_mods";
    if (exists(dirMods) && !fs::is_empty(dirMods)) {
        for (int counter = 1; rootVersion["PACKS"](key = (prefix + to_string(counter))); counter++) {}
        root["SERVERS"][arguments[1]]["PACKS"][key];
        StringNode &rootPack = rootVersion["PACKS"][key];
        for (const auto &entry : fs::directory_iterator(dirMods)) {
            string fileName = relative(entry.path(), dirMods);
            if (!entry.is_regular_file() && !entry.is_symlink() || entry.path().extension() != ".jar") {
                cout << "Found invalid imported mod file \"" << fileName << "\"\n";
            } else {
                vector<module_entry> modules = lookFile(entry);
                if (modules.empty()) {
                    vector<module_entry> existing = lookRoot(rootModules, fileName);
                    if (existing.empty()) {
                        string key1, prefix1 = ".unknown";
                        for (int counter = 1; rootModules(key1 = (prefix1 + to_string(counter))); counter++) {}
                        rootModules[key1]["default"].value = fileName;
                        rootModules[key1]["default"]["MODEL"].value = codeToString(MODEL_FORGE, 2);
                        rootModules[key1]["default"]["STANDALONE"];
                        rootPack["ENTRIES"][key1].value = "Adefault";
                        fs::copy(entry, dirVersionEntries);
                    } else {
                        for (auto &module: existing) {
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                } else {
                    for (auto &module: modules) {
                        if (bool edit = shouldVerify(rootModules, module); edit) {
                            StringNode &rootModule = rootModules[module.id][module.version];
                            rootModule.value = fileName;
                            rootModule["MODEL"].value = codeToString(module.model, 2);
                            rootModule["STANDALONE"];
                            for (const auto &[depId, depVersion]: module.dependency) {
                                if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
                                string realDepVersion = ((char)depVersion.second) + depVersion.first;
                                rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
                            }
                            for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
                                if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
                                    it = rootModule["DEPENDENCIES"].children.erase(it);
                                } else ++it;
                            }
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                    fs::copy(entry, dirVersionEntries);
                }
            }
        }
    }
    if (exists(dirPlugins) && !fs::is_empty(dirPlugins)) {
        prefix = arguments[1] + "_plugins";
        for (int counter = 1; rootVersion["PACKS"](key = (prefix + to_string(counter))); counter++) {}
        root["SERVERS"][arguments[1]]["PACKS"][key];
        StringNode &rootPack = rootVersion["PACKS"][key];
        for (const auto &entry : fs::directory_iterator(dirPlugins)) {
            string fileName = relative(entry.path(), dirPlugins);
            if (!entry.is_regular_file() && !entry.is_symlink()  || entry.path().extension() != ".jar") {
                cout << "Found invalid imported plugin file \"" << fileName << "\"\n";
            } else {
                vector<module_entry> modules = lookFile(entry);
                if (modules.empty()) {
                    vector<module_entry> existing = lookRoot(rootModules, fileName);
                    if (existing.empty()) {
                        string key1, prefix1 = ".unknown";
                        for (int counter = 1; rootModules(key1 = (prefix1 + to_string(counter))); counter++) {}
                        rootModules[key1]["default"].value = fileName;
                        rootModules[key1]["default"]["MODEL"].value = codeToString(MODEL_PLUGIN, 2);
                        rootModules[key1]["default"]["STANDALONE"];
                        rootPack["ENTRIES"][key1].value = "Adefault";
                    } else {
                        for (auto &module: existing) {
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                } else {
                    for (auto &module: modules) {
                        if (bool edit = shouldVerify(rootModules, module); edit) {
                            StringNode &rootModule = rootModules[module.id][module.version];
                            rootModule.value = fileName;
                            rootModule["MODEL"].value = codeToString(module.model, 2);
                            rootModule["STANDALONE"];
                            for (const auto &[depId, depVersion]: module.dependency) {
                                if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
                                string realDepVersion = ((char)depVersion.second) + depVersion.first;
                                rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
                            }
                            for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
                                if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
                                    it = rootModule["DEPENDENCIES"].children.erase(it);
                                } else ++it;
                            }
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                }
            }
        }
    }
    if (exists(dirConfigs) && !fs::is_empty(dirConfigs)) {
        prefix = arguments[1] + "_configs";
        for (int counter = 1; rootVersion["META-CONFIGS"](key = (prefix + to_string(counter))); counter++) {}
        fs::path dirVersionConfigs = ensureExists(dirVersion / "config", key);
        root["SERVERS"][arguments[1]]["META-CONFIG"].value = key;
        for (const auto& config : fs::recursive_directory_iterator(dirConfigs)) {
            string fileName = relative(config.path(), dirConfigs);
            if (config.is_regular_file()) {
                fs::copy(config, dirVersionConfigs);
                rootVersion["META-CONFIGS"][key][fileName].value = "VALID";
            } else if (!config.is_directory()) {
                cout << "Found invalid imported config file \"" << fileName << "\"\n";
            }
        }
    }

    cout << "Please, run \"mserman verify\" to finish creation\nNote that you will need to setup core by yourself\n";
    flushConfig(root, "mserman.conf");
    return 0;
}

set<pair<string, string>> collectPack(StringNode &pack, StringNode &root, int side) {
    set<pair<string, string>> entries;
    set<pair<string, string>> checked;
    function<void(const string&, const string&)> dfs = [&entries, &dfs, &checked, &root, &side](const string &module, const string &version) {
        StringNode &rootMod = root["ENTRIES"][module];
        string depVersion = version;
        module_entry::match condition = depVersion.empty() ? module_entry::approx : (module_entry::match) depVersion[0];
        depVersion = depVersion.empty() ? "" : depVersion.substr(1);
        depVersion = findVersion(rootMod, depVersion, condition);
        if (!checked.insert({module, depVersion}).second) return;
        if (rootMod[depVersion]("SYNTHETIC")) return;
        int modSide = stringToCode(rootMod[depVersion]["SIDE"].value, 2, SIDE_BOTH);
        if (modSide & side) {
            entries.emplace(module, depVersion);
            for (auto &item: rootMod[depVersion]["DEPENDENCIES"]) {
                dfs(item.first, item.second.value);
            }
        }
    };
    for (auto &entry: pack["ENTRIES"]) {
        dfs(entry.first, entry.second.value);
    }
    for (auto &family: pack["FAMILIES"]) {
        const string &mode = family.second.value;
        for (auto &[mod, modVersion]: root["FAMILIES"][family.first]) {
            if (modVersion(mode))
                dfs(mod, modVersion.value);
        }
    }
    return entries;
}

/**Verification operation
 *
 * @param options options such as "--minecraft", "--allow-unsafe"
 * @param arguments currently unused
 * @return nothing
 */
int verifyOp(set<string> &options, vector<string> &arguments) {
    StringNode root, cache;
    StatNode stats;
    parseConfig(root, "mserman.conf");
    parseConfig(cache, "cache.conf", false);
    bool doMinecraft = options.contains("minecraft");
    cache[".COMMENT1"].value = "GENERATED BY PROGRAM. VERIFICATION DATA";
    cache[".COMMENT2"].value = "EDIT AT YOUR OWN RISK!! Changing this data may cause DATA LOSS,";
    cache[".COMMENT3"].value = "or INSTABILITY in program behaviour.";
    cache[".COMMENT4"].value = "Data here is NOT verified before usage!";
    cache[".COMMENT5"].value = "However, deleting this file IS SAFE";
    fs::path dirRoot = root["GENERAL"]["ROOT-DIR"].value;
    fs::path dirMinecraft = root["GENERAL"]["MINECRAFT"].value;
    fs::path dirBackups = root["GENERAL"]["BACKUP-DIR"].value;
    fs::path dirVersions = ensureExists(dirRoot, "version");
    fs::path dirServers = ensureExists(dirRoot, "server");
    {
        for (const auto &version: fs::directory_iterator(dirVersions)) {
            if (!version.is_directory()) cout << "Found non-version object " << version.path().filename() << " in versions folder\n";
            else if (!root("VERSIONS")(version.path().filename())) cout << "Found unknown version " << version.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["VERSIONS"][version.path().filename()]["SELF"]++;
        }
        for (auto &[version, rootVersion]: root["VERSIONS"]) {
            cout << "Processing version \"" << version << "\"\n";
            const vector<string> versionType = {"VERSIONS", version};
            StringNode &rootModules = rootVersion["ENTRIES"];
            StringNode &rootFamilies = rootVersion["FAMILIES"];
            StringNode &rootPacks = rootVersion["PACKS"];
            StringNode &rootMinecraft = rootVersion["MINECRAFT"];
            fs::path dirVersion = dirVersions / version;
            fs::path dirEntries = ensureExists(dirVersion, "entries");
            fs::path dirConfigs = ensureExists(dirVersion, "config");
            fs::path dirCores = ensureExists(dirVersion, "core");
            {
                for (const auto &entry : fs::directory_iterator(dirEntries)) {
                    string fileName = relative(entry.path(), dirEntries);
                    if (!entry.is_regular_file() || entry.path().extension() != ".jar") {
                        cout << "Found invalid file \"" << fileName << "\"\n";
                        stats["UNKNOWN"][versionType]["ENTRIES"][fileName]++;
                    } else {
                        vector<module_entry> modules = lookFile(entry);
                        if (modules.empty()) {
                            vector<module_entry> existing = lookRoot(rootModules, fileName);
                            if (existing.empty()) {
                                string key, prefix = ".unknown";
                                for (int counter = 1; rootModules(key = (prefix + to_string(counter))); counter++) {}
                                rootModules[key]["default"].value = fileName;
                                rootModules[key]["default"]["MODEL"].value = codeToString(MODEL_FORGE, 2);
                                rootModules[key]["default"]["STANDALONE"];
                            }
                        } else {
                            for (auto &module: modules) {
                                if (bool edit = shouldVerify(rootModules, module); edit) {
                                    StringNode &rootModule = rootModules[module.id][module.version];
                                    rootModule.value = fileName;
                                    rootModule["MODEL"].value = codeToString(module.model, 2);
                                    rootModule["STANDALONE"];
                                    for (const auto &[depId, depVersion]: module.dependency) {
                                        if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
                                        string realDepVersion = ((char)depVersion.second) + depVersion.first;
                                        rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
                                    }
                                    for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
                                        if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
                                            it = rootModule["DEPENDENCIES"].children.erase(it);
                                        } else ++it;
                                    }
                                }
                            }
                        }
                    }
                }
                set<string> processed;
                set<string> removed;
                function<void(const string&, StringNode&)> dfs = [&removed, &processed, &dfs, &stats, &versionType, &dirEntries, &rootModules](const string& module, StringNode &rootEntry) {
                    if (!processed.insert(module).second) return;
                    set<string> removedVersion;
                    for (auto &[moduleVersion, rootModule]: rootEntry) {
                        if (rootModule("SYNTHETIC")) {
                            cout << "Ignoring SYNTHETIC " << combine(module, moduleVersion) << '\n';
                            stats["VALID"][versionType]["ENTRIES"][module][moduleVersion]++;
                            continue;
                        }
                        string &status = stringToUpper(rootModule["STATUS"].value);
                        fs::path filePath = dirEntries / rootModule.value;
                        if (is_regular_file(filePath) && filePath.extension() == ".jar") {
                            int model = stringToCode(rootModule["MODEL"].value, 2, MODEL_FORGE);
                            rootModule["MODEL"].value = codeToString(model, 2);
                            int codeSide = stringToCode(rootModule["SIDE"].value, 2, SIDE_BOTH);
                            if (model == MODEL_PLUGIN) codeSide = SIDE_SERVER;
                            rootModule["SIDE"].value = codeToString(codeSide, 2);
                            rootModule["DEPENDENCIES"];
                            if (status != "VALID" && status != "F-VALID") {
                                cout << "Need to configure " << combine(module, moduleVersion) << "\n";
                                stats["NEW"][versionType][module][moduleVersion]++;
                                status = "NEW";
                                continue;
                            }
                            bool fine = true;
                            for (auto &[dep, rootDependency] : rootModule["DEPENDENCIES"]) {
                                if (rootModules(dep)) {
                                    dfs(dep, rootModules[dep]);
                                    string depVersion = rootDependency.value;
                                    module_entry::match condition = depVersion.empty() ? module_entry::approx : (module_entry::match) depVersion[0];
                                    depVersion = depVersion.empty() ? "" : depVersion.substr(1);
                                    depVersion = findVersion(rootModules[dep], depVersion, condition);
                                    if (!depVersion.empty() && stats("VALID")(versionType)("ENTRIES")(dep)(depVersion)) {
                                        if (rootModules[dep][depVersion]("SYNTHETIC")) continue;
                                        int depModel = stringToCode(rootModules[dep][depVersion]["MODEL"].value, 2,MODEL_FORGE);
                                        if ((depModel & model) != model) {
                                            cout << "Dependency for " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is using wrong model\n";
                                            stats["LOST"][versionType]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["MODEL"]++;
                                            fine = false;
                                        }
                                        int depSide = stringToCode(rootModules[dep][depVersion]["SIDE"].value, 2,SIDE_BOTH);
                                        if ((depSide & codeSide) != codeSide) {
                                            cout << "Dependency for  " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is on the wrong side\n";
                                            stats["LOST"][versionType]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["SIDE"]++;
                                            fine = false;
                                        }
                                    } else {
                                        cout << "Failed to resolve dependency for " << combine(module, moduleVersion)<< ": " << combine(dep, depVersion.empty() ? rootDependency.value : depVersion)<< "\n";
                                        stats["LOST"][versionType]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]++;
                                        fine = false;
                                    }
                                } else {
                                    cout << "Failed to find dependency for " << combine(module, moduleVersion) << ":  " << combine(dep, rootDependency.value) << "\n";
                                    stats["LOST"][versionType]["ENTRIES"][dep][rootDependency.value]["BY-ENTRY"][module][moduleVersion]++;
                                    fine = false;
                                }
                            }
                            if (fine) stats["VALID"][versionType]["ENTRIES"][module][moduleVersion]++;
                            continue;
                        } else if (!exists(filePath) || rootModule.value.empty()) {
                            if (status == "NOT-VALID" || rootModule.value.empty()) {
                                removedVersion.insert(moduleVersion);
                                continue;
                            } else if (status != "GONE") cout << "Failed to find " << combine(module, moduleVersion) << " with file \"" << rootModule.value << "\"\n";
                            else cout << "Need to remove entry for gone " << combine(module, moduleVersion) << "\n";
                            status = "GONE";
                        } else {
                            cout << "Found invalid " << combine(module, moduleVersion) << " with file \"" << rootModule.value << "\"\n";
                            status = "NOT-VALID";
                        }
                        stats["UNKNOWN"][versionType]["ENTRIES"][module][moduleVersion]++;
                    }
                    for (const auto &moduleVersion: removedVersion) rootEntry.children.erase(moduleVersion);
                    if (rootEntry.children.empty()) removed.insert(module);
                };
                for (auto &[module, rootModule]: rootModules) dfs(module, rootModule);
                for (const auto &module: removed) rootModules.children.erase(module);
                for (auto &[family, rootFamily]: rootFamilies) {
                    cout << "Processing family \"" << family << "\"\n";
                    bool fine = true;
                    unordered_map<int, vector<pair<string, string>>> modelInfo;
                    for (auto &[module, rootModule]: rootFamily) {
                        string moduleVersion = rootModule.value;
                        module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
                        moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
                        moduleVersion = findVersion(rootModules[module], moduleVersion, condition);
                        if (!stats("VALID")(versionType)("ENTRIES")(module)(moduleVersion)) {
                            cout << "Failed to resolve entry " << combine(module, moduleVersion) << '\n';
                            stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
                            fine = false;
                        } else if(!rootModules[module][moduleVersion]("STANDALONE")) {
                            cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
                            stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
                            fine = false;
                        } else {
                            auto &list = modelInfo[stringToCode(rootModules[module][moduleVersion]["MODEL"].value, 2, MODEL_FORGE)];
                            list.emplace_back(module, moduleVersion);
                        }
                    }
                    auto maxModel = std::max_element(
                            modelInfo.begin(), modelInfo.end(),
                            [](const auto& a, const auto& b) {
                                return a.second.size() < b.second.size();
                            }
                    );

                    if (maxModel != modelInfo.end()) {
                        for (const auto& [model, data] : modelInfo) {
                            if (model != maxModel->first) {
                                fine = false;
                                for (const auto& [module, moduleVersion] : data) {
                                    cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
                                    stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]["MODEL"]++;
                                }
                            }
                        }
                    }
                    cache[versionType]["FAMILIES"][family]["MODEL"].value = codeToString(maxModel->first, 2);
                    if (fine)
                        stats["VALID"][versionType]["FAMILIES"][family]++;
                }
                for (auto &[pack, rootPack]: rootPacks) {
                    cout << "Processing pack \"" << pack << "\"\n";
                    bool fine = true;
                    unordered_map<int, vector<pair<string, pair<bool, string>>>> modelInfo;
                    for (auto &[module, rootModule]: rootPack["ENTRIES"]) {
                        string moduleVersion = rootModule.value;
                        module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
                        moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
                        moduleVersion = findVersion(rootModules[module], moduleVersion, condition);
                        if (!stats("VALID")(versionType)("ENTRIES")(module)(moduleVersion)) {
                            cout << "Failed to resolve entry " << combine(module, moduleVersion) << "\n";
                            stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
                            fine = false;
                        } else if(!rootModules[module][moduleVersion]("STANDALONE")) {
                            cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
                            stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
                            fine = false;
                        } else {
                            auto &list = modelInfo[stringToCode(rootModules[module][moduleVersion]["MODEL"].value, 2, MODEL_FORGE)];
                            list.emplace_back(module, pair{false, moduleVersion});
                        }
                    }
                    for (auto &[family, rootFamily]: rootPack["FAMILIES"])
                        if (!stats("VALID")(versionType)("FAMILIES")(family)) {
                            cout << "Failed to resolve family \"" << family << "\"\n";
                            stats["LOST"][versionType]["FAMILIES"][family]["BY-PACK"][pack]++;
                            fine = false;
                        } else {
                            auto &list = modelInfo[stringToCode(cache[versionType]["FAMILIES"][family]["MODEL"].value, 2, MODEL_FORGE)];
                            list.emplace_back(family, pair{true, ""});
                        }
                    auto maxModel = std::max_element(
                            modelInfo.begin(), modelInfo.end(),
                            [](const auto& a, const auto& b) {
                                return a.second.size() < b.second.size();
                            }
                    );

                    if (maxModel != modelInfo.end()) {
                        for (const auto& [model, data] : modelInfo) {
                            if (model != maxModel->first) {
                                fine = false;
                                for (const auto& [module, moduleInfo] : data) {
                                    const auto& [moduleType, moduleVersion] = moduleInfo;
                                    if (!moduleType) {
                                        cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
                                        stats["LOST"][versionType]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]["MODEL"]++;
                                    } else  {
                                        cout << "Family \"" << module << "\" should have different model\n";
                                        stats["LOST"][versionType]["FAMILIES"][module]["BY-PACK"][pack]["MODEL"]++;
                                    }
                                }
                            }
                        }
                        cache[versionType]["PACKS"][pack]["MODEL"].value = codeToString(maxModel->first, 2);
                    } else {
                        cache[versionType]["PACKS"][pack]["MODEL"].value = codeToString(MODEL_FORGE, 2);
                    }
                    if (fine)
                        stats["VALID"][versionType]["PACKS"][pack]++;
                }
                cout.flush();
            }
            {
                for (const auto &metaConfig: fs::directory_iterator(dirConfigs)) {
                    if (!metaConfig.is_directory()) cout << "Found non-meta-config object " << metaConfig.path().filename() << " in meta-configs folder of \"" << version << "\"\n";
                    else if (!rootVersion("META-CONFIGS")(metaConfig.path().filename())) cout << "Found unknown meta-config " << metaConfig.path().filename() << " in folder of \"" << version << "\"\n";
                    else continue;
                    stats["UNKNOWN"][versionType]["META-CONFIGS"][metaConfig.path().filename()]++;
                }
                for (auto &[metaConfig, rootConfigs]: rootVersion["META-CONFIGS"]) {
                    cout << "Processing meta-config \"" << metaConfig << "\"\n";
                    const vector<string> metaType = {"META-CONFIGS", metaConfig};
                    fs::path dirConfig = ensureExists(dirConfigs, metaConfig);
                    for (const auto& config : fs::recursive_directory_iterator(dirConfig)) {
                        string fileName = relative(config.path(), dirConfig);
                        if (config.is_regular_file()) {
                            if (!rootConfigs(fileName))
                                rootConfigs[fileName];
                        } else if (!config.is_directory()) {
                            cout << "Found invalid config file '" << fileName << "'\n";
                            stats["UNKNOWN"][versionType][metaType][fileName]++;
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
                                fine = false;
                                status = "NEW";
                                stats["NEW"][versionType][metaType][config.first]++;
                            }
                            continue;
                        } else if (!exists(filePath)) {
                            if (status == "NOT-VALID") {
                                removed.insert(config.first);
                                fine = false;
                                continue;
                            } else if (status != "GONE") cout << "Failed to find config file '" << config.first << "'\n";
                            else cout << "Need to clean entry for gone config '" << config.first << "'\n";
                            status = "GONE";
                            fine = false;
                        } else {
                            cout << "Found invalid config file '" << config.first << "'\n";
                            status = "NOT-VALID";
                            fine = false;
                        }
                        stats["UNKNOWN"][versionType][metaType][config.first]++;
                    }
                    if (fine) stats["VALID"][versionType][metaType]++;
                    for (const auto &entry: removed) rootConfigs.children.erase(entry);
                }
                cout.flush();
            }
            {
                for (const auto &core: fs::directory_iterator(dirCores)) {
                    if (!core.is_directory()) cout << "Found non-core object " << core.path().filename() << " in cores folder of \"" << version << "\"\n";
                    else if (!rootVersion("CORES")(core.path().filename())) cout << "Found unknown core " << core.path().filename() << " in folder of \"" << version << "\"\n";
                    else continue;
                    stats["UNKNOWN"][versionType]["CORES"][core.path().filename()]++;
                }
                for (auto &[core, rootCore]: rootVersion["CORES"]) {
                    cout << "Processing core \"" << core << "\"\n";
                    const vector<string> coreType = {"CORES", core};
                    fs::path dir = ensureExists(dirCores, core);
                    string &status = stringToUpper(rootCore["STATUS"].value);
                    int codeSupport = stringToCode(rootCore["SUPPORT"].value, 3, 0b000);
                    rootCore["SUPPORT"].value = codeToString(codeSupport, 3);
                    bool validTests = (bool) rootCore("USE-FOR-SIMULATION");
                    const string coreRuntime = rootCore["JAVA-RUNTIME"].value;
                    const string coreMain = rootCore["MAIN"].value;
                    if (status != "NOT-VALID" && status != "VALID") {
                        cout << "Need to configure core \"" << core << "\"\n";
                        stats["NEW"][versionType][coreType]++;
                        status = "NEW";
                        continue;
                    }
                    bool fine = true;
                    if (!is_regular_file(dir / coreMain)) {
                        cout << "Main file for core \"" << core << "\" not found\n";
                        stats["LOST"][versionType][coreType]["MAIN"]++;
                        fine = false;
                    }
                    if (!is_regular_file(((fs::path)coreRuntime) / "java")) {
                        cout << "Runtime for core \"" << core << "\" not found\n";
                        stats["LOST"][versionType][coreType]["RUNTIME"]++;
                        fine = false;
                    } else {
                        cout << "Checking Java runtime \"" << coreRuntime << "\" for core \"" << core << "\"" << endl;
                        string command = (((fs::path)coreRuntime) / "java").string() + " -version";
                        bool bufferFine = !system(command.c_str());
                        cout.flush();
                        cout << (!bufferFine ? "------------------------\nNot a valid Java runtime\n------------------------" : "-------------\nRuntime valid\n-------------") << endl;
                        fine = fine && bufferFine;
                    }
                    status = fine ? "VALID" : "NOT-VALID";
                    if (fine) stats["VALID"][versionType][coreType]++;
                }
                cout.flush();
            }
            {
               if (doMinecraft && root["GENERAL"]["MINECRAFT"]["VERSION"].value == version) {
                   if (!is_directory(dirMinecraft)) {
                       cout << "Invalid minecraft directory " << dirMinecraft << '\n';
                   } else {
                       cout << "Processing minecraft directory " << dirMinecraft << '\n';
                       string &oldVersion = cache["OLD-MINECRAFT"].value;
                       if (cache["OLD-MINECRAFT"].value != root["GENERAL"]["MINECRAFT"]["VERSION"].value) {
                           if (!root["VERSIONS"](oldVersion)) {
                               StringNode &oldRoot = root["VERSIONS"][oldVersion];
                               fs::path dirBackedUp = ensureExists(dirBackups / "version", oldVersion);
                               for (auto &[entry, ignored]: oldRoot["MINECRAFT"]["SWAP-WHITELIST"]) {
                                   if (entry == "mods" || entry == "config" || entry == "versions") {
                                       cout << "Please avoid using directories such as \"mods\", \"config\", \"versions\" in your swap whitelist\n";
                                       continue;
                                   }
                                   fs::path absoluteItem = dirBackedUp / entry;
                                   fs::path absoluteLink = dirMinecraft / entry;
                                   try {
                                       if (is_symlink(absoluteLink)) {
                                           remove(absoluteLink);
                                       } else if (exists(absoluteLink)) {
                                           rename(absoluteLink, absoluteItem);
                                       }
                                   } catch (const fs::filesystem_error &ex) {
                                       if (is_symlink(absoluteLink)) {
                                           remove(absoluteLink);
                                       }
                                   }
                               }
                           } else {
                               fs::path dirBackedUp = ensureExists(dirBackups / "version", version);
                               for (auto &[entry, ignored]: rootMinecraft["SWAP-WHITELIST"]) {
                                   if (entry == "mods" || entry == "config" || entry == "versions") {
                                       cout << "Please avoid using directories such as \"mods\", \"config\", \"versions\" in your swap whitelist\n";
                                       continue;
                                   }
                                   fs::path absoluteItem = dirBackedUp / entry;
                                   fs::path absoluteLink = dirMinecraft / entry;
                                   try {
                                       if (is_symlink(absoluteLink)) {
                                           remove(absoluteLink);
                                       } else if (exists(absoluteLink)) {
                                           rename(absoluteLink, absoluteItem);
                                       }
                                   } catch (const fs::filesystem_error &ex) {
                                       if (is_symlink(absoluteLink)) {
                                           remove(absoluteLink);
                                       }
                                   }
                               }
                           }
                       }
                       {
                           fs::path dirBackedUp = ensureExists(dirBackups / "version", version);
                           for (auto &[entry, ignored]: rootMinecraft["SWAP-WHITELIST"]) {
                               if (entry == "mods" || entry == "config" || entry == "versions") {
                                   cout << "Please avoid using directories such as \"mods\", \"config\", \"versions\" in your swap whitelist\n";
                                   continue;
                               }
                               fs::path absoluteItem = dirBackedUp / entry;
                               fs::path absoluteLink = dirMinecraft / entry;
                               try {
                                   if (is_symlink(absoluteLink) && !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                       remove(absoluteLink);
                                       create_symlink(absolute(absoluteItem), absoluteLink);
                                       cout << "Symlink to object \"" << entry << "\" fixed\n";
                                   } else if (!exists(absoluteLink)) {
                                       create_symlink(absolute(absoluteItem), absoluteLink);
                                       cout << "Symlink to mod \"" << entry << "\" created\n";
                                   } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                       cout << "Failed to create symlink to mod \"" << entry
                                            << "\", location already taken\n";
                                   }
                               } catch (const fs::filesystem_error &ex) {
                                   if (is_symlink(absoluteLink)) {
                                       remove(absoluteLink);
                                       create_symlink(absolute(absoluteItem), absoluteLink);
                                       cout << "Symlink to mod \"" << entry << "\" fixed\n";
                                   }
                               }
                           }
                       }
                       for (auto &[pack, ignored]: rootMinecraft["PACKS"]) {
                           if (!stats("VALID")(versionType)("PACKS")(pack)) {
                               cout << "Pack \"" << pack << "\" not valid\n";
                               stats["LOST"][versionType]["PACKS"][pack]["BY-USER"]++;
                               continue;
                           }
                           int packModel = stringToCode(cache[versionType]["PACKS"][pack]["MODEL"].value, 2, MODEL_FORGE);
                           if (packModel == MODEL_FORGE || packModel == MODEL_FABRIC) {
                               fs::path dirMods = ensureExists(dirMinecraft, "mods");
                               for (set<pair<string, string>> mods = collectPack(rootVersion["PACKS"][pack], rootVersion,
                                                                                 SIDE_SERVER); auto &[mod, modVersion]: mods) {
                                   string modName = rootVersion["ENTRIES"][mod][modVersion].value;
                                   fs::path absoluteItem = dirEntries / modName;
                                   fs::path absoluteLink = dirMods / modName;
                                   try {
                                       if (is_symlink(absoluteLink) &&
                                           !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                           remove(absoluteLink);
                                           create_symlink(absolute(absoluteItem), absoluteLink);
                                           cout << "Symlink to mod " << combine(mod, modVersion) << " fixed\n";
                                       } else if (!exists(absoluteLink)) {
                                           create_symlink(absolute(absoluteItem), absoluteLink);
                                           cout << "Symlink to mod " << combine(mod, modVersion) << " created\n";
                                       } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                           cout << "Failed to create symlink to mod " << combine(mod, modVersion)
                                                << ", location already taken\n";
                                       }
                                   } catch (const fs::filesystem_error &ex) {
                                       if (is_symlink(absoluteLink)) {
                                           remove(absoluteLink);
                                           create_symlink(absolute(absoluteItem), absoluteLink);
                                           cout << "Symlink to mod " << combine(mod, modVersion) << " fixed\n";
                                       }
                                   }
                               }
                           } else if (packModel == MODEL_PLUGIN) {
                               cout << "Plugin packs (\"" << pack << "\") are not supported on client\n";
                           }
                       }
                       if (!stats("VALID")(versionType)("META-CONFIGS")(rootMinecraft["META-CONFIG"])) {
                           cout << "Meta-config \"" << rootMinecraft["META-CONFIG"].value << "\" not valid\n";
                           stats["LOST"][versionType]["META-CONFIGS"][rootMinecraft["META-CONFIG"]]["BY-USER"]++;
                       } else {
                           fs::path dirServerConfig = dirMinecraft / "config";
                           fs::path dirConfig = ensureExists(dirVersion / "config", rootMinecraft["META-CONFIG"].value);
                           try {
                               if (is_symlink(dirServerConfig) &&
                                   !(absolute(read_symlink(dirServerConfig)) == absolute(dirConfig))) {
                                   remove(dirServerConfig);
                                   create_symlink(absolute(dirConfig), dirServerConfig);
                                   cout << "Symlink to config folder fixed\n";
                               } else if (!exists(dirServerConfig)) {
                                   create_symlink(absolute(dirConfig), dirServerConfig);
                                   cout << "Symlink to config folder created\n";
                               } else if (exists(dirServerConfig) && !is_symlink(dirServerConfig)) {
                                   cout << "Failed to create symlink to config folder, location already taken\n";
                               }
                           } catch (const fs::filesystem_error &ex) {
                               if (is_symlink(dirServerConfig)) {
                                   remove(dirServerConfig);
                                   create_symlink(absolute(dirConfig), dirServerConfig);
                                   cout << "Symlink to config folder fixed\n";
                               }
                           }
                       }
                       oldVersion = version;
                   }
               }
            }
        }
    }
    {
        //TODO backup verification
        for (const auto &server: fs::directory_iterator(dirServers)) {
            if (!server.is_directory()) cout << "Found non-server object " << server.path().filename() << " in servers folder\n";
            else if (!root("SERVERS")(server.path().filename())) cout << "Found unknown server " << server.path().filename() << "\n";
            else continue;
            stats["UNKNOWN"]["SERVERS"][server.path().filename()]++;
        }
        for (auto &[server, rootServer]: root["SERVERS"]) {
            cout << "Processing server \"" << server << "\"\n";
            const vector<string> serverType = {"SERVERS", server};
            string &status = stringToUpper(rootServer["STATUS"].value);
            string &serverCore = rootServer["CORE"].value;
            string &serverVersion = rootServer["VERSION"].value;
            const vector<string> coreType = {"CORES", serverCore};
            const vector<string> versionType = {"VERSIONS", serverVersion};
            rootServer["JAVA-ARGS"];
            if (status != "NOT-VALID" && status != "VALID") {
                cout << "Need to configure server\n";
                stats["NEW"][serverType]++;
                status = "NEW";
                continue;
            }
            bool fine = true;
            if (!root(versionType)) {
                cout << "Version for server not found\n";
                stats["LOST"][versionType]["BY-SERVER"][server]++;
                status = "NOT-VALID";
                continue;
            }
            if (!stats("VALID")(versionType)(coreType)) {
                cout << "Core for server not valid\n";
                stats["LOST"][versionType][coreType]["BY-SERVER"][server]++;
                status = "NOT-VALID";
                continue;
            }
            StringNode &rootVersion = root[versionType];
            StringNode &rootCore = rootVersion[coreType];
            int support = stringToCode(rootCore["SUPPORT"].value, 3, 0b000);
            for (auto &[pack, ignored]: rootServer["PACKS"]) {
                if (!stats("VALID")(versionType)("PACKS")(pack)) {
                    cout << "Pack \"" << pack << "\" not valid\n";
                    stats["LOST"][versionType]["PACKS"][pack]["BY-SERVER"][server]++;
                    fine = false;
                } else {
                    int packModel = stringToCode(cache[versionType]["PACKS"][pack]["MODEL"].value, 2, MODEL_FORGE);
                    if (!((support >> packModel) & 1)) {
                        cout << "Pack \"" << pack << "\" should have different model\n";
                        stats["LOST"][versionType]["PACKS"][pack]["BY-SERVER"][server]["MODEL"]++;
                        fine = false;
                    }
                }
            }
            if (support & (SUPPORT_FORGE | SUPPORT_FABRIC)) {
                if (!stats("VALID")(versionType)("META-CONFIGS")(rootServer["META-CONFIG"])) {
                    cout << "Meta-config \"" << rootServer["META-CONFIG"].value << "\" not valid\n";
                    stats["LOST"][versionType]["META-CONFIGS"][rootServer["META-CONFIG"]]["BY-SERVER"][server]++;
                    fine = false;
                }
            }
            status = fine ? "VALID" : "NOT-VALID";
            if (fine) {
                cache[serverType]["VALID"];
                stats["VALID"][serverType]++;
                if (!doMinecraft) {
                    fs::path dirServer = ensureExists(dirServers, server);
                    fs::path dirVersion = ensureExists(dirVersions, serverVersion);
                    fs::path coreDir = ensureExists(dirVersion / "core", serverCore);
                    for (const auto &entry: fs::directory_iterator(coreDir)) {
                        fs::path relativeItem = relative(entry.path(), coreDir);
                        fs::path absoluteLink = dirServer / relativeItem;
                        try {
                            if (is_symlink(absoluteLink) &&
                                !(absolute(read_symlink(absoluteLink)) == absolute(entry.path()))) {
                                remove(absoluteLink);
                                create_symlink(absolute(entry.path()), absoluteLink);
                                cout << "Symlink fixed: " << relativeItem << '\n';
                            } else if (!exists(absoluteLink)) {
                                create_symlink(absolute(entry.path()), absoluteLink);
                                cout << "Symlink created: " << relativeItem << '\n';
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink, location already taken: " << relativeItem << '\n';
                            }
                        } catch (const fs::filesystem_error &) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                create_symlink(absolute(entry.path()), absoluteLink);
                                cout << "Symlink fixed: " << relativeItem << '\n';
                            }
                        }
                    }
                    for (auto &[pack, ignored]: rootServer["PACKS"]) {
                        const vector<string> packType = {"PACKS", pack};
                        int packModel = stringToCode(cache[versionType]["PACKS"][pack]["MODEL"].value, 2, MODEL_FORGE);
                        if (packModel == MODEL_FORGE || packModel == MODEL_FABRIC) {
                            fs::path dirServerMod = ensureExists(dirServer, "mods");
                            fs::path dirMod = ensureExists(dirVersion, "entries");
                            for (set<pair<string, string>> mods = collectPack(rootVersion["PACKS"][pack], rootVersion, SIDE_SERVER); auto &[mod, modVersion]: mods) {
                                string modName = rootVersion["ENTRIES"][mod][modVersion].value;
                                fs::path absoluteItem = dirMod / modName;
                                fs::path absoluteLink = dirServerMod / modName;
                                try {
                                    if (is_symlink(absoluteLink) &&
                                        !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                        remove(absoluteLink);
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to mod " << combine(mod, modVersion) << " fixed\n";
                                    } else if (!exists(absoluteLink)) {
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to mod " << combine(mod, modVersion) << " created\n";
                                    } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                        cout << "Failed to create symlink to mod " << combine(mod, modVersion) << ", location already taken\n";
                                    }
                                } catch (const fs::filesystem_error &ex) {
                                    if (is_symlink(absoluteLink)) {
                                        remove(absoluteLink);
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to mod " << combine(mod, modVersion) << " fixed\n";
                                    }
                                }
                            }
                        } else if (packModel == MODEL_PLUGIN) {
                            fs::path dirServerPlugin = ensureExists(dirServer, "plugins");
                            fs::path dirPlugins = ensureExists(dirVersion, "entries");
                            for (set<pair<string, string>> plugins = collectPack(rootVersion["PACKS"][pack], rootVersion, SIDE_SERVER); auto &[plugin, pluginVersion]: plugins) {
                                string pluginName = rootVersion["ENTRIES"][plugin][pluginVersion].value;
                                fs::path absoluteItem = dirPlugins / pluginName;
                                fs::path absoluteLink = dirServerPlugin / pluginName;
                                try {
                                    if (is_symlink(absoluteLink) &&
                                        !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                        remove(absoluteLink);
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to plugin " << combine(plugin, pluginVersion) << " fixed\n";
                                    } else if (!exists(absoluteLink)) {
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to plugin " << combine(plugin, pluginVersion) << " created\n";
                                    } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                        cout << "Failed to create symlink to plugin " << combine(plugin, pluginVersion) << ", location already taken\n";
                                    }
                                } catch (const fs::filesystem_error &ex) {
                                    if (is_symlink(absoluteLink)) {
                                        remove(absoluteLink);
                                        create_symlink(absolute(absoluteItem), absoluteLink);
                                        cout << "Symlink to plugin " << combine(plugin, pluginVersion) << " fixed\n";
                                    }
                                }
                            }
                        }
                    }
                    if (support & (SUPPORT_FABRIC | SUPPORT_FORGE)) {
                        fs::path dirServerConfig = dirServer /"config";
                        fs::path dirConfig = ensureExists(dirVersion / "config", rootServer["META-CONFIG"].value);
                        try {
                            if (is_symlink(dirServerConfig) &&
                                !(absolute(read_symlink(dirServerConfig)) == absolute(dirConfig))) {
                                remove(dirServerConfig);
                                create_symlink(absolute(dirConfig), dirServerConfig);
                                cout << "Symlink to config folder fixed\n";
                            } else if (!exists(dirServerConfig)) {
                                create_symlink(absolute(dirConfig), dirServerConfig);
                                cout << "Symlink to config folder created\n";
                            } else if (exists(dirServerConfig) && !is_symlink(dirServerConfig)) {
                                cout << "Failed to create symlink to config folder, location already taken\n";
                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(dirServerConfig)) {
                                remove(dirServerConfig);
                                create_symlink(absolute(dirConfig), dirServerConfig);
                                cout << "Symlink to config folder fixed\n";
                            }
                        }
                    }
                }
            }
        }
        cout.flush();
    }
    setHash(root, cache);
    flushConfig(root, "mserman.conf");
    flushConfig(cache, "cache.conf");
    auto stringStats = stats.convert<StringNode>(+([](const int &toMap) {return to_string(toMap);}));
    flushConfig(stringStats, "report.log");
    return 0;
}