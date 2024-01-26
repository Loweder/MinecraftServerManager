#include "main.hpp"
#include <cstring>
#include <iostream>
#include <readline/readline.h>
#include <readline/history.h>

char **tab_complete(const char *input, int start, int end) {
	rl_attempted_completion_over = 1;
	set<string> completed;
	vector<string> output;
	string data = rl_line_buffer;
	size_t pos = 0, last = 0;
	while ((pos = data.find(' ', last)) != string::npos) {
		if (pos - last > 0)
			output.emplace_back(data.substr(last, pos - last));
		last = pos + 1;
	}
	output.emplace_back(input);
	if (output.size() == 1) {
		for (const auto &[op, ignored] : operationMap) {
			if (op.starts_with(output[0])) completed.emplace(op);
		}
	} else {
		if (operationMap.contains(output[0])) {
			operation toDo = operationMap.at(output[0]);
			output[0] = "completion";
			toDo(completed, output);
		}
	}
	if (completed.empty()) return NULL;
	if (completed.size() == 1) {
		char** array = new char*[2];
		array[0] = strdup(completed.begin()->c_str());
		array[1] = NULL;
		return array;
	} else {
		char** array = new char*[completed.size() + 2];
		auto it = completed.begin();
    		std::string prefix = *it;
		int i = 1;
   		for (;it != completed.end(); ++it) {
			array[i++] = strdup(it->c_str());
        		size_t j = 0;
       			while (j < prefix.size() && j < it->size() && prefix[j] == (*it)[j]) {
            			++j;
        		}

        		prefix = prefix.substr(0, j);
    		}
		array[0] = strdup(prefix.c_str());
		array[completed.size() + 1] = NULL;
		return array;
	}
}
//TODO this
int addOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "completion") {

	} else {

	}
	return 0;
}

int removeOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "completion") {

	} else {

	}
	return 0;
}

int setOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "completion") {

	} else {

	}
	return 0;
}

int unsetOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "completion") {

	} else {

	}
	return 0;
}

int listOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "completion") {

	} else {

	}
	return 0;
}

int dirOp(set<string> &completion, vector<string> &arguments) {
	if (arguments[0] == "interactive") {
		if (arguments.size() == 1) {
			cout << "CWD is now: <empty>" << endl;
			cwd.clear();
		} else {
			vector<string> part = {arguments.begin() + 1, arguments.end()};
			if (root.root(part)) {
				cwd = part;	
				cout << "CWD is now:";
				for (auto &str : cwd)
					cout << " " << str;
				cout << endl;
			} else {
				cout << "CWD not changed: path doesn't exist" << endl;
			}
		}
	} else if (arguments[0] == "completion") {
		const string &last = arguments.back();
		vector<string> part = {arguments.begin() + 1, arguments.end() - 1};
		for (const auto &[key, node] : root.root(part)) {
			if (key.starts_with(last)) completion.emplace(key);	
		}
	} else {
		cout << "Should only call this function from interactive shell" << endl;
		return 1;
	}
	return 0;
}

int interactiveOp(set<string> &, vector<string> &arguments) {
	if (arguments.size() >= 1 && arguments[0] == "completion") return 0;
	if (arguments.size() >= 1 && arguments[0] == "interactive") {
		cout << "Should not call this function from interactive shell" << endl;
		return 1;
	}
	rl_attempted_completion_function = tab_complete;
	char* input;
	while ((input = readline(">> ")) != nullptr) {
      		if (input[0] != '\0') {
            		add_history(input);
			vector<string> output;
			set<string> options;
			string data = rl_line_buffer;
			size_t pos = 0, last = 0;
			while ((pos = data.find(' ', last)) != string::npos) {
				if (pos - last > 0)
					output.emplace_back(data.substr(last, pos - last));
				last = pos + 1;
			}
			if (last != data.size())
				output.emplace_back(data.substr(last));
			operation toDo = operationMap.at(output[0]);
			output[0] = "interactive";
			int result = toDo(options, output);
			if (result & 0x100) flushConfig(result & 0x200);
        	}

        	free(input);
    	}
	return 0;
}
