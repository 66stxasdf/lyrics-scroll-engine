// 基于C++的歌词滚动引擎与实现 —— 核心实现
#include "lyrics_engine.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace lyrics {
namespace {

// 最后一句的"默认持续时长"。最后一行没有下一句可推算 span，用它兜底，
// 让超长的最后一句也能在 kLastLineSpanMs 内完整滚过屏幕。
constexpr int64_t kLastLineSpanMs = 6000;

// 一句歌词最多"占住屏幕"多久（15 秒）。正常句间隔 6~8 秒；超过它判定"本句已唱完"，
// 画面转为空闲，避免歌词文件出现大段空白（间奏/缺失）时画面原地卡住。
constexpr int64_t kMaxLineHoldMs = 15000;

// 间奏占位符（UTF-8 编码的省略号，'…'）。
const char kInterludeUtf8[] = "\xE2\x80\xA6";

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 创作者信息行："作词 : xxx" / "作曲：xxx" 等不是歌词，若当作大字显示会错位，直接丢弃。
bool isCreditLine(const std::string& text) {
    static const char* kKeys[] = {
        "作词", "作曲", "编曲", "制作人", "混音", "母带", "吉他", "贝斯", "鼓",
        "和声", "录音", "监制", "出品", "统筹", "宣发", "文案", "封面", "题字",
        "原唱", "翻唱", "演唱", "词曲", "曲词", "配唱", "制作", "弦乐", "钢琴",
        "二胡", "古筝", "琵琶", "唢呐", "笛", "箫", "萧", "OP", "SP", "词", "曲"
    };
    if (text.empty()) return false;
    for (const char* k : kKeys) {
        const size_t n = std::strlen(k);
        if (text.size() <= n) continue;
        if (text.compare(0, n, k) != 0) continue;
        size_t p = n;
        while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
        if (p >= text.size()) continue;
        const unsigned char c0 = static_cast<unsigned char>(text[p]);
        if (c0 == ':') return true;  // 半角冒号 ':'
        // 全角冒号 "：" = U+FF1A，UTF-8 = EF BC 9A
        if (c0 == 0xEF && p + 2 < text.size() &&
            static_cast<unsigned char>(text[p + 1]) == 0xBC &&
            static_cast<unsigned char>(text[p + 2]) == 0x9A) {
            return true;
        }
    }
    return false;
}

// 解析行首的一个时间标签 [mm:ss.xx] / [mm:ss.xxx] / [mm:ss:xx]。
// 成功：pos 移动到 ']' 之后，ms 为毫秒；失败：pos 不变，返回 false。
bool parseTimeTag(const std::string& s, size_t& pos, int64_t& ms) {
    if (pos >= s.size() || s[pos] != '[') return false;
    size_t i = pos + 1;
    int64_t minutes = 0, seconds = 0, fraction = 0;
    int fracDigits = 0;
    bool got = false;

    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        minutes = minutes * 10 + (s[i] - '0');
        ++i;
        got = true;
    }
    if (!got) return false;

    if (i < s.size() && s[i] == ':') {
        ++i;
        got = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            seconds = seconds * 10 + (s[i] - '0');
            ++i;
            got = true;
        }
        if (!got) return false;
        if (i < s.size() && (s[i] == '.' || s[i] == ':')) {
            ++i;
            got = false;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                fraction = fraction * 10 + (s[i] - '0');
                ++fracDigits;
                ++i;
                got = true;
            }
        }
    }
    if (i >= s.size() || s[i] != ']') return false;

    int64_t fracMs = 0;
    if (fracDigits == 2)      fracMs = fraction * 10;   // 百分秒
    else if (fracDigits == 3) fracMs = fraction;        // 毫秒
    else if (fracDigits == 1) fracMs = fraction * 100;  // 十分之一秒

    ms = minutes * 60000 + seconds * 1000 + fracMs;
    pos = i + 1;
    return true;
}

}  // namespace

size_t Engine::utf8Length(const std::string& s) noexcept {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;  // 非 UTF-8 续字节即一个字符起点
    return n;
}

