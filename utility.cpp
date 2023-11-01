#include "main.hpp"
#include <iostream>
#include <fstream>

void StatNode::operator++(int) { // NOLINT(*-no-recursion)
    value++;
    if (parent)
        (*parent)++;
}
StatNode& StatNode::operator[](const string &index) {
    if (isNull()) return nullObject;
    if (!children.contains(index)) children.emplace(index, StatNode(this));
    return children[index];
}

#define SA(...) __VA_ARGS__
#define TEMPLATED_NODE(result, name) template<typename type, typename self> \
result Node<type, self>::name

TEMPLATED_NODE(self &, operator[](const vector<string> &path)) {
    if (isNull()) return nullObject;
    self *currentNode = (self *) this;
    for (const string& nodeName : path)
        currentNode = &(currentNode->operator[](nodeName));
    return *currentNode;
}
TEMPLATED_NODE(self &, operator[](const StringNode &index)) {
    return this->operator[](index.value);
}
TEMPLATED_NODE(self &, operator[](const string &index)) {
    if (isNull()) return nullObject;
    if (!children.contains(index)) children.emplace(index, self());
    return children[index];
}

TEMPLATED_NODE(const self &, operator()(const vector<string> &path) const) {
    if (isNull()) return nullObject;
    const self *currentNode = (self *) this;
    for (const string& nodeName : path)
        currentNode = &(currentNode->operator()(nodeName));
    return *currentNode;
}
TEMPLATED_NODE(const self &, operator()(const StringNode &index) const) {
    return this->operator()(index.value);
}
TEMPLATED_NODE(const self &, operator()(const string &index) const) {
    if (isNull()) return nullObject;
    if (!children.contains(index))
        return nullObject;
    return children.find(index)->second;
}

TEMPLATED_NODE(bool, val(const type &index) const) {
    if (isNull()) return false;
    return any_of(children.begin(), children.end(), [&index](const pair<string, self> &item) {
        return item.second.value == index;
    });
}
TEMPLATED_NODE(, operator bool() const) {
    return !isNull();
}

TEMPLATED_NODE(SA(map<string, self>::iterator), end()) {
    return children.end();
}
TEMPLATED_NODE(SA(map<string, self>::iterator), begin()){
    return children.begin();
}

TEMPLATED_NODE(size_t, hash() const) {
    std::hash<string> keyHashGen;
    std::hash<type> hashGen;
    size_t seed = hashGen(value);
    for (const auto &item: children) {
        if (item.first == "HASH-GEN") continue;
        seed ^= keyHashGen(item.first) + 0x9e3779b9 + (seed << 6) + (seed >> 2) ^ item.second.hash();
    }
    return seed;
}

TEMPLATED_NODE(bool, isNull() const) {
    return ((self*)this)==&nullObject;
}
TEMPLATED_NODE(self, nullObject);

void exitWithUsage() {
    //TODO: Add reset/delete/edit/sort/schedule functionality, implement no-report, no-hash-gen, simulate. Add support for 1 letter arguments
    //  Add "no comment gen", machine cache file, clearing other files in mods/configs
    cerr << "Minecraft SERver MANager - Utility for easy server managements"
            "Usage: mserman [options...] [--] <command>\n"
            "Commands:\n"
            "    boot <server>                      Start Minecraft server\n"
            "    schedule <server> <time>           Schedule Minecraft server\n"
            "    edit <path> <value>                Edit data in config\n"
            "    sort                               Sort data in config\n"
            "    [-m|s] verify                      Verify data in config\n"
            "    [-y] reset [hard]                  Clean data in config (CAUTION! Dangerous)\n"
            "    make <server> <core>               Create server\n"
            "    [-y] delete <server>               Delete server (CAUTION! Dangerous)\n"
            "    -a[y] backup (save|load)           Load/backup all worlds (CAUTION! Dangerous)\n"
            "    [-y] backup (save|load) <server>   Load/backup world (CAUTION! Dangerous)\n"
            "    collect <server>                   Collect user mods in archive\n"
            "Options:\n"
            "    -h    --help                       Display this window\n"
            "    -v    --version                    Display version\n"
            "    -a    --all                        Select all\n"
            "    -s    --simulate                   Simulate server with every mod/plugin to get dependencies/ids\n"
            "    -m    --minecraft                  Only link in Minecraft folder\n"
            "    -n    --no-report                  Do not generate verification report\n"
            "    -y    --force-yes                  Do not ask for confirmation (CAUTION! Dangerous)\n"
            "    -g    --ignore-hash-gen            Do not verify HASH-GEN value. Only use this if you know what you're doing\n";
    exit(EXIT_FAILURE);
}
void printError(const string &message) {
    cerr << message << endl;
    exit(EXIT_FAILURE);
}

