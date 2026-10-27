// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "engine.h"
#include "compat.h"
#include <unicode/brkiter.h>
#include <unicode/normalizer2.h>
#include <unicode/ubrk.h>
#include <unicode/unistr.h>
#include <lua.hpp>
#include <zlib.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace emoji {
namespace {
void check(lua_State *state, int status) {
    if (status != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        std::string error = message ? message : "Lua failure";
        lua_pop(state, 1);
        throw std::runtime_error(error);
    }
}
std::vector<std::string> languages(std::string language) {
    // Locale encodings and modifiers are not CLDR dictionary filenames.
    language = language.substr(0, language.find_first_of(".@"));
    for (auto &c : language) if (c == '-') c = '_';
    if (language == "zh_TW") language = "zh_Hant";
    if (language == "zh_HK") language = "zh_Hant_HK";
    if (language == "zh_CN") language = "zh";
    if (language == "nb") language = "no"; // CLDR data uses no in some releases.
    if (language.empty() || language.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::runtime_error("Invalid dictionary language");
    std::vector<std::string> result;
    while (!language.empty()) {
        result.push_back(language);
        auto pos = language.rfind('_');
        if (pos == std::string::npos) break;
        language.resize(pos);
    }
    if (result.back() != "en") result.emplace_back("en");
    return result;
}
}

std::string Engine::decompress(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open emoji dictionary: " + path);
    auto size = file.tellg();
    if (size < 5 || size > 16 * 1024 * 1024) throw std::runtime_error("Invalid compressed dictionary size");
    std::string compressed(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(compressed.data(), size)) throw std::runtime_error("Cannot read emoji dictionary");
    uint32_t expected = 0;
    for (int i = 0; i < 4; ++i) expected |= uint32_t(static_cast<unsigned char>(compressed[i])) << (8 * i);
    if (!expected || expected > 32 * 1024 * 1024) throw std::runtime_error("Invalid uncompressed dictionary size");
    std::string output(expected, '\0');
    uLongf outputSize = expected;
    uLong inputSize = compressed.size() - 4;
    if (uncompress2(reinterpret_cast<Bytef *>(output.data()), &outputSize,
                    reinterpret_cast<const Bytef *>(compressed.data() + 4), &inputSize) != Z_OK ||
        outputSize != expected || inputSize != compressed.size() - 4)
        throw std::runtime_error("Corrupt compressed emoji dictionary");
    return output;
}

std::vector<std::string> Engine::words(const std::string &text) {
    UErrorCode status = U_ZERO_ERROR;
    const auto *normalizer = icu::Normalizer2::getNFKCCasefoldInstance(status);
    icu::UnicodeString normalized;
    normalizer->normalize(icu::UnicodeString::fromUTF8(text), normalized, status);
    std::unique_ptr<icu::BreakIterator> iterator(icu::BreakIterator::createWordInstance(icu::Locale::getRoot(), status));
    if (U_FAILURE(status)) throw std::runtime_error("Unicode normalization failed");
    iterator->setText(normalized);
    std::vector<std::string> result;
    int32_t start = iterator->first();
    for (int32_t end = iterator->next(); end != icu::BreakIterator::DONE; start = end, end = iterator->next()) {
        if (iterator->getRuleStatus() < UBRK_WORD_NUMBER) continue;
        std::string word;
        normalized.tempSubStringBetween(start, end).toUTF8String(word);
        result.push_back(std::move(word));
    }
    return result;
}

std::string Engine::locate(const std::string &path) const {
    return packageFile(path);
}
Engine *Engine::self(lua_State *state) {
    return static_cast<Engine *>(lua_touserdata(state, lua_upvalueindex(1)));
}
int Engine::luaWords(lua_State *state) {
    size_t size;
    const char *input = luaL_checklstring(state, 1, &size);
    try {
        auto result = words(std::string(input, size));
        lua_createtable(state, result.size(), 0);
        int i = 0;
        for (const auto &word : result) {
            lua_pushlstring(state, word.data(), word.size());
            lua_rawseti(state, -2, ++i);
        }
        return 1;
    } catch (const std::exception &error) { lua_pushstring(state, error.what()); }
    return lua_error(state);
}
int Engine::luaRead(lua_State *state) {
    const char *language = luaL_checkstring(state, 1);
    try {
        auto *engine = self(state);
        lua_newtable(state);
        int i = 0;
        for (const auto &lang : languages(language)) {
            std::string path = engine->dictionaries_.empty()
                ? engine->locate("emoji/data/" + lang + ".dict")
                : (std::filesystem::path(engine->dictionaries_) / (lang + ".dict")).string();
            if (path.empty() || !std::filesystem::exists(path)) continue;
            auto data = decompress(path);
            lua_pushlstring(state, data.data(), data.size());
            lua_rawseti(state, -2, ++i);
        }
        if (!i) throw std::runtime_error("No emoji dictionaries found; install Fcitx's emoji data");
        return 1;
    } catch (const std::exception &error) { lua_pushstring(state, error.what()); }
    return lua_error(state);
}
int Engine::luaMetadata(lua_State *state) {
    const auto &path = self(state)->metadata_;
    lua_pushlstring(state, path.data(), path.size());
    return 1;
}

Engine::Engine(std::string script, std::string metadata, std::string dictionaries)
    : metadata_(metadata.empty() ? locate("emoji-picker/metadata.lua") : std::move(metadata)),
      dictionaries_(std::move(dictionaries)) {
    if (script.empty()) script = locate("emoji-picker/search.lua");
    if (script.empty() || metadata_.empty()) throw std::runtime_error("Emoji picker Lua files are not installed");
    state_ = luaL_newstate();
    if (!state_) throw std::runtime_error("Cannot create Lua state");
    try {
        luaL_openlibs(state_);
        lua_newtable(state_);
        for (auto [name, function] : {std::pair{"words", luaWords}, {"read_dictionary", luaRead}, {"metadata_path", luaMetadata}}) {
            lua_pushlightuserdata(state_, this);
            lua_pushcclosure(state_, function, 1);
            lua_setfield(state_, -2, name);
        }
        lua_setglobal(state_, "emoji_native");
        check(state_, luaL_loadfile(state_, script.c_str()));
        check(state_, lua_pcall(state_, 0, 1, 0));
        module_ = luaL_ref(state_, LUA_REGISTRYINDEX);
    } catch (...) { lua_close(state_); state_ = nullptr; throw; }
}
Engine::~Engine() { if (state_) lua_close(state_); }
Results Engine::search(const std::string &query, const Settings &settings) {
    lua_settop(state_, 0);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, module_);
    lua_getfield(state_, -1, "search");
    lua_pushlstring(state_, query.data(), query.size());
    lua_newtable(state_);
    for (const auto &[name, value] : settings) {
        lua_pushlstring(state_, value.data(), value.size());
        lua_setfield(state_, -2, name.c_str());
    }
    check(state_, lua_pcall(state_, 2, 1, 0));
    Results result;
    for (size_t i = 1; i <= lua_rawlen(state_, -1); ++i) {
        lua_rawgeti(state_, -1, i);
        lua_rawgeti(state_, -1, 1);
        lua_rawgeti(state_, -2, 2);
        result.emplace_back(lua_tostring(state_, -2), lua_tostring(state_, -1));
        lua_pop(state_, 3);
    }
    lua_settop(state_, 0);
    return result;
}
void Engine::clear() {
    lua_settop(state_, 0);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, module_);
    lua_getfield(state_, -1, "clear");
    check(state_, lua_pcall(state_, 0, 0, 0));
    lua_settop(state_, 0);
}
}
