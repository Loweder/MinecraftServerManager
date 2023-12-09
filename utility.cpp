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
#define TEMPLATED_NODE(result, name, args) template<typename Value, typename Self> \
result Node<Value, Self>::name args

TEMPLATED_NODE(Self &, operator[], (const vector<string> &path)) {
    if (isNull()) return nullObject;
    Self *currentNode = (Self *) this;
    for (const string& nodeName : path)
        currentNode = &((*currentNode)[nodeName]);
    return *currentNode;
}
TEMPLATED_NODE(Self &, operator[], (const StringNode &index)) {
    return this->operator[](index.value);
}
TEMPLATED_NODE(Self &, operator[], (const string &index)) {
    if (isNull()) return nullObject;
    if (!children.contains(index)) children.emplace(index, Self());
    return children[index];
}

TEMPLATED_NODE(const Self &, operator(), (const vector<string> &path) const) {
    if (isNull()) return nullObject;
    const Self *currentNode = (Self *) this;
    for (const string& nodeName : path)
        currentNode = &(currentNode->operator()(nodeName));
    return *currentNode;
}
TEMPLATED_NODE(const Self &, operator(), (const StringNode &index) const) {
    return this->operator()(index.value);
}
TEMPLATED_NODE(const Self &, operator(), (const string &index) const) {
    if (isNull()) return nullObject;
    if (!children.contains(index))
        return nullObject;
    return children.find(index)->second;
}

TEMPLATED_NODE(bool, val, (const Value &index) const) {
    if (isNull()) return false;
    return any_of(children.begin(), children.end(), [&index](const pair<string, Self> &item) {
        return item.second.value == index;
    });
}
TEMPLATED_NODE(, operator bool, () const) {
    return !isNull();
}

TEMPLATED_NODE(SA(map<string, Self>::iterator), end, ()) {
    return children.end();
}
TEMPLATED_NODE(SA(map<string, Self>::iterator), begin, ()){
    return children.begin();
}

TEMPLATED_NODE(size_t, hash, () const) {
    std::hash<string> keyHashGen;
    std::hash<Value> hashGen;
    size_t seed = hashGen(value);
    for (const auto &[key, eValue]: children) {
        if (key == "HASH-GEN") continue;
        seed ^= keyHashGen(key) + 0x9e3779b9 + (seed << 6) + (seed >> 2) ^ eValue.hash();
    }
    return seed;
}

TEMPLATED_NODE(bool, isNull, () const) {
    return ((Self*)this)==&nullObject;
}
TEMPLATED_NODE(Self, nullObject,);

void exitWithUsage() {
    auto a = set<string>();
    auto b = vector<string>();
    exit(helpOp(a, b));
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
    if (word.contains(' ') || word.contains('\''))
        return '"' + word + '"';
    else if (word.empty())
        return "\"\"";
    else
        return word;
}
void flushConfig(StringNode &config, const fs::path &path, bool format, bool compact) {
    ofstream file{path};
    if (!file.is_open()) printError("Failed to open " + path.filename().string() + " for writing.\n");
    function<void(StringNode&, const string&)> lambda = [&file,&lambda, &format, &compact](StringNode& rootNode, const string &level) {
        bool needTab = true, rootSize = rootNode.children.size() > 1 || !compact;
        for (auto &[key, child]: rootNode) {
            file << (format && needTab && rootSize ? level : "") << writeWord(key);
            if (!child.value.empty() || !child.children.empty()) {
                if (!child.value.empty()) file << " = " << writeWord(child.value) << (format ? (child.children.empty() ? "\n" : " < ") : (child.children.empty() ? "" : " < "));
                if (!child.children.empty()) {
                    if (format) {
                        bool size = child.children.size() > 1 || !compact;
                        if (size) file << "\n";
                        lambda(child, size ? level + "    " : level);
                        file << (rootSize ? level : "") << "> ";
                        needTab = false;
                    } else {
                        lambda(child, "");
                        file << "> ";
                    }
                    continue;
                }
            } else file << (format ? " >\n" : " > ");
            needTab = true;
        }
        if (format && !needTab && rootSize) file << "\n";
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
void parseConfig(StringNode &config, const fs::path &filePath, bool mkDef) {
    if (!fs::exists(filePath)) {
        if (!mkDef) return;
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

void parseJsonInternal(StringNode &config, const Json::Value &root) { // NOLINT(*-no-recursion)
    if (root.isArray()) {
        int index = 0;
        for (const auto &item: root) {
            parseJsonInternal(config[to_string(index++)], item);
        }
    } else if (root.isObject()) {
        for (const auto &item: root.getMemberNames()) {
            parseJsonInternal(config[item], root.get(item, Json::Value::null));
        }
    } else {
        config.value = root.asString();
    }
}
void parseJson(StringNode &config, istream &from) {
    Json::CharReaderBuilder reader;
    Json::Value root;
    Json::parseFromStream(reader, from, &root, nullptr);
    parseJsonInternal(config, root);
}

bool checkHash(StringNode &root, StringNode &cache) {
    try {
        if(!cache("HASH-GEN")("VALUE")) {
            return false;
        }
        size_t value = stoul(cache["HASH-GEN"]["VALUE"].value, nullptr, 16);
        size_t seed = root.hash();
        seed ^= cache.hash() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return value == seed;
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
void setHash(StringNode &root, StringNode &cache) {
    size_t seed = root.hash();
    seed ^= cache.hash() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    cache["HASH-GEN"]["VALUE"].value = ultos(seed);
}
