// test_engine.cpp —— 歌词引擎独立单元测试（不依赖 Android，可纯 C++ 编译运行）
//
// 编译运行：
//   g++ -std=c++17 -I../include -o test_engine \
//       test_engine.cpp ../src/lyrics_engine.cpp
//   ./test_engine
//
// 也可用等价脚本 tools 中的 Python 版本交叉验证。
#include "lyrics_engine.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  [FAIL] %s (line %d)\n", #cond, __LINE__);         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void testLrcParsing() {
    std::printf("test: LRC 解析\n");
    const std::string lrc =
        "[ti:测试歌曲]\n"
        "[ar:测试歌手]\n"
        "[al:测试专辑]\n"
        "[offset:+500]\n"
        "[00:00.00]第一句歌词\n"
        "[00:05.50]第二句歌词\n"
        "[00:12.00][01:20.00]重复出现的句子\n"
        "[01:30]没有小数的标签\n"
        "[02:00.000]毫秒精度\n"
        "\n"
        "[02:30.00]\n";  // 间奏行（无文本）

    lyrics::Engine e;
    const int n = e.loadLrc(lrc);
    CHECK(n == 7);  // 6 行有效歌词（重复行算 2 行）+ 1 行间奏
    CHECK(e.offsetMs() == 500);

    e.updatePosition(0);
    CHECK(e.currentIndex() == 0);
    CHECK(e.currentText() == "第一句歌词");

    e.updatePosition(4000);
    CHECK(e.currentIndex() == 0);

    e.updatePosition(5000);  // +500ms 偏移后正好等于第二句时间
    CHECK(e.currentIndex() == 1);
    CHECK(e.currentText() == "第二句歌词");
    CHECK(e.scrollProgress() < 0.01f);

    e.updatePosition(6500);
    CHECK(e.currentIndex() == 1);
    CHECK(e.currentText() == "第二句歌词");
    CHECK(e.nextText() == "重复出现的句子");
    const float p = e.scrollProgress();
    CHECK(p > 0.1f && p < 0.3f);

    e.updatePosition(90000);  // 01:30.00 + 500ms 偏移
    CHECK(e.currentIndex() == 4);

    e.updatePosition(200000);
    CHECK(e.currentIndex() == 6);  // 最后一行（间奏占位符）
    CHECK(e.currentText() == "\xE2\x80\xA6");

    // 第一句之前的负进度：处于"空闲"，current_ == -1，nextText() 有意返回空串
    // （避免把第 1 句误当"下一句"显示在底部）。
    e.setOffsetMs(-600);
    e.updatePosition(0);
    CHECK(e.currentIndex() == -1);
    CHECK(e.nextText().empty());
}

static void testSortAndProgress() {
    std::printf("test: 乱序时间轴与滚动进度\n");
    const std::string lrc =
        "[00:10.00]第十秒\n"
        "[00:01.00]第一秒\n"
        "[00:05.00]第五秒\n";
    lyrics::Engine e;
    CHECK(e.loadLrc(lrc) == 3);

    e.updatePosition(0);
    CHECK(e.currentIndex() == -1);
    e.updatePosition(1000);
    CHECK(e.currentIndex() == 0);
    CHECK(e.currentText() == "第一秒");
    e.updatePosition(3000);
    CHECK(e.currentIndex() == 0);
    CHECK(e.scrollProgress() > 0.4f && e.scrollProgress() < 0.6f);
    e.updatePosition(5000);
    CHECK(e.currentIndex() == 1);
    CHECK(e.currentText() == "第五秒");
    e.updatePosition(10000);
    CHECK(e.currentIndex() == 2);
    CHECK(e.scrollProgress() == 0.0f);  // 最后一行
}

