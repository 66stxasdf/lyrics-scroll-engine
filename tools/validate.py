#!/usr/bin/env python3
# validate.py —— 用 Python 忠实移植歌词引擎核心逻辑，交叉验证算法正确性。
# 目的：在本机暂无 C++ 编译器时，确认《基于C++的歌词滚动引擎与实现》的
#       解析 / 同步 / 滚动进度 / UTF-8 高亮 / 边界 行为正确。
# 对应 C++ 源码： src/lyrics_engine.cpp 与 tests/test_engine.cpp。
import sys
import re

K_LAST_LINE_SPAN_MS = 6000
K_MAX_LINE_HOLD_MS = 15000
INTERLUDE = "…"
CREDIT_KEYS = ["作词","作曲","编曲","制作人","混音","母带","吉他","贝斯","鼓",
    "和声","录音","监制","出品","统筹","宣发","文案","封面","题字",
    "原唱","翻唱","演唱","词曲","曲词","配唱","制作","弦乐","钢琴",
    "二胡","古筝","琵琶","唢呐","笛","箫","萧","OP","SP","词","曲"]

def _trim(s):
    return s.strip()

def _is_credit(text):
    if not text:
        return False
    for k in CREDIT_KEYS:
        n = len(k.encode("utf-8"))
        if len(text) <= n:
            continue
        if not text.startswith(k):
            continue
        p = n
        while p < len(text) and text[p] in " \t":
            p += 1
        if p >= len(text):
            continue
        c0 = text[p]
        if c0 == ":":
            return True
        if text[p:].startswith("："):
            return True
    return False

def _parse_time_tag(s, pos):
    # 返回 (ok, new_pos, ms)
    if pos >= len(s) or s[pos] != '[':
        return (False, pos, 0)
    i = pos + 1
    minutes = 0; seconds = 0; fraction = 0; frac_digits = 0; got = False
    while i < len(s) and s[i].isdigit():
        minutes = minutes * 10 + int(s[i]); i += 1; got = True
    if not got:
        return (False, pos, 0)
    if i < len(s) and s[i] == ':':
        i += 1; got = False
        while i < len(s) and s[i].isdigit():
            seconds = seconds * 10 + int(s[i]); i += 1; got = True
        if not got:
            return (False, pos, 0)
        if i < len(s) and s[i] in ('.', ':'):
            i += 1; got = False
            while i < len(s) and s[i].isdigit():
                fraction = fraction * 10 + int(s[i]); i += 1; frac_digits += 1; got = True
    if i >= len(s) or s[i] != ']':
        return (False, pos, 0)
    if frac_digits == 2: frac_ms = fraction * 10
    elif frac_digits == 3: frac_ms = fraction
    elif frac_digits == 1: frac_ms = fraction * 100
    else: frac_ms = 0
    ms = minutes * 60000 + seconds * 1000 + frac_ms
    return (True, i + 1, ms)

def utf8_length(s):
    return sum(1 for c in s.encode("utf-8") if (c & 0xC0) != 0x80)

def utf8_substr(s, char_pos, char_len):
    chars = list(s)  # Python str 已是 Unicode 字符序列
    if char_pos >= len(chars):
        return ""
    return "".join(chars[char_pos:char_pos + char_len])