std::string Engine::utf8Substr(const std::string& s, size_t charPos, size_t charLen) {
    size_t i = 0, chars = 0;
    size_t start = std::string::npos;
    while (i < s.size()) {
        if (chars == charPos) start = i;
        if (start != std::string::npos && chars == charPos + charLen)
            return s.substr(start, i - start);
        unsigned char c = static_cast<unsigned char>(s[i]);
        const int w = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        i += w;
        ++chars;
    }
    if (start == std::string::npos) return "";
    return s.substr(start);  // charLen 超出剩余字符时取到末尾
}

int Engine::loadLrc(const std::string& lrc) {
    lines_.clear();
    order_.clear();
    offsetMs_ = 0;
    current_ = -1;
    progress_ = 0.0f;

    // 跳过 UTF-8 BOM（EF BB BF）
    size_t start = 0;
    if (lrc.size() >= 3 &&
        static_cast<unsigned char>(lrc[0]) == 0xEF &&
        static_cast<unsigned char>(lrc[1]) == 0xBB &&
        static_cast<unsigned char>(lrc[2]) == 0xBF) {
        start = 3;
    }
    const std::string body = lrc.substr(start);

    std::string raw;
    size_t i = 0;
    const size_t len = body.size();
    // 手工按行切分，天然兼容 \n 与 \r\n，且无需引入 <sstream>。
    while (i <= len) {
        size_t nl = body.find('\n', i);
        if (nl == std::string::npos) nl = len;
        raw = body.substr(i, nl - i);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        i = nl + 1;

        if (trim(raw).empty()) continue;

        // 一行可有多个时间标签：[00:01.00][00:31.00]歌词
        std::vector<int64_t> times;
        size_t pos = 0;
        while (true) {
            const size_t save = pos;
            int64_t ms = 0;
            if (parseTimeTag(raw, pos, ms)) {
                times.push_back(ms);
            } else {
                pos = save;
                break;
            }
        }
        std::string text = trim(raw.substr(pos));

        if (times.empty()) {
            // 元信息行，如 [offset:+500] / [ti:歌名] / [length:03:24]
            const std::string lower = toLower(text);
            if (lower.rfind("[offset:", 0) == 0) {
                offsetMs_ = std::atoll(text.c_str() + 8);
            }
            continue;
        }
        if (isCreditLine(text)) continue;  // 创作者信息行：不当歌词，也不占行号
        if (text.empty()) text = kInterludeUtf8;  // 间奏占位符 "…"
        for (const int64_t t : times) {
            LyricLine line;
            line.timeMs = t;
            line.text = text;
            lines_.push_back(std::move(line));
        }
    }

    rebuildIndex();
    return static_cast<int>(lines_.size());
}

int Engine::loadPlainLines(const std::vector<std::string>& lines, int64_t durationMs) {
    lines_.clear();
    order_.clear();
    offsetMs_ = 0;
    current_ = -1;
    progress_ = 0.0f;

    std::vector<std::string> cleaned;
    cleaned.reserve(lines.size());
    for (const std::string& l : lines) {
        std::string t = trim(l);
        if (t.empty()) continue;
        if (isCreditLine(t)) continue;
        cleaned.push_back(std::move(t));
    }
    if (cleaned.empty()) return 0;

    int64_t per = durationMs > 0
                      ? durationMs / static_cast<int64_t>(cleaned.size())
                      : 4000;
    if (per < 500) per = 500;  // 每行至少 0.5s，避免纯文本模式滚得太快

    lines_.reserve(cleaned.size());
    for (size_t k = 0; k < cleaned.size(); ++k) {
        LyricLine line;
        line.timeMs = static_cast<int64_t>(k) * per;
        line.text = cleaned[k];
        lines_.push_back(std::move(line));
    }
    rebuildIndex();
    return static_cast<int>(lines_.size());
}

void Engine::clear() noexcept {
    lines_.clear();
    order_.clear();
    offsetMs_ = 0;
    positionMs_ = 0;
    current_ = -1;
    progress_ = 0.0f;
}

void Engine::setOffsetMs(int64_t offsetMs) noexcept { offsetMs_ = offsetMs; }

