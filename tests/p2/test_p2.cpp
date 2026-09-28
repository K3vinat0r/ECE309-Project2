// tests/p2/test_p2.cpp
//
// Test suite for Project 2: Message / Conversation / SentinelScanner, plus
// the provided Harness driven by them. Plain assert()-based: the first
// failed assert aborts the program and prints the file, line and expression.
//
// Black-box on purpose: nothing here touches private members. Two things
// the spec asks us to check are private, so they are measured through the
// public interface instead:
//   * capacity_ growth  -> a reallocation is visible as begin() changing.
//   * pending_ size     -> always equals (bytes fed - bytes emitted).

#undef NDEBUG  // keeps assert() turned on
#include <cassert>

#include "core/conversation.h"
#include "core/message.h"
#include "core/sentinel_scanner.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// TEST(Name) just means "void Name()". It matches the spec's sample test.
#define TEST(name) void name()

const std::string SENTINEL = "<|end_conversation|>";

// Temporary files (deleted at the end of main).
const std::string SCRIPT_PATH = "p2_test_script.txt";
const std::string TRANSCRIPT_PATH = "p2_test_transcript.txt";
const std::string TRANSCRIPT_PATH_2 = "p2_test_transcript_2.txt";

// Your words: change these to anything you like. Don't include the sentinel,
// a line that is exactly ---, or an empty line.
const std::string SYSTEM_PROMPT = "Be concise.";
const std::string USER_1 = "hello";
const std::string REPLY_1 = "Hi! What can I do for you today?";
const std::string USER_2 = "nothing, bye";
const std::string REPLY_2 = "Goodbye.";

// ---------------------------------------------------------------------------
// Helpers for the Conversation tests
// ---------------------------------------------------------------------------

// Builds a Conversation with messages "msg0", "msg1", ...
Conversation make_conversation(std::size_t n) {
    Conversation c;
    for (std::size_t i = 0; i < n; i++) {
        c.append(Message(Role::User, "msg" + std::to_string(i)));
    }
    return c;
}

// Checks that c matches make_conversation(n).
void check_conversation(const Conversation& c, std::size_t n) {
    assert(c.size() == n);
    for (std::size_t i = 0; i < n; i++) {
        assert(c.at(i).content() == "msg" + std::to_string(i));
    }
}

// True if at(i) throws out_of_range.
bool at_throws(const Conversation& c, std::size_t i) {
    try {
        c.at(i);
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helper for the SentinelScanner tests
// ---------------------------------------------------------------------------

// Feeds text to a scanner in chunks of chunk_size. Returns the safe text and
// sets found to true if the sentinel was seen.
std::string scan(const std::string& text, std::size_t chunk_size, bool& found) {
    SentinelScanner scanner(SENTINEL);
    std::string safe;
    found = false;
    for (std::size_t i = 0; i < text.size(); i += chunk_size) {
        SentinelScanner::Out out = scanner.feed(text.substr(i, chunk_size));
        safe += out.safe_text;
        if (out.sentinel_found) {
            found = true;
            return safe;
        }
    }
    safe += scanner.flush().safe_text;
    return safe;
}

// ---------------------------------------------------------------------------
// Helpers for the Harness tests
// ---------------------------------------------------------------------------

// Fake user input: gives the Harness a list of lines, then EOF.
class ScriptedInput : public InputSource {
public:
    ScriptedInput() {}
    ScriptedInput(const std::vector<std::string>& lines) : lines_(lines) {}

    std::string read_line() override {
        if (next_ < lines_.size()) {
            eof_ = false;
            return lines_[next_++];
        }
        eof_ = true;
        return "";
    }
    bool is_eof() const override { return eof_; }

    std::size_t consumed() const { return next_; }

private:
    std::vector<std::string> lines_;
    std::size_t next_ = 0;
    bool eof_ = false;
};

// Fake output: saves what the Harness prints.
class CaptureOutput : public OutputSink {
public:
    void write(std::string_view text) override { text_ += text; }
    const std::string& text() const { return text_; }

private:
    std::string text_;
};

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream file(path);
    file << contents;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::string contents;
    std::string line;
    while (std::getline(file, line)) {
        contents += line + "\n";
    }
    return contents;
}

const char* role_name(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
    }
    return "assistant";
}

