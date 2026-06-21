#pragma once
#include <string>

// 表示する挨拶文字列を組み立てる純粋関数（ハード非依存・テスト対象）。
// name が空のときは "World" を既定値として使う。
//   make_greeting("CoreS3-Lite") -> "Hello, CoreS3-Lite!"
//   make_greeting("")            -> "Hello, World!"
std::string make_greeting(const std::string& name);