static void testPlainLines() {
    std::printf("test: 纯文本行模式\n");
    std::vector<std::string> lines;
    lines.push_back("  第一行  ");
    lines.push_back("");
    lines.push_back("第二行");
    lines.push_back("第三行");
    lyrics::Engine e;
    CHECK(e.loadPlainLines(lines, 6000) == 3);  // 总时长 6s，3 行 -> 每行 2s
    e.updatePosition(0);
    CHECK(e.currentIndex() == 0);
    CHECK(e.currentText() == "第一行");
    e.updatePosition(2500);
    CHECK(e.currentIndex() == 1);
    CHECK(e.currentText() == "第二行");
    e.updatePosition(5500);
    CHECK(e.currentIndex() == 2);
    CHECK(e.currentText() == "第三行");
}

static void testUtf8AndKaraoke() {
    std::printf("test: UTF-8 与卡拉OK逐字高亮\n");
    CHECK(lyrics::Engine::utf8Length("心底的孤独和叹息") == 8);
    CHECK(lyrics::Engine::utf8Substr("心底的孤独和叹息", 0, 3) == "心底的");
    CHECK(lyrics::Engine::utf8Substr("abc", 1, 100) == "bc");  // 超出取末尾

    // 中文歌词：progress=0.5 时高亮约一半字符
    const std::string lrc =
        "[00:00.00]心底的孤独和叹息\n"
        "[00:04.00]能否听清\n";
    lyrics::Engine e;
    CHECK(e.loadLrc(lrc) == 2);
    e.updatePosition(2000);  // 0~4s 中点 -> progress 0.5
    CHECK(e.currentIndex() == 0);
    const float pr = e.scrollProgress();
    CHECK(pr > 0.4f && pr < 0.6f);
    const int hi = e.highlightedChars();
    CHECK(hi == 4);  // 8 字的一半
    CHECK(lyrics::Engine::utf8Substr(e.currentText(), 0, static_cast<size_t>(hi)) == "心底的孤");
}

static void testApiAndBoundary() {
    std::printf("test: 新增 API 与边界\n");
    const std::string lrc =
        "[00:00.00]第一句\n"
        "[00:05.00]第二句\n"
        "[00:10.00]第三句\n";
    lyrics::Engine e;
    CHECK(e.loadLrc(lrc) == 3);

    e.updatePosition(7000);
    CHECK(e.currentIndex() == 1);
    CHECK(e.lastLineTimeMs() == 10000);
    CHECK(e.durationMs() == 10000 + 6000);  // 末行默认跨度

    const auto all = e.lines();
    CHECK(all.size() == 3);
    CHECK(all[0].text == "第一句");
    CHECK(all[2].timeMs == 10000);
    CHECK(e.lineText(2) == "第三句");
    CHECK(e.lineTimeMs(0) == 0);

    // 边界：空歌词、无时间轴、超长尾句
    lyrics::Engine e2;
    CHECK(e2.loadLrc("") == 0);
    CHECK(e2.isEmpty());
    CHECK(e2.currentIndex() == -1);
    CHECK(e2.durationMs() == 0);

    CHECK(e2.loadLrc("没有时间轴的纯文本\n第二行") == 0);  // 无时间标签 -> 0 行
    CHECK(e2.loadLrc("[00:00.00]唯一一句") == 1);
    e2.updatePosition(999999);  // 远超末行默认跨度，进度夹取到 1.0（已唱完）
    CHECK(e2.currentIndex() == 0);
    CHECK(e2.scrollProgress() == 1.0f);

    // 压力测试：5000 行
    std::string big;
    for (int i = 0; i < 5000; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%02d:%02d.00]第%d句\n",
                      i / 60, i % 60, i);
        big += buf;
    }
    CHECK(e2.loadLrc(big) == 5000);
    e2.updatePosition(41 * 60 * 1000 + 30 * 1000);  // 41:30.00
    CHECK(e2.currentIndex() == 2490);
}

int main() {
    testLrcParsing();
    testSortAndProgress();
    testPlainLines();
    testUtf8AndKaraoke();
    testApiAndBoundary();
    if (g_failures == 0) {
        std::printf("\n全部测试通过 \u2713\n");
        return 0;
    }
    std::printf("\n%d 个断言失败 \u2717\n", g_failures);
    return 1;
}
