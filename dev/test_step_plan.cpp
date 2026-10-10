// Checks for guided test plans (mod/uevr/src/utility/WuWaStepPlan.hpp): parsing,
// step progress, A/B settings with restore, the results log and reloading.
// Uses a temporary folder; no game, devices or Windows APIs.
//   g++ -std=c++20 -Wall -Wextra -I<nlohmann/json include> dev/test_step_plan.cpp -o test_step_plan && test_step_plan
#include "../mod/uevr/src/utility/WuWaStepPlan.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace wuwa_steps;
namespace fs = std::filesystem;

namespace {
int checks{};
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}

bool rejects(const Json& plan, const char* fragment) {
    try {
        parse(plan);
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(fragment) != std::string::npos;
    }
    return false;
}

Json sample() {
    return Json::parse(R"({
        "version": 1, "id": "bars-1", "title": "Dialogue black bars", "scene": "Any letterboxed dialogue",
        "steps": [
            {"id": "baseline", "title": "Baseline", "do": "Start a dialogue.", "expect": "Bars in both eyes or none.",
             "answers": ["No bars", "Left eye only", "Right eye only", "Both eyes"]},
            {"id": "screen", "title": "Stereo screen", "settings": {"VR_2DScreenMode": true, "UI_Size": 1.5}},
            {"id": "screen-only", "title": "Screen, default size", "settings": {"VR_2DScreenMode": true}},
            {"id": "done", "title": "Back to normal"}
        ]})");
}

void write(const fs::path& path, const std::string& text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << text;
}

std::vector<Json> results(const fs::path& folder) {
    std::vector<Json> lines;
    std::ifstream in{folder / "results.jsonl"};
    for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    return lines;
}
}

