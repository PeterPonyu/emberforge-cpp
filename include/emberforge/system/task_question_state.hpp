#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace emberforge::system {

enum class StarterTaskStatus {
    Pending,
    InProgress,
    WaitingForUser,
    Completed,
    Failed,
    Stopped,
};

enum class StarterQuestionStatus {
    Pending,
    Answered,
};

[[nodiscard]] std::string to_string(StarterTaskStatus status);
[[nodiscard]] std::string to_string(StarterQuestionStatus status);

struct StarterTaskRecord {
    std::string id;
    std::string kind;
    std::string input;
    StarterTaskStatus status{StarterTaskStatus::Pending};
    std::optional<std::string> question_id;
    std::optional<std::string> answer;
    std::optional<std::string> output;
    std::string created_at;
    std::string updated_at;
};

struct StarterQuestionRecord {
    std::string id;
    std::string task_id;
    std::string text;
    StarterQuestionStatus status{StarterQuestionStatus::Pending};
    std::optional<std::string> answer;
    std::string created_at;
    std::optional<std::string> answered_at;
};

class TaskQuestionStateStore {
public:
    explicit TaskQuestionStateStore(std::filesystem::path state_path = {});

    [[nodiscard]] StarterTaskRecord create_prompt_task(const std::string& input);
    [[nodiscard]] std::vector<StarterTaskRecord> list_tasks() const;
    [[nodiscard]] std::optional<StarterTaskRecord> get_task(const std::string& task_id) const;
    [[nodiscard]] std::pair<StarterTaskRecord, StarterQuestionRecord> ask_question(const std::string& task_id,
                                                                                   const std::string& text);
    [[nodiscard]] std::vector<StarterQuestionRecord> list_questions(std::optional<StarterQuestionStatus> status) const;
    [[nodiscard]] std::pair<StarterTaskRecord, StarterQuestionRecord> answer_question(const std::string& question_id,
                                                                                      const std::string& answer);
    [[nodiscard]] StarterTaskRecord stop_task(const std::string& task_id);

private:
    void load();
    void persist() const;

    std::filesystem::path state_path_;
    std::size_t next_task_index_{1};
    std::size_t next_question_index_{1};
    std::vector<StarterTaskRecord> tasks_;
    std::vector<StarterQuestionRecord> questions_;
};

} // namespace emberforge::system
