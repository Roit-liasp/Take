/*
 * Copyright (c) 2026, roit
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "toml.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define TAKE_ISATTY _isatty
#define TAKE_FILENO _fileno
#else
#include <sys/wait.h>
#include <unistd.h>
#define TAKE_ISATTY isatty
#define TAKE_FILENO fileno
#endif

namespace fs = std::filesystem;

namespace {

constexpr const char *TAKE_VERSION = "0.0.4";

bool g_useColor = true;

std::string colorize(const std::string &code, const std::string &text) {
    if (!g_useColor)
        return text;
    return "\x1b[" + code + "m" + text + "\x1b[0m";
}

void die(const std::string &msg) {
    std::cerr << colorize("1;31", "take: error:") << " " << msg << "\n";
    std::exit(1);
}

void warn(const std::string &msg) {
    std::cerr << colorize("1;33", "take: warning:") << " " << msg << "\n";
}

struct Function {
    std::string name;
    std::string desc;
    std::string dir;
    std::vector<std::string> deps;
    std::vector<std::string> run;
    std::vector<std::string> runOverride;
    bool quiet = false;
    bool ignoreErrors = false;
};

std::string currentOsTag()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unix";
#endif
}

std::string substitute(const std::string &input, const std::map<std::string, std::string> &vars, const std::map<std::string, std::string> &builtins, const std::string &ctx) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '{') {
            size_t end = input.find('}', i + 2);
            if (end == std::string::npos) {
                warn(ctx + ": unclosed variable '${' - leaving as is");
                out += input[i];
                ++i;
                continue;
            }
            std::string name = input.substr(i + 2, end - (i + 2));
            std::string value;
            bool found = false;

            if (name.rfind("env:", 0) == 0) {
                const char *e = std::getenv(name.substr(4).c_str());
                if (e) {
                    value = e;
                    found = true;
                }
            } else {
                auto bit = builtins.find(name);
                if (bit != builtins.end()) {
                    value = bit->second;
                    found = true;
                } else {
                    auto vit = vars.find(name);
                    if (vit != vars.end()) {
                        value = vit->second;
                        found = true;
                    }
                }
            }

            if (!found)
                warn(ctx + ": variable '${" + name + "}' is undefined - substituting an empty string");

            out += value;
            i = end + 1;
        } else {
            out += input[i];
            ++i;
        }
    }
    return out;
}

struct Config {
    std::map<std::string, Function> functions;
    std::vector<std::string> order;
    std::map<std::string, std::string> vars;
    std::string defaultFunction;
};

Config loadConfig(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
        die("Unable to open the file '" + path.string() + "'");

    std::stringstream ss;
    ss << f.rdbuf();

    toml::Document doc;
    try {
        doc = toml::parse(ss.str());
    }
    catch (const toml::ParseError &e) {
        die(path.string() + ": " + e.what());
    }

    Config cfg;

    auto itDefault = doc.root.find("default");
    if (itDefault != doc.root.end())
        cfg.defaultFunction = itDefault->second.asString();

    if (doc.sections.count("vars"))
        for (auto &[k, v] : doc.sections["vars"])
            cfg.vars[k] = v.asString();

    std::string osTag = currentOsTag();

    for (const auto &name : doc.sectionOrder) {
        if (name == "vars")
            continue;

        Function fn;
        fn.name = name;
        const auto &tbl = doc.sections[name];

        if (auto it = tbl.find("desc"); it != tbl.end())
            fn.desc = it->second.asString();
        if (auto it = tbl.find("dir"); it != tbl.end())
            fn.dir = it->second.asString();
        if (auto it = tbl.find("deps"); it != tbl.end())
            fn.deps = it->second.asStringArray();
        if (auto it = tbl.find("run"); it != tbl.end())
            fn.run = it->second.asStringArray();
        if (auto it = tbl.find("quiet"); it != tbl.end())
            fn.quiet = (it->second.type == toml::Type::Bool) ? it->second.boolean : false;
        if (auto it = tbl.find("ignore_errors"); it != tbl.end())
            fn.ignoreErrors = (it->second.type == toml::Type::Bool) ? it->second.boolean : false;

        std::string osKey = "run_" + osTag;
        if (auto it = tbl.find(osKey); it != tbl.end())
            fn.run = it->second.asStringArray();
        else if (osTag != "windows") {
            if (auto it2 = tbl.find("run_unix"); it2 != tbl.end())
                fn.run = it2->second.asStringArray();
        }

        cfg.functions[name] = fn;
        cfg.order.push_back(name);
    }

    return cfg;
}

struct Options {
    bool dryRun = false;
    bool quiet = false;
    std::string args;
};

int runFunction(const Config &cfg, const std::string &name, std::set<std::string> &done, std::vector<std::string> &stack, const Options &opts, const fs::path &baseDir) {
    if (done.count(name))
        return 0;

    if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
        std::string cycle;
        for (auto &s : stack)
            cycle += s + " -> ";
        cycle += name;
        die("dependency cycle detected: " + cycle);
    }

    auto it = cfg.functions.find(name);
    if (it == cfg.functions.end()) {
        std::string available;
        for (auto &n : cfg.order)
            available += (available.empty() ? "" : ", ") + n;
        die("function '" + name + "' not found. Available functions: " + available);
    }
    const Function &fn = it->second;

    stack.push_back(name);
    for (const auto &dep : fn.deps)
        runFunction(cfg, dep, done, stack, opts, baseDir);
    stack.pop_back();

    bool quiet = opts.quiet || fn.quiet;

    if (!quiet)
        std::cerr << colorize("1;34", "==>") << " take " << colorize("1;36", name) << (fn.desc.empty() ? "" : (" — " + fn.desc)) << "\n";

    std::map<std::string, std::string> builtins;
    builtins["TAKE_FUNC"] = name;
    builtins["TAKE_DIR"] = baseDir.string();
    builtins["ARGS"] = opts.args;

    fs::path prevDir;
    bool changedDir = false;
    if (!fn.dir.empty()) {
        std::string resolvedDir = substitute(fn.dir, cfg.vars, builtins, name);
        fs::path target = fs::path(resolvedDir).is_absolute() ? fs::path(resolvedDir) : (baseDir / resolvedDir);
        prevDir = fs::current_path();
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec)
            die("[" + name + "] Unable to switch to the directory '" + target.string() + "': " + ec.message());
        changedDir = true;
    }

    for (const auto &rawCmd : fn.run) {
        std::string cmd = substitute(rawCmd, cfg.vars, builtins, name);
        if (!quiet)
            std::cerr << colorize("2", "  $ " + cmd) << "\n";

        if (!opts.dryRun) {
            int rc = std::system(cmd.c_str());
            int code = 0;
#if !defined(_WIN32)
            if (rc != -1)
                code = WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
            else
                code = 1;
#else
            code = rc;
#endif
            if (code != 0 && !fn.ignoreErrors) {
                if (changedDir) {
                    std::error_code ec;
                    fs::current_path(prevDir, ec);
                }
                die("[" + name + "] The command finished with code " + std::to_string(code) + ": " + cmd);
            }
        }
    }

    if (changedDir) {
        std::error_code ec;
        fs::current_path(prevDir, ec);
    }

    done.insert(name);
    return 0;
}

void printUsage() {
    std::cout <<
        "Take " << TAKE_VERSION << " Simple build system.\n"
        "Usage:\n"
        "  take                          show the list of functions\n"
        "  take <function>               execute the function and its dependencies\n"
        "  take <function> -- arg        pass arguments to ${ARGS}\n"
        "  take -l, --list               list of functions with descriptions\n"
        "  take -f <file>                use a different .toml file (default: take.toml)\n"
        "  take -C <dir>                 first, go to the catalog\n"
        "  take -n, --dry-run            show commands without executing them\n"
        "  take -q, --quiet              do not print executed commands\n"
        "  take --no-color               disable color output\n"
        "  take -v, --version            show version\n"
        "  take -h, --help               show help\n";
}

void printList(const Config &cfg) {
    size_t width = 0;
    for (auto &n : cfg.order)
        width = std::max(width, n.size());

    std::cout << "Functions in take.toml:\n";
    for (auto &n : cfg.order) {
        const Function &fn = cfg.functions.at(n);
        std::cout << "  " << colorize("1;36", n) << std::string(width - n.size(), ' ') << "   " << fn.desc << "\n";
    }
    if (!cfg.defaultFunction.empty())
        std::cout << "\nBy default: " << colorize("1;36", cfg.defaultFunction) << "\n";
}

}

int main(int argc, char **argv) {
#if !defined(_WIN32)
    g_useColor = TAKE_ISATTY(TAKE_FILENO(stderr));
#else
    g_useColor = false;
#endif

    std::vector<std::string> args(argv + 1, argv + argc);

    std::string configFile = "take.toml";
    Options opts;
    std::string function;
    bool listRequested = false;

    size_t i = 0;
    for (; i < args.size(); ++i) {
        const std::string &a = args[i];
        if (a == "--") {
            ++i;
            break;
        } else if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        } else if (a == "-v" || a == "--version") {
            std::cout << "take " << TAKE_VERSION << "\n";
            return 0;
        } else if (a == "-l" || a == "--list") {
            listRequested = true;
        } else if (a == "-n" || a == "--dry-run") {
            opts.dryRun = true;
        } else if (a == "-q" || a == "--quiet") {
            opts.quiet = true;
        } else if (a == "--no-color") {
            g_useColor = false;
        } else if (a == "-f") {
            if (i + 1 >= args.size())
                die("The -f flag requires an argument (path to a .toml file).");
            configFile = args[++i];
        }
        else if (a == "-C") {
            if (i + 1 >= args.size())
                die("The -C flag requires an argument (directory).");
            std::error_code ec;
            fs::current_path(args[++i], ec);
            if (ec)
                die("Unable to switch to the directory '" + args[i] + "': " + ec.message());
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            die("unknown flag '" + a + "' (see take --help)");
        } else {
            if (!function.empty())
                die("more than one function specified: '" + function + "' and '" + a + "'");
            function = a;
        }
    }

    if (i < args.size()) {
        std::string joined;
        for (; i < args.size(); ++i) {
            if (!joined.empty())
                joined += " ";
            joined += args[i];
        }
        opts.args = joined;
    }

    fs::path cfgPath(configFile);
    if (!fs::exists(cfgPath))
        die("Configuration file '" + configFile + "' not found in " + fs::current_path().string());

    Config cfg = loadConfig(cfgPath);
    fs::path baseDir = fs::absolute(cfgPath).parent_path();

    if (listRequested) {
        printList(cfg);
        return 0;
    }

    if (function.empty()) {
        if (!cfg.defaultFunction.empty()) {
            function = cfg.defaultFunction;
        } else {
            printList(cfg);
            return 0;
        }
    }

    std::set<std::string> done;
    std::vector<std::string> stack;
    return runFunction(cfg, function, done, stack, opts, baseDir);
}
