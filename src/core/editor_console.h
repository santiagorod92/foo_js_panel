#pragma once
#include <functional>
#include <string>
#include <vector>

namespace jsp {

struct ConsoleReply {
    std::string text;
    bool clear = false;
    std::string echo;
};

class EditorConsole {
public:
    using Eval = std::function<std::string(const std::string&)>;
    ConsoleReply submit(const std::string& input, const Eval& eval);
    std::string watches(const Eval& eval) const;
    bool watching() const { return !m_watches.empty(); }

private:
    std::vector<std::string> m_watches;
};

}
