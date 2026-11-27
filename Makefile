# 基于C++的歌词滚动引擎与实现 —— 构建脚本（Windows / Linux / macOS 通用）
# 前置：任意支持 C++17 的编译器（g++ / clang++ / MSVC cl）
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INC      := -Iinclude

TARGET_DEMO := demo
TARGET_TEST := test_engine

.PHONY: all demo test clean

all: demo test

demo: src/main.cpp src/lyrics_engine.cpp include/lyrics_engine.h
	$(CXX) $(CXXFLAGS) $(INC) src/main.cpp src/lyrics_engine.cpp -o $(TARGET_DEMO)

test: tests/test_engine.cpp src/lyrics_engine.cpp include/lyrics_engine.h
	$(CXX) $(CXXFLAGS) $(INC) tests/test_engine.cpp src/lyrics_engine.cpp -o $(TARGET_TEST)
	./$(TARGET_TEST)

clean:
	-rm -f $(TARGET_DEMO) $(TARGET_TEST)