class Engine:
    def __init__(self):
        self.lines_ = []   # list of (timeMs, text)
        self.order_ = []
        self.offset_ms_ = 0
        self.position_ms_ = 0
        self.current_ = -1
        self.progress_ = 0.0

    def _rebuild(self):
        self.order_ = sorted(range(len(self.lines_)),
                            key=lambda i: self.lines_[i][0])

    def load_lrc(self, lrc):
        self.lines_ = []; self.order_ = []
        self.offset_ms_ = 0; self.current_ = -1; self.progress_ = 0.0
        start = 0
        b = lrc.encode("utf-8")
        if len(b) >= 3 and b[0] == 0xEF and b[1] == 0xBB and b[2] == 0xBF:
            start = 3
        body = lrc[start:]
        for raw in body.split("\n"):
            if raw.endswith("\r"):
                raw = raw[:-1]
            if not _trim(raw):
                continue
            times = []; pos = 0
            while True:
                save = pos
                ok, pos, ms = _parse_time_tag(raw, pos)
                if ok:
                    times.append(ms)
                else:
                    pos = save
                    break
            text = _trim(raw[pos:])
            if not times:
                if text.lower().startswith("[offset:"):
                    # 与 C++ 的 atoll 一致：从 '+500]' 中 tolerant 地解析出整数，忽略结尾 ']'
                    m = re.search(r'[+-]?\d+', text[8:])
                    self.offset_ms_ = int(m.group()) if m else 0
                continue
            if _is_credit(text):
                continue
            if not text:
                text = INTERLUDE
            for t in times:
                self.lines_.append((t, text))
        self._rebuild()
        return len(self.lines_)

    def load_plain(self, lines, duration_ms):
        self.lines_ = []; self.order_ = []
        self.offset_ms_ = 0; self.current_ = -1; self.progress_ = 0.0
        cleaned = [l.strip() for l in lines if l.strip() and not _is_credit(l.strip())]
        if not cleaned:
            return 0
        per = duration_ms // len(cleaned) if duration_ms > 0 else 4000
        if per < 500:
            per = 500
        self.lines_ = [(i * per, cleaned[i]) for i in range(len(cleaned))]
        self._rebuild()
        return len(self.lines_)

    def set_offset(self, ms): self.offset_ms_ = ms

    def update(self, pos):
        self.position_ms_ = pos + self.offset_ms_
        if not self.order_:
            self.current_ = -1; self.progress_ = 0.0; return
        ans = -1; lo, hi = 0, len(self.order_) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            if self.lines_[self.order_[mid]][0] <= self.position_ms_:
                ans = mid; lo = mid + 1
            else:
                hi = mid - 1
        self.current_ = ans
        if self.current_ < 0:
            self.progress_ = 0.0; return
        cur_t = self.lines_[self.order_[self.current_]][0]
        if self.current_ + 1 < len(self.order_):
            next_t = self.lines_[self.order_[self.current_ + 1]][0]
        else:
            next_t = cur_t + K_LAST_LINE_SPAN_MS
        span = next_t - cur_t
        if span > K_MAX_LINE_HOLD_MS and (self.position_ms_ - cur_t) > K_MAX_LINE_HOLD_MS:
            self.current_ = -1; self.progress_ = 0.0; return
        scroll_span = K_MAX_LINE_HOLD_MS if span > K_MAX_LINE_HOLD_MS else span
        self.progress_ = (self.position_ms_ - cur_t) / scroll_span if scroll_span > 0 else 0.0
        self.progress_ = max(0.0, min(1.0, self.progress_))

    def highlighted(self):
        if self.current_ < 0:
            return 0
        t = self.current_text()
        total = utf8_length(t)
        if total == 0:
            return 0
        h = int(self.progress_ * total + 0.5)
        return max(0, min(total, h))

    def current_text(self):
        if self.current_ < 0 or self.current_ >= len(self.order_):
            return ""
        return self.lines_[self.order_[self.current_]][1]
    def prev_text(self):
        if self.current_ < 0: return ""
        return self.lines_[self.order_[self.current_-1]][1] if self.current_-1 >= 0 else ""
    def next_text(self):
        if self.current_ < 0: return ""
        nxt = self.current_ + 1
        return self.lines_[self.order_[nxt]][1] if nxt < len(self.order_) else ""

    def last_line_time(self):
        return self.lines_[self.order_[-1]][0] if self.order_ else -1
    def duration(self):
        t = self.last_line_time()
        return 0 if t < 0 else t + K_LAST_LINE_SPAN_MS
    def lines(self):
        return [self.lines_[i] for i in self.order_]


# ---- 断言测试（与 test_engine.cpp 对应）----
fails = 0
def check(cond, msg=""):
    global fails
    if not cond:
        print("  [FAIL]", msg); fails += 1

