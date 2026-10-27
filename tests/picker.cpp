// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/testing.h>
#include <cstdlib>
#include "compat.h"
#include <unistd.h>
#include <fcitx-utils/eventdispatcher.h>
#include <testfrontend_public.h>
#include <testim_public.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace fcitx;
void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    auto testHome = std::filesystem::temp_directory_path() / ("emoji-picker-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(testHome);
    setenv("SKIP_FCITX_PATH", "1", 1);
    setenv("FCITX_CONFIG_DIRS", testHome.string().c_str(), 1);
    setenv("FCITX_CONFIG_HOME", testHome.string().c_str(), 1);
    setenv("FCITX_DATA_HOME", testHome.string().c_str(), 1);
    auto addonPaths = std::string(argv[1]) + ":" + emoji::fcitxDirectory("addondir");
    auto dataPaths = std::string(argv[2]) + ":" + emoji::fcitxDirectory("pkgdatadir", "testing");
    setenv("FCITX_ADDON_DIRS", addonPaths.c_str(), 1);
    setenv("FCITX_DATA_DIRS", dataPaths.c_str(), 1);
    char arg0[] = "test-emoji-picker", arg1[] = "--disable=all", arg2[] = "--enable=emoji-picker,testfrontend,testim";
    char *args[] = {arg0,arg1,arg2};
    int result = 0;
    try {
        Instance instance(3,args);
        instance.addonManager().registerDefaultLoader(nullptr);
        EventDispatcher dispatcher;
        dispatcher.attach(&instance.eventLoop());
        dispatcher.schedule([&]() {
            try {
                auto *picker = instance.addonManager().addon("emoji-picker", true);
                auto *frontend = instance.addonManager().addon("testfrontend", true);
                auto *im = instance.addonManager().addon("testim", true);
                require(picker && frontend && im, "Addons not loaded");
                require(picker->getConfig(), "Missing GUI configuration");
                RawConfig schema;
                picker->getConfig()->dumpDescription(schema);
                require(!schema.subItems().empty(), "Missing configuration schema");
                require(schema.get("PickerConfig/TriggerKeys/Type")->value() == "List|Key", "Triggers do not use standard key-list editor");
                require(schema.get("PickerConfig/Country/IsEnum")->value() == "True", "Country is not a dropdown");
                require(schema.get("PickerConfig/Country/Enum")->subItems().size() > 250, "Incomplete flag dropdown");
                RawConfig saved;
                picker->getConfig()->save(saved);
                require(saved.get("TriggerKeys/0") && saved.get("Country"), "Missing trigger/country settings");
                for (const auto &field : {"Language","Gender","SecondGender","SkinTone","SecondSkinTone","Hair","Direction","Presentation","Country"})
                    require(bool(saved.get(field)), std::string("Missing GUI field: ") + field);
                RawConfig options;
                options.setValueByPath("TriggerKeys/0", "Control+Alt+e");
                options.setValueByPath("TriggerKeys/1", "Control+Alt+x");
                options.setValueByPath("Gender", "Woman");
                options.setValueByPath("SkinTone", "3");
                options.setValueByPath("PageSize", "3");
                picker->setConfig(options);
                RawConfig examples;
                picker->getConfig()->dumpDescription(examples);
                require(examples.get("PickerConfig/Gender/EnumI18n/2")->value().starts_with("👩🏽"), "Gender example ignores saved skin tone");
                require(examples.get("PickerConfig/SkinTone/EnumI18n/6")->value().starts_with("👩🏿"), "Tone example ignores saved gender");
                require(examples.get("PickerConfig/SecondSkinTone/EnumI18n/0")->value().starts_with("🧑🏽"), "Inherited tone example");
                require(examples.get("PickerConfig/Hair/EnumI18n/1")->value().starts_with("👩🏽‍🦰"), "Hair example ignores other preferences");
                require(examples.get("PickerConfig/Presentation/EnumI18n/2")->value().starts_with("☀︎"), "Missing text presentation example");
                require(examples.get("PickerConfig/Country/EnumI18n/1")->value().find("🇦") == 0, "Missing flag example");
                auto group = instance.inputMethodManager().currentGroup();
                group.inputMethodList().clear();
                group.inputMethodList().push_back(InputMethodGroupItem("testim"));
                instance.inputMethodManager().setGroup(std::move(group));
                // Keep the regular IM from consuming keys; the picker must own
                // only its active interaction.
                im->call<ITestIM::setHandler>([](const InputMethodEntry &, KeyEvent &) {});
                auto uuid = frontend->call<ITestFrontend::createInputContext>("emoji-test");
                auto *ic = instance.inputContextManager().findByUUID(uuid);
                ic->setCapabilityFlags(CapabilityFlag::Preedit);
                auto key = [&](const std::string &name) {
                    if (!ic->hasFocus()) ic->focusIn();
                    KeyEvent event(ic, Key(name), false);
                    return ic->keyEvent(event);
                };
                auto type = [&](const std::string &text) {
                    for (char c : text) require(key(c == ' ' ? "space" : std::string(1,c)), "Typing not handled");
                };
                require(key("Control+Alt+x"), "Second trigger not handled");
                require(bool(ic->inputPanel().candidateList()), "Picker did not open");
                type("cat smil");
                require(ic->inputPanel().clientPreedit().toString() == "cat smil", "Space committed instead of separating terms");
                auto list = ic->inputPanel().candidateList();
                require(list && !list->empty(), "Missing search results");
                auto selected = list->candidate(list->cursorIndex()).text().toString().substr(0, list->candidate(list->cursorIndex()).text().toString().find(" "));
                require(selected == "😸" || selected == "😺" || selected == "😻", "Cat search returned unrelated emoji");
                frontend->call<ITestFrontend::pushCommitExpectation>(selected);
                require(key("Return"), "Commit not handled");
                require(ic->inputPanel().clientPreedit().empty(), "Preedit not cleared on commit");
                require(key("Control+Alt+e"), "First trigger not handled");
                type("woman raising hand");
                list = ic->inputPanel().candidateList();
                require(list->candidate(0).text().toString().starts_with("🙋🏽‍♀️"), "Configured variant not first: " + list->candidate(0).text().toString());
                key("Down");
                require(list->cursorIndex() == 1, "Candidate navigation");
                key("Page_Down");
                require(list->toPageable()->currentPage() == 1, "Paging");
                key("Escape");
                require(!ic->inputPanel().candidateList(), "Cancel left candidates");
                key("Control+Alt+e"); type("zzzznonexistent"); key("Return");
                require(ic->inputPanel().clientPreedit().toString() == "zzzznonexistent", "No-result Enter committed query");
                key("Control+Alt+e");
                require(!ic->inputPanel().candidateList(), "Trigger did not toggle picker off");
                key("Control+Alt+e"); type("cat"); key("Left"); key("BackSpace");
                require(ic->inputPanel().clientPreedit().toString() == "ct", "Cursor/backspace editing");
                key("Home"); key("Delete");
                require(ic->inputPanel().clientPreedit().toString() == "t", "Delete editing");
                key("Control+u");
                require(ic->inputPanel().clientPreedit().empty(), "Clear query");
                key("eacute");
                require(ic->inputPanel().clientPreedit().toString() == "é", "Unicode query input");
                key("BackSpace");
                require(ic->inputPanel().clientPreedit().empty(), "Unicode backspace");
                type("smil");
                ic->focusOut();
                require(!ic->inputPanel().candidateList() && ic->inputPanel().clientPreedit().empty(), "Focus loss did not cancel");
                require(!key("Escape"), "Picker still owns keyboard after focus loss");
                ic->setCapabilityFlags(CapabilityFlag::Password);
                require(!key("Control+Alt+e"), "Password trigger accepted");
                ic->setCapabilityFlags(CapabilityFlag::Sensitive);
                require(!key("Control+Alt+e"), "Sensitive trigger accepted");
                ic->setCapabilityFlags(CapabilityFlag::Preedit);
                ic->inputPanel().setClientPreedit(Text("unfinished"));
                require(!key("Control+Alt+e"), "Existing composition discarded");
                ic->inputPanel().reset();
                key("Control+Alt+e"); type("cat");
                RawConfig change; change.setValueByPath("Language", "de"); picker->setConfig(change);
                key("Control+u"); type("katze");
                require(ic->inputPanel().candidateList() && !ic->inputPanel().candidateList()->empty(), "Live config change");
                key("Escape");
                require(std::filesystem::exists(testHome / "conf/emoji-picker.conf"), "Configuration not saved");
                picker->reloadConfig();
                RawConfig reloaded; picker->getConfig()->save(reloaded);
                require(reloaded.get("Language")->value() == "de" && reloaded.get("TriggerKeys/1"), "Saved settings not reloaded");
                key("Control+Alt+x"); type("katze");
                require(!ic->inputPanel().candidateList()->empty(), "Reload broke engine");
                // Candidate clicks must commit only the glyph, including on
                // old releases where its display text also contains the name.
                list = ic->inputPanel().candidateList();
                auto clicked = list->candidate(0).text().toString();
                clicked = clicked.substr(0, clicked.find(" "));
                frontend->call<ITestFrontend::pushCommitExpectation>(clicked);
                list->candidate(0).select(ic);
                require(!ic->inputPanel().candidateList(), "Click left picker active");
                key("Control+Alt+x"); type("katze");
                InvokeActionEvent action(InvokeActionEvent::Action::LeftClick, 1, ic);
                ic->invokeAction(action);
                require(action.accepted(), "Preedit click not handled");
                key("Delete");
                require(ic->inputPanel().clientPreedit().toString() == "ktze", "Preedit click did not move cursor");
                ic->reset();
                require(!ic->inputPanel().candidateList(), "Input reset left picker active");
                require(!key("Escape"), "Picker owns keyboard after input reset");
                key("Control+Alt+x"); type("katze");
                InputContextSwitchInputMethodEvent switched(InputMethodSwitchedReason::Other, "testim", ic);
                instance.postEvent(switched);
                require(!ic->inputPanel().candidateList(), "Input method switch left picker active");
                key("Control+Alt+x"); type("katze");
                frontend->call<ITestFrontend::destroyInputContext>(uuid);
                // Destruction while open must be safe.
                std::cout << "Native picker integration passed\n";
            } catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
            instance.exit();
        });
        instance.exec();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::filesystem::remove_all(testHome);
    return result;
}
