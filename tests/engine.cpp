// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <zlib.h>
using namespace emoji;
void require(bool condition, const std::string &message) { if (!condition) throw std::runtime_error(message); }
size_t position(const Results &rows, const std::string &glyph) {
    auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row) { return row.first == glyph; });
    require(it != rows.end(), "Missing emoji: " + glyph);
    return it - rows.begin();
}
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Usage: test-engine source dictionaries");
        std::string root = argv[1];
        Engine engine(root+"/lua/search.lua", root+"/data/metadata.lua", argv[2]);
        auto all = engine.search("", {});
        auto payload = Engine::decompress(std::string(argv[2])+"/en.dict");
        size_t offset = 0;
        auto integer = [&]() {
            uint32_t n = 0;
            for (int i=0; i<4; ++i) n |= uint32_t(static_cast<unsigned char>(payload.at(offset++))) << (8*i);
            return n;
        };
        auto string = [&]() { auto n=integer(); auto value=payload.substr(offset,n); offset+=n; return value; };
        auto count = integer();
        for (uint32_t i=0; i<count; ++i) {
            require(all.at(i).first == string(), "Unconfigured ranking changes dictionary source order");
            auto annotations = integer();
            for (uint32_t j=0; j<annotations; ++j) string();
        }
        position(engine.search("cracking face", {}), "\U0001FAEB");
        position(engine.search("monarch butterfly", {}), "\U0001FACC");
        auto thumbs = engine.search("leftwards thumb sign", {{"SkinTone","3"}});
        require(position(thumbs,"\U0001FAF9\U0001F3FD") < position(thumbs,"\U0001FAF9"), "Emoji 18 tone preference");
        auto walking = engine.search("person walking", {{"Direction","Right"}});
        require(position(walking,"🚶‍➡️") < position(walking,"🚶"), "Catalogue right-facing preference");
        auto normal = engine.search("cat smil", {{"Language","en"}});
        require(!normal.empty(), "Multiword prefix search");
        position(normal, "😸");
        require(normal == engine.search("smil cat", {}), "Query order changes results");
        require(normal == engine.search("ＣＡＴ  SMIL", {}), "NFKC and casefold");
        require(engine.search("cat nonexistentword", {}).empty(), "AND semantics");
        auto base = engine.search("raising hand", {});
        auto preferred = engine.search("raising hand", {{"Gender","Woman"},{"SkinTone","3"}});
        require(base.size() == preferred.size(), "Preferences remove results");
        require(position(preferred,"🙋🏽‍♀️") < position(preferred,"🙋🏽‍♂️"), "Gender preference");
        require(position(preferred,"🙋🏽‍♀️") < position(preferred,"🙋‍♀️"), "Tone preference");
        auto explicitGender = engine.search("man raising hand", {{"Gender","Woman"}});
        position(explicitGender, "🙋‍♂️");
        auto hand = engine.search("hand", {{"Gender","Woman"}});
        require(position(hand,"👋") < position(hand,"🙋‍♀️"), "Preference outranks unrelated relevance/order");
        auto tonedHand = engine.search("hand", {{"SkinTone","3"}});
        require(position(tonedHand,"👋🏽") < position(tonedHand,"🙋🏽"), "Waving hand loses its family position");
        for (const auto &alternative : {"👋", "👋🏻", "👋🏼", "👋🏾", "👋🏿"})
            require(position(tonedHand,"🙋🏽") < position(tonedHand,alternative), "Unselected waving-hand tones bury raised hand");
        require(tonedHand.size() == engine.search("hand", {}).size(), "Demotion removes hand variants");
        require(position(hand,"✍️") < position(hand,"🙋‍♂️"), "Unselected gender is not at bottom");
        require(engine.search("dark skin tone hand", {{"SkinTone","1"}}) ==
                engine.search("dark skin tone hand", {}), "Explicit tone query demoted by default");
        auto couple = engine.search("couple heart", {{"Gender","Woman"},{"SecondGender","Woman"},{"SkinTone","1"},{"SecondSkinTone","5"}});
        require(position(couple,"👩🏻‍❤️‍👩🏿") < position(couple,"👨‍❤️‍👨"), "Couple preference");
        auto wrestling = engine.search("wrestling", {{"SecondGender","Woman"}});
        require(position(wrestling,"🤼‍♀️") < position(wrestling,"🤼‍♂️"), "Legacy pair gender preference");
        auto professions = engine.search("health worker", {{"Gender","Person"}});
        require(position(professions,"🧑‍⚕️") < position(professions,"👩‍⚕️"), "Neutral profession preference");
        auto children = engine.search("young", {{"Gender","Woman"}});
        require(position(children,"👧") < position(children,"👦"), "Child gender family");
        auto handshake = engine.search("handshake", {{"SkinTone","1"},{"SecondSkinTone","5"}});
        require(position(handshake,"🫱🏻‍🫲🏿") < position(handshake,"🫱🏿‍🫲🏻"), "Independent handshake tones");
        auto explicitTone = engine.search("woman raising hand dark skin tone", {{"SkinTone","1"}});
        require(position(explicitTone,"🙋🏿‍♀️") < explicitTone.size(), "Explicit skin tone overridden");
        auto flags = engine.search("flag", {{"Country","CA"}});
        require(position(flags,"🇨🇦") < position(flags,"🇺🇸"), "Flag preference");
        auto usa = engine.search("flag united states", {{"Country","CA"}});
        require(!usa.empty() && usa.front().first == "🇺🇸", "Explicit country overrides preference");
        auto hair = engine.search("person", {{"Hair","Red"}});
        require(position(hair,"🧑‍🦰") < position(hair,"🧑‍🦱"), "Hair preference");
        auto presentation = engine.search("sun", {{"Presentation","Text"}});
        require(position(presentation,"☀︎") < position(presentation,"☀️"), "Presentation preference");
        auto region = engine.search("smile", {{"Language","en_GB"}});
        position(region, "😄");
        auto german = engine.search("katze", {{"Language","de"}});
        require(!german.empty(), "German annotations");
        require(Engine::words("CAFÉ café") == std::vector<std::string>({"café","café"}), "Canonical normalization");
        bool rejected = false;
        try { engine.search("", {{"Language","../../etc/passwd"}}); } catch (...) { rejected = true; }
        require(rejected, "Language traversal not rejected");
        auto corrupt = std::filesystem::temp_directory_path()/"fcitx-emoji-corrupt.dict";
        { std::ofstream out(corrupt, std::ios::binary); out << "invalid"; }
        rejected = false;
        try { Engine::decompress(corrupt.string()); } catch (...) { rejected = true; }
        std::filesystem::remove(corrupt);
        require(rejected, "Corrupt dictionary not rejected");
        auto fixture = std::filesystem::temp_directory_path() / "emoji-dictionary-fixture";
        std::filesystem::create_directories(fixture);
        auto writeFixture = [&](const std::string &data) {
            uLongf size = compressBound(data.size());
            std::string compressed(size, '\0');
            require(compress2(reinterpret_cast<Bytef *>(compressed.data()), &size,
                             reinterpret_cast<const Bytef *>(data.data()), data.size(), Z_BEST_SPEED) == Z_OK, "Fixture compression");
            std::ofstream out(fixture / "en.dict", std::ios::binary);
            uint32_t expected = data.size();
            for (int i=0; i<4; ++i) out.put(static_cast<char>(expected >> (8*i)));
            out.write(compressed.data(), size);
        };
        Engine malformed(root+"/lua/search.lua", root+"/data/metadata.lua", fixture.string());
        for (auto data : {payload.substr(0,payload.size()-1), payload+"trailing", std::string("\xff\xff\xff\xff",4)}) {
            writeFixture(data);
            rejected = false;
            try { malformed.search("cat", {}); } catch (...) { rejected = true; }
            require(rejected, "Malformed binary dictionary accepted by Lua parser");
        }
        writeFixture(payload);
        require(malformed.search("cat smil", {}) == normal, "Recovery from parse failure");
        auto uint32 = [](uint32_t value) {
            std::string out;
            for (int i=0; i<4; ++i) out.push_back(static_cast<char>(value >> (8*i)));
            return out;
        };
        auto encoded = [&](const std::string &text) { return uint32(text.size()) + text; };
        writeFixture(uint32(2) + encoded("🚶") + uint32(1) + encoded("walking") +
                     encoded("🚶‍➡️") + uint32(1) + encoded("walking"));
        Engine olderDictionary(root+"/lua/search.lua", root+"/data/metadata.lua", fixture.string());
        position(olderDictionary.search("cracking face", {}), "\U0001FAEB");
        position(olderDictionary.search("monarch butterfly", {}), "\U0001FACC");
        auto catalogue = olderDictionary.search("", {});
        require(std::count_if(catalogue.begin(), catalogue.end(), [](const auto &row) {
            return row.first == "🚶‍➡️";
        }) == 1, "Dictionary and metadata duplicate an emoji");
        auto futureMetadata = fixture / "metadata.lua";
        {
            std::ofstream out(futureMetadata);
            out << R"(return {
{emoji="🚶",name="person walking",family="walking",direction="Left",source=""},
{emoji="🚶‍➡️",name="person walking facing right",family="walking",direction="Right",source=""}
})";
        }
        Engine directional(root+"/lua/search.lua", futureMetadata.string(), fixture.string());
        require(directional.search("walking", {{"Direction","Right"}}).front().first == "🚶‍➡️", "Right-facing preference");
        require(directional.search("walking", {{"Direction","Left"}}).front().first == "🚶", "Left-facing preference");
        require(directional.search("walking right", {{"Direction","Left"}}).front().first == "🚶‍➡️", "Explicit direction override");
        std::filesystem::remove_all(fixture);
        engine.clear();
        require(engine.search("cat smil", {}) == normal, "Reload changes deterministic order");
        auto start = std::chrono::steady_clock::now();
        for (int i=0; i<100; ++i) engine.search("cat smil", {});
        std::cout << "100 searches: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count() << " ms\n";
        std::cout << "Search, variants, Unicode, fallback, and error checks passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
