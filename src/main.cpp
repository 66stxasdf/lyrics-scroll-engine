// 基于C++的歌词滚动引擎与实现 —— 控制台演示
// 编译： g++ -std=c++17 -I../include main.cpp ../src/lyrics_engine.cpp -o demo
// 运行： ./demo
//
// 演示内容：加载一段示例 LRC，以 300ms 步长模拟播放，
// 逐帧打印"上一句 / 当前句(卡拉OK高亮) / 下一句"与滚动进度。
#include "lyrics_engine.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// 用 '[' ']' 包裹"已唱到的字符"，直观展示卡拉OK逐字高亮效果。
std::string karaoke(const std::string& line, int highlighted) {
    const std::string sung = lyrics::Engine::utf8Substr(line, 0, static_cast<size_t>(highlighted));
    const size_t total = lyrics::Engine::utf8Length(line);
    const std::string rest =
        lyrics::Engine::utf8Substr(line, static_cast<size_t>(highlighted), total);
    return "[" + sung + "]" + rest;
}

}  // namespace

// 读取整个文件为字符串（用于从外部 .lrc 载入）。
std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string sample;
    if (argc > 1) {
        sample = readFile(argv[1]);
        if (sample.empty()) {
            std::printf("无法读取文件: %s\n", argv[1]);
            return 1;
        }
        std::printf("已从文件载入: %s\n", argv[1]);
    } else {
    // 示例歌词（UTF-8，含中文，用于验证中文逐字高亮）。
    sample =
        "[ti:示例歌曲]\n"
        "[ar:WorkBuddy]\n"
        "[offset:0]\n"
        "[00:00.50]夜空中最亮的星\n"
        "[00:04.00]能否听清\n"
        "[00:08.00]那仰望的人\n"
        "[00:12.00]心底的孤独和叹息\n"
        "[00:30.00]\n";  // 间奏（无文本）
    }

    lyrics::Engine engine;
    const int n = engine.loadLrc(sample);
    std::printf("《基于C++的歌词滚动引擎与实现》演示\n");
    std::printf("加载成功，有效歌词行数 = %d\n", n);
    std::printf("估算总时长 = %.1f s\n\n", engine.durationMs() / 1000.0);

    const int64_t duration = engine.durationMs();
    int lastSec = -1;
    for (int64_t pos = 0; pos <= duration; pos += 300) {
        engine.updatePosition(pos);
        const int sec = static_cast<int>(pos / 1000);
        if (sec == lastSec) continue;  // 每秒打印一帧
        lastSec = sec;

        const int idx = engine.currentIndex();
        if (idx < 0) {
            std::printf("t=%02d.%ds  空闲（前奏 / 间奏 / 尾奏）\n", sec, static_cast<int>(pos / 100) % 10);
            continue;
        }

        const std::string cur = engine.currentText();
        const int hi = engine.highlightedChars();
        std::printf("t=%02d.%ds  行%d/%d  进度%.0f%%\n", sec,
                    static_cast<int>(pos / 100) % 10, idx + 1, n,
                    engine.scrollProgress() * 100.0f);
        const std::string prev = engine.prevText();
        const std::string next = engine.nextText();
        if (!prev.empty()) std::printf("  上: %s\n", prev.c_str());
        std::printf("  本: %s\n", karaoke(cur, hi).c_str());
        if (!next.empty()) std::printf("  下: %s\n", next.c_str());
        std::printf("\n");
    }

    // 额外验证：UTF-8 字符计数与子串（中文按"字"而非"字节"）。
    std::printf("--- UTF-8 工具自测 ---\n");
    const std::string cn = "心底的孤独和叹息";  // 8 个汉字
    std::printf("\"%s\" 字符数 = %zu（应为 8）\n", cn.c_str(),
                lyrics::Engine::utf8Length(cn));
    std::printf("前 3 字 = \"%s\"（应为 心底的）\n",
                lyrics::Engine::utf8Substr(cn, 0, 3).c_str());
    return 0;
}
