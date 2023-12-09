#ifndef MAIN_HPP
#define MAIN_HPP

#include "cmake-build-debug/version.h"
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

int stringToCode(const string &str, int bits, int defaultValue) __attribute__ ((pure));
string codeToString(int val, int bits) __attribute__ ((pure));
string &stringToUpper(string &str) __attribute__ ((pure));

struct StringNode;

template<typename Value, typename Self>
struct Node {
    using value_type [[maybe_unused]] = Value;
    map<string, Self> children;
    Value value{};
    virtual ~Node() = default;
    Self &operator[](const vector<string> &path);
    Self &operator[](const StringNode &index);
    virtual Self &operator[](const string &index);

    [[nodiscard]] const Self &operator()(const vector<string>& path) const;
    [[nodiscard]] const Self &operator()(const StringNode &index) const;
    [[nodiscard]] const Self &operator()(const string &index) const;

    [[nodiscard]] bool val(const Value &index) const;
    [[nodiscard]] explicit operator bool() const;

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
struct StatNode: Node<int, StatNode> {
    StatNode(): parent(nullptr) {};
    explicit StatNode(StatNode *parent): parent(parent) {};
    StatNode* const parent;
    void operator++(int);
    using Node::operator[];
    StatNode &operator[](const string &index) override;
};
struct StringNode: Node<string, StringNode> {};
template struct Node<string, StringNode>;
template struct Node<int, StatNode>;

void flushConfig(StringNode &config, const fs::path &path, bool format = true, bool compact=false);
void parseConfig(StringNode &config, const fs::path &filePath, bool mkDef = true);
void parseJson(StringNode &config, istream &from);
bool checkHash(StringNode &root, StringNode &cache);
void setHash(StringNode &root, StringNode &cache);

using operation = int(*)(set<string>&,vector<string>&);
int switchOp(set<string> &module, vector<string> &dirEntries);
int verifyOp(set<string> &module, vector<string> &dirEntries);
int importOp(set<string> &options, vector<string> &arguments);
int makeOp(set<string> &options, vector<string> &arguments);
int collectOp(set<string> &options, vector<string> &arguments);
int backupOp(set<string> &options, vector<string> &arguments);
int bootOp(set<string> &options, vector<string> &arguments);
int versionOp(set<string> &options, vector<string> &arguments);
int helpOp(set<string> &options, vector<string> &arguments);
int exitOp(set<string> &options, vector<string> &arguments);
int nullOp(set<string> &options, vector<string> &arguments);

inline fs::path ensureExists(const fs::path& root, const string &value) {
    fs::path result = root / value;
    create_directories(result);
    return result;
}
constexpr string mapOption(const string &raw) {
    if (raw == "-a" || raw == "--all") return "all";
    if (raw == "-v" || raw == "--version") return "functional-version";
    if (raw == "-h" || raw == "--help") return "functional-help";
    if (raw == "-m" || raw == "--minecraft") return "minecraft";
    if (raw == "-n" || raw == "--no-report") return "no-report";
    if (raw == "-y" || raw == "--force-yes") return "force-yes";
    return "custom" + raw;
}
constexpr operation mapOperation(const string &name) {
    if (name == "switch") return switchOp;
    if (name == "verify") return verifyOp;
    if (name == "make") return makeOp;
    if (name == "import") return importOp;
    if (name == "collect") return collectOp;
    if (name == "backup") return backupOp;
    if (name == "boot") return bootOp;
    if (name == "help") return helpOp;
    if (name == "version") return versionOp;
    if (name == "exit") return exitOp;
    return nullOp;
}

pid_t forkToServer(pair<const string, StringNode &> serverEntry, StringNode& root, int input, int output, int error);
set<pair<string, string>> collectPack(StringNode &module, StringNode& root, int side);


#endif //MAIN_HPP
