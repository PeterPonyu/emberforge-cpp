#include "emberforge/system/task_question_state.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace emberforge::system {

namespace {

std::string now_iso() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::filesystem::path default_state_path() {
    const char* explicit_path = std::getenv("EMBER_TASK_STATE_PATH");
    if (explicit_path && explicit_path[0] != '\0') {
        return std::filesystem::path(explicit_path);
    }
    const char* config_home = std::getenv("EMBER_CONFIG_HOME");
    if (config_home && config_home[0] != '\0') {
        return std::filesystem::path(config_home) / "task-question-state.json";
    }
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".emberforge" / "task-question-state.json";
    }
    if (auto* pw = getpwuid(getuid()); pw && pw->pw_dir) {
        return std::filesystem::path(pw->pw_dir) / ".emberforge" / "task-question-state.json";
    }
    return std::filesystem::path(".emberforge") / "task-question-state.json";
}

StarterTaskStatus parse_task_status(const std::string& value) {
    if (value == "in_progress") return StarterTaskStatus::InProgress;
    if (value == "waiting_for_user") return StarterTaskStatus::WaitingForUser;
    if (value == "completed") return StarterTaskStatus::Completed;
    if (value == "failed") return StarterTaskStatus::Failed;
    if (value == "stopped") return StarterTaskStatus::Stopped;
    return StarterTaskStatus::Pending;
}

StarterQuestionStatus parse_question_status(const std::string& value) {
    if (value == "answered") return StarterQuestionStatus::Answered;
    return StarterQuestionStatus::Pending;
}

} // namespace

std::string to_string(StarterTaskStatus status) {
    switch (status) {
        case StarterTaskStatus::Pending: return "pending";
        case StarterTaskStatus::InProgress: return "in_progress";
        case StarterTaskStatus::WaitingForUser: return "waiting_for_user";
        case StarterTaskStatus::Completed: return "completed";
        case StarterTaskStatus::Failed: return "failed";
        case StarterTaskStatus::Stopped: return "stopped";
    }
    return "pending";
}

std::string to_string(StarterQuestionStatus status) {
    switch (status) {
        case StarterQuestionStatus::Pending: return "pending";
        case StarterQuestionStatus::Answered: return "answered";
    }
    return "pending";
}

TaskQuestionStateStore::TaskQuestionStateStore(std::filesystem::path state_path)
    : state_path_(state_path.empty() ? default_state_path() : std::move(state_path)) {
    load();
}

namespace {

std::filesystem::path transcript_path_for(const std::filesystem::path& state_path) {
    return state_path.parent_path() / "task-question-transcript.jsonl";
}

void append_transcript_record(const std::filesystem::path& transcript_path,
                              const nlohmann::json& record) {
    std::filesystem::create_directories(transcript_path.parent_path());
    if (!std::filesystem::exists(transcript_path)) {
        std::ofstream meta_out(transcript_path);
        meta_out << nlohmann::json{
            {"type", "session"},
            {"id", "task-question-runtime"},
            {"createdAt", now_iso()},
            {"planMode", false},
        }.dump() << '\n';
    }
    std::ofstream out(transcript_path, std::ios::app);
    if (!out) {
        return;
    }
    out << record.dump() << '\n';
}

} // namespace

