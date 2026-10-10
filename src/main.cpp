#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/file.hpp>
#ifdef GEODE_IS_WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif
#ifdef GEODE_IS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif
using namespace geode::prelude;

struct KF { float d, u; };
struct Cfg {
    bool enabled = true, startpos = false, practice = false, pauseUndeafen = false;
    bool ctrl = false, shift = false, alt = false;
    int key = 0;
    std::vector<KF> kf{{50.f, 100.f}};
} g;
bool g_deafened = false, g_capturing = false;
std::function<void()> g_onCap;

static std::filesystem::path cfgPath() { return Mod::get()->getSaveDir() / "autodeafen.json"; }

static void save() {
    auto o = matjson::Value::object();
    o["enabled"] = g.enabled; o["startpos"] = g.startpos; o["practice"] = g.practice;
    o["pauseUndeafen"] = g.pauseUndeafen;
    o["ctrl"] = g.ctrl; o["shift"] = g.shift; o["alt"] = g.alt; o["key"] = g.key;
    auto arr = matjson::Value::array();
    for (auto& k : g.kf) {
        auto e = matjson::Value::object();
        e["deafen"] = (double)k.d; e["undeafen"] = (double)k.u;
        arr.push(e);
    }
    o["keyframes"] = arr;
    (void)file::writeString(cfgPath(), o.dump());
}

static void load() {
    auto s = file::readString(cfgPath());
    if (!s) return;
    auto r = matjson::parse(s.unwrap());
    if (!r) return;
    auto o = r.unwrap();
    g.enabled = o["enabled"].as<bool>().unwrapOr(true);
    g.startpos = o["startpos"].as<bool>().unwrapOr(false);
    g.practice = o["practice"].as<bool>().unwrapOr(false);
    g.pauseUndeafen = o["pauseUndeafen"].as<bool>().unwrapOr(false);
    g.ctrl = o["ctrl"].as<bool>().unwrapOr(false);
    g.shift = o["shift"].as<bool>().unwrapOr(false);
    g.alt = o["alt"].as<bool>().unwrapOr(false);
    g.key = o["key"].as<int>().unwrapOr(0);
    if (auto a = o["keyframes"].asArray()) {
        g.kf.clear();
        for (auto& e : a.unwrap())
            g.kf.push_back({(float)e["deafen"].as<double>().unwrapOr(50.0),
                            (float)e["undeafen"].as<double>().unwrapOr(100.0)});
    }
}

$on_mod(Loaded) { load(); }

#ifdef GEODE_IS_MACOS
static int macKey(int k) {
    static const int L[26] = {0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    static const int N[10] = {29,18,19,20,21,23,22,26,28,25};
    static const int F[20] = {122,120,99,118,96,97,98,100,101,109,103,111,105,107,113,106,64,79,80,90};
    if (k >= 'A' && k <= 'Z') return L[k - 'A'];
    if (k >= '0' && k <= '9') return N[k - '0'];
    if (k >= 112 && k <= 131) return F[k - 112];
    return -1;
}
#endif

static void pressCombo() {
    if (!g.key) return;
#ifdef GEODE_IS_WINDOWS
    std::vector<INPUT> v;
    auto add = [&](int vk, bool up) {
        INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = (WORD)vk;
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0; v.push_back(i);
    };
    if (g.ctrl) add(VK_CONTROL, 0);
    if (g.shift) add(VK_SHIFT, 0);
    if (g.alt) add(VK_MENU, 0);
    add(g.key, 0); add(g.key, 1);
    if (g.alt) add(VK_MENU, 1);
    if (g.shift) add(VK_SHIFT, 1);
    if (g.ctrl) add(VK_CONTROL, 1);
    SendInput((UINT)v.size(), v.data(), sizeof(INPUT));
#elif defined(GEODE_IS_MACOS)
    int c = macKey(g.key);
    if (c < 0) return;
    CGEventFlags f = 0;
    if (g.ctrl) f |= kCGEventFlagMaskControl;
    if (g.shift) f |= kCGEventFlagMaskShift;
    if (g.alt) f |= kCGEventFlagMaskAlternate;
    for (bool d : {true, false}) {
        CGEventRef e = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)c, d);
        CGEventSetFlags(e, f);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }
#endif
}

static void setDeafen(bool s) {
    if (s == g_deafened) return;
    g_deafened = s;
    pressCombo();
}

static std::string keyName() {
    if (!g.key) return "Set Key";
    std::string s;
    if (g.ctrl) s += "Ctrl+";
    if (g.shift) s += "Shift+";
    if (g.alt) s += "Alt+";
    return s + CCKeyboardDispatcher::get()->keyToString((enumKeyCodes)g.key);
}

class $modify(ADKeys, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double ts) {
        if (g_capturing && down && !repeat) {
            int k = (int)key;
            if (k == 16 || k == 17 || k == 18 || (k >= 160 && k <= 165))
                return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, ts);
            g_capturing = false;
            if (key != KEY_Escape) {
                g.key = k;
                g.ctrl = this->getControlKeyPressed();
                g.shift = this->getShiftKeyPressed();
                g.alt = this->getAltKeyPressed();
                save();
            }
            if (g_onCap) g_onCap();
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, ts);
    }
};

