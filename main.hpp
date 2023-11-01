#ifndef MAIN_HPP
#define MAIN_HPP

#include "cmake-build-debug/version.h"
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

int stringToCode(const string &str, int bits, int defaultValue);
string codeToString(int val, int bits);
string &stringToUpper(string &str);

struct StringNode;

template<typename type, typename self>
struct Node {
    typedef type value_type;
    map<string, self> children;
    type value{};
    self &operator[](const vector<string> &path);
    self &operator[](const StringNode &index);
    virtual self &operator[](const string &index);

    [[nodiscard]] const self &operator()(const vector<string>& path) const;
    [[nodiscard]] const self &operator()(const StringNode &index) const;
    [[nodiscard]] const self &operator()(const string &index) const;

    [[nodiscard]] bool val(const type &index) const;
    [[nodiscard]] explicit operator bool() const;

    map<string, self>::iterator begin();
    map<string, self>::iterator end();

    template<typename to>
    to convert(to::value_type(*mapper)(const type&)) { // NOLINT(*-no-recursion)
        to result;
        result.value = mapper(value);
        for (auto &item: *this)
            result[item.first] = item.second.template convert<to>(mapper);
        return result;
    }
    [[nodiscard]] size_t hash() const;
protected:
    static self nullObject;
    [[nodiscard]] inline bool isNull() const;
};
struct StatNode: public Node<int, StatNode> {
    StatNode(): parent(nullptr) {};
    explicit StatNode(StatNode *parent): parent(parent) {};
    StatNode* const parent;
    void operator++(int);
    using Node<int, StatNode>::operator[];
    StatNode &operator[](const string &index) override;
};
struct StringNode: public Node<string, StringNode> {};
template struct Node<string, StringNode>;
template struct Node<int, StatNode>;

string writeWord(const string &word);
void flushConfig(StringNode &config, const fs::path &path, bool format = true);
bool readWord(istream &file, string &word);
void parseConfig(StringNode &config, const fs::path &filePath);
bool checkHash(StringNode &root);
void setHash(StringNode &root);
inline fs::path ensureExists(const fs::path& root, const string &value) {
    fs::path result = root / value;
    create_directories(result);
    return result;
}
constexpr string mapArgument(const string &raw) {
    if (raw == "-a" || raw == "--all") return "all";
    else if (raw == "-v" || raw == "--version") return "version";
    else if (raw == "-h" || raw == "--help") return "help";
    else if (raw == "-m" || raw == "--minecraft") return "minecraft";
    else if (raw == "-n" || raw == "--no-report") return "no-report";
    else if (raw == "-y" || raw == "--force-yes") return "force-yes";
    return raw;
}

pid_t forkToServer(pair<const string, StringNode &> serverEntry, StringNode& root, int input, int output, int error);
set<string> collectModPack(StringNode &pack, StringNode& root, bool side);
set<string> collectPluginPack(StringNode &pack, StringNode& root);


#endif //MAIN_HPP
