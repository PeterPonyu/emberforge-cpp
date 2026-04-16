#include "emberforge/system/buddy.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <unistd.h>
#include <utility>

#include <nlohmann/json.hpp>

namespace emberforge::system {

namespace {

struct BuddyTemplate {
    const char* name;
    const char* species;
    const char* personality;
};

constexpr std::array<BuddyTemplate, 18> BUDDY_TEMPLATES{{
    {"Waddles", "duck", "Quirky and easily amused. Leaves rubber duck debugging tips everywhere."},
    {"Goosberry", "goose", "Assertive and honks at bad code. Takes no prisoners in code reviews."},
    {"Gooey", "blob", "Adaptable and goes with the flow. Sometimes splits into two when confused."},
    {"Whiskers", "cat", "Independent and judgmental. Watches you type with mild disdain."},
    {"Ember", "dragon", "Fiery and passionate about architecture. Hoards good variable names."},
    {"Inky", "octopus", "Multitasker extraordinaire. Wraps tentacles around every problem at once."},
    {"Hoots", "owl", "Wise but verbose. Always says \"let me think about that\" for exactly 3 seconds."},
    {"Waddleford", "penguin", "Cool under pressure. Slides gracefully through merge conflicts."},
    {"Shelly", "turtle", "Patient and thorough. Believes slow and steady wins the deploy."},
    {"Trailblazer", "snail", "Methodical and leaves a trail of useful comments. Never rushes."},
    {"Casper", "ghost", "Ethereal and appears at the worst possible moments with spooky insights."},
    {"Axie", "axolotl", "Regenerative and cheerful. Recovers from any bug with a smile."},
    {"Chill", "capybara", "Zen master. Remains calm while everything around is on fire."},
    {"Spike", "cactus", "Prickly on the outside but full of good intentions. Thrives on neglect."},
    {"Byte", "robot", "Efficient and literal. Processes feedback in binary."},
    {"Flops", "rabbit", "Energetic and hops between tasks. Finishes before you start."},
    {"Spore", "mushroom", "Quietly insightful. Grows on you over time."},
    {"Chonk", "chonk", "Big, warm, and takes up the whole couch. Prioritizes comfort over elegance."},
}};

std::string title_case_species(const std::string& species) {
    if (species.empty()) {
        return species;
    }
    std::string value = species;
    value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}

std::string render_companion(const std::string& prefix,
                             const StarterBuddyCompanion& companion,
                             const std::string& note = {}) {
    std::ostringstream oss;
    oss << prefix << '\n';
    oss << "name: " << companion.name << '\n';
    oss << "species: " << title_case_species(companion.species) << '\n';
    oss << "personality: " << companion.personality << '\n';
    oss << "status: " << (companion.muted ? "muted" : "active");
    if (!note.empty()) {
        oss << '\n' << note;
    }
    return oss.str();
}

std::string render_command(const std::string& prefix, std::initializer_list<std::string> lines) {
    std::ostringstream oss;
    oss << prefix;
    for (const auto& line : lines) {
        oss << '\n' << line;
    }
    return oss.str();
}

StarterBuddyCompanion next_template(std::size_t& next_index) {
    const auto& templ = BUDDY_TEMPLATES[next_index % BUDDY_TEMPLATES.size()];
    next_index += 1;
    return StarterBuddyCompanion{
        .name = templ.name,
        .species = templ.species,
        .personality = templ.personality,
        .muted = false,
    };
}

std::filesystem::path default_state_path() {
    const char* explicit_path = std::getenv("EMBER_BUDDY_STATE_PATH");
    if (explicit_path && explicit_path[0] != '\0') {
        return std::filesystem::path(explicit_path);
    }

    const char* config_home = std::getenv("EMBER_CONFIG_HOME");
    if (config_home && config_home[0] != '\0') {
        return std::filesystem::path(config_home) / "buddy-state.json";
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".emberforge" / "buddy-state.json";
    }

    if (auto* pw = getpwuid(getuid()); pw && pw->pw_dir) {
        return std::filesystem::path(pw->pw_dir) / ".emberforge" / "buddy-state.json";
    }

    return std::filesystem::path(".emberforge") / "buddy-state.json";
}

} // namespace

StarterBuddyState::StarterBuddyState(std::filesystem::path state_path)
    : state_path_(state_path.empty() ? default_state_path() : std::move(state_path)) {
    load();
}

void StarterBuddyState::load() {
    std::ifstream in(state_path_);
    if (!in) {
        return;
    }

    try {
        nlohmann::json data;
        in >> data;
        next_index_ = data.value("next_index", static_cast<std::size_t>(0));
        if (data.contains("companion") && !data["companion"].is_null()) {
            const auto& companion = data["companion"];
            companion_ = StarterBuddyCompanion{
                .name = companion.value("name", ""),
                .species = companion.value("species", ""),
                .personality = companion.value("personality", ""),
                .muted = data.value("muted", false),
            };
        }
    } catch (...) {
        next_index_ = 0;
        companion_.reset();
    }
}

void StarterBuddyState::persist() const {
    std::filesystem::create_directories(state_path_.parent_path());
    nlohmann::json data;
    data["next_index"] = next_index_;
    data["muted"] = companion_ ? companion_->muted : false;
    if (companion_) {
        data["companion"] = {
            {"name", companion_->name},
            {"species", companion_->species},
            {"personality", companion_->personality},
        };
    } else {
        data["companion"] = nullptr;
    }

    std::ofstream out(state_path_);
    if (!out) {
        return;
    }
    out << data.dump(2) << '\n';
}

std::optional<StarterBuddyCompanion> StarterBuddyState::current() const {
    return companion_;
}

std::pair<bool, StarterBuddyCompanion> StarterBuddyState::hatch() {
    if (companion_) {
        return {false, *companion_};
    }
    companion_ = next_template(next_index_);
    persist();
    return {true, *companion_};
}

StarterBuddyCompanion StarterBuddyState::rehatch() {
    companion_ = next_template(next_index_);
    persist();
    return *companion_;
}

std::optional<StarterBuddyCompanion> StarterBuddyState::mute() {
    if (!companion_) {
        return std::nullopt;
    }
    companion_->muted = true;
    persist();
    return companion_;
}

std::optional<StarterBuddyCompanion> StarterBuddyState::unmute() {
    if (!companion_) {
        return std::nullopt;
    }
    companion_->muted = false;
    persist();
    return companion_;
}

std::string execute_buddy_command(StarterBuddyState& state, const std::string& payload) {
    std::istringstream iss(payload);
    std::string action;
    iss >> action;

    if (action.empty()) {
        const auto companion = state.current();
        if (!companion) {
            return "[command] buddy\nstatus: no companion\ntip: use /buddy hatch to get one";
        }
        return render_companion(
            "[command] buddy",
            *companion,
            "commands: /buddy pet /buddy mute /buddy unmute /buddy hatch /buddy rehatch"
        );
    }

    if (action == "hatch") {
        if (state.current()) {
            return render_command(
                "[command] buddy hatch",
                {
                    "status: companion already active",
                    "tip: use /buddy to inspect it or /buddy rehatch to replace it",
                }
            );
        }
        const auto [created, companion] = state.hatch();
        (void)created;
        return render_companion(
            "[command] buddy hatch",
            companion,
            "note: starter buddy translation from claude-code-src"
        );
    }

    if (action == "rehatch") {
        return render_companion(
            "[command] buddy rehatch",
            state.rehatch(),
            "note: previous companion replaced"
        );
    }

    if (action == "pet") {
        const auto companion = state.current();
        if (!companion) {
            return "[command] buddy pet\nstatus: no companion\ntip: use /buddy hatch to get one";
        }
        std::ostringstream oss;
        oss << "[command] buddy pet\n";
        oss << "reaction: " << companion->name << " purrs happily!\n";
        oss << "status: " << (companion->muted ? "muted" : "active");
        return oss.str();
    }

    if (action == "mute") {
        const auto companion = state.current();
        if (!companion) {
            return "[command] buddy mute\nstatus: no companion\ntip: use /buddy hatch to get one";
        }
        if (companion->muted) {
            return render_command(
                "[command] buddy mute",
                {
                    "status: already muted",
                    "tip: use /buddy unmute to bring it back",
                }
            );
        }
        state.mute();
        return render_command(
            "[command] buddy mute",
            {
                "status: muted",
                "note: companion will hide quietly until /buddy unmute",
            }
        );
    }

    if (action == "unmute") {
        const auto companion = state.current();
        if (!companion) {
            return "[command] buddy unmute\nstatus: no companion\ntip: use /buddy hatch to get one";
        }
        if (!companion->muted) {
            return render_command("[command] buddy unmute", {"status: already active"});
        }
        state.unmute();
        return render_command(
            "[command] buddy unmute",
            {
                "status: active",
                "note: welcome back",
            }
        );
    }

    return "[command] buddy\nunsupported action: " + action +
           "\ncommands: /buddy pet /buddy mute /buddy unmute /buddy hatch /buddy rehatch";
}

} // namespace emberforge::system