class DeafenPopup : public Popup {
    CCNode* m_content = nullptr;
    ButtonSprite* m_keySpr = nullptr;

    bool init() {
        if (!Popup::init(360.f, 360.f)) return false;
        this->setTitle("Auto Deafen");
        g_onCap = [this] { this->build(); };
        this->build();
        return true;
    }
    ~DeafenPopup() { g_capturing = false; g_onCap = nullptr; }

    CCLabelBMFont* label(char const* t, float x, float y, float sc = .45f) {
        auto l = CCLabelBMFont::create(t, "bigFont.fnt");
        l->setScale(sc); l->setAnchorPoint({0, .5f}); l->setPosition({x, y});
        m_content->addChild(l);
        return l;
    }
    void toggle(CCMenu* m, float y, bool val, std::function<void(bool)> cb) {
        auto t = CCMenuItemExt::createTogglerWithStandardSprites(.6f, [cb](CCMenuItemToggler* t) {
            cb(!t->isToggled()); save();
        });
        t->toggle(val);
        t->setPosition({m_size.width - 45.f, y});
        m->addChild(t);
    }
    void build() {
        if (m_content) m_content->removeFromParent();
        m_content = CCNode::create();
        m_mainLayer->addChild(m_content);
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_content->addChild(menu);
        float H = m_size.height, W = m_size.width;

        label("Keybind", 30, H - 55);
        m_keySpr = ButtonSprite::create(keyName().c_str(), 130, true, "bigFont.fnt", "GJ_button_04.png", 28.f, .55f);
        auto kb = CCMenuItemExt::createSpriteExtra(m_keySpr, [this](CCMenuItemSpriteExtra*) {
            g_capturing = true;
            m_keySpr->setString("Press a key...");
        });
        kb->setPosition({W - 110.f, H - 55.f});
        menu->addChild(kb);

        if (!g.key) { label("Set a key to unlock options", 70, H - 110, .4f); return; }

        label("Master Control", 30, H - 95);
        toggle(menu, H - 95, g.enabled, [](bool v) { g.enabled = v; });
        label("Work in Startpos", 30, H - 125);
        toggle(menu, H - 125, g.startpos, [](bool v) { g.startpos = v; });
        label("Work in Practice", 30, H - 155);
        toggle(menu, H - 155, g.practice, [](bool v) { g.practice = v; });
        label("Undeafen on Pause", 30, H - 185);
        toggle(menu, H - 185, g.pauseUndeafen, [](bool v) { g.pauseUndeafen = v; });

        label("Deafen %", 105, H - 220, .35f);
        label("Undeafen %", 195, H - 220, .35f);
        for (size_t i = 0; i < g.kf.size(); i++) {
            float y = H - 250.f - i * 34.f;
            label(fmt::format("#{}", i + 1).c_str(), 40, y, .4f);
            auto mk = [&](float x, bool deafen) {
                auto in = TextInput::create(70.f, "%", "bigFont.fnt");
                in->setFilter("0123456789.");
                in->setMaxCharCount(5);
                in->setString(fmt::format("{:g}", deafen ? g.kf[i].d : g.kf[i].u));
                in->setCallback([i, deafen](std::string const& s) {
                    if (i >= g.kf.size()) return;
                    if (auto n = numFromString<float>(s)) {
                        (deafen ? g.kf[i].d : g.kf[i].u) = std::clamp(n.unwrap(), 0.f, 100.f);
                        save();
                    }
                });
                in->setPosition({x, y});
                m_content->addChild(in);
            };
            mk(125, true); mk(215, false);
            auto del = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_deleteIcon_001.png", .5f,
                [this, i](CCMenuItemSpriteExtra*) { g.kf.erase(g.kf.begin() + i); save(); build(); });
            del->setPosition({295, y});
            menu->addChild(del);
        }
        if (g.kf.size() < 3) {
            auto add = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_plusBtn_001.png", .6f,
                [this](CCMenuItemSpriteExtra*) { g.kf.push_back({50.f, 100.f}); save(); build(); });
            add->setPosition({W / 2, 28});
            menu->addChild(add);
        }
    }
public:
    static DeafenPopup* create() {
        auto r = new DeafenPopup();
        if (r->init()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }
};

class $modify(PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto icon = CCSprite::create("headphones.png"_spr);
        auto spr = CircleButtonSprite::create(icon, CircleBaseColor::Green, CircleBaseSize::Medium);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) { DeafenPopup::create()->show(); });
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition({CCDirector::get()->getWinSize().width - 40.f, 40.f});
        this->addChild(menu);
    }
};

class $modify(ADPlay, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        bool ok = g.enabled && g.key
               && !(m_isPracticeMode && !g.practice)
               && !(m_startPosObject && !g.startpos);
        float p = this->getCurrentPercent();
        bool want = false;
        if (ok) for (auto& k : g.kf) if (p >= k.d && p < k.u) { want = true; break; }
        setDeafen(want);
    }
    void pauseGame(bool b) {
        PlayLayer::pauseGame(b);
        if (g.pauseUndeafen) setDeafen(false); // postUpdate re-deafens on resume if still in range
    }
    void levelComplete() { PlayLayer::levelComplete(); setDeafen(false); }
    void onQuit() { setDeafen(false); PlayLayer::onQuit(); }
};
