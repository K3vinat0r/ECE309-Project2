#ifndef CORE_MESSAGE_H
#define CORE_MESSAGE_H

#include <string>

enum class Role { System, User, Assistant };

class Message {
public:
    Message(); // Required to initialize empty array slots
    Message(Role role, std::string content);

    Role               role()    const noexcept;
    const std::string& content() const noexcept;
private:
    Role        role_;
    std::string content_;
};

inline Message::Message() : role_(Role::System), content_() {}
inline Message::Message(Role role, std::string content)
    : role_(role), content_(std::move(content)) {}

inline Role Message::role() const noexcept { return role_; }
inline const std::string& Message::content() const noexcept { return content_;}

#endif