void TaskQuestionStateStore::load() {
    std::ifstream in(state_path_);
    if (!in) {
        return;
    }

    try {
        nlohmann::json data;
        in >> data;
        next_task_index_ = data.value("next_task_index", static_cast<std::size_t>(1));
        next_question_index_ = data.value("next_question_index", static_cast<std::size_t>(1));

        tasks_.clear();
        for (const auto& task : data.value("tasks", nlohmann::json::array())) {
            tasks_.push_back(StarterTaskRecord{
                .id = task.value("id", ""),
                .kind = task.value("kind", ""),
                .input = task.value("input", ""),
                .status = parse_task_status(task.value("status", "pending")),
                .question_id = task.contains("question_id") && !task["question_id"].is_null()
                    ? std::optional<std::string>{task["question_id"].get<std::string>()}
                    : std::nullopt,
                .answer = task.contains("answer") && !task["answer"].is_null()
                    ? std::optional<std::string>{task["answer"].get<std::string>()}
                    : std::nullopt,
                .output = task.contains("output") && !task["output"].is_null()
                    ? std::optional<std::string>{task["output"].get<std::string>()}
                    : std::nullopt,
                .created_at = task.value("created_at", ""),
                .updated_at = task.value("updated_at", ""),
            });
        }

        questions_.clear();
        for (const auto& question : data.value("questions", nlohmann::json::array())) {
            questions_.push_back(StarterQuestionRecord{
                .id = question.value("id", ""),
                .task_id = question.value("task_id", ""),
                .text = question.value("text", ""),
                .status = parse_question_status(question.value("status", "pending")),
                .answer = question.contains("answer") && !question["answer"].is_null()
                    ? std::optional<std::string>{question["answer"].get<std::string>()}
                    : std::nullopt,
                .created_at = question.value("created_at", ""),
                .answered_at = question.contains("answered_at") && !question["answered_at"].is_null()
                    ? std::optional<std::string>{question["answered_at"].get<std::string>()}
                    : std::nullopt,
            });
        }
    } catch (...) {
        next_task_index_ = 1;
        next_question_index_ = 1;
        tasks_.clear();
        questions_.clear();
    }
}

void TaskQuestionStateStore::persist() const {
    std::filesystem::create_directories(state_path_.parent_path());
    nlohmann::json data;
    data["next_task_index"] = next_task_index_;
    data["next_question_index"] = next_question_index_;
    data["tasks"] = nlohmann::json::array();
    for (const auto& task : tasks_) {
        data["tasks"].push_back({
            {"id", task.id},
            {"kind", task.kind},
            {"input", task.input},
            {"status", to_string(task.status)},
            {"question_id", task.question_id ? nlohmann::json(*task.question_id) : nlohmann::json(nullptr)},
            {"answer", task.answer ? nlohmann::json(*task.answer) : nlohmann::json(nullptr)},
            {"output", task.output ? nlohmann::json(*task.output) : nlohmann::json(nullptr)},
            {"created_at", task.created_at},
            {"updated_at", task.updated_at},
        });
    }
    data["questions"] = nlohmann::json::array();
    for (const auto& question : questions_) {
        data["questions"].push_back({
            {"id", question.id},
            {"task_id", question.task_id},
            {"text", question.text},
            {"status", to_string(question.status)},
            {"answer", question.answer ? nlohmann::json(*question.answer) : nlohmann::json(nullptr)},
            {"created_at", question.created_at},
            {"answered_at", question.answered_at ? nlohmann::json(*question.answered_at) : nlohmann::json(nullptr)},
        });
    }

    std::ofstream out(state_path_);
    if (!out) {
        return;
    }
    out << data.dump(2) << '\n';
}

StarterTaskRecord TaskQuestionStateStore::create_prompt_task(const std::string& input) {
    StarterTaskRecord task{
        .id = "task-" + std::to_string(next_task_index_++),
        .kind = "prompt",
        .input = input,
        .status = StarterTaskStatus::InProgress,
        .created_at = now_iso(),
        .updated_at = now_iso(),
    };
    tasks_.push_back(task);
    persist();
    append_transcript_record(
        transcript_path_for(state_path_),
        nlohmann::json{
            {"type", "message"},
            {"role", "system"},
            {"blocks", nlohmann::json::array({
                {
                    {"type", "task_state"},
                    {"task_id", task.id},
                    {"status", to_string(task.status)},
                    {"input", task.input},
                }
            })},
            {"timestamp", now_iso()},
        }
    );
    return task;
}

std::vector<StarterTaskRecord> TaskQuestionStateStore::list_tasks() const {
    return tasks_;
}

std::optional<StarterTaskRecord> TaskQuestionStateStore::get_task(const std::string& task_id) const {
    for (const auto& task : tasks_) {
        if (task.id == task_id) {
            return task;
        }
    }
    return std::nullopt;
}

