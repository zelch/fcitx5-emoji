// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
namespace emoji {
using Choices = std::vector<std::pair<std::string, std::string>>;
#include "countries.inc"
inline Choices flagChoices() {
    Choices result{{"Any", "No preference"}};
    for (auto [code, name] : countries) result.emplace_back(code, name);
    std::sort(result.begin()+1, result.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    return result;
}
struct ChoiceConstraint {
    using Type = std::string;
    Choices choices;
    bool check(const std::string &value) const {
        return std::any_of(choices.begin(), choices.end(), [&](const auto &choice) { return choice.first == value; });
    }
    void dumpDescription(fcitx::RawConfig &) const {}
};
struct ChoiceAnnotation : fcitx::EnumAnnotation {
    Choices choices;
    explicit ChoiceAnnotation(Choices values = {}) : choices(std::move(values)) {}
    void dumpDescription(fcitx::RawConfig &config) const {
        fcitx::EnumAnnotation::dumpDescription(config);
        for (size_t i = 0; i < choices.size(); ++i) {
            config.setValueByPath("Enum/"+std::to_string(i), choices[i].first);
            config.setValueByPath("EnumI18n/"+std::to_string(i), choices[i].second);
        }
    }
};
struct LanguageConstraint {
    using Type = std::string;
    bool check(const std::string &value) const {
        return !value.empty() && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
    }
    void dumpDescription(fcitx::RawConfig &) const {}
};
using ChoiceOption = fcitx::Option<std::string, ChoiceConstraint,
                                 fcitx::DefaultMarshaller<std::string>, ChoiceAnnotation>;
inline const Choices genderChoices{{"Any","No preference"},{"Person","Person / neutral"},{"Woman","Woman"},{"Man","Man"}};
inline const Choices toneChoices{{"Any","No preference"},{"0","Unmodified"},{"1","Light"},{"2","Medium-light"},{"3","Medium"},{"4","Medium-dark"},{"5","Dark"}};
inline Choices secondToneChoices() {
    auto result = toneChoices;
    result.insert(result.begin(), {"Inherit","Use first person's preference"});
    return result;
}
inline const Choices hairChoices{{"Any","No preference"},{"Red","Red"},{"Curly","Curly"},{"White","White"},{"Bald","Bald"}};
inline const Choices directionChoices{{"Any","No preference"},{"Left","Left"},{"Right","Right"}};
inline const Choices presentationChoices{{"Any","No preference"},{"Emoji","Emoji"},{"Text","Text"}};
#define EMOJI_CHOICE(member, path, label, defaultValue, choices) \
    ChoiceOption member{this, path, label, defaultValue, ChoiceConstraint{choices}, {}, ChoiceAnnotation{choices}};
FCITX_CONFIGURATION(
    PickerConfig,
    fcitx::KeyListOption triggerKeys{this, "TriggerKeys", "Activation shortcuts",
        {fcitx::Key("Control+Alt+e")},
        fcitx::KeyListConstrain({fcitx::KeyConstrainFlag::AllowModifierLess})};
    fcitx::Option<std::string, LanguageConstraint> language{
        this, "Language", "Annotation language (CLDR locale, e.g. en or de)", "en",
        LanguageConstraint{}};
    fcitx::Option<int, fcitx::IntConstrain> pageSize{this, "PageSize", "Candidates per page", 8, fcitx::IntConstrain(3, 20)};
    EMOJI_CHOICE(gender, "Gender", "Person gender", "Any", genderChoices)
    EMOJI_CHOICE(secondGender, "SecondGender", "Second person's gender", "Any", genderChoices)
    EMOJI_CHOICE(skinTone, "SkinTone", "Skin tone", "Any", toneChoices)
    EMOJI_CHOICE(secondSkinTone, "SecondSkinTone", "Second person's skin tone", "Inherit", secondToneChoices())
    EMOJI_CHOICE(hair, "Hair", "Hair", "Any", hairChoices)
    EMOJI_CHOICE(direction, "Direction", "Facing direction (where supported)", "Any", directionChoices)
    EMOJI_CHOICE(presentation, "Presentation", "Symbol presentation", "Any", presentationChoices)
    EMOJI_CHOICE(country, "Country", "Country or subdivision flag", "Any", flagChoices())
);
#undef EMOJI_CHOICE
inline std::string examplePerson(const std::string &gender, const std::string &tone,
                                 const std::string &hair) {
    std::string result = gender == "Woman" ? "👩" : gender == "Man" ? "👨" : "🧑";
    if (tone.size() == 1 && tone[0] >= '1' && tone[0] <= '5')
        result += fcitx::utf8::UCS4ToUTF8(0x1f3fa + tone[0] - '0');
    if (hair == "Red") result += "‍🦰";
    else if (hair == "Curly") result += "‍🦱";
    else if (hair == "White") result += "‍🦳";
    else if (hair == "Bald") result += "‍🦲";
    return result;
}
inline std::string exampleFlag(const std::string &code) {
    if (code == "Any") return "🏳️";
    std::string result;
    if (code.size() == 2) {
        for (auto c : code) result += fcitx::utf8::UCS4ToUTF8(0x1f1e6 + c - 'A');
    } else {
        result = "🏴";
        for (auto c : code) result += fcitx::utf8::UCS4ToUTF8(0xe0000 + c);
        result += fcitx::utf8::UCS4ToUTF8(0xe007f);
    }
    return result;
}
// Enum labels are a snapshot of saved settings when the editor requests its
// schema. Fcitx's generic editor does not recalculate labels while editing.
inline void updateExamples(const PickerConfig &config) {
    auto annotate = [](const ChoiceOption &option, Choices choices, auto example) {
        for (auto &[value, label] : choices) label = std::string(example(value)) + "  " + label;
        option.annotation().choices = std::move(choices);
    };
    const auto secondTone = *config.secondSkinTone == "Inherit" ? *config.skinTone : *config.secondSkinTone;
    annotate(config.gender, genderChoices, [&](const auto &gender) {
        return examplePerson(gender, *config.skinTone, *config.hair);
    });
    annotate(config.secondGender, genderChoices, [&](const auto &gender) {
        return examplePerson(gender, secondTone, *config.hair);
    });
    annotate(config.skinTone, toneChoices, [&](const auto &tone) {
        return examplePerson(*config.gender, tone, *config.hair);
    });
    annotate(config.secondSkinTone, secondToneChoices(), [&](const auto &tone) {
        return examplePerson(*config.secondGender, tone == "Inherit" ? *config.skinTone : tone, *config.hair);
    });
    annotate(config.hair, hairChoices, [&](const auto &hair) {
        return examplePerson(*config.gender, *config.skinTone, hair);
    });
    annotate(config.direction, directionChoices, [&](const auto &direction) {
        std::string result = "🚶";
        if (*config.skinTone != "Any" && *config.skinTone != "0")
            result += fcitx::utf8::UCS4ToUTF8(0x1f3fa + (*config.skinTone)[0] - '0');
        if (*config.gender == "Woman") result += "‍♀️";
        else if (*config.gender == "Man") result += "‍♂️";
        if (direction == "Right") result += "‍➡️";
        return result;
    });
    annotate(config.presentation, presentationChoices, [](const auto &value) {
        return value == "Text" ? "☀︎" : value == "Emoji" ? "☀️" : "☀︎ / ☀️";
    });
    annotate(config.country, flagChoices(), exampleFlag);
}
}
