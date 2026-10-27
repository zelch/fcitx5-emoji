// SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "config.h"
#include "engine.h"
#include "compat.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterface.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/inputbuffer.h>
#include <fcitx-utils/textformatflags.h>
#include <functional>
#include <memory>

namespace emoji {
using namespace fcitx;
struct PickerState {
    InputBuffer buffer;
    std::string error;
    PickerState() { buffer.setMaxSize(256); }
};
class EmojiCandidate : public CandidateWord {
public:
    EmojiCandidate(std::string glyph, std::string name,
                   std::function<void(InputContext *, const std::string &)> commit)
        : CandidateWord(Text(glyph)), glyph_(std::move(glyph)), commit_(std::move(commit)) {
#if EMOJI_HAS_CANDIDATE_COMMENT
        setComment(Text(std::move(name)));
#else
        setText(Text(glyph_ + " " + name));
#endif
    }
    void select(InputContext *ic) const override {
        // reset destroys the candidate list; copy everything needed first.
        auto commit = commit_;
        auto glyph = glyph_;
        commit(ic, glyph);
    }
private:
    std::string glyph_;
    std::function<void(InputContext *, const std::string &)> commit_;
};
class Picker : public AddonInstance, public PickerMode<PickerState> {
public:
    explicit Picker(Instance *instance) : instance_(instance) {
        readAsIni(config_, "conf/emoji-picker.conf");
        registerMode(instance_);
    }
    ~Picker() override { unregisterMode(); }
    std::string_view name() const override { return "emoji-picker"; }
    const Configuration *getConfig() const override {
        updateExamples(config_);
        return &config_;
    }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/emoji-picker.conf");
        refresh();
    }
    void reloadConfig() override {
        readAsIni(config_, "conf/emoji-picker.conf");
        // Recreate the Lua state so edited scripts and metadata reload too.
        engine_.reset();
        refresh();
    }
    bool triggerTempMode(const KeyEvent &event) override {
        auto *ic = event.inputContext();
        if (event.isRelease() || !matchesTrigger(event) ||
            ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive) ||
            !ic->inputPanel().preedit().empty() || !ic->inputPanel().clientPreedit().empty()) return false;
        auto *state = property(ic);
        state->buffer.clear();
        state->setActive(true);
        update(ic);
        return true;
    }
    void reset(InputContext *ic) override {
        auto *state = property(ic);
        state->setActive(false);
        state->buffer.clear();
        state->error.clear();
        ic->inputPanel().reset();
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    bool keyEvent(const KeyEvent &event) override {
        if (event.isRelease()) return true;
        auto *ic = event.inputContext();
        const auto key = event.key();
        auto &buffer = property(ic)->buffer;
        auto list = ic->inputPanel().candidateList();
        if (key.check(FcitxKey_Escape) || matchesTrigger(event)) {
            reset(ic); return true;
        }
        if (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) {
            if (list && !list->empty() && list->cursorIndex() >= 0) list->candidate(list->cursorIndex()).select(ic);
            return true;
        }
        if (key.check(FcitxKey_Up) || key.check(Key("Shift+Tab")) || key.check(FcitxKey_Down) || key.check(FcitxKey_Tab)) {
            if (list && !list->empty()) {
                if (key.check(FcitxKey_Up) || key.check(Key("Shift+Tab"))) list->toCursorMovable()->prevCandidate();
                else list->toCursorMovable()->nextCandidate();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            }
            return true;
        }
        if (key.check(FcitxKey_Page_Up) || key.check(FcitxKey_Page_Down)) {
            if (list && !list->empty()) {
                auto *page = list->toPageable();
                if (key.check(FcitxKey_Page_Up) && page->hasPrev()) page->prev();
                if (key.check(FcitxKey_Page_Down) && page->hasNext()) page->next();
                ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            }
            return true;
        }
        if (key.check(FcitxKey_Left) || key.check(FcitxKey_Right) || key.check(FcitxKey_Home) || key.check(FcitxKey_End)) {
            if (key.check(FcitxKey_Left) && buffer.cursor()) buffer.setCursor(buffer.cursor()-1);
            if (key.check(FcitxKey_Right) && buffer.cursor() < buffer.size()) buffer.setCursor(buffer.cursor()+1);
            if (key.check(FcitxKey_Home)) buffer.setCursor(0);
            if (key.check(FcitxKey_End)) buffer.setCursor(buffer.size());
            header(ic); return true;
        }
        if (key.check(FcitxKey_BackSpace)) buffer.backspace();
        else if (key.check(FcitxKey_Delete)) buffer.del();
        else if (key.check(Key("Control+u").normalize())) buffer.clear();
        else {
            if (key.isModifier() || key.hasModifier()) return true;
            auto composed = instance_->processComposeString(ic, key.sym());
            if (!composed) return true;
            auto text = composed->empty() ? Key::keySymToUTF8(key.sym()) : *composed;
            if (text.empty() || static_cast<unsigned char>(text[0]) < 0x20 || text[0] == 0x7f) return true;
            buffer.type(text);
        }
        update(ic);
        return true;
    }
    bool invokeAction(InvokeActionEvent &event) override {
        auto *ic = event.inputContext();
        if (event.action() == InvokeActionEvent::Action::LeftClick) {
            auto &buffer = property(ic)->buffer;
            buffer.setCursor(std::min(static_cast<size_t>(std::max(0, event.cursor())), buffer.size()));
            header(ic);
        }
        return true;
    }
private:
    bool matchesTrigger(const KeyEvent &event) const {
        for (const auto &key : *config_.triggerKeys) {
            if (matchesKey(event, key)) return true;
        }
        return false;
    }
    Settings settings() const {
        RawConfig raw;
        config_.save(raw);
        Settings result;
        for (const auto &key : raw.subItems()) result[key] = raw.get(key)->value();
        return result;
    }
    void refresh() {
        instance_->inputContextManager().foreach([this](InputContext *ic) {
            if (property(ic)->isActive()) update(ic);
            return true;
        });
    }
    void header(InputContext *ic) {
        auto *state = property(ic);
        auto &panel = ic->inputPanel();
        Text text(state->buffer.userInput(), TextFormatFlags{TextFormatFlag::Underline} | TextFormatFlag::DontCommit);
        text.setCursor(state->buffer.cursorByChar());
        if (ic->capabilityFlags().test(CapabilityFlag::Preedit)) {
            panel.setClientPreedit(text);
            panel.setPreedit(Text());
        } else {
            panel.setPreedit(text);
            panel.setClientPreedit(Text());
        }
        panel.setAuxUp(Text("Emoji search"));
        panel.setAuxDown(Text(state->error.empty() ? "Space separates words; Enter inserts; Esc cancels" : state->error));
        ic->updatePreedit();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    void update(InputContext *ic) {
        auto *state = property(ic);
        auto list = std::make_unique<CommonCandidateList>();
        list->setPageSize(*config_.pageSize);
        list->setLayoutHint(CandidateLayoutHint::Vertical);
        list->setCursorIncludeUnselected(false);
        list->setCursorKeepInSamePage(false);
        list->setCursorPositionAfterPaging(CursorPositionAfterPaging::ResetToFirst);
        state->error.clear();
        try {
            if (!engine_) engine_ = std::make_unique<Engine>();
            auto rows = engine_->search(state->buffer.userInput(), settings());
            for (auto &[glyph, name] : rows) {
                list->append<EmojiCandidate>(std::move(glyph), std::move(name), [this](InputContext *context, const std::string &text) {
                    reset(context);
                    context->commitString(text);
                });
            }
            if (rows.empty()) state->error = "No matching emoji";
        } catch (const std::exception &error) { state->error = error.what(); }
        if (!list->empty()) list->setGlobalCursorIndex(0);
        ic->inputPanel().setCandidateList(std::move(list));
        header(ic);
    }
    Instance *instance_;
    PickerConfig config_;
    std::unique_ptr<Engine> engine_;
};
class Factory : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override { return new Picker(manager->instance()); }
};
}
FCITX_ADDON_FACTORY(emoji::Factory)
