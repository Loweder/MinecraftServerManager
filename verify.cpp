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
    zip_stat_t stat;
    for (int i = 0; i < num; ++i) {
        zip_stat_init(&stat);
        if (!zip_stat_index(jar, i, 0, &stat)) {
            if (forgeEntry != stat.name && fabricEntry != stat.name && bukkitEntry != stat.name) continue;
            zip_file_t *subFile = zip_fopen_index(jar, i, 0);
            if (!subFile) continue;
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
                string_node info;
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
 * @param mc_version version to check
 * @return list of found modules
 */
vector<module_entry> lookRoot(root_pack &root, const string &filename, const string &mc_version) {
    vector<module_entry> result;
    for (auto &[id, info]: root.root["ENTRIES"]) {
        if (!info(mc_version)) continue;
        for (auto &[versionId, fileInfo]: info[mc_version]) {
            if (fileInfo.value == filename) {
                module_entry entry;
                entry.id = id;
                entry.version = versionId;
                entry.model = fileInfo["MODEL"].asCode(2, MODEL_FORGE);
                result.emplace_back(entry);
            }
        }
    }
    return result;
}

bool shouldVerify(root_pack &root, module_entry &entry, const string &mc_version) {
    if (!root.root["ENTRIES"](entry.id)(mc_version)(entry.version)("STATUS")) return true;
    return root.root["ENTRIES"][entry.id][mc_version][entry.version]["STATUS"].value != "F-VALID";
}
bool versionMatch(const string &source, const string &version, module_entry::match condition) {
    if (condition == module_entry::exact) return source == version;
    if (condition == module_entry::approx) return source.starts_with(version);
    if (condition == module_entry::not_exact) return source != version;
    strong_ordering diff = strong_ordering::equal;
    vector<string> version1;
    vector<string> version2;

    size_t start = 0, end = source.find('.');
    do {
        version1.push_back(source.substr(start, end - start));
        start = end + 1;
    } while ((end = source.find('.', start)) != std::string::npos);
    version1.push_back(source.substr(start));
    start = 0, end = source.find('.');
    do {
        version2.push_back(version.substr(start, end - start));
        start = end + 1;
    } while ((end = version.find('.', start)) != std::string::npos);
    version2.push_back(version.substr(start));


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
string findVersion(string_node &in, const string &version, module_entry::match condition) {
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
pid_t forkToServer(root_pack &root, string &server, int input, int output, int error) {
    pid_t child_pid = fork();
    if (child_pid == 0) {
        string_node &rootServer = root.root["SERVERS"][server];
        string_node &core = root.root["CORES"][rootServer["CORE"]];
        dup2(input, STDIN_FILENO);
        dup2(output, STDOUT_FILENO);
        dup2(error, STDERR_FILENO);
        const fs::path pathTo = static_cast<fs::path>(root.root["GENERAL"]["ROOT-DIR"].value) / ("server/" + server + "/");
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
        if (!strArgs.empty())
            args.push_back(strArgs.data());
        args.push_back((char*)"-jar");
        args.push_back(core["MAIN"].value.data());
        args.push_back(nullptr);
        execvp((((fs::path)core["JAVA-RUNTIME"].value) / "java").c_str(), args.data());
    }
    return child_pid;
}
set<pair<string, string>> collectPack(root_pack &root, string_node &pack, const string &mc_version, int side) {
    set<pair<string, string>> entries;
    set<pair<string, string>> checked;
    function<void(const string&, const string&)> dfs = [&entries, &dfs, &checked, &root, &side, &mc_version](const string &module, const string &version) {
        if (!root.root("ENTRIES")(module)(mc_version)) return;
        string_node &rootMod = root.root["ENTRIES"][module][mc_version];
        string depVersion = version;
        module_entry::match condition = depVersion.empty() ? module_entry::approx : (module_entry::match) depVersion[0];
        depVersion = depVersion.empty() ? "" : depVersion.substr(1);
        depVersion = findVersion(rootMod, depVersion, condition);
        if (!checked.insert({module, depVersion}).second) return;
        if (rootMod[depVersion]("SYNTHETIC")) return;
        int modSide = rootMod[depVersion]["SIDE"].asCode(2, SIDE_BOTH);
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
        for (auto &[mod, modVersion]: root.root["FAMILIES"][family.first]) {
            if (modVersion(mode))
                dfs(mod, modVersion.value);
        }
    }
    return entries;
}
void linkPacks(root_pack &root, string_node &packs, const string &dest, int side) {
    map<string, set<pair<string, string>>> mods;
    map<string, set<pair<string, string>>> plugins;
    for (auto &[pack, mc_version]: packs) {
        int packModel = root.cache["PACKS"][pack][mc_version]["MODEL"].asCode(2, MODEL_FORGE);
        if (side == SIDE_CLIENT && packModel == MODEL_PLUGIN) {
            continue;
        }
        set<pair<string, string>> modules = collectPack(root, root.root["PACKS"][pack], mc_version.value, side);
        if (packModel == MODEL_PLUGIN) {
            plugins[mc_version.value].insert(modules.begin(), modules.end());
        } else {
            mods[mc_version.value].insert(modules.begin(), modules.end());
        }
    }
    auto processModules = [&root, &dest](const std::string& type, map<string, set<pair<string, string>>> &sModules) {
        if (sModules.empty()) return;
        path_node& dir = dest.empty() ? root.paths["MINECRAFT"][type + "s"] : root.paths["GENERAL"]["server"][dest][type + "s"];
        ensureExists(dir);
        for (auto& absoluteLink : fs::directory_iterator(dir.value)) {
            std::string fileName = absoluteLink.path().lexically_relative(dir.value);
            for (auto& [mc_version, modules] : sModules) {
                path_node& dirEntries = root.paths["GENERAL"]["entries"][mc_version];
                std::vector<module_entry> existent = lookRoot(root, fileName, mc_version);
                if (existent.empty()) continue;
                fs::path absoluteItem = absolute(dirEntries.value / fileName);
                for (auto& module : existent) {
                    if (modules.contains({module.id, module.version})) {
                        if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
                            remove(absoluteLink);
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink to " << type << " " << combine(module.id, module.version) << " fixed\n";
                        }
                        goto contOuter;
                    }
                }
            }
            if (is_symlink(absoluteLink)) {
                remove(absoluteLink);
                cout << "Symlink to " << type << " " << fileName << " removed\n";
            } else {
                cout << "Found unknown " << type << " " << fileName << '\n';
            }
            contOuter:
            asm("nop");
        }
        for (auto &[mc_version, modules] : sModules) {
            path_node &dirEntries = root.paths["GENERAL"]["entries"][mc_version];
            for (auto &[module, moduleVersion]: modules) {
                string modName = root.root["ENTRIES"][module][mc_version][moduleVersion].value;
                fs::path absoluteItem = absolute(dirEntries.value / modName);
                fs::path absoluteLink = dir.value / modName;
                if (!exists(symlink_status(absoluteLink))) {
                    create_symlink(absoluteItem, absoluteLink);
                    cout << "Symlink to " << type << " " << combine(module, moduleVersion) << " created\n";
                } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                    cout << "Failed to create symlink to " << type << " " << combine(module, moduleVersion)
                         << ", location already taken\n";
                }
            }
        }
    };
    processModules("mod", mods);
    processModules("plugin", plugins);
}

int importOp(set<string> &options, vector<string> &arguments) {
    if (arguments.size() != 4) exitWithUsage();
    root_pack root;
    string &server = arguments[1];
    string &mc_version = arguments[2];
    if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
    if (root.root("SERVERS")(server)) printError("Server with name \"" + arguments[1] + "\" already exists");
    if (!root.root("VERSIONS")(mc_version)) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
    root.paths["SERVER"] = path_node(arguments[3]);
    string_node &rootModules = root.root["ENTRIES"];
    string_node &rootServer = root.root["SERVERS"][server];
    if (!is_directory(root.paths["SERVER"].value)) printError("Path \"" + arguments[3] + "\" doesn't point to server directory");
    rootServer["CORE"];
    string key, prefix = server + "_mods";
    if (exists(root.paths["SERVER"]["mods"].value) && !fs::is_empty(root.paths["SERVER"]["mods"].value)) {
        for (int counter = 1; root.root["PACKS"](key = (prefix + to_string(counter))); counter++) {}
        rootServer["PACKS"][key].value = mc_version;
        string_node &rootPack = root.root["PACKS"][key];
        for (const auto &entry : fs::directory_iterator(root.paths["SERVER"]["mods"].value)) {
            string fileName = entry.path().lexically_relative(root.paths["SERVER"]["mods"].value);
            if (!entry.is_regular_file() && !entry.is_symlink() || entry.path().extension() != ".jar") {
                cout << "Found invalid imported mod file \"" << fileName << "\"\n";
            } else {
                vector<module_entry> modules = lookFile(entry);
                if (modules.empty()) {
                    vector<module_entry> existing = lookRoot(root, fileName, mc_version);
                    if (existing.empty()) {
                        string temp = fileName.substr(0, fileName.size() - 4);
                        string key1 = '.' + stringToLower(temp);
                        rootModules[key1][mc_version]["default"].value = fileName;
                        rootModules[key1][mc_version]["default"]["MODEL"].toCode(MODEL_FORGE, 2);
                        rootModules[key1][mc_version]["default"]["STANDALONE"];
                        rootPack["ENTRIES"][key1].value = "Adefault";
                    } else {
                        for (auto &module: existing) {
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                } else {
                    for (auto &module: modules) {
                        if (bool edit = shouldVerify(root, module, mc_version); edit) {
                            string_node &rootModule = rootModules[module.id][mc_version][module.version];
                            rootModule.value = fileName;
                            rootModule["MODEL"].toCode(module.model, 2);
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
                fs::copy(entry, root.paths["GENERAL"]["entries"][mc_version].value, fs::copy_options::update_existing);
            }
        }
        prefix = arguments[1] + "_configs";
        for (int counter = 1; root.root["META-CONFIGS"](key = (prefix + to_string(counter))); counter++) {}
        path_node dirSysConfigs = ensureExists(root.paths["GENERAL"]["config"][key]);
        root.root["META-CONFIGS"][key];
        rootServer["META-CONFIG"].value = key;
        if (exists(root.paths["SERVER"]["config"].value)) {
            for (const auto& config : fs::recursive_directory_iterator(root.paths["SERVER"]["config"].value)) {
                string fileName = config.path().lexically_relative(root.paths["SERVER"]["config"].value);
                if (config.is_regular_file()) {
                    fs::create_directories((dirSysConfigs.value / fileName).parent_path());
                    fs::copy(config, dirSysConfigs.value / fileName);
                    root.root["META-CONFIGS"][key][fileName].value = "VALID";
                } else if (!config.is_directory()) {
                    cout << "Found invalid imported config file \"" << fileName << "\"\n";
                }
            }
        }
    }
    if (exists(root.paths["SERVER"]["plugins"].value) && !fs::is_empty(root.paths["SERVER"]["plugins"].value)) {
        prefix = server + "_plugins";
        for (int counter = 1; root.root["PACKS"](key = (prefix + to_string(counter))); counter++) {}
        rootServer["PACKS"][key].value = mc_version;
        string_node &rootPack = root.root["PACKS"][key];
        for (const auto &entry : fs::directory_iterator(root.paths["SERVER"]["plugins"].value)) {
            string fileName = entry.path().lexically_relative(root.paths["SERVER"]["plugins"].value);
            if (!entry.is_regular_file() && !entry.is_symlink() || entry.path().extension() != ".jar") {
                cout << "Found invalid imported plugin file \"" << fileName << "\"\n";
            } else {
                vector<module_entry> modules = lookFile(entry);
                if (modules.empty()) {
                    vector<module_entry> existing = lookRoot(root, fileName, mc_version);
                    if (existing.empty()) {
                        string temp = fileName.substr(0, fileName.size() - 4);
                        string key1 = '.' + stringToLower(temp);
                        rootModules[key1][mc_version]["default"].value = fileName;
                        rootModules[key1][mc_version]["default"]["MODEL"].toCode(MODEL_PLUGIN, 2);
                        rootModules[key1][mc_version]["default"]["STANDALONE"];
                        rootPack["ENTRIES"][key1].value = "Adefault";
                    } else {
                        for (auto &module: existing) {
                            rootPack["ENTRIES"][module.id].value = "A" + module.version;
                        }
                    }
                } else {
                    for (auto &module: modules) {
                        if (bool edit = shouldVerify(root, module, mc_version); edit) {
                            string_node &rootModule = rootModules[module.id][mc_version][module.version];
                            rootModule.value = fileName;
                            rootModule["MODEL"].toCode(module.model, 2);
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
                fs::copy(entry, root.paths["GENERAL"]["entries"][mc_version].value, fs::copy_options::update_existing);
            }
        }
    }

    cout << "Please, run \"mserman verify\" to finish creation\nNote that you will need to setup core by yourself\n";
    flushConfig(root);
    return 0;
}
/**Verification operation
 *
 * @param options options such as "--minecraft", "--allow-unsafe"
 * @param arguments currently unused
 * @return nothing
 */
int verifyOp(set<string> &options, vector<string>&) {
    root_pack root;
    parseConfig(root);
    bool doMinecraft = options.contains("minecraft");
    {
        for (const auto &mc_version: fs::directory_iterator(ensureExists(root.paths["GENERAL"]["entries"]).value)) {
            if (!mc_version.is_directory()) cout << "Found non-version object " << mc_version.path().filename() << " in entries folder\n";
            else if (!root.root("VERSIONS")(mc_version.path().filename())) cout << "Found unknown version folder " << mc_version.path().filename() << " in entries folder\n";
            else continue;
            root.stats["UNKNOWN"]["VERSIONS"][mc_version.path().filename()]++;
        }
        string_node &rootModules = root.root["ENTRIES"];
        for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
            path_node &dirEntries = ensureExists(root.paths["GENERAL"]["entries"][mc_version]);
            for (const auto &entry : fs::directory_iterator(dirEntries.value)) {
                string fileName = relative(entry.path(), dirEntries.value);
                if (!entry.is_regular_file() || entry.path().extension() != ".jar") {
                    cout << "Found invalid file \"" << fileName << "\"\n";
                    root.stats["UNKNOWN"]["ENTRIES"][fileName]++;
                } else {
                    vector<module_entry> modules = lookFile(entry);
                    if (modules.empty()) {
                        vector<module_entry> existing = lookRoot(root, fileName, mc_version);
                        if (existing.empty()) {
                            string temp = fileName.substr(0, fileName.size() - 4);
                            string key1 = '.' + stringToLower(temp);
                            rootModules[key1][mc_version]["default"].value = fileName;
                            rootModules[key1][mc_version]["default"]["MODEL"].toCode(MODEL_FORGE, 2);
                            rootModules[key1][mc_version]["default"]["STANDALONE"];
                        }
                    } else {
                        for (auto &module: modules) {
                            if (bool edit = shouldVerify(root, module, mc_version); edit) {
                                string_node &rootModule = rootModules[module.id][mc_version][module.version];
                                rootModule.value = fileName;
                                rootModule["MODEL"].toCode(module.model, 2);
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
        }
        set<string> processed;
        set<string> removed;
        function<void(const string&, string_node&)> dfs = [&removed, &processed, &root, &dfs, &rootModules](const string& module, string_node &rootEntry) {
            if (!processed.insert(module).second) return;
            set<string> removedMCVersion;
            for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
                if (!rootEntry(mc_version)) continue;
                set<string> removedVersion;
                for (auto &[moduleVersion, rootModule]: rootEntry[mc_version]) {
                    if (rootModule("SYNTHETIC")) {
                        cout << "Ignoring SYNTHETIC " << combine(module, moduleVersion) << '\n';
                        root.stats["VALID"]["ENTRIES"][module][moduleVersion]++;
                        continue;
                    }
                    string &status = rootModule["STATUS"].upper();
                    fs::path filePath = root.paths["GENERAL"]["entries"][mc_version].value / rootModule.value;
                    if (is_regular_file(filePath) && filePath.extension() == ".jar") {
                        int model = rootModule["MODEL"].asCode(2, MODEL_FORGE);
                        rootModule["MODEL"].toCode(model, 2);
                        int codeSide = rootModule["SIDE"].asCode(2, SIDE_BOTH);
                        if (model == MODEL_PLUGIN) codeSide = SIDE_SERVER;
                        rootModule["SIDE"].toCode(codeSide, 2);
                        rootModule["DEPENDENCIES"];
                        if (status != "VALID" && status != "F-VALID") {
                            cout << "Need to configure " << combine(module, moduleVersion) << "\n";
                            root.stats["NEW"][module][moduleVersion]++;
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
                                depVersion = findVersion(rootModules[dep][mc_version], depVersion, condition);
                                if (!depVersion.empty() && root.stats("VALID")("ENTRIES")(dep)(depVersion)) {
                                    if (!rootModules[dep](mc_version)) continue;
                                    if (rootModules[dep][mc_version][depVersion]("SYNTHETIC")) continue;
                                    int depModel = rootModules[dep][mc_version][depVersion]["MODEL"].asCode(2,MODEL_FORGE);
                                    if ((depModel & model) != model) {
                                        cout << "Dependency for " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is using wrong model\n";
                                        root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["MODEL"]++;
                                        fine = false;
                                    }
                                    int depSide = rootModules[dep][mc_version][depVersion]["SIDE"].asCode(2,SIDE_BOTH);
                                    if ((depSide & codeSide) != codeSide) {
                                        cout << "Dependency for  " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is on the wrong side\n";
                                        root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["SIDE"]++;
                                        fine = false;
                                    }
                                } else {
                                    cout << "Failed to resolve dependency for " << combine(module, moduleVersion)<< ": " << combine(dep, depVersion.empty() ? rootDependency.value : depVersion)<< "\n";
                                    root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]++;
                                    fine = false;
                                }
                            } else {
                                cout << "Failed to find dependency for " << combine(module, moduleVersion) << ":  " << combine(dep, rootDependency.value) << "\n";
                                root.stats["LOST"]["ENTRIES"][dep][rootDependency.value]["BY-ENTRY"][module][moduleVersion]++;
                                fine = false;
                            }
                        }
                        if (fine) root.stats["VALID"]["ENTRIES"][module][moduleVersion]++;
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
                    root.stats["UNKNOWN"]["ENTRIES"][module][moduleVersion]++;
                }
                for (const auto &moduleVersion: removedVersion) rootEntry[mc_version].children.erase(moduleVersion);
                if (rootEntry[mc_version].children.empty()) removedMCVersion.insert(module);
            }
            for (const auto &moduleVersion: removedMCVersion) rootEntry.children.erase(moduleVersion);
            if (rootEntry.children.empty()) removed.insert(module);
        };
        for (auto &[module, rootModule]: rootModules) dfs(module, rootModule);
        for (const auto &module: removed) rootModules.children.erase(module);
        for (auto &[family, rootFamily]: root.root["FAMILIES"]) {
            cout << "Processing family \"" << family << "\"\n";
            bool fine = true;
            for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
                unordered_map<int, vector<pair<string, string>>> modelInfo;
                for (auto &[module, rootModule]: rootFamily) {
                    string moduleVersion = rootModule.value;
                    module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
                    moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
                    if (!rootModules(module)) {
                        cout << "Failed to find entry " << combine(module, moduleVersion) << '\n';
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
                        fine = false;
                        continue;
                    }
                    if (!rootModules(module)(mc_version)) {
                        continue;
                    }
                    moduleVersion = findVersion(rootModules[module][mc_version], moduleVersion, condition);
                    if (!root.stats("VALID")("ENTRIES")(module)(moduleVersion)) {
                        cout << "Failed to resolve entry " << combine(module, moduleVersion) << '\n';
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
                        fine = false;
                    } else if(!rootModules[module][mc_version][moduleVersion]("STANDALONE")) {
                        cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
                        fine = false;
                    } else {
                        auto &list = modelInfo[rootModules[module][mc_version][moduleVersion]["MODEL"].asCode(2, MODEL_FORGE)];
                        list.emplace_back(module, moduleVersion);
                    }
                }
                auto maxModel = std::max_element( modelInfo.begin(), modelInfo.end(),[](const auto& a, const auto& b) {
                    return a.second.size() < b.second.size();
                });

                if (maxModel != modelInfo.end()) {
                    for (const auto& [model, data] : modelInfo) {
                        if (model != maxModel->first) {
                            fine = false;
                            for (const auto& [module, moduleVersion] : data) {
                                cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
                                root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]["MODEL"]++;
                            }
                        }
                    }
                    root.cache["FAMILIES"][family][mc_version]["MODEL"].toCode(maxModel->first, 2);
                } else {
                    root.cache["FAMILIES"][family][mc_version]["MODEL"].toCode(MODEL_FORGE, 2);
                }
            }
            if (fine)
                root.stats["VALID"]["FAMILIES"][family]++;
        }
        for (auto &[pack, rootPack]: root.root["PACKS"]) {
            cout << "Processing pack \"" << pack << "\"\n";
            bool fine = true;
            for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
                unordered_map<int, vector<pair<string, pair<bool, string>>>> modelInfo;
                for (auto &[module, rootModule]: rootPack["ENTRIES"]) {
                    string moduleVersion = rootModule.value;
                    module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
                    moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
                    if (!rootModules(module)) {
                        cout << "Failed to find entry " << combine(module, moduleVersion) << '\n';
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
                        fine = false;
                        continue;
                    }
                    if (!rootModules(module)(mc_version)) {
                        continue;
                    }
                    moduleVersion = findVersion(rootModules[module][mc_version], moduleVersion, condition);
                    if (!root.stats("VALID")("ENTRIES")(module)(moduleVersion)) {
                        cout << "Failed to resolve entry " << combine(module, moduleVersion) << '\n';
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
                        fine = false;
                    } else if(!rootModules[module][mc_version][moduleVersion]("STANDALONE")) {
                        cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
                        root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
                        fine = false;
                    } else {
                        auto &list = modelInfo[rootModules[module][mc_version][moduleVersion]["MODEL"].asCode(2, MODEL_FORGE)];
                        list.emplace_back(module, pair{false, moduleVersion});
                    }
                }
                for (auto &[family, rootFamily]: rootPack["FAMILIES"])
                    if (!root.stats("VALID")("FAMILIES")(family)) {
                        cout << "Failed to resolve family \"" << family << "\"\n";
                        root.stats["LOST"]["FAMILIES"][family]["BY-PACK"][pack]++;
                        fine = false;
                    } else {
                        auto &list = modelInfo[root.cache["FAMILIES"][family][mc_version]["MODEL"].asCode(2, MODEL_FORGE)];
                        list.emplace_back(family, pair{true, ""});
                    }
                auto maxModel = std::max_element(modelInfo.begin(), modelInfo.end(),[](const auto& a, const auto& b) {
                    return a.second.size() < b.second.size();
                });

                if (maxModel != modelInfo.end()) {
                    for (const auto& [model, data] : modelInfo) {
                        if (model != maxModel->first) {
                            fine = false;
                            for (const auto& [module, moduleInfo] : data) {
                                const auto& [moduleType, moduleVersion] = moduleInfo;
                                if (!moduleType) {
                                    cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
                                    root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]["MODEL"]++;
                                } else  {
                                    cout << "Family \"" << module << "\" should have different model\n";
                                    root.stats["LOST"]["FAMILIES"][module]["BY-PACK"][pack]["MODEL"]++;
                                }
                            }
                        }
                    }
                    root.cache["PACKS"][pack][mc_version]["MODEL"].toCode(maxModel->first, 2);
                } else {
                    root.cache["PACKS"][pack][mc_version]["MODEL"].toCode(MODEL_FORGE, 2);
                }
            }
            if (fine)
                root.stats["VALID"]["PACKS"][pack]++;
        }
        cout.flush();
    }
    {
        for (const auto &metaConfig: fs::directory_iterator(ensureExists(root.paths["GENERAL"]["config"]).value)) {
            if (!metaConfig.is_directory()) cout << "Found non-meta-config object " << metaConfig.path().filename() << " in meta-configs folder\n";
            else if (!root.root("META-CONFIGS")(metaConfig.path().filename())) cout << "Found unknown meta-config folder " << metaConfig.path().filename() << '\n';
            else continue;
            root.stats["UNKNOWN"]["META-CONFIGS"][metaConfig.path().filename()]++;
        }
        for (auto &[metaConfig, rootConfigs]: root.root["META-CONFIGS"]) {
            cout << "Processing meta-config \"" << metaConfig << "\"\n";
            const vector<string> metaType = {"META-CONFIGS", metaConfig};
            path_node &dirConfigs = ensureExists(root.paths["GENERAL"]["config"][metaConfig]);
            for (const auto& config : fs::recursive_directory_iterator(dirConfigs.value)) {
                string fileName = config.path().lexically_relative(dirConfigs.value);
                if (config.is_regular_file()) {
                    if (none_of(rootConfigs.begin(), rootConfigs.end(),
                                [&](const auto& entry) { return fileName.starts_with(entry.first); })) {
                        rootConfigs[fileName];
                    }
                } else if (!config.is_directory()) {
                    cout << "Found invalid config file '" << fileName << "'\n";
                    root.stats["UNKNOWN"][metaType][fileName]++;
                }
            }
            set<string> removed;
            bool fine = true;
            for (auto &config : rootConfigs) {
                string &status = config.second.upper();
                fs::path filePath = dirConfigs.value / config.first;
                if (is_regular_file(filePath) || is_directory(filePath)) {
                    if (status != "VALID") {
                        cout << "Need to configure config '" << config.first << "'\n";
                        status = "NEW";
                        root.stats["NEW"][metaType][config.first]++;
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
                } else {
                    cout << "Found invalid config file '" << config.first << "'\n";
                    status = "NOT-VALID";
                }
                fine = false;
                root.stats["UNKNOWN"][metaType][config.first]++;
            }
            if (fine) root.stats["VALID"][metaType]++;
            for (const auto &entry: removed) rootConfigs.children.erase(entry);
        }
        cout.flush();
    }
    {
        for (const auto &core: fs::directory_iterator(ensureExists(root.paths["GENERAL"]["core"]).value)) {
            if (!core.is_directory()) cout << "Found non-core object " << core.path().filename() << " in cores folder\n";
            else if (!root.root("CORES")(core.path().filename())) cout << "Found unknown core " << core.path().filename() << " in folder\n";
            else continue;
            root.stats["UNKNOWN"]["CORES"][core.path().filename()]++;
        }
        for (auto &[core, rootCore]: root.root["CORES"]) {
            cout << "Processing core \"" << core << "\"\n";
            const vector<string> coreType = {"CORES", core};
            path_node &dirCore = ensureExists(root.paths["GENERAL"]["core"][core]);
            string &status = rootCore["STATUS"].upper();
            int codeSupport = rootCore["SUPPORT"].asCode(3, 0b000);
            rootCore["SUPPORT"].toCode(codeSupport, 3);;
            bool validTests = (bool) rootCore("USE-FOR-SIMULATION");
            const fs::path coreRuntime = rootCore["JAVA-RUNTIME"].value;
            const string coreMain = rootCore["MAIN"].value;
            if (status != "NOT-VALID" && status != "VALID") {
                cout << "Need to configure core \"" << core << "\"\n";
                root.stats["NEW"][coreType]++;
                status = "NEW";
                continue;
            }
            bool fine = true;
            if (!is_regular_file(dirCore.value / coreMain)) {
                cout << "Main file for core \"" << core << "\" not found\n";
                root.stats["LOST"][coreType]["MAIN"]++;
                fine = false;
            }
            if (!is_regular_file(coreRuntime / "java")) {
                cout << "Runtime for core \"" << core << "\" not found\n";
                root.stats["LOST"][coreType]["RUNTIME"]++;
                fine = false;
            } else {
                cout << "Checking Java runtime " << coreRuntime << " for core \"" << core << "\"" << endl;
                string command = (coreRuntime / "java").string() + " -version";
                bool bufferFine = !system(command.c_str());
                cout.flush();
                cout << (!bufferFine ? "------------------------\nNot a valid Java runtime\n------------------------" : "-------------\nRuntime valid\n-------------") << endl;
                fine = fine && bufferFine;
            }
            status = fine ? "VALID" : "NOT-VALID";
            if (fine) root.stats["VALID"][coreType]++;
        }
        cout.flush();
    }
    {
        //TODO check the same way as servers
        if (doMinecraft) {
            string &mc_version = root.root["GENERAL"]["MINECRAFT"]["VERSION"].value;
            if (!root.root["VERSIONS"](mc_version)) {
                cout << "User uses unknown version\n";
                root.stats["LOST"]["VERSIONS"][mc_version]["BY-USER"]++;
            } else if (!is_directory(root.paths["MINECRAFT"].value)) {
                cout << "Invalid minecraft directory " << root.paths["MINECRAFT"].value << '\n';
            } else {
                string_node &rootMinecraft = root.root["GENERAL"]["MINECRAFT"]["VERSIONS"][mc_version];
                path_node &dirMinecraft = root.paths["MINECRAFT"];
                cout << "Processing minecraft directory " << dirMinecraft.value << '\n';
                string &oldVersion = root.cache["OLD-MINECRAFT"].value;
                string &combination = rootMinecraft["SWAP-WHITELIST"].value;
                if (combination.empty()) combination = "default";
                string &oldCombination = root.cache["OLD-COMBINATION"].value;
                if (oldVersion != mc_version || oldCombination != combination) {
                    bool old_valid = (bool) root.root["VERSIONS"](oldVersion);
                    string_node &root_used = old_valid ? root.root["GENERAL"]["MINECRAFT"]["VERSIONS"][oldVersion] : rootMinecraft;
                    path_node &dirBackedUp = ensureExists(root.paths["BACKUP"]["version"][old_valid ? oldVersion : mc_version][oldCombination]);
                    for (auto &[entry, ignored]: root_used["SWAP-WHITELIST"]) {
                        if (entry == "mods" || entry == "config" || entry == "versions") {
                            cout << "Please avoid using directories such as \"mods\", \"config\", \"versions\" in your swap whitelist\n";
                            continue;
                        }
                        fs::path absoluteItem = dirBackedUp.value / entry;
                        fs::path absoluteLink = dirMinecraft.value / entry;
                        if (is_symlink(absoluteLink)) {
                            remove(absoluteLink);
                        } else if (exists(absoluteLink)) {
                            rename(absoluteLink, absoluteItem);
                        }
                    }
                }
                {
                    path_node &dirBackedUp = ensureExists(root.paths["BACKUP"]["version"][mc_version][combination]);
                    for (auto &[entry, ignored]: rootMinecraft["SWAP-WHITELIST"]) {
                        if (entry == "mods" || entry == "config" || entry == "versions") {
                            cout << "Please avoid using directories such as \"mods\", \"config\", \"versions\" in your swap whitelist\n";
                            continue;
                        }
                        fs::path absoluteItem = absolute(dirBackedUp.value / entry);
                        fs::path absoluteLink = dirMinecraft.value / entry;
                        if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
                            remove(absoluteLink);
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink to object \"" << entry << "\" fixed\n";
                        } else if (!exists(symlink_status(absoluteLink))) {
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink to mod \"" << entry << "\" created\n";
                        } else if (exists(symlink_status(absoluteLink)) && !is_symlink(absoluteLink)) {
                            cout << "Failed to create symlink to mod \"" << entry
                                 << "\", location already taken\n";
                        }
                    }
                }
                string_node valid_packs;
                for (auto &[pack, ignored]: rootMinecraft["PACKS"]) {
                    if (!root.stats("VALID")("PACKS")(pack)) {
                        cout << "Pack \"" << pack << "\" not valid\n";
                        root.stats["LOST"]["PACKS"][pack]["BY-USER"]++;
                        continue;
                    }
                    valid_packs[pack].value = mc_version;
                }
                linkPacks(root, valid_packs, "", SIDE_CLIENT);
                if (!root.stats("VALID")("META-CONFIGS")(rootMinecraft["META-CONFIG"])) {
                    cout << "Meta-config \"" << rootMinecraft["META-CONFIG"].value << "\" not valid\n";
                    root.stats["LOST"]["META-CONFIGS"][rootMinecraft["META-CONFIG"]]["BY-USER"]++;
                } else {
                    path_node &dirServerConfig = ensureExists(dirMinecraft["config"]);
                    path_node &dirConfig = root.paths["GENERAL"]["config"][rootMinecraft["META-CONFIG"]];
                    auto it = fs::recursive_directory_iterator(dirServerConfig.value);
                    for (const auto& absoluteLink : it) {
                        string fileName = absoluteLink.path().lexically_relative(dirServerConfig.value);
                        fs::path absoluteItem = absolute(dirConfig.value / fileName);
                        if (is_directory(symlink_status(absoluteLink))) continue;
                        else if (is_regular_file(symlink_status(absoluteLink))) {
                            remove(absoluteItem);
                            create_directories(absoluteItem.parent_path());
                            rename(absoluteLink, absoluteItem);
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Moved config file " << fileName << '\n';
                        } else if (!exists(absoluteItem)) {
                            it.disable_recursion_pending();
                            remove(absoluteLink);
                            cout << "Symlink to config file \"" << fileName << "\" removed\n";
                        } else if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
                            it.disable_recursion_pending();
                            remove(absoluteLink);
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink to config file \"" << fileName << "\" fixed\n";
                        }
                    }
                    for (const auto& absoluteItem : fs::recursive_directory_iterator(dirConfig.value)) {
                        string fileName = absoluteItem.path().lexically_relative(dirConfig.value);
                        fs::path absoluteLink = dirServerConfig.value / fileName;
                        if (!is_regular_file(absoluteItem)) continue;
                        if (!exists(symlink_status(absoluteLink))) {
                            create_directories(absoluteLink.parent_path());
                            create_symlink(absolute(absoluteItem), absoluteLink);
                            cout << "Symlink to config file \"" << fileName << "\" created\n";
                        } else if (exists(symlink_status(absoluteLink)) && !is_symlink(absoluteLink)) {
                            cout << "Failed to create symlink to config file \"" << fileName << "\", location already taken\n";
                        }
                    }
                }
                oldVersion = mc_version;
                oldCombination = combination;
            }
        }
    }
    {
        for (const auto &backup: fs::directory_iterator(ensureExists(root.paths["BACKUP"]["server"]).value)) {
            if (!backup.is_regular_file() || backup.path().extension() != ".bak") cout << "Found non-backup object " << backup.path().filename() << " in server backups folder\n";
            else if (!root.root("SERVERS")(backup.path().stem().filename())) cout << "Found unknown server backup " << backup.path().filename() << "\n";
            else continue;
            root.stats["UNKNOWN"]["SERVERS"][backup.path().stem().filename()]["BACKUP"]++;
        }
        for (const auto &server: fs::directory_iterator(ensureExists(root.paths["GENERAL"]["server"]).value)) {
            if (!server.is_directory()) cout << "Found non-server object " << server.path().filename() << " in servers folder\n";
            else if (!root.root("SERVERS")(server.path().filename())) cout << "Found unknown server " << server.path().filename() << "\n";
            else continue;
            root.stats["UNKNOWN"]["SERVERS"][server.path().filename()]++;
        }
        for (auto &[server, rootServer]: root.root["SERVERS"]) {
            cout << "Processing server \"" << server << "\"\n";
            const vector<string> serverType = {"SERVERS", server};
            string &status = rootServer["STATUS"].upper();
            string &core = rootServer["CORE"].value;
            const vector<string> coreType = {"CORES", core};
            rootServer["JAVA-ARGS"];
            if (status != "NOT-VALID" && status != "VALID") {
                cout << "Need to configure server\n";
                root.stats["NEW"][serverType]++;
                status = "NEW";
                continue;
            }
            bool fine = true;
            if (!root.stats("VALID")(coreType)) {
                cout << "Core for server not valid\n";
                root.stats["LOST"][coreType]["BY-SERVER"][server]++;
                status = "NOT-VALID";
                continue;
            }
            string_node &rootCore = root.root[coreType];
            int support = rootCore["SUPPORT"].asCode(3, 0b000);
            for (auto &[pack, packVersion]: rootServer["PACKS"]) {
                if (!root.stats("VALID")("PACKS")(pack)) {
                    cout << "Pack \"" << pack << "\" not valid\n";
                    root.stats["LOST"]["PACKS"][pack]["BY-SERVER"][server]++;
                    fine = false;
                } else {
                    int packModel = root.cache["PACKS"][pack]["MODEL"].asCode(2, MODEL_FORGE);
                    if (!((support >> packModel) & 1)) {
                        cout << "Pack \"" << pack << "\" should have different model\n";
                        root.stats["LOST"]["PACKS"][pack]["BY-SERVER"][server]["MODEL"]++;
                        fine = false;
                    }
                }
                if (!root.root["VERSIONS"](packVersion)) {
                    cout << "Pack \"" << pack << "\" uses unknown version\n";
                    root.stats["LOST"]["VERSIONS"][packVersion]["BY-SERVER"][server]++;
                    fine = false;
                }
            }
            if (support & (SUPPORT_FORGE | SUPPORT_FABRIC)) {
                if (!root.stats("VALID")("META-CONFIGS")(rootServer["META-CONFIG"])) {
                    cout << "Meta-config \"" << rootServer["META-CONFIG"].value << "\" not valid\n";
                    root.stats["LOST"]["META-CONFIGS"][rootServer["META-CONFIG"]]["BY-SERVER"][server]++;
                    fine = false;
                }
            }
            status = fine ? "VALID" : "NOT-VALID";
            if (fine) {
                root.cache[serverType]["VALID"];
                root.stats["VALID"][serverType]++;
                if (!doMinecraft) {
                    path_node &dirServer = ensureExists(root.paths["GENERAL"]["server"][server]);
                    path_node &dirCore = root.paths["GENERAL"]["core"][core];
                    for (const auto &entry: fs::directory_iterator(dirCore.value)) {
                        string fileName = entry.path().lexically_relative(dirCore.value);
                        fs::path absoluteItem = absolute(entry.path());
                        fs::path absoluteLink = dirServer.value / fileName;
                        if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
                            remove(absoluteLink);
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink fixed: " << fileName << '\n';
                        } else if (!exists(absoluteLink)) {
                            create_symlink(absoluteItem, absoluteLink);
                            cout << "Symlink created: " << fileName << '\n';
                        } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                            cout << "Failed to create symlink, location already taken: " << fileName << '\n';
                        }
                    }
                    linkPacks(root, rootServer["PACKS"], server, SIDE_SERVER);
                    if (support & (SUPPORT_FABRIC | SUPPORT_FORGE)) {
                        path_node &dirServerConfig = ensureExists(dirServer["config"]);
                        path_node &dirConfig = root.paths["GENERAL"]["config"][rootServer["META-CONFIG"]];
                        auto it = fs::recursive_directory_iterator(dirServerConfig.value);
                        for (const auto& absoluteLink : it) {
                            string fileName = absoluteLink.path().lexically_relative(dirServerConfig.value);
                            fs::path absoluteItem = absolute(dirConfig.value / fileName);
                            if (is_directory(symlink_status(absoluteLink))) continue;
                            else if (is_regular_file(symlink_status(absoluteLink))) {
                                remove(absoluteItem);
                                create_directories(absoluteItem.parent_path());
                                rename(absoluteLink, absoluteItem);
                                create_symlink(absoluteItem, absoluteLink);
                                cout << "Moved config file " << fileName << '\n';
                            } else if (!exists(absoluteItem)) {
                                it.disable_recursion_pending();
                                remove(absoluteLink);
                                cout << "Symlink to config file \"" << fileName << "\" removed\n";
                            } else if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
                                it.disable_recursion_pending();
                                remove(absoluteLink);
                                create_symlink(absoluteItem, absoluteLink);
                                cout << "Symlink to config file \"" << fileName << "\" fixed\n";
                            }
                        }
                        for (const auto& absoluteItem : fs::recursive_directory_iterator(dirConfig.value)) {
                            string fileName = absoluteItem.path().lexically_relative(dirConfig.value);
                            fs::path absoluteLink = dirServerConfig.value / fileName;
                            if (!is_regular_file(absoluteItem)) continue;
                            if (!exists(symlink_status(absoluteLink))) {
                                create_directories(absoluteLink.parent_path());
                                create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink to config file \"" << fileName << "\" created\n";
                            } else if (exists(symlink_status(absoluteLink)) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to config file \"" << fileName << "\", location already taken\n";
                            }
                        }
                    }
                }
            }
        }
        cout.flush();
    }
    flushConfig(root);
    return 0;
}
