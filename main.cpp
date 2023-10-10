#include <functional>
#include <map>
#include <vector>
#include <set>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/wait.h>
#include <zip.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
using namespace std;
namespace fs = filesystem;

void exitWithUsage() __attribute__ ((__noreturn__));
void printError(const string &message) __attribute__ ((__noreturn__));
bool printErrorName(const string &nameWhere, const string &nameFor);

template<typename type>
struct Node {
    type value{};
    map<string, Node> children;
    void makeIfAbsent(const string &path) {
        if (children.find(path) == children.end())
            children[path] = Node();
    }
    void makeIfAbsent(const vector<string>& path) {
        Node *currentNode = this;

        for (const string& nodeName : path) {
            auto it = currentNode->children.find(nodeName);
            if (it == currentNode->children.end())
                currentNode->children[nodeName] = Node();
            currentNode = &currentNode->children[nodeName];
        }
    }
    Node<type> &accessNode(const string &path) {
        makeIfAbsent(path);
        return children[path];
    }
    Node<type> &accessNode(const vector<string>& path) {
        makeIfAbsent(path);
        Node *currentNode = this;
        for (const string& nodeName : path) {
            currentNode = &currentNode->children[nodeName];
        }
        return *currentNode;
    }
    bool lookForNode(const vector<string>& path) {
        Node* currentNode = this;
        for (const string& nodeName : path) {
            if (currentNode->children.find(nodeName) == currentNode->children.end())
                return false;
            currentNode = &currentNode->children[nodeName];
        }
        return true;
    }
    bool lookForValue(const type &pathValue) {
        return any_of(children.begin(), children.end(), [&pathValue](const auto& item) {return item.second.value == pathValue;});
    }
    _Rb_tree_iterator<pair<const basic_string<char>, Node<type>>> begin() { return children.begin(); }
    _Rb_tree_iterator<pair<const basic_string<char>, Node<type>>> end() { return children.end(); }
};

typedef Node<string> StringNode;

void flushConfig(StringNode &config) {
    ofstream file{"mserman.conf"};
    if (!file.is_open()) printError("Failed to open mserman.conf for writing.\n");
    function<void(StringNode&, const string&)> lambda = [&file,&lambda](StringNode& node, const string &level) {
        bool clean = true;
        bool finalNewline = true;
        for (auto &item : node) {
            if (!item.second.children.empty() && !item.second.value.empty()) printError("Was not expecting node with both value and children");
            if (!item.second.value.empty()) {
                file << (clean ? level : " ") << item.first << " = " << item.second.value << endl;
                clean = true;
                finalNewline = true;
            } else if (!item.second.children.empty()) {
                file << (clean ? level : " ") << item.first << endl;
                lambda(item.second, level + "    ");
                file << level << ">";
                clean = false;
                finalNewline = false;
            } else {
                file << (clean ? level : " ") << item.first << " >" << endl;
                clean = true;
                finalNewline = true;
            }
        }
        if (!finalNewline) file << endl;
    };
    lambda(config, "");
    file.flush();
    file.close();
}
void parseConfig(StringNode &config) {
    if (!fs::exists("mserman.conf")) {
        ofstream file{"mserman.conf"};
        if (!file.is_open()) printError("Failed to create default mserman config.\n");
        file << "General RootDir = data >";
        file.close();
    }
    ifstream file{"mserman.conf"};
    if (!file.is_open()) printError("Failed to open mserman.conf for reading.\n");
    vector<string> path;
    string word;
    while (file >> word) {
        if (word == "=" || word == ">") {
            if (word == "=") file >> config.accessNode(path).value;
            path.pop_back();
        } else {
            path.push_back(word);
            config.makeIfAbsent(path);
        }
    }
    if (!path.empty()) printError("Expected '>'x" + to_string(path.size()) + ", got 'eof'.\n");
    file.close();
}