// Saves a conversation in transcript format (same as main.cpp's --save,
// which the tests can't call).
void write_transcript(const Conversation& conv, const std::string& path) {
    std::ofstream file(path);
    for (std::size_t i = 0; i < conv.size(); i++) {
        if (i > 0) {
            file << "---\n";
        }
        file << "role: " << role_name(conv.at(i).role()) << "\n";
        file << conv.at(i).content() << "\n";
    }
}

// ===========================================================================
// Conversation tests
// ===========================================================================

// Item 1: an empty Conversation is safe.
TEST(EmptyConversationBounds) {
    Conversation c;
    assert(c.size() == 0);
    assert(c.begin() == c.end());
    assert(at_throws(c, 0));

    // With one message, index 0 works and index 1 throws.
    c.append(Message(Role::User, "hi"));
    assert(c.at(0).content() == "hi");
    assert(at_throws(c, 1));
}

// Item 2: the system message stays first after growth.
TEST(SystemMessageStaysFirst) {
    Conversation c;
    c.append(Message(Role::System, SYSTEM_PROMPT));
    for (std::size_t i = 0; i < 20; i++) {
        c.append(Message(Role::User, "message " + std::to_string(i)));
    }
    // 21 messages, so the array grew several times.
    assert(c.size() == 21);
    assert(c.at(0).role() == Role::System);
    assert(c.at(0).content() == SYSTEM_PROMPT);
    for (std::size_t i = 0; i < 20; i++) {
        assert(c.at(i + 1).content() == "message " + std::to_string(i));
    }

    // Range-for goes oldest first, so the system message is first.
    assert(c.begin()->role() == Role::System);
    int count = 0;
    for (const Message& m : c) {
        assert(m.content().size() > 0);
        count++;
    }
    assert(count == 21);

    // A copy keeps the same order.
    Conversation copy(c);
    assert(copy.at(0).role() == Role::System);
    assert(copy.at(20).content() == "message 19");
}

// Item 3: a copy gets its own buffer (deep copy).
TEST(CopyMakesDifferentBuffer) {
    Conversation original = make_conversation(5);

    Conversation copy(original);
    assert(copy.begin() != original.begin());  // different memory
    check_conversation(copy, 5);

    // Changing the copy must not change the original.
    copy.append(Message(Role::User, "only in the copy"));
    assert(copy.size() == 6);
    assert(original.size() == 5);

    // Copy assignment replaces what was there before.
    Conversation assigned;
    assigned.append(Message(Role::User, "old"));
    assigned = original;
    assert(assigned.begin() != original.begin());
    check_conversation(assigned, 5);

    // Assigning to itself must not break it.
    Conversation& same = assigned;
    assigned = same;
    check_conversation(assigned, 5);
}

// Item 4: a move steals the buffer and empties the source.
TEST(MoveStealsBufferAndZerosSource) {
    Conversation source = make_conversation(5);
    const Message* buffer = source.begin();

    Conversation dest(std::move(source));
    assert(dest.begin() == buffer);  // same memory: stolen, not copied
    check_conversation(dest, 5);
    assert(source.size() == 0);
    assert(source.begin() == nullptr);  // source was zeroed

    // The moved-from object must still work.
    source.append(Message(Role::User, "reused"));
    assert(source.size() == 1);

    // Move assignment does the same, and frees the old buffer.
    Conversation target = make_conversation(3);
    Conversation other = make_conversation(6);
    const Message* other_buffer = other.begin();
    target = std::move(other);
    assert(target.begin() == other_buffer);
    check_conversation(target, 6);
    assert(other.size() == 0);
    assert(other.begin() == nullptr);

    // Moving to itself must not break it.
    Conversation& same = target;
    target = std::move(same);
    check_conversation(target, 6);
}