int main() {
    // Parsing.
    const auto plan = parse(sample());
    check(plan.steps.size() == 4 && plan.steps[0].answers.size() == 4, "sample plan did not parse");
    check(plan.steps[1].answers == std::vector<std::string>{"Pass", "Fail"}, "steps without answers did not default to Pass / Fail");
    check(plan.steps[1].settings.at("VR_2DScreenMode") == "true" && plan.steps[1].settings.at("UI_Size") == "1.5",
        "setting values were not converted to UEVR text");
    auto bad = sample(); bad["version"] = 2;
    check(rejects(bad, "version"), "a wrong version was accepted");
    bad = sample(); bad["id"] = "has space";
    check(rejects(bad, "id"), "an unsafe plan id was accepted");
    bad = sample(); bad["steps"][1]["id"] = "baseline";
    check(rejects(bad, "duplicate"), "duplicate step ids were accepted");
    bad = sample(); bad["steps"][0]["answers"] = Json::array({"1", "2", "3", "4", "5", "6", "7", "8", "9"});
    check(rejects(bad, "answers"), "nine answers were accepted");
    bad = sample(); bad["steps"][0]["do"] = std::string(max_text + 1, 'x');
    check(rejects(bad, "too long"), "an over-long instruction was accepted");
    bad = sample(); bad["steps"][1]["settings"] = Json{{"r.Kuro.Letterbox", Json::array()}};
    check(rejects(bad, "setting"), "a list was accepted as a setting value");
    bad = sample(); bad["steps"] = Json::array();
    check(rejects(bad, "steps"), "a plan without steps was accepted");

    // Runner with a fake settings store.
    const auto folder = fs::temp_directory_path() / "wuwa-step-plan-test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    std::map<std::string, std::string> store{{"VR_2DScreenMode", "false"}, {"UI_Size", "2.000000"}};
    Settings settings;
    settings.get = [&](const std::string& key) -> std::optional<std::string> {
        const auto found = store.find(key);
        return found == store.end() ? std::nullopt : std::optional<std::string>{found->second};
    };
    settings.set = [&](const std::string& key, const std::string& value) { store[key] = value; return true; };

    Runner runner;
    std::int64_t now = 1'000'000;
    runner.poll(folder, now, settings);
    check(!runner.loaded() && runner.error().empty(), "an empty folder produced a plan or an error");
    write(folder / "plan.json", sample().dump());
    runner.poll(folder, now + 500, settings);
    check(!runner.loaded(), "polling ran more than once a second");
    now += 1000;
    runner.poll(folder, now, settings);
    check(runner.active() && runner.index() == 0 && runner.step()->id == "baseline", "the plan did not load on step 1");

    runner.answer("Right eye only", Json{{"recording", "idle"}}, now, settings);
    check(runner.index() == 1 && store["VR_2DScreenMode"] == "true" && store["UI_Size"] == "1.5",
        "step 2 did not apply its settings");
    runner.answer("Pass", {}, now, settings);
    check(store["VR_2DScreenMode"] == "true" && store["UI_Size"] == "2.000000",
        "a setting the next step does not mention was not restored");
    runner.back(now, settings);
    check(runner.index() == 1 && store["UI_Size"] == "1.5", "going back did not re-apply the step's settings");
    runner.answer("Fail", {}, now, settings);
    runner.answer("Pass", {}, now, settings);
    check(runner.index() == 3 && store["VR_2DScreenMode"] == "false" && runner.touched().empty(),
        "settings were not restored once no step needed them");
    runner.mark(Json{{"recording", "recording"}}, now);
    runner.answer("Pass", {}, now, settings);
    check(runner.finished() && !runner.active() && runner.step() == nullptr, "the plan did not finish after the last step");
    runner.answer("Pass", {}, now, settings);
    check(runner.index() == 4, "an answer after the end moved past it");

    const auto lines = results(folder);
    int answers = 0, marks = 0;
    for (const auto& line : lines) {
        answers += line.value("event", "") == "answer";
        marks += line.value("event", "") == "mark";
        check(line.value("plan", "") == "bars-1" && line.contains("unix_ms"), "a result line lacks its plan or time");
    }
    check(answers == 5 && marks == 1, "the results log does not hold every answer and mark");
    check(lines.front().value("event", "") == "plan_loaded" && lines.back().value("event", "") == "plan_finished",
        "the results log does not start with the load and end with the finish");
    check(lines[1].value("answer", "") == "Right eye only" && lines[1].at("context").value("recording", "") == "idle",
        "an answer lost its text or context");

    // Restart and reloads.
    runner.restart(now, settings);
    check(runner.index() == 0 && runner.active(), "restart did not return to step 1");
    runner.answer("No bars", {}, now, settings);
    check(store["VR_2DScreenMode"] == "true", "restart lost the step settings");
    write(folder / "plan.json", "{ \"version\": 1, \"id\": ");
    now += 1000;
    runner.poll(folder, now, settings);
    check(runner.active() && runner.index() == 1 && !runner.error().empty(),
        "a half-written plan replaced the running one or reported nothing");
    auto edited = sample();
    edited["steps"][0]["title"] = "Baseline (edited)";
    write(folder / "plan.json", edited.dump());
    now += 1000;
    runner.poll(folder, now, settings);
    check(runner.index() == 1 && runner.error().empty() && runner.plan()->steps[0].title == "Baseline (edited)",
        "editing the same plan lost the current step");
    auto other = sample();
    other["id"] = "ao-1";
    write(folder / "plan.json", other.dump(1));
    now += 1000;
    runner.poll(folder, now, settings);
    check(runner.plan()->id == "ao-1" && runner.index() == 0 && store["VR_2DScreenMode"] == "false",
        "a new plan did not start fresh with settings restored");
    runner.answer("Pass", {}, now, settings);
    fs::remove(folder / "plan.json");
    now += 1000;
    runner.poll(folder, now, settings);
    check(!runner.loaded() && store["VR_2DScreenMode"] == "false" && store["UI_Size"] == "2.000000",
        "removing the plan did not unload it and restore settings");

    // Unknown setting keys are skipped and never restored to a guessed value.
    auto unknown = sample();
    unknown["steps"][0]["settings"] = Json{{"VR_NotARealKey", 1}};
    write(folder / "plan.json", unknown.dump());
    now += 1000;
    runner.poll(folder, now, settings);
    check(runner.active() && !store.contains("VR_NotARealKey") && runner.touched().empty(), "an unknown key was written");

    fs::remove_all(folder);
    std::cout << "PASS: " << checks << " guided test plan checks (temporary folder, fake settings)\n";
    return 0;
}
