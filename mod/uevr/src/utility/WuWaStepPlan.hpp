#pragma once

// Guided test plans for playtesting in the headset (docs/VR-MENU.md).
//
// A plan is written on the PC (by hand or by Claude) to
// <profile>/wuwa-steps/plan.json. The game shows the current step in the
// headset, the player answers from the VR menu, and every answer or mark is
// appended to <profile>/wuwa-steps/results.jsonl for the PC side to read.
// A step may switch UEVR settings for an A/B comparison; touched settings are
// restored when the plan finishes, is removed, or is replaced.
//
// Local files only. Plans are display text plus setting values, size-limited
// and validated; nothing in a plan can run commands or reach the network.
// UI thread only.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace wuwa_steps {
using Json = nlohmann::json;

struct Step {
    std::string id, title, action, expect;
    std::vector<std::string> answers;
    std::map<std::string, std::string> settings; // UEVR config key -> value while this step is current
};

struct Plan {
    std::string id, title, scene;
    std::vector<Step> steps;
};

constexpr std::size_t max_plan_bytes = 64 * 1024;
constexpr std::size_t max_steps = 60;
constexpr std::size_t max_answers = 8;
constexpr std::size_t max_text = 600;
constexpr std::size_t max_label = 80;

inline bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

inline std::string text(const Json& object, const char* key, std::size_t limit, bool required) {
    if (!object.contains(key)) {
        if (required) throw std::runtime_error(std::string("missing \"") + key + "\"");
        return {};
    }
    if (!object.at(key).is_string()) throw std::runtime_error(std::string("\"") + key + "\" must be text");
    auto value = object.at(key).get<std::string>();
    if (value.size() > limit) throw std::runtime_error(std::string("\"") + key + "\" is too long");
    if (required && value.empty()) throw std::runtime_error(std::string("\"") + key + "\" is empty");
    return value;
}

// Throws std::runtime_error naming the first problem.
inline Plan parse(const Json& json) {
    if (!json.is_object()) throw std::runtime_error("plan must be a JSON object");
    if (!json.contains("version") || json.at("version") != 1) throw std::runtime_error("\"version\" must be 1");
    Plan plan;
    plan.id = text(json, "id", 64, true);
    if (!valid_id(plan.id)) throw std::runtime_error("\"id\" may only use letters, digits, - _ .");
    plan.title = text(json, "title", max_label, true);
    plan.scene = text(json, "scene", max_text, false);
    if (!json.contains("steps") || !json.at("steps").is_array() || json.at("steps").empty()) {
        throw std::runtime_error("\"steps\" must be a non-empty list");
    }
    if (json.at("steps").size() > max_steps) throw std::runtime_error("too many steps");
    for (const auto& item : json.at("steps")) {
        if (!item.is_object()) throw std::runtime_error("each step must be an object");
        Step step;
        step.id = text(item, "id", 64, true);
        if (!valid_id(step.id)) throw std::runtime_error("step \"id\" may only use letters, digits, - _ .");
        for (const auto& other : plan.steps) {
            if (other.id == step.id) throw std::runtime_error("duplicate step id " + step.id);
        }
        step.title = text(item, "title", max_label, true);
        step.action = text(item, "do", max_text, false);
        step.expect = text(item, "expect", max_text, false);
        if (item.contains("answers")) {
            if (!item.at("answers").is_array() || item.at("answers").size() > max_answers) {
                throw std::runtime_error("\"answers\" must be a list of at most 8");
            }
            for (const auto& answer : item.at("answers")) {
                if (!answer.is_string() || answer.get<std::string>().empty() || answer.get<std::string>().size() > max_label) {
                    throw std::runtime_error("each answer must be short, non-empty text");
                }
                step.answers.push_back(answer.get<std::string>());
            }
        }
        if (step.answers.empty()) step.answers = {"Pass", "Fail"};
        if (item.contains("settings")) {
            if (!item.at("settings").is_object() || item.at("settings").size() > 16) {
                throw std::runtime_error("\"settings\" must be an object of at most 16 keys");
            }
            for (const auto& [key, value] : item.at("settings").items()) {
                if (!valid_id(key)) throw std::runtime_error("setting key " + key + " is not a UEVR config key");
                if (value.is_boolean()) step.settings[key] = value.get<bool>() ? "true" : "false";
                else if (value.is_number()) step.settings[key] = value.dump();
                else if (value.is_string() && value.get<std::string>().size() <= max_label) step.settings[key] = value.get<std::string>();
                else throw std::runtime_error("setting " + key + " must be true / false, a number or short text");
            }
        }
        plan.steps.push_back(std::move(step));
    }
    return plan;
}

// Reading and writing UEVR settings by config key, supplied by the host.
struct Settings {
    std::function<std::optional<std::string>(const std::string&)> get;
    std::function<bool(const std::string&, const std::string&)> set;
};