int main(int argc, const char** argv) {
    set<string> options;
    vector<string> arguments;
    bool readingOptions = true;
    for (int i = 1; i < argc; ++i) {
        string asString = argv[i];
        if (asString.starts_with('-') && readingOptions) {
            if (asString == "--")
                readingOptions = false;
            else
                options.insert(asString);
        } else {
            arguments.push_back(asString);
            readingOptions = false;
        }
    }
    if (arguments.empty() || options.contains("--help") || options.contains("-h")) exitWithUsage();
    if (arguments[0] == "verify") {
        StringNode config{};
        parseConfig(config);
        bool proceedLinking = true;
        Node<int> stats;
        fs::path versionsDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / "version/";
        fs::path coresDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / "core/";
        fs::path serversDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / "server/";
        fs::path backupsDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / "backup/";
        create_directories(versionsDir);
        create_directories(coresDir);
        create_directories(serversDir);
        create_directories(backupsDir);
        for (const auto &item: fs::directory_iterator(versionsDir)) {
            if (item.is_directory()) {
                if (!config.lookForNode({"Versions", item.path().filename()}))
                    cout << "Found unknown version: " << item.path().filename() << endl;
            } else
                cout << "Found unknown file: " << item.path().filename() << " in versions folder" << endl;
        }
        for (auto &item: config.accessNode("Versions")) {
            StringNode &useMods = item.second.accessNode("Mods");
            StringNode &usePlugins = item.second.accessNode("Plugins");
            fs::path dirMods = versionsDir / (item.first + "/mods/");
            fs::path dirPlugins = versionsDir / (item.first + "/plugins/");
            fs::path dirConfig = versionsDir / (item.first + "/config/");
            create_directories(dirConfig);
            create_directories(dirMods);
            create_directories(dirPlugins);
            for (const auto &item2: fs::directory_iterator(dirConfig)) {
                if (item2.is_directory()) {
                    if (!item.second.lookForNode({"Configs", item2.path().filename()}))
                        cout << "Found unknown configs folder: " << item2.path().filename() << " in version: " << item.first << endl;
                } else
                    cout << "Found unknown file: " << item2.path().filename() << " in configs of version: " << item.first << endl;
            }
            for (auto &item2: item.second.accessNode("Configs")) {
                fs::path dir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("version/" + item.first + "/config/" + item2.first);
                fs::create_directories(dir);
                for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        string filePath = relative(entry.path(), dir);
                        if (!item2.second.lookForNode({filePath})) {
                            fs::file_time_type creationTime = fs::last_write_time(entry);
                            auto t = chrono::system_clock::to_time_t(fs::__file_clock::to_sys(creationTime));
                            tm tm = *localtime(&t);
                            stringstream ss;
                            ss << put_time(&tm, "%d.%m.%y_%H:%M");
                            string formattedTime = ss.str();
                            item2.second.accessNode(filePath).value = formattedTime;
                        }
                    }
                }
                for (auto &item3 : item2.second) {
                    if (!fs::exists(dir / item3.first) && item3.second.value != "Gone") {
                        item3.second.value = "Gone";
                        cout << "Failed to find config '" << item.first << "/" << item2.first << "/" << item3.first << "', assuming its gone" << endl;
                    } else if (fs::exists(dir / item3.first) && (item3.second.value == "Gone" || item3.second.value.empty())) {
                        fs::file_time_type creationTime = fs::last_write_time(dir / item3.first);
                        auto t = chrono::system_clock::to_time_t(fs::__file_clock::to_sys(creationTime));
                        tm tm = *localtime(&t);
                        stringstream ss;
                        ss << put_time(&tm, "%d.%m.%y_%H:%M");
                        string formattedTime = ss.str();
                        item3.second.value = formattedTime;
                        cout << "Expected for config '" << item.first << "/" << item2.first << "/" << item3.first << "' to be deleted" << endl;
                    } else if (item3.second.value != "Done" && item3.second.value != "Gone") {
                        cout << "Need to configure '" << item.first << "/" << item2.first << "/" << item3.first << "' config, which was created at " << item3.second.value << endl;
                    }
                }
            }
            function<void(StringNode&, fs::path&)> f1 = [](StringNode &node, fs::path &dir) {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        string filePath = relative(entry.path(), dir);
                        if (!node.lookForValue(filePath)) {
                            string key;
                            for (int counter = 1; node.children.find(key = ("temp" + to_string(counter))) != node.children.end(); counter++);
                            node.accessNode(key).value = filePath;
                        }
                    }
                }
            };
            f1(useMods, dirMods);
            f1(usePlugins, dirPlugins);
            function<void(StringNode&, fs::path&, const string)> f2 = [&item](StringNode &node, fs::path &dir, const string &name) {
                for (auto it = node.begin(); it != node.end();) {
                    if (it->second.value.empty()) {
                        it = node.children.erase(it);
                        cout << "Failed to find " << name << " '" << item.first << "/" << it->first << "', assuming its an error" << endl;
                        continue;
                    } else if (!fs::exists(dir / it->second.value) && !it->first.starts_with("gone")) {
                        string key;
                        for (int counter = 1; node.children.find(key = ("gone" + to_string(counter++))) != node.children.end(););
                        node.accessNode(key).value = it->second.value;
                        it = node.children.erase(it);
                        cout << "Failed to find " << name << " '" << item.first << "/" << it->second.value << "', assuming its gone" << endl;
                        continue;
                    } else if (fs::exists(dir / it->second.value) && it->first.starts_with("gone")) {
                        string key;
                        for (int counter = 1; node.children.find(key = ("temp" + to_string(counter))) != node.children.end(); counter++);
                        node.accessNode(key).value = it->second.value;
                        it = node.children.erase(it);
                        cout << "Expected for " << name << " '" << item.first << "/" << it->second.value << "', to be deleted" << endl;
                        continue;
                    } else if (it->first.starts_with("temp")) {
                        cout << "Need to rename " << name << " '" << item.first << "/" << it->second.value << "', named as " << it->first << endl;
                    }
                    it++;
                }
            };
            f2(useMods, dirMods, "mod");
            f2(usePlugins, dirPlugins, "plugin");
        }
        for (const auto &item: fs::directory_iterator(coresDir)) {
            if (item.is_directory()) {
                if (!config.lookForNode({"Cores", item.path().filename()}))
                    cout << "Found unknown core: " << item.path().filename() << endl;
            } else
                cout << "Found unknown file: " << item.path().filename() << " in cores folder" << endl;
        }
        for (auto &item: config.accessNode("Cores")) {
            if (!config.lookForNode({"Versions", item.second.accessNode("Version").value})) proceedLinking = printErrorName("Core " + item.first, "Version");
            if (item.second.accessNode("Support").value.size() != 2) item.second.accessNode("Support").value = "--";
            fs::path dir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("core/" + item.first);
            fs::create_directories(dir);
            if (!fs::is_regular_file(dir / item.second.accessNode("Core").value))
                cout << "Main jar-file '" << item.second.accessNode("Core").value << "' in core " << item.first << " doesn't exist" << endl;
            if (!fs::is_regular_file(((fs::path)item.second.accessNode("JavaPath").value) / "java"))
                cout << "Java runtime '" << item.second.accessNode("JavaPath").value << "' for core " << item.first << " doesn't exist" << endl;
            else {
                cout << "Checking Java runtime '" << item.second.accessNode("JavaPath").value << "' for core " << item.first << endl;
                string command = ((((fs::path)item.second.accessNode("JavaPath").value)) / "java").string() + " -version";
                cout << (system(command.c_str()) ? "------------------------\nNot a valid Java runtime\n------------------------" : "-------------\nRuntime valid\n-------------") << endl;
            }
        }
        for (const auto &item: fs::directory_iterator(serversDir)) {
            if (item.is_directory()) {
                if (!config.lookForNode({"Servers", item.path().filename()}))
                    cout << "Found unknown server: " << item.path().filename() << endl;
            } else
                cout << "Found unknown file: " << item.path().filename() << " in servers folder" << endl;
        }
        for (const auto &item: fs::directory_iterator(backupsDir)) {
            if (item.is_regular_file() && item.path().extension() == ".bak") {
                if (!config.lookForNode({"Servers", item.path().stem().filename()}))
                    cout << "Found unknown backup: " << item.path().filename() << endl;
            } else
                cout << "Found unknown object: " << item.path().filename() << " in backups folder" << endl;
        }
        for (auto &item: config.accessNode("Archs")) {
            string core = item.second.accessNode("Core").value;
            if (!config.lookForNode({"Cores", core})) proceedLinking = printErrorName("Arch " + item.first, "Core");
            StringNode &coreNode = config.accessNode({"Cores", core});
            string support = coreNode.accessNode("Support").value;
            if (support[1] == '+') if (!config.lookForNode({"Versions", coreNode.accessNode("Version").value, "Configs", item.second.accessNode("Config").value})) proceedLinking = printErrorName("Arch " + item.first, "Config");
        }
        for (auto &item: config.accessNode("Servers")) {
            string arch = item.second.accessNode("Arch").value;
            if (!config.lookForNode({"Archs", arch})) proceedLinking = printErrorName("Server " + item.first, "Arch");
            StringNode &coreNode = config.accessNode({"Cores", config.accessNode({"Archs", arch, "Core"}).value});
            StringNode &version = config.accessNode({"Versions", coreNode.accessNode("Version").value});
            string support = coreNode.accessNode("Support").value;
            if (support[1] == '+') {
                for (const auto &item2: item.second.accessNode("Mods"))
                    if (item2.first.starts_with("gone") || !version.lookForNode({"Mods", item2.first})) proceedLinking = printErrorName("Server " + item.first, "Mod");
                for (const auto &item2: item.second.accessNode("UserMods"))
                    if (item2.first.starts_with("gone") || !version.lookForNode({"Mods", item2.first})) proceedLinking = printErrorName("Server " + item.first, "Mod");
            }
            if (support[0] == '+')
                for (const auto &item2: item.second.accessNode("Plugins"))
                    if (item2.first.starts_with("gone") || !version.lookForNode({"Plugins", item2.first})) proceedLinking = printErrorName("Server " + item.first, "Plugin");
        }
        if (!proceedLinking) {
            cout << "Found errors in configuration, will not proceed with linking\n";
            flushConfig(config);
            return 0;
        }
        int fixed = 0;
        int failed = 0;
        int fine = 0;
        if (options.contains("--minecraft") || options.contains("-m")) {
            fs::path dir = config.accessNode({"General", (string) "Minecraft", "Dir"}).value;
            string server = config.accessNode({"General", (string) "Minecraft", "Server"}).value;
            if (!is_directory(dir)) {
                failed++;
                cout << "Invalid minecraft directory '" << dir << "'\n";
            } else if (!config.lookForNode({"Servers", server})) {
                failed++;
                cout << "Server selected in Minecraft configuration '" << server << "' is invalid\n";
            } else {
                string core = config.accessNode({"Archs", config.accessNode({"Servers", server, "Arch"}).value, "Core"}).value;
                string version = config.accessNode({"Cores", core, "Version"}).value;
                fs::path versionDir =
                        ((fs::path) config.accessNode({"General", (string) "RootDir"}).value) / ("version/" + version);
                create_directories(versionDir);
                string support = config.accessNode({"Cores", core, "Support"}).value;
                if (support.size() != 2 || support[1] != '+') {
                    failed++;
                    cout << "Server selected in Minecraft configuration '" << server << "' doesn't seem to support mods\n";
                } else {
                    fs::path modDir = dir / "mods";
                    fs::path configDir = dir / "config";
                    fs::path versionMod = versionDir / "mods";
                    fs::path versionConfig = versionDir / ("config/" + config.accessNode(
                            {"Archs", config.accessNode({"Servers", server, "Arch"}).value, "Config"}).value);
                    create_directories(modDir);
                    create_directories(versionMod);
                    create_directories(versionConfig);
                    for (auto &item2: config.accessNode({"Servers", server, "Mods"})) {
                        string modName = config.accessNode({"Versions", version, "Mods", item2.first}).value;
                        fs::path absoluteItem = versionMod / modName;
                        fs::path absoluteLink = modDir / modName;
                        try {
                            if (is_symlink(absoluteLink) &&
                                !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in minecraft configuration to mod fixed: " << modName
                                     << endl;
                                fixed++;
                            } else if (!exists(absoluteLink)) {
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in minecraft configuration to mod created: " << modName
                                     << endl;
                                fixed++;
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to mod in minecraft configuration, location already taken: " << modName << endl;
                                failed++;
                            } else {
                                fine++;
                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in minecraft configuration to mod fixed: " << modName
                                     << endl;
                                fixed++;
                            }
                        }
                    }
                    try {
                        if (is_symlink(configDir) &&
                            !(absolute(read_symlink(configDir)) == absolute(versionConfig))) {
                            remove(configDir);
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in minecraft configuration to config folder fixed" << endl;
                            fixed++;
                        } else if (!exists(configDir)) {
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in minecraft configuration to config folder created" << endl;
                            fixed++;
                        } else if (exists(configDir) && !is_symlink(configDir)) {
                            cout << "Failed to create symlink to config folder in minecraft configuration, location already taken" << endl;
                            failed++;
                        } else {
                            fine++;
                        }
                    } catch (const fs::filesystem_error &ex) {
                        if (is_symlink(configDir)) {
                            remove(configDir);
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in minecraft configuration to config folder fixed" << endl;
                            fixed++;
                        }
                    }
                }
            }
        }
        for (auto &item: config.accessNode("Servers")) {
            fs::path dir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("server/" + item.first);
            create_directories(dir);
            string core = config.accessNode({"Archs", item.second.accessNode("Arch").value, "Core"}).value;
            fs::path coreDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("core/" + core);
            create_directories(coreDir);
            string version = config.accessNode({"Cores", core, "Version"}).value;
            fs::path versionDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("version/" + version);
            create_directories(versionDir);
            for (const auto &item2: fs::directory_iterator(coreDir)) {
                fs::path relativeItem = relative(item2.path(), coreDir);
                fs::path absoluteLink = dir / relativeItem;
                try {
                    if (is_symlink(absoluteLink) && !(absolute(read_symlink(absoluteLink)) == absolute(item2.path()))) {
                        remove(absoluteLink);
                        fs::create_symlink(absolute(item2.path()), absoluteLink);
                        cout << "Symlink in server '" << item.first << "' fixed: " << relativeItem << endl;
                        fixed++;
                    } else if (!exists(absoluteLink)) {
                        fs::create_symlink(absolute(item2.path()), absoluteLink);
                        cout << "Symlink in server '" << item.first << "' created: " << relativeItem << endl;
                        fixed++;
                    } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                        cout << "Failed to create symlink in server '" << item.first
                                  << "', location already taken: " << relativeItem << endl;
                        failed++;
                    } else {
                        fine++;
                    }
                } catch (const fs::filesystem_error &ex) {
                    if (is_symlink(absoluteLink)) {
                        remove(absoluteLink);
                        fs::create_symlink(absolute(item2.path()), absoluteLink);
                        cout << "Symlink in server '" << item.first << "' fixed: " << relativeItem << endl;
                        fixed++;
                    }
                }
            }
            string support = config.accessNode({"Cores", core, "Support"}).value;
            if (support.size() != 2) {
                failed++;
                cout << "Failed to determine server's '" << item.first << "' core support\n";
            } else {
                if (support[0] == '+') {
                    fs::path pluginDir = dir / "plugins";
                    fs::path versionPlugin = versionDir / "plugins";
                    create_directories(pluginDir);
                    create_directories(versionPlugin);
                    for (auto &item2: item.second.accessNode("Plugins")) {
                        string pluginName = config.accessNode({"Versions", version, "Plugins", item2.first}).value;
                        fs::path absoluteItem = versionPlugin / pluginName;
                        fs::path absoluteLink = pluginDir / pluginName;
                        try {
                            if (is_symlink(absoluteLink) && !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to plugin fixed: " << pluginName << endl;
                                fixed++;
                            } else if (!exists(absoluteLink)) {
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to plugin created: " << pluginName << endl;
                                fixed++;
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to plugin in server '" << item.first
                                          << "', location already taken: " << pluginName << endl;
                                failed++;
                            } else {
                                fine++;
                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to plugin fixed: " << pluginName << endl;
                                fixed++;
                            }
                        }
                    }
                }
                if (support[1] == '+') {
                    fs::path modDir = dir / "mods";
                    fs::path configDir = dir / "config";
                    fs::path versionMod = versionDir / "mods";
                    fs::path versionConfig = versionDir / ("config/" + config.accessNode({"Archs", item.second.accessNode("Arch").value, "Config"}).value);
                    create_directories(modDir);
                    create_directories(versionMod);
                    create_directories(versionConfig);
                    for (auto &item2: item.second.accessNode("Mods")) {
                        string modName = config.accessNode({"Versions", version, "Mods", item2.first}).value;
                        fs::path absoluteItem = versionMod / modName;
                        fs::path absoluteLink = modDir / modName;
                        try {
                            if (is_symlink(absoluteLink) && !(absolute(read_symlink(absoluteLink)) == absolute(absoluteItem))) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to mod fixed: " << modName << endl;
                                fixed++;
                            } else if (!exists(absoluteLink)) {
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to mod created: " << modName << endl;
                                fixed++;
                            } else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
                                cout << "Failed to create symlink to mod in server '" << item.first
                                          << "', location already taken: " << modName << endl;
                                failed++;
                            } else {
                                fine++;
                            }
                        } catch (const fs::filesystem_error &ex) {
                            if (is_symlink(absoluteLink)) {
                                remove(absoluteLink);
                                fs::create_symlink(absolute(absoluteItem), absoluteLink);
                                cout << "Symlink in server '" << item.first << "' to mod fixed: " << modName << endl;
                                fixed++;
                            }
                        }
                    }
                    try {
                        if (is_symlink(configDir) && !(absolute(read_symlink(configDir)) == absolute(versionConfig))) {
                            remove(configDir);
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in server '" << item.first << "' to config folder fixed" << endl;
                            fixed++;
                        } else if (!exists(configDir)) {
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in server '" << item.first << "' to config folder created" << endl;
                            fixed++;
                        } else if (exists(configDir) && !is_symlink(configDir)) {
                            cout << "Failed to create symlink to config folder in server '" << item.first
                                      << "', location already taken" << endl;
                            failed++;
                        } else {
                            fine++;
                        }
                    } catch (const fs::filesystem_error &ex) {
                        if (is_symlink(configDir)) {
                            remove(configDir);
                            fs::create_symlink(absolute(versionConfig), configDir);
                            cout << "Symlink in server '" << item.first << "' to config folder fixed" << endl;
                            fixed++;
                        }
                    }
                }
            }
        }
        cout << "Fixed " << fixed << " symlinks, failed to fix " << failed << " symlinks, okay " << fine << " symlinks\n";
        flushConfig(config);
    } else if (arguments[0] == "make") {
        if (arguments.size() != 3) exitWithUsage();
        StringNode config{};
        parseConfig(config);
        if (config.lookForNode({"Servers", arguments[2]}))
            printError("Server with name '" + arguments[2] + "' already exists\n");
        if (!config.lookForNode({"Archs", arguments[1]}))
            printError("Arch with name '" + arguments[1] + "' doesn't exist\n");
        config.accessNode({"Servers", arguments[2], "Arch"}).value = arguments[1];
        cout << "Please, run 'mserman verify' to finish creation\n";
        flushConfig(config);
    } else if (arguments[0] == "backup") {
        if (arguments.size() != 3) exitWithUsage();
        //TODO: Implement loading and '-a' support
        StringNode config{};
        parseConfig(config);
        if (!config.lookForNode({"Servers", arguments[2]}))
            printError("Server with name '" + arguments[2] + "' doesn't exist\n");
        if (arguments[1] == "load") {
            fs::path archivePath = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("backup/" + arguments[2] + ".bak");
            fs::path serverDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("server/" + arguments[2] + "/");
            if (!exists(archivePath))
                printError("Server '" + arguments[2] + "' doesn't have a backup\n");
            while (true) {
                char choice;
                cout << "Do you want to proceed? (y/n): ";
                cin >> choice;

                if (choice == 'y' || choice == 'Y') break;
                else if (choice == 'n' || choice == 'N') printError("Aborted\n");
                else cout << "Invalid choice. Please enter 'y' or 'n'." << endl;
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
            if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err) + "\n");
        } else if (arguments[1] == "save") {
            while (true) {
                char choice;
                cout << "Do you want to proceed? (y/n): ";
                cin >> choice;

                if (choice == 'y' || choice == 'Y') break;
                else if (choice == 'n' || choice == 'N') printError("Aborted\n");
                else cout << "Invalid choice. Please enter 'y' or 'n'." << endl;
            }
            fs::path archivePath = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("backup/" + arguments[2] + ".bak");
            fs::path serverDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("server/" + arguments[2] + "/");
            zip_t *archive = zip_open(archivePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
            for (const auto &item: fs::directory_iterator(serverDir)) {
                if (item.is_directory() && item.path().filename().string().starts_with("world")) {
                    function<void(const fs::path&)> recFunc = [&archive, &serverDir, &recFunc](const fs::path &dir){
                        for (const auto &item2: fs::directory_iterator(dir)) {
                            if (item2.is_regular_file()) {
                                zip_source_t *source = zip_source_file(archive, item2.path().c_str(), 0, 0);
                                if (!source) printError("Was unable to open file as zip source\n");
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
            if (zip_get_num_entries(archive, 0) == 0) printError("No world files found\n");
            if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err) + "\n");
        } else exitWithUsage();
        flushConfig(config);
    } else if (arguments[0] == "collect") {
        if (arguments.size() != 2) exitWithUsage();
        StringNode config{};
        parseConfig(config);
        if (!config.lookForNode({"Servers", arguments[1]}))
            printError("Server with name '" + arguments[1] + "' doesn't exist\n");
        string arch = config.accessNode({"Servers", arguments[1], "Arch"}).value;
        string core = config.accessNode({"Archs", arch, "Core"}).value;
        if (config.accessNode({"Cores", core, "Support"}).value[1] != '+')
            printError("Server with name '" + arguments[1] + "' doesn't use mods\n");
        zip_t *archive = zip_open((arguments[1] + "_user.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
        if (!archive) printError("Was unable to make archive");
        string version = config.accessNode({"Cores", core, "Version"}).value;
        StringNode &versionNode = config.accessNode({"Versions", version, "Mods"});
        fs::path versionsDir = ((fs::path)config.accessNode({"General", (string) "RootDir"}).value) / ("version/" + version);
        fs::path modsDir = versionsDir / "mods/";
        fs::path configDir = versionsDir / ("config/" + config.accessNode({"Archs", arch, "Config"}).value);
        function<void(const fs::path&)> recFunc = [&archive, &configDir, &recFunc](const fs::path &dir){
            for (const auto &item2: fs::directory_iterator(dir)) {
                if (item2.is_regular_file()) {
                    zip_source_t *source = zip_source_file(archive, item2.path().c_str(), 0, 0);
                    if (!source) printError("Was unable to open file as zip source\n");
                    zip_file_add(archive, ("config" / relative(item2.path(), configDir)).c_str(), source, ZIP_FL_OVERWRITE);
                } else if (item2.is_directory()) {
                    zip_dir_add(archive, ("config" / relative(item2.path(), configDir)).c_str(), 0);
                    recFunc(item2.path());
                }
            }
        };
        zip_dir_add(archive, "config", 0);
        recFunc(configDir);
        zip_dir_add(archive, "mods", 0);
        for (auto &item: config.accessNode({"Servers", arguments[1], "UserMods"})) {
            string realName = versionNode.accessNode(item.first).value;
            fs::path file = modsDir / realName;
            if (!fs::is_regular_file(file)) printError("One of mods is not a regular file\n");
            string filePath = file.string();

            struct zip_source *source = zip_source_file(archive, filePath.c_str(), 0, 0);
            if (!source) printError("Was unable to open mod file as zip source\n");

            zip_file_add(archive, ("mods/" + realName).c_str(), source, ZIP_FL_OVERWRITE);
        }
        if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err) + "\n");
    } else if (arguments[0] == "boot") {
        if (arguments.size() != 2) exitWithUsage();
        StringNode config{};
        parseConfig(config);
        if (!config.lookForNode({"Servers", arguments[1]}))
            printError("Server with name '" + arguments[1] + "' doesn't exist\n");
        pid_t child_pid = fork();
        if (child_pid == -1) {
            cerr << "Fork failed." << endl;
            return 1;
        } else if (child_pid == 0) {
            StringNode &core = config.accessNode({"Cores", config.accessNode({"Archs", config.accessNode({"Servers", arguments[1], "Arch"}).value, "Core"}).value});
            dup2(STDOUT_FILENO, STDERR_FILENO);
            dup2(STDIN_FILENO, STDIN_FILENO);
            fs::path pathTo = ((fs::path)config.accessNode({"General", (string)"RootDir"}).value) / ("server/" + arguments[1] + "/");
            fs::current_path(pathTo);
            execlp((((fs::path)core.accessNode("JavaPath").value) / "java").c_str(), "java", "-jar", core.accessNode("Core").value.c_str(), NULL);
            cerr << "Exec failed." << endl;
            return 1;
        } else {
            int status;
            waitpid(child_pid, &status, 0);
            if (WIFEXITED(status)) {
                cout << "Child process exited with status " << WEXITSTATUS(status) << endl;
            }
        }
    } else exitWithUsage();
    return 0;
}

void exitWithUsage() {
    cout << "Minecraft SERver MANager - Utility for easy server managements"
            "Usage: mserman [options...] [--] <command>\n"
            "Commands:\n"
            "    [-m] verify                       Verify validity of data\n"
            "    make <arch> <name>                Create Minecraft server\n"
            "    -a backup (save|load)             Load/backup all worlds\n"
            "    backup (save|load) <server>       Load/backup world\n"
            "    collect <server>                  Collect user mods in archive\n"
            "    boot <server>                     Start Minecraft server\n"
            "    schedule <server> <time>          Schedule Minecraft server\n"
            "Options:\n"
            "    -a    --all        Select all\n"
            "    -h    --help       Display this window\n"
            "    -m    --minecraft  Only link in Minecraft folder\n";
    exit(EXIT_FAILURE);
}
void printError(const string &message) {
    cerr << message;
    exit(EXIT_FAILURE);
}
bool printErrorName(const string &nameWhere, const string &nameFor) {
    cerr << "Invalid name for '" << nameFor << "' in '" << nameWhere <<"'\n";
    return false;
}

#pragma clang diagnostic pop