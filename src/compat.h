// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-FileCopyrightText: 2026 CSSlayer <wengxt@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later
// Older-Fcitx event handling is adapted from Fcitx5
// src/lib/fcitx/tempmodemanager.cpp (TempModeManager).
#pragma once
#include <fcitx/instance.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputcontext.h>
#include <fcitx/event.h>
#include <memory>
#include <vector>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#else
#include <fcitx-utils/standardpath.h>
#endif
#if __has_include(<fcitx/tempmode.h>)
#include <fcitx/tempmode.h>
#include <fcitx/tempmodemanager.h>
#endif
namespace emoji {
inline std::string packageFile(const std::string &name) {
#if __has_include(<fcitx-utils/standardpaths.h>)
    return fcitx::StandardPaths::global().locate(fcitx::StandardPathsType::PkgData, name).string();
#else
    return fcitx::StandardPath::global().locate(fcitx::StandardPath::Type::PkgData, name);
#endif
}
inline std::string fcitxDirectory(const char *name, const char *child = "") {
#if __has_include(<fcitx-utils/standardpaths.h>)
    return fcitx::StandardPaths::fcitxPath(name, child).string();
#else
    if (!*child) return fcitx::StandardPath::fcitxPath(name);
    return fcitx::StandardPath::fcitxPath(name, child);
#endif
}
inline bool matchesKey(const fcitx::KeyEvent &event, const fcitx::Key &key) {
    auto normalized = key.normalize();
    // These primitives are available on 5.1.3 too. Check the original layout
    // as well as the input-method-translated layout, and retain keycode keys.
    if (key.code()) return event.rawKey().check(normalized);
    return event.key().check(normalized) || event.origKey().normalize().check(normalized);
}
#if __has_include(<fcitx/tempmode.h>)
template<typename State>
class PickerMode : public fcitx::SimpleTempMode<State> {
protected:
    void registerMode(fcitx::Instance *instance) { instance->tempModeManager().registerTempMode(*this); }
    void unregisterMode() { this->unregister(); }
};
#else
// Older Fcitx releases implement temporary modes through event watchers.
// Keep keyboard ownership and state scoped to each input context, just as
// TempModeManager does on newer releases.
template<typename State>
class PickerMode {
    struct ContextState : fcitx::InputContextProperty, State {
        bool active = false;
        bool isActive() const { return active; }
        void setActive(bool value) { active = value; }
    };
public:
    virtual ~PickerMode() { unregisterMode(); }
    virtual std::string_view name() const = 0;
    virtual bool triggerTempMode(const fcitx::KeyEvent &) = 0;
    virtual bool keyEvent(const fcitx::KeyEvent &) = 0;
    virtual bool invokeAction(fcitx::InvokeActionEvent &) = 0;
    virtual void reset(fcitx::InputContext *) = 0;
protected:
    ContextState *property(fcitx::InputContext *ic) { return ic->propertyFor(&factory_); }
    void registerMode(fcitx::Instance *instance) {
        instance->inputContextManager().registerProperty(std::string(name()), &factory_);
        auto watch = [&](fcitx::EventType type, fcitx::EventWatcherPhase phase, fcitx::EventHandler handler) {
            handlers_.push_back(instance->watchEvent(type, phase, std::move(handler)));
        };
        watch(fcitx::EventType::InputContextKeyEvent, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &base) {
                auto &event = static_cast<fcitx::KeyEvent &>(base);
                if (property(event.inputContext())->isActive()) {
                    event.filter();
                    if (keyEvent(event)) event.accept();
                }
            });
        watch(fcitx::EventType::InputContextKeyEvent, fcitx::EventWatcherPhase::Default,
            [this](fcitx::Event &base) {
                auto &event = static_cast<fcitx::KeyEvent &>(base);
                if (!property(event.inputContext())->isActive() && triggerTempMode(event)) event.filterAndAccept();
            });
        watch(fcitx::EventType::InputContextInvokeAction, fcitx::EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &base) {
                auto &event = static_cast<fcitx::InvokeActionEvent &>(base);
                if (property(event.inputContext())->isActive()) {
                    event.filter();
                    if (invokeAction(event)) event.accept();
                }
            });
        for (auto type : {fcitx::EventType::InputContextFocusOut, fcitx::EventType::InputContextReset,
                          fcitx::EventType::InputContextSwitchInputMethod}) {
            watch(type, fcitx::EventWatcherPhase::Default, [this](fcitx::Event &base) {
                auto &event = static_cast<fcitx::InputContextEvent &>(base);
                if (property(event.inputContext())->isActive()) reset(event.inputContext());
            });
        }
    }
    void unregisterMode() { handlers_.clear(); factory_.unregister(); }
private:
    fcitx::SimpleInputContextPropertyFactory<ContextState> factory_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>> handlers_;
};
#endif
}