std::pair<StarterTaskRecord, StarterQuestionRecord> TaskQuestionStateStore::ask_question(const std::string& task_id,
                                                                                         const std::string& text) {
    for (auto& task : tasks_) {
        if (task.id != task_id) {
            continue;
        }
        if (task.status == StarterTaskStatus::Completed || task.status == StarterTaskStatus::Failed || task.status == StarterTaskStatus::Stopped) {
            throw std::runtime_error("task is not active: " + task_id);
        }
        StarterQuestionRecord question{
            .id = "question-" + std::to_string(next_question_index_++),
            .task_id = task_id,
            .text = text,
            .status = StarterQuestionStatus::Pending,
            .created_at = now_iso(),
        };
        task.question_id = question.id;
        task.status = StarterTaskStatus::WaitingForUser;
        task.updated_at = now_iso();
        questions_.push_back(question);
        persist();
        append_transcript_record(
            transcript_path_for(state_path_),
            nlohmann::json{
                {"type", "message"},
                {"role", "system"},
                {"blocks", nlohmann::json::array({
                    {
                        {"type", "question_state"},
                        {"question_id", question.id},
                        {"task_id", question.task_id},
                        {"status", to_string(question.status)},
                        {"text", question.text},
                    },
                    {
                        {"type", "task_state"},
                        {"task_id", task.id},
                        {"status", to_string(task.status)},
                        {"question_id", question.id},
                    }
                })},
                {"timestamp", now_iso()},
            }
        );
        return {task, question};
    }
    throw std::runtime_error("task not found: " + task_id);
}

std::vector<StarterQuestionRecord> TaskQuestionStateStore::list_questions(std::optional<StarterQuestionStatus> status) const {
    std::vector<StarterQuestionRecord> result;
    for (const auto& question : questions_) {
        if (!status.has_value() || question.status == *status) {
            result.push_back(question);
        }
    }
    return result;
}

std::pair<StarterTaskRecord, StarterQuestionRecord> TaskQuestionStateStore::answer_question(const std::string& question_id,
                                                                                            const std::string& answer) {
    for (auto& question : questions_) {
        if (question.id != question_id) {
            continue;
        }
        if (question.status == StarterQuestionStatus::Answered) {
            throw std::runtime_error("question already answered: " + question_id);
        }
        question.status = StarterQuestionStatus::Answered;
        question.answer = answer;
        question.answered_at = now_iso();

        for (auto& task : tasks_) {
            if (task.id != question.task_id) {
                continue;
            }
            task.answer = answer;
            task.output = "Task resumed after " + question.id + " and completed with answer: " + answer;
            task.status = StarterTaskStatus::Completed;
            task.updated_at = now_iso();
            persist();
            append_transcript_record(
                transcript_path_for(state_path_),
                nlohmann::json{
                    {"type", "message"},
                    {"role", "system"},
                    {"blocks", nlohmann::json::array({
                        {
                            {"type", "question_state"},
                            {"question_id", question.id},
                            {"task_id", question.task_id},
                            {"status", to_string(question.status)},
                            {"answer", answer},
                        },
                        {
                            {"type", "task_state"},
                            {"task_id", task.id},
                            {"status", to_string(task.status)},
                            {"question_id", question.id},
                            {"answer", answer},
                            {"output", *task.output},
                        }
                    })},
                    {"timestamp", now_iso()},
                }
            );
            return {task, question};
        }
        throw std::runtime_error("task not found for question: " + question.task_id);
    }
    throw std::runtime_error("question not found: " + question_id);
}

StarterTaskRecord TaskQuestionStateStore::stop_task(const std::string& task_id) {
    for (auto& task : tasks_) {
        if (task.id != task_id) {
            continue;
        }
        if (task.status != StarterTaskStatus::Completed && task.status != StarterTaskStatus::Failed) {
            task.status = StarterTaskStatus::Stopped;
            task.updated_at = now_iso();
            persist();
            append_transcript_record(
                transcript_path_for(state_path_),
                nlohmann::json{
                    {"type", "message"},
                    {"role", "system"},
                    {"blocks", nlohmann::json::array({
                        {
                            {"type", "task_state"},
                            {"task_id", task.id},
                            {"status", to_string(task.status)},
                        }
                    })},
                    {"timestamp", now_iso()},
                }
            );
        }
        return task;
    }
    throw std::runtime_error("task not found: " + task_id);
}

} // namespace emberforge::system