void Engine::rebuildIndex() {
    order_.resize(lines_.size());
    for (size_t k = 0; k < lines_.size(); ++k) order_[k] = static_cast<int>(k);
    std::stable_sort(order_.begin(), order_.end(),
                     [this](int a, int b) { return lines_[a].timeMs < lines_[b].timeMs; });
}

void Engine::updatePosition(int64_t positionMs) noexcept {
    positionMs_ = positionMs + offsetMs_;
    if (order_.empty()) {
        current_ = -1;
        progress_ = 0.0f;
        return;
    }

    // 二分查找：最后一个 timeMs <= positionMs_ 的行
    int lo = 0, hi = static_cast<int>(order_.size()) - 1, ans = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (lines_[order_[mid]].timeMs <= positionMs_) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    current_ = ans;
    if (current_ < 0) {
        progress_ = 0.0f;
        return;
    }

    const int64_t curT = lines_[order_[current_]].timeMs;
    int64_t nextT;
    if (current_ + 1 < static_cast<int>(order_.size())) {
        nextT = lines_[order_[current_ + 1]].timeMs;
    } else {
        // 最后一行没有"下一句"参照：给一个默认跨度，让整句在 kLastLineSpanMs 内完整滚过。
        nextT = curT + kLastLineSpanMs;
    }
    const int64_t span = nextT - curT;

    // 歌词文件可能存在大段空白（间奏/缺失）。若空洞超 kMaxLineHoldMs 且已超过本句时长，
    // 判定"本句已结束、下一句还没到"，结束本句（画面转为空闲），不再无限期卡住。
    if (span > kMaxLineHoldMs && (positionMs_ - curT) > kMaxLineHoldMs) {
        current_ = -1;
        progress_ = 0.0f;
        return;
    }

    // 滚动用"有效时长"：空洞段落按 kMaxLineHoldMs 滚完，避免一句字用几十秒才滚到底。
    const int64_t scrollSpan = (span > kMaxLineHoldMs) ? kMaxLineHoldMs : span;
    progress_ = scrollSpan > 0
                    ? static_cast<float>(positionMs_ - curT) / static_cast<float>(scrollSpan)
                    : 0.0f;
    if (progress_ < 0.0f) progress_ = 0.0f;
    if (progress_ > 1.0f) progress_ = 1.0f;
}

int Engine::highlightedChars() const {
    if (current_ < 0) return 0;
    const std::string t = currentText();
    const size_t total = utf8Length(t);
    if (total == 0) return 0;
    int h = static_cast<int>(progress_ * static_cast<float>(total) + 0.5f);
    if (h < 0) h = 0;
    if (h > static_cast<int>(total)) h = static_cast<int>(total);
    return h;
}

int64_t Engine::currentLineTimeMs() const {
    if (current_ < 0 || current_ >= static_cast<int>(order_.size())) return -1;
    return lines_[order_[current_]].timeMs;
}

int64_t Engine::lastLineTimeMs() const {
    if (order_.empty()) return -1;
    return lines_[order_.back()].timeMs;
}

int64_t Engine::durationMs() const noexcept {
    const int64_t t = lastLineTimeMs();
    return t < 0 ? 0 : t + kLastLineSpanMs;
}

std::string Engine::lineText(int index) const {
    if (index < 0 || index >= static_cast<int>(order_.size())) return std::string();
    return lines_[order_[index]].text;
}

int64_t Engine::lineTimeMs(int index) const {
    if (index < 0 || index >= static_cast<int>(order_.size())) return -1;
    return lines_[order_[index]].timeMs;
}

std::string Engine::currentText() const { return lineText(current_); }
std::string Engine::prevText() const { return lineText(current_ - 1); }
std::string Engine::nextText() const {
    // current_ == -1 时（还没唱到第一句 / 落进空白段）没有"下一句"，否则会把第 1 句误显示。
    if (current_ < 0) return std::string();
    return lineText(current_ + 1);
}

std::vector<LyricLine> Engine::lines() const {
    std::vector<LyricLine> out;
    out.reserve(order_.size());
    for (const int idx : order_) out.push_back(lines_[idx]);
    return out;
}

}  // namespace lyrics
