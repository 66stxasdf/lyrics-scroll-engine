#ifndef LYRICS_ENGINE_H
#define LYRICS_ENGINE_H

// 基于C++的歌词滚动引擎与实现
// ---------------------------------------------------------------
// 纯 C++17 实现，不依赖任何平台 API（无 Android / 无第三方库），
// 可脱离移动端单独在 PC 上编译、运行与单元测试。
//
// 设计目标：
//   1. 正确解析 LRC（多时间标签、乱序、offset、元信息、BOM、\r\n）；
//   2. 以 O(log n) 二分查找定位"当前唱到哪一句"；
//   3. 以 O(1) 计算"当前句 -> 下一句"的连续滚动进度（0..1）；
//   4. 对中文歌词做 UTF-8 安全的"逐字卡拉OK高亮"；
//   5. 处理好"最后一句没有下一句"与"歌词文件大段空白"两类边界。

#include <cstdint>
#include <string>
#include <vector>

namespace lyrics {

// 一行歌词（按时间排序后的逻辑单元）。
struct LyricLine {
    int64_t timeMs = 0;  // 该行起始时间（毫秒，未含 offset 修正）
    std::string text;    // 歌词文本（UTF-8 编码）
};

// 歌词解析 / 同步 / 滚动进度引擎。
class Engine {
public:
    Engine() = default;

    // 解析 LRC 文本，返回解析出的有效行数（0 = 失败 / 无时间轴）。
    // 支持：
    //   - 多时间标签  [00:01.00][00:31.00]歌词
    //   - 时间格式    [mm:ss.xx] / [mm:ss.xxx] / [mm:ss:xx] / [m:ss]
    //   - 全局偏移    [offset:+500] / [offset:-300]
    //   - 元信息行    [ti:] [ar:] [al:] [length:] 等自动跳过
    //   - 自动忽略 UTF-8 BOM 与 Windows \r\n
    int loadLrc(const std::string& lrc);

    // 纯文本行（无时间轴）模式：按歌曲总时长均分每行时间。
    // lines 为按行给出的文本；durationMs 为歌曲总时长（毫秒）。
    int loadPlainLines(const std::vector<std::string>& lines, int64_t durationMs);

    void clear() noexcept;
    void reset() noexcept { clear(); }

    // 用户手动微调：正数 = 歌词提前出现，负数 = 歌词延后。
    void setOffsetMs(int64_t offsetMs) noexcept;
    int64_t offsetMs() const noexcept { return offsetMs_; }

    // 每帧（如 30fps）调用一次，传入当前播放进度（毫秒），内部叠加 offset。
    void updatePosition(int64_t positionMs) noexcept;

    int lineCount() const noexcept { return static_cast<int>(lines_.size()); }
    bool isEmpty() const noexcept { return lines_.empty(); }

    // -1 = 还没唱到第一句 / 落进歌词空白段
    int currentIndex() const noexcept { return current_; }
    // 当前句 -> 下一句 的连续滚动进度，范围 [0,1]
    float scrollProgress() const noexcept { return progress_; }

    // 卡拉OK逐字高亮：根据滚动进度与当前句字符数，返回"已唱到"的字符数（UTF-8 安全）。
    int highlightedChars() const;

    // 当前行起始时间（未修正 offset）；无当前行时返回 -1
    int64_t currentLineTimeMs() const;
    // 最后一行起始时间；无歌词时返回 -1
    int64_t lastLineTimeMs() const;
    // 估算总时长 = 最后一行时间 + 末行默认跨度
    int64_t durationMs() const noexcept;

    std::string lineText(int index) const;
    int64_t lineTimeMs(int index) const;
    std::string currentText() const;
    std::string prevText() const;
    std::string nextText() const;

    // 整份歌词（已按时间排序），便于整页 / 列表渲染。
    std::vector<LyricLine> lines() const;

    // ---- UTF-8 工具（公开，便于调用方做"按字符"渲染）----
    static size_t utf8Length(const std::string& s) noexcept;
    // 返回从第 charPos 个字符起、长度为 charLen 个字符的子串；
    // charLen 超过剩余字符时取到末尾。
    static std::string utf8Substr(const std::string& s, size_t charPos, size_t charLen);

private:
    void rebuildIndex();

    std::vector<LyricLine> lines_;
    std::vector<int> order_;  // 行号按时间排序后的索引表

    int64_t offsetMs_   = 0;
    int64_t positionMs_ = 0;
    int     current_    = -1;
    float   progress_   = 0.0f;
};

}  // namespace lyrics

#endif  // LYRICS_ENGINE_H
