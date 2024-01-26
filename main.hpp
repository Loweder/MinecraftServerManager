#ifndef MAIN_HPP
#define MAIN_HPP

#include "json/json.h"
#include <string>
#include <filesystem>
#include <map>
#include <vector>
#include <set>
#include <functional>
#include <zip.h>

using namespace std;
namespace fs = filesystem;

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

    virtual typename map<string, Self>::iterator begin();
    virtual typename map<string, Self>::iterator end();

    virtual typename map<string, Self>::const_iterator begin() const;
    virtual typename map<string, Self>::const_iterator end() const ;
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
    explicit path_node(fs::path &&input) { value = input; };
    using node::operator[];
    path_node &operator[](const string &index) override;
    using node::begin;
    typename map<string, path_node>::iterator begin() override;
};
struct string_node: node<string, string_node> {
    string &upper();
    string &lower();
    string &def(const string &str);
};
template struct node<string, string_node>;
template struct node<int, stat_node>;
template struct node<fs::path, path_node>;

class zip_obuf : public streambuf {
	public:
		explicit zip_obuf(zip_source_t* zipSource) : zipsrc_(zipSource) {
			this->setp(buffer_, buffer_ + 8192);	
		}

	protected:
		int_type overflow(int_type ch) override {
			if (ch != traits_type::eof()) {
				sync();
				sputc(ch);
			}
			return ch;
		}
		int_type sync() override {
			zip_source_write(zipsrc_, buffer_, pptr() - pbase());
			setp(buffer_, buffer_ + 8192);
			return 0;
		}
		
	private:
		zip_source_t* zipsrc_;
		char buffer_[8192] = {};
};
class zip_ibuf : public streambuf {
	public:
		explicit zip_ibuf(zip_file_t* zipFile) : zipFile_(zipFile) {}

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

/* TODO I don't have time for this now
struct thread_pool {
    thread_pool(size_t threadCount);
    ~thread_pool();
    bool enqueue(function<void()> task);
    void wait();
private:
    void worker_thread();
    vector<thread> workers;
    deque<function<void()>> tasks;
    mutex mx;
    condition_variable condition;
    condition_variable main_condition;
    int state = 0;
};*/

extern struct _root_type {
    string_node root;
    string_node cache;
    path_node paths;
    stat_node stats;
    bool valid = false;
} root;
extern vector<string> cwd;

void flushConfig(string_node &config, ostream &stream, bool format=true, bool compact=true);
void parseConfig(string_node &config, istream &stream);
void flushConfig(string_node &config, const fs::path &path, bool format=true, bool compact=true);
void parseConfig(string_node &config, const fs::path &filePath, bool mkDef=true);
void flushConfig(bool write_hash=true, bool format=true, bool compact=true);
bool parseConfig();
void parseJson(string_node &config, istream &from);
bool checkHash();
void setHash();

using operation = int(*)(set<string>&,vector<string>&);
int helpOp(set<string> &options, vector<string> &arguments);
int versionOp(set<string> &options, vector<string> &arguments);
int interactiveOp(set<string> &options, vector<string> &arguments);
int addOp(set<string> &options, vector<string> &arguments);
int removeOp(set<string> &options, vector<string> &arguments);
int setOp(set<string> &options, vector<string> &arguments);
int unsetOp(set<string> &options, vector<string> &arguments);
int listOp(set<string> &options, vector<string> &arguments);
int dirOp(set<string> &options, vector<string> &arguments);
int sortOp(set<string> &options, vector<string> &arguments);
int switchOp(set<string> &options, vector<string> &arguments);
int verifyOp(set<string> &options, vector<string> &arguments);
int importOp(set<string> &options, vector<string> &arguments);
int makeOp(set<string> &options, vector<string> &arguments);
int collectOp(set<string> &options, vector<string> &arguments);
int backupOp(set<string> &options, vector<string> &arguments);
int bootOp(set<string> &options, vector<string> &arguments);

inline path_node &ensureExists(path_node &path) {
    create_directories(path.value);
    return path;
}
inline string mapOption(const string &raw) {
    static const unordered_map<string, string> optionMap = {
            {"v", "functional-version"},
            {"h", "functional-help"},
            {"i", "functional-interactive"},
            {"a", "all"},
            {"m", "minecraft"},
            {"d", "deep"},
            {"y", "force-yes"},
            {"n", "no-report"},
            {"--version", "functional-version"},
            {"--help", "functional-help"},
            {"--interactive", "functional-interactive"},
            {"--all", "all"},
            {"--force-root", "force-root"},
            {"--minecraft", "minecraft"},
            {"--deep", "deep"},
            {"--force-yes", "force-yes"},
            {"--no-report", "no-report"},
    };
    auto it = optionMap.find(raw);
    if (it != optionMap.end())
        return it->second;
    return "custom-" + ((raw.size() == 1 ? "s-" : "m-") + raw);
}

static const unordered_map<string, operation> operationMap = {
	{"help", helpOp},
	{"version", versionOp},
	{"interactive", interactiveOp},
	{"add", addOp},
	{"remove", removeOp},
	{"set", setOp},
	{"unset", unsetOp},
	{"list", listOp},
	{"dir", dirOp},
	{"sort", sortOp},
	{"switch", switchOp},
	{"boot", bootOp},
	{"verify", verifyOp},
	{"collect", collectOp},
	{"import", importOp},
	{"make", makeOp},
	{"backup", backupOp},
};

pid_t forkToServer(string &server, int input, int output, int error);
set<pair<string, string>> collectPack(string_node &module, const string &mc_version, int side);

#endif //MAIN_HPP
