#include "main.hpp"
#include <iostream>
#include <fstream>

void stat_node::operator++(int) { // NOLINT(*-no-recursion)
    value++;
    if (parent)
        (*parent)++;
}
stat_node &stat_node::operator[](const string &index) {
    if (isNull()) return nullObject;
    if (!children.contains(index)) children.emplace(index, stat_node(this));
    return children[index];
}
path_node &path_node::operator[](const string &index) {
    if (isNull()) return nullObject;
    if (!children.contains(index)) children.emplace(index, value.empty() ? path_node() : path_node(value / index));
    return children[index];
}
string &string_node::upper() {
    transform(value.begin(), value.end(), value.begin(), ::toupper);
    return value;
}
string &string_node::lower() {
    return stringToLower(value);
}
int string_node::asCode(int bits, int default_value) {
    int result = 0;
    for (int i = 0; i < min((int) value.size(), bits); ++i)
        if (value[i] == '+') result |= 1 << i;
    for (int i = (int) value.size(); i < bits; ++i)
        if (default_value & (1 << i)) result |= 1 << i;
    return result;
}
void string_node::toCode(int value1, int bits) {
    string result;
    for (int i = 0; i < bits; ++i)
        result += (value1 & (1 << i)) ? '+' : '-';
    value = result;
}

#define SA(...) __VA_ARGS__
#define TEMPLATED_NODE(result, name, args) template<typename Value, typename Self> \
result node<Value, Self>::name args