int stringToCode(const string &str, int bits, int defaultValue) {
    int result = 0;
    for (int i = 0; i < min((int) str.size(), bits); ++i)
        if (str[i] == '+') result |= 1 << i;
    for (int i = (int) str.size(); i < bits; ++i)
        if (defaultValue & (1 << i)) result |= 1 << i;
    return result;
}
string codeToString(int val, int bits) {
    string result;
    for (int i = 0; i < bits; ++i)
        result += (val & (1 << i)) ? '+' : '-';
    return result;
}
string &stringToUpper(string &str) {
    transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

string writeWord(const string &word) {
    if (word.contains(' '))
        return '"' + word + '"';
    else
        return word;
}
void flushConfig(StringNode &config, const fs::path &path, bool format) {
    ofstream file{path};
    if (!file.is_open()) printError("Failed to open " + path.filename().string() + " for writing.\n");
    function<void(StringNode&, const string&)> lambda = [&file,&lambda, &format](StringNode& rootNode, const string &level) {
        bool needTab = true;
        for (auto &item: rootNode) {
            file << (format && needTab ? level : "") << writeWord(item.first);
            StringNode &node = item.second;
            if (!node.value.empty() || !node.children.empty()) {
                if (!node.value.empty()) file << " = " << writeWord(node.value) << (format ? (node.children.empty() ? "\n" : " <") : (node.children.empty() ? "" : " < "));
                if (!node.children.empty()) {
                    if (format) {
                        file << "\n";
                        lambda(item.second, level + "    ");
                        file << level << "> ";
                        needTab = false;
                    } else {
                        lambda(item.second, "");
                        file << "> ";
                    }
                    continue;
                }
            } else file << (format ? " >\n" : " > ");
            needTab = true;
        }
        if (format && !needTab) file << "\n";
    };
    lambda(config, "");
    file.flush();
    file.close();
}
bool readWord(istream &file, string &word) {
    bool res = (bool) (file >> word);
    if (word.starts_with('"') && (!word.ends_with('"') || word.size() == 1)) {
        string buf;
        do {
            res = (bool) (file >> buf);
            word += " " + buf;
        } while (!buf.ends_with('"'));
    }
    if (word.starts_with('"') && word.ends_with('"'))
        word = word.substr(1, word.size() - 2);
    return res;
}
void parseConfig(StringNode &config, const fs::path &filePath) {
    if (!fs::exists(filePath)) {
        ofstream file{filePath};
        if (!file.is_open()) printError("Failed to create default " + filePath.filename().string() + " config.\n");
        file << "GENERAL ROOT-DIR = data >";
        file.close();
    }
    ifstream file{filePath};
    if (!file.is_open()) printError("Failed to open " + filePath.filename().string() + " for reading.\n");
    vector<string> path;
    string dropped;
    string word;
    while (readWord(file, word)) {
        if (word == "<")
            path.push_back(dropped);
        else if (word == "=" || word == ">") {
            if (word == "=") readWord(file, config[path].value);
            if (path.empty()) printError("Expected 'eof', got '>'\n");
            dropped = path.back();
            path.pop_back();
        } else {
            path.push_back(word);
            config[path];
        }
    }
    if (!path.empty()) printError("Expected '>'x" + to_string(path.size()) + ", got 'eof'.\n");
    file.close();
}

bool checkHash(StringNode &root) {
    try {
        if(!root("HASH-GEN")("VALUE")) {
            return false;
        }
        size_t value = stoul(root["HASH-GEN"]["VALUE"].value, nullptr, 16);
        return value == root.hash();
    } catch (exception const& ex) {
        return false;
    }
}
string ultos(unsigned long value) {
    std::string result;
    while (value > 0) {
        size_t digit = value % 16;
        char digitChar = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
        result.insert(0, 1, digitChar);
        value /= 16;
    }

    return result;
}
void setHash(StringNode &root) {
    root["HASH-GEN"]["VALUE"].value = ultos(root.hash());
    root["HASH-GEN"]["_COMMENT1"].value = "GENERATED BY HASH-GEN. DO NOT TOUCH!";
    root["HASH-GEN"]["_COMMENT2"].value = "CHANGING THIS VALUE WILL INVALIDATE YOUR CONFIG";
}