def t_parse():
    print("test: LRC 解析")
    lrc = ("[ti:测试歌曲]\n[ar:测试歌手]\n[al:测试专辑]\n[offset:+500]\n"
           "[00:00.00]第一句歌词\n[00:05.50]第二句歌词\n"
           "[00:12.00][01:20.00]重复出现的句子\n[01:30]没有小数的标签\n"
           "[02:00.000]毫秒精度\n\n[02:30.00]\n")
    e = Engine(); n = e.load_lrc(lrc)
    check(n == 7, "n==7")
    check(e.offset_ms_ == 500, "offset==500")
    e.update(0); check(e.current_ == 0); check(e.current_text() == "第一句歌词")
    e.update(4000); check(e.current_ == 0)
    e.update(5000); check(e.current_ == 1); check(e.current_text() == "第二句歌词")
    check(e.progress_ < 0.01)
    e.update(6500); check(e.current_ == 1); check(e.next_text() == "重复出现的句子")
    p = e.progress_; check(0.1 < p < 0.3, "progress 0.1~0.3")
    e.update(90000); check(e.current_ == 4)
    e.update(200000); check(e.current_ == 6); check(e.current_text() == INTERLUDE)
    e.set_offset(-600); e.update(0); check(e.current_ == -1); check(e.next_text() == "")

def t_sort():
    print("test: 乱序与进度")
    e = Engine(); check(e.load_lrc("[00:10.00]第十秒\n[00:01.00]第一秒\n[00:05.00]第五秒\n") == 3)
    e.update(0); check(e.current_ == -1)
    e.update(1000); check(e.current_ == 0); check(e.current_text() == "第一秒")
    e.update(3000); check(0.4 < e.progress_ < 0.6)
    e.update(5000); check(e.current_ == 1); check(e.current_text() == "第五秒")
    e.update(10000); check(e.current_ == 2); check(e.progress_ == 0.0)

def t_plain():
    print("test: 纯文本")
    e = Engine(); check(e.load_plain(["  第一行  ","","第二行","第三行"], 6000) == 3)
    e.update(0); check(e.current_ == 0); check(e.current_text() == "第一行")
    e.update(2500); check(e.current_ == 1); check(e.current_text() == "第二行")
    e.update(5500); check(e.current_ == 2); check(e.current_text() == "第三行")

def t_utf8():
    print("test: UTF-8 与卡拉OK")
    check(utf8_length("心底的孤独和叹息") == 8, "utf8 len 8")
    check(utf8_substr("心底的孤独和叹息", 0, 3) == "心底的", "substr 心底的")
    check(utf8_substr("abc", 1, 100) == "bc", "substr 超出取末尾")
    e = Engine(); e.load_lrc("[00:00.00]心底的孤独和叹息\n[00:04.00]能否听清\n")
    e.update(2000)
    check(e.current_ == 0)
    pr = e.progress_; check(0.4 < pr < 0.6, "progress 0.5")
    check(e.highlighted() == 4, "highlighted==4")
    check(utf8_substr(e.current_text(), 0, e.highlighted()) == "心底的孤", "sung part")

def t_api():
    print("test: 新增 API 与边界")
    e = Engine(); e.load_lrc("[00:00.00]第一句\n[00:05.00]第二句\n[00:10.00]第三句\n")
    e.update(7000)
    check(e.current_ == 1); check(e.last_line_time() == 10000); check(e.duration() == 16000)
    all_ = e.lines(); check(len(all_) == 3); check(all_[0][1] == "第一句"); check(all_[2][0] == 10000)
    check(e.lines_[e.order_[0]][1] == "第一句")
    e2 = Engine()
    check(e2.load_lrc("") == 0); check(len(e2.lines_) == 0); check(e2.current_ == -1); check(e2.duration() == 0)
    check(e2.load_lrc("没有时间轴的纯文本\n第二行") == 0)
    check(e2.load_lrc("[00:00.00]唯一一句") == 1)
    e2.update(999999); check(e2.current_ == 0); check(e2.progress_ == 1.0)
    big = "".join("[%02d:%02d.00]第%d句\n" % (i//60, i%60, i) for i in range(5000))
    check(e2.load_lrc(big) == 5000)
    e2.update(41*60*1000 + 30*1000); check(e2.current_ == 2490)

t_parse(); t_sort(); t_plain(); t_utf8(); t_api()
if fails == 0:
    print("\nPython 交叉验证：全部通过 ✓  (算法逻辑与 C++ 版本一致)")
    sys.exit(0)
else:
    print("\n%d 个断言失败 ✗" % fails)
    sys.exit(1)
