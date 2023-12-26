#ifndef MAIN_HPP
#define MAIN_HPP

#include "bit_defines.hpp"
#include "json/json.h"
#include <string>
#include <filesystem>
#include <map>
#include <vector>
#include <set>
#include <functional>

using namespace std;
namespace fs = filesystem;

void exitWithUsage() __attribute__ ((__noreturn__));
void printError(const string &message) __attribute__ ((__noreturn__));

string &stringToLower(string &str) __attribute__ ((pure));

struct string_node;

template<typename Value, typename Self>
struct node {
    using value_type [[maybe_unused]] = Value;
    map<string, Self> children;
    Value value{};
    virtual ~node() = default;
    Self &operator[](const vector<string> &path);
    Self &operator[](const string_node &index);
    virtual Self &operator[](const string &index);

    [[nodiscard]] const Self &operator()(const vector<string>& path) const;
    [[nodiscard]] const Self &operator()(const string_node &index) const;
    [[nodiscard]] virtual const Self &operator()(const string &index) const;

    [[nodiscard]] virtual explicit operator bool() const;

    typename map<string, Self>::iterator begin();
    typename map<string, Self>::iterator end();

    template<typename To>
    To convert(typename To::value_type(*mapper)(const Value&)) { // NOLINT(*-no-recursion)
        To result;
        result.value = mapper(value);
        for (auto &item: *this)
            result[item.first] = item.second.template convert<To>(mapper);
        return result;
    }
    [[nodiscard]] size_t hash() const;
protected:
    static Self nullObject;
    [[nodiscard]] bool isNull() const;
};
struct stat_node: node<int, stat_node> {
    stat_node(): parent(nullptr) {};
    void operator++(int);
    using node::operator[];
    stat_node &operator[](const string &index) override;
private:
    explicit stat_node(stat_node *parent): parent(parent) {};
    stat_node* const parent;
};
struct path_node: node<fs::path, path_node> {
    path_node() = default;
    explicit path_node(fs::path &&input) {value = input;}
    using node::operator[];
    path_node &operator[](const string &index) override;
};
struct string_node: node<string, string_node> {
    string &upper();
    string &lower();
    int asCode(int bits, int default_value);
    void toCode(int value, int bits);
};
template struct node<string, string_node>;
template struct node<int, stat_node>;
template struct node<fs::path, path_node>;

// TODO I don't have time for this now
//struct thread_pool {
//    thread_pool(size_t threadCount);
//    ~thread_pool();
//    bool enqueue(function<void()> task);
//    void wait();
//private:
//    void worker_thread();
//    vector<thread> workers;
//    deque<function<void()>> tasks;
//    mutex mx;
//    condition_variable condition;
//    condition_variable main_condition;
//    int state = 0;
//};

struct root_pack {
    string_node root;
    string_node cache;
    path_node paths;
    stat_node stats;
};

void flushConfig(string_node &config, ostream &stream, bool format=true, bool compact=true);
void parseConfig(string_node &config, istream &stream);
void flushConfig(string_node &config, const fs::path &path, bool format=true, bool compact=true);
void parseConfig(string_node &config, const fs::path &filePath, bool mkDef=true);
void flushConfig(root_pack &root, bool write_hash=true, bool format=true, bool compact=true);
bool parseConfig(root_pack &root);
void parseJson(string_node &config, istream &from);
bool checkHash(root_pack &root);
void setHash(root_pack &root);

using operation = int(*)(set<string>&,vector<string>&);
int sortOp(set<string> &options, vector<string> &arguments);
int switchOp(set<string> &options, vector<string> &arguments);
int verifyOp(set<string> &options, vector<string> &arguments);
int importOp(set<string> &options, vector<string> &arguments);
int makeOp(set<string> &options, vector<string> &arguments);
int collectOp(set<string> &options, vector<string> &arguments);
int backupOp(set<string> &options, vector<string> &arguments);
int bootOp(set<string> &options, vector<string> &arguments);
int versionOp(set<string> &options, vector<string> &arguments);
int helpOp(set<string> &options, vector<string> &arguments);
int exitOp(set<string> &options, vector<string> &arguments);
int nullOp(set<string> &options, vector<string> &arguments);

inline path_node &ensureExists(path_node &path) {
    create_directories(path.value);
    return path;
}
inline string mapOption(const string &raw) {
    static const unordered_map<string, string> optionMap = {
            {"v", "functional-version"},
            {"h", "functional-help"},
            {"a", "all"},
            {"m", "minecraft"},
            {"d", "deep"},
            {"y", "force-yes"},
            {"--version", "functional-version"},
            {"--help", "functional-help"},
            {"--all", "all"},
            {"--force-root", "force-root"},
            {"--minecraft", "minecraft"},
            {"--deep", "deep"},
            {"--force-yes", "force-yes"},
            {"n", "no-report"},
            {"--no-report", "no-report"}
    };
    auto it = optionMap.find(raw);
    if (it != optionMap.end())
        return it->second;
    return "custom-" + ((raw.size() == 1 ? "s-" : "m-") + raw);
}
constexpr operation mapOperation(const string &name) {
    if (name == "switch") return switchOp;
    if (name == "boot") return bootOp;
    if (name == "sort") return sortOp;
    if (name == "verify") return verifyOp;
    if (name == "collect") return collectOp;
    if (name == "import") return importOp;
    if (name == "make") return makeOp;
    if (name == "backup") return backupOp;
    if (name == "help") return helpOp;
    if (name == "version") return versionOp;
    if (name == "exit") return exitOp;
    return nullOp;
}

pid_t forkToServer(root_pack &root, string &server, int input, int output, int error);
set<pair<string, string>> collectPack(root_pack &root, string_node &module, const string &mc_version, int side);


//TODO will be used soon
//extern uint64_t flags;

#endif //MAIN_HPP
