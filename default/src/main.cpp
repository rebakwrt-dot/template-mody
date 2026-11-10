#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <unordered_map>
#include <array>
#include <vector>
#include <random>
#include <sstream>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    using QRow = std::array<float, 2>; // [0] = release, [1] = hold

    double getD(char const* k) { return Mod::get()->getSettingValue<double>(k); }
    int getI(char const* k) { return static_cast<int>(Mod::get()->getSettingValue<int64_t>(k)); }

    std::mt19937& rng() {
        static std::mt19937 r{std::random_device{}()};
        return r;
    }
    float rand01() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

    struct Step { uint64_t key; int action; float reward; };
}

class $modify(RLPlayLayer, PlayLayer) {
    struct Fields {
        std::unordered_map<uint64_t, QRow> q;
        std::vector<Step> traj;
        int frame = 0;
        int attempts = 0;
        float bestPercent = 0.f;
        float lastPercent = 0.f;
        int curAction = 0;
        bool dead = false;
        bool done = false;
    };

    // ---------- persistence (per level) ----------
    std::string saveKey() {
        return "level-" + std::to_string(m_level->m_levelID.value());
    }

    void loadKnowledge() {
        auto f = m_fields.self();
        f->q.clear();
        auto data = Mod::get()->getSavedValue<std::string>(saveKey(), "");
        std::stringstream ss(data);
        std::string tok;
        bool header = true;
        while (std::getline(ss, tok, ';')) {
            std::stringstream ts(tok);
            if (header) {
                ts >> f->attempts;
                ts.ignore(1);
                ts >> f->bestPercent;
                header = false;
                continue;
            }
            uint64_t k; float a, b; char c;
            if (ts >> k >> c >> a >> c >> b) f->q[k] = {a, b};
        }
    }

    void saveKnowledge() {
        auto f = m_fields.self();
        std::stringstream ss;
        ss << f->attempts << "," << f->bestPercent << ";";
        for (auto& [k, v] : f->q) ss << k << "," << v[0] << "," << v[1] << ";";
        Mod::get()->setSavedValue<std::string>(saveKey(), ss.str());
    }

    // ---------- hooks ----------
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        loadKnowledge();
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        if (Mod::get()->getSettingValue<bool>("reset-knowledge")) {
            f->q.clear();
            f->attempts = 0;
            f->bestPercent = 0.f;
            saveKnowledge();
            Mod::get()->setSettingValue<bool>("reset-knowledge", false);
        }
        f->traj.clear();
        f->frame = 0;
        f->lastPercent = 0.f;
        f->curAction = 0;
        f->dead = false;
        f->done = false;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (f->dead || f->done || m_player1->m_isDead) return;

        int repeat = getI("action-repeat");
        if (f->frame++ % repeat != 0) return;

        // 1) reward for the previous step: progress (+ exploration bonus for new territory)
        float percent = this->getCurrentPercent();
        if (!f->traj.empty()) {
            float delta = std::max(0.f, percent - f->lastPercent);
            float r = delta * getD("reward-per-percent");
            if (percent > f->bestPercent) {
                r += (percent - std::max(f->bestPercent, f->lastPercent)) * getD("exploration-bonus");
                f->bestPercent = percent;
            }
            f->traj.back().reward = r;
        }
        f->lastPercent = percent;

        // 2) state: time bucket + grounded + vertical direction
        uint64_t key = (uint64_t)(f->frame / repeat) * 8
                     + (m_player1->m_isOnGround ? 4 : 0)
                     + (m_player1->m_yVelocity > 0 ? 2 : 0)
                     + (m_player1->m_isUpsideDown ? 1 : 0);
        QRow& row = f->q[key];

        // 3) epsilon-greedy action
        double eps = std::max(getD("epsilon-min"),
                              getD("epsilon-start") * std::pow(getD("epsilon-decay"), f->attempts));
        int action;
        if (rand01() < eps) action = rand01() < getD("jump-chance") ? 1 : 0;
        else if (row[0] == row[1]) action = 0;
        else action = row[1] > row[0] ? 1 : 0;

        f->traj.push_back({key, action, 0.f});

        // 4) apply input
        if (action == 1 && f->curAction == 0) m_player1->pushButton(PlayerButton::Jump);
        if (action == 0 && f->curAction == 1) m_player1->releaseButton(PlayerButton::Jump);
        f->curAction = action;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);
        auto f = m_fields.self();
        if (player == m_player1 && !f->dead && !f->done) {
            f->dead = true;
            if (Mod::get()->getSettingValue<bool>("enabled")) learn(false);
        }
    }

    void levelComplete() {
        auto f = m_fields.self();
        if (!f->done && !f->dead && Mod::get()->getSettingValue<bool>("enabled")) {
            f->done = true;
            learn(true);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        saveKnowledge();
        PlayLayer::onQuit();
    }

    // ---------- learning ----------
    void learn(bool won) {
        auto f = m_fields.self();
        if (f->traj.empty()) return;

        if (won) {
            f->traj.back().reward += 100.f * (float)getD("reward-per-percent");
        } else {
            // dying: reward of the final step becomes 0, and the steps just before it are penalised
            f->traj.back().reward = 0.f;
            int window = getI("death-window");
            float pen = (float)getD("death-penalty");
            for (int i = 0; i < window && i < (int)f->traj.size(); i++) {
                f->traj[f->traj.size() - 1 - i].reward -= pen * (1.f - (float)i / window);
            }
        }

        // backward Q-learning pass over the attempt (terminal state value = 0)
        float alpha = (float)getD("learning-rate");
        float gamma = (float)getD("discount");
        float nextMax = 0.f;
        for (int i = (int)f->traj.size() - 1; i >= 0; i--) {
            auto& s = f->traj[i];
            QRow& row = f->q[s.key];
            row[s.action] += alpha * (s.reward + gamma * nextMax - row[s.action]);
            nextMax = std::max(row[0], row[1]);
        }

        f->attempts++;
        if (f->attempts % 10 == 0 || won) saveKnowledge();
    }
};