class Runner {
public:
    // Reloads plan.json when its size or write time changes, at most every second.
    void poll(const std::filesystem::path& folder, std::int64_t unix_ms, const Settings& settings) {
        if (unix_ms < m_next_poll) return;
        m_next_poll = unix_ms + 1000;
        m_folder = folder;
        std::error_code ec;
        const auto path = folder / "plan.json";
        if (!std::filesystem::exists(path, ec)) {
            if (m_plan) unload(settings, unix_ms, "plan removed");
            m_stamp.clear();
            m_error.clear();
            return;
        }
        const auto size = std::filesystem::file_size(path, ec);
        const auto time = std::filesystem::last_write_time(path, ec);
        const auto stamp = std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
        if (ec || stamp == m_stamp) return;
        m_stamp = stamp;
        try {
            if (size == 0 || size > max_plan_bytes) throw std::runtime_error("plan.json must be 1 byte to 64 KB");
            std::ifstream in{path, std::ios::binary};
            auto plan = parse(Json::parse(in));
            const bool same = m_plan && m_plan->id == plan.id;
            const int keep = same ? (std::min)(m_index, static_cast<int>(plan.steps.size())) : 0;
            if (m_plan && !same) unload(settings, unix_ms, "plan replaced");
            if (!same) m_answers.clear();
            m_plan = std::move(plan);
            m_index = keep;
            m_error.clear();
            log({{"event", same ? "plan_updated" : "plan_loaded"}, {"steps", m_plan->steps.size()}}, unix_ms);
            sync(settings);
        } catch (const std::exception& e) {
            m_error = e.what();
        }
    }

    bool loaded() const { return m_plan.has_value(); }
    bool finished() const { return m_plan && m_index >= static_cast<int>(m_plan->steps.size()); }
    bool active() const { return m_plan && !finished(); }
    const Plan* plan() const { return m_plan ? &*m_plan : nullptr; }
    const Step* step() const { return active() ? &m_plan->steps[m_index] : nullptr; }
    int index() const { return m_index; }
    int count() const { return m_plan ? static_cast<int>(m_plan->steps.size()) : 0; }
    const std::string& error() const { return m_error; }
    std::uint64_t revision() const { return m_revision; }

    // Records an answer for the current step and moves to the next one.
    void answer(const std::string& answer, const Json& context, std::int64_t unix_ms, const Settings& settings) {
        if (!active()) return;
        log({{"event", "answer"}, {"step", step()->id}, {"index", m_index}, {"answer", answer}, {"context", context}}, unix_ms);
        m_answers[m_index] = answer;
        ++m_index;
        sync(settings);
        if (finished()) log({{"event", "plan_finished"}}, unix_ms);
    }

    void mark(const Json& context, std::int64_t unix_ms) {
        if (!m_plan) return;
        log({{"event", "mark"}, {"step", active() ? step()->id : std::string{"finished"}}, {"context", context}}, unix_ms);
    }

    void back(std::int64_t unix_ms, const Settings& settings) {
        if (!m_plan || m_index == 0) return;
        --m_index;
        log({{"event", "back"}, {"step", m_plan->steps[m_index].id}}, unix_ms);
        sync(settings);
    }

    void restart(std::int64_t unix_ms, const Settings& settings) {
        if (!m_plan) return;
        m_index = 0;
        m_answers.clear();
        log({{"event", "restart"}}, unix_ms);
        sync(settings);
    }

    // Jumps to any step without answering; earlier answers stay saved.
    void go(int index, std::int64_t unix_ms, const Settings& settings) {
        if (!m_plan || index < 0 || index >= count() || index == m_index) return;
        m_index = index;
        log({{"event", "go"}, {"step", m_plan->steps[m_index].id}, {"index", m_index}}, unix_ms);
        sync(settings);
    }

    // The latest answer given to a step in this session, or empty.
    std::string answer_for(int index) const {
        const auto it = m_answers.find(index);
        return it == m_answers.end() ? std::string{} : it->second;
    }

    // Settings this plan changed, with the values to restore.
    const std::map<std::string, std::string>& touched() const { return m_original; }

private:
    void unload(const Settings& settings, std::int64_t unix_ms, const char* why) {
        restore_all(settings);
        log({{"event", "plan_unloaded"}, {"why", why}}, unix_ms);
        m_plan.reset();
        m_index = 0;
        m_answers.clear();
        ++m_revision;
    }

    // The current step's settings are applied; anything a previous step
    // changed and this one does not mention goes back to its original value.
    void sync(const Settings& settings) {
        ++m_revision;
        const auto* current = step();
        for (auto it = m_original.begin(); it != m_original.end();) {
            if (!current || !current->settings.contains(it->first)) {
                if (settings.set) settings.set(it->first, it->second);
                it = m_original.erase(it);
            } else {
                ++it;
            }
        }
        if (!current) return;
        for (const auto& [key, value] : current->settings) {
            if (!m_original.contains(key)) {
                const auto before = settings.get ? settings.get(key) : std::nullopt;
                if (!before) continue; // unknown key: leave it alone
                m_original[key] = *before;
            }
            if (settings.set) settings.set(key, value);
        }
    }

    void restore_all(const Settings& settings) {
        for (const auto& [key, value] : m_original) {
            if (settings.set) settings.set(key, value);
        }
        m_original.clear();
    }

    void log(Json line, std::int64_t unix_ms) {
        if (m_folder.empty()) return;
        line["unix_ms"] = unix_ms;
        if (m_plan) line["plan"] = m_plan->id;
        std::error_code ec;
        std::filesystem::create_directories(m_folder, ec);
        std::ofstream out{m_folder / "results.jsonl", std::ios::binary | std::ios::app};
        out << line.dump() << '\n';
    }

    std::optional<Plan> m_plan;
    int m_index{};
    std::int64_t m_next_poll{};
    std::uint64_t m_revision{};
    std::string m_stamp, m_error;
    std::filesystem::path m_folder;
    std::map<std::string, std::string> m_original;
    std::map<int, std::string> m_answers;
};
}