// Item 5: capacity goes 0 -> 1, then doubles (1, 2, 4, 8, ...).
// capacity_ is private, so the test watches begin(): if the address changes,
// the array was reallocated. That should only happen when it is full.
TEST(GrowthFollowsDocumentedFactor) {
    Conversation c;
    const Message* previous = c.begin();
    std::size_t expected_capacity = 0;
    int reallocations = 0;

    for (std::size_t n = 1; n <= 1000; n++) {
        bool expect_realloc = false;
        if (c.size() == expected_capacity) {
            expect_realloc = true;
            if (expected_capacity == 0) {
                expected_capacity = 1;
            } else {
                expected_capacity = expected_capacity * 2;
            }
        }

        c.append(Message(Role::User, "msg" + std::to_string(n - 1)));

        bool reallocated = (c.begin() != previous);
        assert(reallocated == expect_realloc);
        previous = c.begin();
        if (reallocated) {
            reallocations++;
        }
    }

    // Capacities 1, 2, 4, ..., 1024 is 11 reallocations.
    assert(reallocations == 11);
    check_conversation(c, 1000);  // nothing was lost
}

// ===========================================================================
// SentinelScanner tests
// ===========================================================================

// Item 6: text with no sentinel passes through unchanged.
TEST(ScannerCleanText) {
    const std::string text = "Hello there, nothing special here.";
    for (std::size_t chunk = 1; chunk <= text.size(); chunk++) {
        bool found = true;
        std::string safe = scan(text, chunk, found);
        assert(!found);
        assert(safe == text);
    }
}

// Item 7: the sentinel is caught at every possible split (spec section 3.4).
TEST(ScannerCatchesSentinelAtEveryBoundary) {
    const std::string text = "Goodbye." + SENTINEL;

    // Shape 1: the whole sentinel in one chunk.
    SentinelScanner whole(SENTINEL);
    SentinelScanner::Out out = whole.feed(text);
    assert(out.sentinel_found);
    assert(out.safe_text == "Goodbye.");

    // Shape 2: split into two chunks at every possible point.
    for (std::size_t split = 0; split <= text.size(); split++) {
        SentinelScanner scanner(SENTINEL);
        SentinelScanner::Out out1 = scanner.feed(text.substr(0, split));
        SentinelScanner::Out out2 = scanner.feed(text.substr(split));
        assert((out1.sentinel_found || out2.sentinel_found) &&
               "sentinel must be caught regardless of split point");
        assert(out1.safe_text + out2.safe_text == "Goodbye.");
    }

    // Shape 3: one character at a time.
    bool found = false;
    std::string safe = scan(text, 1, found);
    assert(found);
    assert(safe == "Goodbye.");
}

// Item 8: text that only looks like the sentinel must not trigger it.
TEST(ScannerIgnoresFalseAlarms) {
    std::vector<std::string> decoys;
    decoys.push_back("say <|end_world|> now");
    decoys.push_back("missing the closing bracket <|end_conversation| now");
    decoys.push_back("missing the bar <|end_conversation> now");
    decoys.push_back("missing the opening bracket |end_conversation|> now");
    decoys.push_back("the stream ends in a partial match <|end_conv");

    for (std::size_t d = 0; d < decoys.size(); d++) {
        for (std::size_t chunk = 1; chunk <= 5; chunk++) {
            bool found = true;
            std::string safe = scan(decoys[d], chunk, found);
            assert(!found);
            assert(safe == decoys[d]);
        }
    }

    // A partial match right before the real sentinel.
    bool found = false;
    std::string safe = scan("<|end_" + SENTINEL, 1, found);
    assert(found);
    assert(safe == "<|end_");
}

