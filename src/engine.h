// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct lua_State;
namespace emoji {
using Settings = std::map<std::string, std::string>;
using Results = std::vector<std::pair<std::string, std::string>>;
class Engine {
public:
    // Explicit paths are used by standalone tests. Empty paths use Fcitx's
    // package data lookup, including user overrides.
    Engine(std::string script = {}, std::string metadata = {}, std::string dictionaries = {});
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Results search(const std::string &query, const Settings &settings);
    void clear();
    static std::vector<std::string> words(const std::string &text);
    static std::string decompress(const std::string &path);
private:
    static int luaWords(lua_State *);
    static int luaRead(lua_State *);
    static int luaMetadata(lua_State *);
    static Engine *self(lua_State *);
    std::string locate(const std::string &) const;
    std::string metadata_, dictionaries_;
    lua_State *state_ = nullptr;
    int module_ = 0;
};
}