TEMPLATED_NODE(Self &, operator[], (const vector<string> &path)) {
    if (isNull()) return nullObject;
    Self *currentNode = (Self *) this;
    for (const string& nodeName : path)
        currentNode = &((*currentNode)[nodeName]);
    return *currentNode;
}
TEMPLATED_NODE(Self &, operator[], (const string_node &index)) {
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
TEMPLATED_NODE(const Self &, operator(), (const string_node &index) const) {
    return this->operator()(index.value);
}
TEMPLATED_NODE(const Self &, operator(), (const string &index) const) {
    if (isNull()) return nullObject;
    if (auto ptr = children.find(index); ptr == children.end())
        return nullObject;
    else return ptr->second;
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

string &stringToLower(string &str) {
    transform(str.begin(), str.end(), str.begin(), ::tolower);
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
void flushConfig(string_node &config, ostream &stream, bool format, bool compact) {
    function<void(string_node&, const string&)> lambda = [&stream,&lambda, &format, &compact](string_node& rootNode, const string &level) {
        bool needTab = true, rootSize = rootNode.children.size() > 1 || !compact;
        for (auto &[key, child]: rootNode) {
            stream << (format && needTab && rootSize ? level : "") << writeWord(key);
            if (!child.value.empty() || !child.children.empty()) {
                if (!child.value.empty()) stream << " = " << writeWord(child.value) << (format ? (child.children.empty() ? "\n" : " < ") : (child.children.empty() ? "" : " < "));
                if (!child.children.empty()) {
                    if (format) {
                        bool size = child.children.size() > 1 || !compact;
                        if (size) stream << "\n";
                        lambda(child, size ? level + "    " : level);
                        stream << (rootSize ? level : "") << "> ";
                        needTab = false;
                    } else {
                        lambda(child, "");
                        stream << "> ";
                    }
                    continue;
                }
            } else stream << (format ? " >\n" : " > ");
            needTab = true;
        }
        if (format && !needTab && rootSize) stream << "\n";
    };
    lambda(config, "");
    stream.flush();
}
void flushConfig(string_node &config, const fs::path &path, bool format, bool compact) {
    ofstream file{path};
    if (!file.is_open()) printError("Failed to open " + path.filename().string() + " for writing.\n");
    flushConfig(config, file, format, compact);
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
void parseConfig(string_node &config, istream &stream) {
    vector<string> path;
    string dropped;
    string word;
    while (readWord(stream, word)) {
        if (word == "<")
            path.push_back(dropped);
        else if (word == "=" || word == ">") {
            if (word == "=") readWord(stream, config[path].value);
            if (path.empty()) printError("Expected 'eof', got '>'\n");
            dropped = path.back();
            path.pop_back();
        } else {
            path.push_back(word);
            config[path];
        }
    }
    if (!path.empty()) printError("Expected '>'x" + to_string(path.size()) + ", got 'eof'.\n");
}
void parseConfig(string_node &config, const fs::path &filePath, bool mkDef) {
    if (!fs::exists(filePath)) {
        if (!mkDef) return;
        ofstream file{filePath};
        if (!file.is_open()) printError("Failed to create default " + filePath.filename().string() + " config.\n");
        file << "GENERAL ROOT-DIR = data BACKUP-DIR = backup >";
        file.close();
    }
    ifstream file{filePath};
    if (!file.is_open()) printError("Failed to open " + filePath.filename().string() + " for reading.\n");
    parseConfig(config, file);
    file.close();
}
void flushConfig(root_pack &root, bool write_hash, bool format, bool compact) {
    root.cache[".COMMENT1"].value = "GENERATED BY PROGRAM. VERIFICATION DATA";
    root.cache[".COMMENT2"].value = "EDIT AT YOUR OWN RISK!! Changing this data may cause DATA LOSS,";
    root.cache[".COMMENT3"].value = "or INSTABILITY in program behaviour.";
    root.cache[".COMMENT4"].value = "Data here is NOT verified before usage!";
    root.cache[".COMMENT5"].value = "However, deleting this file IS SAFE";
    if (write_hash) setHash(root);
    flushConfig(root.root, "mserman.local", format, compact);
    flushConfig(root.cache, "cache.dat", format, compact);
    auto string_stats = root.stats.convert<string_node>(+([](const int &toMap) {return to_string(toMap);}));
    auto string_paths = root.paths.convert<string_node>(+([](const fs::path &toMap) {return (string) toMap;}));
    string_stats["FILES"] = string_paths;
    flushConfig(string_stats, "report.log", format, compact);
}
bool parseConfig(root_pack &root) {
    parseConfig(root.root, "mserman.local");
    parseConfig(root.cache, "cache.dat", false);
    root.paths["GENERAL"] = path_node(root.root["GENERAL"]["ROOT-DIR"].value);
    root.paths["BACKUP"] = path_node(root.root["GENERAL"]["BACKUP-DIR"].value);
    root.paths["MINECRAFT"] = path_node(root.root["GENERAL"]["MINECRAFT"].value);
    return checkHash(root);
}

void parseJsonInternal(string_node &config, const Json::Value &root) { // NOLINT(*-no-recursion)
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
void parseJson(string_node &config, istream &from) {
    Json::CharReaderBuilder reader;
    Json::Value root;
    Json::parseFromStream(reader, from, &root, nullptr);
    parseJsonInternal(config, root);
}

bool checkHash(root_pack &root) {
    try {
        if(!root.cache("HASH-GEN")("VALUE")) {
            return false;
        }
        size_t value = stoul(root.cache["HASH-GEN"]["VALUE"].value, nullptr, 16);
        size_t seed = root.root.hash();
        seed ^= root.cache.hash() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
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
void setHash(root_pack &root) {
    size_t seed = root.root.hash();
    seed ^= root.cache.hash() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    root.cache["HASH-GEN"]["VALUE"].value = ultos(seed);
}

//thread_pool::thread_pool(size_t threadCount) {
//    for (int i = 0; i < threadCount; ++i) {
//        workers.emplace_back([this] {worker_thread();});
//    }
//}
//thread_pool::~thread_pool() {
//    {
//        unique_lock lock(mx);
//        state |= 1;
//    }
//    condition.notify_all();
//    for (auto &worker: workers) {
//        worker.join();
//    }
//}
//void thread_pool::wait(int by) {
//    {
//        unique_lock lock(mx);
//        state |= 2;
//        main_condition.wait(lock, [this, &by] { return scheduled[by] == 0; });
//        state ^= 2;
//    }
//}
//bool thread_pool::enqueue(function<void()> task, int by) {
//    {
//        unique_lock lock(mx);
//        if (state & 1) return false;
//        tasks.emplace_back(std::move(task), by);
//        scheduled[by]++;
//    }
//    condition.notify_one();
//    return true;
//}
//void thread_pool::worker_thread() {
//    while (true) {
//        pair<function<void()>, int> task;
//        {
//            unique_lock lock(mx);
//            while (tasks.empty()) {
//                if (state & 2) {
//                    lock.unlock();
//                    main_condition.notify_one();
//                    lock.lock();
//                }
//                condition.wait(lock);
//                if (state & 1 && tasks.empty())
//                    return;
//            }
//            task = std::move(tasks.front());
//            tasks.pop_front();
//            lock.unlock();
//            task.first();
//            lock.lock();
//            scheduled[task.second]--;
//        }
//    }
//}