// Item 9: pending_ never grows past sentinel.size() - 1.
// pending_ is private, so measure it: pending_.size() equals
// (characters fed - characters emitted).
// The stream repeats "<|end_conversation|" so it always looks like the start
// of the sentinel, but the last '>' never comes.
TEST(ScannerPendingNeverExceedsBound) {
    const std::size_t bound = SENTINEL.size() - 1;
    const std::string unit = SENTINEL.substr(0, SENTINEL.size() - 1);

    SentinelScanner scanner(SENTINEL);
    std::size_t fed = 0;
    std::size_t emitted = 0;
    std::size_t biggest = 0;

    // 4 MB, one character at a time.
    for (std::size_t i = 0; i < 4 * 1024 * 1024; i++) {
        std::string one_char(1, unit[i % unit.size()]);
        SentinelScanner::Out out = scanner.feed(one_char);
        assert(!out.sentinel_found);
        fed++;
        emitted += out.safe_text.size();

        assert(emitted <= fed);
        std::size_t pending = fed - emitted;
        assert(pending <= bound && "pending_ grew past sentinel.size() - 1");
        if (pending > biggest) {
            biggest = pending;
        }
    }

    emitted += scanner.flush().safe_text.size();
    assert(emitted == fed);      // nothing was lost
    assert(biggest == bound);    // the buffer did fill up
}

// ===========================================================================
// Harness tests (the provided Harness running on my classes)
// ===========================================================================

// Item 10: the loop stops at the turn limit.
TEST(HarnessStopsAtTurnLimit) {
    write_file(SCRIPT_PATH,
               "role: assistant\none\n---\n"
               "role: assistant\ntwo\n---\n"
               "role: assistant\nthree\n---\n"
               "role: assistant\nfour\n");

    HarnessConfig cfg;
    cfg.max_turns = 3;
    Harness harness(std::make_unique<ScriptedModelClient>(SCRIPT_PATH), cfg);
    std::vector<std::string> lines = {"a", "b", "c", "d"};
    ScriptedInput in(lines);
    CaptureOutput out;

    StopReason reason = harness.run(in, out);
    assert(reason.kind == StopReason::Kind::TurnLimit);
    assert(in.consumed() == 3);                 // never asked for a 4th line
    assert(harness.conversation().size() == 6);  // 3 user + 3 assistant
    assert(harness.conversation().at(1).content() == "one");
    assert(harness.conversation().at(5).content() == "three");
}

// Item 11: the loop stops when the scanner finds the sentinel, wherever the
// chunks split. "chunk: N" in the script sends N characters at a time, so
// this tries every N.
TEST(HarnessHaltsWhenSentinelFound) {
    const std::string reply = "Goodbye." + SENTINEL;

    for (std::size_t chunk = 1; chunk <= reply.size(); chunk++) {
        write_file(SCRIPT_PATH,
                   "chunk: " + std::to_string(chunk) + "\n"
                   "role: assistant\n" + reply + "\n---\n"
                   "role: assistant\nnever reached\n");

        HarnessConfig cfg;
        Harness harness(std::make_unique<ScriptedModelClient>(SCRIPT_PATH), cfg);
        std::vector<std::string> lines = {"hi", "unused", "unused"};
        ScriptedInput in(lines);
        CaptureOutput out;

        StopReason reason = harness.run(in, out);
        assert(reason.kind == StopReason::Kind::Sentinel);
        assert(in.consumed() == 1);  // stopped right after this reply

        // The user sees the reply but no piece of the sentinel.
        assert(out.text().find("Goodbye.") != std::string::npos);
        assert(out.text().find('<') == std::string::npos);

        // The stored reply keeps the sentinel so a replay stops too.
        assert(harness.conversation().size() == 2);
        assert(harness.conversation().at(1).content() == reply);
    }
}

// EOF (Ctrl-D) ends the loop cleanly.
TEST(HarnessExitsOnEof) {
    write_file(SCRIPT_PATH, "role: assistant\none\n---\nrole: assistant\ntwo\n");
    HarnessConfig cfg;

    Harness harness(std::make_unique<ScriptedModelClient>(SCRIPT_PATH), cfg);
    std::vector<std::string> lines = {"only line"};  // then EOF
    ScriptedInput in(lines);
    CaptureOutput out;

    StopReason reason = harness.run(in, out);
    assert(reason.kind == StopReason::Kind::UserExit);
    assert(harness.conversation().size() == 2);
}

// Item 12: save a conversation, replay it, and get the same conversation.
TEST(TranscriptRoundTrip) {
    Conversation session;
    session.append(Message(Role::System, SYSTEM_PROMPT));
    session.append(Message(Role::User, USER_1));
    session.append(Message(Role::Assistant, REPLY_1));
    session.append(Message(Role::User, USER_2));
    session.append(Message(Role::Assistant, REPLY_2 + SENTINEL));
    write_transcript(session, TRANSCRIPT_PATH);

    // Replay it through ReplayModelClient and the Harness.
    std::unique_ptr<ReplayModelClient> replay =
        std::make_unique<ReplayModelClient>(TRANSCRIPT_PATH);
    HarnessConfig cfg;
    cfg.system_message = replay->system_message();
    Harness harness(std::move(replay), cfg);
    std::vector<std::string> lines = {USER_1, USER_2};
    ScriptedInput in(lines);
    CaptureOutput out;

    StopReason reason = harness.run(in, out);
    assert(reason.kind == StopReason::Kind::Sentinel);

    // Every message matches the original.
    const Conversation& replayed = harness.conversation();
    assert(replayed.size() == session.size());
    for (std::size_t i = 0; i < session.size(); i++) {
        assert(replayed.at(i).role() == session.at(i).role());
        assert(replayed.at(i).content() == session.at(i).content());
    }

    // The user saw the replies but never the sentinel.
    assert(out.text().find(REPLY_1) != std::string::npos);
    assert(out.text().find(REPLY_2) != std::string::npos);
    assert(out.text().find(SENTINEL) == std::string::npos);

    // Saving the replay gives the identical file.
    write_transcript(replayed, TRANSCRIPT_PATH_2);
    assert(read_file(TRANSCRIPT_PATH_2) == read_file(TRANSCRIPT_PATH));
}

// ---------------------------------------------------------------------------
int main() {
    EmptyConversationBounds();
    std::cout << "PASS EmptyConversationBounds\n";
    SystemMessageStaysFirst();
    std::cout << "PASS SystemMessageStaysFirst\n";
    CopyMakesDifferentBuffer();
    std::cout << "PASS CopyMakesDifferentBuffer\n";
    MoveStealsBufferAndZerosSource();
    std::cout << "PASS MoveStealsBufferAndZerosSource\n";
    GrowthFollowsDocumentedFactor();
    std::cout << "PASS GrowthFollowsDocumentedFactor\n";

    ScannerCleanText();
    std::cout << "PASS ScannerCleanText\n";
    ScannerCatchesSentinelAtEveryBoundary();
    std::cout << "PASS ScannerCatchesSentinelAtEveryBoundary\n";
    ScannerIgnoresFalseAlarms();
    std::cout << "PASS ScannerIgnoresFalseAlarms\n";
    ScannerPendingNeverExceedsBound();
    std::cout << "PASS ScannerPendingNeverExceedsBound\n";

    HarnessStopsAtTurnLimit();
    std::cout << "PASS HarnessStopsAtTurnLimit\n";
    HarnessHaltsWhenSentinelFound();
    std::cout << "PASS HarnessHaltsWhenSentinelFound\n";
    HarnessExitsOnEof();
    std::cout << "PASS HarnessExitsOnEof\n";
    TranscriptRoundTrip();
    std::cout << "PASS TranscriptRoundTrip\n";

    // Delete the temporary files.
    std::remove(SCRIPT_PATH.c_str());
    std::remove(TRANSCRIPT_PATH.c_str());
    std::remove(TRANSCRIPT_PATH_2.c_str());

    std::cout << "\nAll tests passed.\n";
    return 0;
}